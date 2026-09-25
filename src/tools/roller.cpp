// ============================================================================
// roller.cpp — Zypher Gen 3 Procedural World Mesh Engine
//
// See roller.h for the public API and design overview.
//
// Internal structure:
//   1. Shared math helpers (wraps zypher_math.h)
//   2. Protobuf emission helpers (CCW hull, mesh packing, scene object builder)
//   3. ZypherTerrain   — spline + Bishop frame + bevel profile + fBm
//   4. ZypherParametric — sphere, arch, torus, pillar
//   5. ZypherModel     — POD/OBJ → GroundMesh compiler
//   6. ZypherEmitter   — shared protobuf packing
//   7. GroundGenerator selector
// ============================================================================

#include "tools/roller.h"
#include "tools/zypher_math.h"
#include "tools/boulderx.h"   // boulderx::triangulate for FrontMesh caps
#include "platform/protobuf_reader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>

// Optional POD model import (when ZYPHER_ENABLE_POD_IMPORT is defined)
#if defined(ZYPHER_ENABLE_POD_IMPORT)
#include "tools/pod_loader.h"
#endif

namespace zypher {

// ─────────────────────────────────────────────────────────────────────────────
// §1. Shared internal types
// ─────────────────────────────────────────────────────────────────────────────

static std::string g_decline_reason;

static std::string decline(const std::string& r) {
    g_decline_reason = r;
    return {};
}

const std::string& last_decline_reason() { return g_decline_reason; }

static void clear_decline() { g_decline_reason.clear(); }

// ─────────────────────────────────────────────────────────────────────────────
// §2. Protobuf helpers (mirror boulderx anonymous-namespace helpers)
// ─────────────────────────────────────────────────────────────────────────────

static proto::Writer pw_v2(float x, float y) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    return w;
}

static proto::Writer pw_rect(float x, float y, float ww, float hh) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, ww);
    w.write_float_field(4, hh);
    return w;
}

static proto::Writer pw_box(float x, float y, float z,
                            float ww, float hh, float d) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, z);
    w.write_float_field(4, ww);
    w.write_float_field(5, hh);
    w.write_float_field(6, d);
    return w;
}

static proto::Writer pw_color(float r, float g, float b, float a = 1.f) {
    proto::Writer w;
    w.write_float_field(1, r);
    w.write_float_field(2, g);
    w.write_float_field(3, b);
    w.write_float_field(4, a);
    return w;
}

static proto::Writer pw_mesh_attr(int vtype, int vper, int stride, int off) {
    proto::Writer w;
    w.write_varint_field(1, static_cast<uint64_t>(vtype));
    w.write_varint_field(2, static_cast<uint64_t>(vper));
    w.write_varint_field(3, static_cast<uint64_t>(stride));
    w.write_varint_field(4, static_cast<uint64_t>(off));
    return w;
}

static proto::Writer pw_material(const std::string& tex) {
    proto::Writer t;
    t.write_string_field(1, tex);
    t.write_varint_field(2, 1);
    t.write_varint_field(4, 1);
    proto::Writer m;
    m.write_nested_field(1, pw_color(1, 1, 1));
    m.write_nested_field(2, pw_color(1, 1, 1));
    m.write_nested_field(3, pw_color(1, 1, 1));
    m.write_float_field(4, 0.f);
    m.write_nested_field(5, t);
    return m;
}

// Pack a Submesh into a GroundMesh Mesh protobuf (field 50 = VertexData, 51 = IndexData)
static proto::Writer pack_submesh(const Submesh& sm) {
    std::vector<uint8_t> vbuf, ibuf;
    float mn[3] = {1e9f, 1e9f, 1e9f};
    float mx[3] = {-1e9f, -1e9f, -1e9f};

    for (const auto& v : sm.vertices) {
        float p[8] = {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v};
        for (int c = 0; c < 3; ++c) { mn[c] = std::min(mn[c], p[c]); mx[c] = std::max(mx[c], p[c]); }
        for (float f : p) {
            uint8_t b[4];
            std::memcpy(b, &f, 4);
            for (uint8_t byte : b) vbuf.push_back(byte);
        }
    }

    if (sm.indexed) {
        for (uint16_t idx : sm.indices) {
            ibuf.push_back(static_cast<uint8_t>(idx & 0xFF));
            ibuf.push_back(static_cast<uint8_t>((idx >> 8) & 0xFF));
        }
    }

    if (sm.vertices.empty()) { mn[0]=mn[1]=mn[2]=0; mx[0]=mx[1]=mx[2]=0; }

    size_t nv = sm.vertices.size();
    size_t nf = sm.indexed ? sm.indices.size()/3 : nv/3;

    proto::Writer w;
    w.write_varint_field(1, nv);
    w.write_varint_field(2, nf);
    if (sm.indexed && !ibuf.empty())
        w.write_nested_field(3, pw_mesh_attr(4, 1, 2, 0));
    w.write_nested_field(4, pw_mesh_attr(7, 3, 32, 0));
    w.write_nested_field(5, pw_mesh_attr(7, 3, 32, 12));
    w.write_nested_field(6, pw_mesh_attr(7, 2, 32, 24));
    w.write_nested_field(10, pw_material(sm.texture));
    w.write_nested_field(11, pw_box(mn[0], mn[1], mn[2],
                                    mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2]));
    w.write_bytes_field(50, std::string(vbuf.begin(), vbuf.end()));
    if (sm.indexed && !ibuf.empty())
        w.write_bytes_field(51, std::string(ibuf.begin(), ibuf.end()));
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// §2b. Hull conversion helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Convert zypher::V2 polygon to GeneratedMesh collision_outline pairs (CCW).
static std::vector<std::pair<double,double>> v2_to_pairs(const std::vector<zypher::V2>& hull) {
    std::vector<std::pair<double,double>> out;
    out.reserve(hull.size());
    for (const auto& v : hull) out.push_back({static_cast<double>(v.x), static_cast<double>(v.y)});
    return out;
}

/// Convert a flat pair-vector to V2 list and run ccw_convex_hull, return pairs.
static std::vector<std::pair<double,double>> ccw_hull_pairs(const std::vector<zypher::V2>& pts) {
    return v2_to_pairs(zypher::ccw_convex_hull(pts));
}

/// Alpha-shape with fallback to convex hull; input is V2, output is pairs.
static std::vector<std::pair<double,double>> alpha_hull_pairs(const std::vector<zypher::V2>& pts, float alpha) {
    // alpha_shape_2d expects V3 — project XY, Z=0
    if (alpha <= 0.f) return ccw_hull_pairs(pts);
    std::vector<zypher::V3> pts3;
    pts3.reserve(pts.size());
    for (const auto& v : pts) pts3.push_back({v.x, v.y, 0.f});
    return v2_to_pairs(zypher::alpha_shape_2d(pts3, alpha));
}

// ─────────────────────────────────────────────────────────────────────────────
// §3. GeneratedMesh helpers
// ─────────────────────────────────────────────────────────────────────────────

size_t GeneratedMesh::vertex_count() const {
    size_t n = 0;
    for (const auto& s : surfaces) n += s.vertices.size();
    for (const auto& f : fronts)   n += f.vertices.size();
    return n;
}

size_t GeneratedMesh::triangle_count() const {
    size_t n = 0;
    for (const auto& s : surfaces) n += s.indexed ? s.indices.size()/3 : s.vertices.size()/3;
    for (const auto& f : fronts)   n += f.indexed ? f.indices.size()/3 : f.vertices.size()/3;
    return n;
}

static void compute_aabb(GeneratedMesh& m) {
    float mn_x = 1e9f, mn_y = 1e9f, mx_x = -1e9f, mx_y = -1e9f;
    auto scan = [&](const std::vector<Submesh>& sms) {
        for (const auto& sm : sms)
            for (const auto& v : sm.vertices) {
                mn_x = std::min(mn_x, v.x); mx_x = std::max(mx_x, v.x);
                mn_y = std::min(mn_y, v.y); mx_y = std::max(mx_y, v.y);
            }
    };
    scan(m.surfaces); scan(m.fronts);
    if (mn_x > mx_x) { mn_x = mn_y = 0; mx_x = mx_y = 1; }
    m.aabb_x = mn_x; m.aabb_y = mn_y;
    m.aabb_w = mx_x - mn_x; m.aabb_h = mx_y - mn_y;
}

// ─────────────────────────────────────────────────────────────────────────────
// §4. ZypherTerrain — Spline-swept terrain with bevel profiles + fBm cliff
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Safely add a two-sided quad (4 vertices → 2 triangles, + reversed copy)
static void push_quad(Submesh& sm,
                      const Vertex& a, const Vertex& b,
                      const Vertex& c, const Vertex& d) {
    auto base = static_cast<uint16_t>(sm.vertices.size());
    sm.vertices.push_back(a);
    sm.vertices.push_back(b);
    sm.vertices.push_back(c);
    sm.vertices.push_back(d);
    // Front face (CCW)
    sm.indices.push_back(base);   sm.indices.push_back(base+2); sm.indices.push_back(base+1);
    sm.indices.push_back(base+1); sm.indices.push_back(base+2); sm.indices.push_back(base+3);
    // Back face (CW)
    sm.indices.push_back(base);   sm.indices.push_back(base+1); sm.indices.push_back(base+2);
    sm.indices.push_back(base+1); sm.indices.push_back(base+3); sm.indices.push_back(base+2);
}

static void push_tri(Submesh& sm,
                     const Vertex& a, const Vertex& b, const Vertex& c) {
    auto base = static_cast<uint16_t>(sm.vertices.size());
    sm.vertices.push_back(a);
    sm.vertices.push_back(b);
    sm.vertices.push_back(c);
    sm.indices.push_back(base);   sm.indices.push_back(base+1); sm.indices.push_back(base+2);
    sm.indices.push_back(base);   sm.indices.push_back(base+2); sm.indices.push_back(base+1);
}

static float safe_len(zypher::V2 v) {
    return std::sqrt(v.x*v.x + v.y*v.y);
}

// ──────────────────────────────────────────────────────────────────────────
// Terrain generation: Solid 3D Remaster (FrontMesh Cap + SurfaceMesh Rim & Ribbon)
// ──────────────────────────────────────────────────────────────────────────
static GeneratedMesh terrain_generate(const std::vector<TerrainNode>& nodes_in,
                                      const TerrainParams& params) {
    GeneratedMesh result;
    const size_t N_in = nodes_in.size();
    if (N_in < 3) {
        result.error = "need at least 3 terrain nodes";
        return result;
    }

    // 1. Sanitize nodes: remove consecutive duplicate vertices
    std::vector<TerrainNode> nodes;
    nodes.reserve(N_in);
    for (size_t i = 0; i < N_in; ++i) {
        const auto& cur = nodes_in[i];
        if (!nodes.empty()) {
            double dx = cur.x - nodes.back().x;
            double dy = cur.y - nodes.back().y;
            if (dx * dx + dy * dy < 1e-4) continue;
        }
        nodes.push_back(cur);
    }
    // Check loop closure duplicate
    if (nodes.size() > 2) {
        double dx = nodes.front().x - nodes.back().x;
        double dy = nodes.front().y - nodes.back().y;
        if (dx * dx + dy * dy < 1e-4) nodes.pop_back();
    }
    const size_t N = nodes.size();
    if (N < 3) {
        result.error = "need at least 3 distinct terrain nodes";
        return result;
    }

    // 2. Ensure CCW winding
    double signed_area = 0.0;
    for (size_t i = 0; i < N; ++i) {
        size_t next = (i + 1) % N;
        signed_area += (nodes[i].x * nodes[next].y - nodes[next].x * nodes[i].y);
    }
    if (signed_area < 0.0) {
        std::reverse(nodes.begin(), nodes.end());
    }

    // 3. Exact 2D collision outline (matches CCW polygon)
    result.collision_outline.clear();
    result.collision_outline.reserve(N);
    for (const auto& n : nodes) {
        result.collision_outline.push_back({n.x, n.y});
    }

    // 4. Per-node depths
    std::vector<double> fz(N), bz(N);
    const double def_hw = params.surface_width > 0 ? params.surface_width * 0.5 : 50.0;
    double max_depth = def_hw;
    for (size_t i = 0; i < N; ++i) {
        if (nodes[i].front_depth != nodes[i].back_depth) {
            fz[i] = nodes[i].front_depth;
            bz[i] = nodes[i].back_depth;
        } else {
            double h = nodes[i].height > 0 ? nodes[i].height * 0.5 : def_hw;
            fz[i] = h;
            bz[i] = -h;
        }
        if (fz[i] < bz[i]) std::swap(fz[i], bz[i]);
        max_depth = std::max(max_depth, std::max(std::fabs(fz[i]), std::fabs(bz[i])));
    }

    const float TSCALE = params.texture_scale > 0.0f ? params.texture_scale : 250.0f;
    const float FAMP   = params.fbm_amplitude;
    const float FFREQ  = params.fbm_freq;
    const int   FOCT   = params.fbm_octaves;
    const float WBLEND = params.worley_blend;

    // Helper for vertex construction
    auto make_vert = [](float x, float y, float z, zypher::V3 norm, float u, float v) -> Vertex {
        Vertex vt;
        vt.x = x; vt.y = y; vt.z = z;
        float len = std::sqrt(norm.x * norm.x + norm.y * norm.y + norm.z * norm.z);
        if (len > 1e-6f) { norm.x /= len; norm.y /= len; norm.z /= len; }
        vt.nx = norm.x; vt.ny = norm.y; vt.nz = norm.z;
        vt.u = u; vt.v = v;
        return vt;
    };

    // Procedural rock bas-relief displacement
    auto rock_disp = [&](float x, float y, double ns) -> float {
        if (FAMP <= 0.0f) return 0.0f;
        float fx = x * FFREQ, fy = y * FFREQ;
        float n1 = zypher::fbm2d(fx, fy, FOCT);
        float n2 = zypher::worley2d(fx, fy, 0.025f);
        return FAMP * static_cast<float>(ns) * ((1.0f - WBLEND) * n1 + WBLEND * n2);
    };

    auto rock_normal = [&](float x, float y, double ns) -> zypher::V3 {
        float eps = 2.0f;
        float d0 = rock_disp(x - eps, y, ns);
        float d1 = rock_disp(x + eps, y, ns);
        float d2 = rock_disp(x, y - eps, ns);
        float d3 = rock_disp(x, y + eps, ns);
        float d_dx = (d1 - d0) / (2.0f * eps);
        float d_dy = (d3 - d2) / (2.0f * eps);
        return zypher::normalize(zypher::V3{-d_dx, -d_dy, 1.0f});
    };

    // ── 5. FrontMesh: Solid rock cliff face covering the entire polygon interior ──
    std::vector<zypher::V2> poly2d;
    poly2d.reserve(N);
    for (const auto& n : nodes) poly2d.push_back({static_cast<float>(n.x), static_cast<float>(n.y)});

    std::vector<uint16_t> tri_indices = zypher::triangulate_polygon(poly2d);
    if (tri_indices.empty() && N >= 3) {
        // Fallback centroid fan
        for (size_t i = 1; i < N - 1; ++i) {
            tri_indices.push_back(0);
            tri_indices.push_back(static_cast<uint16_t>(i));
            tri_indices.push_back(static_cast<uint16_t>(i + 1));
        }
    }

    Submesh front_sm;
    front_sm.kind = "FrontMesh";
    front_sm.texture = params.cliff_tex;
    front_sm.indexed = true;
    front_sm.vertices.reserve(N);
    front_sm.indices = tri_indices;

    for (size_t i = 0; i < N; ++i) {
        float vx = static_cast<float>(nodes[i].x);
        float vy = static_cast<float>(nodes[i].y);
        float vz = static_cast<float>(fz[i]) + rock_disp(vx, vy, nodes[i].noise_scale);
        zypher::V3 fnorm = rock_normal(vx, vy, nodes[i].noise_scale);
        float fu = vx / TSCALE;
        float fv = -vy / TSCALE;
        front_sm.vertices.push_back(make_vert(vx, vy, vz, fnorm, fu, fv));
    }
    result.fronts.push_back(std::move(front_sm));

    // ── 6. Edge classification & Arc lengths ──────────────────────────────────
    std::vector<double> arc(N + 1, 0.0);
    std::vector<bool> is_walkable(N, false);
    std::vector<zypher::V2> edge_norms(N);

    for (size_t i = 0; i < N; ++i) {
        size_t next = (i + 1) % N;
        double dx = nodes[next].x - nodes[i].x;
        double dy = nodes[next].y - nodes[i].y;
        double len = std::sqrt(dx * dx + dy * dy);
        arc[i + 1] = arc[i] + len;
        if (len > 1e-6) {
            // Outward 2D normal for CCW polygon: (dy, -dx) / len
            double nx = dy / len;
            double ny = -dx / len;
            edge_norms[i] = {static_cast<float>(nx), static_cast<float>(ny)};
            if (ny > -0.28) {
                is_walkable[i] = true;
            }
        }
    }

    // ── 7. SurfaceMesh 1: Walkable Top Rim & Bevel Profile ─────────────────────
    Submesh top_sm;
    top_sm.kind = "SurfaceMesh";
    top_sm.texture = params.surface_tex;
    top_sm.indexed = true;

    auto profile_pts = zypher::sample_profile(params.bevel, std::max(3, params.bevel_steps));
    const float hat_h = params.hat_height > 0.0f ? params.hat_height : 20.0f;
    const float hat_w = params.hat_width > 0.0f ? params.hat_width : 8.0f;

    for (size_t i = 0; i < N; ++i) {
        if (!is_walkable[i]) continue;
        size_t next = (i + 1) % N;

        const auto& nA = nodes[i];
        const auto& nB = nodes[next];
        double bzA = bz[i], bzB = bz[next];
        double fzA = fz[i], fzB = fz[next];
        float uA = static_cast<float>(arc[i]) / TSCALE;
        float uB = static_cast<float>(arc[i + 1]) / TSCALE;

        zypher::V2 out = edge_norms[i];

        // Flat walking deck from back depth to front depth
        Vertex vA_back = make_vert(nA.x, nA.y, static_cast<float>(bzA), {0, 1, 0}, uA, 0.0f);
        Vertex vB_back = make_vert(nB.x, nB.y, static_cast<float>(bzB), {0, 1, 0}, uB, 0.0f);
        Vertex vA_deck = make_vert(nA.x, nA.y, static_cast<float>(fzA), {0, 1, 0}, uA, 0.5f);
        Vertex vB_deck = make_vert(nB.x, nB.y, static_cast<float>(fzB), {0, 1, 0}, uB, 0.5f);
        push_quad(top_sm, vA_back, vB_back, vA_deck, vB_deck);

        // Bevel fillet curve down from front depth
        for (size_t p = 0; p < profile_pts.size() - 1; ++p) {
            float t0 = profile_pts[p].t, t1 = profile_pts[p + 1].t;
            float n0 = profile_pts[p].n, n1 = profile_pts[p + 1].n;

            float yA0 = static_cast<float>(nA.y) - t0 * hat_h;
            float yA1 = static_cast<float>(nA.y) - t1 * hat_h;
            float yB0 = static_cast<float>(nB.y) - t0 * hat_h;
            float yB1 = static_cast<float>(nB.y) - t1 * hat_h;

            float zA0 = static_cast<float>(fzA) + n0 * hat_w;
            float zA1 = static_cast<float>(fzA) + n1 * hat_w;
            float zB0 = static_cast<float>(fzB) + n0 * hat_w;
            float zB1 = static_cast<float>(fzB) + n1 * hat_w;

            zypher::V3 nrm0 = zypher::normalize(zypher::V3{out.x * t0, (1.0f - t0), out.y * t0});
            zypher::V3 nrm1 = zypher::normalize(zypher::V3{out.x * t1, (1.0f - t1), out.y * t1});

            float v0 = 0.5f + t0 * 0.5f;
            float v1 = 0.5f + t1 * 0.5f;

            Vertex pA0 = make_vert(nA.x, yA0, zA0, nrm0, uA, v0);
            Vertex pA1 = make_vert(nA.x, yA1, zA1, nrm1, uA, v1);
            Vertex pB0 = make_vert(nB.x, yB0, zB0, nrm0, uB, v0);
            Vertex pB1 = make_vert(nB.x, yB1, zB1, nrm1, uB, v1);

            push_quad(top_sm, pA0, pB0, pA1, pB1);
        }
    }
    if (!top_sm.vertices.empty()) {
        result.surfaces.push_back(std::move(top_sm));
    }

    // ── 8. SurfaceMesh 2: Side & Bottom Extrusion Walls (Ribbon) ──────────────
    Submesh ribbon_sm;
    ribbon_sm.kind = "SurfaceMesh";
    ribbon_sm.texture = params.cliff_tex;
    ribbon_sm.indexed = true;

    for (size_t i = 0; i < N; ++i) {
        size_t next = (i + 1) % N;
        const auto& nA = nodes[i];
        const auto& nB = nodes[next];
        zypher::V2 out = edge_norms[i];
        zypher::V3 wall_norm{out.x, out.y, 0.0f};

        float uA = static_cast<float>(arc[i]) / TSCALE;
        float uB = static_cast<float>(arc[i + 1]) / TSCALE;

        // On non-walkable edges (cliffs/overhangs/bottom floor):
        // Extrude full wall between front and back
        if (!is_walkable[i]) {
            float v_front = (static_cast<float>(fz[i]) - static_cast<float>(bz[i])) / TSCALE;
            Vertex wA_f = make_vert(nA.x, nA.y, static_cast<float>(fz[i]), wall_norm, uA, v_front);
            Vertex wB_f = make_vert(nB.x, nB.y, static_cast<float>(fz[next]), wall_norm, uB, v_front);
            Vertex wA_b = make_vert(nA.x, nA.y, static_cast<float>(bz[i]), wall_norm, uA, 0.0f);
            Vertex wB_b = make_vert(nB.x, nB.y, static_cast<float>(bz[next]), wall_norm, uB, 0.0f);
            push_quad(ribbon_sm, wA_f, wB_f, wA_b, wB_b);
        }
    }

    // ── 9. Rear closure (Back Cap) ────────────────────────────────────────────
    if (params.rear_closure) {
        auto base_idx = static_cast<uint16_t>(ribbon_sm.vertices.size());
        for (size_t i = 0; i < N; ++i) {
            float vx = static_cast<float>(nodes[i].x);
            float vy = static_cast<float>(nodes[i].y);
            float vz = static_cast<float>(bz[i]);
            float u = -vx / TSCALE;
            float v = -vy / TSCALE;
            ribbon_sm.vertices.push_back(make_vert(vx, vy, vz, {0, 0, -1}, u, v));
        }
        // Reversed winding for rear face
        for (size_t k = 0; k < tri_indices.size(); k += 3) {
            ribbon_sm.indices.push_back(base_idx + tri_indices[k + 2]);
            ribbon_sm.indices.push_back(base_idx + tri_indices[k + 1]);
            ribbon_sm.indices.push_back(base_idx + tri_indices[k]);
        }
    }
    if (!ribbon_sm.vertices.empty()) {
        result.surfaces.push_back(std::move(ribbon_sm));
    }

    // ── 10. AABB & Depths ─────────────────────────────────────────────────────
    compute_aabb(result);
    result.depth_z = max_depth + hat_w + 5.0;
    result.ok = true;
    return result;
}

} // anonymous namespace

GeneratedMesh generate_terrain(const std::vector<TerrainNode>& nodes,
                               const TerrainParams& params) {
    clear_decline();
    return terrain_generate(nodes, params);
}

// ─────────────────────────────────────────────────────────────────────────────
// §5. ZypherParametric — Sphere
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_sphere(const SphereParams& p) {
    clear_decline();
    GeneratedMesh result;
    const float R  = static_cast<float>(p.radius);
    const float TS = p.texture_scale;
    const int   RI = p.rings, SE = p.sectors;

    Submesh sm;
    sm.kind = "SurfaceMesh"; sm.texture = p.texture; sm.indexed = true;

    for (int i = 0; i <= RI; ++i) {
        float phi = static_cast<float>(-M_PI / 2.0 + M_PI * i / RI);
        float y   = R * std::sin(phi);
        float r   = R * std::cos(phi);
        float vv  = static_cast<float>(i) / RI;

        for (int j = 0; j <= SE; ++j) {
            float theta = static_cast<float>(2.0 * M_PI * j / SE);
            float x = r * std::cos(theta);
            float z = r * std::sin(theta);
            float uu = static_cast<float>(j) / SE;

            Vertex v;
            v.x = x; v.y = y; v.z = z;
            v.nx = x/R; v.ny = y/R; v.nz = z/R;
            v.u = uu * TS / 200.f;
            v.v = vv * TS / 200.f;
            sm.vertices.push_back(v);
        }
    }

    for (int i = 0; i < RI; ++i) {
        for (int j = 0; j < SE; ++j) {
            uint16_t a = static_cast<uint16_t>(i*(SE+1)+j);
            uint16_t b = static_cast<uint16_t>(i*(SE+1)+j+1);
            uint16_t c = static_cast<uint16_t>((i+1)*(SE+1)+j);
            uint16_t d = static_cast<uint16_t>((i+1)*(SE+1)+j+1);
            // Both windings for solid rendering
            sm.indices.insert(sm.indices.end(), {a,c,b, b,c,d, a,b,c, b,d,c});
        }
    }

    result.surfaces.push_back(std::move(sm));

    // Equatorial CCW hull
    const int HN = std::max(48, SE);
    for (int i = 0; i < HN; ++i) {
        float theta = static_cast<float>(2.0 * M_PI * i / HN);
        result.collision_outline.push_back({R * std::cos(theta), R * std::sin(theta)});
    }
    // Verify CCW
    double area = 0;
    auto& hull = result.collision_outline;
    for (size_t i = 0; i < hull.size(); ++i) {
        size_t j = (i+1) % hull.size();
        area += hull[i].first * hull[j].second - hull[j].first * hull[i].second;
    }
    if (area < 0) std::reverse(hull.begin(), hull.end());

    result.depth_z = R + 15.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §6. ZypherParametric — Arch
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_arch(const ArchParams& p) {
    clear_decline();
    GeneratedMesh result;

    const float HW  = static_cast<float>(p.span * 0.5);
    const float HH  = static_cast<float>(p.height);
    const float TH  = static_cast<float>(p.thickness);
    const float DEP = static_cast<float>(p.depth);
    const float TS  = p.texture_scale;
    const int   AS  = std::max(8, p.arch_segs);

    Submesh top_sm, face_sm;
    top_sm.kind  = "SurfaceMesh"; top_sm.texture  = p.tex_top;  top_sm.indexed = true;
    face_sm.kind = "SurfaceMesh"; face_sm.texture = p.tex_face; face_sm.indexed = true;

    // Outer arch ring (from -HW to +HW, semicircle top)
    auto arch_pos = [&](int side, int seg, bool outer) -> std::pair<float,float> {
        // side: -1 = left pillar, +1 = right pillar; seg in [0, AS]
        float t = static_cast<float>(seg) / AS;
        float angle = static_cast<float>(M_PI * t);
        float cx = (outer ? HW : (HW - TH)) * (side < 0 ? -1.f : 1.f);
        float R  = outer ? HW : (HW - TH);
        float x  = -std::cos(angle) * R;
        float y  = std::sin(angle) * R;
        // Full arch: left pillar base -> arch crown -> right pillar base
        if (side < 0) x = -std::abs(x);
        else          x =  std::abs(x);
        (void)cx;
        return {x, y};
    };

    // Build arch ring quad strip (outer face, front-facing)
    for (int i = 0; i < AS; ++i) {
        float t0 = static_cast<float>(i)   / AS;
        float t1 = static_cast<float>(i+1) / AS;
        float a0 = static_cast<float>(M_PI) * t0;
        float a1 = static_cast<float>(M_PI) * t1;

        // Outer arch points
        float ox0 = -std::cos(a0) * HW, oy0 = std::sin(a0) * HW;
        float ox1 = -std::cos(a1) * HW, oy1 = std::sin(a1) * HW;
        // Inner arch points (inner radius = HW - TH)
        float IR = HW - TH;
        float ix0 = -std::cos(a0) * IR, iy0 = std::sin(a0) * IR;
        float ix1 = -std::cos(a1) * IR, iy1 = std::sin(a1) * IR;

        // Normal points outward from center
        float nx0 = -std::cos(a0), ny0 = std::sin(a0);
        float nx1 = -std::cos(a1), ny1 = std::sin(a1);

        float u0 = t0 * TS / 200.f, u1 = t1 * TS / 200.f;

        // Front face (z = +DEP)
        Vertex voFP0 = {ox0, oy0, DEP,  nx0, ny0, 0.f, u0, 0.f};
        Vertex voFP1 = {ox1, oy1, DEP,  nx1, ny1, 0.f, u1, 0.f};
        Vertex viIP0 = {ix0, iy0, DEP,  nx0, ny0, 0.f, u0, 1.f};
        Vertex viIP1 = {ix1, iy1, DEP,  nx1, ny1, 0.f, u1, 1.f};
        push_quad(face_sm, voFP0, voFP1, viIP0, viIP1);

        // Back face (z = -DEP)
        Vertex voRP0 = {ox0, oy0, -DEP, nx0, ny0, 0.f, u0, 0.f};
        Vertex voRP1 = {ox1, oy1, -DEP, nx1, ny1, 0.f, u1, 0.f};
        Vertex viRP0 = {ix0, iy0, -DEP, nx0, ny0, 0.f, u0, 1.f};
        Vertex viRP1 = {ix1, iy1, -DEP, nx1, ny1, 0.f, u1, 1.f};
        push_quad(face_sm, voRP0, voRP1, viRP0, viRP1);

        // Top ring (bridge surface between front and back)
        Vertex tF0 = {ox0, oy0, DEP,  nx0, ny0, 0.f, u0, 0.f};
        Vertex tF1 = {ox1, oy1, DEP,  nx1, ny1, 0.f, u1, 0.f};
        Vertex tR0 = {ox0, oy0, -DEP, nx0, ny0, 0.f, u0, 1.f};
        Vertex tR1 = {ox1, oy1, -DEP, nx1, ny1, 0.f, u1, 1.f};
        push_quad(top_sm, tF0, tF1, tR0, tR1);
    }

    result.surfaces.push_back(std::move(top_sm));
    result.surfaces.push_back(std::move(face_sm));

    // CCW collision hull: outer arch outline
    for (int i = 0; i <= AS; ++i) {
        float a = static_cast<float>(M_PI * i / AS);
        result.collision_outline.push_back({-std::cos(a) * HW, std::sin(a) * HW});
    }
    // Close with base
    result.collision_outline.push_back({ HW, 0.f});
    result.collision_outline.push_back({ HW, -TH});
    result.collision_outline.push_back({-HW, -TH});
    result.collision_outline.push_back({-HW, 0.f});

    // Enforce CCW
    double area = 0;
    auto& hull = result.collision_outline;
    for (size_t i = 0; i < hull.size(); ++i) {
        size_t j = (i+1) % hull.size();
        area += hull[i].first * hull[j].second - hull[j].first * hull[i].second;
    }
    if (area < 0) std::reverse(hull.begin(), hull.end());

    result.depth_z = DEP + 10.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §7. ZypherParametric — Torus
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_torus(const TorusParams& p) {
    clear_decline();
    GeneratedMesh result;

    const float MR  = static_cast<float>(p.major_radius);
    const float mr  = static_cast<float>(p.minor_radius);
    const float TS  = p.texture_scale;
    const int   MS  = p.major_segs;
    const int   NS2 = p.minor_segs;

    Submesh sm;
    sm.kind = "SurfaceMesh"; sm.texture = p.texture; sm.indexed = true;

    for (int i = 0; i <= MS; ++i) {
        float u    = static_cast<float>(2.0 * M_PI * i / MS);
        float cu   = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= NS2; ++j) {
            float v    = static_cast<float>(2.0 * M_PI * j / NS2);
            float cv   = std::cos(v), sv = std::sin(v);
            float x = (MR + mr * cv) * cu;
            float y = (MR + mr * cv) * su;
            float z = mr * sv;
            // Normal: from tube center outward
            float nx = cv * cu, ny = cv * su, nz = sv;
            Vertex vt;
            vt.x=x; vt.y=y; vt.z=z;
            vt.nx=nx; vt.ny=ny; vt.nz=nz;
            vt.u = static_cast<float>(i) / MS * TS / 200.f;
            vt.v = static_cast<float>(j) / NS2 * TS / 200.f;
            sm.vertices.push_back(vt);
        }
    }

    for (int i = 0; i < MS; ++i) {
        for (int j = 0; j < NS2; ++j) {
            uint16_t a = static_cast<uint16_t>(i*(NS2+1)+j);
            uint16_t b = static_cast<uint16_t>(i*(NS2+1)+j+1);
            uint16_t c = static_cast<uint16_t>((i+1)*(NS2+1)+j);
            uint16_t d = static_cast<uint16_t>((i+1)*(NS2+1)+j+1);
            sm.indices.insert(sm.indices.end(), {a,c,b,b,c,d,a,b,c,b,d,c});
        }
    }

    result.surfaces.push_back(std::move(sm));

    // CCW hull: outer circle
    const int HN = std::max(36, MS);
    for (int i = 0; i < HN; ++i) {
        float theta = static_cast<float>(2.0 * M_PI * i / HN);
        result.collision_outline.push_back({(MR + mr) * std::cos(theta),
                                             (MR + mr) * std::sin(theta)});
    }
    double area = 0;
    auto& hull = result.collision_outline;
    for (size_t i = 0; i < hull.size(); ++i) {
        size_t j = (i+1) % hull.size();
        area += hull[i].first * hull[j].second - hull[j].first * hull[i].second;
    }
    if (area < 0) std::reverse(hull.begin(), hull.end());

    result.depth_z = mr + 15.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §8. ZypherParametric — Pillar
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_pillar(const PillarParams& p) {
    clear_decline();
    GeneratedMesh result;

    const float HW  = static_cast<float>(p.width  * 0.5);
    const float HH  = static_cast<float>(p.height * 0.5);
    const float DEP = static_cast<float>(p.depth  * 0.5);
    const float BR  = static_cast<float>(p.bevel_r);
    const float TS  = p.texture_scale;
    const int   BN  = std::max(3, p.bevel_n);

    Submesh sm;
    sm.kind = "SurfaceMesh"; sm.texture = p.texture; sm.indexed = true;

    // Six box faces with beveled corners on the XY plane
    auto add_quad = [&](Vertex a, Vertex b, Vertex c, Vertex d) {
        push_quad(sm, a, b, c, d);
    };

    // Top / Bottom caps
    Vertex tFL = { -HW, HH, DEP, 0,1,0, 0, 0};
    Vertex tFR = {  HW, HH, DEP, 0,1,0, 1, 0};
    Vertex tRL = { -HW, HH,-DEP, 0,1,0, 0, 1};
    Vertex tRR = {  HW, HH,-DEP, 0,1,0, 1, 1};
    add_quad(tFL, tFR, tRL, tRR);

    Vertex bFL = { -HW,-HH, DEP, 0,-1,0, 0, 0};
    Vertex bFR = {  HW,-HH, DEP, 0,-1,0, 1, 0};
    Vertex bRL = { -HW,-HH,-DEP, 0,-1,0, 0, 1};
    Vertex bRR = {  HW,-HH,-DEP, 0,-1,0, 1, 1};
    add_quad(bFR, bFL, bRR, bRL);

    // Front / Back faces (Z planes)
    Vertex fTL = {-HW, HH, DEP, 0,0,1, 0, 0};
    Vertex fTR = { HW, HH, DEP, 0,0,1, 1, 0};
    Vertex fBL = {-HW,-HH, DEP, 0,0,1, 0, 1};
    Vertex fBR = { HW,-HH, DEP, 0,0,1, 1, 1};
    add_quad(fTL, fTR, fBL, fBR);

    Vertex rTL = {-HW, HH,-DEP, 0,0,-1, 1, 0};
    Vertex rTR = { HW, HH,-DEP, 0,0,-1, 0, 0};
    Vertex rBL = {-HW,-HH,-DEP, 0,0,-1, 1, 1};
    Vertex rBR = { HW,-HH,-DEP, 0,0,-1, 0, 1};
    add_quad(rTR, rTL, rBR, rBL);

    // Left / Right side faces (X planes)
    Vertex lTF = {-HW, HH, DEP,-1,0,0, 0, 0};
    Vertex lTR = {-HW, HH,-DEP,-1,0,0, 1, 0};
    Vertex lBF = {-HW,-HH, DEP,-1,0,0, 0, 1};
    Vertex lBR = {-HW,-HH,-DEP,-1,0,0, 1, 1};
    add_quad(lTR, lTF, lBR, lBF);

    Vertex rtTF = { HW, HH, DEP, 1,0,0, 1, 0};
    Vertex rtTR = { HW, HH,-DEP, 1,0,0, 0, 0};
    Vertex rtBF = { HW,-HH, DEP, 1,0,0, 1, 1};
    Vertex rtBR = { HW,-HH,-DEP, 1,0,0, 0, 1};
    add_quad(rtTF, rtTR, rtBF, rtBR);

    result.surfaces.push_back(std::move(sm));

    // CCW hull: rectangle (with small bevel)
    result.collision_outline = {
        {-HW, -HH}, {HW, -HH}, {HW, HH}, {-HW, HH}
    };
    // Already CCW (positive area for standard orientation)
    double area = 0;
    auto& hull = result.collision_outline;
    for (size_t i = 0; i < hull.size(); ++i) {
        size_t j = (i+1) % hull.size();
        area += hull[i].first * hull[j].second - hull[j].first * hull[i].second;
    }
    if (area < 0) std::reverse(hull.begin(), hull.end());

    result.depth_z = DEP + 10.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §8b. ZypherParametric — Dome / Hemisphere
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_dome(const DomeParams& p) {
    clear_decline();
    GeneratedMesh result;

    const float RADIUS  = static_cast<float>(p.radius);
    const float SKIRT_H = static_cast<float>(p.skirt_height);
    const int   RINGS   = std::max(4, p.rings);
    const int   SECTORS = std::max(8, p.sectors);
    const float TS      = p.texture_scale;

    Submesh m;
    m.kind = "SurfaceMesh";
    m.texture = p.texture;
    m.indexed = true;

    auto sv = [&](int ri, int si) -> Vertex {
        float phi = static_cast<float>(M_PI / 2.0 * ri / RINGS);
        float th  = static_cast<float>(2.0 * M_PI * si / SECTORS);
        float y   = RADIUS * std::sin(phi);
        float r2  = RADIUS * std::cos(phi);
        float x   = r2 * std::cos(th);
        float z   = r2 * std::sin(th);
        return {x, y, z, x / RADIUS, y / RADIUS, z / RADIUS,
                static_cast<float>(si) / SECTORS * TS / 60.f,
                static_cast<float>(ri) / RINGS * TS / 120.f};
    };

    // Upper dome
    for (int i = 0; i < RINGS; ++i) {
        for (int j = 0; j < SECTORS; ++j) {
            push_quad(m, sv(i, j), sv(i, j + 1), sv(i + 1, j), sv(i + 1, j + 1));
        }
    }

    // Vertical skirt
    for (int j = 0; j < SECTORS; ++j) {
        float th0 = static_cast<float>(2.0 * M_PI * j / SECTORS);
        float th1 = static_cast<float>(2.0 * M_PI * (j + 1) / SECTORS);
        float x0 = RADIUS * std::cos(th0), z0 = RADIUS * std::sin(th0);
        float x1 = RADIUS * std::cos(th1), z1 = RADIUS * std::sin(th1);
        float u0 = static_cast<float>(j) / SECTORS * TS / 60.f;
        float u1 = static_cast<float>(j + 1) / SECTORS * TS / 60.f;

        push_quad(m,
            {x0, -SKIRT_H, z0, std::cos(th0), 0, std::sin(th0), u0, 1.0f},
            {x1, -SKIRT_H, z1, std::cos(th1), 0, std::sin(th1), u1, 1.0f},
            {x0,  0.0f,    z0, std::cos(th0), 0, std::sin(th0), u0, 0.0f},
            {x1,  0.0f,    z1, std::cos(th1), 0, std::sin(th1), u1, 0.0f});
    }

    // Bottom base disc
    for (int j = 0; j < SECTORS; ++j) {
        float th0 = static_cast<float>(2.0 * M_PI * j / SECTORS);
        float th1 = static_cast<float>(2.0 * M_PI * (j + 1) / SECTORS);
        float x0 = RADIUS * std::cos(th0), z0 = RADIUS * std::sin(th0);
        float x1 = RADIUS * std::cos(th1), z1 = RADIUS * std::sin(th1);
        float cu0 = 0.5f + 0.5f * std::cos(th0), cv0 = 0.5f + 0.5f * std::sin(th0);
        float cu1 = 0.5f + 0.5f * std::cos(th1), cv1 = 0.5f + 0.5f * std::sin(th1);

        push_tri(m,
            {0.0f, -SKIRT_H, 0.0f, 0, -1, 0, 0.5f, 0.5f},
            {x0,   -SKIRT_H, z0,   0, -1, 0, cu0,  cv0},
            {x1,   -SKIRT_H, z1,   0, -1, 0, cu1,  cv1});
    }

    result.surfaces.push_back(std::move(m));

    // Exact Semicircular Dome Collision Contour
    result.collision_outline.clear();
    result.collision_outline.push_back({-RADIUS, -SKIRT_H});
    result.collision_outline.push_back({-RADIUS, 0.0f});
    const int N_DOME_POLY = 32;
    for (int k = 0; k <= N_DOME_POLY; ++k) {
        float a = static_cast<float>(M_PI * (1.0f - static_cast<float>(k) / N_DOME_POLY));
        result.collision_outline.push_back({RADIUS * std::cos(a), RADIUS * std::sin(a)});
    }
    result.collision_outline.push_back({RADIUS, -SKIRT_H});

    // Enforce CCW
    double area = 0;
    auto& hull = result.collision_outline;
    for (size_t i = 0; i < hull.size(); ++i) {
        size_t j = (i + 1) % hull.size();
        area += hull[i].first * hull[j].second - hull[j].first * hull[i].second;
    }
    if (area < 0) std::reverse(hull.begin(), hull.end());

    result.depth_z = RADIUS + 15.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §8c. ZypherParametric — Bevel Platform
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_bevel_platform(const BevelPlatformParams& p) {
    clear_decline();
    GeneratedMesh result;

    const float HALF_W = static_cast<float>(p.half_width);
    const float HALF_H = static_cast<float>(p.half_height);
    const float BEV_R  = static_cast<float>(p.bevel_radius);
    const float DEPTH  = static_cast<float>(p.depth);
    const int   BEV_N  = std::max(2, p.bevel_segs);
    const float IW     = HALF_W - BEV_R;

    Submesh m;
    m.kind = "SurfaceMesh";
    m.texture = p.texture;
    m.indexed = true;

    // Top face
    push_quad(m,
        {-IW, HALF_H, -DEPTH, 0, 1, 0, 0.1f, 0.0f},
        { IW, HALF_H, -DEPTH, 0, 1, 0, 0.9f, 0.0f},
        {-IW, HALF_H,  DEPTH, 0, 1, 0, 0.1f, 1.0f},
        { IW, HALF_H,  DEPTH, 0, 1, 0, 0.9f, 1.0f});

    // Left & Right top bevel arcs
    for (int side : {-1, 1}) {
        float cx = side * IW;
        float cy = HALF_H - BEV_R;

        for (int k = 0; k < BEV_N; ++k) {
            float t0 = static_cast<float>(k) / BEV_N;
            float t1 = static_cast<float>(k + 1) / BEV_N;

            float a0 = static_cast<float>(M_PI / 2.0 * (1.0f - t0));
            float a1 = static_cast<float>(M_PI / 2.0 * (1.0f - t1));
            if (side < 0) { a0 = static_cast<float>(M_PI) - a0; a1 = static_cast<float>(M_PI) - a1; }

            float x0 = cx + BEV_R * std::cos(a0), y0 = cy + BEV_R * std::sin(a0);
            float x1 = cx + BEV_R * std::cos(a1), y1 = cy + BEV_R * std::sin(a1);

            float nx0 = std::cos(a0), ny0 = std::sin(a0);
            float nx1 = std::cos(a1), ny1 = std::sin(a1);

            push_quad(m,
                {x0, y0, -DEPTH, nx0, ny0, 0, (x0 / (2 * HALF_W) + 0.5f), 0.0f},
                {x1, y1, -DEPTH, nx1, ny1, 0, (x1 / (2 * HALF_W) + 0.5f), 0.0f},
                {x0, y0,  DEPTH, nx0, ny0, 0, (x0 / (2 * HALF_W) + 0.5f), 1.0f},
                {x1, y1,  DEPTH, nx1, ny1, 0, (x1 / (2 * HALF_W) + 0.5f), 1.0f});
        }
    }

    float SIDE_Y = HALF_H - BEV_R;

    // Front Face
    push_quad(m,
        {-IW, -HALF_H, DEPTH, 0, 0, 1, 0.1f, 1.0f},
        { IW, -HALF_H, DEPTH, 0, 0, 1, 0.9f, 1.0f},
        {-IW,  SIDE_Y, DEPTH, 0, 0, 1, 0.1f, 0.2f},
        { IW,  SIDE_Y, DEPTH, 0, 0, 1, 0.9f, 0.2f});

    for (int side : {-1, 1}) {
        float cx = side * IW;
        float cy = HALF_H - BEV_R;
        for (int k = 0; k < BEV_N; ++k) {
            float t0 = static_cast<float>(k) / BEV_N;
            float t1 = static_cast<float>(k + 1) / BEV_N;
            float a0 = static_cast<float>(M_PI / 2.0 * t0), a1 = static_cast<float>(M_PI / 2.0 * t1);
            if (side < 0) { a0 = static_cast<float>(M_PI) - a0; a1 = static_cast<float>(M_PI) - a1; }
            float x0 = cx + BEV_R * std::cos(a0), y0 = cy + BEV_R * std::sin(a0);
            float x1 = cx + BEV_R * std::cos(a1), y1 = cy + BEV_R * std::sin(a1);
            push_quad(m,
                {x0, -HALF_H, DEPTH, 0, 0, 1, 0.0f, 1.0f},
                {x1, -HALF_H, DEPTH, 0, 0, 1, 1.0f, 1.0f},
                {x0,  y0,     DEPTH, 0, 0, 1, 0.0f, 0.0f},
                {x1,  y1,     DEPTH, 0, 0, 1, 1.0f, 0.0f});
        }
    }

    // Back Face
    push_quad(m,
        {-IW, -HALF_H, -DEPTH, 0, 0, -1, 0.1f, 1.0f},
        { IW, -HALF_H, -DEPTH, 0, 0, -1, 0.9f, 1.0f},
        {-IW,  SIDE_Y, -DEPTH, 0, 0, -1, 0.1f, 0.2f},
        { IW,  SIDE_Y, -DEPTH, 0, 0, -1, 0.9f, 0.2f});

    for (int side : {-1, 1}) {
        float cx = side * IW;
        float cy = HALF_H - BEV_R;
        for (int k = 0; k < BEV_N; ++k) {
            float t0 = static_cast<float>(k) / BEV_N;
            float t1 = static_cast<float>(k + 1) / BEV_N;
            float a0 = static_cast<float>(M_PI / 2.0 * t0), a1 = static_cast<float>(M_PI / 2.0 * t1);
            if (side < 0) { a0 = static_cast<float>(M_PI) - a0; a1 = static_cast<float>(M_PI) - a1; }
            float x0 = cx + BEV_R * std::cos(a0), y0 = cy + BEV_R * std::sin(a0);
            float x1 = cx + BEV_R * std::cos(a1), y1 = cy + BEV_R * std::sin(a1);
            push_quad(m,
                {x0, -HALF_H, -DEPTH, 0, 0, -1, 0.0f, 1.0f},
                {x1, -HALF_H, -DEPTH, 0, 0, -1, 1.0f, 1.0f},
                {x0,  y0,     -DEPTH, 0, 0, -1, 0.0f, 0.0f},
                {x1,  y1,     -DEPTH, 0, 0, -1, 1.0f, 0.0f});
        }
    }

    // Left and Right walls
    push_quad(m,
        {-HALF_W, -HALF_H, -DEPTH, -1, 0, 0, 0.0f, 1.0f},
        {-HALF_W, -HALF_H,  DEPTH, -1, 0, 0, 1.0f, 1.0f},
        {-HALF_W,  SIDE_Y, -DEPTH, -1, 0, 0, 0.0f, 0.0f},
        {-HALF_W,  SIDE_Y,  DEPTH, -1, 0, 0, 1.0f, 0.0f});

    push_quad(m,
        {HALF_W, -HALF_H, -DEPTH, 1, 0, 0, 0.0f, 1.0f},
        {HALF_W, -HALF_H,  DEPTH, 1, 0, 0, 1.0f, 1.0f},
        {HALF_W,  SIDE_Y, -DEPTH, 1, 0, 0, 0.0f, 0.0f},
        {HALF_W,  SIDE_Y,  DEPTH, 1, 0, 0, 1.0f, 0.0f});

    // Bottom Face
    push_quad(m,
        {-HALF_W, -HALF_H, -DEPTH, 0, -1, 0, 0.0f, 0.0f},
        { HALF_W, -HALF_H, -DEPTH, 0, -1, 0, 1.0f, 0.0f},
        {-HALF_W, -HALF_H,  DEPTH, 0, -1, 0, 0.0f, 1.0f},
        { HALF_W, -HALF_H,  DEPTH, 0, -1, 0, 1.0f, 1.0f});

    result.surfaces.push_back(std::move(m));

    // Collision outline with beveled top corners
    result.collision_outline.clear();
    result.collision_outline.push_back({-HALF_W, -HALF_H});
    result.collision_outline.push_back({ HALF_W, -HALF_H});
    result.collision_outline.push_back({ HALF_W,  SIDE_Y});
    // Right bevel
    for (int k = 1; k <= BEV_N; ++k) {
        float a = static_cast<float>(M_PI / 2.0 * k / BEV_N);
        result.collision_outline.push_back({IW + BEV_R * std::cos(a), SIDE_Y + BEV_R * std::sin(a)});
    }
    // Left bevel
    for (int k = 0; k <= BEV_N; ++k) {
        float a = static_cast<float>(M_PI / 2.0 * (1.0f + static_cast<float>(k) / BEV_N));
        result.collision_outline.push_back({-IW + BEV_R * std::cos(a), SIDE_Y + BEV_R * std::sin(a)});
    }
    result.collision_outline.push_back({-HALF_W, SIDE_Y});

    // Enforce CCW
    double area = 0;
    auto& hull = result.collision_outline;
    for (size_t i = 0; i < hull.size(); ++i) {
        size_t j = (i + 1) % hull.size();
        area += hull[i].first * hull[j].second - hull[j].first * hull[i].second;
    }
    if (area < 0) std::reverse(hull.begin(), hull.end());

    result.depth_z = DEPTH + 10.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §9. ZypherModel — model mesh import
//
// [NOTE / WARNING]:
// EXPERIMENTAL — DO NOT WIRE TO GUI FOR NOW.
// Earlier experiments converting arbitrary complex character PODs (e.g. hiro.POD)
// into GroundMesh caused engine crashes in vanilla Swordigo.
// Deeper research is required on vanilla GroundMeshRenderer limitations.
// ─────────────────────────────────────────────────────────────────────────────

GeneratedMesh generate_model_mesh(const std::vector<ModelSubmesh>& submeshes,
                                  const ModelImportParams& params) {
    clear_decline();
    GeneratedMesh result;
    if (submeshes.empty()) { result.error = "no submeshes"; return result; }

    std::vector<std::pair<double,double>> all_xy;

    for (const auto& msm : submeshes) {
        Submesh sm;
        sm.kind = "SurfaceMesh";
        sm.texture = msm.texture.empty() ? params.default_tex : msm.texture;
        sm.indexed = true;

        for (const auto& v8 : msm.verts) {
            Vertex v;
            v.x = v8[0] * params.scale; v.y = v8[1] * params.scale; v.z = v8[2] * params.scale;
            v.nx = v8[3]; v.ny = v8[4]; v.nz = v8[5];
            v.u = v8[6]; v.v = v8[7];
            sm.vertices.push_back(v);
            all_xy.push_back({v.x, v.y});
        }
        for (uint32_t idx : msm.indices) {
            if (idx > 65535) continue;
            sm.indices.push_back(static_cast<uint16_t>(idx));
        }
        if (params.two_sided) {
            size_t base = sm.indices.size();
            for (size_t i = 0; i < base; i += 3) {
                sm.indices.push_back(sm.indices[i]);
                sm.indices.push_back(sm.indices[i+2]);
                sm.indices.push_back(sm.indices[i+1]);
            }
        }
        result.surfaces.push_back(std::move(sm));
    }

    // Convert all_xy pairs to V2 for ccw_hull_pairs
    {
        std::vector<zypher::V2> pts2d;
        pts2d.reserve(all_xy.size());
        for (const auto& p : all_xy) pts2d.push_back({static_cast<float>(p.first), static_cast<float>(p.second)});
        result.collision_outline = ccw_hull_pairs(pts2d);
    }
    double min_z = 1e9, max_z = -1e9;
    for (const auto& sm : result.surfaces)
        for (const auto& v : sm.vertices) {
            min_z = std::min(min_z, (double)v.z);
            max_z = std::max(max_z, (double)v.z);
        }
    result.depth_z = std::max({std::fabs(min_z), std::fabs(max_z), 30.0}) + 15.0;
    compute_aabb(result);
    result.ok = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// §10. ZypherEmitter — shared protobuf emission
// ─────────────────────────────────────────────────────────────────────────────

std::string emit_scene_object(const GeneratedMesh& mesh,
                              const std::string& identifier,
                              double pos_x, double pos_y,
                              const ComponentIds& ids,
                              const std::vector<std::string>& extra_textures) {
    if (!mesh.ok) return {};

    // Collect unique textures
    std::vector<std::string> textures;
    auto add_tex = [&](const std::string& t) {
        if (!t.empty() && std::find(textures.begin(), textures.end(), t) == textures.end())
            textures.push_back(t);
    };
    for (const auto& sm : mesh.surfaces) add_tex(sm.texture);
    for (const auto& fm : mesh.fronts)   add_tex(fm.texture);
    for (const auto& et : extra_textures) add_tex(et);

    // GroundPolygon (strictly CCW winding enforced via shoelace area)
    std::vector<std::pair<double, double>> ccw_outline = mesh.collision_outline;
    double signed_area = 0.0;
    for (size_t i = 0; i < ccw_outline.size(); ++i) {
        size_t next = (i + 1) % ccw_outline.size();
        signed_area += (ccw_outline[i].first * ccw_outline[next].second - ccw_outline[next].first * ccw_outline[i].second);
    }
    if (signed_area < 0.0) {
        std::reverse(ccw_outline.begin(), ccw_outline.end());
    }

    proto::Writer poly;
    for (const auto& pt : ccw_outline)
        poly.write_nested_field(1, pw_v2(static_cast<float>(pt.first),
                                         static_cast<float>(pt.second)));
    poly.write_varint_field(2, 0); // Convex: 0
    poly.write_varint_field(3, 1); // Closed: 1

    float dz = static_cast<float>(mesh.depth_z);
    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1);         // Collides
    gpc.write_float_field(4, -dz);        // MinDepth
    gpc.write_float_field(5,  dz);        // MaxDepth

    proto::Writer comp_poly;
    comp_poly.write_string_field(1, "GroundPolygon");
    comp_poly.write_varint_field(2, static_cast<uint64_t>(ids.polygon_id));
    comp_poly.write_nested_field(kWireGroundPolygon, gpc);

    // GroundMeshComponent
    float ax = static_cast<float>(mesh.aabb_x), ay = static_cast<float>(mesh.aabb_y);
    float aw = static_cast<float>(mesh.aabb_w + 20.f), ah = static_cast<float>(mesh.aabb_h + 20.f);
    proto::Writer gmc;
    gmc.write_nested_field(7, pw_rect(ax - 10.f, ay - 10.f, aw, ah));
    for (const auto& sm : mesh.surfaces) gmc.write_nested_field(8, pack_submesh(sm));
    for (const auto& fm : mesh.fronts)   gmc.write_nested_field(9, pack_submesh(fm));
    gmc.write_nested_field(10, pw_color(1, 1, 1));

    proto::Writer comp_mesh;
    comp_mesh.write_string_field(1, "GroundMesh");
    comp_mesh.write_varint_field(2, static_cast<uint64_t>(ids.mesh_id));
    comp_mesh.write_nested_field(kWireGroundMesh, gmc);

    // GroundMeshGenerator
    proto::Writer ggc;
    ggc.write_varint_field(1, static_cast<uint64_t>(ids.polygon_id));
    ggc.write_varint_field(2, static_cast<uint64_t>(ids.mesh_id));
    ggc.write_varint_field(3, static_cast<uint64_t>(ids.tm_front_id));
    ggc.write_varint_field(4, static_cast<uint64_t>(ids.tm_surface_id));
    ggc.write_varint_field(5, 1291618994ULL);
    ggc.write_float_field(6, 0.f);
    ggc.write_varint_field(7, 1);
    ggc.write_float_field(8, aw);
    ggc.write_float_field(9, ah);
    ggc.write_float_field(10, aw * 0.5f);
    ggc.write_float_field(11, ah * 0.5f);

    proto::Writer comp_gen;
    comp_gen.write_string_field(1, "GroundMeshGenerator");
    comp_gen.write_varint_field(2, static_cast<uint64_t>(ids.generator_id));
    comp_gen.write_nested_field(kWireGenerator, ggc);

    // CollisionShape
    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1);
    csc.write_float_field(6, -dz);
    csc.write_float_field(7,  dz);
    csc.write_varint_field(11, 1);
    proto::Writer comp_col;
    comp_col.write_string_field(1, "CollisionShape");
    comp_col.write_varint_field(2, static_cast<uint64_t>(ids.collision_id));
    comp_col.write_varint_field(4, static_cast<uint64_t>(ids.polygon_id));
    comp_col.write_nested_field(kWireShape,     shape);
    comp_col.write_nested_field(kWireShapeComp, csc);

    // TextureMappings
    auto make_tm = [&](const std::string& tex, int cid) {
        proto::Writer tm;
        tm.write_string_field(1, tex);
        tm.write_float_field(2, 200.f);
        tm.write_nested_field(3, pw_v2(0, 0));
        proto::Writer c;
        c.write_string_field(1, "TextureMapping");
        c.write_varint_field(2, static_cast<uint64_t>(cid));
        c.write_nested_field(kWireTextureMapping, tm);
        return c;
    };

    // SceneObject
    proto::Writer obj;
    obj.write_string_field(1, "SceneObject");
    obj.write_string_field(2, identifier);
    obj.write_bytes_field(3, comp_poly.to_string());
    obj.write_bytes_field(3, comp_mesh.to_string());
    obj.write_bytes_field(3, comp_gen.to_string());
    obj.write_bytes_field(3, comp_col.to_string());
    int tm_cid = ids.tm_surface_id;
    for (const auto& tex : textures) {
        auto c = make_tm(tex, tm_cid++);
        obj.write_bytes_field(3, c.to_string());
    }
    obj.write_nested_field(4, pw_v2(static_cast<float>(pos_x), static_cast<float>(pos_y)));
    obj.write_float_field(5, 0.f); // Depth (standalone = 0, caller sets when injecting)
    obj.write_float_field(6, 0.f); // Rotation
    obj.write_float_field(7, 1.f); // Scaling
    obj.write_nested_field(8, pw_rect(ax - 10.f, ay - 10.f, aw, ah));
    obj.write_varint_field(9, 0);  // Active

    proto::Writer scene;
    scene.write_bytes_field(1, obj.to_string());
    return scene.to_string();
}

// ─────────────────────────────────────────────────────────────────────────────
// §11. Convenience wrappers
// ─────────────────────────────────────────────────────────────────────────────

static std::string gen_obj(const GeneratedMesh& m, const std::string& id,
                            const ComponentIds* ids_in) {
    if (!m.ok) return {};
    ComponentIds ids = ids_in ? *ids_in : ComponentIds{};
    return emit_scene_object(m, id, 0.0, 0.0, ids);
}

std::string generate_terrain_object(const std::vector<TerrainNode>& nodes,
                                    const TerrainParams& params,
                                    const std::string& identifier,
                                    const ComponentIds* ids) {
    return gen_obj(generate_terrain(nodes, params), identifier, ids);
}

std::string generate_sphere_object(const SphereParams& p, const std::string& id, const ComponentIds* ids) {
    return gen_obj(generate_sphere(p), id, ids);
}

std::string generate_arch_object(const ArchParams& p, const std::string& id, const ComponentIds* ids) {
    return gen_obj(generate_arch(p), id, ids);
}

std::string generate_torus_object(const TorusParams& p, const std::string& id, const ComponentIds* ids) {
    return gen_obj(generate_torus(p), id, ids);
}

std::string generate_pillar_object(const PillarParams& p, const std::string& id, const ComponentIds* ids) {
    return gen_obj(generate_pillar(p), id, ids);
}

std::string generate_dome_object(const DomeParams& p, const std::string& id, const ComponentIds* ids) {
    return gen_obj(generate_dome(p), id, ids);
}

std::string generate_bevel_platform_object(const BevelPlatformParams& p, const std::string& id, const ComponentIds* ids) {
    return gen_obj(generate_bevel_platform(p), id, ids);
}

// ─────────────────────────────────────────────────────────────────────────────
// §12. GroundGenerator selector
// ─────────────────────────────────────────────────────────────────────────────

static GroundGenerator g_generator = [] {
    const char* env = std::getenv("RUBY_GROUND_GENERATOR");
    if (env) {
        if (std::strcmp(env, "zypher")  == 0) return GroundGenerator::Zypher;
        if (std::strcmp(env, "boulderx") == 0 ||
            std::strcmp(env, "zenith")  == 0)  return GroundGenerator::BoulderX;
        if (std::strcmp(env, "boulder") == 0)  return GroundGenerator::Boulder;
    }
    return GroundGenerator::Boulder;
}();

GroundGenerator ground_generator() { return g_generator; }
void set_ground_generator(GroundGenerator w) { g_generator = w; }

const char* ground_generator_id(GroundGenerator w) {
    switch (w) {
        case GroundGenerator::Zypher:  return "zypher";
        case GroundGenerator::BoulderX: return "boulderx";
        default:                       return "boulder";
    }
}

GroundGenerator ground_generator_from_id(const std::string& id) {
    if (id == "zypher")              return GroundGenerator::Zypher;
    if (id == "boulderx" || id == "zenith") return GroundGenerator::BoulderX;
    return GroundGenerator::Boulder;
}

} // namespace zypher
