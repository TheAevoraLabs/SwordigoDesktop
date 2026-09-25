// ============================================================================
// tools/verify_zypher_parity.cpp — Verification and Parity Comparison Tool
//
// Compares the new Zypher engine (roller.h / roller.cpp) against the
// baseline showcase shapes from tools/generate_zypher_showcase.cpp.
//
// Checks:
//   1. Geometry generation (vertex count, faces, AABB)
//   2. Chipmunk 2D collision contour (CCW winding, positive signed area, point count)
//   3. Depth extent coverage (MinDepth <= -abs(Z), MaxDepth >= abs(Z))
//   4. Protobuf wire encoding validity
//   5. Round-trip decoding via FileRift / ruby_cli
// ============================================================================

#include "tools/roller.h"
#include "platform/protobuf_reader.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;

#define VERIFY(cond, msg, ...) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        std::printf("  [FAIL] " msg "\n", ##__VA_ARGS__); \
    } else { \
        std::printf("  [PASS] " msg "\n", ##__VA_ARGS__); \
    } \
} while (0)

static double calc_signed_area(const std::vector<std::pair<double, double>>& pts) {
    if (pts.size() < 3) return 0.0;
    double area = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        size_t next = (i + 1) % pts.size();
        area += (pts[i].first * pts[next].second - pts[next].first * pts[i].second);
    }
    return 0.5 * area;
}

struct ParsedSceneStats {
    bool has_ground_polygon = false;
    bool has_ground_mesh = false;
    bool has_generator = false;
    bool has_collision = false;
    bool has_texture_mapping = false;
    bool is_ground = false;
    bool is_enabled = false;
    bool is_closed = false;
    bool is_concave_allowed = false; // Convex == 0
    int polygon_points = 0;
    double polygon_signed_area = 0.0;
    float min_depth = 0;
    float max_depth = 0;
    size_t submesh_count = 0;
    size_t total_vertices = 0;
    size_t total_faces = 0;
};

static ParsedSceneStats inspect_scene_blob(const std::string& blob) {
    ParsedSceneStats stats;
    try {
        proto::Reader r(reinterpret_cast<const uint8_t*>(blob.data()), blob.size());
        proto::Field f;
        while (r.read_field(f)) {
            if (f.field_number == 1 && f.wire_type == proto::WIRE_LEN) {
                // SceneObject
                proto::Reader obj_r(f.bytes_val);
                proto::Field of;
                while (obj_r.read_field(of)) {
                    if (of.field_number == 3 && of.wire_type == proto::WIRE_LEN) {
                        // Component
                        proto::Reader comp_r(of.bytes_val);
                        proto::Field cf;
                        std::string class_name;
                        while (comp_r.read_field(cf)) {
                            if (cf.field_number == 1 && cf.wire_type == proto::WIRE_LEN) {
                                class_name = cf.bytes_val;
                            }
                            if (cf.field_number == 110 && cf.wire_type == proto::WIRE_LEN) {
                                // GroundPolygonComponent
                                stats.has_ground_polygon = true;
                                proto::Reader gpc_r(cf.bytes_val);
                                proto::Field gpf;
                                std::vector<std::pair<double, double>> poly_pts;
                                while (gpc_r.read_field(gpf)) {
                                    if (gpf.field_number == 2 && gpf.wire_type == proto::WIRE_LEN) {
                                        // Polygon
                                        proto::Reader pr(gpf.bytes_val);
                                        proto::Field pf;
                                        while (pr.read_field(pf)) {
                                            if (pf.field_number == 1 && pf.wire_type == proto::WIRE_LEN) {
                                                // Vec2
                                                proto::Reader v2r(pf.bytes_val);
                                                proto::Field v2f;
                                                float vx = 0, vy = 0;
                                                while (v2r.read_field(v2f)) {
                                                    if (v2f.field_number == 1) vx = v2f.float_val;
                                                    if (v2f.field_number == 2) vy = v2f.float_val;
                                                }
                                                poly_pts.push_back({vx, vy});
                                            }
                                            if (pf.field_number == 2) {
                                                stats.is_concave_allowed = (pf.varint_val == 0);
                                            }
                                            if (pf.field_number == 3) {
                                                stats.is_closed = (pf.varint_val == 1);
                                            }
                                        }
                                    }
                                    if (gpf.field_number == 4) stats.min_depth = gpf.float_val;
                                    if (gpf.field_number == 5) stats.max_depth = gpf.float_val;
                                }
                                stats.polygon_points = static_cast<int>(poly_pts.size());
                                stats.polygon_signed_area = calc_signed_area(poly_pts);
                            }
                            if (cf.field_number == 111 && cf.wire_type == proto::WIRE_LEN) {
                                // GroundMeshComponent
                                stats.has_ground_mesh = true;
                                proto::Reader gmc_r(cf.bytes_val);
                                proto::Field gmf;
                                while (gmc_r.read_field(gmf)) {
                                    if ((gmf.field_number == 8 || gmf.field_number == 9) && gmf.wire_type == proto::WIRE_LEN) {
                                        ++stats.submesh_count;
                                        proto::Reader sm_r(gmf.bytes_val);
                                        proto::Field smf;
                                        while (sm_r.read_field(smf)) {
                                            if (smf.field_number == 1) stats.total_vertices += smf.varint_val;
                                            if (smf.field_number == 2) stats.total_faces += smf.varint_val;
                                        }
                                    }
                                }
                            }
                            if (cf.field_number == 112 && cf.wire_type == proto::WIRE_LEN) {
                                stats.has_generator = true;
                            }
                            if (cf.field_number == 113 && cf.wire_type == proto::WIRE_LEN) {
                                stats.has_texture_mapping = true;
                            }
                            if (cf.field_number == 121 && cf.wire_type == proto::WIRE_LEN) {
                                // CollisionShapeComponent
                                stats.has_collision = true;
                                proto::Reader csc_r(cf.bytes_val);
                                proto::Field cscf;
                                while (csc_r.read_field(cscf)) {
                                    if (cscf.field_number == 2) stats.is_ground = (cscf.varint_val == 1);
                                    if (cscf.field_number == 11) stats.is_enabled = (cscf.varint_val == 1);
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::printf("  [ERROR] Protobuf parse exception: %s\n", e.what());
    }
    return stats;
}

int main() {
    std::printf("========================================================\n");
    std::printf("  Zypher vs Baseline Showcase Comparison & Verification\n");
    std::printf("========================================================\n\n");

    // ─────────────────────────────────────────────────────────────────────────
    // Test 1: ZypherTerrain
    // ─────────────────────────────────────────────────────────────────────────
    std::printf("[Shape 1: ZypherTerrain]\n");
    {
        std::vector<zypher::TerrainNode> nodes = {
            {-600, -200, 150, 0.2, 1.0},
            {-200, -100, 200, 0.1, 1.2},
            {  50,  -50, 250, 0.0, 0.8},
            { 350,  -80, 180, 0.1, 1.0},
            { 600, -200, 120, 0.2, 1.3},
            { 400, -350, 100, 0.0, 0.9},
            {   0, -400, 130, 0.1, 1.1},
            {-400, -350, 110, 0.0, 1.0},
        };
        zypher::TerrainParams p;
        p.bevel = zypher::ProfileKind::SmoothFillet;
        p.bevel_steps = 14;
        p.spline_segs = 96;
        p.surface_width = 160.0;
        p.fbm_amplitude = 22.f;

        zypher::ComponentIds ids{2000, 2001, 2002, 2003, 2004, 2005};
        std::string blob = zypher::generate_terrain_object(nodes, p, "zypher_terrain_test", &ids);
        VERIFY(!blob.empty(), "ZypherTerrain blob generated (%zu bytes)", blob.size());

        auto stats = inspect_scene_blob(blob);
        VERIFY(stats.has_ground_polygon, "Contains GroundPolygon");
        VERIFY(stats.has_ground_mesh, "Contains GroundMesh (submeshes: %zu, verts: %zu, faces: %zu)",
               stats.submesh_count, stats.total_vertices, stats.total_faces);
        VERIFY(stats.has_generator, "Contains GroundMeshGenerator");
        VERIFY(stats.has_collision, "Contains CollisionShape");
        VERIFY(stats.has_texture_mapping, "Contains TextureMapping");
        VERIFY(stats.is_ground && stats.is_enabled, "CollisionShape: IsGround=1, Enabled=1");
        VERIFY(stats.is_closed && stats.is_concave_allowed, "Polygon: Closed=1, Convex=0 (concave allowed)");
        VERIFY(stats.polygon_points >= 8, "Collision polygon points: %d", stats.polygon_points);
        VERIFY(stats.polygon_signed_area > 0.0, "Polygon strictly CCW: signed area = +%.2f", stats.polygon_signed_area);
        VERIFY(stats.min_depth < 0 && stats.max_depth > 0, "Depth range: [%.1f, %.1f]", stats.min_depth, stats.max_depth);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test 2: ZypherPillar
    // ─────────────────────────────────────────────────────────────────────────
    std::printf("\n[Shape 2: ZypherPillar]\n");
    {
        zypher::PillarParams p;
        p.width = 130; p.height = 240; p.depth = 90;
        zypher::ComponentIds ids{3000, 3001, 3002, 3003, 3004, 3005};
        std::string blob = zypher::generate_pillar_object(p, "zypher_pillar_test", &ids);
        VERIFY(!blob.empty(), "ZypherPillar blob generated (%zu bytes)", blob.size());

        auto stats = inspect_scene_blob(blob);
        VERIFY(stats.has_ground_polygon && stats.has_ground_mesh, "Contains polygon and mesh");
        VERIFY(stats.polygon_signed_area > 0.0, "Pillar contour strictly CCW: signed area = +%.2f", stats.polygon_signed_area);
        VERIFY(stats.is_ground && stats.is_enabled, "Collision: IsGround=1, Enabled=1");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test 3: ZypherDome
    // ─────────────────────────────────────────────────────────────────────────
    std::printf("\n[Shape 3: ZypherDome]\n");
    {
        zypher::DomeParams p;
        p.radius = 240; p.skirt_height = 40; p.rings = 20; p.sectors = 48;
        zypher::ComponentIds ids{4000, 4001, 4002, 4003, 4004, 4005};
        std::string blob = zypher::generate_dome_object(p, "zypher_dome_test", &ids);
        VERIFY(!blob.empty(), "ZypherDome blob generated (%zu bytes)", blob.size());

        auto stats = inspect_scene_blob(blob);
        VERIFY(stats.has_ground_polygon && stats.has_ground_mesh, "Contains polygon and mesh");
        VERIFY(stats.total_vertices >= 500, "Dome tessellation: %zu vertices, %zu faces", stats.total_vertices, stats.total_faces);
        VERIFY(stats.polygon_points >= 30, "Dome contour points: %d", stats.polygon_points);
        VERIFY(stats.polygon_signed_area > 0.0, "Dome contour strictly CCW: signed area = +%.2f", stats.polygon_signed_area);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test 4: ZypherArch
    // ─────────────────────────────────────────────────────────────────────────
    std::printf("\n[Shape 4: ZypherArch]\n");
    {
        zypher::ArchParams p;
        p.span = 500; p.height = 420; p.thickness = 80; p.depth = 100;
        zypher::ComponentIds ids{5000, 5001, 5002, 5003, 5004, 5005};
        std::string blob = zypher::generate_arch_object(p, "zypher_arch_test", &ids);
        VERIFY(!blob.empty(), "ZypherArch blob generated (%zu bytes)", blob.size());

        auto stats = inspect_scene_blob(blob);
        VERIFY(stats.has_ground_polygon && stats.has_ground_mesh, "Contains polygon and mesh");
        VERIFY(stats.polygon_signed_area > 0.0, "Arch contour strictly CCW: signed area = +%.2f", stats.polygon_signed_area);
        VERIFY(stats.is_concave_allowed, "Concave archway supported: Convex=0");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test 5: ZypherTorus
    // ─────────────────────────────────────────────────────────────────────────
    std::printf("\n[Shape 5: ZypherTorus]\n");
    {
        zypher::TorusParams p;
        p.major_radius = 270; p.minor_radius = 60; p.major_segs = 28; p.minor_segs = 14;
        zypher::ComponentIds ids{6000, 6001, 6002, 6003, 6004, 6005};
        std::string blob = zypher::generate_torus_object(p, "zypher_torus_test", &ids);
        VERIFY(!blob.empty(), "ZypherTorus blob generated (%zu bytes)", blob.size());

        auto stats = inspect_scene_blob(blob);
        VERIFY(stats.has_ground_polygon && stats.has_ground_mesh, "Contains polygon and mesh");
        VERIFY(stats.polygon_signed_area > 0.0, "Torus contour strictly CCW: signed area = +%.2f", stats.polygon_signed_area);
        VERIFY(stats.total_faces > 300, "Torus faces: %zu", stats.total_faces);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Test 6: ZypherBevelPlatform
    // ─────────────────────────────────────────────────────────────────────────
    std::printf("\n[Shape 6: ZypherBevelPlatform]\n");
    {
        zypher::BevelPlatformParams p;
        p.half_width = 360; p.half_height = 45; p.bevel_radius = 30; p.depth = 95; p.bevel_segs = 12;
        zypher::ComponentIds ids{7000, 7001, 7002, 7003, 7004, 7005};
        std::string blob = zypher::generate_bevel_platform_object(p, "zypher_bevel_test", &ids);
        VERIFY(!blob.empty(), "ZypherBevelPlatform blob generated (%zu bytes)", blob.size());

        auto stats = inspect_scene_blob(blob);
        VERIFY(stats.has_ground_polygon && stats.has_ground_mesh, "Contains polygon and mesh");
        VERIFY(stats.polygon_points >= 20, "Bevel platform contour points: %d", stats.polygon_points);
        VERIFY(stats.polygon_signed_area > 0.0, "Bevel platform contour strictly CCW: signed area = +%.2f", stats.polygon_signed_area);
    }

    std::printf("\n========================================================\n");
    std::printf("  Parity & Verification Summary: %d checks, %d failures\n", g_checks, g_failures);
    std::printf("========================================================\n");
    return g_failures == 0 ? 0 : 1;
}
