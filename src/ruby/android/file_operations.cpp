/* file_operations.cpp — see file_operations.h for the job-model provenance. */

#include "file_operations.h"

#include "platform/zip_stream.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QRunnable>
#include <QSet>
#include <QTemporaryFile>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace ruby::android {

namespace {

constexpr qint64 kCopyChunk = 256 * 1024;

QString joinPath(const QString& dir, const QString& name) {
    if (dir.isEmpty()) return name;
    if (dir.endsWith(QLatin1Char('/'))) return dir + name;
    return dir + QLatin1Char('/') + name;
}

QString formatBytes(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    double kb = bytes / 1024.0;
    if (kb < 1024.0) return QString::number(kb, 'f', 1) + QStringLiteral(" KB");
    double mb = kb / 1024.0;
    if (mb < 1024.0) return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
    double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + QStringLiteral(" GB");
}

bool existsOrLink(const QString& path) {
    const QFileInfo fi(path);
    return fi.exists() || fi.isSymLink();
}

/// True when `child` is `parent` itself or lives underneath it. Used to reject
/// copying/moving a folder into its own subtree, which would recurse forever.
bool isSameOrInside(const QString& parent, const QString& child) {
    QString p = QDir::cleanPath(parent);
    QString c = QDir::cleanPath(child);
    if (p.isEmpty() || c.isEmpty()) return false;
    if (p == c) return true;
    if (!p.endsWith(QLatin1Char('/'))) p += QLatin1Char('/');
    return c.startsWith(p, Qt::CaseSensitive);
}

/// "file.txt" -> "file (1).txt"; "dir" -> "dir (1)".
QString uniqueTarget(const QString& dir, const QString& name) {
    const QFileInfo fi(name);
    QString base = fi.baseName();
    const QString suffix = fi.completeSuffix();
    if (base.isEmpty()) base = name;
    QString candidate = joinPath(dir, name);
    if (!existsOrLink(candidate)) return candidate;
    for (int i = 1; i < 100000; ++i) {
        const QString alt = base + QStringLiteral(" (") + QString::number(i) + QLatin1Char(')')
                            + (suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
        candidate = joinPath(dir, alt);
        if (!existsOrLink(candidate)) return candidate;
    }
    return joinPath(dir, name);
}

/// Remove a file or a whole tree, retrying read-only entries with the owner
/// write bit added. Symlinks are removed as links (never followed).
bool removeForced(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink()) return true;

    if (!fi.isDir() || fi.isSymLink()) {
        if (QFile::remove(path)) return true;
        QFile::setPermissions(path, fi.permissions() | QFileDevice::WriteOwner
                                          | QFileDevice::ReadOwner);
        return QFile::remove(path);
    }

    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    bool ok = true;
    for (const auto& e : entries) {
        if (!removeForced(e.absoluteFilePath())) ok = false;
    }
    if (!dir.rmdir(path)) {
        QFile::setPermissions(path, fi.permissions() | QFileDevice::WriteOwner
                                          | QFileDevice::ReadOwner
                                          | QFileDevice::ExeOwner);
        ok = dir.rmdir(path) && ok;
    }
    return ok;
}

/// Bytes and item count of a tree. `items` counts the root itself plus every
/// descendant (files and folders), which is what the progress bar reports.
qint64 scanTree(const QString& path, int* items) {
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink()) return 0;
    if (!fi.isDir() || fi.isSymLink()) {
        if (items) ++(*items);
        return fi.size();
    }
    if (items) ++(*items);
    qint64 total = 0;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                          | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo c = it.fileInfo();
        if (items) ++(*items);
        if (!c.isDir() || c.isSymLink()) total += c.size();
    }
    return total;
}

/// Archive-oriented scan: only regular files contribute bytes and item count,
/// so "3 of 42 files" is accurate even when the tree has many folders.
qint64 scanFiles(const QString& path, int* files) {
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink()) return 0;
    if (!fi.isDir() || fi.isSymLink()) {
        if (files) ++(*files);
        return fi.size();
    }
    qint64 total = 0;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                          | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo c = it.fileInfo();
        if (!c.isDir() || c.isSymLink()) {
            if (files) ++(*files);
            total += c.size();
        }
    }
    return total;
}

uint32_t zipModeFor(const QFileInfo& fi) {
    uint32_t mode = fi.isDir() ? 0040000u : 0100000u;
    const QFileDevice::Permissions p = fi.permissions();
    if (p & QFileDevice::ReadOwner)    mode |= 0400u;
    if (p & QFileDevice::WriteOwner)   mode |= 0200u;
    if (p & QFileDevice::ExeOwner)     mode |= 0100u;
    if (p & QFileDevice::ReadGroup)    mode |= 0040u;
    if (p & QFileDevice::WriteGroup)   mode |= 0020u;
    if (p & QFileDevice::ExeGroup)     mode |= 0010u;
    if (p & QFileDevice::ReadOther)    mode |= 0004u;
    if (p & QFileDevice::WriteOther)   mode |= 0002u;
    if (p & QFileDevice::ExeOther)     mode |= 0001u;
    if (fi.isDir()) mode |= 0111u;
    return mode << 16;
}

/// Already-compressed payloads are stored, not deflated. This mirrors what
/// Ark/KIO do for media containers and keeps a 1 GB zip of APKs from pinning
/// a core for a minute for <2% gain.
int zipMethodFor(const QString& path) {
    static const QSet<QString> stored = {
        QStringLiteral("zip"), QStringLiteral("apk"), QStringLiteral("jar"),
        QStringLiteral("gz"), QStringLiteral("xz"), QStringLiteral("bz2"),
        QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("zst"),
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("heic"),
        QStringLiteral("mp3"), QStringLiteral("ogg"), QStringLiteral("opus"),
        QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("flac"),
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("3gp")
    };
    return stored.contains(QFileInfo(path).suffix().toLower()) ? 0 : 8;
}

QString kindTitle(FileOperations::JobKind kind) {
    switch (kind) {
    case FileOperations::CopyJob:    return QStringLiteral("Copying");
    case FileOperations::MoveJob:    return QStringLiteral("Moving");
    case FileOperations::DeleteJob:  return QStringLiteral("Deleting");
    case FileOperations::ArchiveJob: return QStringLiteral("Compressing");
    case FileOperations::ExtractJob: return QStringLiteral("Extracting");
    }
    return QStringLiteral("Working");
}

/// QThreadPool entry point. QThreadPool has no std::function overload on the
/// Qt 6.6 Android build, so wrap the callable in a one-shot QRunnable.
class FunctionRunnable : public QRunnable {
public:
    explicit FunctionRunnable(std::function<void()> fn) : m_fn(std::move(fn)) {
        setAutoDelete(true);
    }
    void run() override { m_fn(); }

private:
    std::function<void()> m_fn;
};

} // namespace

FileOperations::FileOperations(QObject* parent)
    : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
    m_progressClock.start();
}

FileOperations::~FileOperations() {
    m_cancel.store(true);
    {
        QMutexLocker locker(&m_conflictMutex);
        m_conflictWait.wakeAll();
    }
    m_pool.waitForDone(5000);
}

// ─── Queueing ───────────────────────────────────────────────────────────────

bool FileOperations::copy(const QStringList& sources, const QString& destDir) {
    if (sources.isEmpty() || destDir.isEmpty()) return false;
    Job job;
    job.kind = CopyJob;
    job.sources = sources;
    job.destDir = destDir;
    enqueue(job);
    return true;
}

bool FileOperations::move(const QStringList& sources, const QString& destDir) {
    if (sources.isEmpty() || destDir.isEmpty()) return false;
    Job job;
    job.kind = MoveJob;
    job.sources = sources;
    job.destDir = destDir;
    enqueue(job);
    return true;
}

bool FileOperations::remove(const QStringList& paths) {
    if (paths.isEmpty()) return false;
    Job job;
    job.kind = DeleteJob;
    job.sources = paths;
    enqueue(job);
    return true;
}

bool FileOperations::archive(const QStringList& sources, const QString& outZipPath,
                             const QString& baseDir) {
    if (sources.isEmpty()) return false;
    Job job;
    job.kind = ArchiveJob;
    job.sources = sources;
    job.outZipPath = outZipPath;
    job.destDir = baseDir;
    enqueue(job);
    return true;
}

bool FileOperations::extract(const QString& archivePath, const QString& destDir) {
    if (archivePath.isEmpty() || destDir.isEmpty()) return false;
    Job job;
    job.kind = ExtractJob;
    job.sources = { archivePath };
    job.destDir = destDir;
    enqueue(job);
    return true;
}

void FileOperations::enqueue(const Job& job) {
    m_queue.append(job);
    if (!m_busy.load()) startNext();
}

void FileOperations::startNext() {
    if (m_busy.load() || m_queue.isEmpty()) return;

    const Job job = m_queue.takeFirst();
    m_cancel.store(false);
    m_policy.store(-1);
    m_activeKind = int(job.kind);

    m_workDone = 0;
    m_workTotal = 0;
    m_workItemsDone = 0;
    m_workItemsTotal = 0;
    m_workFailures = 0;
    m_failedNames.clear();
    m_lastPost = -1000;

    m_title = kindTitle(job.kind);
    m_detail.clear();
    m_progress = -1.0;
    m_bytesText.clear();
    m_itemsText.clear();

    m_busy.store(true);
    emit stateChanged();

    m_pool.start(new FunctionRunnable([this, job]() { runJob(job); }));
}

void FileOperations::runJob(const Job& job) {
    QString summary;
    bool ok = false;

    switch (job.kind) {
    case CopyJob:    ok = runCopyOrMove(job, false, &summary); break;
    case MoveJob:    ok = runCopyOrMove(job, true, &summary); break;
    case DeleteJob:  ok = runDelete(job, &summary); break;
    case ArchiveJob: ok = runArchive(job, &summary); break;
    case ExtractJob: ok = runExtract(job, &summary); break;
    }

    const bool wasCancelled = cancelled();
    if (wasCancelled) {
        ok = false;
        summary = QStringLiteral("Cancelled");
    } else {
        if (summary.isEmpty()) {
            summary = ok ? QStringLiteral("Done") : QStringLiteral("Failed");
        }
        if (m_workFailures > 0) {
            summary += QStringLiteral(" · %1 failed").arg(m_workFailures);
        }
    }

    QMetaObject::invokeMethod(this, [this, job, ok, wasCancelled, summary]() {
        m_busy.store(false);
        m_progress = -1.0;
        m_detail.clear();
        m_bytesText.clear();
        m_itemsText.clear();
        m_conflictActive = false;
        m_conflictSource.clear();
        m_conflictTarget.clear();
        m_policy.store(-1);
        emit stateChanged();
        emit finished(int(job.kind), ok, wasCancelled, summary, job.destDir);
        startNext();
    }, Qt::QueuedConnection);
}

// ─── Progress / conflict plumbing ───────────────────────────────────────────

void FileOperations::reportProgress(qint64 done, qint64 total, int itemsDone,
                                    int itemsTotal, const QString& detail) {
    const qint64 now = m_progressClock.elapsed();
    const bool final = (total > 0 && done >= total)
                       || (itemsTotal > 0 && itemsDone >= itemsTotal);
    if (!final && now - m_lastPost < 40) return;
    m_lastPost = now;

    QMetaObject::invokeMethod(this, [this, done, total, itemsDone, itemsTotal, detail]() {
        if (!m_busy.load()) return;
        if (total > 0) {
            m_progress = qBound<qreal>(0.0, qreal(done) / qreal(total), 1.0);
            m_bytesText = formatBytes(done) + QStringLiteral(" / ") + formatBytes(total);
        } else if (itemsTotal > 0) {
            m_progress = qBound<qreal>(0.0, qreal(itemsDone) / qreal(itemsTotal), 1.0);
            m_bytesText.clear();
        } else {
            m_progress = -1.0;
            m_bytesText.clear();
        }
        m_itemsText = itemsTotal > 0
            ? QString::number(itemsDone) + QStringLiteral(" / ") + QString::number(itemsTotal)
            : QString();
        if (!detail.isEmpty()) m_detail = detail;
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void FileOperations::tick(qint64 bytes, const QString& detail) {
    m_workDone += bytes;
    reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal, detail);
}

void FileOperations::tickItem(const QString& detail) {
    ++m_workItemsDone;
    reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal, detail);
}

void FileOperations::recordFailure(const QString& path) {
    ++m_workFailures;
    if (m_failedNames.size() < 50) m_failedNames.append(path);
}

int FileOperations::askConflict(const QString& source, const QString& target,
                                bool isDir) {
    const int policy = m_policy.load();
    if (policy >= 0) return policy;

    QMutexLocker locker(&m_conflictMutex);
    if (m_cancel.load()) return CancelOperation;

    const int id = ++m_nextConflictId;
    m_waitingConflictId = id;
    m_conflictResponse = -1;

    QMetaObject::invokeMethod(this, [this, source, target, isDir]() {
        if (!m_busy.load()) return;
        m_conflictActive = true;
        m_conflictSource = source;
        m_conflictTarget = target;
        m_conflictIsDir = isDir;
        emit stateChanged();
        emit conflictRequested();
    }, Qt::QueuedConnection);

    while (m_conflictResponse < 0 && !m_cancel.load()) {
        m_conflictWait.wait(&m_conflictMutex);
    }

    int decision = m_conflictResponse;
    m_conflictResponse = -1;
    m_waitingConflictId = 0;
    locker.unlock();

    QMetaObject::invokeMethod(this, [this]() {
        if (!m_conflictActive) return;
        m_conflictActive = false;
        m_conflictSource.clear();
        m_conflictTarget.clear();
        emit stateChanged();
    }, Qt::QueuedConnection);

    if (m_cancel.load()) return CancelOperation;
    if (decision < Overwrite || decision > CancelOperation) return CancelOperation;
    return decision;
}

void FileOperations::resolveConflict(int decision, bool applyToAll) {
    if (decision < Overwrite || decision > CancelOperation) return;
    if (applyToAll) m_policy.store(decision);

    QMutexLocker locker(&m_conflictMutex);
    if (m_waitingConflictId == 0) return;
    m_conflictResponse = decision;
    m_conflictWait.wakeAll();
}

void FileOperations::cancel() {
    m_cancel.store(true);
    QMutexLocker locker(&m_conflictMutex);
    m_conflictWait.wakeAll();
}

// ─── Copy / move ────────────────────────────────────────────────────────────

bool FileOperations::makeDirectory(const QString& path) {
    const QFileInfo fi(path);
    if (fi.isDir()) return true;
    return QDir().mkpath(path);
}

bool FileOperations::copyOne(const QString& source, const QString& target,
                             const QString& detail) {
    if (cancelled()) return false;

    const QFileInfo sfi(source);
    if (QDir::cleanPath(source) == QDir::cleanPath(target)) return true;

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        recordFailure(source);
        tick(sfi.size(), detail);
        ++m_workItemsDone;
        reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal, detail);
        return false;
    }
    const QFileDevice::Permissions sourcePerms = in.permissions();

    // Write a sibling temp file and rename it over the target. A failure or a
    // cancel can never expose a truncated file, and overwriting keeps the old
    // file intact until the new copy is complete.
    QTemporaryFile tmp(target + QStringLiteral(".ruby-part-XXXXXX"));
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        in.close();
        recordFailure(source);
        tick(sfi.size(), detail);
        ++m_workItemsDone;
        reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal, detail);
        return false;
    }
    const QString tempName = tmp.fileName();
    const qint64 sourceSize = sfi.size();

    bool ok = true;
    qint64 copied = 0;
    QByteArray buffer;
    buffer.resize(int(kCopyChunk));

    while (!in.atEnd()) {
        if (cancelled()) { ok = false; break; }
        const qint64 got = in.read(buffer.data(), buffer.size());
        if (got < 0) { ok = false; break; }
        if (got == 0) break;
        if (tmp.write(buffer.constData(), got) != got) { ok = false; break; }
        copied += got;
        tick(got, detail);
    }

    tmp.flush();
    tmp.close();
    in.close();

    if (!ok) {
        QFile::remove(tempName);
        if (!cancelled()) recordFailure(source);
        tick(qMax<qint64>(0, sourceSize - copied), detail);
        ++m_workItemsDone;
        reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal, detail);
        return false;
    }

    QFile::setPermissions(tempName, sourcePerms);
    {
        QFile meta(tempName);
        if (meta.open(QIODevice::ReadWrite)) {
            meta.setFileTime(sfi.lastModified(), QFileDevice::FileModificationTime);
            meta.close();
        }
    }

    bool placed = false;
    if (!existsOrLink(target)) {
        placed = QFile::rename(tempName, target);
    }
    if (!placed) {
        // Overwrite: remove the old target first, then rename. POSIX rename
        // replaces atomically, but Qt's wrapper refuses an existing target.
        if (existsOrLink(target)) removeForced(target);
        placed = QFile::rename(tempName, target);
    }
    if (!placed) {
        QFile::remove(tempName);
        recordFailure(source);
    }

    ++m_workItemsDone;
    reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal, detail);
    return placed;
}

bool FileOperations::copyTree(const QString& sourceDir, const QString& targetDir) {
    if (cancelled()) return false;

    if (!makeDirectory(targetDir)) {
        recordFailure(targetDir);
        int items = 0;
        const qint64 bytes = scanTree(sourceDir, &items);
        m_workDone += bytes;
        m_workItemsDone += items;
        reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                       QFileInfo(sourceDir).fileName());
        return false;
    }

    const QFileInfo sfi(sourceDir);
    tickItem(sfi.fileName());

    QDir dir(sourceDir);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);

    bool ok = true;
    for (const auto& entry : entries) {
        if (cancelled()) return false;

        const QString src = entry.absoluteFilePath();
        const QString dst = joinPath(targetDir, entry.fileName());

        if (entry.isDir() && !entry.isSymLink()) {
            if (!copyTree(src, dst)) ok = false;
            continue;
        }

        QString finalTarget = dst;
        if (existsOrLink(dst)) {
            const int decision = askConflict(src, dst, false);
            if (decision == CancelOperation) {
                m_cancel.store(true);
                return false;
            }
            if (decision == Skip) {
                tick(entry.size(), entry.fileName());
                ++m_workItemsDone;
                reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                               m_workItemsTotal, entry.fileName());
                continue;
            }
            if (decision == KeepBoth) {
                finalTarget = uniqueTarget(targetDir, entry.fileName());
            } else if (!removeForced(finalTarget)) {
                recordFailure(finalTarget);
                tick(entry.size(), entry.fileName());
                ++m_workItemsDone;
                reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                               m_workItemsTotal, entry.fileName());
                ok = false;
                continue;
            }
        }

        if (!copyOne(src, finalTarget, entry.fileName())) ok = false;
    }

    // Restore the source folder's mode and mtime after its children exist, so
    // a read-only source folder still receives its contents.
    QFile::setPermissions(targetDir, QFile::permissions(sourceDir));
    {
        QFile meta(targetDir);
        if (meta.open(QIODevice::ReadOnly)) {
            meta.setFileTime(sfi.lastModified(), QFileDevice::FileModificationTime);
            meta.close();
        }
    }
    return ok;
}

bool FileOperations::runCopyOrMove(const Job& job, bool move, QString* summary) {
    const QString destDir = QDir::cleanPath(job.destDir);
    if (!QFileInfo(destDir).isDir() && !QDir().mkpath(destDir)) {
        *summary = QStringLiteral("Destination folder is not accessible");
        return false;
    }

    // Planning pass: the progress bar is determinate from the first frame.
    for (const QString& src : job.sources) {
        int items = 0;
        m_workTotal += scanTree(src, &items);
        m_workItemsTotal += items;
    }
    reportProgress(0, m_workTotal, 0, m_workItemsTotal, QString());

    bool allOk = true;
    for (const QString& srcPath : job.sources) {
        if (cancelled()) break;

        const QFileInfo sfi(srcPath);
        if (!sfi.exists() && !sfi.isSymLink()) {
            recordFailure(srcPath);
            allOk = false;
            continue;
        }

        const bool isDir = sfi.isDir() && !sfi.isSymLink();
        const QString name = sfi.fileName();
        if (name.isEmpty()) {
            recordFailure(srcPath);
            allOk = false;
            continue;
        }

        if (isDir && isSameOrInside(srcPath, destDir)) {
            // "Copy Folder into Folder/Folder" would recurse forever.
            recordFailure(srcPath);
            int items = 0;
            const qint64 bytes = scanTree(srcPath, &items);
            m_workDone += bytes;
            m_workItemsDone += items;
            reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                           name);
            allOk = false;
            continue;
        }

        QString target = joinPath(destDir, name);

        // Pasting into the folder the item already lives in:
        //  - copy -> create a "name (1)" duplicate instead of destroying the source
        //  - move -> it is already there; just account for it and move on
        if (QDir::cleanPath(target) == QDir::cleanPath(srcPath)) {
            if (move) {
                int items = 0;
                m_workDone += scanTree(srcPath, &items);
                m_workItemsDone += items;
                reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                               m_workItemsTotal, name);
                continue;
            }
            target = uniqueTarget(destDir, name);
        }

        if (existsOrLink(target)) {
            const int decision = askConflict(srcPath, target, isDir);
            if (decision == CancelOperation) {
                m_cancel.store(true);
                break;
            }
            if (decision == Skip) {
                int items = 0;
                const qint64 bytes = scanTree(srcPath, &items);
                m_workDone += bytes;
                m_workItemsDone += items;
                reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                               m_workItemsTotal, name);
                continue;
            }
            if (decision == KeepBoth) {
                target = uniqueTarget(destDir, name);
            } else if (!isDir) {
                if (!removeForced(target)) {
                    recordFailure(target);
                    int items = 0;
                    const qint64 bytes = scanTree(srcPath, &items);
                    m_workDone += bytes;
                    m_workItemsDone += items;
                    reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                                   m_workItemsTotal, name);
                    allOk = false;
                    continue;
                }
            }
            // Folder + Overwrite = merge, never delete the destination tree.
        }

        bool ok = false;
        if (move) {
            // rename() is atomic and instant, but only when the target does not
            // exist (directory merges and overwrites need the copy path).
            if (!existsOrLink(target)) {
                ok = QFile::rename(srcPath, target);
            }
            if (ok) {
                int items = 0;
                m_workDone += scanTree(target, &items);
                m_workItemsDone += items;
                reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                               m_workItemsTotal, name);
            } else {
                ok = isDir ? copyTree(srcPath, target) : copyOne(srcPath, target, name);
                if (ok && !cancelled()) {
                    if (!removeForced(srcPath)) {
                        recordFailure(srcPath);
                        ok = false;
                    }
                }
            }
        } else {
            ok = isDir ? copyTree(srcPath, target) : copyOne(srcPath, target, name);
        }

        if (!ok) allOk = false;
    }

    if (cancelled()) {
        *summary = QStringLiteral("Cancelled");
        return false;
    }
    *summary = (move ? QStringLiteral("Moved %1 item(s)")
                     : QStringLiteral("Copied %1 item(s)"))
                   .arg(m_workItemsDone);
    return allOk && m_workFailures == 0;
}

// ─── Delete ─────────────────────────────────────────────────────────────────

bool FileOperations::deleteTree(const QString& path) {
    if (cancelled()) return false;

    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink()) return true;

    if (!fi.isDir() || fi.isSymLink()) {
        bool ok = QFile::remove(path);
        if (!ok) {
            QFile::setPermissions(path, fi.permissions() | QFileDevice::WriteOwner
                                              | QFileDevice::ReadOwner);
            ok = QFile::remove(path);
        }
        if (!ok) recordFailure(path);
        tick(fi.size(), fi.fileName());
        ++m_workItemsDone;
        reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                       fi.fileName());
        return ok;
    }

    QDir dir(path);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    bool ok = true;
    for (const auto& entry : entries) {
        if (cancelled()) return false;
        if (!deleteTree(entry.absoluteFilePath())) ok = false;
    }

    if (!dir.rmdir(path)) {
        QFile::setPermissions(path, fi.permissions() | QFileDevice::WriteOwner
                                          | QFileDevice::ReadOwner
                                          | QFileDevice::ExeOwner);
        ok = dir.rmdir(path) && ok;
    }
    if (!ok) recordFailure(path);
    ++m_workItemsDone;
    reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                   fi.fileName());
    return ok;
}

bool FileOperations::runDelete(const Job& job, QString* summary) {
    for (const QString& path : job.sources) {
        int items = 0;
        m_workTotal += scanTree(path, &items);
        m_workItemsTotal += items;
    }
    reportProgress(0, m_workTotal, 0, m_workItemsTotal, QString());

    bool allOk = true;
    for (const QString& path : job.sources) {
        if (cancelled()) break;
        if (!deleteTree(path)) allOk = false;
    }

    if (cancelled()) {
        *summary = QStringLiteral("Cancelled");
        return false;
    }
    *summary = QStringLiteral("Deleted %1 item(s)").arg(m_workItemsDone);
    return allOk && m_workFailures == 0;
}

// ─── Archive ────────────────────────────────────────────────────────────────

bool FileOperations::archiveWalk(zipstream::Writer& writer, const QString& current,
                                 const QString& prefix, const QString& outZip) {
    if (cancelled()) return false;

    QDir dir(current);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);

    bool ok = true;
    for (const auto& entry : entries) {
        if (cancelled()) return false;

        const QString path = entry.absoluteFilePath();
        const QString rel = prefix + QLatin1Char('/') + entry.fileName();

        if (entry.isDir() && !entry.isSymLink()) {
            std::string error;
            if (!writer.addDirectory(rel.toStdString(), zipModeFor(entry),
                                     [this]() { return cancelled(); }, &error)) {
                if (!cancelled()) recordFailure(path);
                ok = false;
                continue;
            }
            if (!archiveWalk(writer, path, rel, outZip)) ok = false;
            continue;
        }

        if (QDir::cleanPath(path) == QDir::cleanPath(outZip)) {
            // Never fold the archive into itself.
            m_workDone += entry.size();
            ++m_workItemsDone;
            reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                           m_workItemsTotal, entry.fileName());
            continue;
        }

        const qint64 base = m_workDone;
        const QString detail = entry.fileName();
        auto progress = [this, base, detail](uint64_t processed, uint64_t, const std::string&) {
            reportProgress(base + qint64(processed), m_workTotal, m_workItemsDone,
                           m_workItemsTotal, detail);
        };

        std::string error;
        const bool added = writer.addFile(
            rel.toStdString(), path.toStdString(), zipMethodFor(path),
            zipModeFor(entry), [this]() { return cancelled(); }, progress, &error);

        m_workDone += entry.size();
        ++m_workItemsDone;
        reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                       detail);
        if (!added) {
            if (!cancelled()) recordFailure(path);
            ok = false;
        }
    }
    return ok;
}

bool FileOperations::runArchive(const Job& job, QString* summary) {
    QString dest = job.outZipPath;
    if (dest.isEmpty()) {
        if (job.sources.size() == 1) {
            const QFileInfo sfi(job.sources.first());
            dest = joinPath(sfi.absolutePath(),
                            (sfi.isDir() ? sfi.fileName() : sfi.completeBaseName())
                                + QStringLiteral(".zip"));
        } else {
            const QString base = job.destDir.isEmpty() ? QDir::currentPath()
                                                       : job.destDir;
            dest = joinPath(base, QStringLiteral("Archive_%1.zip").arg(
                               QDateTime::currentDateTime().toString(
                                   QStringLiteral("yyyyMMdd_hhmmss"))));
        }
    }

    dest = QDir::cleanPath(dest);
    const QString parentDir = QFileInfo(dest).absolutePath();
    if (!QDir().mkpath(parentDir)) {
        *summary = QStringLiteral("Cannot create the archive folder");
        return false;
    }
    if (existsOrLink(dest)) dest = uniqueTarget(parentDir, QFileInfo(dest).fileName());

    for (const QString& src : job.sources) {
        int files = 0;
        m_workTotal += scanFiles(src, &files);
        m_workItemsTotal += files;
    }
    reportProgress(0, m_workTotal, 0, m_workItemsTotal, QString());

    zipstream::Writer writer(dest.toStdString());
    std::string error;
    if (!writer.open(&error)) {
        *summary = QStringLiteral("Cannot create archive: ")
                   + QString::fromStdString(error);
        return false;
    }

    bool ok = true;
    QSet<QString> usedPrefixes;
    for (const QString& srcPath : job.sources) {
        if (cancelled()) break;

        const QFileInfo sfi(srcPath);
        if (!sfi.exists() && !sfi.isSymLink()) {
            recordFailure(srcPath);
            ok = false;
            continue;
        }

        // Two selected files can share a name (from different folders); give
        // the second a unique archive root instead of writing a duplicate
        // entry that extractors resolve ambiguously.
        QString prefix = sfi.fileName();
        if (usedPrefixes.contains(prefix)) {
            for (int i = 1; i < 100000; ++i) {
                const QString alt = prefix + QStringLiteral(" (") + QString::number(i)
                                    + QLatin1Char(')');
                if (!usedPrefixes.contains(alt)) { prefix = alt; break; }
            }
        }
        usedPrefixes.insert(prefix);
        if (sfi.isDir() && !sfi.isSymLink()) {
            std::string err;
            if (!writer.addDirectory(prefix.toStdString() + "/", zipModeFor(sfi),
                                     [this]() { return cancelled(); }, &err)) {
                if (cancelled()) break;
                recordFailure(srcPath);
                ok = false;
                continue;
            }
            if (!archiveWalk(writer, srcPath, prefix, dest)) ok = false;
        } else {
            if (QDir::cleanPath(srcPath) == QDir::cleanPath(dest)) continue;
            const qint64 base = m_workDone;
            const QString detail = sfi.fileName();
            auto progress = [this, base, detail](uint64_t processed, uint64_t,
                                                 const std::string&) {
                reportProgress(base + qint64(processed), m_workTotal,
                               m_workItemsDone, m_workItemsTotal, detail);
            };
            std::string err;
            const bool added = writer.addFile(
                prefix.toStdString(), srcPath.toStdString(), zipMethodFor(srcPath),
                zipModeFor(sfi), [this]() { return cancelled(); }, progress, &err);
            m_workDone += sfi.size();
            ++m_workItemsDone;
            reportProgress(m_workDone, m_workTotal, m_workItemsDone,
                           m_workItemsTotal, detail);
            if (!added) {
                if (!cancelled()) recordFailure(srcPath);
                ok = false;
            }
        }
    }

    if (cancelled()) {
        QFile::remove(dest);
        *summary = QStringLiteral("Cancelled");
        return false;
    }

    if (!writer.close(&error)) {
        QFile::remove(dest);
        *summary = QStringLiteral("Cannot finish archive: ")
                   + QString::fromStdString(error);
        return false;
    }

    // Force the final frame even when every file was stored (0-byte entries).
    m_workDone = m_workTotal;
    m_workItemsDone = m_workItemsTotal;
    reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                   QFileInfo(dest).fileName());

    *summary = QStringLiteral("Created %1 (%2 file(s))")
                   .arg(QFileInfo(dest).fileName())
                   .arg(m_workItemsTotal);
    return ok && m_workFailures == 0;
}

// ─── Extract ────────────────────────────────────────────────────────────────

bool FileOperations::runExtract(const Job& job, QString* summary) {
    const QString archivePath = job.sources.value(0);
    const QString destDir = job.destDir;

    std::vector<zipstream::EntryInfo> entries;
    std::string error;
    if (!zipstream::listEntries(archivePath.toStdString(), entries, &error)) {
        *summary = QStringLiteral("Not a ZIP archive: ") + QString::fromStdString(error);
        return false;
    }

    for (const auto& e : entries) {
        if (!e.name.empty() && e.name.back() != '/') {
            m_workTotal += qint64(e.uncompSize);
            ++m_workItemsTotal;
        }
    }
    reportProgress(0, m_workTotal, 0, m_workItemsTotal, QString());

    QString lastName;
    auto progress = [this, &lastName](uint64_t done, uint64_t, const std::string& name) {
        const QString current = QString::fromStdString(name);
        if (current != lastName) {
            lastName = current;
            if (m_workItemsDone < m_workItemsTotal) ++m_workItemsDone;
        }
        reportProgress(qint64(done), m_workTotal, m_workItemsDone,
                       m_workItemsTotal, current);
    };

    auto cancelFn = [this]() { return cancelled(); };

    auto conflictFn = [this](const std::string& entryName,
                             const std::string& targetPath,
                             bool isDir) -> zipstream::ConflictDecision {
        const int decision = askConflict(QString::fromStdString(entryName),
                                         QString::fromStdString(targetPath), isDir);
        switch (decision) {
        case Overwrite:       return zipstream::ConflictDecision::Overwrite;
        case Skip:            return zipstream::ConflictDecision::Skip;
        case KeepBoth:        return zipstream::ConflictDecision::KeepBoth;
        default:              return zipstream::ConflictDecision::Cancel;
        }
    };

    const zipstream::ExtractResult result = zipstream::extractAll(
        archivePath.toStdString(), destDir.toStdString(), true, progress, cancelFn,
        conflictFn);

    if (result.cancelled) {
        *summary = QStringLiteral("Cancelled");
        return false;
    }
    if (!result.ok) {
        *summary = QStringLiteral("Extraction failed: ")
                   + QString::fromStdString(result.error);
        return false;
    }

    m_workDone = m_workTotal;
    m_workItemsDone = m_workItemsTotal;
    reportProgress(m_workDone, m_workTotal, m_workItemsDone, m_workItemsTotal,
                   QFileInfo(destDir).fileName());

    *summary = QStringLiteral("Extracted %1 file(s)").arg(result.extracted);
    if (result.skipped > 0) {
        *summary += QStringLiteral(" · %1 skipped").arg(result.skipped);
    }
    if (!result.failed.empty()) {
        m_workFailures += int(result.failed.size());
    }
    return true;
}

} // namespace ruby::android
