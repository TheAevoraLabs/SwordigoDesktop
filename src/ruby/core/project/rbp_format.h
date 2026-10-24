#pragma once
// ============================================================================
// rbp_format.h - Ruby Project Descriptor (.rbp) binary reader and writer.
//
// File layout (every multi-byte integer is little-endian):
//
//   Header (16 bytes)
//     char[4]  magic           'R', 'B', 'P', '2'
//     uint32_t format_version  2
//     uint32_t header_size     16
//     uint32_t chunk_count
//
//   Chunk (repeated chunk_count times)
//     char[4]  chunk_type      'META', 'PKGS', 'STAT' (or a future FourCC)
//     uint32_t chunk_length    payload bytes, excluding this 8-byte envelope
//     uint8_t  payload[chunk_length]
//
//   String (inside a payload)
//     uint16_t byte_length + raw UTF-8 bytes (no null terminator)
//
// Forward compatibility: a chunk whose FourCC is not understood is skipped
// using chunk_length. Known chunks may appear in any order; if a known chunk
// is repeated, its first occurrence wins. Trailing bytes inside a known chunk
// are ignored so fields can be appended in later revisions.
//
// The descriptor is Ruby-owned metadata. It never contains Launcher-only
// content and is never opened as text.
// ============================================================================

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace ruby::project {

// Header constants. FourCC values are the four ASCII bytes in file order,
// represented as a big-endian integer (so 'R','B','P','2' reads as 0x52425032).
inline constexpr quint32 kRbpMagic = 0x52425032u;
inline constexpr quint32 kRbpFormatVersion = 2;
inline constexpr quint32 kRbpHeaderSize = 16;
inline constexpr quint32 kRbpChunkEnvelopeSize = 8;

inline constexpr quint32 kRbpChunkMeta = 0x4D455441u;  // 'M','E','T','A'
inline constexpr quint32 kRbpChunkPkgs = 0x504B4753u;  // 'P','K','G','S'
inline constexpr quint32 kRbpChunkStat = 0x53544154u;  // 'S','T','A','T'

// Defensive limits. A project descriptor is a few hundred bytes; these bounds
// only exist so a corrupt or hostile file cannot force large allocations.
inline constexpr qint64 kRbpMaxFileSize = 8 * 1024 * 1024;
inline constexpr quint32 kRbpMaxChunkSize = 4 * 1024 * 1024;
inline constexpr quint32 kRbpMaxChunkCount = 256;
inline constexpr int kRbpMaxStringBytes = 0xFFFF;
inline constexpr int kRbpMaxPackages = 0xFFFF;

// META.target_abis bitfield.
enum TargetAbi : quint16 {
    TargetAbi_None = 0x0,
    TargetAbi_Arm64V8A = 0x1,
    TargetAbi_ArmeabiV7A = 0x2,
    TargetAbi_X86_64 = 0x4
};

inline constexpr quint16 kRbpDefaultTargetAbis = static_cast<quint16>(
    TargetAbi_Arm64V8A | TargetAbi_ArmeabiV7A);

// META.compiler_pipeline. Unknown values are preserved on read so a descriptor
// written by a newer build still round-trips through an older one.
enum class CompilerPipeline : quint8 {
    FileRiftV2 = 0,
    ProtocBinary = 1,
    RawPassthrough = 2
};

// PKGS entry flags. Bit 0: 0 = runtime library, 1 = devtime library.
enum PackageFlag : quint8 {
    PackageFlag_Devtime = 0x1
};

struct RbpMeta {
    QString id;
    QString name;
    QString author;
    QString version;
    QString description;
    quint64 created_ts = 0;
    quint64 last_modified_ts = 0;
    quint16 target_abis = kRbpDefaultTargetAbis;
    CompilerPipeline compiler_pipeline = CompilerPipeline::FileRiftV2;
};

struct RbpPackage {
    QString id;
    QString version;
    quint8 flags = 0;

    bool is_devtime() const { return (flags & PackageFlag_Devtime) != 0; }
};

struct RbpStats {
    quint32 edit_count = 0;
    quint32 scene_count = 0;
    QString last_opened_scene;
};

struct RbpProject {
    RbpMeta meta;
    QVector<RbpPackage> packages;
    RbpStats stats;
};

enum class RbpError {
    None = 0,
    FileNotFound,
    IoError,
    FileTooLarge,
    BadMagic,
    UnsupportedVersion,
    BadHeaderSize,
    Truncated,
    TrailingData,
    BadChunkLength,
    TooManyChunks,
    MissingMetadata,
    StringTooLong,
    TooManyPackages,
    InvalidData,
    AtomicRenameFailed
};

// Human-readable description for logging and UI. ASCII only.
QString rbp_error_string(RbpError error);

// Reads and validates a descriptor from disk. `out` is only modified when the
// whole file parses successfully. `error_message` is optional and receives
// context (for example the failing chunk or byte offset).
RbpError read_rbp(const QString& path, RbpProject& out,
                  QString* error_message = nullptr);

// Serializes `project` and saves it atomically: the bytes go to
// `<path>.tmp`, are flushed and closed, then renamed over `path`. On any
// failure the previous descriptor is left intact and no `.tmp` is left behind.
RbpError write_rbp(const QString& path, const RbpProject& project,
                   QString* error_message = nullptr);

// In-memory variants, used by tests and by callers that already hold bytes.
// parse_rbp() leaves `out` untouched on failure; serialize_rbp() leaves `out`
// empty on failure.
RbpError parse_rbp(const QByteArray& bytes, RbpProject& out,
                   QString* error_message = nullptr);
RbpError serialize_rbp(const RbpProject& project, QByteArray& out,
                       QString* error_message = nullptr);

namespace detail {

// Shared atomic file replacement used by the descriptor and Launcher manifest
// writers. Exposed for the sibling serializer in this directory; not part of
// the stable project API.
RbpError write_file_atomic(const QString& path, const QByteArray& bytes,
                           QString* error_message = nullptr);

} // namespace detail

} // namespace ruby::project
