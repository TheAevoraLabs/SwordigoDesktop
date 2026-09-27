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
#include <functional>
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
// The PRNG is the engine's (boulderx.h, Frand): it used to be an LCG of this
// file's own invention, which meant the *pattern* of a generated mesh's relief
// could never match a shipped one even once the depth law was right. It also
// treated seed 0 as 1 — a seed that is 0 on every object in the caves.

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
[[maybe_unused]] V2 vertex_normal(const std::vector<Node>& p, int i) {
    const int n = static_cast<int>(p.size());
    const V2 n1 = edge_normal(p[(i - 1 + n) % n], p[i]);
    const V2 n2 = edge_normal(p[i], p[(i + 1) % n]);
    return normalize2({n1.x + n2.x, n1.y + n2.y});
}

// The engine's own wall normal.
//
// GenerateSurfaceMesh builds it by summing the two edge VECTORS and rotating the
// sum, not by averaging two separately-normalized edge normals:
//     v64 = (cur.x - prev.x) + (next.x - cur.x);
//     v65 = (next.y - cur.y) + (cur.y - prev.y);   // both reduce to next-prev
//     Vector2::Normalized()
// so n = rot90(normalize(next - prev)) — a fixed 2-vertex stencil that does not
// weight the two edges equally whenever they differ in length. Measured on
// thecave_part15 obj9#10 node 1: the bake's wall normal is (0.558983, 0.829179),
// which is normalize(node2 - node0) = (-0.829, 0.559) rotated, while averaging
// the two unit normals gives (0.595, 0.797). Same at node 0, whose prev is the
// last node of the ring — which is why the seam node is not a special case here.
V2 smoothed_normal(const std::vector<Node>& p, int i) {
    const int n = static_cast<int>(p.size());
    const Node& prev = p[(i - 1 + n) % n];
    const Node& next = p[(i + 1) % n];
    return normalize2({next.y - prev.y, -(next.x - prev.x)});
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

// Slack for "is this point strictly inside the candidate ear", in squared units.
// Used from the second relaxation pass on, where boundary contact must stop
// blocking (see triangulate below).
constexpr double kEarEps = 1e-9;
inline V2 sub(const V2& p, const V2& q) { return {p.x - q.x, p.y - q.y}; }

// Caver::PointInsideTriangle(a,b,c,t) verbatim:
//   (a-c)x(t-c) >= 0 && (b-a)x(t-a) >= 0 && (c-b)x(t-b) >= 0
// "Inside" is inclusive, so a point exactly on an edge BLOCKS the ear (which is
// why the engine under-clips polygons with collinear runs).
[[maybe_unused]] bool point_inside_triangle(const V2& a, const V2& b, const V2& c, const V2& t) {
    return crosso(sub(a, c), sub(t, c)) >= 0.0 &&
           crosso(sub(b, a), sub(t, a)) >= 0.0 &&
           crosso(sub(c, b), sub(t, b)) >= 0.0;
}

// Caver::IsConvexVertex compares the POLAR ANGLE of the two edge vectors leaving
// the vertex and tests the wrapped difference — not a raw cross-product sign.
// For a CCW polygon that is equivalent to cross(prev->cur, cur->next) > 0.
[[maybe_unused]] bool is_convex_vertex(const std::vector<V2>& v, int i) {
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

// ── The engine's submesh chunking ───────────────────────────────────────────
//
// GroundMeshComponent does not hold one mesh per generated family: it holds a
// LIST, and every entry is capped at 20 faces. thecave_part15 obj9#10 ships 14
// submeshes for one generator — 7 rim, 4 ribbon, 3 cap — and the cap's 57
// triangles arrive as 20 + 20 + 17. The rule that produces them was measured off
// the ribbon, where it is unambiguous: a chunk covers 10 quads (20 faces), and
// each chunk re-emits the vertices its own quads use, in first-use order. That
// makes the ribbon's chunks 11 vertex groups each (22 vertices), with the node on
// the boundary appearing in BOTH chunks — which is exactly what the bake shows
// (chunks of 22/22/22 vertices, joined at nodes 29, 39 and 49) and why the
// shipped vertex count is higher than the geometry needs.
//
// This is not cosmetic. Rebuilding a shipped object as ONE submesh per family
// rewrites 60-95% of its GroundMesh bytes even when nothing was edited — the
// arrays shift against each other and an index-confirmed "I moved 0.001 and the
// diff is everywhere" is the result. Chunking identically is what makes a
// round-tripped object diff only where it was actually changed.
constexpr int kMaxSubmeshFaces = 20;

// Split one built family into the engine's chunks. Order is preserved, so the
// rim's chunks stay before the ribbon's and the cap's stay in triangulation
// order, which is what the bake's submesh sequence records.
std::vector<Submesh> chunk_submesh(const Submesh& sm) {
    std::vector<Submesh> out;
    if (sm.vertices.empty()) return out;
    const size_t indices_per_chunk = 3 * static_cast<size_t>(kMaxSubmeshFaces);
    const size_t verts_per_chunk = 3 * static_cast<size_t>(kMaxSubmeshFaces);

    auto start_chunk = [&]() {
        Submesh c;
        c.kind = sm.kind;
        c.texture = sm.texture;
        c.indexed = sm.indexed;
        return c;
    };

    if (!sm.indexed) {
        // FrontMesh: no index buffer, so a chunk is just a run of whole
        // triangles (the cap's 20 + 20 + 17).
        for (size_t off = 0; off < sm.vertices.size(); off += verts_per_chunk) {
            Submesh c = start_chunk();
            const size_t end = std::min(off + verts_per_chunk, sm.vertices.size());
            c.vertices.assign(sm.vertices.begin() + static_cast<long>(off),
                              sm.vertices.begin() + static_cast<long>(end));
            out.push_back(std::move(c));
        }
        return out;
    }

    for (size_t off = 0; off < sm.indices.size();) {
        Submesh c = start_chunk();
        const size_t end = std::min(off + indices_per_chunk, sm.indices.size());
        // The chunk gets its own copy of the vertices it uses, in ASCENDING
        // source-index order — not first-use order. The difference matters: the
        // ribbon's two vertices per node are [back, front] and first-use order
        // would emit [back0, front0, front1, back1, ...], destroying that
        // pairing and interleaving the two planes. Ascending order keeps each
        // node's pair together, which is what the shipped chunks hold (the
        // ribbon's are 24/22/22/22 vertices for 20 faces each — whole pairs).
        // A vertex shared with the neighbouring chunk is a fresh copy here, not
        // a reference; that is how the shipped chunks overlap at their boundary
        // nodes.
        std::vector<char> used(sm.vertices.size(), 0);
        for (size_t k = off; k < end; ++k) {
            const uint16_t v = sm.indices[k];
            if (v < sm.vertices.size()) used[v] = 1;
        }
        std::vector<int> remap(sm.vertices.size(), -1);
        for (size_t v = 0; v < sm.vertices.size(); ++v) {
            if (!used[v]) continue;
            remap[v] = static_cast<int>(c.vertices.size());
            c.vertices.push_back(sm.vertices[v]);
        }
        for (size_t k = off; k < end; ++k) {
            const uint16_t v = sm.indices[k];
            if (v < sm.vertices.size()) c.indices.push_back(static_cast<uint16_t>(remap[v]));
        }
        out.push_back(std::move(c));
        off = end;
    }
    return out;
}

// One vertex of a swept profile: position, a normal that is either an XY
// direction (the rings, whose outer/inner walls face sideways) or a pure Z
// direction (the bevel bulges and the dome caps), and the world-position UV the
// engine's TextureMapping::TexCoordForPosition produces.
Vertex make_profile_vertex(double x, double y, double z, V2 n, double scale, double nz = 0.0) {
    Vertex v;
    v.x = static_cast<float>(x);
    v.y = static_cast<float>(y);
    v.z = static_cast<float>(z);
    v.nx = static_cast<float>(n.x);
    v.ny = static_cast<float>(n.y);
    v.nz = static_cast<float>(nz);
    v.u = static_cast<float>(0.5 + x / scale);
    v.v = static_cast<float>(0.5 + y / scale);
    return v;
}

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
//
// `skip[i]` is empty for MeshType 0, whose one strip is GenerateSurfaceMesh and
// covers the whole outline; for MeshType 1 it marks the nodes the rim owns, and
// the ribbon steps over them. That is the sibling function's name:
// GeneratePlainSurfaceWithHatGaps generates the plain surface *with gaps where
// the hat is*, and the bake shows the gaps plainly — thecave_crypt2 obj5's
// nodes 18..22 carry the rim's +-(SurfaceWidth/2 + HatWidthOffset1) pair and the
// cap's +SurfaceWidth/2 and **no ribbon vertex at all**, while node 17, the last
// node before that rim run, carries all three. Without the gaps the ribbon walls
// the same stretch the rim already walls, one plane inside it — the "every side
// is wrapped" look, and a wall the vanilla mesh does not have.
// Defined below, next to the surface families: the wall needs its own u phase.
std::vector<double> ribbon_u(const std::vector<Node>& p, const std::vector<char>& emitted,
                             double scale);

Submesh build_ribbon(const std::vector<Node>& outline, const std::vector<double>& back,
                     const std::vector<double>& front, const std::string& texture,
                     const Params& params, bool close_loop,
                     const std::vector<char>& skip = {}) {
    Submesh sm;
    sm.kind = "SurfaceMesh";
    sm.texture = texture;
    sm.indexed = true;

    const size_t n = outline.size();
    const double scale = params.texture_scale > 0 ? params.texture_scale : 250.0;
    std::vector<char> emitted(n, 1);
    for (size_t i = 0; i < n; ++i)
        if (!skip.empty() && skip[i]) emitted[i] = 0;
    const std::vector<double> u = ribbon_u(outline, emitted, scale);
    std::vector<int> at(n, -1);
    for (size_t i = 0; i < n; ++i) {
        if (!emitted[i]) continue;
        const V2 nrm = smoothed_normal(outline, static_cast<int>(i));
        const double uu = u[i];
        at[i] = static_cast<int>(sm.vertices.size());
        Vertex b;
        b.x = static_cast<float>(outline[i].x);
        b.y = static_cast<float>(outline[i].y);
        b.z = static_cast<float>(back[i]);
        b.nx = static_cast<float>(nrm.x);
        b.ny = static_cast<float>(nrm.y);
        b.u = static_cast<float>(uu);
        b.v = static_cast<float>(tex_v(back[i], scale));
        push_vertex(sm, b);
        Vertex f = b;
        f.z = static_cast<float>(front[i]);
        f.v = static_cast<float>(tex_v(front[i], scale));
        push_vertex(sm, f);
    }

    const size_t segments = close_loop ? n : n - 1;
    for (size_t i = 0; i < segments; ++i) {
        const size_t j = (i + 1) % n;
        if (at[i] < 0 || at[j] < 0) continue;      // a gap: no quad across it
        const uint16_t a = static_cast<uint16_t>(at[i]);         // back, point i
        const uint16_t b = static_cast<uint16_t>(at[i] + 1);     // front, point i
        const uint16_t c = static_cast<uint16_t>(at[j]);         // back, point j
        const uint16_t d = static_cast<uint16_t>(at[j] + 1);     // front, point j
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
// A dome hat, in the shape boulder gives it (src/tools/boulder.cpp,
// generate_hat_mesh) so the two generators agree on a sheet's geometry.
//
// The shape is drawn the way the engine draws its own sweeps: a profile is
// described once and then swept, and the sweep is capped at both ends. The two
// halves map onto the engine symbols the way its own two calls divide the same
// work — InsertRoundHatVertices emits the profile ring (one pair of vertices per
// cross-section sample, "back plane" then "front plane", sharing the sample's
// XY normal) and InsertCapForRoundHat closes it (the two centre vertices and
// their fans, which is exactly what that function adds after calling the other).
//
// The measurement that matters: the dome must sit ON the surface, so its base is
// the polygon's own top edge. boulder lifts it 0.05 so the dome and the ground
// do not z-fight where they meet; that lift is kept verbatim.
//
// It spans the sheet's whole depth (min..max over the outline), not a fixed
// thickness, so a dome on a thick ledge is as deep as the ledge — the same rule
// doc 09 §4 records for the engine's own sweeps.
Submesh build_hat(const Hat& hat, double base_y, double z_back, double z_front,
                  const Params& params) {
    Submesh sm;
    sm.kind = "SurfaceMesh";
    sm.texture = params.surface_texture;   // a dome is walkable surface
    sm.indexed = true;

    const int N = 18;                     // cross-section segments, as boulder sweeps
    const double scale = params.texture_scale > 0 ? params.texture_scale : 250.0;
    const double r = std::max(1.0, hat.radius);
    const double h = std::max(1.0, hat.height);
    const double base = base_y + 0.05;    // anti z-fighting lift, from boulder
    double z0 = z_back, z1 = z_front;
    if (z0 > z1) std::swap(z0, z1);       // editor-dragged or noisy depths

    // The profile: a half-ellipse from one footprint edge, over the apex, to the
    // other, with outward normals. Each sample contributes its back-plane and
    // front-plane vertex, exactly as the engine's sweep emits a vertex pair per
    // profile point.
    std::vector<uint16_t> ring0(N + 1), ring1(N + 1);
    for (int t = 0; t <= N; ++t) {
        const double phi = kPi * static_cast<double>(t) / static_cast<double>(N);
        const double px = hat.x + r * std::cos(phi);
        const double py = base + h * std::sin(phi);
        const V2 nrm = normalize2({std::cos(phi), std::sin(phi)});
        ring0[t] = static_cast<uint16_t>(sm.vertices.size());
        sm.vertices.push_back(make_profile_vertex(px, py, z0, nrm, scale));
        ring1[t] = static_cast<uint16_t>(sm.vertices.size());
        sm.vertices.push_back(make_profile_vertex(px, py, z1, nrm, scale));
    }

    // The ridge between the two planes. Winding matches the ribbon's so the
    // outward face of the dome stays outward.
    for (int t = 0; t < N; ++t) {
        const uint16_t a = ring0[t], b = ring0[t + 1];
        const uint16_t c = ring1[t + 1], d = ring1[t];
        sm.indices.push_back(a); sm.indices.push_back(b); sm.indices.push_back(c);
        sm.indices.push_back(a); sm.indices.push_back(c); sm.indices.push_back(d);
    }

    // The caps: a fan from the footprint centre closes the dome at each plane.
    const uint16_t cb = static_cast<uint16_t>(sm.vertices.size());
    sm.vertices.push_back(make_profile_vertex(hat.x, base, z0, {0.0, 0.0}, scale, -1.0));
    for (int t = 0; t < N; ++t) {
        sm.indices.push_back(cb); sm.indices.push_back(ring0[t + 1]);
        sm.indices.push_back(ring0[t]);
    }
    const uint16_t cf = static_cast<uint16_t>(sm.vertices.size());
    sm.vertices.push_back(make_profile_vertex(hat.x, base, z1, {0.0, 0.0}, scale, 1.0));
    for (int t = 0; t < N; ++t) {
        sm.indices.push_back(cf); sm.indices.push_back(ring1[t]);
        sm.indices.push_back(ring1[t + 1]);
    }
    return sm;
}

// The engine's own "does this node carry a rim cross-section" test.
//
// GenerateSurfaceMeshWithRoundHat (0x3A7160 arm64 / 0x2D3BDC arm32) rejects a
// node unless the *outgoing* edge's outward normal is within 45 degrees of +Y;
// the rejected nodes are what the sibling GeneratePlainSurfaceWithHatGaps walks
// instead (its own name says so: the plain ribbon is generated *with hat gaps*).
// The polarity is pinned on the shipped meshes rather than on the decompile,
// whose sign convention is easy to read backwards: `rim iff out_normal.y >=
// 1/sqrt(2)` reproduces every node's rim/no-rim decision exactly on
// florennum_cave1 obj9#14/#16/#17, forest_cave0 obj9#16/#21/#27, forest_cave1
// obj5 and half a dozen more, and 96% of 378 nodes over 12 cave objects
// (.scratch/rim_rule.py is that measurement).
//
// This is the rule that decides whether a floor gets a rounded brim on a given
// side, and it is the reason a generator that puts the cross-section on *every*
// node wraps the whole outline — both sides, the back and the front — in
// surface-mapped geometry the shipped engine only draws on the +Y-facing runs.
// ── The two surface families ────────────────────────────────────────────────
//
// GroundMeshGeneratorComponent::GenerateMesh calls the two surface variants
// with the SAME threshold, the literal 0.70711 = cos(45 deg), and each takes
// half the ring:
//
//   * InsertRoundHatVertices (via GenerateSurfaceMeshWithRoundHat) is the rim;
//   * GeneratePlainSurfaceWithHatGaps is the plain wall, *with gaps where the
//     hat is*.
//
// Which nodes belong to which was measured on thecave_part15 obj9#10 (59 nodes,
// the shipped 14 submeshes): the rim owns nodes 0..20 and the plain wall owns
// 0, 16, 17, 20 and 21..58 — i.e. a node takes the rim if *either* of its edge
// normals is at or past the threshold, and takes the plain wall unless *both*
// are. The three nodes with exactly one qualifying edge (0, 16, 20) therefore
// get both, which is what the engine's state machine does with its run
// boundaries, and the two nodes inside the rim run (18, 19) get no wall at all.
// Getting this wrong is visible twice over: nodes that get both when they should
// get one wall the same stretch twice, five units apart, and nodes that get a
// wall inside a rim run are the "every side is wrapped" look.
constexpr double kRimNormalCos = 0.70711;   // 1/sqrt(2), the engine's literal

V2 incoming_normal(const std::vector<Node>& p, size_t i) {
    const size_t n = p.size();
    return edge_normal(p[(i + n - 1) % n], p[i]);
}

// Outward normal of the edge leaving node i, for a CCW ring (the same
// construction as edge_normal, on one edge).
V2 outgoing_normal(const std::vector<Node>& p, size_t i) {
    const size_t n = p.size();
    return edge_normal(p[i], p[(i + 1) % n]);
}

inline bool faces_rim(const V2& n) { return n.y >= kRimNormalCos; }

bool rim_node(const std::vector<Node>& p, size_t i) {
    if (p.size() < 3) return false;
    return faces_rim(incoming_normal(p, i)) || faces_rim(outgoing_normal(p, i));
}

bool plain_node(const std::vector<Node>& p, size_t i) {
    if (p.size() < 3) return true;
    return !(faces_rim(incoming_normal(p, i)) && faces_rim(outgoing_normal(p, i)));
}

// Per-node u, the texture's along-the-ring phase.
//
// The engine feeds TextureMapping::TexCoordForPosition the *synthesised*
// position (arcLength, depth, 0) — not the vertex — so u = 0.5 + arc/Scale and
// v = 0.5 + z/Scale, and the arc only advances on a step it emits and only
// across a run of emitted nodes: a step whose ring-predecessor was skipped adds
// nothing, and a run restarts the arc. Measured: on obj9#10 the wall's u at
// node 17 is 0.5 + |16->17|/250 and at node 20 it is *the same* as at node 17
// (the 18/19 span contributes nothing), while node 0 — the far end of the run
// that starts at node 20 — carries 0.5 + (the ring length 20..0)/250.
//
// `emitted[i]` selects the family; nodes that are not emitted still advance
// nothing, which is what keeps the texture phase continuous across the gaps.
std::vector<double> ribbon_u(const std::vector<Node>& p, const std::vector<char>& emitted,
                             double scale) {
    const size_t n = p.size();
    std::vector<double> u(n, 0.5);
    if (n == 0 || scale <= 0.0) return u;

    int start = -1;
    for (size_t i = 0; i < n; ++i) {
        if (!emitted[i]) continue;
        if (!emitted[(i + n - 1) % n]) { start = static_cast<int>(i); break; }
    }
    if (start < 0) {
        // No run boundary anywhere: either nothing is emitted or the whole ring is.
        for (size_t i = 0; i < n; ++i)
            if (emitted[i]) { start = static_cast<int>(i); break; }
    }
    if (start < 0) return u;

    double arc = 0.0;
    int i = start;
    for (size_t guard = 0; guard <= n; ++guard) {
        u[static_cast<size_t>(i)] = 0.5 + arc / scale;
        const int next = (i + 1) % static_cast<int>(n);
        if (!emitted[static_cast<size_t>(next)]) break;
        const Node& a = p[static_cast<size_t>(i)];
        const Node& b = p[static_cast<size_t>(next)];
        arc += std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        if (next == start) break;
        i = next;
    }
    return u;
}

// Caver::Polygon::VertexAtIndexIsConvexCCW — the engine's own convexity test,
// and the gate on the rim's 3-unit corner shift.
bool convex_ccw(const std::vector<Node>& p, size_t i) {
    const size_t n = p.size();
    const V2 a = normalize2({p[i].x - p[(i + n - 1) % n].x, p[i].y - p[(i + n - 1) % n].y});
    const V2 b = normalize2({p[(i + 1) % n].x - p[i].x, p[(i + 1) % n].y - p[i].y});
    const double turn = a.x * b.y - a.y * b.x;
    if (std::fabs(turn) < 1e-12) {
        // A collinear vertex has no turn; the engine's polar-angle test wraps the
        // difference to 0 there as well, so it reads as "not convex" and the node
        // keeps the bare outline position. Reproduced rather than averaged away,
        // because a shift moves a rim ring 3 units off the node's own XY and the
        // shipped ring at florennum_cave1 obj9#18 node 4 is *not* shifted there.
        return false;
    }
    return turn > 0.0;
}

// GroundMeshGenerator::InsertRoundHatVertices — the rounded rim.
//
// The profile is FOUR vertices per node, not six:
//
//   A = (x,        y,        back  - W1)        normal = +n
//   B = (x,        y,        front + W1)        normal = +n
//   C = (x,        y - 0.4H, front + W1 + W2)   normal = +bevel
//   D = (x,        y - H,    front + W1 + 0.1)  normal = -n
//
// where n is the wall normal (smoothed_normal), H is HatHeight, W1/W2 the two
// width offsets, and the "inward" steps are a *pure -Y* translation — the
// decompile computes `pos.y + HatHeight * -0.4` with no normal component, and
// the bake agrees to the last digit (thecave_part15 obj9#10 node 0, H 20:
// the bevel at -8.000 and the lip at -20.000 in Y with X unchanged).
//
// Only the three *front* bands are swept between consecutive rim nodes, with the
// engine's own six triangles:
//   (A0,B0,B1) (A1,A0,B1) (A1,B1,C1) (C0,A1,C1) (C0,C1,D1) (D0,C0,D1)
// read off the shipped IndexData of obj9#10's first submesh. The back half of
// the profile (the far bevel and lip) belongs to the run's END CAP, not to the
// sweep: InsertCapForRoundHat inserts the two extra vertices and four faces that
// close a rim run's end, and that is why the bake holds six-vertex cross-sections
// only at the nodes where a run starts and stops.
//
// Sweeping all six profile edges at every node — which this file used to do — is
// the "top layer wraps every side" geometry: three of the six bands sit five
// units from the plain wall's planes and z-fight with it along the whole run.
Submesh build_rim(const std::vector<Node>& outline, const std::vector<double>& back,
                  const std::vector<double>& front, const Params& params) {
    Submesh sm;
    sm.kind = "SurfaceMesh";
    sm.texture = params.surface_texture;
    sm.indexed = true;

    const size_t n = outline.size();
    const double H = params.hat_height;
    const double W1 = params.hat_width_offset_1;
    const double W2 = params.hat_width_offset_2;
    const double scale = params.texture_scale > 0 ? params.texture_scale : 250.0;

    std::vector<char> emitted(n, 0);
    std::vector<char> cap_node(n, 0);
    for (size_t i = 0; i < n; ++i) {
        emitted[i] = rim_node(outline, i) ? 1 : 0;
        // GenerateMesh's two thresholds leave each ring node in exactly one of
        // three classes, and the bake shows what each one gets:
        //   both edge normals past cos45  -> rim ring only (nodes 1..15, 18, 19)
        //   exactly one past              -> rim ring AND a wall AND a cap
        //                                    (nodes 0, 16, 17, 20)
        //   neither                       -> wall only (nodes 21..58)
        // The "exactly one" nodes are the engine's state-machine transitions, and
        // they are where InsertCapForRoundHat closes a run. Derived, not guessed:
        // obj9#10's rim strip carries six-vertex cross-sections at exactly
        // 0/16/17/20 and four-vertex ones everywhere else it is present.
        cap_node[i] = (plain_node(outline, i) && rim_node(outline, i)) ? 1 : 0;
    }

    // Two adjacent transition nodes are a flange, not a band: the bake's rim strip
    // is 130 triangles, which is 19 bands (114) plus four end caps (16), and the
    // missing band is the 16 -> 17 step — the two runs butt together there.
    auto band = [&](size_t i, size_t j) { return !(cap_node[i] && cap_node[j]); };

    // The rim's u walks the ring once: it advances only across a step that has a
    // band, so a flange leaves it where it was. Measured: obj9#10's rim u at node
    // 16 is 19.2742 = 0.5 + (0..16)/250, at node 17 it is the *same* 19.2742
    // across the 770-unit flange, and it resumes with 17 -> 18.
    std::vector<double> u(n, 0.5);
    {
        long start = -1;
        for (size_t i = 0; i < n; ++i)
            if (emitted[i] && !emitted[(i + n - 1) % n]) { start = static_cast<long>(i); break; }
        if (start < 0)
            for (size_t i = 0; i < n; ++i)
                if (emitted[i]) { start = static_cast<long>(i); break; }
        if (start >= 0) {
            double arc = 0.0;
            size_t i = static_cast<size_t>(start);
            for (size_t guard = 0; guard <= n; ++guard) {
                u[i] = 0.5 + arc / scale;
                const size_t nx = (i + 1) % n;
                if (!emitted[nx]) break;
                if (band(i, nx)) {
                    const Node& a = outline[i];
                    const Node& b = outline[nx];
                    arc += std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                }
                if (nx == static_cast<size_t>(start)) break;
                i = nx;
            }
        }
    }

    // The bevel's normal is the engine's own: it rotates the (rate of change of
    // depth, x) vector, so the chamfer faces along the slope of the terrain.
    // InsertRoundHatVertices: v62 = (this[1][next] - this[1][cur], 0, next.x - cur.x),
    // normalized. Measured at node 0: (0.164, 0, 0.986) for a 162.5 depth step
    // over a 976.7 x step.
    auto bevel_normal = [&](size_t i) {
        const size_t j = (i + 1) % n;
        const double dz = front[j] - front[i];
        const double dx = outline[j].x - outline[i].x;
        const double len = std::sqrt(dz * dz + dx * dx);
        if (len < 1e-12) return V3{0.0, 0.0, 1.0};
        return V3{dz / len, 0.0, dx / len};
    };

    // The 3-unit corner shift, which the engine applies ONLY inside
    // InsertCapForRoundHat — so a ring is shifted at a run's end, never along it,
    // and only when Caver::Polygon::VertexAtIndexIsConvexCCW says the node turns
    // CCW. The direction is *away from the predecessor*, and both halves are
    // measured on obj9#10's four cap nodes:
    //   node 0  ring at node + ( 2.577, -1.536) = 3*normalize(node0 - node58)
    //   node 16 ring at node + (-2.594,  1.507) = 3*normalize(node16 - node15)
    //   node 20 ring at node + (-2.946, -0.566) = 3*normalize(node20 - node19)
    //   node 17 ring at node + ( 0.000,  0.000) — the one reflex corner, so no
    //                                             shift at all
    // Using the successor instead (which this file did) moves node 16's ring by
    // (0.04, 3.00) instead of (-2.59, 1.51), i.e. the wrong way by 3 units; and
    // shifting every ring rather than only the capped ones offsets 23 of the
    // object's 59 cross-sections by up to 3 units.
    auto shifted = [&](size_t i, double& bx, double& by) {
        bx = outline[i].x;
        by = outline[i].y;
        if (!cap_node[i]) return;
        if (!convex_ccw(outline, i)) return;
        const size_t j = (i + n - 1) % n;
        const V2 away = normalize2({outline[i].x - outline[j].x, outline[i].y - outline[j].y});
        bx += 3.0 * away.x;
        by += 3.0 * away.y;
    };

    // The 4-vertex cross-section at a rim node. `at` is the index its ring was
    // emitted at (-1 when the node is not part of the rim).
    auto emit_ring = [&](size_t i) {
        const V2 nrm = smoothed_normal(outline, static_cast<int>(i));
        const V3 bev = bevel_normal(i);
        double bx = 0.0, by = 0.0;
        shifted(i, bx, by);
        struct Prof { double dy, z, nx, ny, nz; };
        const Prof prof[4] = {
            {0.0,      back[i] - W1,        nrm.x,  nrm.y,  0.0},
            {0.0,      front[i] + W1,       nrm.x,  nrm.y,  0.0},
            {-0.4 * H, front[i] + W1 + W2,  bev.x,  bev.y,  bev.z},
            {-H,       front[i] + W1 + 0.1, -nrm.x, -nrm.y, 0.0},
        };
        const int at = static_cast<int>(sm.vertices.size());
        for (const Prof& p : prof) {
            Vertex v;
            v.x = static_cast<float>(bx);
            v.y = static_cast<float>(by + p.dy);
            v.z = static_cast<float>(p.z);
            v.nx = static_cast<float>(p.nx);
            v.ny = static_cast<float>(p.ny);
            v.nz = static_cast<float>(p.nz);
            v.u = static_cast<float>(u[i]);
            v.v = static_cast<float>(tex_v(p.z, scale));
            push_vertex(sm, v);
        }
        return at;
    };

    // InsertCapForRoundHat: close a rim run's end with the far bevel and lip —
    // the two vertices the sweep leaves out — and four faces over the hexagon
    // A,B,C,D,D',C'. `reversed` is the engine's `a2` flag: the run's start and
    // its end close in opposite windings so both read as outward faces.
    auto cap_run_end = [&](size_t i, int ring, double run_u, bool reversed) {
        const V2 nrm = smoothed_normal(outline, static_cast<int>(i));
        const V3 bev = bevel_normal(i);
        double bx = 0.0, by = 0.0;
        shifted(i, bx, by);
        const double zc2 = back[i] - W1 - W2;    // far bevel
        const double zc1 = back[i] - W1 + 0.1;   // far lip
        Vertex far_bevel;
        far_bevel.x = static_cast<float>(bx);
        far_bevel.y = static_cast<float>(by - 0.4 * H);
        far_bevel.z = static_cast<float>(zc2);
        far_bevel.nx = static_cast<float>(bev.x);
        far_bevel.ny = static_cast<float>(bev.y);
        far_bevel.nz = static_cast<float>(bev.z);
        far_bevel.u = static_cast<float>(run_u);
        far_bevel.v = static_cast<float>(tex_v(zc2, scale));
        Vertex far_lip = far_bevel;
        far_lip.y = static_cast<float>(by - H);
        far_lip.z = static_cast<float>(zc1);
        far_lip.nx = static_cast<float>(-nrm.x);
        far_lip.ny = static_cast<float>(-nrm.y);
        far_lip.nz = 0.0f;
        far_lip.v = static_cast<float>(tex_v(zc1, scale));
        const uint16_t fb = static_cast<uint16_t>(sm.vertices.size());
        push_vertex(sm, far_lip);
        push_vertex(sm, far_bevel);
        const uint16_t A = static_cast<uint16_t>(ring + 0);
        const uint16_t B = static_cast<uint16_t>(ring + 1);
        const uint16_t C = static_cast<uint16_t>(ring + 2);
        const uint16_t D = static_cast<uint16_t>(ring + 3);
        const uint16_t L = fb;        // far lip
        const uint16_t V = fb + 1;    // far bevel
        auto tri = [&](uint16_t a, uint16_t b, uint16_t c) {
            if (reversed) { sm.indices.push_back(a); sm.indices.push_back(c); sm.indices.push_back(b); }
            else          { sm.indices.push_back(a); sm.indices.push_back(b); sm.indices.push_back(c); }
        };
        tri(A, D, L);
        tri(A, B, D);
        tri(B, C, D);
        tri(A, L, V);
    };

    // Ring per rim node, then the bands between ring-ADJACENT rim nodes, then a
    // cap at each end of each maximal run.
    std::vector<int> ring_of(n, -1);
    for (size_t i = 0; i < n; ++i)
        if (emitted[i]) ring_of[i] = emit_ring(i);

    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        if (ring_of[i] < 0 || ring_of[j] < 0) continue;
        if (!band(i, j)) continue;                 // a flange closes, it does not sweep
        const int r0 = ring_of[i], r1 = ring_of[j];
        const uint16_t a0 = static_cast<uint16_t>(r0 + 0), b0 = static_cast<uint16_t>(r0 + 1);
        const uint16_t c0 = static_cast<uint16_t>(r0 + 2), d0 = static_cast<uint16_t>(r0 + 3);
        const uint16_t a1 = static_cast<uint16_t>(r1 + 0), b1 = static_cast<uint16_t>(r1 + 1);
        const uint16_t c1 = static_cast<uint16_t>(r1 + 2), d1 = static_cast<uint16_t>(r1 + 3);
        // GenerateSurfaceMeshWithRoundHat's six AddIndex calls, in order.
        sm.indices.push_back(a0); sm.indices.push_back(b0); sm.indices.push_back(b1);
        sm.indices.push_back(a1); sm.indices.push_back(a0); sm.indices.push_back(b1);
        sm.indices.push_back(a1); sm.indices.push_back(b1); sm.indices.push_back(c1);
        sm.indices.push_back(c0); sm.indices.push_back(a1); sm.indices.push_back(c1);
        sm.indices.push_back(c0); sm.indices.push_back(c1); sm.indices.push_back(d1);
        sm.indices.push_back(d0); sm.indices.push_back(c0); sm.indices.push_back(d1);
    }

    // One cap per transition node, oriented along the ring: a "rise" (the
    // incoming normal short of the threshold, the outgoing past it) starts a run
    // and a "fall" ends one. The two windings are the engine's `a2` flag.
    for (size_t i = 0; i < n; ++i) {
        if (!cap_node[i] || ring_of[i] < 0) continue;
        const bool rising = !faces_rim(incoming_normal(outline, i)) &&
                             faces_rim(outgoing_normal(outline, i));
        cap_run_end(i, ring_of[i], u[i], /*reversed=*/!rising);
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

bool recover_node_depths(const std::vector<Node>& outline,
                         const std::vector<float>& positions,
                         double tolerance,
                         std::vector<double>& front_out,
                         std::vector<double>& back_out) {
    front_out.clear();
    back_out.clear();
    if (outline.size() < 3 || positions.size() < 3) return false;
    const double tol = tolerance > 0.0 ? tolerance : 0.05;

    // The PLAIN plane per node: the shallowest non-negative z and the deepest
    // non-positive one on the node's own XY.
    //
    // Taking the extremes instead used to be this function's whole bug. A
    // MeshType 1 rim rings its node with vertices at `plain +- HatWidthOffset1`
    // (and +- W2 at the bevel), so the extremes are the RIM's planes, and
    // build_rim then adds HatWidthOffset1 to whatever it is handed — putting the
    // rim a full W1 further out than the engine does. On thecave_crypt2 obj5
    // (SurfaceWidth 180, W1 5) that is the difference between a mesh whose
    // envelope is +-100 and one at +-105.
    //
    // "Closest to z = 0" is the same rule tools/ground_mesh_truth.py's
    // ribbon_depths() measures, and it is exact wherever the ribbon bakes a
    // vertex. It is NOT exact at a rim node: the ribbon yields that node to the
    // rim, so no plain-plane BACK vertex exists there and the deepest
    // non-positive z is still W1 too far out. Nothing in the bake can recover
    // that — which is why engine_node_depths() is the primary route and this is
    // the fallback for a ground object whose generator is gone.
    const size_t n = outline.size();
    std::vector<double> front(n, 0.0), back(n, 0.0);
    // Two accumulators per node, because "closest to zero" is a min over the
    // positives and a max over the negatives — not a min and a max over all z.
    std::vector<double> pos(n, 1e30), neg(n, -1e30);
    std::vector<bool> matched(n, false);
    for (size_t v = 0; v + 2 < positions.size(); v += 3) {
        const double x = positions[v], y = positions[v + 1], z = positions[v + 2];
        for (size_t k = 0; k < n; ++k) {
            if (std::fabs(outline[k].x - x) > tol || std::fabs(outline[k].y - y) > tol)
                continue;
            matched[k] = true;
            if (z > 0.0) pos[k] = std::min(pos[k], z);
            else if (z < 0.0) neg[k] = std::max(neg[k], z);
            else pos[k] = neg[k] = 0.0;
        }
    }

    int found = 0;
    for (size_t k = 0; k < n; ++k) {
        // A column with only positives (or only negatives) still yields its
        // outer plane; a node with neither is unmatched.
        if (pos[k] < 1e29) { front[k] = pos[k]; ++found; }
        if (neg[k] > -1e29) { back[k] = neg[k]; ++found; }
    }
    if (found < 3) return false;

    // A node with no match falls back to the sheet's own range, so a partially
    // recoverable mesh keeps its thickness instead of gaining a zero-depth spike.
    //
    // Reached by 3 of the 2,538 ground objects in the 202 decoded scenes: the
    // engine does bake one vertex per outline node at that node's XY, so matching
    // recovers the per-node depth array for the other 2,535.
    // (.scratch/ground_import_probe.py is that measurement.)
    double lo = 0.0, hi = 0.0;
    for (size_t k = 0; k < n; ++k) {
        if (!matched[k]) continue;
        hi = std::max(hi, front[k]);
        lo = std::min(lo, back[k]);
    }
    front_out.resize(n);
    back_out.resize(n);
    for (size_t k = 0; k < n; ++k) {
        front_out[k] = pos[k] < 1e29 ? pos[k] : (neg[k] > -1e29 ? -neg[k] : hi);
        back_out[k] = neg[k] > -1e29 ? neg[k] : (pos[k] < 1e29 ? -pos[k] : lo);
    }
    return true;
}

// Ear clipping, in the engine's own shape but without its stall.
//
// Caver::TriangulatePolygon is not recoverable from the bake, and the port no
// longer pretends it is. Measured on thecave_part15 obj9#10 (59 nodes, the object
// the vault-style "everything crumbles into one place" was reported on):
//
//  * the bake's cap is a complete, non-overlapping triangulation of the outline
//    (its 57 triangles cover exactly the polygon's area, and the polygon has no
//    self-intersections), so the engine does produce a real triangulation;
//  * but it is not an ear clip of any reading tested. 236 parameterisations of
//    the walk (start vertex, direction, strict/inclusive inside-test, and where
//    `current` moves after a clip and after a rejection) share at most 18 of the
//    57 triangles with the bake; minimum-weight, greedy-shortest-diagonal,
//    Delaunay (50 of 57 violate the empty-circumcircle property) and a tiled fan
//    all miss too;
//  * what the bake does show is a *concentration* bound: its busiest node carries
//    8 of 57 cap triangles. The recovered-walk port carried 29 of 57 on node 10 —
//    one apex spanning the whole band, which is the starburst the editor showed —
//    because resuming the walk at the *previous* vertex lets one apex absorb
//    every clip.
//
// So the port cuts at the SHORTEST VALID DIAGONAL instead, recursively. That is
// start-independent, produces local triangles by construction (no apex can span
// the field), and comes closest to the bake of every rule tried: busiest node 9 of
// 57 against the bake's 8, and surface deviation mean 33.9 / max 165.4 over a
// 40x40 interior grid (the recovered walk scored 43.8 / 176.6 on the same grid).
// The residual is documented in doc 13: a faithful *look*, not byte-identical
// triangles.

// Is `t` inside the closed sub-ring `pts` (ray casting)? A diagonal's midpoint
// being outside the ring is what catches the case that breaks a plain
// crossing-only test: a segment that crosses nothing but runs outside a concave
// throat, which silently overlaps another triangle and inflates the cap's area.
bool point_in_ring(const std::vector<V2>& pts, const V2& t) {
    const size_t m = pts.size();
    bool in = false;
    for (size_t i = 0, j = m - 1; i < m; j = i++) {
        const V2 a = pts[i];
        const V2 b = pts[j];
        if ((a.y > t.y) != (b.y > t.y) &&
            t.x < (b.x - a.x) * (t.y - a.y) / (b.y - a.y) + a.x) {
            in = !in;
        }
    }
    return in;
}

// Do segments (p1,p2) and (p3,p4) properly cross, or touch anywhere except at a
// shared endpoint? A diagonal that only touches an edge is unusable — it would
// produce overlapping cap triangles — so both cases are rejected.
bool segments_conflict(const V2& p1, const V2& p2, const V2& p3, const V2& p4) {
    const double o1 = crosso(sub(p2, p1), sub(p3, p1));
    const double o2 = crosso(sub(p2, p1), sub(p4, p1));
    const double o3 = crosso(sub(p4, p3), sub(p1, p3));
    const double o4 = crosso(sub(p4, p3), sub(p2, p3));
    const double eps = 1e-9;
    if (((o1 > eps) != (o2 > eps)) && ((o3 > eps) != (o4 > eps))) return true;
    // Collinear/touching cases: a candidate endpoint lying on the other segment.
    auto on = [&](const V2& a, const V2& b, const V2& t) {
        if (std::fabs(crosso(sub(b, a), sub(t, a))) > eps) return false;
        return t.x >= std::min(a.x, b.x) - eps && t.x <= std::max(a.x, b.x) + eps &&
               t.y >= std::min(a.y, b.y) - eps && t.y <= std::max(a.y, b.y) + eps;
    };
    return on(p1, p2, p3) || on(p1, p2, p4) || on(p3, p4, p1) || on(p3, p4, p2);
}

// ── The cap's triangulation: sharpest corner first ──────────────────────────
//
// The cap's vertices sit at each node's OWN front depth, so the cap outline is a
// tent, not a plane: which diagonals get cut fully determines the 3D surface.
// That is why a fan from a single node reads as "all the curvatures and the Z
// differences crumpled into one point" — with one apex, every triangle spans the
// whole outline and the tent's creases radiate from one spot.
//
// Measured against the shipped bake (tools/ground_mesh_truth.py dump, checked by
// tests/ground_mesh_exact_test) on thecave_part15 obj9#10 — 59 nodes, 57 cap
// triangles — the rule below reproduces 19 of those 57 triangles. Everything
// else tried was worse: scan-from-head ear clipping 5 (best of 236 start/restart
// combinations), largest area 8, minimum weight (DP) 2, Delaunay and
// greedy-shortest-diagonal 0, and the shortest-valid-diagonal split that was here
// before 0 to 3 — which is the fan in the bake diff.
//
// Why this rule: a reflex vertex is never an ear, so the vertices that survive to
// the end are the reflex ones, and each ends up the hub of a fan over the chain
// that was eaten around it. That is the bake's own shape — its hubs are mostly
// reflex vertices (43, 9, 11, 13, 15, 19, 23, 51), each fanning a short chain,
// and no hub appears in more than 8 of its 57 triangles.
//
// The containment test runs against the reflex set, never the whole ring, which
// is the structure Caver::BuildConcaveList (0x2D6E14) + Caver::IsAnEar (0x2D6EEC)
// set up, and keeps each step O(n).
bool ear_clip_ring(const std::vector<V2>& pts, const std::vector<int>& idx,
                   std::vector<uint16_t>& out, bool relaxed) {
    const int m = static_cast<int>(pts.size());
    if (m < 3) return false;

    std::vector<int> prv(m), nxt(m);
    std::vector<char> live(m, 1);
    for (int i = 0; i < m; ++i) {
        prv[i] = (i - 1 + m) % m;
        nxt[i] = (i + 1) % m;
    }

    // BuildConcaveList: the reflex set is built once, then maintained — a vertex
    // that the ring makes convex again leaves it.
    auto is_reflex = [&](int i) {
        return !(crosso(sub(pts[i], pts[prv[i]]), sub(pts[nxt[i]], pts[i])) > 0.0);
    };
    std::vector<char> reflex(m, 0);
    for (int i = 0; i < m; ++i) reflex[i] = is_reflex(i) ? 1 : 0;

    const double eps = 1e-9;
    int remaining = m;
    while (remaining > 3) {
        int pick = -1;
        double pick_ang = 0.0;
        for (int i = 0; i < m; ++i) {
            if (!live[i] || reflex[i]) continue;   // a reflex corner is not an ear
            const int p = prv[i];
            const int q = nxt[i];
            const V2 a = pts[p], b = pts[i], c = pts[q];
            // The interior angle at the tip. A zero or straight corner contributes
            // no area; the strict pass skips it, the relaxed pass keeps it so a
            // duplicated or exactly-collinear node still yields an ear.
            const double e1x = a.x - b.x, e1y = a.y - b.y;
            const double e2x = c.x - b.x, e2y = c.y - b.y;
            const double ang = std::fabs(std::atan2(e1x * e2y - e1y * e2x,
                                                   e1x * e2x + e1y * e2y));
            if (!relaxed && (ang <= 1e-12 || ang >= kPi - 1e-12)) continue;
            if (pick >= 0 && ang >= pick_ang) continue;   // ties keep the lower index
            bool blocked = false;
            for (int j = 0; j < m && !blocked; ++j) {
                if (!live[j] || j == p || j == i || j == q) continue;
                const V2 t = pts[j];
                if (relaxed && (std::fabs(t.x - a.x) < eps && std::fabs(t.y - a.y) < eps))
                    continue;
                const double d1 = crosso(sub(a, c), sub(t, c));
                const double d2 = crosso(sub(b, a), sub(t, a));
                const double d3 = crosso(sub(c, b), sub(t, b));
                if (d1 > eps && d2 > eps && d3 > eps) blocked = true;
            }
            if (blocked) continue;
            pick = i;
            pick_ang = ang;
        }
        if (pick < 0) return false;
        const int p = prv[pick], q = nxt[pick];
        out.push_back(static_cast<uint16_t>(idx[p]));
        out.push_back(static_cast<uint16_t>(idx[pick]));
        out.push_back(static_cast<uint16_t>(idx[q]));
        live[pick] = 0;
        nxt[p] = q;
        prv[q] = p;
        --remaining;
        reflex[p] = is_reflex(p) ? 1 : 0;
        reflex[q] = is_reflex(q) ? 1 : 0;
    }

    int a0 = -1;
    for (int i = 0; i < m; ++i)
        if (live[i]) { a0 = i; break; }
    if (a0 < 0) return false;
    const int a1 = nxt[a0], a2 = nxt[a1];
    out.push_back(static_cast<uint16_t>(idx[a0]));
    out.push_back(static_cast<uint16_t>(idx[a1]));
    out.push_back(static_cast<uint16_t>(idx[a2]));
    return true;
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

    // Recursive shortest-valid-diagonal split. `ring` holds positions and `order`
    // the matching outline indices, so the emitted triangles index the caller's
    // outline no matter how the ring was rotated or reversed above.
    std::vector<V2> ring2(ring);
    std::vector<int> order(orig);

    // Candidates are visited shortest-first, so the first valid one is the
    // shortest valid diagonal and the scan can stop there. `touching` relaxes the
    // endpoint-contact rule, which is what rescues outlines with a duplicated or
    // collinear node (the case the old port fanned). `fanned` records that a
    // sub-ring had no valid diagonal at all, so the caller can retry relaxed
    // before accepting a fan.
    bool fanned = false;
    std::function<void(std::vector<V2>&, std::vector<int>&, bool)> split =
        [&](std::vector<V2>& pts, std::vector<int>& idx, bool touching) {
            const int m = static_cast<int>(pts.size());
            if (m < 3) return;
            if (m == 3) {
                out_indices.push_back(static_cast<uint16_t>(idx[0]));
                out_indices.push_back(static_cast<uint16_t>(idx[1]));
                out_indices.push_back(static_cast<uint16_t>(idx[2]));
                return;
            }
            std::vector<std::pair<double, std::pair<int, int>>> cands;
            cands.reserve(static_cast<size_t>(m) * static_cast<size_t>(m) / 2);
            for (int i = 0; i < m; ++i) {
                for (int j = i + 2; j < m; ++j) {
                    if (i == 0 && j == m - 1) continue;   // that is a ring edge
                    const double dx = pts[i].x - pts[j].x;
                    const double dy = pts[i].y - pts[j].y;
                    cands.push_back({dx * dx + dy * dy, {i, j}});
                }
            }
            std::sort(cands.begin(), cands.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& cand : cands) {
                const int i = cand.second.first;
                const int j = cand.second.second;
                bool ok = true;
                for (int k = 0; k < m && ok; ++k) {
                    const int k2 = (k + 1) % m;
                    if (k == i || k == j || k2 == i || k2 == j) continue;
                    if (touching) {
                        // Only a proper crossing rejects here.
                        const double o1 = crosso(sub(pts[k2], pts[k]), sub(pts[i], pts[k]));
                        const double o2 = crosso(sub(pts[k2], pts[k]), sub(pts[j], pts[k]));
                        const double o3 = crosso(sub(pts[j], pts[i]), sub(pts[k], pts[i]));
                        const double o4 = crosso(sub(pts[j], pts[i]), sub(pts[k2], pts[i]));
                        const double eps = 1e-9;
                        if (((o1 > eps) != (o2 > eps)) && ((o3 > eps) != (o4 > eps)))
                            ok = false;
                    } else if (segments_conflict(pts[i], pts[j], pts[k], pts[k2])) {
                        ok = false;
                    }
                }
                if (!ok) continue;
                // The midpoint must also lie inside the sub-ring, or the diagonal
                // merely runs alongside a concave throat without crossing it.
                const V2 mid{(pts[i].x + pts[j].x) * 0.5, (pts[i].y + pts[j].y) * 0.5};
                if (!point_in_ring(pts, mid)) continue;
                std::vector<V2> left(pts.begin() + i, pts.begin() + j + 1);
                std::vector<int> lefti(idx.begin() + i, idx.begin() + j + 1);
                std::vector<V2> right;
                std::vector<int> righti;
                right.reserve(static_cast<size_t>(m) - static_cast<size_t>(j) +
                              static_cast<size_t>(i) + 1);
                for (int k = j; k < m; ++k) {
                    right.push_back(pts[k]);
                    righti.push_back(idx[k]);
                }
                for (int k = 0; k <= i; ++k) {
                    right.push_back(pts[k]);
                    righti.push_back(idx[k]);
                }
                split(left, lefti, touching);
                split(right, righti, touching);
                return;
            }
            // No diagonal passed. A simple polygon always has an ear (Two Ears
            // Theorem), so clip one before even considering a fan: scan for a
            // convex corner with no ring vertex strictly inside its triangle,
            // take the first, and recurse. This is the safety net that keeps a
            // duplicated or exactly-collinear node from collapsing the cap.
            for (int k = 0; k < m; ++k) {
                const int kp = (k - 1 + m) % m;
                const int kn = (k + 1) % m;
                const V2 a = pts[kp];
                const V2 b = pts[k];
                const V2 c = pts[kn];
                const double turn = crosso(sub(b, a), sub(c, b));
                if (turn <= 1e-9) continue;
                bool blocked = false;
                for (int q = 0; q < m && !blocked; ++q) {
                    if (q == kp || q == k || q == kn) continue;
                    const V2 t = pts[q];
                    const double d1 = crosso(sub(a, c), sub(t, c));
                    const double d2 = crosso(sub(b, a), sub(t, a));
                    const double d3 = crosso(sub(c, b), sub(t, b));
                    if (d1 > 1e-9 && d2 > 1e-9 && d3 > 1e-9) blocked = true;
                }
                if (blocked) continue;
                out_indices.push_back(static_cast<uint16_t>(idx[kp]));
                out_indices.push_back(static_cast<uint16_t>(idx[k]));
                out_indices.push_back(static_cast<uint16_t>(idx[kn]));
                std::vector<V2> rest(pts);
                std::vector<int> resti(idx);
                rest.erase(rest.begin() + k);
                resti.erase(resti.begin() + k);
                split(rest, resti, touching);
                return;
            }
            // Nothing valid at all. Fan it, and let the area gate below decide
            // whether the result is still a faithful cap.
            fanned = true;
            for (int k = 1; k + 1 < m; ++k) {
                out_indices.push_back(static_cast<uint16_t>(idx[0]));
                out_indices.push_back(static_cast<uint16_t>(idx[k]));
                out_indices.push_back(static_cast<uint16_t>(idx[k + 1]));
            }
        };
    // Sharpest corner first. The shortest-diagonal split below stays as the
    // fallback: it is the machinery that rescues a duplicated or exactly
    // collinear node, and the area gate at the end still judges the result.
    if (!ear_clip_ring(ring, order, out_indices, /*relaxed=*/false)) {
        out_indices.clear();
        if (!ear_clip_ring(ring, order, out_indices, /*relaxed=*/true)) {
            out_indices.clear();
            split(ring2, order, /*touching=*/false);
        }
    }
    if (fanned) {
        // A sub-ring stalled. Retry the whole outline with endpoint contact
        // allowed (a duplicated or exactly-collinear node makes every strict
        // diagonal invalid), and only accept the fan if that fails too.
        out_indices.clear();
        fanned = false;
        split(ring2, order, /*touching=*/true);
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
    if (std::fabs(emitted - expected) > tol) {
        if (std::getenv("BOULDERX_DEBUG_TRI")) {
            std::fprintf(stderr,
                         "[tri] n=%zu tris=%zu emitted=%.1f expected=%.1f fanned=%d\n",
                         n, out_indices.size() / 3, emitted, expected, fanned ? 1 : 0);
        }
        return false;
    }
    return !out_indices.empty();
}

bool generate(const std::vector<Node>& outline_in, const Params& params, Mesh& out,
              const std::vector<Hat>* hats) {
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

    // The depth arrays. `Engine` is GenerateMesh's own law and the default: the
    // per-node depth comes from SurfaceWidth and HorizNoise, with two frand()
    // draws per node. `Outline` is the port's extension (relief dragged in the
    // mesh editor), and is what the editors ask for when they have their own
    // per-node depths to honour.
    std::vector<double> front(n), back(n);
    if (params.depth_source == Params::DepthSource::Engine) {
        engine_node_depths(params, n, front, back);
    } else {
        Frand rng(params.random_seed);
        for (size_t i = 0; i < n; ++i) {
            const double f = rng.next();
            front[i] = outline[i].front_depth + (f - 0.5) * params.horiz_noise;
            const double b = rng.next();
            back[i] = outline[i].back_depth - (b - 0.5) * params.horiz_noise;
        }
    }

    // Field 8 order is significant: GenerateMesh adds the rim first, then the
    // plain-with-gaps ribbon. Shipped objects confirm it (SurfaceMesh #1 carries
    // SurfaceTextureMapping, #2 carries FrontTextureMapping).
    //
    // MeshType 1 splits the outline between the two: the rim takes its nodes and
    // the ribbon yields to it, keeping only the first node of each rim run so the
    // two strips still meet (thecave_crypt2 obj5 node 17 carries the rim's +/-95,
    // the ribbon's +/-90 *and* the cap's +90; nodes 18..22 carry no ribbon vertex
    // at all). MeshType 0 has no rim, so its single strip covers everything.
    std::vector<char> no_ribbon(n, 0);
    if (params.mesh_type == 1) {
        for (size_t i = 0; i < n; ++i)
            if (!plain_node(outline, i)) no_ribbon[i] = 1;
    }
    // Each family goes out in the engine's <=20-face chunks (chunk_submesh), in
    // the engine's order: every rim chunk, then every ribbon chunk, then the
    // cap's, which is the sequence GenerateMesh's two AddSurfaceMesh calls and
    // AddFrontMesh produce once GroundMeshComponent has split them.
    auto append_chunked = [](std::vector<Submesh>& dst, const Submesh& sm) {
        for (auto& c : chunk_submesh(sm)) dst.push_back(std::move(c));
    };
    if (params.mesh_type == 1)
        append_chunked(out.surfaces, build_rim(outline, back, front, params));
    append_chunked(out.surfaces, build_ribbon(outline, back, front,
                                              params.mesh_type == 1 ? params.front_texture
                                                                    : params.surface_texture,
                                              params, /*close_loop=*/true, no_ribbon));
    append_chunked(out.fronts, build_cap(outline, front, params));

    // A refused triangulation yields a cap with no vertices, and chunking then
    // contributes no chunk at all — so test the vector, not just its first entry.
    if (out.fronts.empty() || out.fronts[0].vertices.empty()) {
        out.error = "triangulation failed (degenerate or self-intersecting outline)";
        return false;
    }

    // Dome hats last, one SurfaceMesh each — the order boulder emits and the
    // order doc 09 §3 records for a generated GroundMesh. A hat with no radius
    // or no height has no geometry to contribute and is skipped rather than
    // emitted as a degenerate spike.
    if (hats) {
        double base_y = outline[0].y;
        double z_lo = back[0], z_hi = front[0];
        for (size_t i = 0; i < n; ++i) {
            base_y = std::max(base_y, outline[i].y);
            z_lo = std::min(z_lo, back[i]);
            z_hi = std::max(z_hi, front[i]);
        }
        for (const Hat& hat : *hats) {
            if (hat.radius <= 0.0 || hat.height <= 0.0) continue;
            out.surfaces.push_back(build_hat(hat, base_y, z_lo, z_hi, params));
        }
    }

    out.ok = true;
    return true;
}

// ── The engine's per-node depth law ─────────────────────────────────────────
void engine_node_depths(const Params& params, size_t node_count,
                        std::vector<double>& front_out, std::vector<double>& back_out) {
    front_out.assign(node_count, 0.0);
    back_out.assign(node_count, 0.0);
    const double half = params.surface_width * 0.5;
    const double noise = params.horiz_noise;
    Frand rng(params.random_seed);
    for (size_t i = 0; i < node_count; ++i) {
        // Two draws per node, front then back — the order GenerateMesh's loop
        // makes them in, and the order matters because the sequence is shared.
        const double f = rng.next();
        const double b = rng.next();
        front_out[i] = (f - 0.5) * noise + half;
        back_out[i] = -half - noise * (b - 0.5);
    }
}

// ── The flat-Z ship path — #will be unlocked after ───────────────────────
// One constant plane for every node: the vanilla-exact SurfaceWidth/2 (see the
// declaration in boulderx.h). No PRNG draw, so the arrays depend on nothing but
// the object's own SurfaceWidth — the same number in, the same number out,
// every import, on every platform.
void flat_node_depths(double plane, size_t node_count,
                      std::vector<double>& front_out, std::vector<double>& back_out) {
    front_out.assign(node_count, plane);
    back_out.assign(node_count, -plane);
}

void flat_node_depths(const Params& params, size_t node_count,
                      std::vector<double>& front_out, std::vector<double>& back_out) {
    flat_node_depths(params.surface_width * 0.5, node_count, front_out, back_out);
}

// The declared-depth form of the same rule. MeshType 1 declares
// plane + HatWidthOffset1, so the plane is that much SHALLOWER than |MaxDepth|;
// subtracting it is what keeps the emit from drifting the declaration a second
// W1 further out on every edit (the "it over-increases the Z" report).
double flat_plane_declared(double abs_max_depth, int mesh_type,
                           double hat_width_offset_1) {
    double plane = std::fabs(abs_max_depth);
    if (mesh_type == 1) plane -= std::fabs(hat_width_offset_1);
    // A declaration that already carries no room for the rim must not produce a
    // negative (inverted) slab.
    if (!(plane > 0.0)) plane = 0.0;
    return plane;
}

// ── .swdm text ──────────────────────────────────────────────────────────────
// A boulderx sheet is format v2: per-node depths, which v1 (boulder) has no way
// to store. The marker is emitted first so a reader can tell the dialects apart
// before it has parsed anything — boulder::parse_gmesh() relies on exactly that
// to refuse a v2 sheet rather than flatten it.
std::string serialize_swdm(const SwdmDocument& doc) {
    std::ostringstream o;
    o << "// Swordigo Desktop Mesh (.swdm) — Zenith sheet, format v2\n";
    o << "// Per-node depth. Boulder (v1) cannot read this file; Zenith reads both.\n";
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
    // Dome hats, in boulder's block shape so the sheet stays readable by the v1
    // parser too. Written only when there are hats, so a hatless sheet is
    // byte-identical to what this writer produced before hats existed.
    if (!doc.hats.empty()) {
        o << "    Hat[\n";
        for (const auto& h : doc.hats)
            o << "    " << h.x << " " << h.y << " " << h.radius << " " << h.height << "\n";
        o << "    ]\n";
    }
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
// `from_boulder_dialect` records which one this was, and a `Hat[` block in
// either dialect becomes real dome geometry in `hats` — read, not merely
// flagged, because a flag can only ever justify a refusal.
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
                // v1 sheet carries must NOT become one.
                Hat h;
                if (vs >> h.x >> h.y >> h.radius >> h.height) out.hats.push_back(h);
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
                                        const ComponentIds* ids,
                                        const std::vector<Hat>* hats) {
    clear_decline_reason();
    if (outline_in.size() < 3)
        return decline("polygon has fewer than 3 nodes");
    ComponentIds cid = ids ? *ids : ComponentIds{};

    Mesh mesh;
    if (!generate(outline_in, params, mesh, hats))
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
    double aabb_x = left - outset;
    double aabb_y = bottom - outset;
    double aabb_w = (right - left) + 2 * outset;
    double aabb_h = (top - bottom) + 2 * outset;

    // A dome hat stands OUTSIDE the polygon's own bounds: it rises `height`
    // above the top edge and its footprint can overhang the side. The object's
    // LocalAabb has to contain it, or the engine may cull the dome and leave a
    // hole in the level. boulder leaves the Aabb on the polygon alone; with hats
    // on a sheet that is the visible difference, and a hatless object still gets
    // exactly the polygon rectangle it always did.
    if (hats) {
        for (const Hat& hat : *hats) {
            if (hat.radius <= 0.0 || hat.height <= 0.0) continue;   // no geometry
            const double hx0 = std::min(aabb_x, hat.x - hat.radius);
            const double hx1 = std::max(aabb_x + aabb_w, hat.x + hat.radius);
            const double hy0 = std::min(aabb_y, top + 0.05);              // dome base
            const double hy1 = std::max(aabb_y + aabb_h, top + 0.05 + hat.height);
            aabb_x = hx0;
            aabb_w = hx1 - hx0;
            aabb_y = hy0;
            aabb_h = hy1 - hy0;
        }
    }

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
    // Field 6 is the object's own HorizNoise. `emitted_horiz_noise` lets an edit
    // that rebuilt from already-jittered depths (see recover_node_depths) keep
    // the object's generator parameters while generating without re-applying the
    // jitter — see Params::emitted_horiz_noise. boulder hardcodes 0 here.
    ggc.write_float_field(6, static_cast<float>(params.emitted_horiz_noise >= 0.0
                                                    ? params.emitted_horiz_noise
                                                    : params.horiz_noise));
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
    // A `Hat[` block generates its domes rather than refusing the sheet: see the
    // note on boulderx::Hat in the header for why the dome exists at all when the
    // engine has no such thing.
    return generate_ground_mesh_object(doc.outline, doc.params,
                                       identifier.empty() ? doc.identifier : identifier,
                                       ids, &doc.hats);
}

// ── Generator choice ────────────────────────────────────────────────────────
// The environment variable only sets the *initial* value. A choice saved from
// the settings GUI calls set_ground_generator() afterwards and wins, which is
// what makes RUBY_GROUND_GENERATOR a debugging default rather than something
// that silently overrides the user every launch.
namespace {
GroundGenerator g_ground_generator = [] {
    // Boulder is the DEFAULT. It is the generator that always returns a mesh, so
    // it is what a scene is built with unless someone asks otherwise; BoulderX is
    // opt-in from either settings picker.
    //
    // BoulderX is still the engine-faithful generator — it handles concave
    // outlines / arcs / per-node depth, and its recovered per-node depth is what
    // keeps a vanilla object's relief instead of rebuilding it from one scalar —
    // so selecting it changes nothing about the mesh pipeline. It is also still
    // the automatic fallback target: BoulderX declines what it cannot reproduce
    // and the editors then fall back to Boulder.
    //
    // RUBY_GROUND_GENERATOR overrides the initial value for A/B-ing a scene
    // without a rebuild; a saved picker choice calls set_ground_generator()
    // afterwards and wins, so the variable stays a debugging default.
    const char* env = std::getenv("RUBY_GROUND_GENERATOR");
    if (env) {
        if (std::strcmp(env, "zypher") == 0) return GroundGenerator::Zypher;
        if (std::strcmp(env, "boulder") == 0) return GroundGenerator::Boulder;
        if (std::strcmp(env, "boulderx") == 0 || std::strcmp(env, "zenith") == 0) return GroundGenerator::BoulderX;
    }
    return GroundGenerator::Boulder;
}();
}

GroundGenerator ground_generator() { return g_ground_generator; }
void set_ground_generator(GroundGenerator which) { g_ground_generator = which; }

const char* ground_generator_id(GroundGenerator which) {
    if (which == GroundGenerator::Zypher) return "zypher";
    return which == GroundGenerator::BoulderX ? "boulderx" : "boulder";
}

GroundGenerator ground_generator_from_id(const std::string& id) {
    // Total, and biased the same way the default is: an explicit "zypher", "boulderx" or "zenith" is the
    // only way to select non-default generators.
    if (id == "zypher") return GroundGenerator::Zypher;
    return (id == "boulderx" || id == "zenith") ? GroundGenerator::BoulderX : GroundGenerator::Boulder;
}

const std::string& last_decline_reason() { return g_decline_reason; }
void clear_decline_reason() { g_decline_reason.clear(); }

std::string generate_ground_mesh_object_flat(
    const std::vector<std::pair<double, double>>& polygon_xy,
    int mesh_type, double half_depth,
    const std::string& surface_texture, const std::string& front_texture,
    const std::string& identifier, const std::vector<Hat>& hats,
    const int* ids6) {
    clear_decline_reason();
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

    return generate_ground_mesh_object_nodes(outline, params, identifier, hats, ids6);
}

std::string generate_ground_mesh_object_nodes(
    const std::vector<Node>& outline, const Params& params,
    const std::string& identifier, const std::vector<Hat>& hats, const int* ids6) {
    clear_decline_reason();
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
    return generate_ground_mesh_object(outline, params, identifier, &cid, &hats);
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
