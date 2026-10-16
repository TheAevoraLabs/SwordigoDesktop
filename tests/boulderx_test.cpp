// ============================================================================
// boulderx_test.cpp — behaviour tests for the Caver-faithful ground generator.
//
// The point of these tests is the class of shape boulder cannot produce: concave
// outlines (70% of the 2,518 shipped ground polygons), curves/arcs, per-node
// depth, and HorizNoise relief.  boulder's own header lists the last one as
// unimplemented, and its top-surface builder only recognises edges within
// `top_angle` of horizontal, so a slope or arc produces no surface at all.
//
// Engine agreement is pinned separately, against measured shipped values, by
// boulderx_engine_parity_test.cpp.
// ============================================================================

#include "tools/boulderx.h"
#include "tools/boulder.h"   // the v1 dialect, for the compatibility gate below

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL  %s\n", what.c_str());
        ++g_failures;
    }
}

static void check_near(double got, double want, double tol, const std::string& what) {
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL  %s (got %.6f, want %.6f +/- %g)\n", what.c_str(), got, want, tol);
        ++g_failures;
    }
}

static double tri_area(double ax, double ay, double bx, double by, double cx, double cy) {
    return 0.5 * std::fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
}

static double outline_area(const std::vector<boulderx::Node>& o) {
    double a = 0.0;
    for (size_t i = 0; i < o.size(); ++i) {
        const auto& p = o[i];
        const auto& q = o[(i + 1) % o.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5 * std::fabs(a);
}

// ── 1. Concave outlines: the shape class boulder's template cannot express ──
static void test_concave_triangulation() {
    std::printf("[1] concave / L-shaped / comb triangulation\n");
    struct Case { const char* name; std::vector<std::pair<double, double>> pts; };
    const std::vector<Case> cases = {
        {"L-shape", {{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}}},
        {"U-trough", {{0, 0}, {6, 0}, {6, 5}, {5, 5}, {5, 1}, {1, 1}, {1, 5}, {0, 5}}},
        {"comb", {{0, 0}, {6, 0}, {6, 4}, {5, 4}, {5, 1}, {4, 1}, {4, 4},
                  {3, 4}, {3, 1}, {2, 1}, {2, 4}, {1, 4}, {1, 1}, {0, 1}}},
        {"star-ish spiral", {{0, 0}, {8, 0}, {8, 8}, {1, 8}, {1, 1}, {7, 1},
                             {7, 7}, {2, 7}, {2, 2}, {6, 2}, {6, 6}, {3, 6}}},
    };
    for (const auto& c : cases) {
        std::vector<uint16_t> tris;
        const bool ok = boulderx::triangulate(c.pts, tris);
        check(ok, std::string(c.name) + ": triangulates");
        const size_t n = c.pts.size();
        check(tris.size() == (n - 2) * 3,
              std::string(c.name) + ": " + std::to_string(n - 2) + " triangles");
        double area = 0.0;
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            const auto& a = c.pts[tris[t]];
            const auto& b = c.pts[tris[t + 1]];
            const auto& d = c.pts[tris[t + 2]];
            area += tri_area(a.first, a.second, b.first, b.second, d.first, d.second);
            check(tris[t] != tris[t + 1] && tris[t + 1] != tris[t + 2] && tris[t] != tris[t + 2],
                  std::string(c.name) + ": no degenerate index triple");
        }
        double poly_area = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const auto& p = c.pts[i];
            const auto& q = c.pts[(i + 1) % n];
            poly_area += p.first * q.second - q.first * p.second;
        }
        poly_area = std::fabs(0.5 * poly_area);
        check_near(area, poly_area, poly_area * 1e-6 + 1e-9,
                   std::string(c.name) + ": triangles tile the outline");
    }
}

// ── 2. A real concave vanilla outline (20 nodes, fire_part1.scene) ──────────
static void test_vanilla_concave_outline() {
    std::printf("[2] real 20-node concave vanilla outline\n");
    const std::vector<std::pair<double, double>> xy = {
        {-52.598, 21.748}, {-61.923, -13.003}, {-143.823, -14.321}, {-148.877, -60.396},
        {-134.058, -101.517}, {-73.765, -172.373}, {76.276, -137.133}, {118.227, -74.527},
        {124.546, -12.288}, {148.877, 30.324}, {148.289, 76.231}, {123.966, 153.966},
        {32.495, 172.373}, {23.606, 98.096}, {35.744, 63.808}, {42.112, 58.114},
        {69.459, -7.827}, {40.433, -46.831}, {-34.626, 9.858}, {-51.4, 27.537}};
    boulderx::SwdmDocument doc;
    boulderx::append_polyline(doc.outline, xy, 85.0, -85.0);
    doc.params.mesh_type = 0;
    doc.params.horiz_noise = 0.0;
    // The outline's own depths are the subject of every check below, so this must
    // be the outline-driven law, not the engine's SurfaceWidth law. `Engine` is
    // the shipped default (see test [15]).
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;
    doc.params.surface_texture = "graveyard_ground";
    doc.params.front_texture = "graveyard_ground";

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "20-node concave outline generates");
    // Every family goes out in the engine's <=20-face chunks, so the shape of
    // the result is "some ribbon chunks + some cap chunks, none over 20 faces"
    // and the per-family totals, not one mesh per family (doc 13 §1.3.2).
    auto faces_of = [](const std::vector<boulderx::Submesh>& v) {
        size_t n = 0;
        for (const auto& s : v) n += s.indices.empty() ? s.vertices.size() / 3
                                                      : s.indices.size() / 3;
        return n;
    };
    auto chunked_ok = [&](const std::vector<boulderx::Submesh>& v) {
        for (const auto& s : v)
            if (faces_of({s}) > 20) return false;
        return true;
    };
    check(!mesh.surfaces.empty() && !mesh.fronts.empty(), "MT0 emits a ribbon and a cap");
    check(chunked_ok(mesh.surfaces) && chunked_ok(mesh.fronts),
          "every submesh is at most 20 faces");
    check(faces_of(mesh.fronts) == xy.size() - 2, "cap has n-2 non-indexed triangles");
    check(faces_of(mesh.surfaces) == xy.size() * 2, "ribbon has two triangles per node");
    check_near(mesh.max_abs_z(), 85.0, 1e-3, "outermost depth is the authored half-depth");
}

// ── 3. Curves: boulder produces no surface at all for a sloped/arced top ────
static void test_arc_outline() {
    std::printf("[3] arc / curved outline\n");
    boulderx::SwdmDocument doc;
    // A semicircular hill: flat base, arced crest.
    doc.outline.push_back({-60.0, 0.0, 30.0, -30.0});
    boulderx::append_arc(doc.outline, 0.0, 0.0, 60.0, 180.0, 0.0, 24, 30.0, -30.0);
    doc.outline.push_back({60.0, 0.0, 30.0, -30.0});
    doc.params.mesh_type = 1;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "arced outline generates");
    const size_t corners = doc.outline.size();
    check(corners >= 26, "arc appended its samples");
    size_t cap_corners = 0;
    for (const auto& sm : mesh.fronts) cap_corners += sm.vertices.size();
    check(cap_corners == (corners - 2) * 3, "cap tiles the arched outline");
    // The crest is genuinely 3D: cap vertices reach the crest height.
    double top_y = -1e9;
    for (const auto& sm : mesh.fronts)
        for (const auto& v : sm.vertices) top_y = std::max(top_y, (double)v.y);
    check_near(top_y, 60.0, 0.5, "cap reaches the arc crest");

    // A per-node tilt on the arc (what an editor would drag) must survive.
    for (size_t i = 0; i < doc.outline.size(); ++i)
        doc.outline[i].front_depth = 30.0 + 10.0 * (double)i / (double)doc.outline.size();
    boulderx::Mesh tilted;
    check(boulderx::generate(doc.outline, doc.params, tilted), "tilted arc generates");
    check(tilted.max_abs_z() > 39.0, "per-node front depth raises the outermost plane");
}

// ── 4. Per-node depth: boulder stores Depth as one scalar for the mesh ──────
static void test_per_node_depth() {
    std::printf("[4] per-node depth\n");
    boulderx::SwdmDocument doc;
    const double front[4] = {20.0, 40.0, 60.0, 35.0};
    const double back[4] = {-20.0, -30.0, -50.0, -25.0};
    // A diamond, so the outline has real area (a zero-area outline is refused).
    const double px[4] = {0.0, 50.0, 100.0, 50.0};
    const double py[4] = {0.0, 40.0, 0.0, -40.0};
    for (int i = 0; i < 4; ++i)
        doc.outline.push_back({px[i], py[i], front[i], back[i]});
    doc.params.mesh_type = 0;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "variable-depth outline generates");
    check_near(mesh.max_abs_z(), 60.0, 1e-3, "maximum depth comes from node 2");
    // Node i contributes its own back/front pair, in that order. Matched by
    // position, not by index: ensure_ccw() may have reversed the outline.
    const auto& vs = mesh.surfaces[0].vertices;
    for (int i = 0; i < 4; ++i) {
        bool found = false;
        for (size_t k = 0; k + 1 < vs.size(); k += 2) {
            if (std::fabs(vs[k].x - px[i]) > 1e-3 || std::fabs(vs[k].y - py[i]) > 1e-3) continue;
            check_near(vs[k].z, back[i], 1e-3,
                       "node " + std::to_string(i) + " back depth");
            check_near(vs[k + 1].z, front[i], 1e-3,
                       "node " + std::to_string(i) + " front depth");
            found = true;
        }
        check(found, "node " + std::to_string(i) + " is present in the ribbon");
    }
    // And the cap follows the same per-node front depths (verified engine rule).
    for (const auto& v : mesh.fronts[0].vertices) {
        bool matched = false;
        for (int i = 0; i < 4; ++i)
            if (std::fabs(v.z - front[i]) < 1e-3) matched = true;
        check(matched, "cap corner uses one of the node front depths");
    }
}

// ── 5. HorizNoise: boulder documents this as never implemented ──────────────
static void test_horiz_noise() {
    std::printf("[5] HorizNoise relief + determinism\n");
    boulderx::SwdmDocument doc;
    for (int i = 0; i < 8; ++i) {
        const double a = i * 3.14159265 / 4.0;
        doc.outline.push_back({100.0 * std::cos(a), 100.0 * std::sin(a), 10.0, -10.0});
    }
    doc.params.mesh_type = 0;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;

    boulderx::Mesh flat;
    check(boulderx::generate(doc.outline, doc.params, flat), "noiseless outline generates");
    check_near(flat.max_abs_z(), 10.0, 1e-3, "HorizNoise 0 leaves the authored planes flat");

    doc.params.horiz_noise = 50.0;
    boulderx::Mesh noisy;
    check(boulderx::generate(doc.outline, doc.params, noisy), "noisy outline generates");
    check(noisy.max_abs_z() > 10.5,
          "HorizNoise displaces the mesh past the authored planes (boulder leaves this at 0)");
    check(noisy.max_abs_z() <= 35.0 + 1e-6, "displacement stays inside +/- noise/2");

    // Determinism: the engine guarantees same seed -> same mesh.
    boulderx::Mesh again;
    check(boulderx::generate(doc.outline, doc.params, again), "regenerates");
    check(noisy.surfaces[0].vertices.size() == again.surfaces[0].vertices.size(),
          "same seed -> same vertex count");
    bool identical = true;
    for (size_t i = 0; i < noisy.surfaces[0].vertices.size(); ++i) {
        const auto& a = noisy.surfaces[0].vertices[i];
        const auto& b = again.surfaces[0].vertices[i];
        if (a.z != b.z) identical = false;
    }
    check(identical, "same seed -> byte-identical depth arrays");

    doc.params.random_seed += 1;
    boulderx::Mesh other;
    check(boulderx::generate(doc.outline, doc.params, other), "different seed generates");
    bool differs = false;
    for (size_t i = 0; i < noisy.surfaces[0].vertices.size(); ++i)
        if (other.surfaces[0].vertices[i].z != noisy.surfaces[0].vertices[i].z) differs = true;
    check(differs, "different seed -> different relief");
}

// ── 6. MeshType structure + texture routing (verified against shipped data) ─
static void test_mesh_types() {
    std::printf("[6] MeshType 0/1 structure and texture routing\n");
    boulderx::SwdmDocument doc;
    boulderx::append_rect(doc.outline, 0, 0, 200, 100, 100.0, -100.0);
    doc.params.hat_height = 25.0;
    doc.params.hat_width_offset_1 = 5.0;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;

    doc.params.mesh_type = 0;
    boulderx::Mesh plain;
    check(boulderx::generate(doc.outline, doc.params, plain), "MT0 generates");
    check(plain.surfaces.size() == 1, "MT0 has one SurfaceMesh");
    check(plain.surfaces[0].texture == doc.params.surface_texture,
          "MT0 ribbon carries the surface texture");
    check(plain.fronts.size() == 1 && plain.fronts[0].texture == doc.params.front_texture,
          "MT0 cap carries the front texture");
    check_near(plain.max_abs_z(), 100.0, 1e-3, "MT0 stays on the authored planes");
    check(!plain.fronts[0].indexed, "cap is non-indexed (as InitializeMeshBuilder(false))");

    doc.params.mesh_type = 1;
    boulderx::Mesh rimmed;
    check(boulderx::generate(doc.outline, doc.params, rimmed), "MT1 generates");
    check(rimmed.surfaces.size() == 2, "MT1 has rim + inset ribbon");
    check(rimmed.surfaces[0].texture == doc.params.surface_texture,
          "MT1 SurfaceMesh #1 is the rim, on the surface texture");
    check(rimmed.surfaces[1].texture == doc.params.front_texture,
          "MT1 SurfaceMesh #2 is the inset ribbon, on the front texture");
    // Verified engine envelope for obj12: rim spans +/-105 with a bevel to
    // +/-110 while cap and inset stay at +/-100.
    check_near(rimmed.max_abs_z(), 110.0, 1e-3, "MT1 rim bevels 2*W past the authored plane");
    check_near(rimmed.surfaces[1].vertices[0].z, -100.0, 1e-3, "MT1 inset ribbon stays on the plane");
    for (const auto& v : rimmed.fronts[0].vertices)
        check_near(v.z, 100.0, 1e-3, "MT1 cap stays on the front plane");
    // Where the rim lands. MEASURED rule, not the single-edge reading: a node
    // carries a cross-section when EITHER of its edge normals faces +Y past
    // 1/sqrt(2), and a rim run spans consecutive such nodes. The evidence is
    // thecave_part15 obj9#10, whose 59-node outline the generator reproduces
    // vertex-for-vertex (tools/ground_mesh_truth.py + the node-by-node diff: 53 of
    // 59 nodes exact, the rest 0.004 float noise): there the rim owns nodes 0..20
    // while the plain wall owns 0, 16, 17, 20 and 21..58, which is only consistent
    // with the either-edge rule at the run boundaries. On this CCW rectangle that
    // is node 2 (200,100) — its outgoing edge runs -X, so its normal is +Y — and
    // node 3 (0,100), whose INCOMING edge (200,100)->(0,100) also faces +Y. Two
    // cross-sections, and because the two nodes are adjacent the rim sweeps a band
    // between them instead of stopping.
    check(rimmed.surfaces[0].vertices.size() == 12,
          "MT1 rim covers both +Y-facing nodes (two 6-vertex cross-sections)");
    check(!rimmed.surfaces[0].indices.empty(),
          "adjacent rim nodes sweep a band between their cross-sections");

    // The rim rolls INWARD (verified: obj12's ring steps 99.644 -> 89.644 ->
    // 74.644 in y where the outward normal points +y). Only z leaves the
    // authored planes.
    //
    // The rho = 0 profile pair is not exactly ON the outline: it sits on the node
    // pulled 3 units BACK along its incoming edge — the engine's convex-corner
    // shift, measured at exactly 3.000 at thecave_crypt2 obj5 node 17 and
    // beyond_graveyard obj12 node 2 (x 189.595 = 186.595 + 3). Here node 2's
    // incoming edge (200,0)->(200,100) runs +Y, so its base is (200, 103), and
    // node 3's incoming edge (200,100)->(0,100) runs -X, so its base is (-3, 100).
    // The inset after that is pure (0.4*HatHeight for the bevel, HatHeight for the
    // inner wall), so each cross-section keeps its shifted coordinate: node 2 runs
    // y 103 -> 93 -> 78 at x 200, node 3 runs y 100 -> 90 -> 75 at x -3.
    double max_x = -1e9, min_x = 1e9, max_y = -1e9, min_y = 1e9;
    bool saw2 = false, saw3 = false, saw_bevel = false, saw_wall = false;
    for (const auto& v : rimmed.surfaces[0].vertices) {
        max_x = std::max(max_x, (double)v.x);
        min_x = std::min(min_x, (double)v.x);
        max_y = std::max(max_y, (double)v.y);
        min_y = std::min(min_y, (double)v.y);
        if (std::fabs(v.x - 200.0) < 1e-3 && std::fabs(v.y - 103.0) < 1e-3) saw2 = true;
        if (std::fabs(v.x + 3.0) < 1e-3 && std::fabs(v.y - 100.0) < 1e-3) saw3 = true;
        if (std::fabs(v.y - 93.0) < 1e-3 || std::fabs(v.y - 90.0) < 1e-3) saw_bevel = true;
        if (std::fabs(v.y - 78.0) < 1e-3 || std::fabs(v.y - 75.0) < 1e-3) saw_wall = true;
    }
    check(saw2, "node 2's base sits 3 back along its +Y incoming edge");
    check(saw3, "node 3's base sits 3 back along its -X incoming edge");
    check_near(max_x, 200.0, 1e-3, "the rim only ever moves inward from the shifted base");
    check_near(min_x, -3.0, 1e-3, "…on both cross-sections (the inset is pure -Y)");
    check_near(max_y, 103.0, 1e-3, "node 2's cross-section starts on its shifted y");
    check_near(min_y, 75.0, 1e-3, "and the deepest point is HatHeight in from node 3");
    check(saw_bevel, "the bevel sits 0.4*HatHeight in from the outer pair");
    check(saw_wall, "and the inner wall HatHeight in");
}

// ── 7. .swdm round-trip ─────────────────────────────────────────────────────
static void test_swdm_roundtrip() {
    std::printf("[7] .swdm round-trip\n");
    boulderx::SwdmDocument doc;
    doc.identifier = "ledge";
    boulderx::append_rect(doc.outline, -10, -5, 120, 40, 33.0, -33.0);
    doc.params.mesh_type = 1;
    doc.params.horiz_noise = 12.5;
    doc.params.random_seed = 987654321u;
    doc.params.surface_texture = "crypt_tiles";
    doc.params.front_texture = "crypt_tiles";

    const std::string text = boulderx::serialize_swdm(doc);
    boulderx::SwdmDocument back;
    check(boulderx::parse_swdm(text, back), "parses what it serialized");
    check(back.identifier == doc.identifier, "identifier survives");
    check(back.outline.size() == doc.outline.size(), "node count survives");
    check(back.params.mesh_type == 1, "mesh type survives");
    check_near(back.params.horiz_noise, 12.5, 1e-9, "horiz noise survives");
    check(back.params.random_seed == 987654321u, "seed survives");
    check_near(back.outline[2].front_depth, 33.0, 1e-6, "per-node front depth survives");
    check_near(back.outline[2].back_depth, -33.0, 1e-6, "per-node back depth survives");
    check(back.params.surface_texture == "crypt_tiles", "texture survives");

    check(!boulderx::parse_swdm("GroundMesh{\n    Node : 0 0 1 -1\n}\n", back),
          "rejects an outline with fewer than 3 nodes");
}

// ── 8. Binary scene object ──────────────────────────────────────────────────
static void test_binary_object() {
    std::printf("[8] binary scene object emission\n");
    boulderx::SwdmDocument doc;
    boulderx::append_rect(doc.outline, 0, 0, 100, 50, 45.0, -45.0);
    doc.params.mesh_type = 1;

    const std::string bytes = boulderx::generate_ground_mesh_object(
        doc.outline, doc.params, "test_ground");
    check(!bytes.empty(), "emits a non-empty scene object");

    auto contains = [&](const char* needle) {
        return bytes.find(needle) != std::string::npos;
    };
    check(contains("SceneObject"), "carries the SceneObject template name");
    check(contains("test_ground"), "carries the identifier");
    for (const char* cls : {"GroundPolygon", "GroundMesh", "GroundMeshGenerator",
                            "CollisionShape", "TextureMapping"}) {
        check(contains(cls), std::string("carries component ") + cls);
    }
    check(contains("fire_grass"), "carries the surface texture name");
    check(contains("graveyard_ground"), "carries the front texture name");
    check(bytes.size() > 800, "payload carries real mesh data, not just headers");

    // A degenerate outline must fail rather than emit a collidable heap of
    // nothing: ShapeComponent and GroundMesh are separate components, so a
    // shape without a mesh still collides (the "invisible but solid" bug).
    std::vector<boulderx::Node> line = {{0, 0, 10, -10}, {10, 0, 10, -10}, {20, 0, 10, -10}};
    check(boulderx::generate_ground_mesh_object(line, doc.params, "degenerate").empty(),
          "collinear outline is refused, not emitted");
}

// ── 9. Generator choice + recorded decline reason ───────────────────────────
//
// The settings GUI persists this choice and the mesh-edit HUD reports what it
// actually did, so both the id round-trip and the refusal record are
// load-bearing. A reason that can disagree with an empty result is what lets a
// fallback present itself as success — the silent-loss failure this file exists
// to prevent.
static void test_generator_choice() {
    std::printf("[9] generator choice + decline reason\n");

    // Boulder is the shipped default, and BoulderX is opt-in. Checked before any
    // setter runs, and skipped when the environment variable has deliberately
    // overridden the initial value.
    if (std::getenv("RUBY_GROUND_GENERATOR") == nullptr) {
        check(boulderx::ground_generator() == boulderx::GroundGenerator::Boulder,
              "Boulder is the default generator");
    }

    // Round-trip, plus totality: the id comes from a persisted config file, so an
    // unrecognised one must resolve to a real generator rather than to a third
    // state — and, since Boulder is the default, to the DEFAULT one rather than
    // silently switching a scene to the other. Only the literal "boulderx"
    // selects BoulderX.
    for (boulderx::GroundGenerator g : {boulderx::GroundGenerator::Boulder,
                                        boulderx::GroundGenerator::BoulderX}) {
        check(boulderx::ground_generator_from_id(boulderx::ground_generator_id(g)) == g,
              std::string("id round-trips: ") + boulderx::ground_generator_id(g));
    }
    check(boulderx::ground_generator_from_id("boulderx") == boulderx::GroundGenerator::BoulderX,
          "an explicit boulderx id selects BoulderX (still fully available)");
    check(boulderx::ground_generator_from_id("") == boulderx::GroundGenerator::Boulder,
          "absent id resolves to the default, Boulder");
    check(boulderx::ground_generator_from_id("ZenithMesh") == boulderx::GroundGenerator::Boulder,
          "unknown id resolves to the default, not to the non-default generator");

    // The setter is what the settings bridge drives; it must take effect on the
    // next call, not the next launch.
    boulderx::set_ground_generator(boulderx::GroundGenerator::BoulderX);
    check(boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX,
          "set_ground_generator() takes effect immediately");
    boulderx::set_ground_generator(boulderx::GroundGenerator::Boulder);
    check(boulderx::ground_generator() == boulderx::GroundGenerator::Boulder,
          "and back again");

    const std::vector<std::pair<double, double>> square =
        {{0, 0}, {100, 0}, {100, 100}, {0, 100}};

    // A shape boulderx reproduces: succeeds and leaves no decline behind.
    boulderx::clear_decline_reason();
    const std::string ok = boulderx::generate_ground_mesh_object_flat(
        square, 1, 25.0, "surface_tex", "front_tex", "pick_ok", {}, nullptr);
    check(!ok.empty(), "flat adapter emits a plain square");
    check(boulderx::last_decline_reason().empty(),
          "a successful generation leaves no decline reason");

    // Dome hats are emitted, not refused. boulder owns this geometry (the engine
    // has no such thing — see the note on boulderx::Hat), so a generator that
    // dropped it would delete a dome from a level on a generator switch. The
    // geometry itself is pinned in [14].
    const std::vector<boulderx::Hat> hats = {{50.0, 50.0, 30.0, 20.0}};
    const std::string hat_ok = boulderx::generate_ground_mesh_object_flat(
        square, 1, 25.0, "surface_tex", "front_tex", "pick_hat", hats, nullptr);
    check(!hat_ok.empty(), "a dome hat is emitted rather than refused");
    check(boulderx::last_decline_reason().empty(),
          "and a hat is not a decline the HUD would report");

    // A degenerate request still declines, and a later success must clear that
    // reason, or the HUD would report a fallback that did not happen on this edit.
    const std::vector<std::pair<double, double>> too_few = {{0, 0}, {100, 0}};
    check(boulderx::generate_ground_mesh_object_flat(
              too_few, 1, 25.0, "surface_tex", "front_tex", "pick_bad", {}, nullptr)
              .empty(),
          "a two-node polygon is still refused");
    check(!boulderx::last_decline_reason().empty(), "a refusal always records a reason");
    boulderx::generate_ground_mesh_object_flat(
        square, 1, 25.0, "surface_tex", "front_tex", "pick_ok2", {}, nullptr);
    check(boulderx::last_decline_reason().empty(),
          "a subsequent success clears the stale reason");

    // Same contract on the outline/swdm entry point, which has its own declines.
    boulderx::SwdmDocument doc;
    boulderx::append_polyline(doc.outline, square, 10.0, -10.0);
    check(!boulderx::generate_ground_mesh_object(doc.outline, doc.params, "swdm_ok").empty(),
          "outline path emits");
    check(boulderx::last_decline_reason().empty(), "outline path clears on success");

    const std::vector<boulderx::Node> collinear = {
        {0, 0, 10, -10}, {10, 0, 10, -10}, {20, 0, 10, -10}};
    check(boulderx::generate_ground_mesh_object(collinear, doc.params, "swdm_bad").empty(),
          "collinear outline is refused");
    check(!boulderx::last_decline_reason().empty(),
          "the outline path records a reason too");
}

// How many DISTINCT positive depths the surface ribbon carries — i.e. how many
// different depths the nodes actually got.
//
// Note this is not the mesh's z-extent: a slab with front +40 / back -40 has an
// extent of 80 and yet every node is identical. Flatness is about variation
// across nodes, which is what a z-extent cannot show.
static int distinct_positive_z(const boulderx::Mesh& m) {
    std::vector<double> zs;
    for (const auto& s : m.surfaces)
        for (const auto& v : s.vertices)
            if (v.z > 1e-6) zs.push_back(std::llround(double(v.z) * 1000.0) / 1000.0);
    std::sort(zs.begin(), zs.end());
    zs.erase(std::unique(zs.begin(), zs.end()), zs.end());
    return int(zs.size());
}

// ── 10. Per-node depth reaches the mesh (the "boulderx strips the Z" bug) ───
//
// The editors used to route every regeneration through the flat overload, which
// assigns one half-depth to every node — so an edit replaced vanilla relief with
// a constant-thickness slab. This pins both halves: the uniform path really does
// flatten, and the per-node path really does carry depth through to the emitted
// object.
static void test_node_depth_preserved() {
    std::printf("[10] per-node depth survives generation\n");

    const std::vector<std::pair<double, double>> xy = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
    boulderx::SwdmDocument doc;
    boulderx::append_polyline(doc.outline, xy, 40.0, -40.0);
    doc.params.mesh_type = 0;
    doc.params.horiz_noise = 0.0;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;

    boulderx::Mesh flat;
    check(boulderx::generate(doc.outline, doc.params, flat), "uniform outline generates");
    check(distinct_positive_z(flat) == 1,
          "a uniform outline gives every node the same depth (this is what the flat adapter produced)");

    // Same outline, one depth per node.
    std::vector<boulderx::Node> outline;
    for (size_t i = 0; i < xy.size(); ++i) {
        outline.push_back({xy[i].first, xy[i].second,
                           40.0 + 30.0 * double(i),
                           -(40.0 + 10.0 * double(i))});
    }
    boulderx::Mesh varied;
    check(boulderx::generate(outline, doc.params, varied), "per-node outline generates");
    check(distinct_positive_z(varied) == 4,
          "each node keeps its own depth instead of being flattened to one");

    // And it has to survive into the emitted scene object, which is the only
    // thing the editor ever sees.
    const std::string bin = boulderx::generate_ground_mesh_object_nodes(
        outline, doc.params, "relief_probe", {}, nullptr);
    check(!bin.empty(), "per-node outline emits a scene object");
    check(bin.find("relief_probe") != std::string::npos,
          "the emitted object carries its identifier");

    // Hats ride along with a per-node outline too, instead of deciding whether
    // the object may be generated at all.
    const std::vector<boulderx::Hat> hats = {{50.0, 50.0, 30.0, 20.0}};
    boulderx::clear_decline_reason();
    const std::string with_hat =
        boulderx::generate_ground_mesh_object_nodes(outline, doc.params, "hat", hats, nullptr);
    check(!with_hat.empty(), "the per-node entry point emits dome hats");
    check(boulderx::last_decline_reason().empty(), "and declines nothing");
}

// ── 11. Relief randomiser ──────────────────────────────────────────────────
// Terrain sheets are 2D, so Z can only come from here. It has to be bounded (no
// exploding geometry), deterministic (so a seed you liked is reproducible) and
// actually varied (or the feature does nothing).
static void test_relief_randomiser() {
    std::printf("[11] relief randomiser: bounded, reproducible, not flat\n");

    auto make_outline = [](int n) {
        std::vector<boulderx::Node> o;
        for (int i = 0; i < n; ++i) o.push_back({double(i) * 10.0, 0.0, 0.0, 0.0});
        return o;
    };

    std::vector<boulderx::Node> a = make_outline(12);
    std::vector<boulderx::Node> b = make_outline(12);
    std::vector<boulderx::Node> c = make_outline(12);
    boulderx::randomise_node_depths(a, 50.0, 50.0, 12345u);
    boulderx::randomise_node_depths(b, 50.0, 50.0, 12345u);
    boulderx::randomise_node_depths(c, 50.0, 50.0, 999u);

    bool same_seed_identical = true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].front_depth != b[i].front_depth || a[i].back_depth != b[i].back_depth)
            same_seed_identical = false;
    }
    check(same_seed_identical, "the same seed reproduces the same relief");

    bool seed_changes_result = false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::fabs(a[i].front_depth - c[i].front_depth) > 1e-9)
            seed_changes_result = true;
    check(seed_changes_result, "a different seed gives different relief");

    bool in_range = true, varied = false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].front_depth < 0.0 || a[i].front_depth > 50.0 + 1e-9) in_range = false;
        if (a[i].back_depth > 0.0 || a[i].back_depth < -50.0 - 1e-9) in_range = false;
        if (std::fabs(a[i].front_depth - a[0].front_depth) > 1e-9) varied = true;
    }
    check(in_range, "relief stays inside the range it was given");
    check(varied, "relief is not a constant slab");

    // The ranges are magnitudes, so negatives must be normalised rather than
    // silently producing inverted depth on every node.
    std::vector<boulderx::Node> d = make_outline(6);
    boulderx::randomise_node_depths(d, -30.0, -30.0, 7u);
    bool still_in_range = true;
    for (const auto& n : d)
        if (n.front_depth < 0.0 || n.front_depth > 30.0 + 1e-9 || n.back_depth > 0.0)
            still_in_range = false;
    check(still_in_range, "negative magnitudes are normalised, not inverted");
}

// ── 12. The .swdm dialect gate ──────────────────────────────────────────────
//
// Compatibility is one-way on purpose, and both halves matter:
//
//   * a BoulderX sheet stores one depth PER NODE, which boulder's single-scalar
//     format cannot hold. Reading one in boulder mode would hand back a flat slab
//     that still collides — the "it stripped the Z" bug, silently. boulder must
//     therefore REFUSE it, and say why;
//   * a legacy boulder sheet is still valid input for BoulderX, upgraded to the
//     uniform slab the file actually describes, so existing sheets and every
//     other tool that writes v1 keep working.
static void test_swdm_dialects() {
    std::printf("[12] .swdm dialects: v1 loads in both, v2 only in BoulderX\n");

    // A BoulderX sheet: non-uniform per-node depths, the thing v1 cannot store.
    boulderx::SwdmDocument doc;
    doc.identifier = "gate_probe";
    boulderx::append_rect(doc.outline, 0, 0, 200, 100, 0.0, 0.0);
    doc.outline[0].front_depth = 30.0;
    doc.outline[0].back_depth = -10.0;
    doc.outline[2].front_depth = 90.0;
    doc.outline[2].back_depth = -70.0;
    doc.params.mesh_type = 1;
    const std::string v2 = boulderx::serialize_swdm(doc);

    check(boulderx::swdm_is_boulderx(v2), "BoulderX marks its own sheets as v2");
    check(boulder::is_boulderx_swdm(v2), "and boulder sees the same marker");

    boulderx::SwdmDocument back;
    check(boulderx::parse_swdm(v2, back), "BoulderX reads its own v2 sheet");
    check(!back.from_boulder_dialect, "recognised as the v2 dialect");
    check_near(back.outline[2].front_depth, 90.0, 1e-6, "per-node front depth survives v2");
    check_near(back.outline[2].back_depth, -70.0, 1e-6, "per-node back depth survives v2");

    // boulder refuses it rather than flattening those depths away.
    boulder::clear_parse_error();
    const boulder::GroundMesh refused = boulder::parse_ground_mesh(v2);
    check(refused.polygon.empty(), "boulder refuses a v2 sheet instead of flattening it");
    check(!boulder::last_parse_error().empty(), "and records why");
    check(boulder::last_parse_error().find("BoulderX") != std::string::npos,
          "the reason names the generator to switch to");
    check(boulder::generate_ground_mesh_object(v2, "gate_probe", 0.0).empty(),
          "so no scene object is produced from it either");

    // A legacy v1 sheet, exactly as boulder writes one. Its per-node entries are
    // bare `x y` and the depth is a single scalar pair.
    boulder::GroundMesh legacy;
    legacy.polygon = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
    legacy.min_depth = -25.0;
    legacy.max_depth = 40.0;
    legacy.top_texture = "fire_grass";
    legacy.bottom_texture = "graveyard_ground";
    const std::string v1 = boulder::serialize_swdm(legacy);
    check(!boulderx::swdm_is_boulderx(v1), "a boulder sheet is not marked v2");
    check(!boulder::parse_ground_mesh(v1).polygon.empty(), "boulder still reads its own sheet");

    boulderx::SwdmDocument upgraded;
    check(boulderx::parse_swdm(v1, upgraded), "BoulderX reads a legacy boulder sheet");
    check(upgraded.from_boulder_dialect, "and reports which dialect it read");
    check(upgraded.outline.size() == 4, "the legacy ring survives");
    check_near(upgraded.outline[1].front_depth, 40.0, 1e-6, "MaxDepth became the per-node front");
    check_near(upgraded.outline[1].back_depth, -25.0, 1e-6, "MinDepth became the per-node back");
    check(upgraded.params.surface_texture == "fire_grass", "TopTexture maps to the surface texture");
    check(upgraded.params.front_texture == "graveyard_ground", "BottomTexture maps to the front texture");
    check(upgraded.hats.empty(), "boulder's empty Hat[ block is not mistaken for a dome");

    // And the upgrade is usable end to end.
    check(!boulderx::generate_ground_mesh_object_swdm(v1, "upgraded").empty(),
          "a legacy sheet generates through BoulderX");

    // A v1 sheet that really does carry a dome keeps it: the block is read as
    // geometry, the sheet generates, and the dome survives a write-back.
    std::string with_hat = v1;
    const size_t hat = with_hat.find("Hat[\n");
    if (hat != std::string::npos) with_hat.insert(hat + 5, "10 20 60 40\n");
    boulderx::SwdmDocument hat_doc;
    check(boulderx::parse_swdm(with_hat, hat_doc) && hat_doc.hats.size() == 1,
          "a real Hat[ entry comes back as geometry, not as a flag");
    check_near(hat_doc.hats[0].x, 10.0, 1e-6, "carrying the hat's centre X");
    check_near(hat_doc.hats[0].radius, 60.0, 1e-6, "its radius");
    check(!boulderx::generate_ground_mesh_object_swdm(with_hat, "hat").empty(),
          "and the sheet generates with its dome instead of being refused");

    boulderx::SwdmDocument hat_rt;
    const std::string hat_v2 = boulderx::serialize_swdm(hat_doc);
    check(boulderx::parse_swdm(hat_v2, hat_rt) && hat_rt.hats.size() == 1,
          "the hat survives a v2 write/read round trip");
    check_near(hat_rt.hats[0].height, 40.0, 1e-6, "height included");
    // The hatless sheets stay byte-identical: the block is written only when a
    // hat exists, so nothing that used to parse this format changes shape.
    check(boulderx::serialize_swdm(back).find("Hat[") == std::string::npos,
          "a hatless sheet still writes no Hat[ block");
}

// ── 13. Recovering per-node Z out of a baked mesh ──────────────────────────
//
// The engine bakes one vertex per outline node at that node's exact XY, so a
// shipped mesh IS the per-node depth array. This is what keeps an edit from
// replacing vanilla relief with a uniform slab: the editors import through it.
static void test_depth_recovery() {
    std::printf("[13] per-node depth recovered from a baked mesh\n");

    std::vector<boulderx::Node> outline = {
        {0, 0, 30.0, -18.0}, {120, 0, 55.0, -40.0}, {120, 90, 12.5, -9.0},
        {0, 90, 70.0, -3.0}};

    // What the engine would have baked: a front-plane and a back-plane vertex per
    // node at the node's XY, plus an inset rim vertex that must NOT match (the
    // rim's bevel and inner wall sit inside the outline, not on it).
    std::vector<float> positions;
    for (const auto& n : outline) {
        positions.push_back(float(n.x));
        positions.push_back(float(n.y));
        positions.push_back(float(n.front_depth));
    }
    for (const auto& n : outline) {
        positions.push_back(float(n.x));
        positions.push_back(float(n.y));
        positions.push_back(float(n.back_depth));
    }
    positions.push_back(25.0f);   // inset rim profile, 25 units inside the ring
    positions.push_back(30.0f);
    positions.push_back(85.0f);

    std::vector<double> front, back;
    check(boulderx::recover_node_depths(outline, positions, 0.5, front, back),
          "recovers depths from baked vertex positions");
    check(front.size() == outline.size() && back.size() == outline.size(),
          "one depth pair per node");
    check_near(front[1], 55.0, 1e-4, "front depth comes back");
    check_near(back[1], -40.0, 1e-4, "back depth comes back");
    check_near(front[2], 12.5, 1e-4, "a thinner node keeps its own depth");
    check_near(back[3], -3.0, 1e-4, "the thinnest node is not clamped to the mesh's max");

    // With nothing near the ring there is nothing to recover, and saying so is
    // what stops a caller from inventing relief.
    const std::vector<float> elsewhere = {900, 900, 40, 950, 950, -40, 990, 990, 10};
    std::vector<double> f2, b2;
    check(!boulderx::recover_node_depths(outline, elsewhere, 0.5, f2, b2),
          "a mesh that does not match the outline recovers nothing");

    // A node with no match keeps the recovered range instead of gaining a
    // zero-depth spike.
    std::vector<float> partial;
    for (size_t i = 0; i + 1 < outline.size(); ++i) {
        partial.push_back(float(outline[i].x));
        partial.push_back(float(outline[i].y));
        partial.push_back(float(outline[i].front_depth));
        partial.push_back(float(outline[i].x));
        partial.push_back(float(outline[i].y));
        partial.push_back(float(outline[i].back_depth));
    }
    std::vector<double> f3, b3;
    check(boulderx::recover_node_depths(outline, partial, 0.5, f3, b3),
          "a partially matching mesh still recovers");
    // 55, not 70: the fallback range is taken from the nodes that DID match, so a
    // node with no geometry behind it keeps the sheet's thickness rather than
    // borrowing a depth from a neighbour it was never paired with.
    check_near(f3[3], 55.0, 1e-4, "the unmatched node falls back to the matched front max");
    check(b3[3] <= 0.0, "and never lands on the front side");

    // The recovered depths must survive an edit: generate with noise zeroed (the
    // bake already contains the object's HorizNoise) and the mesh comes back with
    // the same per-node Z rather than one flat value.
    boulderx::SwdmDocument doc;
    doc.params.mesh_type = 0;
    doc.params.horiz_noise = 0.0;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;
    boulderx::Mesh mesh;
    check(boulderx::generate(outline, doc.params, mesh), "the recovered outline generates");
    check(distinct_positive_z(mesh) == 4, "every recovered depth survives generation");

    // The plain plane is what comes back, not the extreme one. A MeshType 1 rim
    // rings its node with vertices at `plain +- HatWidthOffset1`, so taking the
    // highest/lowest z on the column (what this function used to do) returns the
    // RIM's plane, and build_rim then adds HatWidthOffset1 to it a second time.
    // The stack below is one ribbon node's: ribbon +-30, rim +-35, bevel +-40.
    std::vector<double> rf, rb;
    std::vector<boulderx::Node> one_node = {
        {0, 0, 30.0, -30.0}, {100, 0, 30.0, -30.0}, {0, 100, 30.0, -30.0}};
    std::vector<float> three_nodes;
    for (const auto& n : one_node) {
        for (double z : {40.0, 30.0, 35.0}) {
            three_nodes.push_back(float(n.x));
            three_nodes.push_back(float(n.y));
            three_nodes.push_back(float(z));
        }
        for (double z : {-40.0, -30.0, -35.0}) {
            three_nodes.push_back(float(n.x));
            three_nodes.push_back(float(n.y));
            three_nodes.push_back(float(z));
        }
    }
    check(boulderx::recover_node_depths(one_node, three_nodes, 0.5, rf, rb),
          "a rimmed column still recovers");
    check_near(rf[0], 30.0, 1e-4, "recovery returns the plain plane, not the rim's");
    check_near(rb[0], -30.0, 1e-4, "…on the back side too");
}

// ── 14. Dome hats ──────────────────────────────────────────────────────────
//
// A hat is boulder's geometry, not the engine's: `.scratch/ground_dome_probe.py`
// measures all 2,538 ground objects in the 202 decoded scenes and finds no dome
// anywhere (every out-of-footprint vertex is exactly 3.000 from a convex node —
// the rim's corner shift). So there is nothing to pin against a shipped scene
// here; what there IS to pin is that boulderx reproduces boulder's dome, because
// that is what a modder's sheet says the object should look like.
//
// The contract, from src/tools/boulder.cpp's generate_hat_mesh: a footprint
// circle of `radius` at (x, y), a base 0.05 above the polygon's own top edge (the
// anti-z-fighting lift), rising `height`, spanning the sheet's whole depth.
static void test_dome_hats() {
    std::printf("[14] dome hats: boulder's geometry, emitted not refused\n");

    boulderx::SwdmDocument doc;
    boulderx::append_rect(doc.outline, 0, 0, 200, 100, 30.0, -20.0);
    doc.params.mesh_type = 1;
    doc.params.horiz_noise = 0.0;
    doc.params.depth_source = boulderx::Params::DepthSource::Outline;
    doc.params.surface_texture = "hat_surface";
    doc.params.front_texture = "hat_front";
    doc.params.texture_scale = 250.0;

    const std::vector<boulderx::Hat> hats = {
        {50.0, 80.0, 30.0, 40.0},    // a real dome
        {150.0, 80.0, 0.0, 40.0},    // no footprint: must contribute nothing
    };

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh, &hats), "generates with hats");
    // rim + ribbon + one dome (the zero-radius hat is skipped, not emitted as a
    // degenerate spike).
    check(mesh.surfaces.size() == 3, "MT1 + one hat = rim, ribbon, dome");
    check(mesh.fronts.size() == 1, "the cap is unaffected");
    check(mesh.surfaces[2].texture == "hat_surface", "the dome is walkable surface");
    check(mesh.surfaces[2].indexed, "the dome is an indexed SurfaceMesh");

    const boulderx::Submesh& dome = mesh.surfaces[2];
    const int N = 18;   // cross-section segments, as boulder sweeps
    check(dome.vertices.size() == size_t(2 * (N + 1) + 2),
          "two vertices per profile sample plus the two cap centres");
    check(dome.indices.size() / 3 == size_t(2 * N + 2 * N),
          "a ridge quad per segment plus two N-gon fans");

    double lo_x = 1e9, hi_x = -1e9, lo_y = 1e9, hi_y = -1e9, lo_z = 1e9, hi_z = -1e9;
    for (const auto& v : dome.vertices) {
        lo_x = std::min(lo_x, (double)v.x); hi_x = std::max(hi_x, (double)v.x);
        lo_y = std::min(lo_y, (double)v.y); hi_y = std::max(hi_y, (double)v.y);
        lo_z = std::min(lo_z, (double)v.z); hi_z = std::max(hi_z, (double)v.z);
    }
    // The polygon's own top edge is y = 100, so the base sits at 100.05 and the
    // apex at 140.05 — it really does stand ON the surface and rise `height`.
    check_near(lo_y, 100.05, 1e-4, "the dome base is the polygon's top edge (+0.05)");
    check_near(hi_y, 140.05, 1e-4, "and the apex is exactly `height` above it");
    check_near(hi_y - lo_y, 40.0, 1e-4, "so the silhouette height is the authored height");
    check_near(lo_x, 20.0, 1e-4, "footprint left edge = centre - radius");
    check_near(hi_x, 80.0, 1e-4, "footprint right edge = centre + radius");
    check_near(hi_x - lo_x, 60.0, 1e-4, "so the footprint really is 2*radius wide");
    // The sheet's depth: every node is 30/-20 here, so the dome spans it exactly
    // rather than carrying a thickness of its own.
    check_near(lo_z, -20.0, 1e-4, "the dome spans the sheet's back depth");
    check_near(hi_z, 30.0, 1e-4, "and its front depth");

    // Per-node relief must carry into the dome as well, or a hat on a noisy
    // ledge would be a flat plate sunk into it.
    std::vector<boulderx::Node> varied = doc.outline;
    for (size_t i = 0; i < varied.size(); ++i) {
        varied[i].front_depth = 30.0 + 10.0 * double(i);
        varied[i].back_depth = -20.0 - 5.0 * double(i);
    }
    boulderx::Mesh varied_mesh;
    check(boulderx::generate(varied, doc.params, varied_mesh, &hats), "generates on a noisy sheet");
    lo_z = 1e9; hi_z = -1e9;
    for (const auto& v : varied_mesh.surfaces[2].vertices) {
        lo_z = std::min(lo_z, (double)v.z); hi_z = std::max(hi_z, (double)v.z);
    }
    check_near(lo_z, -35.0, 1e-4, "the dome reaches the sheet's deepest back node");
    check_near(hi_z, 60.0, 1e-4, "and its shallowest front node");

    // Independence: the dome is its own submesh with its own 0-based indices, so
    // adding one cannot renumber the rim or the ribbon.
    boulderx::Mesh no_hats;
    check(boulderx::generate(doc.outline, doc.params, no_hats), "generates without hats");
    check(no_hats.surfaces.size() == 2, "no hat means no third SurfaceMesh");
    bool rim_unchanged = no_hats.surfaces[0].indices == mesh.surfaces[0].indices &&
                         no_hats.surfaces[1].indices == mesh.surfaces[1].indices;
    check(rim_unchanged, "a hat does not renumber the existing submeshes");

    // The object's LocalAabb has to contain the dome: it rises above the polygon
    // bounds, and an Aabb that stops at the polygon can get the dome culled.
    // The Aabb is encoded as (x, y, w, h), so with the dome this object's height
    // is 150.05 = -10 (the polygon's outset bottom) .. 140.05 (the apex).
    const std::string bin = boulderx::generate_ground_mesh_object(
        doc.outline, doc.params, "dome_obj", nullptr, &hats);
    check(!bin.empty(), "the object emits with a dome");
    const float want_h = 150.05f;
    const std::string h_bytes(reinterpret_cast<const char*>(&want_h), 4);
    check(bin.find(h_bytes) != std::string::npos,
          "the emitted Aabb height reaches the dome's apex");

    const std::string bin_no_hat = boulderx::generate_ground_mesh_object(
        doc.outline, doc.params, "dome_obj", nullptr, nullptr);
    check(bin_no_hat.find(h_bytes) == std::string::npos,
          "and a hatless object does not borrow a hat's Aabb");

    // Determinism: the same sheet and hats give the same mesh.
    boulderx::Mesh again;
    check(boulderx::generate(doc.outline, doc.params, again, &hats), "regenerates");
    check(again.surfaces[2].vertices.size() == dome.vertices.size() &&
          again.surfaces[2].vertices.back().z == dome.vertices.back().z,
          "the same hats reproduce the same dome");
}

// ── 15. The depth law, and which one runs by default ───────────────────────
//
// GenerateMesh does not read a per-node depth from anywhere. It builds the array
// from the generator's own parameters — `(frand() - 0.5) * HorizNoise +
// SurfaceWidth/2` for the front plane, and the mirror for the back — with two
// draws per node off one shared sequence. That law is what every other part of
// the generator measures from (the ribbon and the cap sit on those planes, and
// build_rim's bevel and inner wall are offsets from them), so it is the default.
//
// The values below are not invented: they are the first eight draws of that law
// on seed 1291618994, SurfaceWidth 200, HorizNoise 40, and the same sequence is
// what reproduces the shipped bakes (see tests/ground_mesh_compare_test.cpp,
// which diffs a regenerated mesh against a real cave object).
static void test_depth_law() {
    std::printf("[15] the engine depth law, and the outline-driven alternative\n");

    // An octagon, so there is real area — a collinear outline has none and is
    // refused before either law is reached.
    std::vector<boulderx::Node> outline;
    for (int i = 0; i < 8; ++i) {
        const double a = i * 3.14159265358979 / 4.0;
        // Authored depths that are deliberately nothing like the law's, so a
        // generator that ignores `depth_source` cannot pass by accident.
        outline.push_back({200.0 * std::cos(a), 200.0 * std::sin(a), 5.0, -5.0});
    }
    boulderx::Params params;
    params.mesh_type = 0;
    params.surface_width = 200.0;
    params.horiz_noise = 40.0;
    params.random_seed = 1291618994u;

    std::vector<double> front, back;
    boulderx::engine_node_depths(params, outline.size(), front, back);
    check(front.size() == outline.size() && back.size() == outline.size(),
          "the law gives one depth pair per node");
    double law_hi = 0.0, law_lo = 0.0;
    for (size_t i = 0; i < outline.size(); ++i) {
        // half = 100, noise/2 = 20, so every plane is inside [80, 120] / [-120, -80]
        // and never on the authored +-5.
        check(front[i] > 79.9 && front[i] < 120.1, "the law's front plane sits on SurfaceWidth/2");
        check(back[i] < -79.9 && back[i] > -120.1, "…and its back plane mirrors it");
        law_hi = std::max(law_hi, front[i]);
        law_lo = std::min(law_lo, back[i]);
    }
    check(law_hi > 100.0, "the law's relief is genuinely per node, not one value");

    boulderx::Mesh engine_mesh;
    params.depth_source = boulderx::Params::DepthSource::Engine;
    check(boulderx::generate(outline, params, engine_mesh), "the law generates");
    // MeshType 0 is the ribbon and the cap alone, and both sit on the plain
    // planes, so the mesh's envelope must be exactly the law's. max_abs_z is
    // over both planes, so the expectation is the larger of the two.
    check_near(engine_mesh.max_abs_z(), std::max(law_hi, -law_lo), 1e-3,
               "the engine law ignores the outline's authored depths");
    double lo_z = 1e9;
    for (const auto& sm : engine_mesh.surfaces)
        for (const auto& v : sm.vertices) lo_z = std::min(lo_z, (double)v.z);
    check_near(lo_z, law_lo, 1e-3, "and reproduces the law's back plane too");

    boulderx::Mesh outline_mesh;
    params.depth_source = boulderx::Params::DepthSource::Outline;
    params.horiz_noise = 0.0;   // the outline carries the jitter already
    check(boulderx::generate(outline, params, outline_mesh), "the outline law generates");
    check_near(outline_mesh.max_abs_z(), 5.0, 1e-3,
               "the outline law honours them instead");

    boulderx::Params fresh;
    check(fresh.depth_source == boulderx::Params::DepthSource::Engine,
          "Engine is the shipped default");
}

// ── The flat-Z ship contract (doc 13 §9) ─────────────────────────────────
// One constant plane per object, |front| == |back| for every node, no PRNG draw
// reaching it, and the declared depth resolving back to that same plane so an
// edit cannot walk the declaration outwards.
static void test_flat_z_ship_contract() {
    std::printf("[16] the flat-Z ship contract (#will be unlocked after)\n");
    check(!boulderx::kPerNodeReliefUnlocked,
          "per-node relief ships staged off (#will be unlocked after)");

    boulderx::Params params;
    params.mesh_type = 1;
    params.surface_width = 180.0;
    params.horiz_noise = 60.0;
    params.random_seed = 0u;            // seed 0 is what the caves carry
    params.hat_width_offset_1 = 5.0;
    params.hat_height = 25.0;

    std::vector<double> front, back;
    boulderx::flat_node_depths(params, 24, front, back);
    bool flat = front.size() == 24 && back.size() == 24;
    for (size_t i = 0; flat && i < front.size(); ++i)
        flat = front[i] == 90.0 && back[i] == -90.0;
    check(flat, "every node lands on SurfaceWidth/2, both sides, no jitter");

    // Neither seed nor HorizNoise may reach the plane: a second draw with a
    // different seed and no noise is the identical array.
    boulderx::Params other = params;
    other.random_seed = 123456u;
    other.horiz_noise = 0.0;
    std::vector<double> f2, b2;
    boulderx::flat_node_depths(other, 24, f2, b2);
    check(f2 == front && b2 == back, "the plane does not depend on seed or noise");

    // MeshType 1 declares plane + W1; the declared rule reads that back to the
    // same plane, so regenerating cannot grow the object.
    check_near(boulderx::flat_plane_declared(95.0, 1, 5.0), 90.0, 1e-9,
               "a MeshType 1 declaration resolves back to its plane");
    check_near(boulderx::flat_plane_declared(90.0, 0, 5.0), 90.0, 1e-9,
               "a MeshType 0 declaration is the plane already");
    check(boulderx::flat_plane_declared(3.0, 1, 5.0) == 0.0,
          "a declaration with no room for the rim does not invert the slab");

    // Generated from flat planes, the cap is PLANAR: the ear order we have not
    // recovered (doc 13 §1.3.1) then cannot show up as a corrupt cap.
    std::vector<boulderx::Node> outline;
    for (int i = 0; i < 24; ++i) {
        const double a = 2.0 * 3.14159265358979 * i / 24.0;
        outline.push_back({200.0 * std::cos(a), 200.0 * std::sin(a),
                           front[size_t(i)], back[size_t(i)]});
    }
    params.depth_source = boulderx::Params::DepthSource::Outline;
    params.horiz_noise = 0.0;
    boulderx::Mesh mesh;
    check(boulderx::generate(outline, params, mesh), "a flat outline generates");
    bool cap_flat = !mesh.fronts.empty();
    for (const auto& sm : mesh.fronts)
        for (const auto& v : sm.vertices) cap_flat = cap_flat && v.z == 90.0f;
    check(cap_flat, "the cap sits planar on the plane");
    // The engine's rim profile peaks at plane + W1 + W2 (the bevel vertex of
    // emit_ring): 90 + 5 + 5 = 100 here, symmetrically -100. One more W1 —
    // 105 — would mean the DECLARED depth was fed in as the plane, which is
    // the "it over-increases the Z" report this path exists to make impossible.
    const double rim_peak = 90.0 + params.hat_width_offset_1 + params.hat_width_offset_2;
    check_near(mesh.max_abs_z(), rim_peak, 1e-6,
               "MeshType 1's rim peaks at plane + W1 + W2 exactly, not a fourth W1 deep");
}

int main() {
    std::printf("========================================\n");
    std::printf("  boulderx — Caver-faithful ground generator\n");
    std::printf("========================================\n");
    test_concave_triangulation();
    test_vanilla_concave_outline();
    test_arc_outline();
    test_per_node_depth();
    test_horiz_noise();
    test_mesh_types();
    test_swdm_roundtrip();
    test_binary_object();
    test_generator_choice();
    test_node_depth_preserved();
    test_relief_randomiser();
    test_swdm_dialects();
    test_depth_recovery();
    test_dome_hats();
    test_depth_law();
    test_flat_z_ship_contract();
    if (g_failures) {
        std::printf("FAIL: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("PASS: boulderx behaviour suite\n");
    return 0;
}
