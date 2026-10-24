// ============================================================================
// diff_scanner_worker.cpp - Background scan of project/ against the baseline.
// ============================================================================

#include "diff_scanner_worker.h"

#include "diff_cache.h"
#include "diff_hash.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaType>

namespace ruby::diff {

namespace {

void set_fatal_error(QString* fatal_error, const QString& message) {
    if (fatal_error) {
        *fatal_error = message;
    }
}

QString relative_asset_path(const QString& assets_dir,
                            const QString& absolute_path) {
    QString relative = QDir(assets_dir).relativeFilePath(absolute_path);
    relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return VanillaManifest::normalize_path(relative);
}

QString normalize_lock_path(const QString& path) {
    if (path.isEmpty()) {
        return QString();
    }
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    while (normalized.startsWith(QStringLiteral("./"))) {
        normalized.remove(0, 2);
    }
    return normalized;
}

quint64 to_unsigned_timestamp(qint64 value) {
    return value > 0 ? static_cast<quint64>(value) : 0;
}

quint64 to_unsigned_size(qint64 value) {
    return value > 0 ? static_cast<quint64>(value) : 0;
}

} // namespace

DiffScannerWorker::DiffScannerWorker(QObject* parent)
    : QThread(parent) {
    qRegisterMetaType<DiffScanResult>("ruby::diff::DiffScanResult");
}

DiffScannerWorker::~DiffScannerWorker() {
    cancelScan();
    wait();
}

QString DiffScannerWorker::assets_dir_for_project(const QString& project_root) {
    if (project_root.isEmpty()) {
        return QString();
    }
    return QDir(project_root).filePath(QStringLiteral("project"));
}

QString DiffScannerWorker::cache_path_for_project(const QString& project_root) {
    if (project_root.isEmpty()) {
        return QString();
    }
    return QDir(project_root).filePath(QStringLiteral(".rbp/diff_cache.bin"));
}

bool DiffScannerWorker::startScan(const DiffScanRequest& request) {
    if (request.assets_dir.trimmed().isEmpty()) {
        return false;
    }

    {
        QMutexLocker locker(&m_request_mutex);
        if (isRunning()) {
            return false;
        }
        m_request = request;
        m_request_valid = true;
        m_cancel.storeRelease(0);

        // Start while the request lock is held: run() blocks on the same lock
        // until this scope ends, and a concurrent scanSync() sees isRunning()
        // true instead of racing the new thread.
        start();
    }
    return true;
}

bool DiffScannerWorker::startScan(const QString& project_root,
                                  const VanillaManifest& manifest,
                                  const QString& cache_path) {
    if (project_root.trimmed().isEmpty()) {
        return false;
    }

    DiffScanRequest request;
    request.assets_dir = assets_dir_for_project(project_root);
    request.cache_path =
        cache_path.isEmpty() ? cache_path_for_project(project_root) : cache_path;
    request.baseline = manifest.snapshot();
    return startScan(request);
}

void DiffScannerWorker::cancelScan() {
    m_cancel.storeRelease(1);
}

bool DiffScannerWorker::isCancelled() const {
    return m_cancel.loadAcquire() != 0;
}

void DiffScannerWorker::addWriteLock(const QString& path) {
    const QString key = normalize_lock_path(path);
    if (key.isEmpty()) {
        return;
    }
    QMutexLocker locker(&m_lock_mutex);
    m_write_locks.insert(key);
}

void DiffScannerWorker::removeWriteLock(const QString& path) {
    const QString key = normalize_lock_path(path);
    if (key.isEmpty()) {
        return;
    }
    QMutexLocker locker(&m_lock_mutex);
    m_write_locks.remove(key);
}

void DiffScannerWorker::clearWriteLocks() {
    QMutexLocker locker(&m_lock_mutex);
    m_write_locks.clear();
}

QStringList DiffScannerWorker::writeLocks() const {
    QMutexLocker locker(&m_lock_mutex);
    QStringList locks = m_write_locks.values();
    locks.sort();
    return locks;
}

bool DiffScannerWorker::isPathLocked(const QString& path) const {
    const QString key = normalize_lock_path(path);
    if (key.isEmpty()) {
        return false;
    }
    QMutexLocker locker(&m_lock_mutex);
    return m_write_locks.contains(key);
}

bool DiffScannerWorker::pathIsLocked(const QString& relative_path,
                                     const QString& absolute_path) const {
    const QString relative_key = normalize_lock_path(relative_path);
    const QString absolute_key = normalize_lock_path(absolute_path);

    QMutexLocker locker(&m_lock_mutex);
    if (relative_key.isEmpty() && absolute_key.isEmpty()) {
        return false;
    }
    return m_write_locks.contains(relative_key) ||
           m_write_locks.contains(absolute_key);
}

void DiffScannerWorker::appendWarning(DiffScanResult& result,
                                      const QString& message) {
    if (result.warnings.size() < kDiffMaxWarnings) {
        result.warnings.append(message);
    } else if (result.warnings.size() == kDiffMaxWarnings) {
        result.warnings.append(
            QStringLiteral("Additional scan warnings were suppressed."));
    }
}

DiffScanResult DiffScannerWorker::scanSync(const DiffScanRequest& request,
                                           QString* error_message) {
    {
        QMutexLocker locker(&m_request_mutex);
        if (isRunning()) {
            if (error_message) {
                *error_message = QStringLiteral("A diff scan is already running");
            }
            return DiffScanResult();
        }
        m_cancel.storeRelease(0);
    }
    return scanImpl(request, false, error_message);
}

DiffScanResult DiffScannerWorker::scanSync(const QString& project_root,
                                           const VanillaManifest& manifest,
                                           const QString& cache_path,
                                           QString* error_message) {
    DiffScanRequest request;
    request.assets_dir = assets_dir_for_project(project_root);
    request.cache_path =
        cache_path.isEmpty() ? cache_path_for_project(project_root) : cache_path;
    request.baseline = manifest.snapshot();
    return scanSync(request, error_message);
}

DiffScanResult DiffScannerWorker::scanImpl(const DiffScanRequest& request,
                                           bool emit_progress,
                                           QString* fatal_error) {
    DiffScanResult result;
    QElapsedTimer timer;
    timer.start();

    if (fatal_error) {
        fatal_error->clear();
    }

    const QString assets_dir = QDir::cleanPath(request.assets_dir);
    if (assets_dir.isEmpty()) {
        set_fatal_error(fatal_error,
                        QStringLiteral("No assets directory was provided"));
        return result;
    }

    const QFileInfo assets_info(assets_dir);
    if (!assets_info.exists() || !assets_info.isDir()) {
        set_fatal_error(fatal_error,
                        QStringLiteral("Assets directory does not exist: %1")
                            .arg(assets_dir));
        result.duration_ms = timer.elapsed();
        return result;
    }
    if (!assets_info.isReadable()) {
        set_fatal_error(fatal_error,
                        QStringLiteral("Assets directory is not readable: %1")
                            .arg(assets_dir));
        result.duration_ms = timer.elapsed();
        return result;
    }

    result.vanilla_base_hash = request.baseline.manifest_hash;

    int last_percent = -1;
    const auto report_progress = [this, emit_progress, &last_percent](
                                     int percent) {
        if (!emit_progress) {
            return;
        }
        const int clamped = qBound(0, percent, 100);
        if (clamped != last_percent) {
            last_percent = clamped;
            emit progress(clamped);
        }
    };

    // Collect first so progress can be measured against a stable total and the
    // entry order is deterministic across filesystems.
    QStringList files;
    QDirIterator iterator(assets_dir, QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        files.append(iterator.next());
    }
    files.sort();

    QHash<QString, DiffFileEntry> cached_entries;
    if (request.use_cache && !request.cache_path.isEmpty()) {
        QVector<DiffFileEntry> loaded;
        DiffCacheInfo cache_info;
        QString cache_error;
        const DiffCacheError cache_result =
            read_diff_cache(request.cache_path, loaded, &cache_info,
                            &cache_error);
        if (cache_result == DiffCacheError::None) {
            result.cache_loaded = true;
            cached_entries.reserve(loaded.size());
            for (const DiffFileEntry& entry : loaded) {
                cached_entries.insert(entry.rel_path, entry);
            }
        } else if (cache_result != DiffCacheError::FileNotFound) {
            appendWarning(result,
                          QStringLiteral("Diff cache ignored: %1")
                              .arg(cache_error));
        }
    }

    const int total = files.size();
    int processed = 0;

    for (const QString& absolute_path : files) {
        if (m_cancel.loadAcquire() != 0) {
            result.cancelled = true;
            break;
        }

        const QString relative_path =
            relative_asset_path(assets_dir, absolute_path);
        if (relative_path.isEmpty()) {
            ++processed;
            continue;
        }

        const QFileInfo file_info(absolute_path);
        if (!file_info.exists() || !file_info.isFile()) {
            ++result.files_skipped;
            appendWarning(result,
                          QStringLiteral("Skipped file that vanished during "
                                         "the scan: %1")
                              .arg(relative_path));
            ++processed;
            report_progress(total > 0
                                ? static_cast<int>((qint64(processed) * 100) /
                                                   total)
                                : 100);
            continue;
        }

        DiffFileEntry entry;
        entry.rel_path = relative_path;
        entry.mtime_secs =
            to_unsigned_timestamp(file_info.lastModified().toSecsSinceEpoch());
        entry.file_size = to_unsigned_size(file_info.size());

        const auto cached = cached_entries.constFind(relative_path);
        const bool locked = pathIsLocked(relative_path, absolute_path);

        if (locked) {
            // Never hash a file the editor is currently writing. Reuse the
            // last known hash when there is one, and keep the cached mtime and
            // size with it: the stat signature must describe the content that
            // produced the cached hash. Copying the current (mid-save) stat
            // would make the next scan trust the stale hash forever.
            ++result.files_locked;
            if (cached == cached_entries.constEnd()) {
                ++result.files_skipped;
                appendWarning(result,
                              QStringLiteral("Skipped locked file with no "
                                             "cached hash: %1")
                                  .arg(relative_path));
                ++processed;
                report_progress(total > 0
                                    ? static_cast<int>(
                                          (qint64(processed) * 100) / total)
                                    : 100);
                continue;
            }
            entry.mtime_secs = cached->mtime_secs;
            entry.file_size = cached->file_size;
            entry.file_hash = cached->file_hash;
        } else if (cached != cached_entries.constEnd() &&
                   cached->mtime_secs == entry.mtime_secs &&
                   cached->file_size == entry.file_size) {
            // Fast path: unchanged stat signature, no file read required.
            entry.file_hash = cached->file_hash;
            ++result.files_from_cache;
        } else {
            quint64 hash = 0;
            QString hash_error;
            if (!xxh64_file(absolute_path, hash, &hash_error)) {
                ++result.files_skipped;
                appendWarning(result,
                              QStringLiteral("Skipped unreadable file %1: %2")
                                  .arg(relative_path, hash_error));
                ++processed;
                report_progress(total > 0
                                    ? static_cast<int>(
                                          (qint64(processed) * 100) / total)
                                    : 100);
                continue;
            }
            entry.file_hash = hash;
            ++result.files_hashed;
        }

        const QString canonical =
            request.baseline.canonical_path(relative_path);
        if (canonical.isEmpty()) {
            entry.status = DiffStatus::Added;
        } else {
            if (canonical != relative_path) {
                ++result.case_mismatches;
                appendWarning(result,
                              QStringLiteral("Path casing differs from the "
                                             "vanilla asset: %1 (vanilla uses "
                                             "%2)")
                                  .arg(relative_path, canonical));
            }
            entry.status =
                request.baseline.is_vanilla_file(relative_path, entry.file_hash)
                    ? DiffStatus::Unchanged
                    : DiffStatus::Modified;
        }

        switch (entry.status) {
        case DiffStatus::Unchanged:
            ++result.unchanged_count;
            break;
        case DiffStatus::Modified:
            ++result.modified_count;
            break;
        case DiffStatus::Added:
            ++result.added_count;
            break;
        }
        result.entries.append(entry);

        ++processed;
        report_progress(total > 0
                            ? static_cast<int>((qint64(processed) * 100) / total)
                            : 100);
    }

    if (result.cancelled) {
        result.duration_ms = timer.elapsed();
        return result;
    }

    if (request.write_cache && !request.cache_path.isEmpty()) {
        if (m_cancel.loadAcquire() != 0) {
            result.cancelled = true;
            result.duration_ms = timer.elapsed();
            return result;
        }

        QString cache_error;
        const DiffCacheError cache_result = write_diff_cache(
            request.cache_path, result.entries, request.baseline.manifest_hash,
            &cache_error);
        if (cache_result == DiffCacheError::None) {
            result.cache_written = true;
        } else {
            result.cache_error = cache_error;
            appendWarning(result,
                          QStringLiteral("Diff cache was not saved: %1")
                              .arg(cache_error));
        }
    }

    report_progress(100);
    result.duration_ms = timer.elapsed();
    return result;
}

void DiffScannerWorker::run() {
    DiffScanRequest request;
    {
        QMutexLocker locker(&m_request_mutex);
        if (!m_request_valid) {
            return;
        }
        request = m_request;
        m_request_valid = false;
    }

    QString fatal_error;
    const DiffScanResult result = scanImpl(request, true, &fatal_error);
    if (!fatal_error.isEmpty()) {
        emit scanFailed(fatal_error);
        return;
    }
    emit scanFinished(result);
}

} // namespace ruby::diff
