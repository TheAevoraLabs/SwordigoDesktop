#pragma once
// ============================================================================
// diff_types.h - Value types shared by the Ruby asset diff engine.
//
// The engine compares files under project/ against the vanilla baseline. It is
// pure C++ (Qt value types only) so the same code runs on Desktop and Android
// and can be unit-tested without a QML or widget layer.
//
// DiffStatus values are stored in .rbp/diff_cache.bin and must not be reordered.
//   Unchanged - path exists in the vanilla baseline with the same content hash
//   Modified  - path exists in the vanilla baseline with different content
//   Added     - path is not part of the vanilla baseline
// ============================================================================

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace ruby::diff {

enum class DiffStatus : quint8 {
    Unchanged = 0,
    Modified = 1,
    Added = 2
};

// ASCII label for logs and UI. Never localized, never empty.
inline QString diff_status_name(DiffStatus status) {
    switch (status) {
    case DiffStatus::Unchanged:
        return QStringLiteral("unchanged");
    case DiffStatus::Modified:
        return QStringLiteral("modified");
    case DiffStatus::Added:
        return QStringLiteral("added");
    }
    return QStringLiteral("unknown");
}

// One tracked asset. Paths are relative to the project/ asset folder, always
// use '/' separators, and are never absolute, so the cache is portable across
// Desktop and Android.
struct DiffFileEntry {
    QString rel_path;
    quint64 mtime_secs = 0;
    quint64 file_size = 0;
    quint64 file_hash = 0;
    DiffStatus status = DiffStatus::Unchanged;

    bool is_dirty() const { return status != DiffStatus::Unchanged; }
};

// Result of one background scan. The counters are derived while scanning so
// the UI does not have to walk the entry list to draw badges.
struct DiffScanResult {
    QVector<DiffFileEntry> entries;

    int unchanged_count = 0;
    int modified_count = 0;
    int added_count = 0;

    // Diagnostics for .rbp/logs/project.log and the project workcenter.
    int files_hashed = 0;      // content read and hashed
    int files_from_cache = 0;  // hash reused because mtime and size matched
    int files_locked = 0;      // skipped while an editor write lock is held
    int files_skipped = 0;     // unreadable or vanished
    int case_mismatches = 0;   // path casing differs from the vanilla entry

    qint64 duration_ms = 0;
    quint64 vanilla_base_hash = 0;
    bool cancelled = false;
    bool cache_loaded = false;
    bool cache_written = false;
    QString cache_error;
    QStringList warnings;

    bool is_empty() const { return entries.isEmpty(); }
    int total_files() const { return entries.size(); }
    int dirty_count() const { return modified_count + added_count; }

    int status_count(DiffStatus status) const {
        int count = 0;
        for (const DiffFileEntry& entry : entries) {
            if (entry.status == status) {
                ++count;
            }
        }
        return count;
    }
};

// Warnings are capped so a pathological project cannot grow the result (or the
// log file) without bound. The scanner reports the cap through warnings itself.
inline constexpr int kDiffMaxWarnings = 64;

} // namespace ruby::diff

Q_DECLARE_METATYPE(ruby::diff::DiffScanResult)
