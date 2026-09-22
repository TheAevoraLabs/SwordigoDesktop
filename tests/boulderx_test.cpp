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
    doc.params.surface_texture = "graveyard_ground";
    doc.params.front_texture = "graveyard_ground";

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "20-node concave outline generates");
    check(mesh.surfaces.size() == 1 && mesh.fronts.size() == 1, "MT0 emits one ribbon + one cap");
    check(mesh.fronts[0].vertices.size() == (xy.size() - 2) * 3,
          "cap has n-2 non-indexed triangles");
    check(mesh.surfaces[0].vertices.size() == xy.size() * 2, "ribbon has two vertices per node");
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

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "arced outline generates");
    const size_t corners = doc.outline.size();
    check(corners >= 26, "arc appended its samples");
    check(mesh.fronts[0].vertices.size() == (corners - 2) * 3, "cap tiles the arched outline");
    // The crest is genuinely 3D: cap vertices reach the crest height.
    double top_y = -1e9;
    for (const auto& v : mesh.fronts[0].vertices) top_y = std::max(top_y, (double)v.y);
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
    // The rim rolls INWARD (verified: obj12's ring steps 99.644 -> 89.644 ->
    // 74.644 in y where the outward normal points +y). Only z leaves the
    // authored planes.
    double max_x = -1e9, min_x = 1e9;
    for (const auto& v : rimmed.surfaces[0].vertices) {
        max_x = std::max(max_x, (double)v.x);
        min_x = std::min(min_x, (double)v.x);
    }
    // The rho = 0 profile points sit exactly ON the outline, so the rim's outer
    // edge reproduces the authored polygon rather than approximating it.
    check_near(max_x, 200.0, 1e-3, "rim outer edge follows the polygon edge");
    check(min_x >= 0.0 - 1e-6, "rim does not push outside the polygon");
    check(min_x < 25.0, "rim rolls in from the far edge");
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

    // Round-trip, plus totality: the id comes from a persisted config file, so
    // an unrecognised one must resolve to Boulder rather than to a third state.
    for (boulderx::GroundGenerator g : {boulderx::GroundGenerator::Boulder,
                                        boulderx::GroundGenerator::BoulderX}) {
        check(boulderx::ground_generator_from_id(boulderx::ground_generator_id(g)) == g,
              std::string("id round-trips: ") + boulderx::ground_generator_id(g));
    }
    check(boulderx::ground_generator_from_id("") == boulderx::GroundGenerator::Boulder,
          "absent id resolves to Boulder");
    check(boulderx::ground_generator_from_id("ZenithMesh") == boulderx::GroundGenerator::Boulder,
          "unknown id resolves to Boulder, not to a generator that isn't there");

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
        square, 1, 25.0, "surface_tex", "front_tex", "pick_ok", false, nullptr);
    check(!ok.empty(), "flat adapter emits a plain square");
    check(boulderx::last_decline_reason().empty(),
          "a successful generation leaves no decline reason");

    // Dome hats: refused, and the refusal says why.
    const std::string refused = boulderx::generate_ground_mesh_object_flat(
        square, 1, 25.0, "surface_tex", "front_tex", "pick_hat", true, nullptr);
    check(refused.empty(), "dome hats are refused rather than silently dropped");
    check(!boulderx::last_decline_reason().empty(), "a refusal always records a reason");
    check(boulderx::last_decline_reason().find("dome hat") != std::string::npos,
          "the reason names dome hats");

    // A later success must clear the earlier refusal, or the HUD would report a
    // fallback that did not happen on this edit.
    boulderx::generate_ground_mesh_object_flat(
        square, 1, 25.0, "surface_tex", "front_tex", "pick_ok2", false, nullptr);
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
        outline, doc.params, "relief_probe", false, nullptr);
    check(!bin.empty(), "per-node outline emits a scene object");
    check(bin.find("relief_probe") != std::string::npos,
          "the emitted object carries its identifier");

    // The per-node entry point must still decline dome hats rather than drop
    // them, exactly like the flat one.
    boulderx::clear_decline_reason();
    check(boulderx::generate_ground_mesh_object_nodes(outline, doc.params, "hat", true, nullptr)
              .empty(),
          "the per-node entry point refuses dome hats too");
    check(!boulderx::last_decline_reason().empty(), "and records why");
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
    check(!upgraded.has_dome_hats, "boulder's empty Hat[ block is not mistaken for a dome");

    // And the upgrade is usable end to end.
    check(!boulderx::generate_ground_mesh_object_swdm(v1, "upgraded").empty(),
          "a legacy sheet generates through BoulderX");

    // A v1 sheet that really does carry a dome is refused rather than emitted
    // without it.
    std::string with_hat = v1;
    const size_t hat = with_hat.find("Hat[\n");
    if (hat != std::string::npos) with_hat.insert(hat + 5, "10 20 60 40\n");
    boulderx::SwdmDocument hat_doc;
    check(boulderx::parse_swdm(with_hat, hat_doc) && hat_doc.has_dome_hats,
          "a real Hat[ entry is recorded");
    check(boulderx::generate_ground_mesh_object_swdm(with_hat, "hat").empty(),
          "and the sheet is refused while dome hats are unimplemented");
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
    if (g_failures) {
        std::printf("FAIL: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("PASS: boulderx behaviour suite\n");
    return 0;
}
