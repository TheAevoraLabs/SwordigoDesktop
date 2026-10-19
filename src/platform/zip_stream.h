#pragma once
/* zip_stream.h — streaming ZIP writer/reader for the mobile file manager.
 *
 * Why this exists next to zip_archive.{h,cpp}:
 *   zip_archive's writer takes `OutEntry.data` (the whole uncompressed file in
 *   RAM) and its extractor reads the whole archive into memory. That is fine for
 *   the APK repack path it was written for, but a file manager must compress and
 *   extract multi-hundred-megabyte trees without the UI thread pausing or the
 *   process getting OOM-killed. This module streams both directions in fixed
 *   256 KiB chunks, reports progress, honours cancellation, and falls back to
 *   ZIP64 when a size/offset no longer fits the classic 32-bit fields.
 *
 * Design source:
 *   Index FM (org.kde.index) does not implement file operations itself; its
 *   Compress/Extract/Remove/Paste actions are `KIO::Job`s provided by MauiKit
 *   FileBrowsing and MauiKit Archiver. Those jobs are asynchronous, emit
 *   percent/processedAmount, can be killed, and stream through a slave instead
 *   of buffering. This is an independent zlib implementation of the same model,
 *   written for Qt/Android where KIO is not available.
 *
 * Layout notes:
 *   - Method 0 (store) and method 8 (raw deflate) are supported, matching
 *     zip_archive.
 *   - Unix mode bits from `external_attr` are preserved on write and applied on
 *     extract (executable bit), so packed shell/ELF assets stay runnable.
 *   - Extraction strips path traversal, same guard as zip::extract_all().
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zipstream {

/// Reports (processed uncompressed bytes, total uncompressed bytes, entry name).
/// `total` is 0 while the total is not known yet.
using ProgressFn = std::function<void(uint64_t processed, uint64_t total, const std::string& entry)>;
/// Returns true when the caller wants the operation to stop.
using CancelFn = std::function<bool()>;

/// What to do when an extraction target already exists.
enum class ConflictDecision { Overwrite, Skip, KeepBoth, Cancel };
/// Called on the worker thread for every collision. Returning Cancel aborts
/// the whole extraction; KeepBoth picks a unique "name (1).ext" sibling.
using ConflictFn = std::function<ConflictDecision(const std::string& entryName,
                                                  const std::string& targetPath,
                                                  bool isDir)>;

struct EntryInfo {
    std::string name;
    uint64_t    uncompSize = 0;
    uint64_t    compSize = 0;
    uint16_t    method = 0;
    uint32_t    externalAttr = 0;
};

struct ExtractResult {
    bool        ok = false;
    bool        cancelled = false;
    size_t      extracted = 0;
    size_t      skipped = 0;
    uint64_t    bytesWritten = 0;
    std::vector<std::string> failed;   // entries that failed (non-fatal)
    std::string error;                 // fatal error text (empty when ok)
};

/// List the central directory without buffering the archive body.
bool listEntries(const std::string& zipPath, std::vector<EntryInfo>& out,
                 std::string* error = nullptr);

/// Extract every entry into `destDir`, streaming each entry in 256 KiB chunks.
/// Existing files are only replaced when `overwrite` is true; otherwise a
/// collision gets a unique "name (1).ext" sibling, so re-extracting never
/// silently destroys a file the user edited.
/// When `conflict` is supplied it is consulted for every existing target and
/// `overwrite` is only used as the fallback for callers that do not pass one.
ExtractResult extractAll(const std::string& zipPath, const std::string& destDir,
                         bool overwrite,
                         const ProgressFn& progress = {},
                         const CancelFn& cancelled = {},
                         const ConflictFn& conflict = {});

// ─── Writer ──────────────────────────────────────────────────────────────────

/// Streaming ZIP writer. Classic ZIP by default; entries and the central
/// directory upgrade themselves to ZIP64 when that is required to stay valid.
class Writer {
public:
    explicit Writer(const std::string& outPath);
    ~Writer();

    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    bool open(std::string* error);

    /// Queue a directory entry. `entryName` must end with '/'.
    bool addDirectory(const std::string& entryName, uint32_t externalAttr,
                      const CancelFn& cancelled, std::string* error);

    /// Stream `sourcePath` from disk into the archive. `method` is 0 (store) or
    /// 8 (deflate). `progress` is called after every chunk with the running
    /// uncompressed byte count for this entry.
    bool addFile(const std::string& entryName, const std::string& sourcePath,
                 int method, uint32_t externalAttr,
                 const CancelFn& cancelled, const ProgressFn& progress,
                 std::string* error);

    /// Finalize the central directory. Must be called for a valid archive.
    bool close(std::string* error);

    bool wasCancelled() const { return m_cancelled; }
    uint64_t writtenBytes() const { return m_written; }

private:
    struct Impl;
    Impl* m_impl = nullptr;
    bool m_cancelled = false;
    uint64_t m_written = 0;
};

} // namespace zipstream
