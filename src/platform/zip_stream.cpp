/* zip_stream.cpp — see zip_stream.h for provenance and layout notes. */

#include "platform/zip_stream.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace zipstream {

namespace {

constexpr size_t kChunk = 256 * 1024;
constexpr uint32_t kMax32 = 0xFFFFFFFFu;
constexpr uint32_t kLocalSig   = 0x04034b50u;
constexpr uint32_t kCentralSig = 0x02014b50u;
constexpr uint32_t kEocdSig    = 0x06054b50u;
constexpr uint32_t kZip64EocdSig = 0x06064b50u;
constexpr uint32_t kZip64LocatorSig = 0x07064b50u;

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x & 0xff));
    v.push_back(uint8_t((x >> 8) & 0xff));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x & 0xff));
    v.push_back(uint8_t((x >> 8) & 0xff));
    v.push_back(uint8_t((x >> 16) & 0xff));
    v.push_back(uint8_t((x >> 24) & 0xff));
}
void put64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(uint8_t((x >> (8 * i)) & 0xff));
}
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p) {
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x |= uint64_t(p[i]) << (8 * i);
    return x;
}

void dosTime(std::time_t when, uint16_t& time, uint16_t& date) {
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &when);
#else
    localtime_r(&when, &tmv);
#endif
    time = uint16_t((uint16_t(tmv.tm_hour) << 11) | (uint16_t(tmv.tm_min) << 5) |
                    uint16_t(tmv.tm_sec / 2));
    date = uint16_t((uint16_t(tmv.tm_year + 1900 - 1980) << 9) |
                    (uint16_t(tmv.tm_mon + 1) << 5) | uint16_t(tmv.tm_mday));
}

/// Normalise an archive entry name: '/' separators, no drive/root prefix and
/// no way to escape the extraction root. Embedded ".." segments are resolved
/// by popping the previous component (the same rule tar uses), so
/// "a/../../etc/passwd" becomes "etc/passwd" instead of escaping.
std::string sanitizeEntryName(const std::string& name) {
    const bool trailingSlash = !name.empty()
                               && (name.back() == '/' || name.back() == '\\');
    std::vector<std::string> parts;
    std::string component;
    auto flush = [&]() {
        if (component == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!component.empty() && component != ".") {
            parts.push_back(component);
        }
        component.clear();
    };
    for (char c : name) {
        if (c == '\\' || c == '/') flush();
        else component.push_back(c);
    }
    flush();

    std::string cleaned;
    for (const auto& part : parts) {
        if (!cleaned.empty()) cleaned.push_back('/');
        cleaned += part;
    }
    if (trailingSlash && !cleaned.empty()) cleaned.push_back('/');
    return cleaned;
}

/// Pick a name that does not exist yet: "file.txt" -> "file (1).txt".
std::filesystem::path uniqueTarget(const std::filesystem::path& dir, const std::string& name) {
    std::filesystem::path candidate = dir / name;
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec)) return candidate;

    const std::filesystem::path p(name);
    const std::string stem = p.stem().string();
    const std::string ext = p.extension().string();
    for (int i = 1; i < 100000; ++i) {
        std::string alt = stem + " (" + std::to_string(i) + ")" + ext;
        candidate = dir / alt;
        if (!std::filesystem::exists(candidate, ec)) return candidate;
    }
    return dir / name;
}

/// Apply the Unix mode bits carried in the ZIP external attributes.
void applyUnixMode(const std::filesystem::path& path, uint32_t externalAttr) {
    const uint32_t mode = (externalAttr >> 16) & 0xFFFFu;
    if (mode == 0) return;
    if ((mode & 0111u) == 0) return;
    std::error_code ec;
    auto perms = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                 std::filesystem::perms::others_exec;
    std::filesystem::permissions(path, perms, std::filesystem::perm_options::add, ec);
}

struct CentralRecord {
    std::string name;
    uint16_t method = 8;
    uint16_t modTime = 0;
    uint16_t modDate = 0;
    uint32_t crc = 0;
    uint64_t compSize = 0;
    uint64_t uncompSize = 0;
    uint64_t localOffset = 0;
    uint32_t externalAttr = 0;
};

/// Serialize the ZIP64 extra field for the fields that overflow.
std::vector<uint8_t> zip64Extra(bool needUncomp, uint64_t uncomp,
                                bool needComp, uint64_t comp,
                                bool needOffset, uint64_t offset) {
    std::vector<uint8_t> payload;
    if (needUncomp) put64(payload, uncomp);
    if (needComp) put64(payload, comp);
    if (needOffset) put64(payload, offset);
    std::vector<uint8_t> extra;
    if (payload.empty()) return extra;
    put16(extra, 0x0001);
    put16(extra, uint16_t(payload.size()));
    extra.insert(extra.end(), payload.begin(), payload.end());
    return extra;
}

} // namespace

// ─── Writer ──────────────────────────────────────────────────────────────────

struct Writer::Impl {
    explicit Impl(const std::string& path) : outPath(path) {}

    std::string outPath;
    std::fstream out;
    std::vector<CentralRecord> records;
    uint64_t written = 0;
};

Writer::Writer(const std::string& outPath) : m_impl(new Impl(outPath)) {}

Writer::~Writer() {
    if (m_impl) {
        if (m_impl->out.is_open()) m_impl->out.close();
        delete m_impl;
    }
}

bool Writer::open(std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!m_impl) return fail("no writer state");
    m_impl->out.open(m_impl->outPath, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!m_impl->out.is_open()) return fail("cannot create archive");
    return true;
}

bool Writer::addDirectory(const std::string& entryName, uint32_t externalAttr,
                          const CancelFn& cancelled, std::string* error) {
    if (m_cancelled) return false;
    if (cancelled && cancelled()) {
        m_cancelled = true;
        return false;
    }
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!m_impl || !m_impl->out.is_open()) return fail("archive not open");

    std::string name = sanitizeEntryName(entryName);
    if (name.empty()) return true;
    if (name.back() != '/') name.push_back('/');

    uint16_t mtime = 0, mdate = 0;
    dosTime(std::time(nullptr), mtime, mdate);

    CentralRecord rec;
    rec.name = name;
    rec.method = 0;
    rec.modTime = mtime;
    rec.modDate = mdate;
    rec.crc = 0;
    rec.compSize = 0;
    rec.uncompSize = 0;
    rec.localOffset = m_impl->written;
    rec.externalAttr = externalAttr;

    std::vector<uint8_t> header;
    put32(header, kLocalSig);
    put16(header, 20);
    put16(header, 0);
    put16(header, 0);
    put16(header, mtime);
    put16(header, mdate);
    put32(header, 0);
    put32(header, 0);
    put32(header, 0);
    put16(header, uint16_t(name.size()));
    put16(header, 0);
    header.insert(header.end(), name.begin(), name.end());

    m_impl->out.seekp(std::streamoff(m_impl->written));
    m_impl->out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
    if (!m_impl->out.good()) return fail("write failed");
    m_impl->written += header.size();
    m_impl->records.push_back(std::move(rec));
    return true;
}

bool Writer::addFile(const std::string& entryName, const std::string& sourcePath,
                     int method, uint32_t externalAttr,
                     const CancelFn& cancelled, const ProgressFn& progress,
                     std::string* error) {
    if (m_cancelled) return false;
    if (cancelled && cancelled()) {
        m_cancelled = true;
        return false;
    }
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!m_impl || !m_impl->out.is_open()) return fail("archive not open");

    const std::string name = sanitizeEntryName(entryName);
    if (name.empty()) return fail("empty entry name");
    if (method != 0 && method != 8) method = 8;

    std::error_code ec;
    const uint64_t uncompSize = uint64_t(std::filesystem::file_size(sourcePath, ec));
    if (ec) return fail("cannot stat source file");

    std::ifstream in(sourcePath, std::ios::binary);
    if (!in.is_open()) return fail("cannot read source file");

    // Deflate can grow incompressible data slightly; reserve ZIP64 margin.
    const bool zip64Entry = uncompSize >= 0xFFFF0000ull;

    uint16_t mtime = 0, mdate = 0;
    std::time_t when = std::time(nullptr);
    {
        std::error_code tec;
        auto ft = std::filesystem::last_write_time(sourcePath, tec);
        if (!tec) {
            // file_clock -> system_clock is not portable; rely on now() for the
            // archive stamp. The on-disk file keeps its real mtime.
        }
    }
    dosTime(when, mtime, mdate);

    const uint64_t localOffset = m_impl->written;
    std::vector<uint8_t> extra;
    if (zip64Entry) {
        extra = zip64Extra(true, uncompSize, true, 0, false, 0);
    }

    // Local header, sizes/CRC patched in place once the payload is streamed.
    std::vector<uint8_t> header;
    put32(header, kLocalSig);
    put16(header, zip64Entry ? 45 : 20);
    put16(header, 0);
    put16(header, uint16_t(method));
    put16(header, mtime);
    put16(header, mdate);
    put32(header, 0);
    put32(header, zip64Entry ? kMax32 : 0);
    put32(header, zip64Entry ? kMax32 : 0);
    put16(header, uint16_t(name.size()));
    put16(header, uint16_t(extra.size()));
    header.insert(header.end(), name.begin(), name.end());
    header.insert(header.end(), extra.begin(), extra.end());

    m_impl->out.seekp(std::streamoff(m_impl->written));
    m_impl->out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
    if (!m_impl->out.good()) return fail("write failed");
    m_impl->written += header.size();

    uint32_t crc = 0;
    uint64_t compSize = 0;
    uint64_t processed = 0;

    std::vector<char> inBuf(kChunk);
    std::vector<char> outBuf(kChunk);

    if (method == 0) {
        while (in.good()) {
            if (cancelled && cancelled()) {
                m_cancelled = true;
                return false;
            }
            in.read(inBuf.data(), std::streamsize(inBuf.size()));
            const std::streamsize got = in.gcount();
            if (got <= 0) break;
            crc = uint32_t(crc32(crc, reinterpret_cast<const Bytef*>(inBuf.data()), uInt(got)));
            m_impl->out.write(inBuf.data(), got);
            if (!m_impl->out.good()) return fail("write failed");
            m_impl->written += uint64_t(got);
            processed += uint64_t(got);
            compSize += uint64_t(got);
            if (progress) progress(processed, uncompSize, name);
        }
    } else {
        z_stream zs;
        std::memset(&zs, 0, sizeof(zs));
        if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                         Z_DEFAULT_STRATEGY) != Z_OK) {
            return fail("deflate init failed");
        }

        bool ok = true;
        while (in.good()) {
            if (cancelled && cancelled()) {
                m_cancelled = true;
                ok = false;
                break;
            }
            in.read(inBuf.data(), std::streamsize(inBuf.size()));
            const std::streamsize got = in.gcount();
            if (got <= 0) break;
            crc = uint32_t(crc32(crc, reinterpret_cast<const Bytef*>(inBuf.data()), uInt(got)));
            processed += uint64_t(got);

            zs.next_in = reinterpret_cast<Bytef*>(inBuf.data());
            zs.avail_in = uInt(got);
            while (zs.avail_in > 0) {
                zs.next_out = reinterpret_cast<Bytef*>(outBuf.data());
                zs.avail_out = uInt(outBuf.size());
                const int rc = deflate(&zs, Z_NO_FLUSH);
                if (rc == Z_STREAM_ERROR) {
                    ok = false;
                    break;
                }
                const size_t produced = outBuf.size() - zs.avail_out;
                if (produced > 0) {
                    m_impl->out.write(outBuf.data(), std::streamsize(produced));
                    if (!m_impl->out.good()) {
                        ok = false;
                        break;
                    }
                    m_impl->written += produced;
                    compSize += produced;
                }
            }
            if (!ok) break;
            if (progress) progress(processed, uncompSize, name);
        }

        if (ok) {
            // Flush the deflate stream.
            int rc = Z_OK;
            while (rc != Z_STREAM_END) {
                zs.next_out = reinterpret_cast<Bytef*>(outBuf.data());
                zs.avail_out = uInt(outBuf.size());
                rc = deflate(&zs, Z_FINISH);
                if (rc == Z_STREAM_ERROR) {
                    ok = false;
                    break;
                }
                const size_t produced = outBuf.size() - zs.avail_out;
                if (produced > 0) {
                    m_impl->out.write(outBuf.data(), std::streamsize(produced));
                    if (!m_impl->out.good()) {
                        ok = false;
                        break;
                    }
                    m_impl->written += produced;
                    compSize += produced;
                }
            }
        }
        deflateEnd(&zs);
        if (!ok) return fail(m_cancelled ? "cancelled" : "compression failed");
    }

    // Patch the local header with the real CRC and sizes.
    const uint64_t endOffset = m_impl->written;
    m_impl->out.seekp(std::streamoff(localOffset + 14));
    std::vector<uint8_t> patch;
    put32(patch, crc);
    put32(patch, uint32_t(std::min<uint64_t>(compSize, kMax32)));
    put32(patch, uint32_t(std::min<uint64_t>(uncompSize, kMax32)));
    m_impl->out.write(reinterpret_cast<const char*>(patch.data()), std::streamsize(patch.size()));
    if (!m_impl->out.good()) return fail("write failed");

    if (zip64Entry) {
        // Overwrite the two placeholder 8-byte values in the local extra field.
        const uint64_t extraDataOffset = localOffset + 30 + name.size() + 4;
        m_impl->out.seekp(std::streamoff(extraDataOffset));
        std::vector<uint8_t> sizes;
        put64(sizes, uncompSize);
        put64(sizes, compSize);
        m_impl->out.write(reinterpret_cast<const char*>(sizes.data()), std::streamsize(sizes.size()));
        if (!m_impl->out.good()) return fail("write failed");
    }

    m_impl->out.seekp(std::streamoff(endOffset));

    CentralRecord rec;
    rec.name = name;
    rec.method = uint16_t(method);
    rec.modTime = mtime;
    rec.modDate = mdate;
    rec.crc = crc;
    rec.compSize = compSize;
    rec.uncompSize = uncompSize;
    rec.localOffset = localOffset;
    rec.externalAttr = externalAttr;
    m_impl->records.push_back(std::move(rec));
    return true;
}

bool Writer::close(std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (m_cancelled) return fail("cancelled");
    if (!m_impl) return fail("no writer state");
    if (!m_impl->out.is_open()) return fail("archive not open");

    const uint64_t cdOffset = m_impl->written;
    uint64_t cdSize = 0;

    for (const auto& rec : m_impl->records) {
        const bool needUncomp = rec.uncompSize > kMax32;
        const bool needComp = rec.compSize > kMax32;
        const bool needOffset = rec.localOffset > kMax32;
        const bool zip64 = needUncomp || needComp || needOffset;

        std::vector<uint8_t> extra = zip64
            ? zip64Extra(needUncomp, rec.uncompSize, needComp, rec.compSize,
                         needOffset, rec.localOffset)
            : std::vector<uint8_t>();

        std::vector<uint8_t> central;
        put32(central, kCentralSig);
        put16(central, zip64 ? 45 : 20);
        put16(central, zip64 ? 45 : 20);
        put16(central, 0);
        put16(central, rec.method);
        put16(central, rec.modTime);
        put16(central, rec.modDate);
        put32(central, rec.crc);
        put32(central, needComp ? kMax32 : uint32_t(rec.compSize));
        put32(central, needUncomp ? kMax32 : uint32_t(rec.uncompSize));
        put16(central, uint16_t(rec.name.size()));
        put16(central, uint16_t(extra.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, rec.externalAttr);
        put32(central, needOffset ? kMax32 : uint32_t(rec.localOffset));
        central.insert(central.end(), rec.name.begin(), rec.name.end());
        central.insert(central.end(), extra.begin(), extra.end());

        m_impl->out.seekp(std::streamoff(m_impl->written));
        m_impl->out.write(reinterpret_cast<const char*>(central.data()), std::streamsize(central.size()));
        if (!m_impl->out.good()) return fail("write failed");
        m_impl->written += central.size();
        cdSize += central.size();
    }

    const uint64_t entryCount = uint64_t(m_impl->records.size());
    const bool needZip64Eocd = entryCount > 0xFFFF || cdOffset > kMax32 || cdSize > kMax32;

    if (needZip64Eocd) {
        const uint64_t zip64EocdOffset = m_impl->written;
        std::vector<uint8_t> z;
        put32(z, kZip64EocdSig);
        put64(z, 44);            // size of remaining record
        put16(z, 45);
        put16(z, 45);
        put32(z, 0);
        put32(z, 0);
        put64(z, entryCount);
        put64(z, entryCount);
        put64(z, cdSize);
        put64(z, cdOffset);
        m_impl->out.seekp(std::streamoff(m_impl->written));
        m_impl->out.write(reinterpret_cast<const char*>(z.data()), std::streamsize(z.size()));
        if (!m_impl->out.good()) return fail("write failed");
        m_impl->written += z.size();

        std::vector<uint8_t> loc;
        put32(loc, kZip64LocatorSig);
        put32(loc, 0);
        put64(loc, zip64EocdOffset);
        put32(loc, 1);
        m_impl->out.write(reinterpret_cast<const char*>(loc.data()), std::streamsize(loc.size()));
        if (!m_impl->out.good()) return fail("write failed");
        m_impl->written += loc.size();
    }

    std::vector<uint8_t> eocd;
    put32(eocd, kEocdSig);
    put16(eocd, 0);
    put16(eocd, 0);
    put16(eocd, needZip64Eocd ? 0xFFFF : uint16_t(entryCount));
    put16(eocd, needZip64Eocd ? 0xFFFF : uint16_t(entryCount));
    put32(eocd, needZip64Eocd ? kMax32 : uint32_t(cdSize));
    put32(eocd, needZip64Eocd ? kMax32 : uint32_t(cdOffset));
    put16(eocd, 0);
    m_impl->out.write(reinterpret_cast<const char*>(eocd.data()), std::streamsize(eocd.size()));
    if (!m_impl->out.good()) return fail("write failed");
    m_impl->written += eocd.size();

    m_impl->out.flush();
    m_impl->out.close();
    return true;
}

// ─── Reader ──────────────────────────────────────────────────────────────────

namespace {

struct ParsedEntry {
    std::string name;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint64_t compSize = 0;
    uint64_t uncompSize = 0;
    uint64_t dataOffset = 0;
    uint32_t externalAttr = 0;
};

/// Parse the central directory without loading the archive body.
bool parseCentralDirectory(std::ifstream& in, uint64_t fileSize,
                           std::vector<ParsedEntry>& out, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (fileSize < 22) return fail("not a zip archive");

    const uint64_t tailLen = std::min<uint64_t>(fileSize, 65557);
    std::vector<uint8_t> tail(static_cast<size_t>(tailLen));
    in.clear();
    in.seekg(std::streamoff(fileSize - tailLen));
    in.read(reinterpret_cast<char*>(tail.data()), std::streamsize(tail.size()));
    if (in.gcount() != std::streamsize(tail.size())) return fail("read failed");

    size_t eocdPos = SIZE_MAX;
    for (size_t i = tail.size(); i-- > 0;) {
        if (i + 22 > tail.size()) continue;
        if (rd32(&tail[i]) == kEocdSig) {
            eocdPos = i;
            break;
        }
    }
    if (eocdPos == SIZE_MAX) return fail("end of central directory not found");

    const uint8_t* e = &tail[eocdPos];
    uint64_t entryCount = rd16(e + 10);
    uint64_t cdSize = rd32(e + 12);
    uint64_t cdOffset = rd32(e + 16);

    // ZIP64 EOCD locator sits directly before the EOCD when present.
    if (eocdPos >= 20) {
        const uint8_t* loc = &tail[eocdPos - 20];
        if (rd32(loc) == kZip64LocatorSig) {
            const uint64_t zip64Offset = rd64(loc + 8);
            if (zip64Offset + 56 <= fileSize) {
                uint8_t z[56];
                in.clear();
                in.seekg(std::streamoff(zip64Offset));
                in.read(reinterpret_cast<char*>(z), sizeof(z));
                if (in.gcount() == std::streamsize(sizeof(z)) && rd32(z) == kZip64EocdSig) {
                    entryCount = rd64(z + 32);
                    cdSize = rd64(z + 40);
                    cdOffset = rd64(z + 48);
                }
            }
        }
    }

    if (cdOffset > fileSize || cdSize > fileSize || cdOffset + cdSize > fileSize) {
        return fail("central directory out of range");
    }

    std::vector<uint8_t> cd(static_cast<size_t>(cdSize));
    in.clear();
    in.seekg(std::streamoff(cdOffset));
    in.read(reinterpret_cast<char*>(cd.data()), std::streamsize(cd.size()));
    if (in.gcount() != std::streamsize(cd.size())) return fail("read failed");

    size_t pos = 0;
    for (uint64_t n = 0; n < entryCount; ++n) {
        if (pos + 46 > cd.size()) return fail("truncated central directory");
        const uint8_t* c = &cd[pos];
        if (rd32(c) != kCentralSig) return fail("bad central directory signature");

        const uint16_t nameLen = rd16(c + 28);
        const uint16_t extraLen = rd16(c + 30);
        const uint16_t commentLen = rd16(c + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > cd.size()) return fail("truncated entry");

        ParsedEntry ent;
        ent.method = rd16(c + 10);
        ent.crc = rd32(c + 16);
        ent.compSize = rd32(c + 20);
        ent.uncompSize = rd32(c + 24);
        ent.externalAttr = rd32(c + 38);
        uint64_t localOffset = rd32(c + 42);
        ent.name.assign(reinterpret_cast<const char*>(c + 46), nameLen);

        // Walk the extra fields for the ZIP64 record.
        size_t xoff = 46 + nameLen;
        size_t xend = xoff + extraLen;
        while (xoff + 4 <= xend) {
            const uint16_t id = rd16(&cd[xoff]);
            const uint16_t sz = rd16(&cd[xoff + 2]);
            if (xoff + 4 + sz > xend) break;
            if (id == 0x0001) {
                size_t p = xoff + 4;
                const size_t pend = p + sz;
                if (ent.uncompSize == kMax32 && p + 8 <= pend) {
                    ent.uncompSize = rd64(&cd[p]);
                    p += 8;
                }
                if (ent.compSize == kMax32 && p + 8 <= pend) {
                    ent.compSize = rd64(&cd[p]);
                    p += 8;
                }
                if (localOffset == kMax32 && p + 8 <= pend) {
                    localOffset = rd64(&cd[p]);
                    p += 8;
                }
            }
            xoff += 4 + sz;
        }
        pos += 46 + nameLen + extraLen + commentLen;

        // Resolve the real data offset from the local header.
        if (localOffset + 30 > fileSize) return fail("local header out of range");
        uint8_t lh[30];
        in.clear();
        in.seekg(std::streamoff(localOffset));
        in.read(reinterpret_cast<char*>(lh), sizeof(lh));
        if (in.gcount() != std::streamsize(sizeof(lh)) || rd32(lh) != kLocalSig) {
            return fail("bad local header");
        }
        const uint16_t lname = rd16(lh + 26);
        const uint16_t lextra = rd16(lh + 28);
        ent.dataOffset = localOffset + 30 + lname + lextra;
        out.push_back(std::move(ent));
    }
    return true;
}

} // namespace

bool listEntries(const std::string& zipPath, std::vector<EntryInfo>& out, std::string* error) {
    std::ifstream in(zipPath, std::ios::binary);
    if (!in.is_open()) {
        if (error) *error = "cannot open archive";
        return false;
    }
    in.seekg(0, std::ios::end);
    const uint64_t size = uint64_t(in.tellg());
    in.seekg(0);

    std::vector<ParsedEntry> parsed;
    if (!parseCentralDirectory(in, size, parsed, error)) return false;

    out.clear();
    out.reserve(parsed.size());
    for (auto& p : parsed) {
        EntryInfo info;
        info.name = p.name;
        info.uncompSize = p.uncompSize;
        info.compSize = p.compSize;
        info.method = p.method;
        info.externalAttr = p.externalAttr;
        out.push_back(std::move(info));
    }
    return true;
}

ExtractResult extractAll(const std::string& zipPath, const std::string& destDir,
                         bool overwrite,
                         const ProgressFn& progress, const CancelFn& cancelled,
                         const ConflictFn& conflict) {
    ExtractResult res;

    std::ifstream in(zipPath, std::ios::binary);
    if (!in.is_open()) {
        res.error = "cannot open archive";
        return res;
    }
    in.seekg(0, std::ios::end);
    const uint64_t fileSize = uint64_t(in.tellg());
    in.seekg(0);

    std::vector<ParsedEntry> entries;
    if (!parseCentralDirectory(in, fileSize, entries, &res.error)) return res;

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    if (ec && !std::filesystem::is_directory(destDir)) {
        res.error = "cannot create destination directory";
        return res;
    }

    uint64_t total = 0;
    for (const auto& e : entries) {
        if (!e.name.empty() && e.name.back() != '/') total += e.uncompSize;
    }

    uint64_t done = 0;
    std::vector<char> inBuf(kChunk);
    std::vector<char> outBuf(kChunk);

    for (const auto& e : entries) {
        if (cancelled && cancelled()) {
            res.cancelled = true;
            res.error = "cancelled";
            return res;
        }

        const std::string name = sanitizeEntryName(e.name);
        if (name.empty()) continue;

        std::filesystem::path target = std::filesystem::path(destDir) / name;
        const bool isDirEntry = (!name.empty() && name.back() == '/');

        if (isDirEntry) {
            if (std::filesystem::exists(target, ec) && !std::filesystem::is_directory(target, ec)) {
                if (conflict) {
                    const ConflictDecision d = conflict(name, target.string(), true);
                    if (d == ConflictDecision::Cancel) {
                        res.cancelled = true;
                        res.error = "cancelled";
                        return res;
                    }
                    if (d == ConflictDecision::Skip) {
                        ++res.skipped;
                        continue;
                    }
                    if (d == ConflictDecision::KeepBoth) {
                        target = uniqueTarget(target.parent_path(), target.filename().string());
                    } else {
                        std::filesystem::remove_all(target, ec);
                    }
                } else if (!overwrite) {
                    target = uniqueTarget(target.parent_path(), target.filename().string());
                } else {
                    res.failed.push_back(name);
                    continue;
                }
            }
            std::filesystem::create_directories(target, ec);
            applyUnixMode(target, e.externalAttr);
            continue;
        }

        std::filesystem::create_directories(target.parent_path(), ec);
        if (std::filesystem::exists(target, ec)) {
            if (conflict) {
                const ConflictDecision d = conflict(name, target.string(), false);
                if (d == ConflictDecision::Cancel) {
                    res.cancelled = true;
                    res.error = "cancelled";
                    return res;
                }
                if (d == ConflictDecision::Skip) {
                    ++res.skipped;
                    done += e.uncompSize;
                    if (progress) progress(done, total, name);
                    continue;
                }
                if (d == ConflictDecision::KeepBoth) {
                    target = uniqueTarget(target.parent_path(), target.filename().string());
                } else if (std::filesystem::is_directory(target, ec)) {
                    std::filesystem::remove_all(target, ec);
                }
            } else if (!overwrite) {
                target = uniqueTarget(target.parent_path(), target.filename().string());
            } else if (std::filesystem::is_directory(target, ec)) {
                res.failed.push_back(name);
                continue;
            }
        }

        const bool stored = (e.method == 0);
        if (!stored && e.method != 8) {
            res.failed.push_back(name);
            continue;
        }

        in.clear();
        in.seekg(std::streamoff(e.dataOffset));
        if (!in.good()) {
            res.failed.push_back(name);
            continue;
        }

        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            res.failed.push_back(name);
            continue;
        }

        bool entryOk = true;
        uint64_t written = 0;
        uint32_t crc = 0;
        uint64_t remaining = e.compSize;

        if (stored) {
            while (remaining > 0) {
                if (cancelled && cancelled()) {
                    res.cancelled = true;
                    entryOk = false;
                    break;
                }
                const size_t want = size_t(std::min<uint64_t>(remaining, inBuf.size()));
                in.read(inBuf.data(), std::streamsize(want));
                const std::streamsize got = in.gcount();
                if (got <= 0) {
                    entryOk = false;
                    break;
                }
                crc = uint32_t(crc32(crc, reinterpret_cast<const Bytef*>(inBuf.data()), uInt(got)));
                out.write(inBuf.data(), got);
                if (!out.good()) {
                    entryOk = false;
                    break;
                }
                remaining -= uint64_t(got);
                written += uint64_t(got);
                done += uint64_t(got);
                if (progress) progress(done, total, name);
            }
        } else {
            z_stream zs;
            std::memset(&zs, 0, sizeof(zs));
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
                entryOk = false;
            } else {
                int rc = Z_OK;
                while (remaining > 0 && rc != Z_STREAM_END) {
                    if (cancelled && cancelled()) {
                        res.cancelled = true;
                        entryOk = false;
                        break;
                    }
                    const size_t want = size_t(std::min<uint64_t>(remaining, inBuf.size()));
                    in.read(inBuf.data(), std::streamsize(want));
                    const std::streamsize got = in.gcount();
                    if (got <= 0) {
                        entryOk = false;
                        break;
                    }
                    remaining -= uint64_t(got);
                    zs.next_in = reinterpret_cast<Bytef*>(inBuf.data());
                    zs.avail_in = uInt(got);
                    while (zs.avail_in > 0) {
                        zs.next_out = reinterpret_cast<Bytef*>(outBuf.data());
                        zs.avail_out = uInt(outBuf.size());
                        rc = inflate(&zs, Z_NO_FLUSH);
                        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR) {
                            entryOk = false;
                            break;
                        }
                        const size_t produced = outBuf.size() - zs.avail_out;
                        if (produced > 0) {
                            crc = uint32_t(crc32(crc, reinterpret_cast<const Bytef*>(outBuf.data()),
                                                 uInt(produced)));
                            out.write(outBuf.data(), std::streamsize(produced));
                            if (!out.good()) {
                                entryOk = false;
                                break;
                            }
                            written += produced;
                            done += produced;
                            if (progress) progress(done, total, name);
                        }
                        if (rc == Z_STREAM_END) break;
                        if (rc == Z_BUF_ERROR && zs.avail_in == 0) break;
                    }
                    if (!entryOk) break;
                }
                inflateEnd(&zs);
            }
        }

        out.flush();
        out.close();

        if (!entryOk) {
            std::filesystem::remove(target, ec);
            if (res.cancelled) {
                res.error = "cancelled";
                return res;
            }
            res.failed.push_back(name);
            continue;
        }
        if (written != e.uncompSize || crc != e.crc) {
            std::filesystem::remove(target, ec);
            res.failed.push_back(name);
            continue;
        }
        applyUnixMode(target, e.externalAttr);
        res.bytesWritten += written;
        ++res.extracted;
    }

    res.ok = true;
    return res;
}

} // namespace zipstream
