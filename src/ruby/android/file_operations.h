#pragma once
/* file_operations.h — asynchronous, cancellable file manager engine.
 *
 * Model
 * -----
 * Index FM (org.kde.index) delegates every file action to MauiKit
 * FileBrowsing / Archiver, which are asynchronous KIO jobs: they run off the
 * GUI thread, stream data, expose percent + processedAmount, can be killed,
 * and ask the user before overwriting a target. This file is a native Qt/Android
 * implementation of exactly that job model, because KIO is not available here.
 *
 * Every public operation only enqueues a job and returns immediately; the GUI
 * thread never walks a directory tree, never copies a byte and never blocks on
 * a stat().  One worker thread runs jobs in order, which keeps disk throughput
 * high without letting two destructive operations interleave.
 *
 * Guarantees
 * ----------
 *  - Copy/move writes through a sibling temporary file and renames it into
 *    place, so a cancelled or failed copy never leaves a half-written file
 *    (and never destroys the original while overwriting).
 *  - Directory copies merge, preserve empty folders, permissions and mtimes.
 *  - Move falls back to copy+delete across filesystems (rename returns EXDEV).
 *  - Delete retries read-only files after adding the write bit.
 *  - Compression streams through zipstream::Writer; extraction through
 *    zipstream::extractAll, both in 256 KiB chunks with progress/cancel.
 *  - Conflicts (existing target) pause the worker and emit conflictRequested();
 *    resolveConflict() resumes it with overwrite / skip / keep-both / cancel.
 *    "apply to all" pins the choice for the rest of the job.
 */

#include <QObject>
#include <QMutex>
#include <QStringList>
#include <QThreadPool>
#include <QWaitCondition>
#include <QElapsedTimer>

#include <atomic>

namespace zipstream { class Writer; }

namespace ruby::android {

class FileOperations : public QObject {
    Q_OBJECT

public:
    enum JobKind { CopyJob = 0, MoveJob, DeleteJob, ArchiveJob, ExtractJob };
    enum ConflictDecision { Overwrite = 0, Skip = 1, KeepBoth = 2, CancelOperation = 3 };

    explicit FileOperations(QObject* parent = nullptr);
    ~FileOperations() override;

    bool busy() const { return m_busy.load(); }
    QString title() const { return m_title; }
    QString detail() const { return m_detail; }
    qreal progress() const { return m_progress; }
    QString bytesText() const { return m_bytesText; }
    QString itemsText() const { return m_itemsText; }
    bool conflictActive() const { return m_conflictActive; }
    QString conflictSource() const { return m_conflictSource; }
    QString conflictTarget() const { return m_conflictTarget; }
    bool conflictIsDir() const { return m_conflictIsDir; }

    /// Queue operations. Each returns false only when the arguments are empty.
    bool copy(const QStringList& sources, const QString& destDir);
    bool move(const QStringList& sources, const QString& destDir);
    bool remove(const QStringList& paths);
    /// `outZipPath` may be empty for an automatic name in `baseDir`.
    bool archive(const QStringList& sources, const QString& outZipPath,
                 const QString& baseDir);
    bool extract(const QString& archivePath, const QString& destDir);

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resolveConflict(int decision, bool applyToAll = false);

signals:
    /// Any of the read-only properties above changed. QML binds to the
    /// RubyFileModel facade, which re-emits this as operationStateChanged().
    void stateChanged();
    void conflictRequested();
    /// kind is one of JobKind. Emitted on the GUI thread after the worker ends.
    void finished(int kind, bool ok, bool cancelled, const QString& summary,
                  const QString& destDir);

private:
    struct Job {
        JobKind kind = CopyJob;
        QStringList sources;
        QString destDir;
        QString outZipPath;
    };

    void enqueue(const Job& job);
    void startNext();
    void runJob(const Job& job);

    bool runCopyOrMove(const Job& job, bool move, QString* summary);
    bool runDelete(const Job& job, QString* summary);
    bool runArchive(const Job& job, QString* summary);
    bool runExtract(const Job& job, QString* summary);

    bool copyOne(const QString& source, const QString& target, const QString& detail);
    bool copyTree(const QString& sourceDir, const QString& targetDir);
    bool deleteTree(const QString& path);

    bool archiveWalk(zipstream::Writer& writer, const QString& current,
                     const QString& prefix, const QString& outZip);
    bool makeDirectory(const QString& path);

    void reportProgress(qint64 done, qint64 total, int itemsDone, int itemsTotal,
                        const QString& detail);
    void tick(qint64 bytes, const QString& detail);
    void tickItem(const QString& detail);
    void recordFailure(const QString& path);
    int askConflict(const QString& source, const QString& target, bool isDir);
    bool cancelled() const { return m_cancel.load(); }

    QThreadPool m_pool;
    QList<Job> m_queue;

    std::atomic_bool m_busy{false};
    std::atomic_bool m_cancel{false};
    int m_activeKind = -1;

    // Worker-thread-only counters for the job in flight.
    qint64 m_workDone = 0;
    qint64 m_workTotal = 0;
    int m_workItemsDone = 0;
    int m_workItemsTotal = 0;
    int m_workFailures = 0;
    QStringList m_failedNames;
    QElapsedTimer m_progressClock;
    qint64 m_lastPost = -1000;

    // GUI-thread-only presentation state.
    QString m_title;
    QString m_detail;
    qreal m_progress = -1.0;
    QString m_bytesText;
    QString m_itemsText;
    bool m_conflictActive = false;
    QString m_conflictSource;
    QString m_conflictTarget;
    bool m_conflictIsDir = false;

    // Conflict rendezvous between the worker and resolveConflict().
    QMutex m_conflictMutex;
    QWaitCondition m_conflictWait;
    int m_waitingConflictId = 0;
    int m_nextConflictId = 0;
    int m_conflictResponse = -1;
    std::atomic<int> m_policy{-1}; // -1 ask, otherwise ConflictDecision
};

} // namespace ruby::android
