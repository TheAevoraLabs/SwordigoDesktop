// ============================================================================
// rbp_format.cpp - RBP2 binary project descriptor reader and writer.
//
// See rbp_format.h for the byte layout. The reader is deliberately defensive:
// every length is checked against the remaining bytes before allocation, and
// unknown chunks are skipped instead of rejected.
// ============================================================================

#include "rbp_format.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStringConverter>

namespace ruby::project {

namespace {

// Smallest possible PKGS entry: empty id (2) + empty version (2) + flags (1).
constexpr int kPackageEntryMinBytes = 2 + 2 + 1;

void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

RbpError fail(RbpError error, const QString& message, QString* error_message) {
    set_error(error_message, message);
    return error;
}

// FourCC values are the four ASCII bytes in file order, represented as a
// big-endian integer, matching the constants in rbp_format.h.
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

QString fourcc_label(quint32 value) {
    char bytes[4] = {};
    encode_fourcc(value, bytes);
    for (char byte : bytes) {
        const int ch = static_cast<unsigned char>(byte);
        if (ch < 0x20 || ch > 0x7E) {
            return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0'));
        }
    }
    return QString::fromLatin1(bytes, 4);
}

// Bounded little-endian cursor over one QByteArray. QDataStream provides the
// endian conversion; the cursor tracks how many bytes are actually available
// so every read can be validated before it happens.
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

    bool skip(qint64 count) {
        if (count < 0 || m_remaining < count) {
            return false;
        }
        if (count == 0) {
            return true;
        }
        const int step = static_cast<int>(count);
        if (static_cast<qint64>(step) != count) {
            return false;
        }
        if (m_stream.skipRawData(step) != step) {
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

bool read_string(StreamReader& reader, QString& value) {
    quint16 length = 0;
    if (!reader.read_u16(length)) {
        return false;
    }
    if (reader.remaining() < length) {
        return false;
    }
    if (length == 0) {
        value.clear();
        return true;
    }
    QByteArray utf8(length, Qt::Uninitialized);
    if (!reader.read_bytes(utf8.data(), static_cast<int>(length))) {
        return false;
    }

    // Descriptor strings are raw UTF-8. Refuse invalid input instead of
    // silently replacing bytes with U+FFFD and writing the damage back.
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString decoded = decoder.decode(utf8);
    if (decoder.hasError()) {
        return false;
    }
    value = decoded;
    return true;
}

bool write_string(QDataStream& stream, const QString& value,
                  QString* error_message) {
    const QByteArray utf8 = value.toUtf8();
    if (utf8.size() > kRbpMaxStringBytes) {
        set_error(error_message,
                  QStringLiteral("String is %1 bytes; the RBP2 limit is %2 bytes")
                      .arg(utf8.size())
                      .arg(kRbpMaxStringBytes));
        return false;
    }
    stream << static_cast<quint16>(utf8.size());
    if (!utf8.isEmpty() &&
        stream.writeRawData(utf8.constData(), static_cast<int>(utf8.size())) !=
            utf8.size()) {
        set_error(error_message, QStringLiteral("Failed to write string payload"));
        return false;
    }
    return stream.status() == QDataStream::Ok;
}

bool parse_meta_payload(const QByteArray& payload, RbpMeta& meta) {
    StreamReader reader(payload);
    RbpMeta parsed;
    quint8 pipeline = 0;
    if (!read_string(reader, parsed.id) ||
        !read_string(reader, parsed.name) ||
        !read_string(reader, parsed.author) ||
        !read_string(reader, parsed.version) ||
        !read_string(reader, parsed.description) ||
        !reader.read_u64(parsed.created_ts) ||
        !reader.read_u64(parsed.last_modified_ts) ||
        !reader.read_u16(parsed.target_abis) ||
        !reader.read_u8(pipeline)) {
        return false;
    }
    parsed.compiler_pipeline = static_cast<CompilerPipeline>(pipeline);
    meta = parsed;
    return true;
}

bool parse_packages_payload(const QByteArray& payload,
                            QVector<RbpPackage>& packages) {
    StreamReader reader(payload);
    quint16 count = 0;
    if (!reader.read_u16(count)) {
        return false;
    }
    if (reader.remaining() < static_cast<qint64>(count) * kPackageEntryMinBytes) {
        return false;
    }
    QVector<RbpPackage> parsed;
    parsed.reserve(count);
    for (quint16 index = 0; index < count; ++index) {
        RbpPackage package;
        if (!read_string(reader, package.id) ||
            !read_string(reader, package.version) ||
            !reader.read_u8(package.flags)) {
            return false;
        }
        parsed.append(package);
    }
    packages = parsed;
    return true;
}

bool parse_stats_payload(const QByteArray& payload, RbpStats& stats) {
    StreamReader reader(payload);
    RbpStats parsed;
    if (!reader.read_u32(parsed.edit_count) ||
        !reader.read_u32(parsed.scene_count) ||
        !read_string(reader, parsed.last_opened_scene)) {
        return false;
    }
    stats = parsed;
    return true;
}

bool build_meta_payload(const RbpMeta& meta, QByteArray& out,
                        QString* error_message) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (!write_string(stream, meta.id, error_message) ||
        !write_string(stream, meta.name, error_message) ||
        !write_string(stream, meta.author, error_message) ||
        !write_string(stream, meta.version, error_message) ||
        !write_string(stream, meta.description, error_message)) {
        return false;
    }

    const quint8 pipeline = static_cast<quint8>(meta.compiler_pipeline);
    stream << meta.created_ts << meta.last_modified_ts << meta.target_abis
           << pipeline;
    if (stream.status() != QDataStream::Ok) {
        set_error(error_message, QStringLiteral("Failed to encode META payload"));
        return false;
    }
    out = payload;
    return true;
}

bool build_packages_payload(const QVector<RbpPackage>& packages,
                            QByteArray& out, QString* error_message) {
    if (packages.size() > kRbpMaxPackages) {
        set_error(error_message,
                  QStringLiteral("Too many packages (%1); the RBP2 limit is %2")
                      .arg(packages.size())
                      .arg(kRbpMaxPackages));
        return false;
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint16>(packages.size());

    for (const RbpPackage& package : packages) {
        if (!write_string(stream, package.id, error_message) ||
            !write_string(stream, package.version, error_message)) {
            return false;
        }
        stream << package.flags;
    }
    if (stream.status() != QDataStream::Ok) {
        set_error(error_message, QStringLiteral("Failed to encode PKGS payload"));
        return false;
    }
    out = payload;
    return true;
}

bool build_stats_payload(const RbpStats& stats, QByteArray& out,
                         QString* error_message) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    stream << stats.edit_count << stats.scene_count;
    if (!write_string(stream, stats.last_opened_scene, error_message)) {
        return false;
    }
    if (stream.status() != QDataStream::Ok) {
        set_error(error_message, QStringLiteral("Failed to encode STAT payload"));
        return false;
    }
    out = payload;
    return true;
}

bool write_chunk(QDataStream& stream, quint32 type, const QByteArray& payload) {
    char type_bytes[4] = {};
    encode_fourcc(type, type_bytes);
    if (stream.writeRawData(type_bytes, 4) != 4) {
        return false;
    }
    stream << static_cast<quint32>(payload.size());
    if (!payload.isEmpty() &&
        stream.writeRawData(payload.constData(),
                            static_cast<int>(payload.size())) != payload.size()) {
        return false;
    }
    return stream.status() == QDataStream::Ok;
}

RbpError validate_for_write(const RbpProject& project,
                            QString* error_message) {
    const QString* strings[] = {
        &project.meta.id,
        &project.meta.name,
        &project.meta.author,
        &project.meta.version,
        &project.meta.description,
        &project.stats.last_opened_scene,
    };
    for (const QString* value : strings) {
        if (value->toUtf8().size() > kRbpMaxStringBytes) {
            return fail(RbpError::StringTooLong,
                        QStringLiteral("A project string exceeds the RBP2 limit "
                                       "of %1 UTF-8 bytes")
                            .arg(kRbpMaxStringBytes),
                        error_message);
        }
    }

    if (project.packages.size() > kRbpMaxPackages) {
        return fail(RbpError::TooManyPackages,
                    QStringLiteral("Too many packages (%1); the RBP2 limit is %2")
                        .arg(project.packages.size())
                        .arg(kRbpMaxPackages),
                    error_message);
    }
    for (const RbpPackage& package : project.packages) {
        if (package.id.toUtf8().size() > kRbpMaxStringBytes ||
            package.version.toUtf8().size() > kRbpMaxStringBytes) {
            return fail(RbpError::StringTooLong,
                        QStringLiteral("Package string exceeds the RBP2 limit of "
                                       "%1 UTF-8 bytes")
                            .arg(kRbpMaxStringBytes),
                        error_message);
        }
    }
    return RbpError::None;
}

} // namespace

QString rbp_error_string(RbpError error) {
    switch (error) {
    case RbpError::None:
        return QStringLiteral("No error");
    case RbpError::FileNotFound:
        return QStringLiteral("Project descriptor not found");
    case RbpError::IoError:
        return QStringLiteral("File I/O error");
    case RbpError::FileTooLarge:
        return QStringLiteral("Project descriptor is too large");
    case RbpError::BadMagic:
        return QStringLiteral("Bad RBP2 magic");
    case RbpError::UnsupportedVersion:
        return QStringLiteral("Unsupported RBP format version");
    case RbpError::BadHeaderSize:
        return QStringLiteral("Invalid RBP header size");
    case RbpError::Truncated:
        return QStringLiteral("Project descriptor is truncated");
    case RbpError::TrailingData:
        return QStringLiteral("Unexpected trailing bytes after the last chunk");
    case RbpError::BadChunkLength:
        return QStringLiteral("Chunk length is out of range");
    case RbpError::TooManyChunks:
        return QStringLiteral("Too many chunks");
    case RbpError::MissingMetadata:
        return QStringLiteral("Project descriptor has no META chunk");
    case RbpError::StringTooLong:
        return QStringLiteral("String exceeds the RBP2 length limit");
    case RbpError::TooManyPackages:
        return QStringLiteral("Too many rbpkg entries");
    case RbpError::InvalidData:
        return QStringLiteral("Malformed project data");
    case RbpError::AtomicRenameFailed:
        return QStringLiteral("Could not replace the project descriptor");
    }
    return QStringLiteral("Unknown project descriptor error");
}

RbpError parse_rbp(const QByteArray& bytes, RbpProject& out,
                   QString* error_message) {
    StreamReader reader(bytes);

    char magic[4] = {};
    if (!reader.read_bytes(magic, 4)) {
        return fail(RbpError::Truncated,
                    QStringLiteral("File is shorter than the 4-byte magic"),
                    error_message);
    }
    if (decode_fourcc(magic) != kRbpMagic) {
        return fail(RbpError::BadMagic,
                    QStringLiteral("Not a Ruby project descriptor (expected "
                                   "magic RBP2)"),
                    error_message);
    }

    quint32 version = 0;
    quint32 header_size = 0;
    quint32 chunk_count = 0;
    if (!reader.read_u32(version) || !reader.read_u32(header_size) ||
        !reader.read_u32(chunk_count)) {
        return fail(RbpError::Truncated,
                    QStringLiteral("File is shorter than the 16-byte header"),
                    error_message);
    }
    if (version != kRbpFormatVersion) {
        return fail(RbpError::UnsupportedVersion,
                    QStringLiteral("Unsupported RBP format version %1 "
                                   "(expected %2)")
                        .arg(version)
                        .arg(kRbpFormatVersion),
                    error_message);
    }
    if (header_size < kRbpHeaderSize ||
        static_cast<qint64>(header_size) > bytes.size()) {
        return fail(RbpError::BadHeaderSize,
                    QStringLiteral("Invalid header size %1").arg(header_size),
                    error_message);
    }
    if (!reader.skip(static_cast<qint64>(header_size) -
                     static_cast<qint64>(kRbpHeaderSize))) {
        return fail(RbpError::Truncated,
                    QStringLiteral("Header size exceeds the available bytes"),
                    error_message);
    }
    if (chunk_count > kRbpMaxChunkCount) {
        return fail(RbpError::TooManyChunks,
                    QStringLiteral("Descriptor declares %1 chunks; the limit "
                                   "is %2")
                        .arg(chunk_count)
                        .arg(kRbpMaxChunkCount),
                    error_message);
    }

    RbpMeta meta;
    QVector<RbpPackage> packages;
    RbpStats stats;
    bool have_meta = false;
    bool have_packages = false;
    bool have_stats = false;

    for (quint32 index = 0; index < chunk_count; ++index) {
        char type_bytes[4] = {};
        quint32 chunk_length = 0;
        if (!reader.read_bytes(type_bytes, 4) ||
            !reader.read_u32(chunk_length)) {
            return fail(RbpError::Truncated,
                        QStringLiteral("Truncated chunk envelope at chunk %1")
                            .arg(index),
                        error_message);
        }

        const quint32 type = decode_fourcc(type_bytes);
        if (static_cast<qint64>(chunk_length) > reader.remaining()) {
            return fail(RbpError::Truncated,
                        QStringLiteral("Chunk %1 (%2) declares %3 bytes but "
                                       "only %4 remain")
                            .arg(index)
                            .arg(fourcc_label(type))
                            .arg(chunk_length)
                            .arg(reader.remaining()),
                        error_message);
        }

        const bool known = type == kRbpChunkMeta || type == kRbpChunkPkgs ||
                           type == kRbpChunkStat;
        const bool already_seen =
            (type == kRbpChunkMeta && have_meta) ||
            (type == kRbpChunkPkgs && have_packages) ||
            (type == kRbpChunkStat && have_stats);
        if (!known || already_seen) {
            // Forward compatibility: skip chunks this build does not know,
            // and keep the first occurrence of a known chunk.
            if (!reader.skip(chunk_length)) {
                return fail(RbpError::Truncated,
                            QStringLiteral("Could not skip chunk %1 (%2)")
                                .arg(index)
                                .arg(fourcc_label(type)),
                            error_message);
            }
            continue;
        }

        if (chunk_length > kRbpMaxChunkSize) {
            return fail(RbpError::BadChunkLength,
                        QStringLiteral("Chunk %1 (%2) is %3 bytes; the limit "
                                       "is %4")
                            .arg(index)
                            .arg(fourcc_label(type))
                            .arg(chunk_length)
                            .arg(kRbpMaxChunkSize),
                        error_message);
        }

        QByteArray payload(static_cast<qsizetype>(chunk_length),
                           Qt::Uninitialized);
        if (chunk_length > 0 &&
            !reader.read_bytes(payload.data(),
                               static_cast<int>(chunk_length))) {
            return fail(RbpError::Truncated,
                        QStringLiteral("Could not read chunk %1 (%2)")
                            .arg(index)
                            .arg(fourcc_label(type)),
                        error_message);
        }

        bool parsed = false;
        if (type == kRbpChunkMeta) {
            parsed = parse_meta_payload(payload, meta);
            have_meta = parsed;
        } else if (type == kRbpChunkPkgs) {
            parsed = parse_packages_payload(payload, packages);
            have_packages = parsed;
        } else {
            parsed = parse_stats_payload(payload, stats);
            have_stats = parsed;
        }
        if (!parsed) {
            return fail(RbpError::InvalidData,
                        QStringLiteral("Malformed %1 chunk")
                            .arg(fourcc_label(type)),
                        error_message);
        }
    }

    if (reader.remaining() != 0) {
        return fail(RbpError::TrailingData,
                    QStringLiteral("%1 unexpected trailing bytes after the "
                                   "last chunk")
                        .arg(reader.remaining()),
                    error_message);
    }
    if (!have_meta) {
        return fail(RbpError::MissingMetadata,
                    QStringLiteral("RBP2 file has no META chunk"),
                    error_message);
    }

    RbpProject parsed;
    parsed.meta = meta;
    parsed.packages = packages;
    parsed.stats = stats;
    out = parsed;
    return RbpError::None;
}

RbpError serialize_rbp(const RbpProject& project, QByteArray& out,
                       QString* error_message) {
    const RbpError validation = validate_for_write(project, error_message);
    if (validation != RbpError::None) {
        return validation;
    }

    QByteArray meta_payload;
    QByteArray packages_payload;
    QByteArray stats_payload;
    if (!build_meta_payload(project.meta, meta_payload, error_message) ||
        !build_packages_payload(project.packages, packages_payload,
                                error_message) ||
        !build_stats_payload(project.stats, stats_payload, error_message)) {
        return RbpError::InvalidData;
    }
    if (static_cast<quint32>(meta_payload.size()) > kRbpMaxChunkSize ||
        static_cast<quint32>(packages_payload.size()) > kRbpMaxChunkSize ||
        static_cast<quint32>(stats_payload.size()) > kRbpMaxChunkSize) {
        return fail(RbpError::BadChunkLength,
                    QStringLiteral("A project chunk exceeds the RBP2 size "
                                   "limit"),
                    error_message);
    }

    QByteArray file;
    QDataStream stream(&file, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    const char magic[4] = {'R', 'B', 'P', '2'};
    if (stream.writeRawData(magic, 4) != 4) {
        return fail(RbpError::IoError,
                    QStringLiteral("Failed to encode the RBP2 header"),
                    error_message);
    }
    stream << static_cast<quint32>(kRbpFormatVersion)
           << static_cast<quint32>(kRbpHeaderSize) << static_cast<quint32>(3);

    if (!write_chunk(stream, kRbpChunkMeta, meta_payload) ||
        !write_chunk(stream, kRbpChunkPkgs, packages_payload) ||
        !write_chunk(stream, kRbpChunkStat, stats_payload)) {
        return fail(RbpError::IoError,
                    QStringLiteral("Failed to encode project chunks"),
                    error_message);
    }

    out = file;
    return RbpError::None;
}

RbpError read_rbp(const QString& path, RbpProject& out,
                  QString* error_message) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return fail(RbpError::FileNotFound,
                    QStringLiteral("Project descriptor not found: %1").arg(path),
                    error_message);
    }
    if (info.size() > kRbpMaxFileSize) {
        return fail(RbpError::FileTooLarge,
                    QStringLiteral("Project descriptor is %1 bytes; the limit "
                                   "is %2: %3")
                        .arg(info.size())
                        .arg(kRbpMaxFileSize)
                        .arg(path),
                    error_message);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(RbpError::IoError,
                    QStringLiteral("Cannot open %1: %2")
                        .arg(path, file.errorString()),
                    error_message);
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return fail(RbpError::IoError,
                    QStringLiteral("Cannot read %1: %2")
                        .arg(path, file.errorString()),
                    error_message);
    }

    QString parse_message;
    const RbpError result = parse_rbp(bytes, out, &parse_message);
    if (result != RbpError::None) {
        set_error(error_message,
                  QStringLiteral("%1: %2").arg(path, parse_message));
    }
    return result;
}

RbpError write_rbp(const QString& path, const RbpProject& project,
                   QString* error_message) {
    QByteArray bytes;
    const RbpError result = serialize_rbp(project, bytes, error_message);
    if (result != RbpError::None) {
        return result;
    }
    return detail::write_file_atomic(path, bytes, error_message);
}

namespace detail {

RbpError write_file_atomic(const QString& path, const QByteArray& bytes,
                           QString* error_message) {
    if (path.isEmpty()) {
        return fail(RbpError::InvalidData,
                    QStringLiteral("Cannot save to an empty path"),
                    error_message);
    }

    const QString temp_path = path + QStringLiteral(".tmp");
    const bool had_target = QFile::exists(path);
    const QFile::Permissions target_permissions =
        had_target ? QFile::permissions(path) : QFile::Permissions();

    {
        QFile temp(temp_path);
        if (!temp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return fail(RbpError::IoError,
                        QStringLiteral("Cannot open %1 for writing: %2")
                            .arg(temp_path, temp.errorString()),
                        error_message);
        }

        const qint64 written = temp.write(bytes);
        if (written != bytes.size()) {
            const QString reason = temp.errorString();
            temp.close();
            QFile::remove(temp_path);
            return fail(RbpError::IoError,
                        QStringLiteral("Failed to write %1: %2")
                            .arg(temp_path, reason),
                        error_message);
        }
        if (!temp.flush()) {
            const QString reason = temp.errorString();
            temp.close();
            QFile::remove(temp_path);
            return fail(RbpError::IoError,
                        QStringLiteral("Failed to flush %1: %2")
                            .arg(temp_path, reason),
                        error_message);
        }
        temp.close();
        if (temp.error() != QFileDevice::NoError) {
            const QString reason = temp.errorString();
            QFile::remove(temp_path);
            return fail(RbpError::IoError,
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
        return RbpError::None;
    }
    const QString rename_error = source.errorString();

    // Some filesystems refuse to rename onto an existing file. Move the
    // current descriptor aside, install the new one, then drop the backup.
    // If the install fails, put the original back before reporting.
    if (QFile::exists(path)) {
        const QString backup_path = path + QStringLiteral(".bak");
        QFile::remove(backup_path);
        if (QFile::rename(path, backup_path)) {
            if (QFile::rename(temp_path, path)) {
                QFile::remove(backup_path);
                return RbpError::None;
            }
            if (!QFile::rename(backup_path, path)) {
                return fail(RbpError::AtomicRenameFailed,
                            QStringLiteral("Could not install %1 and could not "
                                           "restore the previous file; the "
                                           "backup is %2")
                                .arg(path, backup_path),
                            error_message);
            }
            QFile::remove(temp_path);
            return fail(RbpError::AtomicRenameFailed,
                        QStringLiteral("Could not replace %1; the previous "
                                       "file was restored")
                            .arg(path),
                        error_message);
        }
        QFile::remove(temp_path);
        return fail(RbpError::AtomicRenameFailed,
                    QStringLiteral("Could not replace %1: %2")
                        .arg(path, rename_error),
                    error_message);
    }

    QFile::remove(temp_path);
    return fail(RbpError::IoError,
                QStringLiteral("Could not save %1: %2")
                    .arg(path, rename_error),
                error_message);
}

} // namespace detail

} // namespace ruby::project
