#pragma once
// ============================================================================
// vanilla_manifest.h - Vanilla asset baseline lookup.
//
// The baseline maps a path relative to project/ (for example "levels/level1.pod")
// to the xxHash64 of the pristine Swordigo asset. It is loaded from the
// embedded resource :/assets/vanilla_hashes.bin, which uses this layout (all
// integers little-endian):
//
//   Header (16 bytes)
//     char[4]  magic          'V', 'H', 'B', '1'
//     uint32_t format_version 1
//     uint32_t entry_count
//     uint32_t reserved       0
//
//   Entry (repeated entry_count times)
//     uint16_t byte_length + raw UTF-8 path (no null terminator)
//     uint64_t file_size
//     uint64_t file_hash      xxHash64 of the vanilla file content
//
// The baseline is read-only after it is installed, but lookups happen from the
// diff worker thread while the UI thread may reload it, so every accessor is
// guarded by a read/write lock. The scanner uses snapshot() to take a cheap,
// immutable copy for one scan, which avoids holding the lock for a whole scan.
//
// A manifest entry always has a path. Its hash may be unknown (zero) when the
// baseline was seeded from the built-in path table instead of the real APK
// data; is_vanilla_file() reports false in that case so an unverified file is
// never silently excluded from an export.
// ============================================================================

#include <QByteArray>
#include <QHash>
#include <QReadWriteLock>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace ruby::diff {

inline constexpr quint32 kVanillaManifestMagic = 0x56484231u;  // 'VHB1'
inline constexpr quint32 kVanillaManifestVersion = 1;
inline constexpr quint32 kVanillaManifestHeaderSize = 16;

// Defensive limits. The real baseline describes roughly 1,200 assets.
inline constexpr qint64 kVanillaManifestMaxFileSize = 32 * 1024 * 1024;
inline constexpr quint32 kVanillaManifestMaxEntries = 500000;
inline constexpr int kVanillaManifestMaxPathBytes = 0xFFFF;

struct VanillaAsset {
    QString rel_path;
    quint64 file_size = 0;
    quint64 file_hash = 0;
};

// Immutable lookup copy handed to a scan. Value semantics keep worker threads
// independent from later manifest reloads.
struct VanillaSnapshot {
    QHash<QString, quint64> hashes;        // exact relative path -> hash
    QHash<QString, QString> canonical_paths; // case-folded path -> exact path
    quint64 manifest_hash = 0;

    bool empty() const { return hashes.isEmpty(); }
    int size() const { return hashes.size(); }

    // Path membership. An exact match wins; a case-insensitive fallback still
    // reports a path whose casing differs from the vanilla spelling. Use
    // canonical_path() to find that spelling.
    bool contains_path(const QString& rel_path) const;

    // Hash for a path, case-insensitive fallback included. Returns 0 when the
    // path is unknown (xxHash64 of empty content is never 0, so 0 is safe as a
    // sentinel here).
    quint64 vanilla_hash(const QString& rel_path) const;

    // True only when the path is known and its baseline hash equals `hash`.
    bool is_vanilla_file(const QString& rel_path, quint64 hash) const;

    // Vanilla spelling of the path when it matches case-insensitively, or an
    // empty string when the path is not in the baseline at all.
    QString canonical_path(const QString& rel_path) const;
};

class VanillaManifest {
public:
    VanillaManifest();
    ~VanillaManifest() = default;

    VanillaManifest(const VanillaManifest&) = delete;
    VanillaManifest& operator=(const VanillaManifest&) = delete;

    // Replaces the table from a VHB1 file (QFile accepts resource paths such as
    // ":/assets/vanilla_hashes.bin"). The table is unchanged on failure.
    bool load_from_file(const QString& path, QString* error_message = nullptr);
    bool load_from_bytes(const QByteArray& bytes, QString* error_message = nullptr);

    // Replaces the table with `entries`. Empty paths are ignored, duplicate
    // paths keep the first occurrence, and the manifest hash is recomputed.
    void set_entries(const QVector<VanillaAsset>& entries);

    // Installs the built-in path table. The shipped table has no content
    // hashes because the real baseline is generated from the vanilla APK, so
    // these entries answer contains_path() but not is_vanilla_file().
    void seed_defaults();

    void clear();

    bool is_empty() const;
    int size() const;

    // Stable xxHash64 of the table contents. Written into diff_cache.bin so a
    // later build can notice that the baseline itself changed.
    quint64 manifest_hash() const;

    bool is_vanilla_file(const QString& rel_path, quint64 hash) const;
    quint64 vanilla_hash(const QString& rel_path) const;
    bool contains_path(const QString& rel_path) const;
    QString canonical_path(const QString& rel_path) const;

    VanillaSnapshot snapshot() const;

    // Normalizes a path to '/'-separated form without "./" or a leading '/'.
    // Returns an empty string for an empty path.
    static QString normalize_path(const QString& path);

    // Embedded baseline location: :/assets/vanilla_hashes.bin
    static QString default_resource_path();

private:
    mutable QReadWriteLock m_lock;
    VanillaSnapshot m_data;
};

} // namespace ruby::diff
