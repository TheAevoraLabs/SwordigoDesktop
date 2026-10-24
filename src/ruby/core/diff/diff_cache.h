#pragma once
// ============================================================================
// diff_cache.h - .rbp/diff_cache.bin reader and writer (DCAC, version 1).
//
// File layout (every integer is little-endian):
//
//   Header (24 bytes)
//     char[4]  magic              'D', 'C', 'A', 'C'
//     uint32_t format_version     1
//     uint64_t vanilla_base_hash  manifest hash the statuses were computed from
//     uint32_t entry_count
//     uint32_t reserved           0
//
//   Entry (repeated entry_count times)
//     uint16_t byte_length + raw UTF-8 rel_path (no null terminator)
//     uint64_t mtime_secs
//     uint64_t file_size
//     uint64_t file_hash          xxHash64 of the file content
//     uint8_t  diff_status        0 = Unchanged, 1 = Modified, 2 = Added
//
// The 24-byte header is the layout required by the Ruby Project Specification
// (section 4); the vanilla_base_hash lets a scan detect that the baseline was
// replaced. Readers also accept a 16-byte header (magic, version, entry_count,
// reserved) because an earlier draft of the Phase 2 task described one. The
// writer always emits the specification's 24-byte form.
//
// Writes are atomic: bytes go to "<path>.tmp", are flushed and closed, then the
// file is renamed over the target. A backup-and-restore path handles
// filesystems that refuse rename-over-existing, so a failed save never
// destroys the previous cache.
// ============================================================================

#include "diff_types.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace ruby::diff {

inline constexpr const char* kDiffCacheFileName = "diff_cache.bin";
inline constexpr quint32 kDiffCacheMagic = 0x44434143u;  // 'DCAC'
inline constexpr quint32 kDiffCacheVersion = 1;
inline constexpr quint32 kDiffCacheHeaderSize = 24;
inline constexpr quint32 kDiffCacheLegacyHeaderSize = 16;
inline constexpr int kDiffCacheMaxPathBytes = 0xFFFF;

// Defensive limits. A project with 1,200 vanilla assets plus additions is far
// below these; the bounds stop a corrupt file from forcing huge allocations.
inline constexpr qint64 kDiffCacheMaxFileSize = 32 * 1024 * 1024;
inline constexpr quint32 kDiffCacheMaxEntries = 500000;

enum class DiffCacheError {
    None = 0,
    FileNotFound,
    IoError,
    FileTooLarge,
    BadMagic,
    UnsupportedVersion,
    Truncated,
    BadEntry,
    TooManyEntries,
    DuplicatePath,
    InvalidPath,
    TrailingData,
    AtomicRenameFailed
};

// ASCII description for logs and UI.
QString diff_cache_error_string(DiffCacheError error);

struct DiffCacheInfo {
    quint32 format_version = kDiffCacheVersion;
    quint64 vanilla_base_hash = 0;
    bool legacy_header = false;
};

// In-memory variants, used by tests and by callers that already hold bytes.
// `out` is left untouched on failure.
DiffCacheError parse_diff_cache(const QByteArray& bytes,
                                QVector<DiffFileEntry>& out,
                                DiffCacheInfo* info = nullptr,
                                QString* error_message = nullptr);

DiffCacheError serialize_diff_cache(const QVector<DiffFileEntry>& entries,
                                    quint64 vanilla_base_hash, QByteArray& out,
                                    QString* error_message = nullptr);

// Disk variants. A missing cache is reported as FileNotFound; callers treat
// that as "first scan" rather than an error.
DiffCacheError read_diff_cache(const QString& path,
                               QVector<DiffFileEntry>& out,
                               DiffCacheInfo* info = nullptr,
                               QString* error_message = nullptr);

// Creates the parent directory when needed and saves atomically.
DiffCacheError write_diff_cache(const QString& path,
                                const QVector<DiffFileEntry>& entries,
                                quint64 vanilla_base_hash,
                                QString* error_message = nullptr);

} // namespace ruby::diff
