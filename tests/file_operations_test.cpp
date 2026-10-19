// file_operations_test.cpp — the mobile file manager's copy/move/delete/zip/
// extract engine (src/ruby/android/file_operations.cpp) on the host.
//
// Index FM gets its file operations from KIO/MauiKit jobs: asynchronous,
// streaming, cancellable, with an overwrite prompt. This engine is our Qt-only
// equivalent, and it is the code that actually touches the user's disk on
// Android, so it is tested here without a device:
//
//   * copy preserves nested content, empty folders, and leaves no temp file,
//   * colliding copy asks once, honours overwrite / keep-both / skip,
//   * copying into the item's own folder duplicates instead of destroying,
//   * move prefers rename and falls back to copy+delete across filesystems,
//   * archive keeps the tree and empty folders, and round-trips through the
//     streaming extractor,
//   * a cancel mid-copy leaves neither a target nor a ".ruby-part" temp,
//   * delete removes read-only files,
//   * archive entry names cannot escape the extraction root.
//
// Qt Core + zlib only (no GL, no Widgets, no device).
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <functional>

#include "file_operations.h"
#include "platform/zip_archive.h"
#include "platform/zip_stream.h"

using ruby::android::FileOperations;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

static QByteArray read_all(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

static bool write_all(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

/// Any ".ruby-part-XXXXXX" sibling means a copy was interrupted and leaked.
static bool has_part_files(const QString& dir) {
    const QStringList entries =
        QDir(dir).entryList({ QStringLiteral("*ruby-part*") }, QDir::Files | QDir::Hidden);
    return !entries.isEmpty();
}

struct Outcome {
    bool done = false;
    int kind = -1;
    bool ok = false;
    bool cancelled = false;
    QString summary;
};

/// Runs one queued job to completion, capturing the finished() signal.
static Outcome run_job(FileOperations& ops, const std::function<bool()>& start,
                       int timeoutMs = 60000) {
    Outcome out;
    QEventLoop loop;
    const QMetaObject::Connection conn = QObject::connect(
        &ops, &FileOperations::finished, &loop,
        [&](int kind, bool ok, bool cancelled, const QString& summary,
            const QString&) {
            out = { true, kind, ok, cancelled, summary };
            loop.quit();
        });
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);

    if (!start()) {
        out.summary = QStringLiteral("start() returned false");
        return out;
    }
    loop.exec();
    QObject::disconnect(conn);
    if (!out.done) out.summary = QStringLiteral("TIMEOUT");
    if (!out.ok) {
        std::printf("  (job report: %s)\n", qPrintable(out.summary));
    }
    return out;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString root =
        QDir::tempPath() + QStringLiteral("/ruby-file-operations-test");
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    const QString src = root + "/src";
    const QString dst = root + "/dst";
    const QString dst2 = root + "/dst2";
    check(write_all(src + "/a.txt", "hello"), "fixture a.txt");
    check(write_all(src + "/sub/b.txt", "world"), "fixture b.txt");
    check(QDir().mkpath(src + "/empty"), "fixture empty dir");

    FileOperations ops;

    // Collision answers are supplied automatically so the engine's pause/resume
    // rendezvous is exercised without a UI.
    int autoDecision = FileOperations::Overwrite;
    int conflicts = 0;
    QObject::connect(&ops, &FileOperations::conflictRequested, [&]() {
        ++conflicts;
        ops.resolveConflict(autoDecision, /*applyToAll=*/true);
    });

    // ── copy tree ───────────────────────────────────────────────────────────
    Outcome r = run_job(ops, [&]() { return ops.copy({ src }, dst); });
    check(r.done && r.ok, "copy finished ok");
    check(read_all(dst + "/src/a.txt") == "hello", "copy kept a.txt content");
    check(read_all(dst + "/src/sub/b.txt") == "world", "copy kept nested content");
    check(QFileInfo(dst + "/src/empty").isDir(), "copy preserved empty dir");
    check(!has_part_files(dst), "copy left no temp part file");
    check(ops.progress() < 0.0, "progress reset after the job");

    // ── colliding copy: overwrite, asked exactly once ───────────────────────
    check(write_all(src + "/a.txt", "hello2"), "rewrite fixture");
    r = run_job(ops, [&]() { return ops.copy({ src }, dst); });
    check(r.done && r.ok, "overwrite copy finished ok");
    check(conflicts == 1, "overwrite asked once thanks to apply-to-all");
    check(read_all(dst + "/src/a.txt") == "hello2", "overwrite replaced content");

    // ── colliding copy: keep both ───────────────────────────────────────────
    check(write_all(src + "/a.txt", "hello3"), "rewrite fixture 2");
    autoDecision = FileOperations::KeepBoth;
    r = run_job(ops, [&]() { return ops.copy({ src }, dst); });
    autoDecision = FileOperations::Overwrite;
    check(r.done && r.ok, "keep-both copy finished ok");
    check(read_all(dst + "/src (1)/a.txt") == "hello3", "keep-both made src (1)/a.txt");

    // ── colliding copy: skip leaves the destination untouched ───────────────
    check(write_all(src + "/a.txt", "hello-skip"), "rewrite fixture 3");
    autoDecision = FileOperations::Skip;
    r = run_job(ops, [&]() { return ops.copy({ src + "/a.txt" }, dst + "/src"); });
    autoDecision = FileOperations::Overwrite;
    check(r.done && r.ok, "skip copy finished ok");
    check(read_all(dst + "/src/a.txt") == "hello2", "skip kept destination content");

    // ── copy into the item's own folder duplicates, never destroys ──────────
    r = run_job(ops, [&]() { return ops.copy({ dst + "/src/a.txt" }, dst + "/src"); });
    check(r.done && r.ok, "same-folder copy finished ok");
    check(read_all(dst + "/src/a.txt") == "hello2", "same-folder copy kept original");
    check(read_all(dst + "/src/a (1).txt") == "hello2", "same-folder copy made a (1).txt");

    // ── move a file (rename fast path) ──────────────────────────────────────
    r = run_job(ops, [&]() { return ops.move({ dst + "/src/a.txt" }, dst2); });
    check(r.done && r.ok, "move finished ok");
    check(!QFileInfo::exists(dst + "/src/a.txt"), "move removed source");
    check(read_all(dst2 + "/a.txt") == "hello2", "move kept content");

    // ── move across filesystems falls back to copy + delete ─────────────────
    const QString xdevSrc = QStringLiteral("/dev/shm/ruby-file-ops-xdev.txt");
    if (QFileInfo("/dev/shm").isDir()) {
        check(write_all(xdevSrc, "xdev"), "cross-device fixture");
        r = run_job(ops, [&]() { return ops.move({ xdevSrc }, dst2); });
        check(r.done && r.ok, "cross-device move finished ok");
        check(!QFileInfo::exists(xdevSrc), "cross-device move removed source");
        check(read_all(dst2 + "/ruby-file-ops-xdev.txt") == "xdev",
              "cross-device move kept content");
    }

    // ── archive keeps the tree, then round-trips through extract ────────────
    const QString zipPath = root + "/out.zip";
    r = run_job(ops, [&]() { return ops.archive({ src }, zipPath, root); });
    check(r.done && r.ok, "archive finished ok");
    std::vector<zipstream::EntryInfo> entries;
    std::string zipError;
    check(zipstream::listEntries(zipPath.toStdString(), entries, &zipError),
          "archive is a readable zip");
    bool hasA = false, hasB = false, hasEmpty = false;
    for (const auto& e : entries) {
        if (e.name == "src/a.txt") hasA = true;
        if (e.name == "src/sub/b.txt") hasB = true;
        if (e.name == "src/empty/") hasEmpty = true;
    }
    check(hasA && hasB && hasEmpty, "archive entries kept tree and empty dir");

    const QString out = root + "/extracted";
    r = run_job(ops, [&]() { return ops.extract(zipPath, out); });
    check(r.done && r.ok, "extract finished ok");
    check(read_all(out + "/src/a.txt") == "hello-skip", "extract wrote content");
    check(QFileInfo(out + "/src/empty").isDir(), "extract created empty dir");

    r = run_job(ops, [&]() { return ops.extract(zipPath, out); });
    check(r.done && r.ok, "re-extract with overwrite finished ok");
    check(read_all(out + "/src/a.txt") == "hello-skip", "re-extract replaced content");

    // ── move a whole tree ───────────────────────────────────────────────────
    const QString moved = root + "/moved";
    QDir().mkpath(moved);
    r = run_job(ops, [&]() { return ops.move({ dst }, moved); });
    check(r.done && r.ok, "move tree finished ok");
    check(!QFileInfo::exists(dst), "move tree removed source dir");
    check(read_all(moved + "/dst/src/sub/b.txt") == "world", "move tree kept content");

    // ── cancel a large copy: no target, no leaked temp ──────────────────────
    const QString big = root + "/big.bin";
    {
        QFile f(big);
        check(f.open(QIODevice::WriteOnly), "big fixture open");
        const QByteArray chunk(1024 * 1024, 'x');
        for (int i = 0; i < 512; ++i) f.write(chunk);
    }
    const QString cancelDir = root + "/cancel";
    QDir().mkpath(cancelDir);
    QTimer::singleShot(5, &ops, [&]() { ops.cancel(); });
    r = run_job(ops, [&]() { return ops.copy({ big }, cancelDir); });
    check(r.done, "cancel job finished");
    check(r.cancelled || !r.ok, "cancel reported cancelled or failed");
    check(!QFileInfo::exists(cancelDir + "/big.bin"), "cancel left no target");
    check(!has_part_files(cancelDir), "cancel cleaned the temp part file");

    // ── delete a tree holding a read-only file ──────────────────────────────
    const QString doomed = root + "/doomed";
    check(write_all(doomed + "/ro.txt", "ro"), "delete fixture");
    QFile::setPermissions(doomed + "/ro.txt", QFileDevice::ReadOwner);
    r = run_job(ops, [&]() { return ops.remove({ doomed }); });
    check(r.done && r.ok, "delete finished ok");
    check(!QFileInfo::exists(doomed), "delete removed the tree");

    // ── hostile entry names cannot escape the extraction root ───────────────
    // zip::write_archive stores names verbatim, so it is the right tool to
    // BUILD an archive whose entries point outside the destination.
    const QString evilZip = root + "/evil.zip";
    {
        zip::OutEntry leading;
        leading.name = "../../escaped.txt";
        leading.data = "pwn";
        leading.method = 0;
        zip::OutEntry embedded;
        embedded.name = "sub/../../escaped2.txt";
        embedded.data = "pwn2";
        embedded.method = 0;
        zip::OutEntry absolute;
        absolute.name = "/tmp/escaped3.txt";
        absolute.data = "pwn3";
        absolute.method = 0;
        check(zip::write_archive(evilZip.toStdString(), { leading, embedded, absolute }),
              "hostile archive written verbatim");
    }
    if (QFileInfo::exists(evilZip)) {
        const QString evilOut = root + "/evilout";
        r = run_job(ops, [&]() { return ops.extract(evilZip, evilOut); });
        check(r.done && r.ok, "hostile archive extracted safely");
        check(!QFileInfo::exists(root + "/escaped.txt"),
              "leading ../ stayed inside the destination");
        check(!QFileInfo::exists(QStringLiteral("/tmp/escaped.txt")),
              "leading ../ never reached /tmp");
        check(!QFileInfo::exists(QStringLiteral("/tmp/escaped3.txt")),
              "absolute entry never reached /tmp");
        check(QFileInfo::exists(evilOut + "/escaped.txt"),
              "leading ../ was re-rooted inside the destination");
        check(QFileInfo::exists(evilOut + "/escaped2.txt"),
              "embedded ../ was re-rooted inside the destination");
        check(QFileInfo::exists(evilOut + "/tmp/escaped3.txt"),
              "absolute entry was re-rooted inside the destination");
    }

    std::printf(failures == 0 ? "file_operations_test: ALL TESTS PASSED\n"
                              : "file_operations_test: %d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
