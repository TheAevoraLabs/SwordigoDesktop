#pragma once
// ============================================================================
// diff_scanner_worker.h - Background asset diff scanner.
//
// The worker walks project/ on its own QThread, hashes each file with xxHash64,
// and compares the hash against the vanilla baseline. Results are written to
// <project>/.rbp/diff_cache.bin and delivered through scanFinished().
//
// Two-tier check (spec section 6.1): when the cached mtime and size match the
// file on disk, the cached hash is reused without reading the file. When they
// differ, the file is hashed again, so a USB/ADB/Git timestamp change cannot
// mark a clean file as modified.
//
// Threading contract:
//   - startScan() and cancelScan() may be called from any thread.
//   - addWriteLock()/removeWriteLock() are thread-safe; the editor uses them
//     while saving a file so the scanner does not hash a half-written asset.
//   - progress(), scanFinished() and scanFailed() are emitted from the worker
//     thread. Connect them with the default (auto) connection to run UI code on
//     the receiving object's thread.
//   - scanSync() is the blocking variant for tests and for callers that
//     already run off the UI thread.
// ============================================================================

#include "diff_types.h"
#include "vanilla_manifest.h"

#include <QAtomicInt>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>

namespace ruby::diff {

// Everything one scan needs. Paths are absolute. An empty cache_path disables
// cache reads and writes for this scan.
struct DiffScanRequest {
    QString assets_dir;
    QString cache_path;
    VanillaSnapshot baseline;
    bool use_cache = true;
    bool write_cache = true;
};

class DiffScannerWorker : public QThread {
    Q_OBJECT

public:
    explicit DiffScannerWorker(QObject* parent = nullptr);
    ~DiffScannerWorker() override;

    // Directory helpers for the standard layout:
    //   <project_root>/project              active assets
    //   <project_root>/.rbp/diff_cache.bin cache
    static QString assets_dir_for_project(const QString& project_root);
    static QString cache_path_for_project(const QString& project_root);

    // Starts a scan. Returns false when a scan is already running or the
    // request has no assets directory. A missing directory is reported through
    // scanFailed() so the UI has a single error path.
    bool startScan(const DiffScanRequest& request);

    // Convenience overload for the standard layout. An empty cache_path means
    // cache_path_for_project(project_root); the manifest is snapshotted, so the
    // caller may reload it while the scan runs.
    bool startScan(const QString& project_root, const VanillaManifest& manifest,
                   const QString& cache_path = QString());

    // Requests cancellation. The scan stops between files, skips the cache
    // write, and still emits scanFinished() with cancelled = true.
    void cancelScan();
    bool isCancelled() const;

    // Blocking scan. Must not be called while this worker's thread is running.
    // `error_message` receives a message only for fatal errors (missing or
    // unreadable assets directory).
    DiffScanResult scanSync(const DiffScanRequest& request,
                            QString* error_message = nullptr);
    DiffScanResult scanSync(const QString& project_root,
                            const VanillaManifest& manifest,
                            const QString& cache_path = QString(),
                            QString* error_message = nullptr);

    // Editor write locks (spec section 6.2). Paths may be relative to
    // assets_dir or absolute; separators and "." components are normalized.
    void addWriteLock(const QString& path);
    void removeWriteLock(const QString& path);
    void clearWriteLocks();
    QStringList writeLocks() const;

    // Exact lookup against the normalized lock keys. A scan checks both the
    // path relative to assets_dir and the absolute path, so either form of
    // lock protects a file.
    bool isPathLocked(const QString& path) const;

signals:
    void progress(int percent);
    void scanFinished(const ruby::diff::DiffScanResult& result);
    void scanFailed(const QString& error);

protected:
    void run() override;

private:
    DiffScanResult scanImpl(const DiffScanRequest& request, bool emit_progress,
                            QString* fatal_error);
    bool pathIsLocked(const QString& relative_path,
                      const QString& absolute_path) const;
    static void appendWarning(DiffScanResult& result, const QString& message);

    mutable QMutex m_request_mutex;
    DiffScanRequest m_request;
    bool m_request_valid = false;

    mutable QMutex m_lock_mutex;
    QSet<QString> m_write_locks;

    QAtomicInt m_cancel{0};
};

} // namespace ruby::diff
