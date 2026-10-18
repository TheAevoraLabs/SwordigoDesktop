#pragma once
// ==============================================================================
// rubywater_meta.h — Multi-Sheet Z Extrusion & Metadata for Swordigo WaterMesh
// ==============================================================================
//
// In vanilla Swordigo (libswordigo.so), Caver::WaterMeshComponent::CreateMesh
// hardcodes local vertex Z to +40.0f (front face) and -20.0f (top surface back edge),
// making every single native WaterMesh slab strictly 60 units thick in Z.
// Non-uniform scale_z is unsupported by the engine's scalar scaling pipeline.
//
// To achieve arbitrary fluid Z depth (e.g. 120, 180, 240+ units) in-game without engine
// modifications, Ruby uses automated recursive "Sheeting": tiling N vanilla-compatible
// WaterMesh companion objects along the Z axis spaced at exact intervals of 60 units.
//
// Stored as Protobuf Tag 15 (bytes) inside WaterMeshComponent (Payload 114),
// plus identified by the "__subz_f<N>" / "__subz_b<N>" naming convention.
// Vanilla Swordigo ignores Tag 15 via WireFormatLite::SkipField (100% crash-proof).
//
// In-game:
// - Front-most sheet (+sheets_front) renders the front fluid curtain (+40) and top surface.
// - All sheets behind it render the continuation of the top wave surface, with their front
//   curtain alpha set to 0.0 to prevent internal vertical partition walls.
// - All sheets share synchronized wave displacement, collision hazard boundaries,
//   and texture mapping with zero gaps and zero overlap.
//
// In Ruby Touch / Ruby Studio:
// - The user selects and manipulates the entire fluid volume as a single unified pool.
// - Modders can add/remove sheets to the front (+Z) and back (-Z) side recursively.
// - Master saves the front-most mesh Z and back-most water Z in field 15.
// ==============================================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace rubywater {

constexpr uint32_t kMagicRBW1      = 0x52425731; // 'R', 'B', 'W', '1'
constexpr uint8_t  kVersion2       = 2;
constexpr int      kTagWaterMeta   = 15;         // Protobuf Tag 15 in WaterMeshComponent
constexpr float    kSheetThickness = 60.0f;      // Vanilla native WaterMesh depth is 60.0f

struct WaterMeta {
    uint8_t     is_secondary = 0;       // 0 = Master water pool, 1 = Companion sheet
    int16_t     sheet_index  = 0;       // relative index: 0 = base master, +1..+F = front, -1..-B = back
    uint16_t    sheets_front = 0;       // count of sheets added in +Z direction
    uint16_t    sheets_back  = 0;       // count of sheets added in -Z direction
    uint16_t    total_sheets = 1;       // 1 + sheets_front + sheets_back
    float       total_depth  = 60.0f;   // total_sheets * 60.0f
    float       front_z      = 40.0f;   // front-most Z relative to master pos_z (sheets_front * 60.0f + 40.0f)
    float       back_z       = -20.0f;  // back-most Z relative to master pos_z (-sheets_back * 60.0f - 20.0f)
    std::string master_name;            // Name of the master SceneObject
};

#pragma pack(push, 1)
struct RBW2Header {
    uint32_t magic;             // 0x52425731 ("RBW1")
    uint8_t  version;           // 2
    uint8_t  is_secondary;      // 0 or 1
    int16_t  sheet_index;       // signed relative sheet index
    uint16_t sheets_front;      // count of front sheets
    uint16_t sheets_back;       // count of back sheets
    uint16_t total_sheets;      // 1 + sheets_front + sheets_back
    float    total_depth;       // total_sheets * 60.0f
    float    front_z;           // front-most extent relative to master
    float    back_z;            // back-most extent relative to master
    uint16_t master_name_len;
};

struct RBW1Header {
    uint32_t magic;
    uint8_t  version;
    uint8_t  is_secondary;
    uint16_t sheet_index;
    uint16_t total_sheets;
    float    total_depth;
    float    front_z;
    float    back_z;
    uint16_t master_name_len;
};
#pragma pack(pop)

inline std::string encode_water_meta(const WaterMeta& meta) {
    std::string out;
    RBW2Header hdr;
    hdr.magic = kMagicRBW1;
    hdr.version = kVersion2;
    hdr.is_secondary = meta.is_secondary;
    hdr.sheet_index = meta.sheet_index;
    hdr.sheets_front = meta.sheets_front;
    hdr.sheets_back = meta.sheets_back;
    hdr.total_sheets = meta.total_sheets;
    hdr.total_depth = meta.total_depth;
    hdr.front_z = meta.front_z;
    hdr.back_z = meta.back_z;
    hdr.master_name_len = static_cast<uint16_t>(meta.master_name.size());
    out.assign(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    out.append(meta.master_name);
    return out;
}

inline bool decode_water_meta(const std::string& bytes, WaterMeta& out) {
    if (bytes.size() < sizeof(RBW1Header)) return false;
    uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(magic));
    if (magic != kMagicRBW1) return false;
    uint8_t ver = static_cast<uint8_t>(bytes[4]);

    if (ver >= 2 && bytes.size() >= sizeof(RBW2Header)) {
        RBW2Header hdr;
        std::memcpy(&hdr, bytes.data(), sizeof(hdr));
        out.is_secondary = hdr.is_secondary;
        out.sheet_index = hdr.sheet_index;
        out.sheets_front = hdr.sheets_front;
        out.sheets_back = hdr.sheets_back;
        out.total_sheets = hdr.total_sheets;
        out.total_depth = hdr.total_depth;
        out.front_z = hdr.front_z;
        out.back_z = hdr.back_z;
        size_t name_len = hdr.master_name_len;
        if (sizeof(hdr) + name_len <= bytes.size()) {
            out.master_name.assign(bytes.data() + sizeof(hdr), name_len);
        }
        return true;
    } else if (bytes.size() >= sizeof(RBW1Header)) {
        RBW1Header hdr;
        std::memcpy(&hdr, bytes.data(), sizeof(hdr));
        out.is_secondary = hdr.is_secondary;
        out.sheet_index = static_cast<int16_t>(hdr.sheet_index);
        out.total_sheets = hdr.total_sheets;
        out.sheets_front = 0;
        out.sheets_back = (hdr.total_sheets > 1) ? (hdr.total_sheets - 1) : 0;
        out.total_depth = hdr.total_depth;
        out.front_z = hdr.front_z;
        out.back_z = hdr.back_z;
        size_t name_len = hdr.master_name_len;
        if (sizeof(hdr) + name_len <= bytes.size()) {
            out.master_name.assign(bytes.data() + sizeof(hdr), name_len);
        }
        return true;
    }
    return false;
}

inline bool is_secondary_name(const std::string& name, std::string* out_master = nullptr, int* out_k = nullptr) {
    size_t pos = name.rfind("__subz");
    if (pos != std::string::npos && pos > 0) {
        if (out_master) *out_master = name.substr(0, pos);
        if (out_k) {
            *out_k = 1;
            std::string sub = name.substr(pos + 6);
            if (sub.rfind("_f", 0) == 0) {
                try { *out_k = std::stoi(sub.substr(2)); } catch (...) {}
            } else if (sub.rfind("_b", 0) == 0) {
                try { *out_k = -std::stoi(sub.substr(2)); } catch (...) {}
            } else {
                try { *out_k = std::stoi(sub); } catch (...) {}
            }
        }
        return true;
    }
    return false;
}

inline std::string make_secondary_name(const std::string& master_name, int signed_k) {
    if (signed_k > 0) {
        return master_name + "__subz_f" + std::to_string(signed_k);
    } else {
        return master_name + "__subz_b" + std::to_string(-signed_k);
    }
}

// Calculate pos_z for sheet k (k in -B .. +F) relative to master_base_z recursively without overlap!
inline float calculate_sheet_pos_z(float master_base_z, int signed_k) {
    return master_base_z + static_cast<float>(signed_k) * 60.0f;
}

} // namespace rubywater
