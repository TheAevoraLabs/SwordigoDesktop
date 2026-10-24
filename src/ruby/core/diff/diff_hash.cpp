// ============================================================================
// diff_hash.cpp - xxHash64 implementation.
//
// The algorithm is byte-oriented and endian independent: every multi-byte read
// is assembled from individual bytes, so arm64-v8a, armeabi-v7a, and x86_64
// produce the same digest for the same input.
// ============================================================================

#include "diff_hash.h"

#include <QFile>

#include <cstring>

namespace ruby::diff {

namespace {

constexpr quint64 kPrime1 = 0x9E3779B185EBCA87ull;
constexpr quint64 kPrime2 = 0xC2B2AE3D27D4EB4Full;
constexpr quint64 kPrime3 = 0x165667B19E3779F9ull;
constexpr quint64 kPrime4 = 0x85EBCA77C2B2AE63ull;
constexpr quint64 kPrime5 = 0x27D4EB2F165667C5ull;

constexpr qsizetype kStripeBytes = 32;
constexpr qsizetype kHashBufferBytes = 256 * 1024;

inline quint64 rotl64(quint64 value, int bits) {
    return (value << bits) | (value >> (64 - bits));
}

inline quint64 read_u64_le(const quint8* bytes) {
    return quint64(bytes[0]) |
           (quint64(bytes[1]) << 8) |
           (quint64(bytes[2]) << 16) |
           (quint64(bytes[3]) << 24) |
           (quint64(bytes[4]) << 32) |
           (quint64(bytes[5]) << 40) |
           (quint64(bytes[6]) << 48) |
           (quint64(bytes[7]) << 56);
}

inline quint32 read_u32_le(const quint8* bytes) {
    return quint32(bytes[0]) |
           (quint32(bytes[1]) << 8) |
           (quint32(bytes[2]) << 16) |
           (quint32(bytes[3]) << 24);
}

inline quint64 round64(quint64 accumulator, quint64 input) {
    accumulator += input * kPrime2;
    accumulator = rotl64(accumulator, 31);
    accumulator *= kPrime1;
    return accumulator;
}

inline quint64 merge_round(quint64 accumulator, quint64 lane) {
    accumulator ^= round64(0, lane);
    accumulator = accumulator * kPrime1 + kPrime4;
    return accumulator;
}

inline quint64 avalanche(quint64 hash) {
    hash ^= hash >> 33;
    hash *= kPrime2;
    hash ^= hash >> 29;
    hash *= kPrime3;
    hash ^= hash >> 32;
    return hash;
}

void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

} // namespace

Xxh64::Xxh64(quint64 seed) {
    reset(seed);
}

void Xxh64::reset(quint64 seed) {
    m_seed = seed;
    m_v1 = seed + kPrime1 + kPrime2;
    m_v2 = seed + kPrime2;
    m_v3 = seed;
    m_v4 = seed - kPrime1;
    m_total = 0;
    m_buffered = 0;
}

void Xxh64::consume_stripe(const quint8* stripe) {
    m_v1 = round64(m_v1, read_u64_le(stripe));
    m_v2 = round64(m_v2, read_u64_le(stripe + 8));
    m_v3 = round64(m_v3, read_u64_le(stripe + 16));
    m_v4 = round64(m_v4, read_u64_le(stripe + 24));
}

void Xxh64::update(const void* data, qsizetype length) {
    if (!data || length <= 0) {
        return;
    }

    const quint8* cursor = static_cast<const quint8*>(data);
    qsizetype remaining = length;
    m_total += static_cast<quint64>(length);

    if (m_buffered > 0) {
        const qsizetype needed = kStripeBytes - m_buffered;
        if (remaining < needed) {
            std::memcpy(m_buffer + m_buffered, cursor,
                        static_cast<size_t>(remaining));
            m_buffered += remaining;
            return;
        }
        std::memcpy(m_buffer + m_buffered, cursor, static_cast<size_t>(needed));
        consume_stripe(m_buffer);
        cursor += needed;
        remaining -= needed;
        m_buffered = 0;
    }

    while (remaining >= kStripeBytes) {
        consume_stripe(cursor);
        cursor += kStripeBytes;
        remaining -= kStripeBytes;
    }

    if (remaining > 0) {
        std::memcpy(m_buffer, cursor, static_cast<size_t>(remaining));
        m_buffered = remaining;
    }
}

quint64 Xxh64::digest() const {
    quint64 hash = 0;
    if (m_total >= static_cast<quint64>(kStripeBytes)) {
        hash = rotl64(m_v1, 1) + rotl64(m_v2, 7) + rotl64(m_v3, 12) +
               rotl64(m_v4, 18);
        hash = merge_round(hash, m_v1);
        hash = merge_round(hash, m_v2);
        hash = merge_round(hash, m_v3);
        hash = merge_round(hash, m_v4);
    } else {
        hash = m_seed + kPrime5;
    }

    hash += m_total;

    const quint8* cursor = m_buffer;
    qsizetype remaining = m_buffered;

    while (remaining >= 8) {
        hash ^= round64(0, read_u64_le(cursor));
        hash = rotl64(hash, 27) * kPrime1 + kPrime4;
        cursor += 8;
        remaining -= 8;
    }
    if (remaining >= 4) {
        hash ^= quint64(read_u32_le(cursor)) * kPrime1;
        hash = rotl64(hash, 23) * kPrime2 + kPrime3;
        cursor += 4;
        remaining -= 4;
    }
    while (remaining > 0) {
        hash ^= quint64(*cursor) * kPrime5;
        hash = rotl64(hash, 11) * kPrime1;
        ++cursor;
        --remaining;
    }

    return avalanche(hash);
}

quint64 Xxh64::hash(const void* data, qsizetype length, quint64 seed) {
    Xxh64 state(seed);
    state.update(data, length);
    return state.digest();
}

quint64 xxh64(const void* data, qsizetype length, quint64 seed) {
    return Xxh64::hash(data, length, seed);
}

quint64 xxh64(const QByteArray& data, quint64 seed) {
    return Xxh64::hash(data.constData(), data.size(), seed);
}

quint64 xxh64(const QString& text, quint64 seed) {
    const QByteArray utf8 = text.toUtf8();
    return Xxh64::hash(utf8.constData(), utf8.size(), seed);
}

bool xxh64_file(const QString& path, quint64& hash, QString* error_message,
                qint64* file_size) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        set_error(error_message,
                  QStringLiteral("Cannot open %1: %2")
                      .arg(path, file.errorString()));
        return false;
    }

    Xxh64 state;
    QByteArray buffer(kHashBufferBytes, Qt::Uninitialized);
    qint64 total = 0;

    for (;;) {
        const qint64 read = file.read(buffer.data(), buffer.size());
        if (read < 0) {
            set_error(error_message,
                      QStringLiteral("Cannot read %1: %2")
                          .arg(path, file.errorString()));
            return false;
        }
        if (read == 0) {
            break;
        }
        state.update(buffer.constData(), static_cast<qsizetype>(read));
        total += read;
    }

    if (file.error() != QFileDevice::NoError) {
        set_error(error_message,
                  QStringLiteral("Cannot read %1: %2")
                      .arg(path, file.errorString()));
        return false;
    }

    hash = state.digest();
    if (file_size) {
        *file_size = total;
    }
    return true;
}

} // namespace ruby::diff
