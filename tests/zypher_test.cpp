// ============================================================================
// tests/zypher_test.cpp — Behavioral and regression tests for Zypher Gen 3
//
// Build: g++ -std=c++17 -O2 -I src -I src/tools \
//            tests/zypher_test.cpp src/tools/roller.cpp src/tools/boulderx.cpp \
//            src/tools/pod_loader.cpp src/tools/pod_stamp.cpp \
//            -lm -o bin/zypher_test
// Run:   ./bin/zypher_test
// ============================================================================

#include "tools/zypher_math.h"
#include "tools/roller.h"
#include "platform/protobuf_reader.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ── Minimal test harness ─────────────────────────────────────────────────────
static int g_pass = 0, g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

// ── §1. Math foundation ───────────────────────────────────────────────────────

static void test_v2_v3() {
    zypher::V2 a{3, 4};
    CHECK(std::fabs(zypher::length(a) - 5.f) < 1e-4f);
    zypher::V2 n = zypher::normalize(a);
    CHECK(std::fabs(zypher::length(n) - 1.f) < 1e-4f);

    zypher::V3 u{1,0,0}, v{0,1,0};
    zypher::V3 c = zypher::cross(u, v);
    CHECK(std::fabs(c.x) < 1e-5f && std::fabs(c.y) < 1e-5f && std::fabs(c.z - 1.f) < 1e-5f);
}

static void test_catmull_rom() {
    zypher::CRSpline sp;
    sp.pts = {{0,0,0},{1,0,0},{2,1,0},{3,0,0},{4,0,0}};

    // eval at integer t → should hit control points (centripetal — approximately)
    zypher::V3 p1 = sp.eval(1.f);
    CHECK(std::fabs(p1.x - 1.f) < 0.5f); // not exact but in range

    // Tangent at t=2 should be ~(1,0,0) (smooth hill)
    zypher::V3 tan = sp.eval_tangent(2.f);
    CHECK(std::fabs(zypher::length(tan) - 1.f) < 0.1f);
}

static void test_bishop_frame() {
    zypher::V3 tan0{1,0,0};
    zypher::BishopFrame f0 = zypher::bishop_init(tan0);
    CHECK(std::fabs(zypher::dot(f0.t, f0.r)) < 1e-4f);
    CHECK(std::fabs(zypher::dot(f0.t, f0.s)) < 1e-4f);
    CHECK(std::fabs(zypher::dot(f0.r, f0.s)) < 1e-4f);

    zypher::V3 tan1 = zypher::normalize(zypher::V3{1, 0.5f, 0});
    zypher::BishopFrame f1 = zypher::bishop_step(f0, tan1);
    // r must remain perpendicular to new tangent
    CHECK(std::fabs(zypher::dot(f1.t, f1.r)) < 1e-3f);
}

static void test_convex_hull_ccw() {
    // Square CCW
    std::vector<zypher::V2> pts = {{0,0},{1,0},{1,1},{0,1},{0.5f,0.5f}};
    auto hull = zypher::ccw_convex_hull(pts);
    CHECK(hull.size() >= 4);

    // Check positive area (CCW)
    float area = 0;
    for (size_t i = 0; i < hull.size(); ++i) {
        auto& a = hull[i];
        auto& b = hull[(i+1) % hull.size()];
        area += a.x * b.y - b.x * a.y;
    }
    CHECK(area > 0.f);
}

static void test_triangulate() {
    std::vector<zypher::V2> square = {{0,0},{1,0},{1,1},{0,1}};
    auto tris = zypher::triangulate_polygon(square);
    CHECK(tris.size() == 6); // 2 triangles, 3 indices each
}

static void test_fbm_noise() {
    float v = zypher::fbm2d(1.23f, 4.56f, 4);
    CHECK(v > -2.f && v < 2.f);

    float w = zypher::worley2d(1.f, 2.f, 0.1f);
    CHECK(w >= 0.f && w <= 1.f);
}

static void test_profiles() {
    for (auto kind : {zypher::ProfileKind::SmoothFillet,
                      zypher::ProfileKind::SedimentaryStep,
                      zypher::ProfileKind::GothicMoulding,
                      zypher::ProfileKind::MangaChamfer}) {
        auto pts = zypher::sample_profile(kind, 12);
        CHECK(pts.size() == 12u);
        CHECK(pts.front().t <= 0.01f);
        CHECK(pts.back().t  >= 0.99f);
    }
}

// ── §2. ZypherParametric ─────────────────────────────────────────────────────

static void test_sphere() {
    zypher::SphereParams p;
    p.radius = 150.0; p.rings = 8; p.sectors = 12;
    auto m = zypher::generate_sphere(p);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
    CHECK(m.triangle_count() > 0);
    CHECK(m.collision_outline.size() >= 4);
    // Hull must be CCW
    double area = 0;
    auto& h = m.collision_outline;
    for (size_t i = 0; i < h.size(); ++i) {
        size_t j = (i+1)%h.size();
        area += h[i].first*h[j].second - h[j].first*h[i].second;
    }
    CHECK(area > 0.0);
}

static void test_arch() {
    zypher::ArchParams p;
    p.span = 400; p.height = 300; p.thickness = 60; p.depth = 80; p.arch_segs = 12;
    auto m = zypher::generate_arch(p);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
    // CCW hull
    double area = 0;
    auto& h = m.collision_outline;
    for (size_t i = 0; i < h.size(); ++i) {
        size_t j = (i+1)%h.size();
        area += h[i].first*h[j].second - h[j].first*h[i].second;
    }
    CHECK(area > 0.0);
}

static void test_torus() {
    zypher::TorusParams p;
    p.major_radius = 200; p.minor_radius = 50; p.major_segs = 12; p.minor_segs = 8;
    auto m = zypher::generate_torus(p);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
}

static void test_pillar() {
    zypher::PillarParams p;
    p.width = 100; p.height = 300; p.depth = 100; p.bevel_n = 4; p.bevel_r = 8;
    auto m = zypher::generate_pillar(p);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
}

static void test_dome() {
    zypher::DomeParams p;
    p.radius = 240; p.skirt_height = 40; p.rings = 10; p.sectors = 16;
    auto m = zypher::generate_dome(p);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
    // CCW hull
    double area = 0;
    auto& h = m.collision_outline;
    for (size_t i = 0; i < h.size(); ++i) {
        size_t j = (i + 1) % h.size();
        area += h[i].first * h[j].second - h[j].first * h[i].second;
    }
    CHECK(area > 0.0);
}

static void test_bevel_platform() {
    zypher::BevelPlatformParams p;
    p.half_width = 300; p.half_height = 40; p.bevel_radius = 25; p.depth = 80;
    auto m = zypher::generate_bevel_platform(p);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
    // CCW hull
    double area = 0;
    auto& h = m.collision_outline;
    for (size_t i = 0; i < h.size(); ++i) {
        size_t j = (i + 1) % h.size();
        area += h[i].first * h[j].second - h[j].first * h[i].second;
    }
    CHECK(area > 0.0);
}

// ── §3. ZypherTerrain ─────────────────────────────────────────────────────────

static void test_terrain() {
    std::vector<zypher::TerrainNode> nodes = {
        {-300, -100, 100, 0, 1},
        { 300, -100, 100, 0, 1},
        { 300,  100, 100, 0, 1},
        {-300,  100, 100, 0, 1},
    };
    zypher::TerrainParams params;
    params.spline_segs = 20;
    params.bevel_steps = 6;
    params.fbm_amplitude = 10;
    auto m = zypher::generate_terrain(nodes, params);
    CHECK(m.ok);
    CHECK(m.vertex_count() > 0);
    CHECK(m.collision_outline.size() >= 3);
    // Hull must be CCW
    double area = 0;
    auto& h = m.collision_outline;
    for (size_t i = 0; i < h.size(); ++i) {
        size_t j = (i+1)%h.size();
        area += h[i].first*h[j].second - h[j].first*h[i].second;
    }
    CHECK(area > 0.0);
}

// A regenerated object must not grow when it is read back.
//
// The GroundMeshGenerator used to be emitted from the mesh AABB, so mesh_import
// read that back as SurfaceWidth, derived a per-node depth of half the whole
// plate, and extruded a slab that size — and every edit grew it again (measured
// on thecave_part15 risingGround: XY span 1126 -> 3434, Z span 180 -> 2369 after
// three re-imports). Emitting the generator's own inputs fixes it; this pins
// that, because a regression here is invisible until a user edits an object
// twice and is exactly the "sometimes the mesh is absurdly oversized" report.
// Pull GroundMeshGenerator field 8 (SurfaceWidth) out of an emitted scene blob:
// scene{field1=object}, object{repeated field3=component},
// component{field1="GroundMeshGenerator", field112=generator}, generator{field8}.
static bool emitted_generator_surface_width(const std::string& blob, double& out) {
    proto::Reader scene(blob);
    proto::Field f;
    while (scene.read_field(f)) {
        if (f.field_number != 1 || f.wire_type != proto::WIRE_LEN) continue;
        proto::Reader obj(f.bytes_val);
        proto::Field of;
        while (obj.read_field(of)) {
            if (of.field_number != 3 || of.wire_type != proto::WIRE_LEN) continue;
            proto::Reader comp(of.bytes_val);
            proto::Field cf;
            bool is_gen = false;
            std::string gen_bytes;
            while (comp.read_field(cf)) {
                if (cf.field_number == 1 && cf.wire_type == proto::WIRE_LEN &&
                    cf.bytes_val == "GroundMeshGenerator")
                    is_gen = true;
                else if (cf.field_number == 112 && cf.wire_type == proto::WIRE_LEN)
                    gen_bytes = cf.bytes_val;
            }
            if (!is_gen || gen_bytes.empty()) continue;
            proto::Reader gen(gen_bytes);
            proto::Field gf;
            while (gen.read_field(gf))
                if (gf.field_number == 8 && gf.wire_type == proto::WIRE_I32) {
                    out = gf.float_val;
                    return true;
                }
        }
    }
    return false;
}

static void test_terrain_roundtrip_stability() {
    std::vector<zypher::TerrainNode> nodes = {
        {-300, -100, 0, 45, -45},
        { 300, -100, 0, 45, -45},
        { 300,  100, 0, 45, -45},
        {-300,  100, 0, 45, -45},
    };
    zypher::TerrainParams p;
    p.surface_width = 90.0;
    p.hat_height = 25.0f;
    p.hat_width = 5.0f;
    p.texture_scale = 250.0f;

    // Mirror mesh_import: the per-node planes come from the generator law, so a
    // SurfaceWidth that is really the AABB is what poisons the next generate.
    auto set_depths = [&](double sw) {
        for (auto& n : nodes) { n.front_depth = sw * 0.5; n.back_depth = -sw * 0.5; }
    };
    set_depths(p.surface_width);
    auto a = zypher::generate_terrain(nodes, p);
    CHECK(a.ok);
    // The emitted parameter IS the input, not the mesh bounds.
    CHECK(std::fabs(a.gen_surface_width - 90.0) < 1e-6);
    CHECK(std::fabs(a.gen_hat_height - 25.0) < 1e-6);
    CHECK(std::fabs((double)a.tex_scale - 250.0) < 1e-6);

    // And the EMITTED object actually carries it: parse the bytes back out so a
    // regression in emit_scene_object (which writes the field) is caught too.
    {
        const std::string blob = zypher::generate_terrain_object(nodes, p, "stability", nullptr);
        CHECK(!blob.empty());
        double sw = 0;
        CHECK(emitted_generator_surface_width(blob, sw));
        CHECK(std::fabs(sw - 90.0) < 1e-4);
    }

    zypher::TerrainParams p2 = p;
    p2.surface_width = a.gen_surface_width;
    p2.hat_height = static_cast<float>(a.gen_hat_height);
    set_depths(p2.surface_width);
    auto b = zypher::generate_terrain(nodes, p2);
    CHECK(b.ok);
    CHECK(std::fabs(b.aabb_w - a.aabb_w) < 1e-3);
    CHECK(std::fabs(b.aabb_h - a.aabb_h) < 1e-3);
    CHECK(std::fabs(b.gen_surface_width - 90.0) < 1e-6);
}

// ── §3b. Selectable looks (HatStyle / TopStyle) ───────────────────────────────

static void test_top_styles() {
    std::vector<zypher::TerrainNode> nodes = {
        {-300, -100, 0, 45, -45},
        { 300, -100, 0, 45, -45},
        { 300,  100, 0, 45, -45},
        {-300,  100, 0, 45, -45},
    };

    // Every hat style produces a valid mesh; None simply emits no shoulder
    // rather than failing.
    for (auto hs : {zypher::HatStyle::Vanilla, zypher::HatStyle::Curvy,
                    zypher::HatStyle::Plain,   zypher::HatStyle::ZigZag,
                    zypher::HatStyle::None}) {
        zypher::TerrainParams p;
        p.hat_style = hs;
        auto m = zypher::generate_terrain(nodes, p);
        CHECK(m.ok);
        CHECK(m.vertex_count() > 0);
    }

    // A Plain deck is one quad per walkable edge; a patterned one subdivides.
    zypher::TerrainParams base;
    base.top_style = zypher::TopStyle::Plain;
    auto plain = zypher::generate_terrain(nodes, base);

    // REGRESSION: a Plain deck must actually emit its top quads. The row loop
    // ran `ri < rows - 1` with rows starting at 1 when spacing <= 1, so a Plain
    // deck emitted ZERO top geometry and the walkable surface was simply
    // missing — only the shoulder lip remained. With HatStyle::None the top
    // SurfaceMesh is the deck alone, so its vertex count is the cleanest
    // witness of the bug.
    {
        zypher::TerrainParams flat;
        flat.top_style = zypher::TopStyle::Plain;
        flat.hat_style = zypher::HatStyle::None;
        auto m = zypher::generate_terrain(nodes, flat);
        size_t top_verts = 0;
        for (const auto& sm : m.surfaces)
            if (sm.texture == flat.surface_tex) top_verts += sm.vertices.size();
        CHECK(top_verts >= 12);   // >= one quad per walkable edge (3 on this ring)
    }

    // REGRESSION: the shoulder must close onto the front plane. Every profile
    // used to stop at the OUTER bottom of the drop, leaving the lip's underside
    // open so the side never met the top/cliff face. The closing point returns
    // to lat == 0, i.e. z == the deck's front depth, at the full drop height.
    {
        zypher::TerrainParams closed;
        closed.top_style = zypher::TopStyle::Plain;
        closed.hat_style = zypher::HatStyle::Vanilla;
        closed.fbm_amplitude = 0.0f;
        auto m = zypher::generate_terrain(nodes, closed);
        const double fz = 45.0;   // node front_depth
        const double drop_y = 100.0 - double(closed.hat_height);  // ring top edge
        bool closes = false;
        for (const auto& sm : m.surfaces) {
            if (sm.texture != closed.surface_tex) continue;
            for (const auto& v : sm.vertices)
                if (std::fabs(double(v.z) - fz) < 0.5 && std::fabs(double(v.y) - drop_y) < 0.5)
                    closes = true;
        }
        CHECK(closes);
    }

    zypher::TerrainParams spikey = base;
    spikey.top_style = zypher::TopStyle::Spikey;
    spikey.top_frequency = 8.0f;
    spikey.top_depth = 16.0f;
    spikey.top_min_collision_radius = 8.0f;
    auto sp = zypher::generate_terrain(nodes, spikey);
    CHECK(sp.ok);
    CHECK(sp.vertex_count() > plain.vertex_count());
    // Deterministic: same inputs, same geometry.
    auto sp2 = zypher::generate_terrain(nodes, spikey);
    CHECK(sp2.vertex_count() == sp.vertex_count());
    CHECK(sp2.collision_outline.size() == sp.collision_outline.size());
    // Spikey contributes collision above the min radius...
    CHECK(sp.collision_outline.size() > plain.collision_outline.size());

    // ...but nothing when every feature is below top_min_collision_radius.
    zypher::TerrainParams tiny = spikey;
    tiny.top_min_collision_radius = 64.0f;
    auto tn = zypher::generate_terrain(nodes, tiny);
    CHECK(tn.collision_outline.size() == plain.collision_outline.size());

    // Porous never collides, even with a deep pit.
    zypher::TerrainParams porous = base;
    porous.top_style = zypher::TopStyle::Porous;
    porous.top_frequency = 8.0f;
    porous.top_depth = 40.0f;
    porous.top_min_collision_radius = 0.0f;
    auto po = zypher::generate_terrain(nodes, porous);
    CHECK(po.ok);
    CHECK(po.vertex_count() > plain.vertex_count());
    CHECK(po.collision_outline.size() == plain.collision_outline.size());

    // Bubbly adds a small pentagonal arch per feature (more points than a spike).
    zypher::TerrainParams bubbly = base;
    bubbly.top_style = zypher::TopStyle::Bubbly;
    bubbly.top_frequency = 8.0f;
    bubbly.top_depth = 20.0f;
    bubbly.top_min_collision_radius = 8.0f;
    auto bu = zypher::generate_terrain(nodes, bubbly);
    CHECK(bu.ok);
    CHECK(bu.collision_outline.size() > sp.collision_outline.size());

    // Every style id round-trips; an unknown id is total (falls back).
    for (auto hs : {zypher::HatStyle::Vanilla, zypher::HatStyle::Curvy,
                    zypher::HatStyle::Plain,   zypher::HatStyle::ZigZag,
                    zypher::HatStyle::None}) {
        CHECK(zypher::hat_style_from_id(zypher::hat_style_id(hs)) == hs);
    }
    for (auto ts : {zypher::TopStyle::Plain, zypher::TopStyle::Spikey,
                    zypher::TopStyle::Porous, zypher::TopStyle::Bubbly}) {
        CHECK(zypher::top_style_from_id(zypher::top_style_id(ts)) == ts);
    }
    CHECK(zypher::hat_style_from_id("nonsense") == zypher::HatStyle::Vanilla);
    CHECK(zypher::top_style_from_id("nonsense") == zypher::TopStyle::Plain);
}

// ── §4. Protobuf emission ──────────────────────────────────────────────────────

static void test_sphere_emission() {
    zypher::SphereParams p;
    p.radius = 200; p.rings = 6; p.sectors = 8;
    std::string blob = zypher::generate_sphere_object(p, "test_sphere", nullptr);
    CHECK(!blob.empty());
    // First bytes: field 1, wire type 2 (LEN) = tag 0x0A
    CHECK(static_cast<uint8_t>(blob[0]) == 0x0A);
}

static void test_terrain_emission() {
    std::vector<zypher::TerrainNode> nodes = {
        {-200, -100, 80, 0, 1},
        { 200, -100, 80, 0, 1},
        { 200,  100, 80, 0, 1},
        {-200,  100, 80, 0, 1},
    };
    zypher::TerrainParams params;
    params.spline_segs = 16; params.bevel_steps = 4;
    std::string blob = zypher::generate_terrain_object(nodes, params, "test_terrain", nullptr);
    CHECK(!blob.empty());
    CHECK(static_cast<uint8_t>(blob[0]) == 0x0A);
}

// ── §5. Ground generator selector ─────────────────────────────────────────────

static void test_generator_selector() {
    zypher::set_ground_generator(zypher::GroundGenerator::Zypher);
    CHECK(zypher::ground_generator() == zypher::GroundGenerator::Zypher);
    CHECK(std::string(zypher::ground_generator_id(zypher::GroundGenerator::Zypher)) == "zypher");
    auto from_id = zypher::ground_generator_from_id("boulderx");
    CHECK(from_id == zypher::GroundGenerator::BoulderX);
    auto from_zypher = zypher::ground_generator_from_id("zypher");
    CHECK(from_zypher == zypher::GroundGenerator::Zypher);
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main() {
    std::printf("=== Zypher Gen 3 Test Suite ===\n\n");

    std::printf("[§1] Math foundation\n");
    test_v2_v3();
    test_catmull_rom();
    test_bishop_frame();
    test_convex_hull_ccw();
    test_triangulate();
    test_fbm_noise();
    test_profiles();

    std::printf("[§2] Parametric primitives\n");
    test_sphere();
    test_arch();
    test_torus();
    test_pillar();
    test_dome();
    test_bevel_platform();

    std::printf("[§3] Terrain generator\n");
    test_terrain();
    test_terrain_roundtrip_stability();
    test_top_styles();

    std::printf("[§4] Protobuf emission\n");
    test_sphere_emission();
    test_terrain_emission();

    std::printf("[§5] Generator selector\n");
    test_generator_selector();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
