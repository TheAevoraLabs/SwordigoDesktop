#pragma once
// ============================================================================
// swdm_format.h — the two dialects of a `.swdm` ground-mesh sheet
//
// `.swdm` is a plain-text sheet: a ring of vertices on the level's XY plane plus
// the generator parameters that extrude it. Two dialects exist, and the
// difference is the one thing boulder cannot represent — DEPTH PER NODE.
//
//   v1  "boulder"   (src/tools/boulder.cpp)
//       `MinDepth` / `MaxDepth` as a single scalar pair for the whole sheet, and
//       the ring as a bare `Vertex[ x y ]` block. Every node necessarily shares
//       one depth, so the extruded mesh is a constant-thickness slab.
//
//   v2  "boulderx"  (src/tools/boulderx.cpp)
//       Adds `FormatVersion : 2` and `Generator : 'boulderx'`, and each node
//       carries its own `Node : x y front back` depths — which is what the
//       engine's own GenerateMesh produces (per-node depth arrays) and what
//       makes vanilla terrain 3D. See
//       docs/formats_and_schemas/scenecreator/13_groundmesh_engine_parity_and_boulderx.md
//
// Compatibility is deliberately ONE-WAY, and this header is what makes it
// enforceable rather than a convention:
//
//   * a v1 sheet loads in BOTH generators. boulderx upgrades it to a uniform
//     slab, which is the only thing the file actually says about depth;
//   * a v2 sheet loads in boulderx ONLY. boulder cannot store per-node depth, so
//     reading one would silently flatten relief that a modder authored — the
//     exact "boulderx strips the Z" symptom. boulder refuses instead, and
//     boulder::last_parse_error() says why.
//
// Both the writer and both readers go through this header so the marker can
// never drift between them.
// ============================================================================

#include <cstdlib>
#include <string>

namespace swdm_format {

/// Dialect version written by boulderx::serialize_swdm().
inline constexpr int kBoulderxVersion = 2;

/// Value of the `Generator` key in a boulderx sheet.
inline constexpr const char* kBoulderxGenerator = "boulderx";

// Key spelling, shared by writer and detector.
inline constexpr const char* kVersionKey = "FormatVersion";
inline constexpr const char* kGeneratorKey = "Generator";

inline std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/// Value of `key : value`, with quotes and any trailing `//` comment removed.
/// Empty when the key is absent. Only the FIRST `key :` line is considered,
/// which is the dialect marker's contract — the marker is a header, not a
/// repeated data row (those are `Node` / `Vertex` and are read positionally).
inline std::string value_of(const std::string& text, const std::string& key) {
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;

        const size_t comment = line.find("//");
        if (comment != std::string::npos) line.erase(comment);
        line = trim(line);
        if (line.empty()) continue;

        // Blocks in both dialects (`Vertex[`, `Hat[`, `]`) never contain a
        // colon in their header line, so a colon is what separates a key.
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (trim(line.substr(0, colon)) != key) continue;

        std::string value = trim(line.substr(colon + 1));
        if (value.size() >= 2 &&
            ((value.front() == '\'' && value.back() == '\'') ||
             (value.front() == '"' && value.back() == '"')))
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return std::string();
}

/// True when `text` is a per-node (boulderx, v2) sheet. Anything else — an
/// unmarked sheet, or junk — is treated as v1, because v1 is the format the
/// shipped tooling wrote before v2 existed.
inline bool is_boulderx_sheet(const std::string& text) {
    if (value_of(text, kGeneratorKey) == kBoulderxGenerator) return true;
    const std::string ver = value_of(text, kVersionKey);
    if (ver.empty()) return false;
    // An unparsable version is not evidence of v2; the `Generator` marker is
    // the authoritative one and the version is only a fallback for sheets whose
    // generator key was hand-removed.
    const int v = std::atoi(ver.c_str());
    return v >= kBoulderxVersion;
}

} // namespace swdm_format
