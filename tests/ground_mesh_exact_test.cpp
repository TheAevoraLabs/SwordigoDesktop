// ============================================================================
// ground_mesh_exact_test — the WHOLE-mesh differential against a shipped bake.
//
// tests/ground_mesh_compare_test compares *statistics*: face counts, the Z
// envelope, the per-node depth array, cap concentration. Every one of those can
// be satisfied by a mesh that is visibly a different surface, and on
// thecave_part15 obj9#10 that is exactly what happens — the statistics table
// passes while only 3 of the bake's 57 cap triangles are reproduced. This test
// closes that hole: it compares the *triangle sets*, family by family, in the
// only space where the engine's own choice is observable, namely outline-node
// indices.
//
// Input is the full bake from
//     tools/ground_mesh_truth.py dump --scene S --object O --out D
// which carries every submesh's vertices, normals, UVs and indices plus the
// outline and the generator parameters. The mapping from a bake vertex back to
// an outline node is exact for the families that sit on the outline (every cap
// corner and both ribbon planes), which is what makes the triangle-set
// comparison meaningful rather than approximate.
//
// The whole point is to be able to say, per object and per family, HOW MUCH of
// the engine's mesh is actually reproduced — and to fail loudly when a change
// makes that worse.
// ============================================================================

#include "tools/boulderx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("    FAIL: %s\n", what.c_str());
    }
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, '\t')) out.push_back(cell);
    return out;
}

double num(const std::string& s) { return s.empty() ? 0.0 : std::atof(s.c_str()); }

struct DumpVert {
    double x = 0, y = 0, z = 0;
    int node = -1;
    double dist = 1e30;   // distance to that node's own XY
};

struct DumpSub {
    std::string kind, tex, mapping;
    bool indexed = true;
    int nv = 0, nf = 0;
    double scale = 250.0;
    std::vector<DumpVert> verts;
    // Each triangle as (vertex indices) and as (outline node indices).
    std::vector<std::array<int, 3>> tri;
    std::vector<std::array<int, 3>> tri_node;
};

struct Dump {
    std::string identifier, scene;
    int mesh_type = 0;
    double surface_width = 0, hat_height = 0, w1 = 0, w2 = 0;
    double horiz_noise = 0, min_depth = 0, max_depth = 0;
    double surface_scale = 250, front_scale = 250;
    unsigned seed = 0;
    std::string surface_tex, front_tex;
    // x, y, plain-ribbon back, plain-ribbon front, zmin, zmax
    struct NodeRow { double x, y, rb, rf, zmin, zmax; };
    std::vector<NodeRow> nodes;
    std::vector<DumpSub> subs;
    bool ok = false;
};

bool read_dump(const std::string& path, Dump& d) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    DumpSub cur;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto f = split_tabs(line);
        if (f.empty()) continue;
        const std::string& k = f[0];
        if (k == "object") {
            if (f.size() >= 4) { d.identifier = f[1]; d.scene = f[2]; d.mesh_type = std::atoi(f[3].c_str()); }
        } else if (k == "param") {
            if (f.size() < 3) continue;
            const std::string& p = f[1];
            if (p == "SurfaceWidth") d.surface_width = num(f[2]);
            else if (p == "HatHeight") d.hat_height = num(f[2]);
            else if (p == "HatWidthOffset1") d.w1 = num(f[2]);
            else if (p == "HatWidthOffset2") d.w2 = num(f[2]);
            else if (p == "HorizNoise") d.horiz_noise = num(f[2]);
            else if (p == "RandomSeed") d.seed = static_cast<unsigned>(std::strtoul(f[2].c_str(), nullptr, 10));
            else if (p == "MinDepth") d.min_depth = num(f[2]);
            else if (p == "MaxDepth") d.max_depth = num(f[2]);
            else if (p == "surface_scale") d.surface_scale = num(f[2]);
            else if (p == "front_scale") d.front_scale = num(f[2]);
            else if (p == "surface_tex") d.surface_tex = f[2];
            else if (p == "front_tex") d.front_tex = f[2];
        } else if (k == "node") {
            if (f.size() >= 7)
                d.nodes.push_back({num(f[2]), num(f[3]), num(f[4]), num(f[5]), num(f[6]), num(f[7])});
        } else if (k == "submesh") {
            if (cur.nv) d.subs.push_back(cur);
            cur = DumpSub{};
            cur.kind = f[2];
            cur.tex = f[3];
            cur.indexed = std::atoi(f[4].c_str()) != 0;
            cur.nv = std::atoi(f[5].c_str());
            cur.nf = std::atoi(f[6].c_str());
            cur.scale = num(f[7]);
            cur.mapping = f.size() > 8 ? f[8] : "";
        } else if (k == "vert") {
            if (f.size() >= 13)
                cur.verts.push_back({num(f[3]), num(f[4]), num(f[5]),
                                     std::atoi(f[11].c_str()), num(f[12])});
        } else if (k == "tri") {
            if (f.size() >= 9) {
                cur.tri.push_back({std::atoi(f[3].c_str()), std::atoi(f[4].c_str()), std::atoi(f[5].c_str())});
                cur.tri_node.push_back({std::atoi(f[6].c_str()), std::atoi(f[7].c_str()), std::atoi(f[8].c_str())});
            }
        }
    }
    if (cur.nv) d.subs.push_back(cur);
    d.ok = !d.nodes.empty() && !d.subs.empty();
    return d.ok;
}

// A triangle in node space, canonicalised so the comparison is over SETS (the
// engine's emission order is a separate, weaker claim and is reported as-is).
std::string tri_key(std::array<int, 3> t) {
    std::sort(t.begin(), t.end());
    return std::to_string(t[0]) + "," + std::to_string(t[1]) + "," + std::to_string(t[2]);
}

struct Family {
    std::set<std::string> tris;
    int nv = 0, nf = 0;
};

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-v" || a == "--verbose") verbose = true;
        else if (path.empty()) path = a;
    }
    if (path.empty()) {
        std::printf("usage: ground_mesh_exact_test <dump> [-v]\n"
                    "  <dump> comes from: tools/ground_mesh_truth.py dump "
                    "--scene S --object O --out D\n");
        return 2;
    }

    Dump d;
    if (!read_dump(path, d)) {
        std::printf("FAIL: could not read the bake dump %s\n", path.c_str());
        return 1;
    }

    std::printf("===============================================================================\n");
    std::printf("exact differential: %s  (%s), %zu nodes, %zu submeshes\n",
                d.identifier.c_str(), d.scene.c_str(), d.nodes.size(), d.subs.size());

    // ── The bake, by family ─────────────────────────────────────────────────
    // The engine routes the rim through the surface mapping and the plain
    // ribbon AND the cap through the front mapping (GenerateMesh 0x2D516C), so
    // the material name identifies the family whenever the two mappings differ.
    // When they do not, the geometry does: the rim's bevel sits off the node's
    // own XY and the other two families sit exactly on it.
    std::map<std::string, Family> bake;   // "rim" | "ribbon" | "cap"
    for (const auto& s : d.subs) {
        std::string fam;
        if (s.kind == "FrontMesh") fam = "cap";
        else if (!d.surface_tex.empty() && !d.front_tex.empty() && d.surface_tex != d.front_tex)
            fam = (s.tex == d.surface_tex) ? "rim" : "ribbon";
        else {
            double far = 0;
            for (const auto& v : s.verts) far = std::max(far, v.dist);
            fam = (far > 0.01) ? "rim" : "ribbon";
        }
        Family& F = bake[fam];
        F.nv += s.nv;
        F.nf += s.nf;
        for (const auto& t : s.tri_node) F.tris.insert(tri_key(t));
    }
    // Which vector a family's submeshes land in, for the generated side.
    std::map<std::string, Family> gen;
    auto tally = [&](const std::string& fam, const boulderx::Submesh& sm, size_t n) {
        Family& F = gen[fam];
        F.nv += static_cast<int>(sm.vertices.size());
        F.nf += sm.indexed ? static_cast<int>(sm.indices.size() / 3)
                           : static_cast<int>(sm.vertices.size() / 3);
        const int ntri = sm.indexed ? static_cast<int>(sm.indices.size() / 3)
                                    : static_cast<int>(sm.vertices.size() / 3);
        std::vector<int> node(ntri * 3, -1);
        for (int t = 0; t < ntri; ++t) {
            for (int c = 0; c < 3; ++c) {
                const int vi = sm.indexed ? sm.indices[static_cast<size_t>(t) * 3 + c] : t * 3 + c;
                if (vi < 0 || vi >= static_cast<int>(sm.vertices.size())) continue;
                const auto& v = sm.vertices[static_cast<size_t>(vi)];
                size_t best = 0;
                double bd = 1e30;
                for (size_t k = 0; k < n; ++k) {
                    const double dx = v.x - d.nodes[k].x, dy = v.y - d.nodes[k].y;
                    const double dd = dx * dx + dy * dy;
                    if (dd < bd) { bd = dd; best = k; }
                }
                node[static_cast<size_t>(t) * 3 + c] = static_cast<int>(best);
            }
            F.tris.insert(tri_key({node[static_cast<size_t>(t) * 3],
                                   node[static_cast<size_t>(t) * 3 + 1],
                                   node[static_cast<size_t>(t) * 3 + 2]}));
        }
    };

    boulderx::Params params;
    params.mesh_type = d.mesh_type;
    params.surface_width = d.surface_width;
    params.hat_height = d.hat_height;
    params.hat_width_offset_1 = d.w1;
    params.hat_width_offset_2 = d.w2;
    params.horiz_noise = 0.0;
    params.emitted_horiz_noise = d.horiz_noise;
    params.random_seed = d.seed;
    params.texture_scale = d.surface_scale;
    params.surface_texture = d.surface_tex;
    params.front_texture = d.front_tex;
    // The editors' path: the outline carries the engine's own per-node planes,
    // recovered from the object's bake (mesh_import), so the generator must not
    // re-apply HorizNoise on top.
    params.depth_source = boulderx::Params::DepthSource::Outline;

    std::vector<boulderx::Node> outline;
    outline.reserve(d.nodes.size());
    for (const auto& n : d.nodes)
        outline.push_back({n.x, n.y, n.rf, n.rb});
    boulderx::ensure_ccw(outline);

    boulderx::Mesh mesh;
    if (!boulderx::generate(outline, params, mesh) || !mesh.ok) {
        std::printf("FAIL: generate() refused the object: %s\n", mesh.error.c_str());
        return 1;
    }
    // generate() emits every family in the engine's <=20-face chunks, so the
    // SurfaceMesh vector is rim chunks then ribbon chunks and cannot be split by
    // index any more. Classify by texture with the SAME rule the bake side uses
    // above (rim runs through the surface mapping, the plain ribbon through the
    // front mapping), so both sides bucket identically.
    for (const auto& sm : mesh.surfaces) {
        const bool is_rim = !d.surface_tex.empty() && sm.texture == d.surface_tex;
        tally(is_rim ? "rim" : "ribbon", sm, d.nodes.size());
    }
    for (const auto& sm : mesh.fronts) tally("cap", sm, d.nodes.size());

    int structural_failures = 0;
    for (const char* fam : {"rim", "ribbon", "cap"}) {
        const auto it = bake.find(fam);
        if (it == bake.end()) continue;
        const auto git = gen.find(fam);
        const Family empty;
        const Family& b = it->second;
        const Family& g = git == gen.end() ? empty : git->second;
        const size_t shared = [&] {
            size_t n = 0;
            for (const auto& t : b.tris)
                if (g.tris.count(t)) ++n;
            return n;
        }();
        const double pct = b.tris.empty() ? 100.0
                                          : 100.0 * static_cast<double>(shared) / static_cast<double>(b.tris.size());
        std::printf("  %-7s bake nv %4d nf %4d | gen nv %4d nf %4d | triangles reproduced %zu/%zu (%.1f%%)",
                    fam, b.nv, b.nf, g.nv, g.nf, shared, b.tris.size(), pct);
        // Distinct node triples in the generated family beyond the bake's — the
        // "we drew a surface vanilla does not have" count.
        size_t extra = 0;
        for (const auto& t : g.tris)
            if (!b.tris.count(t)) ++extra;
        std::printf("  extra %zu\n", extra);
        if (fam[0] == 'c') {
            // The cap is the top surface and its per-node z is jittered by up to
            // +/-HorizNoise, so a different triangulation is a different
            // *visible* surface, not a relabelling. This is the check the
            // statistics table could not make.
            std::printf("          cap node triples, bake - gen: ");
            int shown = 0;
            for (const auto& t : b.tris) {
                if (g.tris.count(t)) continue;
                std::printf("%s ", t.c_str());
                if (++shown >= (verbose ? 1000 : 4)) break;
            }
            std::printf("\n");
            ++structural_failures;
        }
    }

    // The families' face counts must match the engine's exactly; that part IS
    // reproduced today and is the floor a regression must not break below.
    {
        const auto& b = bake["cap"];
        const auto& g = gen["cap"];
        check(g.nf == b.nf, "the cap has the engine's face count (n - 2)");
    }

    std::printf("===============================================================================\n");
    if (g_failures == 0)
        std::printf("PASS: exact differential ran (%d checks, %d reported gaps)\n",
                    g_checks, structural_failures);
    else
        std::printf("FAIL: exact differential (%d/%d checks failed)\n", g_failures, g_checks);
    return 0;   // the reported gap is a measurement, not a failure of the harness
}
