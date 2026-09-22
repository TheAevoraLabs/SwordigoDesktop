// ============================================================================
// boulderx.cpp — Caver-faithful ground-mesh generator (see boulderx.h)
//
// Structure mirrors the engine's own call graph:
//
//   generate()
//     ├─ depth arrays        <- GroundMeshGeneratorComponent::GenerateMesh (arm32 0x2D516C)
//     ├─ build_cap()         <- GroundMeshGenerator::GenerateFrontMesh     (0x2D4858)
//     ├─ build_ribbon()      <- GroundMeshGenerator::GenerateSurfaceMesh   (0x2D3690)
//     └─ build_rim()         <- InsertRoundHatVertices                     (0x2D3BDC)
//
// The three verification sources for every constant in here are:
//   1. the arm32 decompiles named above (arm32 chosen where it decompiles better),
//   2. the 2,538 baked ground objects in Scener/data/decoded_scenes, and
//   3. tests/boulderx_engine_parity_test.cpp, which regenerates the engine's own
//      output and compares.
// ============================================================================

#include "tools/boulderx.h"
#include "tools/swdm_format.h"
#include "platform/protobuf_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace boulderx {
namespace {

constexpr size_t kVertexStride = 32;   // pos(12) + normal(12) + uv(8)
constexpr size_t kTriSize = 6;         // 3 x uint16
constexpr double kPi = 3.14159265358979323846;

// ── Decline recording ───────────────────────────────────────────────────────
// Every public entry point clears this on entry and sets it before returning an
// empty string, so `last_decline_reason()` always describes the most recent
// call. See the header for why this is API rather than a log line.
std::string g_decline_reason;

// Records why an entry point is about to return an empty string, then returns
// it. Routing every refusal through here is what keeps the reason and the empty
// result from ever disagreeing.
std::string decline(const std::string& reason) {
    g_decline_reason = reason;
    return std::string();
}

// ── Deterministic per-node depth jitter ─────────────────────────────────────
// The engine calls Caver::frand() (a seeded LCG) once per depth. The exact
// recurrence is not recoverable from the decompile and does not need to be:
// HorizNoise is 0 in 1,859 of the 2,223 shipped generator components, and where
// it is non-zero only the *shape* of the jitter matters (the same reasoning
// docs/formats_and_schemas/scenecreator/12_algorithms.md already records for
// scene_generator's splitmix64). What matters is that it is deterministic:
// same seed -> same mesh, forever, which is what the engine guarantees
// (§5 of doc 09).
struct Frnd {
    uint32_t state;
    explicit Frnd(uint32_t seed) : state(seed ? seed : 1u) {}
    double next() {   // [0,1)
        state = state * 1664525u + 1013904223u;
        return static_cast<double>((state >> 8) & 0xFFFFFFu) / 16777216.0;
    }
};

// ── 2D helpers ──────────────────────────────────────────────────────────────
struct V2 { double x = 0, y = 0; };

V2 normalize2(V2 v) {
    const double len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len < 1e-12) return {0.0, 0.0};
    return {v.x / len, v.y / len};
}

// Outward edge normal for a CCW polygon, exactly as the engine's
// GenerateSurfaceMesh builds it (obj12 vertex 0 -> (-0.984, -0.181) from the
// edge (-186.595,101.816)->(-149.193,-101.816)).
V2 edge_normal(const Node& a, const Node& b) {
    return normalize2({b.y - a.y, -(b.x - a.x)});
}

// Average of the two adjacent edge normals, normalized. Verified against the
// shipped mesh: obj12 vertex 0 normal (0.893, -0.449) for point (186.595,99.662).
V2 vertex_normal(const std::vector<Node>& p, int i) {
    const int n = static_cast<int>(p.size());
    const V2 n1 = edge_normal(p[(i - 1 + n) % n], p[i]);
    const V2 n2 = edge_normal(p[i], p[(i + 1) % n]);
    return normalize2({n1.x + n2.x, n1.y + n2.y});
}

struct V3 { double x = 0, y = 0, z = 0; };

V3 cross3(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

V3 normalize3(V3 v) {
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12) return {0.0, 0.0, 1.0};
    return {v.x / len, v.y / len, v.z / len};
}

// TextureMapping::TexCoordForPosition: UVs are derived from WORLD POSITION —
// v = 0.5 + z/Scale on the ribbons, and (x/Scale + 0.5, y/Scale + 0.5) on the
// cap. Both confirmed byte for byte against obj12 (uv.v 0.08 at z=-105 with
// Scale 250) and its FrontMesh (uv (-0.246, 0.907) at (-186.595, 101.816)).
inline double tex_v(double z, double scale) { return 0.5 + z / scale; }

// ── Triangulation predicates (Caver::IsConvexVertex / PointInsideTriangle) ──
// cross(a,b,c) == (a-c) x (b-c), the same form the engine's
// PointInsideTriangle(a,b,c,t) uses:
//   (a-c)x(t-c) >= 0 && (b-a)x(t-a) >= 0 && (c-b)x(t-b) >= 0
// Note ">=" — boundary points count as inside, as in the engine.
// crosso(P, Q) == P x Q about the origin.
inline double crosso(const V2& p, const V2& q) { return p.x * q.y - p.y * q.x; }
inline V2 sub(const V2& p, const V2& q) { return {p.x - q.x, p.y - q.y}; }

// Caver::PointInsideTriangle(a,b,c,t) verbatim:
//   (a-c)x(t-c) >= 0 && (b-a)x(t-a) >= 0 && (c-b)x(t-b) >= 0
// "Inside" is inclusive, so a point exactly on an edge BLOCKS the ear (which is
// why the engine under-clips polygons with collinear runs).
bool point_inside_triangle(const V2& a, const V2& b, const V2& c, const V2& t) {
    return crosso(sub(a, c), sub(t, c)) >= 0.0 &&
           crosso(sub(b, a), sub(t, a)) >= 0.0 &&
           crosso(sub(c, b), sub(t, b)) >= 0.0;
}

// Caver::IsConvexVertex compares the POLAR ANGLE of the two edge vectors leaving
// the vertex and tests the wrapped difference — not a raw cross-product sign.
// For a CCW polygon that is equivalent to cross(prev->cur, cur->next) > 0.
bool is_convex_vertex(const std::vector<V2>& v, int i) {
    const int n = static_cast<int>(v.size());
    const V2& prev = v[(i - 1 + n) % n];
    const V2& cur = v[i];
    const V2& next = v[(i + 1) % n];
    const double a_in = std::atan2(cur.y - prev.y, cur.x - prev.x);
    const double a_out = std::atan2(next.y - cur.y, next.x - cur.x);
    double d = a_out - a_in;
    while (d <= -kPi) d += 2.0 * kPi;
    while (d > kPi) d -= 2.0 * kPi;
    return d > 0.0;   // CCW turn at the vertex => convex
}

// ── Submesh builders ────────────────────────────────────────────────────────
void push_vertex(Submesh& sm, const Vertex& v) { sm.vertices.push_back(v); }

// GroundMeshGenerator::GenerateFrontMesh — the camera-facing cap.
//
// Ear-clips the outline and emits ONE NON-INDEXED triangle per ear (that is why
// InitializeMeshBuilder is called with indices=false and why shipped FrontMesh
// entries have an empty IndexData). Each corner takes its z from the per-vertex
// front-depth array, and the normal is the true 3D face normal
// normalize(cross(V1-V0, V2-V1)) — flat (0,0,1) when all three depths match,
// genuinely oblique once HorizNoise tilts them (fire_part2 obj3#32:
// n = (0.26, -0.40, 0.88)).
Submesh build_cap(const std::vector<Node>& outline, const std::vector<double>& front,
                  const Params& params) {
    Submesh sm;
    sm.kind = "FrontMesh";
    sm.texture = params.front_texture;
    sm.indexed = false;

    std::vector<std::pair<double, double>> flat;
    flat.reserve(outline.size());
    for (const auto& n : outline) flat.emplace_back(n.x, n.y);

    std::vector<uint16_t> tris;
    if (!triangulate(flat, tris)) return sm;

    const double scale = params.texture_scale > 0 ? params.texture_scale : 250.0;
    // Ear clipping emits triangle corners in outline order for the OUTLINE
    // winding; the engine walks the same ear set.
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const int i0 = tris[t], i1 = tris[t + 1], i2 = tris[t + 2];
        const V3 v0{outline[i0].x, outline[i0].y, front[i0]};
        const V3 v1{outline[i1].x, outline[i1].y, front[i1]};
        const V3 v2{outline[i2].x, outline[i2].y, front[i2]};
        const V3 nrm = normalize3(cross3({v1.x - v0.x, v1.y - v0.y, v1.z - v0.z},
                                         {v2.x - v1.x, v2.y - v1.y, v2.z - v1.z}));
        const V3 corners[3] = {v0, v1, v2};
        for (const V3& c : corners) {
            Vertex out;
            out.x = static_cast<float>(c.x);
            out.y = static_cast<float>(c.y);
            out.z = static_cast<float>(c.z);
            out.nx = static_cast<float>(nrm.x);
            out.ny = static_cast<float>(nrm.y);
            out.nz = static_cast<float>(nrm.z);
            out.u = static_cast<float>(c.x / scale + 0.5);
            out.v = static_cast<float>(c.y / scale + 0.5);
            push_vertex(sm, out);
        }
    }
    return sm;
}

// GroundMeshGenerator::GenerateSurfaceMesh — the perimeter ribbon.
//
// TWO vertices per outline point, one on the back plane and one on the front
// plane, sharing the point's XY normal. Vertices are emitted back-then-front
// (shipped: obj12's first pair is z=-100 then z=+100), UV u advances with
// cumulative arc length and v tracks z. Indices are the engine's own quad
// split, which is why the shipped ring reads [0,2,3, 1,0,3, 2,4,5, 3,2,5, ...].
Submesh build_ribbon(const std::vector<Node>& outline, const std::vector<double>& back,
                     const std::vector<double>& front, const std::string& texture,
                     const Params& params, bool close_loop) {
    Submesh sm;
    sm.kind = "SurfaceMesh";
    sm.texture = texture;
    sm.indexed = true;

    const size_t n = outline.size();
    const double scale = params.texture_scale > 0 ? params.texture_scale : 250.0;
    double arc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            const double dx = outline[i].x - outline[i - 1].x;
            const double dy = outline[i].y - outline[i - 1].y;
            arc += std::sqrt(dx * dx + dy * dy);
        }
        const V2 nrm = vertex_normal(outline, static_cast<int>(i));
        const double u = 0.5 + arc / scale;
        Vertex b;
        b.x = static_cast<float>(outline[i].x);
        b.y = static_cast<float>(outline[i].y);
        b.z = static_cast<float>(back[i]);
        b.nx = static_cast<float>(nrm.x);
        b.ny = static_cast<float>(nrm.y);
        b.u = static_cast<float>(u);
        b.v = static_cast<float>(tex_v(back[i], scale));
        push_vertex(sm, b);
        Vertex f = b;
        f.z = static_cast<float>(front[i]);
        f.v = static_cast<float>(tex_v(front[i], scale));
        push_vertex(sm, f);
    }

    const size_t segments = close_loop ? n : n - 1;
    for (size_t i = 0; i < segments; ++i) {
        const uint16_t a = static_cast<uint16_t>(2 * i);          // back, point i
        const uint16_t b = static_cast<uint16_t>(2 * i + 1);      // front, point i
        const size_t j = (i + 1) % n;
        const uint16_t c = static_cast<uint16_t>(2 * j);          // back, point j
        const uint16_t d = static_cast<uint16_t>(2 * j + 1);      // front, point j
        sm.indices.push_back(a); sm.indices.push_back(c); sm.indices.push_back(d);
        sm.indices.push_back(b); sm.indices.push_back(a); sm.indices.push_back(d);
    }
    return sm;
}

// GroundMeshGenerator::InsertRoundHatVertices — the rounded rim.
//
// Sweeps a 6-point cross-section around the outline. The (inward, z) profile is
// taken from the shipped ring at obj12's corner (186.595, 99.662) with
// D/2 = 100, HatHeight = 25, HatWidthOffset = 5:
//
//   rho=0      z = -105 / +105     normal = +/-outward XY   (the outer edge)
//   rho=0.4H   z = +110 / -110     normal = +Z              (the bevel bulge)
//   rho=H      z = +105.1 / -104.9 normal = -/+outward XY   (the inner wall)
//
// and the bevel sits W beyond the front/back planes. `H + 0.1` is the engine's
// anti-z-fighting nudge, kept verbatim (including its sign asymmetry).
Submesh build_rim(const std::vector<Node>& outline, const std::vector<double>& back,
                  const std::vector<double>& front, const Params& params) {
    Submesh sm;
    sm.kind = "SurfaceMesh";
    sm.texture = params.surface_texture;
    sm.indexed = true;

    const size_t n = outline.size();
    const double H = params.hat_height;
    const double W = params.hat_width_offset_1;
    const double scale = params.texture_scale > 0 ? params.texture_scale : 250.0;

    double arc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            const double dx = outline[i].x - outline[i - 1].x;
            const double dy = outline[i].y - outline[i - 1].y;
            arc += std::sqrt(dx * dx + dy * dy);
        }
        const V2 out = vertex_normal(outline, static_cast<int>(i));
        const V2 in{-out.x, -out.y};
        const double zf = front[i] + W;    // bevel outer edge, camera side
        const double zb = back[i] - W;     // bevel outer edge, far side
        const double u = 0.5 + arc / scale;

        // Cross-section (inward offset, z). z values are taken from the shipped
        // ring at obj12: front plane 100, HatHeight 25, W 5 -> the bevel bulges
        // to 110 = front + 2W and the inner wall sits at 105.1 = front + W + 0.1.
        struct Prof { double rho, z, nx, ny, nz; };
        const Prof prof[6] = {
            {0.0,        zb,          out.x,  out.y,  0.0},
            {0.0,        zf,          out.x,  out.y,  0.0},
            {0.4 * H,    zf + W,      0.0,    0.0,    1.0},
            {H,          zf + 0.1,    in.x,   in.y,   0.0},
            {H,          zb + 0.1,    in.x,   in.y,   0.0},
            {0.4 * H,    zb - W,      0.0,    0.0,    1.0},
        };
        for (const Prof& p : prof) {
            Vertex v;
            v.x = static_cast<float>(outline[i].x + in.x * p.rho);
            v.y = static_cast<float>(outline[i].y + in.y * p.rho);
            v.z = static_cast<float>(p.z);
            v.nx = static_cast<float>(p.nx);
            v.ny = static_cast<float>(p.ny);
            v.nz = static_cast<float>(p.nz);
            v.u = static_cast<float>(u);
            v.v = static_cast<float>(tex_v(p.z, scale));
            push_vertex(sm, v);
        }
    }

    // Sweep the 6 profile edges between consecutive rings, closing the loop.
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        for (int k = 0; k < 6; ++k) {
            const int k2 = (k + 1) % 6;
            const uint16_t a = static_cast<uint16_t>(6 * i + k);
            const uint16_t b = static_cast<uint16_t>(6 * i + k2);
            const uint16_t c = static_cast<uint16_t>(6 * j + k);
            const uint16_t d = static_cast<uint16_t>(6 * j + k2);
            sm.indices.push_back(a); sm.indices.push_back(c); sm.indices.push_back(b);
            sm.indices.push_back(b); sm.indices.push_back(c); sm.indices.push_back(d);
        }
    }
    return sm;
}

// ── Binary emit helpers (field numbers verified in boulder.h / doc 09 §3) ────
proto::Writer make_vector2(double x, double y) {
    proto::Writer w;
    w.write_float_field(1, static_cast<float>(x));
    w.write_float_field(2, static_cast<float>(y));
    return w;
}

proto::Writer make_rectangle(double x, double y, double width, double height) {
    proto::Writer w;
    w.write_float_field(1, static_cast<float>(x));
    w.write_float_field(2, static_cast<float>(y));
    w.write_float_field(3, static_cast<float>(width));
    w.write_float_field(4, static_cast<float>(height));
    return w;
}

proto::Writer make_float_color(float r, float g, float b, float a) {
    proto::Writer w;
    w.write_float_field(1, r);
    w.write_float_field(2, g);
    w.write_float_field(3, b);
    w.write_float_field(4, a);
    return w;
}

proto::Writer make_mesh_material(const std::string& texture_name) {
    proto::Writer tex;
    tex.write_string_field(1, texture_name);
    tex.write_varint_field(2, 1);
    tex.write_varint_field(4, 1);
    proto::Writer mat;
    mat.write_nested_field(1, make_float_color(1, 1, 1, 1));
    mat.write_nested_field(2, make_float_color(1, 1, 1, 1));
    mat.write_nested_field(3, make_float_color(1, 1, 1, 1));
    mat.write_float_field(4, 0.0f);
    mat.write_nested_field(5, tex);
    return mat;
}

proto::Writer make_mesh_data(int value_type, int values_per_vertex, int stride,
                             int data_offset) {
    proto::Writer w;
    w.write_varint_field(1, static_cast<uint64_t>(value_type));
    w.write_varint_field(2, static_cast<uint64_t>(values_per_vertex));
    w.write_varint_field(3, static_cast<uint64_t>(stride));
    w.write_varint_field(4, static_cast<uint64_t>(data_offset));
    return w;
}

proto::Writer make_box(float x, float y, float z, float width, float height, float depth) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, z);
    w.write_float_field(4, width);
    w.write_float_field(5, height);
    w.write_float_field(6, depth);
    return w;
}

void append_float(std::vector<uint8_t>& out, double v) {
    float f = static_cast<float>(v);
    uint8_t b[4];
    std::memcpy(b, &f, 4);
    out.insert(out.end(), b, b + 4);
}

void append_ushort(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

// Interleaved stream: pos(3f) @0, normal(3f) @12, uv(2f) @24, stride 32 — the
// exact layout doc 09 §3 records for every shipped SurfaceMesh/FrontMesh.
void vertex_bits_and_bounds(const Submesh& sm, std::vector<uint8_t>& bits,
                            float* mn, float* mx) {
    mn[0] = mn[1] = mn[2] = 1e9f;
    mx[0] = mx[1] = mx[2] = -1e9f;
    for (const Vertex& v : sm.vertices) {
        const float p[8] = {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v};
        for (float f : p) append_float(bits, f);
        for (int c = 0; c < 3; ++c) {
            const float val = p[c];
            if (val < mn[c]) mn[c] = val;
            if (val > mx[c]) mx[c] = val;
        }
    }
    if (sm.vertices.empty()) { mn[0] = mn[1] = mn[2] = 0.0f; mx[0] = mx[1] = mx[2] = 0.0f; }
}

proto::Writer make_mesh(const Submesh& sm) {
    proto::Writer w;
    std::vector<uint8_t> vbits, ibits;
    float mn[3], mx[3];
    vertex_bits_and_bounds(sm, vbits, mn, mx);
    for (uint16_t idx : sm.indices) append_ushort(ibits, idx);

    const size_t num_vertices = sm.vertices.size();
    const size_t num_faces = sm.indexed
        ? sm.indices.size() / 3
        : sm.vertices.size() / 3;   // non-indexed: one triangle per 3 vertices
    w.write_varint_field(1, num_vertices);
    w.write_varint_field(2, num_faces);
    if (sm.indexed && !ibits.empty())
        w.write_nested_field(3, make_mesh_data(4, 1, 2, 0));
    w.write_nested_field(4, make_mesh_data(7, 3, 32, 0));
    w.write_nested_field(5, make_mesh_data(7, 3, 32, 12));
    w.write_nested_field(6, make_mesh_data(7, 2, 32, 24));
    w.write_nested_field(10, make_mesh_material(sm.texture));
    w.write_nested_field(11, make_box(mn[0], mn[1], mn[2],
                                      mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]));
    w.write_bytes_field(50, std::string(vbits.begin(), vbits.end()));
    if (sm.indexed && !ibits.empty())
        w.write_bytes_field(51, std::string(ibits.begin(), ibits.end()));
    return w;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front())
        return s.substr(1, s.size() - 2);
    return s;
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

size_t Mesh::vertex_count() const {
    size_t n = 0;
    for (const auto& s : surfaces) n += s.vertices.size();
    for (const auto& s : fronts) n += s.vertices.size();
    return n;
}

size_t Mesh::triangle_count() const {
    size_t n = 0;
    for (const auto& s : surfaces) n += s.indices.size() / 3;
    for (const auto& s : fronts) n += s.vertices.size() / 3;
    return n;
}

double Mesh::max_abs_z() const {
    double m = 0.0;
    for (const auto& s : surfaces)
        for (const auto& v : s.vertices) m = std::max(m, std::fabs(static_cast<double>(v.z)));
    for (const auto& s : fronts)
        for (const auto& v : s.vertices) m = std::max(m, std::fabs(static_cast<double>(v.z)));
    return m;
}

void Mesh::bounds(double& x, double& y, double& w, double& h) const {
    x = y = 1e18;
    double mx = -1e18, my = -1e18;
    auto scan = [&](const Submesh& s) {
        for (const auto& v : s.vertices) {
            x = std::min(x, static_cast<double>(v.x));
            y = std::min(y, static_cast<double>(v.y));
            mx = std::max(mx, static_cast<double>(v.x));
            my = std::max(my, static_cast<double>(v.y));
        }
    };
    for (const auto& s : surfaces) scan(s);
    for (const auto& s : fronts) scan(s);
    if (x > mx) { x = y = 0; w = h = 0; return; }
    w = mx - x;
    h = my - y;
}

double signed_area(const std::vector<Node>& outline) {
    double a = 0.0;
    const size_t n = outline.size();
    for (size_t i = 0; i < n; ++i) {
        const Node& p = outline[i];
        const Node& q = outline[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * a;
}

void ensure_ccw(std::vector<Node>& outline) {
    if (outline.size() >= 3 && signed_area(outline) < 0.0)
        std::reverse(outline.begin(), outline.end());
}

void set_uniform_depth(std::vector<Node>& outline, double half_depth) {
    for (auto& n : outline) {
        n.front_depth = half_depth;
        n.back_depth = -half_depth;
    }
}

void append_rect(std::vector<Node>& outline, double x, double y, double w, double h,
                 double front_depth, double back_depth) {
    const Node n[4] = {{x, y, front_depth, back_depth},
                       {x + w, y, front_depth, back_depth},
                       {x + w, y + h, front_depth, back_depth},
                       {x, y + h, front_depth, back_depth}};
    for (const Node& v : n) outline.push_back(v);
}

void append_polyline(std::vector<Node>& outline,
                     const std::vector<std::pair<double, double>>& xy,
                     double front_depth, double back_depth) {
    for (const auto& p : xy)
        outline.push_back({p.first, p.second, front_depth, back_depth});
}

void append_arc(std::vector<Node>& outline, double cx, double cy, double radius,
                double start_deg, double end_deg, int segments,
                double front_depth, double back_depth) {
    if (segments < 1) segments = 1;
    const double a0 = start_deg * kPi / 180.0;
    const double a1 = end_deg * kPi / 180.0;
    for (int i = 0; i <= segments; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double a = a0 + (a1 - a0) * t;
        outline.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a),
                           front_depth, back_depth});
    }
}

bool triangulate(const std::vector<std::pair<double, double>>& polygon,
                 std::vector<uint16_t>& out_indices) {
    out_indices.clear();
    if (polygon.size() < 3) return false;

    const size_t n = polygon.size();
    auto at = [&](int k) {
        const auto& p = polygon[static_cast<size_t>(k)];
        return V2{p.first, p.second};
    };

    // The engine's polygons are CCW (`Convex : 0`, `Closed : 1` everywhere in
    // the shipped data). Normalise so the ear test's sign is fixed, but keep the
    // ORIGINAL indices so the cap's vertices come out in outline order.
    double area = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const V2 a = at(static_cast<int>(i));
        const V2 b = at(static_cast<int>((i + 1) % n));
        area += a.x * b.y - b.x * a.y;
    }
    // A zero-area outline (all nodes collinear, or duplicated) has no interior:
    // refuse it rather than emit a solid collision shape with no visible mesh.
    if (std::fabs(area) * 0.5 <= 1e-9) return false;
    std::vector<int> orig(n);
    std::vector<V2> ring(n);
    for (size_t i = 0; i < n; ++i) orig[i] = static_cast<int>(i);
    if (area < 0.0) std::reverse(orig.begin(), orig.end());
    for (size_t i = 0; i < n; ++i) ring[i] = at(orig[i]);

    int guard = static_cast<int>(n) * static_cast<int>(n) * 2 + 16;
    while (ring.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const int m = static_cast<int>(ring.size());
        for (int i = 0; i < m; ++i) {
            const int ip = (i - 1 + m) % m;
            const int in = (i + 1) % m;
            const V2 a = ring[ip];
            const V2 b = ring[i];
            const V2 c = ring[in];
            // Caver::IsConvexVertex, on the triple as the ring presents it.
            // A collinear vertex (turn == 0) is not convex, so the engine never
            // clips it and its loop can end with a ring bigger than 3.
            if (!is_convex_vertex({a, b, c}, 1)) continue;
            // Caver::IsAnEar -> PointInsideTriangle for every other vertex.
            // "Inside" is >= 0 in the engine, so boundary points block the ear.
            bool contains = false;
            for (int k = 0; k < m; ++k) {
                if (k == ip || k == i || k == in) continue;
                if (point_inside_triangle(a, b, c, ring[k])) { contains = true; break; }
            }
            if (contains) continue;
            out_indices.push_back(static_cast<uint16_t>(orig[ip]));
            out_indices.push_back(static_cast<uint16_t>(orig[i]));
            out_indices.push_back(static_cast<uint16_t>(orig[in]));
            ring.erase(ring.begin() + i);
            orig.erase(orig.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    // The engine simply stops here, which is observable in shipped data: a
    // 24-vertex ground polygon in fire_part5.scene carries a 20-triangle cap
    // (60 vertices) rather than 22, i.e. two ears were never clipped and the
    // cap has holes over its collinear runs. We fan the leftover ring instead —
    // more triangles, never fewer, and no holes.
    while (ring.size() >= 3) {
        out_indices.push_back(static_cast<uint16_t>(orig[0]));
        out_indices.push_back(static_cast<uint16_t>(orig[1]));
        out_indices.push_back(static_cast<uint16_t>(orig[2]));
        ring.erase(ring.begin() + 1);
        orig.erase(orig.begin() + 1);
        if (ring.size() < 3) break;
    }
    // Correctness gate: the emitted ears plus the fan must tile the outline. If
    // the polygon self-intersects or is degenerate, the area will not match and
    // the caller gets a failed generation instead of a silent hole.
    double emitted = 0.0;
    for (size_t t = 0; t + 2 < out_indices.size(); t += 3) {
        const V2 a = at(out_indices[t]);
        const V2 b = at(out_indices[t + 1]);
        const V2 c = at(out_indices[t + 2]);
        emitted += 0.5 * std::fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
    }
    const double expected = std::fabs(area) * 0.5;
    const double tol = std::max(1e-6, expected * 1e-4);
    if (std::fabs(emitted - expected) > tol) return false;
    return !out_indices.empty();
}

bool generate(const std::vector<Node>& outline_in, const Params& params, Mesh& out) {
    out = Mesh{};
    if (outline_in.size() < 3) {
        out.error = "outline needs at least 3 nodes";
        return false;
    }
    if (params.texture_scale <= 0.0) {
        out.error = "texture_scale must be positive";
        return false;
    }

    std::vector<Node> outline = outline_in;
    ensure_ccw(outline);
    const size_t n = outline.size();

    // GroundMeshGeneratorComponent::GenerateMesh: one frand() per depth, so the
    // front and back planes jitter independently. HorizNoise == 0 (1,859 of
    // 2,223 shipped generators) makes the arrays exactly the authored depths.
    for (auto& node : outline)
        if (node.front_depth < node.back_depth)
            std::swap(node.front_depth, node.back_depth);   // editor-dragged values

    std::vector<double> front(n), back(n);
    Frnd rng(params.random_seed);
    for (size_t i = 0; i < n; ++i) {
        const double f = rng.next();
        front[i] = outline[i].front_depth + (f - 0.5) * params.horiz_noise;
        const double b = rng.next();
        back[i] = outline[i].back_depth - (b - 0.5) * params.horiz_noise;
    }

    // Field 8 order is significant: GenerateMesh adds the rim first, then the
    // plain-with-gaps ribbon. Shipped objects confirm it (SurfaceMesh #1 carries
    // SurfaceTextureMapping, #2 carries FrontTextureMapping).
    if (params.mesh_type == 1)
        out.surfaces.push_back(build_rim(outline, back, front, params));
    out.surfaces.push_back(build_ribbon(outline, back, front,
                                        params.mesh_type == 1 ? params.front_texture
                                                              : params.surface_texture,
                                        params, /*close_loop=*/true));
    out.fronts.push_back(build_cap(outline, front, params));

    if (out.fronts[0].vertices.empty()) {
        out.error = "triangulation failed (degenerate or self-intersecting outline)";
        return false;
    }
    out.ok = true;
    return true;
}

// ── .swdm text ──────────────────────────────────────────────────────────────
// A boulderx sheet is format v2: per-node depths, which v1 (boulder) has no way
// to store. The marker is emitted first so a reader can tell the dialects apart
// before it has parsed anything — boulder::parse_gmesh() relies on exactly that
// to refuse a v2 sheet rather than flatten it.
std::string serialize_swdm(const SwdmDocument& doc) {
    std::ostringstream o;
    o << "// Swordigo Desktop Mesh (.swdm) — BoulderX sheet, format v2\n";
    o << "// Per-node depth. Boulder (v1) cannot read this file; BoulderX reads both.\n";
    o << "GroundMesh{\n";
    o << "    " << swdm_format::kVersionKey << " : " << swdm_format::kBoulderxVersion << "\n";
    o << "    " << swdm_format::kGeneratorKey << " : '" << swdm_format::kBoulderxGenerator
      << "'\n";
    o << "    Identifier : '" << doc.identifier << "'\n";
    o << "    MeshType : " << doc.params.mesh_type << "\n";
    o << "    SurfaceWidth : " << doc.params.surface_width << "\n";
    o << "    HatHeight : " << doc.params.hat_height << "\n";
    o << "    HatWidthOffset1 : " << doc.params.hat_width_offset_1 << "\n";
    o << "    HatWidthOffset2 : " << doc.params.hat_width_offset_2 << "\n";
    o << "    HorizNoise : " << doc.params.horiz_noise << "\n";
    o << "    RandomSeed : " << doc.params.random_seed << "\n";
    o << "    TextureScale : " << doc.params.texture_scale << "\n";
    o << "    SurfaceTexture : '" << doc.params.surface_texture << "'\n";
    o << "    FrontTexture : '" << doc.params.front_texture << "'\n";
    o << "    NodeCount : " << doc.outline.size() << "\n";
    o.precision(9);
    for (const auto& n : doc.outline)
        o << "    Node : " << n.x << " " << n.y << " " << n.front_depth << " "
          << n.back_depth << "\n";
    o << "}\n";
    return o.str();
}

// Reads BOTH dialects of a sheet into the same per-node document:
//
//   v2 (boulderx) — `Node : x y front back`, one per vertex.
//   v1 (boulder)  — `MinDepth` / `MaxDepth` scalars plus a `Vertex[ x y ]` ring.
//                   Every node becomes that one depth pair, i.e. a uniform
//                   slab, which is the whole of what a v1 file says.
//
// The keys do not collide, so one reader handles both; only the ring differs.
// `from_boulder_dialect` records which one this was, and `has_dome_hats` records
// a v1 `Hat[` block so the object emitter can refuse rather than drop it.
bool parse_swdm(const std::string& text, SwdmDocument& out) {
    out = SwdmDocument{};
    const bool boulderx_sheet = swdm_format::is_boulderx_sheet(text);
    out.from_boulder_dialect = !boulderx_sheet;

    std::istringstream in(text);
    std::string line;
    int node_count = -1;

    // v1 depth, scalar for the whole sheet. Applied after the loop so a
    // `MinDepth` line before or after the `Vertex[` block behaves the same.
    double legacy_min = -45.0, legacy_max = 45.0;
    bool have_legacy_depth = false;

    int block = 0;   // 1 = Vertex[ … ], 2 = Hat[ … ]
    while (std::getline(in, line)) {
        const std::string t = trim(line);

        if (block != 0) {
            if (t.empty() || t == "]") { block = 0; continue; }
            std::istringstream vs(t);
            if (block == 1) {
                Node n;
                if (vs >> n.x >> n.y) out.outline.push_back(n);
            } else {
                // Four numbers means a real hat; the empty `Hat[\n]` block every
                // v1 sheet carries must NOT count as one.
                double hx = 0, hy = 0, hr = 0, hh = 0;
                if (vs >> hx >> hy >> hr >> hh) out.has_dome_hats = true;
            }
            continue;
        }

        if (t.empty() || t == "}" || t == "GroundMesh{") continue;
        if (t.rfind("Vertex[", 0) == 0 || t.rfind("Vertex [", 0) == 0) { block = 1; continue; }
        if (t.rfind("Hat[", 0) == 0 || t.rfind("Hat [", 0) == 0) { block = 2; continue; }

        // v2 writes `Key : value`, v1 writes `Key value`. Accept both.
        std::string key, val;
        const size_t colon = t.find(':');
        if (colon != std::string::npos) {
            key = trim(t.substr(0, colon));
            val = trim(t.substr(colon + 1));
        } else {
            const size_t sp = t.find_first_of(" \t");
            if (sp == std::string::npos) { key = t; }
            else { key = trim(t.substr(0, sp)); val = trim(t.substr(sp + 1)); }
        }
        val = unquote(val);

        try {
            if (key == "Identifier")        out.identifier = val;
            else if (key == "MeshType")     out.params.mesh_type = std::stoi(val);
            else if (key == "SurfaceWidth") out.params.surface_width = std::stod(val);
            else if (key == "HatHeight")    out.params.hat_height = std::stod(val);
            else if (key == "HatWidthOffset1") out.params.hat_width_offset_1 = std::stod(val);
            else if (key == "HatWidthOffset2") out.params.hat_width_offset_2 = std::stod(val);
            else if (key == "HorizNoise")   out.params.horiz_noise = std::stod(val);
            else if (key == "RandomSeed")
                out.params.random_seed = static_cast<uint32_t>(std::stoul(val));
            else if (key == "TextureScale") out.params.texture_scale = std::stod(val);
            // v2 texture keys.
            else if (key == "SurfaceTexture") out.params.surface_texture = val;
            else if (key == "FrontTexture")   out.params.front_texture = val;
            // v1 texture keys. boulder's `TopTexture` is the walkable surface and
            // `BottomTexture` is the camera-facing cap, which is how they map.
            else if (key == "TopTexture")    out.params.surface_texture = val;
            else if (key == "BottomTexture") out.params.front_texture = val;
            else if (key == "NodeCount")    node_count = std::stoi(val);
            else if (key == "Node") {
                std::istringstream vs(val);
                Node n;
                if (vs >> n.x >> n.y >> n.front_depth >> n.back_depth)
                    out.outline.push_back(n);
            }
            // v1 scalars, and the v1/v2 keys that are scene placement or
            // collision policy rather than generator parameters (Z, TopAngle,
            // GenerateTop, Collides, ReceivesDamage, InflictsDamage,
            // SpecialType, Enabled, FormatVersion, Generator): not part of
            // Params, so deliberately ignored here.
            else if (key == "MinDepth") { legacy_min = std::stod(val); have_legacy_depth = true; }
            else if (key == "MaxDepth") { legacy_max = std::stod(val); have_legacy_depth = true; }
        } catch (const std::exception&) {
            return false;
        }
    }

    if (out.outline.size() < 3) return false;
    if (node_count >= 0 && static_cast<int>(out.outline.size()) != node_count) return false;

    // A v1 sheet has exactly one depth for the whole ring. A v2 sheet already
    // carries per-node depths from its `Node` lines and must not be flattened.
    if (!boulderx_sheet) {
        if (!have_legacy_depth) { legacy_min = -45.0; legacy_max = 45.0; }
        if (legacy_min > legacy_max) std::swap(legacy_min, legacy_max);
        for (auto& n : out.outline) {
            n.front_depth = legacy_max;   // +z, toward the camera
            n.back_depth = legacy_min;    // -z
        }
    }
    return true;
}

bool swdm_is_boulderx(const std::string& text) { return swdm_format::is_boulderx_sheet(text); }

// ── Binary scene object ─────────────────────────────────────────────────────
std::string generate_ground_mesh_object(const std::vector<Node>& outline_in,
                                        const Params& params,
                                        const std::string& identifier,
                                        const ComponentIds* ids) {
    clear_decline_reason();
    if (outline_in.size() < 3)
        return decline("polygon has fewer than 3 nodes");
    ComponentIds cid = ids ? *ids : ComponentIds{};

    Mesh mesh;
    if (!generate(outline_in, params, mesh))
        return decline("mesh generation failed (degenerate outline, or a "
                       "triangulation that does not tile it)");

    std::vector<Node> outline = outline_in;
    ensure_ccw(outline);

    double left = outline[0].x, right = outline[0].x;
    double bottom = outline[0].y, top = outline[0].y;
    for (const auto& v : outline) {
        left = std::min(left, v.x);   right = std::max(right, v.x);
        bottom = std::min(bottom, v.y); top = std::max(top, v.y);
    }
    // GroundPolygon/CollisionShape depth. The engine writes polygon +/-92/+96
    // from its own depth scalar, and shipped data shows what that means: obj12's
    // mesh bevels out to 110 but its GroundPolygon stores +/-105 = front + W for
    // a MeshType 1 object, and fire_part2's stores +/-10 = the plain front plane.
    // Matching that keeps collision on the flat face rather than the bevel tip.
    double max_front = 0.0;
    {
        double lo = 0, hi = 0;
        for (const auto& s : mesh.fronts)
            for (const auto& v : s.vertices) {
                lo = std::min(lo, (double)v.z);
                hi = std::max(hi, (double)v.z);
            }
        max_front = std::max(std::fabs(lo), std::fabs(hi));
    }
    const double depth = max_front + (params.mesh_type == 1 ? params.hat_width_offset_1 : 0.0);

    proto::Writer poly;
    for (const auto& v : outline) poly.write_nested_field(1, make_vector2(v.x, v.y));
    poly.write_varint_field(2, 0);   // Convex
    poly.write_varint_field(3, 1);   // Closed  — every shipped ground polygon is closed

    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1);                       // Collides
    gpc.write_float_field(4, static_cast<float>(-depth)); // MinDepth
    gpc.write_float_field(5, static_cast<float>(depth));  // MaxDepth
    proto::Writer comp_poly;
    comp_poly.write_string_field(1, "GroundPolygon");
    comp_poly.write_varint_field(2, static_cast<uint64_t>(cid.polygon_id));
    comp_poly.write_nested_field(110, gpc);

    // LocalAabb: the engine outsets the polygon bounds by 0.4 * HatHeight when
    // MeshType is 1 (obj12: polygon x in [-186.595, 186.595] -> Aabb X -196.595
    // W 393.190). No outset for the plain path.
    const double outset = params.mesh_type == 1 ? 0.4 * params.hat_height : 0.0;
    const double aabb_x = left - outset;
    const double aabb_y = bottom - outset;
    const double aabb_w = (right - left) + 2 * outset;
    const double aabb_h = (top - bottom) + 2 * outset;

    proto::Writer gmc;
    gmc.write_nested_field(7, make_rectangle(aabb_x, aabb_y, aabb_w, aabb_h));
    for (const auto& sm : mesh.surfaces) gmc.write_nested_field(8, make_mesh(sm));
    for (const auto& fm : mesh.fronts) gmc.write_nested_field(9, make_mesh(fm));
    gmc.write_nested_field(10, make_float_color(1, 1, 1, 1));
    proto::Writer comp_mesh;
    comp_mesh.write_string_field(1, "GroundMesh");
    comp_mesh.write_varint_field(2, static_cast<uint64_t>(cid.mesh_id));
    comp_mesh.write_nested_field(111, gmc);

    proto::Writer ggc;
    ggc.write_varint_field(1, static_cast<uint64_t>(cid.polygon_id));
    ggc.write_varint_field(2, static_cast<uint64_t>(cid.mesh_id));
    ggc.write_varint_field(3, static_cast<uint64_t>(cid.tm_front_id));
    ggc.write_varint_field(4, static_cast<uint64_t>(cid.tm_surface_id));
    ggc.write_varint_field(5, static_cast<uint64_t>(params.random_seed));
    ggc.write_float_field(6, static_cast<float>(params.horiz_noise));  // was hardcoded 0 in boulder
    ggc.write_varint_field(7, static_cast<uint64_t>(params.mesh_type));
    ggc.write_float_field(8, static_cast<float>(params.surface_width));
    ggc.write_float_field(9, static_cast<float>(params.hat_height));
    ggc.write_float_field(10, static_cast<float>(params.hat_width_offset_1));
    ggc.write_float_field(11, static_cast<float>(params.hat_width_offset_2));
    proto::Writer comp_gen;
    comp_gen.write_string_field(1, "GroundMeshGenerator");
    comp_gen.write_varint_field(2, static_cast<uint64_t>(cid.generator_id));
    comp_gen.write_nested_field(112, ggc);

    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1);   // IsGround
    csc.write_float_field(6, static_cast<float>(-depth));  // MinDepth
    csc.write_float_field(7, static_cast<float>(depth));   // MaxDepth
    csc.write_varint_field(11, 1);  // Enabled
    proto::Writer comp_col;
    comp_col.write_string_field(1, "CollisionShape");
    comp_col.write_varint_field(2, static_cast<uint64_t>(cid.collision_id));
    comp_col.write_varint_field(4, static_cast<uint64_t>(cid.polygon_id));
    comp_col.write_nested_field(120, shape);
    comp_col.write_nested_field(121, csc);

    auto texture_component = [&](const std::string& tex, int id) {
        proto::Writer tm;
        tm.write_string_field(1, tex);
        tm.write_float_field(2, static_cast<float>(params.texture_scale));
        tm.write_nested_field(3, make_vector2(0, 0));
        proto::Writer comp;
        comp.write_string_field(1, "TextureMapping");
        comp.write_varint_field(2, static_cast<uint64_t>(id));
        comp.write_nested_field(113, tm);
        return comp;
    };
    proto::Writer comp_tm_surface = texture_component(params.surface_texture, cid.tm_surface_id);
    proto::Writer comp_tm_front = texture_component(params.front_texture, cid.tm_front_id);

    proto::Writer obj;
    obj.write_string_field(1, "SceneObject");
    obj.write_string_field(2, identifier);
    obj.write_bytes_field(3, comp_poly.to_string());
    obj.write_bytes_field(3, comp_mesh.to_string());
    obj.write_bytes_field(3, comp_gen.to_string());
    obj.write_bytes_field(3, comp_col.to_string());
    obj.write_bytes_field(3, comp_tm_surface.to_string());
    obj.write_bytes_field(3, comp_tm_front.to_string());
    obj.write_nested_field(4, make_vector2(0, 0));
    obj.write_float_field(5, static_cast<float>(mesh.max_abs_z()));
    obj.write_float_field(6, 0.0f);
    obj.write_float_field(7, 1.0f);
    obj.write_nested_field(8, make_rectangle(aabb_x, aabb_y, aabb_w, aabb_h));
    obj.write_varint_field(9, 0);

    proto::Writer scene;
    scene.write_bytes_field(1, obj.to_string());
    return scene.to_string();
}

std::string generate_ground_mesh_object_swdm(const std::string& swdm_text,
                                             const std::string& identifier,
                                             const ComponentIds* ids) {
    clear_decline_reason();
    SwdmDocument doc;
    if (!parse_swdm(swdm_text, doc))
        return decline("could not parse the .swdm sheet");
    // A v1 sheet's `Hat[` block is dome-hat geometry, which this generator does
    // not emit yet. Generating the rest of the object anyway would delete a dome
    // from the level with no error, so the sheet is refused whole.
    if (doc.has_dome_hats)
        return decline("the sheet carries dome hats (InsertCapForRoundHat), which are "
                       "not reproduced yet");
    return generate_ground_mesh_object(doc.outline, doc.params,
                                       identifier.empty() ? doc.identifier : identifier,
                                       ids);
}

// ── Generator choice ────────────────────────────────────────────────────────
// The environment variable only sets the *initial* value. A choice saved from
// the settings GUI calls set_ground_generator() afterwards and wins, which is
// what makes RUBY_GROUND_GENERATOR a debugging default rather than something
// that silently overrides the user every launch.
namespace {
GroundGenerator g_ground_generator = [] {
    const char* env = std::getenv("RUBY_GROUND_GENERATOR");
    return (env && std::strcmp(env, "boulderx") == 0) ? GroundGenerator::BoulderX
                                                      : GroundGenerator::Boulder;
}();
}

GroundGenerator ground_generator() { return g_ground_generator; }
void set_ground_generator(GroundGenerator which) { g_ground_generator = which; }

const char* ground_generator_id(GroundGenerator which) {
    return which == GroundGenerator::BoulderX ? "boulderx" : "boulder";
}

GroundGenerator ground_generator_from_id(const std::string& id) {
    return id == "boulderx" ? GroundGenerator::BoulderX : GroundGenerator::Boulder;
}

const std::string& last_decline_reason() { return g_decline_reason; }
void clear_decline_reason() { g_decline_reason.clear(); }

std::string generate_ground_mesh_object_flat(
    const std::vector<std::pair<double, double>>& polygon_xy,
    int mesh_type, double half_depth,
    const std::string& surface_texture, const std::string& front_texture,
    const std::string& identifier, bool has_dome_hats,
    const int* ids6) {
    // Dome hats are InsertCapForRoundHat geometry that boulderx does not emit
    // yet. Refusing is deliberate: emitting the rest of the object without them
    // would delete a dome from the level with no error, which is exactly the
    // silent-loss failure the mesh editors are supposed to avoid.
    clear_decline_reason();
    if (has_dome_hats)
        return decline("dome hats (InsertCapForRoundHat) are not reproduced yet");
    if (polygon_xy.size() < 3)
        return decline("polygon has fewer than 3 nodes");

    Params params;
    params.mesh_type = mesh_type;
    params.surface_texture = surface_texture;
    params.front_texture = front_texture;

    // Uniform depths: this overload cannot express anything else, which is
    // exactly why the editors use generate_ground_mesh_object_nodes().
    std::vector<Node> outline;
    outline.reserve(polygon_xy.size());
    for (const auto& p : polygon_xy)
        outline.push_back({p.first, p.second, half_depth, -half_depth});

    return generate_ground_mesh_object_nodes(outline, params, identifier, has_dome_hats, ids6);
}

std::string generate_ground_mesh_object_nodes(
    const std::vector<Node>& outline, const Params& params,
    const std::string& identifier, bool has_dome_hats, const int* ids6) {
    clear_decline_reason();
    if (has_dome_hats)
        return decline("dome hats (InsertCapForRoundHat) are not reproduced yet");
    if (outline.size() < 3)
        return decline("polygon has fewer than 3 nodes");

    // The per-node depths ride on the outline, so nothing here may rebuild it
    // from a scalar. generate_ground_mesh_object() is what applies
    // params.horiz_noise on top of them.
    ComponentIds cid;
    if (ids6) {
        cid.polygon_id = ids6[0];
        cid.mesh_id = ids6[1];
        cid.generator_id = ids6[2];
        cid.collision_id = ids6[3];
        cid.tm_surface_id = ids6[4];
        cid.tm_front_id = ids6[5];
    }
    return generate_ground_mesh_object(outline, params, identifier, &cid);
}

void randomise_node_depths(std::vector<Node>& outline, double front_max, double back_max,
                           uint32_t seed) {
    // Same LCG family the depth jitter uses elsewhere in this file, seeded
    // explicitly so a result can be reproduced by re-entering the seed.
    uint32_t s = seed ? seed : 1u;
    auto next_unit = [&s]() {
        s = s * 1664525u + 1013904223u;
        return double((s >> 8) & 0xFFFFFFu) / double(0xFFFFFFu);
    };

    if (front_max < 0.0) front_max = -front_max;
    if (back_max < 0.0) back_max = -back_max;

    // Vanilla relief is not white noise: adjacent nodes of the same sheet sit at
    // similar depths, which is what makes the surface read as rolling ground
    // rather than a saw. A random walk over the neighbours reproduces that
    // correlation; independent per-node draws would not.
    const size_t n = outline.size();
    if (n == 0) return;
    double f = next_unit(), b = next_unit();
    for (size_t i = 0; i < n; ++i) {
        // Step, then pull slightly back toward the middle of the range so the
        // walk cannot drift to one edge and stay there.
        f += (next_unit() - 0.5) * 0.6;
        b += (next_unit() - 0.5) * 0.6;
        f -= (f - 0.5) * 0.25;
        b -= (b - 0.5) * 0.25;
        f = std::clamp(f, 0.0, 1.0);
        b = std::clamp(b, 0.0, 1.0);
        outline[i].front_depth = front_max * f;
        outline[i].back_depth = -back_max * b;
    }
}

} // namespace boulderx
