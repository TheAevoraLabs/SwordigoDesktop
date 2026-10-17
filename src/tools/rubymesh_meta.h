#pragma once
// rubymesh_meta.h — Non-destructive authoring metadata for Swordigo ground polygons.
//
// Stored as Protobuf Tag 15 (bytes) inside Caver::GroundPolygonComponent (Payload 110).
// Vanilla Swordigo ignores Tag 15 via WireFormatLite::SkipField (100% crash-proof).
// FileRift recognizes Tag 15 as RubyMeshMeta and round-trips it verbatim.
//
// Decouples authoring control geometry from procedural styling (Zypher bumps, bevels,
// spikes, bubbles) so the editor operates on clean baseline nodes with zero lag,
// zero micro-node clutter, and lossless, reversible style changes.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "platform/protobuf_reader.h"
#include "tools/boulder.h"

namespace rubymeta {

constexpr uint32_t kMagicRBM1 = 0x52424D31; // 'R', 'B', 'M', '1'
constexpr uint8_t  kVersion1  = 1;
constexpr int      kTagMeta   = 15;          // Protobuf Tag 15 in GroundPolygonComponent

enum GeneratorType : uint8_t {
    GenBoulder = 0,
    GenZenith  = 1,
    GenZypher  = 2
};

enum EditorSource : uint8_t {
    SourceRubyTouch = 0,
    SourceRubyGG    = 1,
    SourceCLI       = 2
};

struct RubyMeshMeta {
    uint8_t generator = GenZypher;
    uint8_t editor_source = SourceRubyTouch;

    // Zypher styling configuration
    std::string hat_style = "vanilla";
    std::string top_style = "plain";
    float top_frequency = 1.0f;
    float top_depth = 8.0f;
    float top_min_collision = 2.0f;

    // Boulder / Zenith configuration
    uint32_t random_seed = 1291618994u;
    float horiz_noise = 0.0f;
    uint8_t mesh_type = 1;
    float texture_scale = 250.0f;

    // Baseline authoring polygon (the clean un-deformed geometry)
    std::vector<boulder::PolygonPoint> baseline_points;
    std::vector<float> front_z;
    std::vector<float> back_z;
};

#pragma pack(push, 1)
struct RBM1Header {
    uint32_t magic;                 // 0x52424D31 ("RBM1")
    uint8_t  version;               // 1
    uint8_t  generator;             // 0=Boulder, 1=Zenith, 2=Zypher
    uint8_t  editor_source;         // 0=Touch, 1=GG, 2=CLI
    uint8_t  hat_style_len;
    uint8_t  top_style_len;
    float    top_frequency;
    float    top_depth;
    float    top_min_collision;
    uint32_t random_seed;
    float    horiz_noise;
    uint8_t  mesh_type;
    float    texture_scale;
    uint16_t point_count;
    uint8_t  has_relief;            // 1 if front_z & back_z are present
};
#pragma pack(pop)

inline std::string pack_rubymesh_meta(const RubyMeshMeta& meta) {
    RBM1Header hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.magic = kMagicRBM1;
    hdr.version = kVersion1;
    hdr.generator = meta.generator;
    hdr.editor_source = meta.editor_source;
    hdr.hat_style_len = static_cast<uint8_t>(std::min<size_t>(meta.hat_style.size(), 255));
    hdr.top_style_len = static_cast<uint8_t>(std::min<size_t>(meta.top_style.size(), 255));
    hdr.top_frequency = meta.top_frequency;
    hdr.top_depth = meta.top_depth;
    hdr.top_min_collision = meta.top_min_collision;
    hdr.random_seed = meta.random_seed;
    hdr.horiz_noise = meta.horiz_noise;
    hdr.mesh_type = static_cast<uint8_t>(meta.mesh_type);
    hdr.texture_scale = meta.texture_scale;
    hdr.point_count = static_cast<uint16_t>(std::min<size_t>(meta.baseline_points.size(), 65535));
    const bool have_relief = (meta.front_z.size() == meta.baseline_points.size() &&
                              meta.back_z.size() == meta.baseline_points.size() &&
                              !meta.baseline_points.empty());
    hdr.has_relief = have_relief ? 1 : 0;

    std::string out;
    out.reserve(sizeof(hdr) + hdr.hat_style_len + hdr.top_style_len + hdr.point_count * (have_relief ? 16 : 8));
    out.append(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.append(meta.hat_style.data(), hdr.hat_style_len);
    out.append(meta.top_style.data(), hdr.top_style_len);

    for (size_t i = 0; i < hdr.point_count; ++i) {
        float xy[2] = { static_cast<float>(meta.baseline_points[i].x), static_cast<float>(meta.baseline_points[i].y) };
        out.append(reinterpret_cast<const char*>(xy), sizeof(xy));
    }
    if (have_relief) {
        for (size_t i = 0; i < hdr.point_count; ++i) {
            float fz = meta.front_z[i];
            out.append(reinterpret_cast<const char*>(&fz), sizeof(float));
        }
        for (size_t i = 0; i < hdr.point_count; ++i) {
            float bz = meta.back_z[i];
            out.append(reinterpret_cast<const char*>(&bz), sizeof(float));
        }
    }
    return out;
}

inline bool unpack_rubymesh_meta(const std::string& bytes, RubyMeshMeta& out) {
    if (bytes.size() < sizeof(RBM1Header)) return false;
    RBM1Header hdr;
    std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    if (hdr.magic != kMagicRBM1 || hdr.version != kVersion1) return false;

    size_t expected = sizeof(hdr) + hdr.hat_style_len + hdr.top_style_len +
                      hdr.point_count * (hdr.has_relief ? 16 : 8);
    if (bytes.size() < expected) return false;

    out.generator = hdr.generator;
    out.editor_source = hdr.editor_source;
    out.top_frequency = hdr.top_frequency;
    out.top_depth = hdr.top_depth;
    out.top_min_collision = hdr.top_min_collision;
    out.random_seed = hdr.random_seed;
    out.horiz_noise = hdr.horiz_noise;
    out.mesh_type = hdr.mesh_type;
    out.texture_scale = hdr.texture_scale;

    size_t offset = sizeof(hdr);
    out.hat_style.assign(bytes.data() + offset, hdr.hat_style_len);
    offset += hdr.hat_style_len;
    out.top_style.assign(bytes.data() + offset, hdr.top_style_len);
    offset += hdr.top_style_len;

    out.baseline_points.clear();
    out.baseline_points.reserve(hdr.point_count);
    for (size_t i = 0; i < hdr.point_count; ++i) {
        float xy[2];
        std::memcpy(xy, bytes.data() + offset, sizeof(xy));
        offset += sizeof(xy);
        out.baseline_points.push_back({ static_cast<double>(xy[0]), static_cast<double>(xy[1]) });
    }

    out.front_z.clear();
    out.back_z.clear();
    if (hdr.has_relief) {
        out.front_z.reserve(hdr.point_count);
        for (size_t i = 0; i < hdr.point_count; ++i) {
            float fz;
            std::memcpy(&fz, bytes.data() + offset, sizeof(float));
            offset += sizeof(float);
            out.front_z.push_back(fz);
        }
        out.back_z.reserve(hdr.point_count);
        for (size_t i = 0; i < hdr.point_count; ++i) {
            float bz;
            std::memcpy(&bz, bytes.data() + offset, sizeof(float));
            offset += sizeof(float);
            out.back_z.push_back(bz);
        }
    }
    return true;
}

// Search for Tag 15 inside GroundPolygonComponent payload (110)
inline bool extract_rubymesh_meta(const std::string& comp_raw_data, RubyMeshMeta& out_meta) {
    try {
        proto::Reader reader(comp_raw_data);
        proto::Field f;
        while (reader.read_field(f)) {
            if (f.field_number == 110 && f.wire_type == proto::WIRE_LEN) {
                proto::Reader gpc_reader(f.bytes_val);
                proto::Field gf;
                while (gpc_reader.read_field(gf)) {
                    if (gf.field_number == kTagMeta && gf.wire_type == proto::WIRE_LEN) {
                        return unpack_rubymesh_meta(gf.bytes_val, out_meta);
                    }
                }
            }
        }
    } catch (...) {}
    return false;
}

// Injects or replaces Tag 15 inside GroundPolygonComponent payload (110)
inline std::string inject_rubymesh_meta(const std::string& comp_raw_data, const std::string& meta_bytes) {
    try {
        proto::Reader reader(comp_raw_data);
        proto::Field f;
        proto::Writer w;
        bool found_gpc = false;
        while (reader.read_field(f)) {
            if (f.field_number == 110 && f.wire_type == proto::WIRE_LEN) {
                found_gpc = true;
                proto::Reader gpc_reader(f.bytes_val);
                proto::Field gf;
                proto::Writer gpc_writer;
                while (gpc_reader.read_field(gf)) {
                    if (gf.field_number != kTagMeta) {
                        gpc_writer.write_field(gf);
                    }
                }
                if (!meta_bytes.empty()) {
                    gpc_writer.write_bytes_field(kTagMeta, meta_bytes);
                }
                w.write_nested_field(110, gpc_writer);
            } else {
                w.write_field(f);
            }
        }
        if (found_gpc) return w.to_string();
    } catch (...) {}
    return comp_raw_data;
}

} // namespace rubymeta
