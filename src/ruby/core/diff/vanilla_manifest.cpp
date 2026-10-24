// ============================================================================
// vanilla_manifest.cpp - Vanilla baseline parser and lookup table.
// ============================================================================

#include "vanilla_manifest.h"

#include "diff_hash.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>

namespace ruby::diff {

namespace {

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

bool decode_utf8(const QByteArray& bytes, QString& out, QString* error_message) {
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        set_error(error_message,
                  QStringLiteral("Baseline path is not valid UTF-8"));
        return false;
    }
    out = decoded;
    return true;
}

quint64 compute_manifest_hash(const QHash<QString, quint64>& hashes) {
    QStringList paths = hashes.keys();
    paths.sort();

    Xxh64 state;
    for (const QString& path : paths) {
        const QByteArray utf8 = path.toUtf8();
        state.update(utf8.constData(), utf8.size());
        const quint8 separator = 0;
        state.update(&separator, 1);

        const quint64 value = hashes.value(path);
        quint8 encoded[8] = {};
        for (int index = 0; index < 8; ++index) {
            encoded[index] =
                static_cast<quint8>((value >> (index * 8)) & 0xFFull);
        }
        state.update(encoded, 8);
    }
    return state.digest();
}

// Exact match first, then the case-insensitive canonical spelling. `canonical`
// receives the vanilla path spelling, which lets the scanner warn about casing.
bool lookup_hash(const VanillaSnapshot& data, const QString& rel_path,
                 quint64& hash, QString* canonical) {
    const QString path = VanillaManifest::normalize_path(rel_path);
    if (path.isEmpty()) {
        return false;
    }

    const auto exact = data.hashes.constFind(path);
    if (exact != data.hashes.constEnd()) {
        hash = exact.value();
        if (canonical) {
            *canonical = path;
        }
        return true;
    }

    const auto folded = data.canonical_paths.constFind(path.toCaseFolded());
    if (folded == data.canonical_paths.constEnd()) {
        return false;
    }
    const auto found = data.hashes.constFind(folded.value());
    if (found == data.hashes.constEnd()) {
        return false;
    }

    hash = found.value();
    if (canonical) {
        *canonical = folded.value();
    }
    return true;
}

} // namespace

QString VanillaManifest::normalize_path(const QString& path) {
    if (path.isEmpty()) {
        return QString();
    }

    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    while (normalized.startsWith(QStringLiteral("./"))) {
        normalized.remove(0, 2);
    }
    while (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    if (normalized == QLatin1String(".")) {
        return QString();
    }
    return normalized;
}

QString VanillaManifest::default_resource_path() {
    return QStringLiteral(":/assets/vanilla_hashes.bin");
}

bool VanillaSnapshot::contains_path(const QString& rel_path) const {
    quint64 ignored = 0;
    return lookup_hash(*this, rel_path, ignored, nullptr);
}

quint64 VanillaSnapshot::vanilla_hash(const QString& rel_path) const {
    quint64 hash = 0;
    if (!lookup_hash(*this, rel_path, hash, nullptr)) {
        return 0;
    }
    return hash;
}

bool VanillaSnapshot::is_vanilla_file(const QString& rel_path,
                                      quint64 hash) const {
    quint64 expected = 0;
    if (!lookup_hash(*this, rel_path, expected, nullptr)) {
        return false;
    }
    return expected != 0 && expected == hash;
}

QString VanillaSnapshot::canonical_path(const QString& rel_path) const {
    quint64 ignored = 0;
    QString canonical;
    if (!lookup_hash(*this, rel_path, ignored, &canonical)) {
        return QString();
    }
    return canonical;
}

VanillaManifest::VanillaManifest() = default;

bool VanillaManifest::load_from_bytes(const QByteArray& bytes,
                                      QString* error_message) {
    if (bytes.size() < static_cast<qsizetype>(kVanillaManifestHeaderSize)) {
        set_error(error_message,
                  QStringLiteral("Vanilla baseline is shorter than its "
                                 "16-byte header"));
        return false;
    }

    QDataStream stream(bytes);
    stream.setByteOrder(QDataStream::LittleEndian);

    char magic[4] = {};
    if (stream.readRawData(magic, 4) != 4) {
        set_error(error_message,
                  QStringLiteral("Vanilla baseline header is truncated"));
        return false;
    }
    if (decode_fourcc(magic) != kVanillaManifestMagic) {
        set_error(error_message,
                  QStringLiteral("Not a vanilla baseline (expected magic VHB1)"));
        return false;
    }

    quint32 version = 0;
    quint32 count = 0;
    quint32 reserved = 0;
    stream >> version >> count >> reserved;
    if (stream.status() != QDataStream::Ok) {
        set_error(error_message,
                  QStringLiteral("Vanilla baseline header is truncated"));
        return false;
    }
    if (version != kVanillaManifestVersion) {
        set_error(error_message,
                  QStringLiteral("Unsupported vanilla baseline version %1 "
                                 "(expected %2)")
                      .arg(version)
                      .arg(kVanillaManifestVersion));
        return false;
    }
    if (count > kVanillaManifestMaxEntries) {
        set_error(error_message,
                  QStringLiteral("Vanilla baseline declares %1 entries; the "
                                 "limit is %2")
                      .arg(count)
                      .arg(kVanillaManifestMaxEntries));
        return false;
    }

    QVector<VanillaAsset> entries;
    entries.reserve(static_cast<int>(count));

    for (quint32 index = 0; index < count; ++index) {
        quint16 path_length = 0;
        stream >> path_length;
        if (stream.status() != QDataStream::Ok) {
            set_error(error_message,
                      QStringLiteral("Vanilla baseline is truncated at entry %1")
                          .arg(index));
            return false;
        }
        if (path_length == 0) {
            set_error(error_message,
                      QStringLiteral("Vanilla baseline entry %1 has an empty "
                                     "path")
                          .arg(index));
            return false;
        }

        QIODevice* device = stream.device();
        if (!device || device->bytesAvailable() < path_length + 16) {
            set_error(error_message,
                      QStringLiteral("Vanilla baseline entry %1 is truncated")
                          .arg(index));
            return false;
        }

        QByteArray path_bytes(path_length, Qt::Uninitialized);
        if (stream.readRawData(path_bytes.data(), path_length) != path_length) {
            set_error(error_message,
                      QStringLiteral("Vanilla baseline entry %1 is truncated")
                          .arg(index));
            return false;
        }

        VanillaAsset asset;
        if (!decode_utf8(path_bytes, asset.rel_path, error_message)) {
            return false;
        }

        stream >> asset.file_size >> asset.file_hash;
        if (stream.status() != QDataStream::Ok) {
            set_error(error_message,
                      QStringLiteral("Vanilla baseline entry %1 is truncated")
                          .arg(index));
            return false;
        }
        entries.append(asset);
    }

    set_entries(entries);
    return true;
}

bool VanillaManifest::load_from_file(const QString& path,
                                     QString* error_message) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        set_error(error_message,
                  QStringLiteral("Vanilla baseline not found: %1").arg(path));
        return false;
    }
    if (info.size() > kVanillaManifestMaxFileSize) {
        set_error(error_message,
                  QStringLiteral("Vanilla baseline is %1 bytes; the limit is %2")
                      .arg(info.size())
                      .arg(kVanillaManifestMaxFileSize));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        set_error(error_message,
                  QStringLiteral("Cannot open %1: %2")
                      .arg(path, file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        set_error(error_message,
                  QStringLiteral("Cannot read %1: %2")
                      .arg(path, file.errorString()));
        return false;
    }

    QString parse_error;
    if (!load_from_bytes(bytes, &parse_error)) {
        set_error(error_message,
                  QStringLiteral("%1: %2").arg(path, parse_error));
        return false;
    }
    return true;
}

void VanillaManifest::set_entries(const QVector<VanillaAsset>& entries) {
    VanillaSnapshot snapshot;
    snapshot.hashes.reserve(entries.size());
    snapshot.canonical_paths.reserve(entries.size());

    for (const VanillaAsset& asset : entries) {
        const QString path = normalize_path(asset.rel_path);
        if (path.isEmpty() || snapshot.hashes.contains(path)) {
            continue;
        }
        snapshot.hashes.insert(path, asset.file_hash);
        const QString folded = path.toCaseFolded();
        if (!snapshot.canonical_paths.contains(folded)) {
            snapshot.canonical_paths.insert(folded, path);
        }
    }

    snapshot.manifest_hash = compute_manifest_hash(snapshot.hashes);

    QWriteLocker locker(&m_lock);
    m_data = snapshot;
}

void VanillaManifest::seed_defaults() {
    // Path-only placeholders. Real hashes come from the generated baseline;
    // until then these entries only establish that the paths are vanilla.
    set_entries({
        VanillaAsset{QStringLiteral("hiro.POD"), 0, 0},
        VanillaAsset{QStringLiteral("data/Level1.scene"), 0, 0},
        VanillaAsset{QStringLiteral("levels/level1.pod"), 0, 0},
    });
}

void VanillaManifest::clear() {
    QWriteLocker locker(&m_lock);
    m_data = VanillaSnapshot();
}

bool VanillaManifest::is_empty() const {
    QReadLocker locker(&m_lock);
    return m_data.empty();
}

int VanillaManifest::size() const {
    QReadLocker locker(&m_lock);
    return m_data.size();
}

quint64 VanillaManifest::manifest_hash() const {
    QReadLocker locker(&m_lock);
    return m_data.manifest_hash;
}

bool VanillaManifest::is_vanilla_file(const QString& rel_path,
                                      quint64 hash) const {
    QReadLocker locker(&m_lock);
    return m_data.is_vanilla_file(rel_path, hash);
}

quint64 VanillaManifest::vanilla_hash(const QString& rel_path) const {
    QReadLocker locker(&m_lock);
    return m_data.vanilla_hash(rel_path);
}

bool VanillaManifest::contains_path(const QString& rel_path) const {
    QReadLocker locker(&m_lock);
    return m_data.contains_path(rel_path);
}

QString VanillaManifest::canonical_path(const QString& rel_path) const {
    QReadLocker locker(&m_lock);
    return m_data.canonical_path(rel_path);
}

VanillaSnapshot VanillaManifest::snapshot() const {
    QReadLocker locker(&m_lock);
    return m_data;
}

} // namespace ruby::diff
