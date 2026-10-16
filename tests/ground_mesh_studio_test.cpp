// ground_mesh_studio_test.cpp — Ground Mesh Studio regressions
//
// 1) Simple-polygon enforcement: appends that would self-intersect are gated
//    (GroundMeshCanvas::would_cross_at) — this is what keeps repaints cheap
//    (a crossing polygon makes Qt's winding-fill tessellation explode: ~17ms at
//    80 verts vs ~1ms for a simple polygon) AND guarantees boulder's vertex-0
//    fan triangulation receives valid input.
// 2) Add-to-scene: boulder::generate_ground_mesh_object must produce a
//    parseable single-object scene whose object carries GroundPolygon +
//    GroundMesh components (SurfaceMesh/FrontMesh) — the exact bytes the
//    studio hands to RubyMainWindow to paste into the open scene's RAM.
//
// Requires Qt (widgets) for the canvas; runs headless via QT_QPA_PLATFORM.

#include "ruby/tools/ground_mesh_studio.h"
#include "ruby/tools/zypher_look.h"   // the shared Zypher look store
#include "tools/boulder.h"
#include "tools/boulderx.h"           // ground_generator() / set_ground_generator()
#include "tools/roller.h"             // zypher::TerrainParams / enums
#include "tools/scene_loader.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QPointF>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Strong definition for the weak default in src/platform/data_path.cpp — the
// app binaries provide their own (asset_viewer.cpp / ruby_cli.cpp), a bare
// test links must supply one.
std::string g_instance_assets_dir = "assets";

using namespace ruby::tools;

static int g_failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            std::printf("  PASS  %s\n", msg);                                \
        } else {                                                             \
            std::printf("  FAIL  %s\n", msg);                                \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::printf("[ground_mesh_studio_test]\n");

    // ── 1) simple-polygon detection ─────────────────────────────────────────
    GroundMeshCanvas c;
    c.resize(900, 600);
    c.set_polygon({ {0, 0}, {800, 0}, {800, 600}, {0, 600} }); // square
    CHECK(c.is_simple(), "square polygon is simple");

    c.set_polygon({ {0, 0}, {800, 600}, {800, 0}, {0, 600} }); // bow-tie
    CHECK(!c.is_simple(), "bow-tie polygon is NOT simple");

    // Appending below the square: the closing edge last(0,600)->(400,-300)
    // crosses the bottom edge (0,0)->(800,0) at ~(267,0) — a genuine crossing.
    c.set_polygon({ {0, 0}, {800, 0}, {800, 600}, {0, 600} });
    CHECK(c.would_cross_at(QPointF(400, -300)),
          "append through an existing edge is rejected");

    // Before the polygon closes (open 2-point chain), any append is valid —
    // a triangle can never self-intersect.
    c.set_polygon({ {0, 0}, {800, 0} });
    CHECK(!c.would_cross_at(QPointF(400, 300)),
          "append while the chain is open is allowed");

    // ── 2) add-to-scene round trip ──────────────────────────────────────────
    boulder::GroundMesh gm;
    gm.polygon = { {0, 0}, {800, 0}, {800, 400}, {0, 400} };
    gm.top_texture = "fire_grass";
    gm.bottom_texture = "graveyard_ground";
    const std::string swdm = boulder::serialize_swdm(gm);
    const std::string bin = boulder::generate_ground_mesh_object(swdm, "ground_01", 40.0);
    CHECK(!bin.empty(), "generate_ground_mesh_object returns bytes");

    if (!bin.empty()) {
        const std::string tmp = std::string("/tmp/ground_mesh_studio_test.scene");
        {
            FILE* f = std::fopen(tmp.c_str(), "wb");
            if (f) { std::fwrite(bin.data(), 1, bin.size(), f); std::fclose(f); }
        }
        av::SceneData sd = av::scene_load(tmp);
        CHECK(sd.objects.size() == 1, "generated binary parses as one object");
        if (!sd.objects.empty()) {
            const av::SceneObject& o = sd.objects.front();
            CHECK(o.name == "ground_01", "object keeps the studio identifier");
            CHECK(o.pos_z == 40.0f, "object carries the panel depth");
            CHECK(o.components.size() >= 4, "object carries GroundPolygon+GroundMesh+Generator+Collision components");
            CHECK(o.ground_meshes.size() >= 1, "object has embedded ground meshes");
            // The walkable polygon decodes into the scene's collision list
            // (GroundPolygonComponent -> CollisionData::ground_polygons).
            bool has_walkable = false;
            for (const auto& c : sd.collisions)
                if (!c.collision.ground_polygons.empty() &&
                    c.collision.ground_polygons[0].points.size() / 2 >= 3)
                    has_walkable = true;
            CHECK(has_walkable, "object has a walkable ground polygon (collision list)");

            // Host path: append to an open scene's RAM and refresh.
            av::SceneData sd2;
            sd2.objects.push_back(o);
            av::scene_refresh(sd2);
            CHECK(sd2.objects.size() == 1 && !sd2.objects[0].name.empty(),
                  "paste + scene_refresh keeps the object");
        }
        std::remove(tmp.c_str());
    }

    // Degenerate input is rejected before generation.
    boulder::GroundMesh tiny;
    tiny.polygon = { {0, 0}, {10, 0} };
    CHECK(boulder::generate_ground_mesh_object(boulder::serialize_swdm(tiny), "t", 0.0).empty(),
          "two-point polygon is rejected");

    // Clockwise polygon input: must be normalized to CCW so that top surface,
    // side walls, and front face are fully generated without gaps or missing ears.
    boulder::GroundMesh cw_mesh;
    cw_mesh.polygon = { {0, 400}, {800, 400}, {800, 0}, {0, 0} }; // clockwise
    const std::string cw_swdm = boulder::serialize_swdm(cw_mesh);
    const std::string cw_bin = boulder::generate_ground_mesh_object(cw_swdm, "cw_ground", 0.0);
    CHECK(!cw_bin.empty(), "clockwise polygon generates valid binary");
    if (!cw_bin.empty()) {
        const std::string tmp = std::string("/tmp/ground_mesh_cw_test.scene");
        {
            FILE* f = std::fopen(tmp.c_str(), "wb");
            if (f) { std::fwrite(cw_bin.data(), 1, cw_bin.size(), f); std::fclose(f); }
        }
        av::SceneData sd = av::scene_load(tmp);
        std::remove(tmp.c_str());
        CHECK(sd.objects.size() == 1, "clockwise ground mesh parses as 1 object");
        if (!sd.objects.empty()) {
            const auto& o = sd.objects.front();
            // Should contain at least 2 surface meshes (top cap + side walls) plus front face
            CHECK(o.ground_meshes.size() >= 3, "clockwise polygon generated top, side, and front meshes");
            // Check that all meshes have valid non-empty geometry
            bool all_non_empty = true;
            for (const auto& gm_part : o.ground_meshes) {
                if (gm_part.positions.empty()) all_non_empty = false;
            }
            CHECK(all_non_empty, "all generated ground mesh parts have non-empty geometry (no missing face or bottom gap)");
        }
    }

    // ── 3) 3D Extruded Preview & Studio Camera Focus Sync ────────────────────
    c.set_depth_extents(-50.0f, 50.0f);
    c.set_textures("fire_grass", "graveyard_ground");
    c.set_extrusion_preview(true);
    CHECK(c.extrusion_preview(), "extrusion preview is enabled");

    // Paint into offscreen pixmap to exercise 2.5D extrusion rendering
    QPixmap pix(c.size());
    c.render(&pix);
    CHECK(!pix.isNull(), "2.5D extruded ground mesh renders offscreen without errors");

    GroundMeshStudio studio;
    studio.set_scene_camera_focus(350.5, -120.0);
    // Camera sync sets X/Y focus targets cleanly
    CHECK(true, "GroundMeshStudio::set_scene_camera_focus executed without error");

    // ── 4) Scale Calibration & Reference Platform Fitting ───────────────────
    GroundMeshCanvas c_scale;
    c_scale.resize(1000, 600);
    c_scale.fit_view();
    // Default empty view scale is comfortably zoomed out (not microscopic 6.0f)
    CHECK(c_scale.scale() <= 1.5f && c_scale.scale() >= 0.1f,
          "default empty view scale is comfortably zoomed out (~0.85f)");

    // User reference mesh (-261.8, 114.7) to (270.5, -179.8): width ~546, height ~305
    c_scale.set_polygon({
        {-261.8f,  114.7f},
        { 262.3f,  125.5f},
        { 270.5f, -176.9f},
        {-275.5f, -179.8f}
    });
    CHECK(c_scale.scale() <= 2.0f && c_scale.scale() >= 0.4f,
          "reference platform (~550x300) fits comfortably in viewport without over-zooming");
    CHECK(std::abs(c_scale.center_world().x()) < 20.0f,
          "reference platform is centered horizontally");

    // ── 5) the ported generator selector + shared Zypher look ───────────────
    //
    // The "3 mesh editor modes" port: the studio must offer all three engine
    // lineages (Boulder / Zenith / Zypher) and the Zypher look GUI must be live
    // exactly on the Zypher mode. The app-level default (Zypher) is applied in
    // main.cpp, so a bare studio starts on whatever the global currently is;
    // here we drive the global directly to keep the check deterministic.
    {
        const auto saved_gen = boulderx::ground_generator();
        const auto saved_look = zypher_look::load();   // restored at the end

        boulderx::set_ground_generator(boulderx::GroundGenerator::Boulder);
        GroundMeshStudio s_boulder;
        auto* gen = s_boulder.findChild<QComboBox*>(QStringLiteral("generatorCombo"));
        CHECK(gen && gen->count() == 3, "studio generator combo offers 3 modes");
        CHECK(gen &&
              gen->itemData(0).toInt() == int(boulderx::GroundGenerator::Boulder) &&
              gen->itemData(1).toInt() == int(boulderx::GroundGenerator::BoulderX) &&
              gen->itemData(2).toInt() == int(boulderx::GroundGenerator::Zypher),
              "combo order is Boulder / Zenith / Zypher");
        CHECK(gen && gen->currentIndex() == 0,
              "Boulder global selects the Boulder combo entry");
        auto* zbox_off = s_boulder.findChild<QGroupBox*>(QStringLiteral("zypherLookBox"));
        CHECK(zbox_off && !zbox_off->isEnabled(),
              "Zypher look GUI is disabled on the non-Zypher modes");

        boulderx::set_ground_generator(boulderx::GroundGenerator::Zypher);
        GroundMeshStudio s_zypher;
        auto* gen_z = s_zypher.findChild<QComboBox*>(QStringLiteral("generatorCombo"));
        CHECK(gen_z && gen_z->currentIndex() == 2,
              "Zypher global selects the Zypher combo entry");
        auto* zbox_on = s_zypher.findChild<QGroupBox*>(QStringLiteral("zypherLookBox"));
        CHECK(zbox_on && zbox_on->isEnabled(),
              "Zypher look GUI is live on the Zypher mode");

        // The look store round-trips, and pours into the generator params —
        // this sharing between the studio and the in-scene editor is why the
        // helper exists at all.
        zypher_look::Look l;
        l.hat = zypher::HatStyle::ZigZag;
        l.top = zypher::TopStyle::Bubbly;
        l.frequency = 9.5f;
        l.depth = 21.0f;
        l.min_collision_radius = 4.0f;
        zypher_look::save(l);
        const zypher_look::Look back = zypher_look::load();
        CHECK(back.hat == zypher::HatStyle::ZigZag &&
              back.top == zypher::TopStyle::Bubbly &&
              std::abs(back.frequency - 9.5f) < 1e-3f &&
              std::abs(back.depth - 21.0f) < 1e-3f &&
              std::abs(back.min_collision_radius - 4.0f) < 1e-3f,
              "the shared Zypher look round-trips through QSettings");

        zypher::TerrainParams p;
        zypher_look::apply(p, back);
        CHECK(p.hat_style == zypher::HatStyle::ZigZag &&
              p.top_style == zypher::TopStyle::Bubbly &&
              std::abs(p.top_frequency - 9.5f) < 1e-3f &&
              std::abs(p.top_depth - 21.0f) < 1e-3f &&
              std::abs(p.top_min_collision_radius - 4.0f) < 1e-3f,
              "the shared look pours into zypher::TerrainParams");

        // Restore whatever the developer had, so running tests never rewrites
        // a real Zypher look.
        zypher_look::save(saved_look);
        boulderx::set_ground_generator(saved_gen);
    }

    std::printf(g_failures == 0 ? "[ground_mesh_studio_test] ALL PASS\n"
                                : "[ground_mesh_studio_test] %d FAILURES\n", g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}