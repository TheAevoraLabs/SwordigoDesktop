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

    std::printf("[§4] Protobuf emission\n");
    test_sphere_emission();
    test_terrain_emission();

    std::printf("[§5] Generator selector\n");
    test_generator_selector();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
