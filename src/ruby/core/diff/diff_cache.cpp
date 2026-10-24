// ============================================================================
// diff_cache.cpp - .rbp/diff_cache.bin reader and writer.
//
// Parsing is defensive: every length is validated against the bytes that
// remain before anything is allocated, paths must be safe relative paths, and
// the output vector is only replaced after the whole file parses.
// ============================================================================

#include "diff_cache.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringConverter>

namespace ruby::diff {

namespace {

// Smallest possible entry: empty path length (2) + mtime (8) + size (8) +
// hash (8) + status (1). Empty paths are rejected, so a real entry is larger.
constexpr qint64 kEntryFixedBytes = 2 + 8 + 8 + 8 + 1;

void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

quint32 decode_fourcc(const char* bytes) {
    return (quint32(static_cast<quint8>(bytes[0])) << 24) |
           (quint32(static_cast<quint8>(bytes[1])) << 16) |
           (quint32(static_cast<quint8>(bytes[2])) << 8) |
           quint32(static_cast<quint8>(bytes[3]));
}

void encode_fourcc(quint32 value, char* bytes) {
    bytes[0] = static_cast<char>((value >> 24) & 0xFFu);
    bytes[1] = static_cast<char>((value >> 16) & 0xFFu);
    bytes[2] = static_cast<char>((value >> 8) & 0xFFu);
    bytes[3] = static_cast<char>(value & 0xFFu);
}

// Cache keys are relative to project/ and must stay inside it. Rejecting
// absolute paths, backslashes, empty components, "." and ".." keeps the cache
// from ever naming a file outside the asset tree.
bool is_safe_relative_path(const QString& path) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) ||
        path.startsWith(QLatin1Char('\\')) || path.contains(QLatin1Char('\\')) ||
        path.contains(QChar::Null)) {
        return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString& part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") ||
            part == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}

// Bounded little-endian cursor. QDataStream handles endian conversion; the
// cursor tracks the bytes that remain so every field is checked first.
class StreamReader {
public:
    explicit StreamReader(const QByteArray& data)
        : m_stream(data), m_remaining(data.size()) {
        m_stream.setByteOrder(QDataStream::LittleEndian);
    }

    qint64 remaining() const { return m_remaining; }

    bool read_u8(quint8& value) { return read_integer(value, 1); }
    bool read_u16(quint16& value) { return read_integer(value, 2); }
    bool read_u32(quint32& value) { return read_integer(value, 4); }
    bool read_u64(quint64& value) { return read_integer(value, 8); }

    bool read_bytes(char* destination, int count) {
        if (count < 0 || m_remaining < count) {
            return false;
        }
        if (count == 0) {
            return true;
        }
        if (m_stream.readRawData(destination, count) != count) {
            return false;
        }
        m_remaining -= count;
        return true;
    }

private:
    template <typename T>
    bool read_integer(T& value, int width) {
        if (m_remaining < width) {
            return false;
        }
        m_stream >> value;
        if (m_stream.status() != QDataStream::Ok) {
            return false;
        }
        m_remaining -= width;
        return true;
    }

    QDataStream m_stream;
    qint64 m_remaining = 0;
};

DiffCacheError fail(DiffCacheError error, const QString& message,
                    QString* error_message) {
    set_error(error_message, message);
    return error;
}

DiffCacheError parse_with_layout(const QByteArray& bytes, bool legacy,
                                 QVector<DiffFileEntry>& out,
                                 DiffCacheInfo* info,
                                 QString* error_message) {
    StreamReader reader(bytes);

    char magic[4] = {};
    if (!reader.read_bytes(magic, 4)) {
        return fail(DiffCacheError::Truncated,
                    QStringLiteral("Diff cache is shorter than its magic"),
                    error_message);
    }
    if (decode_fourcc(magic) != kDiffCacheMagic) {
        return fail(DiffCacheError::BadMagic,
                    QStringLiteral("Not a diff cache (expected magic DCAC)"),
                    error_message);
    }

    quint32 version = 0;
    if (!reader.read_u32(version)) {
        return fail(DiffCacheError::Truncated,
                    QStringLiteral("Diff cache header is truncated"),
                    error_message);
    }
    if (version != kDiffCacheVersion) {
        return fail(DiffCacheError::UnsupportedVersion,
                    QStringLiteral("Unsupported diff cache version %1 "
                                   "(expected %2)")
                        .arg(version)
                        .arg(kDiffCacheVersion),
                    error_message);
    }

    quint64 vanilla_base_hash = 0;
    quint32 entry_count = 0;
    quint32 reserved = 0;

    if (legacy) {
        if (!reader.read_u32(entry_count) || !reader.read_u32(reserved)) {
            return fail(DiffCacheError::Truncated,
                        QStringLiteral("Diff cache header is truncated"),
                        error_message);
        }
        if (reserved != 0) {
            return fail(DiffCacheError::BadEntry,
                        QStringLiteral("Legacy diff cache reserved field is "
                                       "not zero"),
                        error_message);
        }
    } else {
        if (!reader.read_u64(vanilla_base_hash) ||
            !reader.read_u32(entry_count) || !reader.read_u32(reserved)) {
            return fail(DiffCacheError::Truncated,
                        QStringLiteral("Diff cache header is truncated"),
                        error_message);
        }
        if (reserved != 0) {
            return fail(DiffCacheError::BadEntry,
                        QStringLiteral("Diff cache reserved field is not zero"),
                        error_message);
        }
    }

    if (entry_count > kDiffCacheMaxEntries) {
        return fail(DiffCacheError::TooManyEntries,
                    QStringLiteral("Diff cache declares %1 entries; the limit "
                                   "is %2")
                        .arg(entry_count)
                        .arg(kDiffCacheMaxEntries),
                    error_message);
    }
    if (reader.remaining() <
        static_cast<qint64>(entry_count) * kEntryFixedBytes) {
        return fail(DiffCacheError::Truncated,
                    QStringLiteral("Diff cache declares %1 entries but only "
                                   "%2 bytes remain")
                        .arg(entry_count)
                        .arg(reader.remaining()),
                    error_message);
    }

    QVector<DiffFileEntry> parsed;
    parsed.reserve(static_cast<int>(entry_count));
    QSet<QString> seen;
    seen.reserve(static_cast<int>(entry_count));

    for (quint32 index = 0; index < entry_count; ++index) {
        quint16 path_length = 0;
        if (!reader.read_u16(path_length)) {
            return fail(DiffCacheError::Truncated,
                        QStringLiteral("Diff cache is truncated at entry %1")
                            .arg(index),
                        error_message);
        }
        if (path_length == 0) {
            return fail(DiffCacheError::BadEntry,
                        QStringLiteral("Diff cache entry %1 has an empty path")
                            .arg(index),
                        error_message);
        }
        if (reader.remaining() <
            static_cast<qint64>(path_length) + (kEntryFixedBytes - 2)) {
            return fail(DiffCacheError::Truncated,
                        QStringLiteral("Diff cache entry %1 is truncated")
                            .arg(index),
                        error_message);
        }

        QByteArray path_bytes(path_length, Qt::Uninitialized);
        if (!reader.read_bytes(path_bytes.data(), path_length)) {
            return fail(DiffCacheError::Truncated,
                        QStringLiteral("Diff cache entry %1 is truncated")
                            .arg(index),
                        error_message);
        }

        QStringDecoder decoder(QStringConverter::Utf8);
        const QString path = decoder.decode(path_bytes);
        if (decoder.hasError()) {
            return fail(DiffCacheError::BadEntry,
                        QStringLiteral("Diff cache entry %1 path is not valid "
                                       "UTF-8")
                            .arg(index),
                        error_message);
        }
        if (!is_safe_relative_path(path)) {
            return fail(DiffCacheError::InvalidPath,
                        QStringLiteral("Diff cache entry %1 path is not a safe "
                                       "relative path")
                            .arg(index),
                        error_message);
        }

        DiffFileEntry entry;
        entry.rel_path = path;
        quint8 status = 0;
        if (!reader.read_u64(entry.mtime_secs) ||
            !reader.read_u64(entry.file_size) ||
            !reader.read_u64(entry.file_hash) || !reader.read_u8(status)) {
            return fail(DiffCacheError::Truncated,
                        QStringLiteral("Diff cache entry %1 is truncated")
                            .arg(index),
                        error_message);
        }
        // Unknown status bytes are treated as Unchanged so a cache written by
        // a newer build still loads; the next scan recomputes the value.
        entry.status = status <= static_cast<quint8>(DiffStatus::Added)
                           ? static_cast<DiffStatus>(status)
                           : DiffStatus::Unchanged;

        // Duplicate paths keep the first occurrence, matching the RBP2 reader.
        if (seen.contains(path)) {
            continue;
        }
        seen.insert(path);
        parsed.append(entry);
    }

    if (reader.remaining() != 0) {
        return fail(DiffCacheError::TrailingData,
                    QStringLiteral("%1 unexpected trailing bytes after the "
                                   "last diff cache entry")
                        .arg(reader.remaining()),
                    error_message);
    }

    out = parsed;
    if (info) {
        info->format_version = version;
        info->vanilla_base_hash = vanilla_base_hash;
        info->legacy_header = legacy;
    }
    return DiffCacheError::None;
}

DiffCacheError write_file_atomic(const QString& path, const QByteArray& bytes,
                                 QString* error_message) {
    if (path.isEmpty()) {
        return fail(DiffCacheError::InvalidPath,
                    QStringLiteral("Cannot save a diff cache to an empty path"),
                    error_message);
    }

    const QFileInfo target(path);
    const QString directory = target.absolutePath();
    if (!directory.isEmpty() && !QDir(directory).exists() &&
        !QDir().mkpath(directory)) {
        return fail(DiffCacheError::IoError,
                    QStringLiteral("Cannot create the diff cache directory %1")
                        .arg(directory),
                    error_message);
    }

    const QString temp_path = path + QStringLiteral(".tmp");
    const bool had_target = target.exists();
    const QFile::Permissions target_permissions =
        had_target ? QFile::permissions(path) : QFile::Permissions();

    {
        QFile temp(temp_path);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return fail(DiffCacheError::IoError,
                        QStringLiteral("Cannot open %1 for writing: %2")
                            .arg(temp_path, temp.errorString()),
                        error_message);
        }

        const qint64 written = temp.write(bytes);
        if (written != bytes.size()) {
            const QString reason = temp.errorString();
            temp.close();
            QFile::remove(temp_path);
            return fail(DiffCacheError::IoError,
                        QStringLiteral("Failed to write %1: %2")
                            .arg(temp_path, reason),
                        error_message);
        }
        if (!temp.flush()) {
            const QString reason = temp.errorString();
            temp.close();
            QFile::remove(temp_path);
            return fail(DiffCacheError::IoError,
                        QStringLiteral("Failed to flush %1: %2")
                            .arg(temp_path, reason),
                        error_message);
        }
        temp.close();
        if (temp.error() != QFileDevice::NoError) {
            const QString reason = temp.errorString();
            QFile::remove(temp_path);
            return fail(DiffCacheError::IoError,
                        QStringLiteral("Failed to close %1: %2")
                            .arg(temp_path, reason),
                        error_message);
        }

        if (had_target && target_permissions != QFile::Permissions()) {
            temp.setPermissions(target_permissions);
        }
    }

    QFile source(temp_path);
    if (source.rename(path)) {
        return DiffCacheError::None;
    }
    const QString rename_error = source.errorString();

    if (QFile::exists(path)) {
        const QString backup_path = path + QStringLiteral(".bak");
        QFile::remove(backup_path);
        if (QFile::rename(path, backup_path)) {
            if (QFile::rename(temp_path, path)) {
                QFile::remove(backup_path);
                return DiffCacheError::None;
            }
            if (!QFile::rename(backup_path, path)) {
                return fail(DiffCacheError::AtomicRenameFailed,
                            QStringLiteral("Could not install %1 and could "
                                           "not restore the previous cache; "
                                           "the backup is %2")
                                .arg(path, backup_path),
                            error_message);
            }
            QFile::remove(temp_path);
            return fail(DiffCacheError::AtomicRenameFailed,
                        QStringLiteral("Could not replace %1; the previous "
                                       "cache was restored")
                            .arg(path),
                        error_message);
        }
        QFile::remove(temp_path);
        return fail(DiffCacheError::AtomicRenameFailed,
                    QStringLiteral("Could not replace %1: %2")
                        .arg(path, rename_error),
                    error_message);
    }

    QFile::remove(temp_path);
    return fail(DiffCacheError::IoError,
                QStringLiteral("Could not save %1: %2")
                    .arg(path, rename_error),
                error_message);
}

} // namespace

QString diff_cache_error_string(DiffCacheError error) {
    switch (error) {
    case DiffCacheError::None:
        return QStringLiteral("No error");
    case DiffCacheError::FileNotFound:
        return QStringLiteral("Diff cache not found");
    case DiffCacheError::IoError:
        return QStringLiteral("Diff cache I/O error");
    case DiffCacheError::FileTooLarge:
        return QStringLiteral("Diff cache is too large");
    case DiffCacheError::BadMagic:
        return QStringLiteral("Bad DCAC magic");
    case DiffCacheError::UnsupportedVersion:
        return QStringLiteral("Unsupported diff cache version");
    case DiffCacheError::Truncated:
        return QStringLiteral("Diff cache is truncated");
    case DiffCacheError::BadEntry:
        return QStringLiteral("Malformed diff cache entry");
    case DiffCacheError::TooManyEntries:
        return QStringLiteral("Too many diff cache entries");
    case DiffCacheError::DuplicatePath:
        return QStringLiteral("Duplicate diff cache path");
    case DiffCacheError::InvalidPath:
        return QStringLiteral("Invalid diff cache path");
    case DiffCacheError::TrailingData:
        return QStringLiteral("Unexpected trailing diff cache bytes");
    case DiffCacheError::AtomicRenameFailed:
        return QStringLiteral("Could not replace the diff cache");
    }
    return QStringLiteral("Unknown diff cache error");
}

DiffCacheError parse_diff_cache(const QByteArray& bytes,
                                QVector<DiffFileEntry>& out,
                                DiffCacheInfo* info,
                                QString* error_message) {
    QVector<DiffFileEntry> parsed;
    DiffCacheInfo parsed_info;
    QString spec_error;
    const DiffCacheError spec = parse_with_layout(bytes, false, parsed,
                                                  &parsed_info, &spec_error);
    if (spec == DiffCacheError::None) {
        out = parsed;
        if (info) {
            *info = parsed_info;
        }
        return DiffCacheError::None;
    }
    if (spec == DiffCacheError::BadMagic ||
        spec == DiffCacheError::UnsupportedVersion) {
        set_error(error_message, spec_error);
        return spec;
    }

    // A 16-byte header was described by an earlier Phase 2 draft. Try it only
    // after the authoritative 24-byte layout fails validation.
    QVector<DiffFileEntry> legacy_entries;
    DiffCacheInfo legacy_info;
    QString legacy_error;
    const DiffCacheError legacy = parse_with_layout(
        bytes, true, legacy_entries, &legacy_info, &legacy_error);
    if (legacy == DiffCacheError::None) {
        out = legacy_entries;
        if (info) {
            *info = legacy_info;
        }
        return DiffCacheError::None;
    }

    set_error(error_message,
              QStringLiteral("%1 (16-byte header fallback: %2)")
                  .arg(spec_error, legacy_error));
    return spec;
}

DiffCacheError serialize_diff_cache(const QVector<DiffFileEntry>& entries,
                                    quint64 vanilla_base_hash, QByteArray& out,
                                    QString* error_message) {
    if (static_cast<quint32>(entries.size()) > kDiffCacheMaxEntries) {
        return fail(DiffCacheError::TooManyEntries,
                    QStringLiteral("Too many diff cache entries (%1)")
                        .arg(entries.size()),
                    error_message);
    }

    QByteArray file;
    QDataStream stream(&file, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    char magic[4] = {};
    encode_fourcc(kDiffCacheMagic, magic);
    if (stream.writeRawData(magic, 4) != 4) {
        return fail(DiffCacheError::IoError,
                    QStringLiteral("Failed to encode the DCAC header"),
                    error_message);
    }
    stream << static_cast<quint32>(kDiffCacheVersion) << vanilla_base_hash
           << static_cast<quint32>(entries.size()) << static_cast<quint32>(0);

    QSet<QString> seen;
    seen.reserve(entries.size());

    for (int index = 0; index < entries.size(); ++index) {
        const DiffFileEntry& entry = entries.at(index);
        if (!is_safe_relative_path(entry.rel_path)) {
            return fail(DiffCacheError::InvalidPath,
                        QStringLiteral("Diff cache entry %1 path is not a safe "
                                       "relative path: %2")
                            .arg(index)
                            .arg(entry.rel_path),
                        error_message);
        }

        const QByteArray path_bytes = entry.rel_path.toUtf8();
        if (path_bytes.isEmpty() ||
            path_bytes.size() > kDiffCacheMaxPathBytes) {
            return fail(DiffCacheError::InvalidPath,
                        QStringLiteral("Diff cache entry %1 path is %2 UTF-8 "
                                       "bytes; the limit is %3")
                            .arg(index)
                            .arg(path_bytes.size())
                            .arg(kDiffCacheMaxPathBytes),
                        error_message);
        }
        if (seen.contains(entry.rel_path)) {
            return fail(DiffCacheError::DuplicatePath,
                        QStringLiteral("Diff cache path appears twice: %1")
                            .arg(entry.rel_path),
                        error_message);
        }
        seen.insert(entry.rel_path);

        const quint8 status = static_cast<quint8>(entry.status);
        if (status > static_cast<quint8>(DiffStatus::Added)) {
            return fail(DiffCacheError::BadEntry,
                        QStringLiteral("Diff cache entry %1 has invalid status "
                                       "%2")
                            .arg(index)
                            .arg(status),
                        error_message);
        }

        stream << static_cast<quint16>(path_bytes.size());
        if (stream.writeRawData(path_bytes.constData(), path_bytes.size()) !=
            path_bytes.size()) {
            return fail(DiffCacheError::IoError,
                        QStringLiteral("Failed to encode diff cache entry %1")
                            .arg(index),
                        error_message);
        }
        stream << entry.mtime_secs << entry.file_size << entry.file_hash
               << status;
        if (stream.status() != QDataStream::Ok) {
            return fail(DiffCacheError::IoError,
                        QStringLiteral("Failed to encode diff cache entry %1")
                            .arg(index),
                        error_message);
        }
    }

    out = file;
    return DiffCacheError::None;
}

DiffCacheError read_diff_cache(const QString& path,
                               QVector<DiffFileEntry>& out,
                               DiffCacheInfo* info,
                               QString* error_message) {
    const QFileInfo file_info(path);
    if (!file_info.exists() || !file_info.isFile()) {
        return fail(DiffCacheError::FileNotFound,
                    QStringLiteral("Diff cache not found: %1").arg(path),
                    error_message);
    }
    if (file_info.size() > kDiffCacheMaxFileSize) {
        return fail(DiffCacheError::FileTooLarge,
                    QStringLiteral("Diff cache is %1 bytes; the limit is %2: %3")
                        .arg(file_info.size())
                        .arg(kDiffCacheMaxFileSize)
                        .arg(path),
                    error_message);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(DiffCacheError::IoError,
                    QStringLiteral("Cannot open %1: %2")
                        .arg(path, file.errorString()),
                    error_message);
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return fail(DiffCacheError::IoError,
                    QStringLiteral("Cannot read %1: %2")
                        .arg(path, file.errorString()),
                    error_message);
    }

    QString parse_error;
    const DiffCacheError result = parse_diff_cache(bytes, out, info,
                                                   &parse_error);
    if (result != DiffCacheError::None) {
        set_error(error_message,
                  QStringLiteral("%1: %2").arg(path, parse_error));
    }
    return result;
}

DiffCacheError write_diff_cache(const QString& path,
                                const QVector<DiffFileEntry>& entries,
                                quint64 vanilla_base_hash,
                                QString* error_message) {
    QByteArray bytes;
    const DiffCacheError result = serialize_diff_cache(entries,
                                                       vanilla_base_hash,
                                                       bytes, error_message);
    if (result != DiffCacheError::None) {
        return result;
    }
    return write_file_atomic(path, bytes, error_message);
}

} // namespace ruby::diff
