#pragma once
// ============================================================================
// diff_hash.h - Self-contained xxHash64 used by the asset diff engine.
//
// xxHash64 is the hash specified for .rbp/diff_cache.bin. This is a clean,
// dependency-free implementation of the public algorithm (seed, four 8-byte
// lanes, 32-byte stripes, little-endian reads) so Desktop and Android builds
// produce identical hashes without pulling a third-party library.
//
// Hash values are content hashes, not security hashes. They exist to detect
// asset edits, never to authenticate data.
// ============================================================================

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace ruby::diff {

// Streaming xxHash64 state. Feed data with update(), then call digest().
// digest() is const and may be called more than once; the state is unchanged.
class Xxh64 {
public:
    explicit Xxh64(quint64 seed = 0);
    ~Xxh64() = default;

    void reset(quint64 seed = 0);
    void update(const void* data, qsizetype length);
    void update(const QByteArray& data) { update(data.constData(), data.size()); }
    quint64 digest() const;

    static quint64 hash(const void* data, qsizetype length, quint64 seed = 0);

private:
    void consume_stripe(const quint8* stripe);

    quint64 m_seed = 0;
    quint64 m_v1 = 0;
    quint64 m_v2 = 0;
    quint64 m_v3 = 0;
    quint64 m_v4 = 0;
    quint64 m_total = 0;
    quint8 m_buffer[32] = {};
    qsizetype m_buffered = 0;
};

// Convenience one-shot hashes. Text is hashed as UTF-8.
quint64 xxh64(const void* data, qsizetype length, quint64 seed = 0);
quint64 xxh64(const QByteArray& data, quint64 seed = 0);
quint64 xxh64(const QString& text, quint64 seed = 0);

// Hashes a file by streaming it in bounded chunks. Returns false and leaves
// `hash` untouched when the file cannot be opened or read completely.
// `file_size` is optional and receives the number of bytes fed to the hash.
bool xxh64_file(const QString& path, quint64& hash, QString* error_message = nullptr,
                qint64* file_size = nullptr);

} // namespace ruby::diff
