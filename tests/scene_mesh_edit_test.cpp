// scene_mesh_edit_test — acceptance test for the in-scene mesh editor (the
// ImGui viewer's "3D projection lock" port).
//
// Self-contained: generates a ground-mesh object through boulder (the real
// backend), injects it into the viewport's scene, then drives the REAL editor
// headlessly (offscreen GL, Mesa swrast):
//   - Mesh Edit arms and the camera locks to the polygon plane (pitch 0,
//     yaw = -rot_y, target at the polygon centre),
//   - synthesized mouse events hover vertex 0, LMB-drag it by a known screen
//     delta, and release,
//   - committing (Esc) regenerates the object's ground geometry through
//     boulder and the regenerated GroundPolygon outline reflects the drag.
//
// The scene file is only a host for has_scene() (optional, SKIP when absent).
// The viewport is intentionally leaked (QOpenGLWidget teardown needs a live GL
// context that offscreen exit cannot guarantee).

#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "ruby/viewport/viewport_3d_widget.h"
#include "tools/scene_workspace.h"   // swk::object_world_matrix / world_to_screen / ImVec2
#include "tools/boulder.h"
#include "platform/protobuf_reader.h"
#include <map>

namespace fs = std::filesystem;

static int g_failures = 0;

// ─────────────────────────────────────────────────────────────────────────
// In-game ground-mesh texture chain.
//
// The Ruby editor draws ground textures from the BAKED MeshData Material
// (MeshData field 10 → Material field 5 → Texture field 1), so a scene can
// look perfectly textured in-editor while the real game shows nothing but
// collision.  That is because the game does not trust the baked meshes for a
// generator-driven object: Caver::GroundMeshGeneratorComponent::GenerateMesh
// rebuilds them via
//     GroundMeshGenerator::GenerateSurfaceMesh(int, Vector2 const*,
//         float const*, float const*, Caver::TextureMapping const*)
// which takes its texture from a TextureMapping component looked up by the
// generator's SurfaceTextureMappingId (inner field 4) / FrontTextureMappingId
// (inner field 3).  If either id is 0, dangling, or points at a TextureMapping
// with an empty name, the object renders UNTEXTURED in-game while still
// colliding (the GroundPolygon/CollisionShape components are separate and
// keep working).  So the ids and their targets — not the baked material — are
// what must survive an edit + save.
// ─────────────────────────────────────────────────────────────────────────
struct GroundTexChain {
    int      generators = 0;
    int      texture_mappings = 0;
    uint32_t front_id = 0;      // GroundMeshGeneratorComponent.FrontTextureMappingId
    uint32_t surface_id = 0;    // GroundMeshGeneratorComponent.SurfaceTextureMappingId
    std::map<uint32_t, std::string> tm_names;   // component id → texture name
};

static GroundTexChain read_ground_tex_chain(const av::SceneObject& obj) {
    GroundTexChain chain;
    for (const auto& comp : obj.components) {
        const int payload = comp.payload_field;
        if (payload == 112 || comp.type_name == "GroundMeshGenerator") {
            ++chain.generators;
            proto::Reader wrapper(comp.raw_data);
            proto::Field f;
            while (wrapper.read_field(f)) {
                if (f.field_number != 112 || f.wire_type != proto::WIRE_LEN) continue;
                proto::Reader ggc(f.bytes_val);
                proto::Field g;
                while (ggc.read_field(g)) {
                    if (g.field_number == 3) chain.front_id   = (uint32_t)g.varint_val;
                    else if (g.field_number == 4) chain.surface_id = (uint32_t)g.varint_val;
                }
            }
        } else if (payload == 113 || comp.type_name == "TextureMapping") {
            ++chain.texture_mappings;
            std::string name;
            proto::Reader wrapper(comp.raw_data);
            proto::Field f;
            while (wrapper.read_field(f)) {
                if (f.field_number != 113 || f.wire_type != proto::WIRE_LEN) continue;
                proto::Reader tmc(f.bytes_val);
                proto::Field g;
                while (tmc.read_field(g)) {
                    if (g.field_number == 1 && g.wire_type == proto::WIRE_LEN) name = g.bytes_val;
                }
            }
            chain.tm_names[(uint32_t)comp.type_id] = name;
        }
    }
    return chain;
}

// Assert the whole chain on one object; returns true when the object would be
// textured in-game.
static bool check_ground_tex_chain(const av::SceneObject& obj, const char* stage) {
    const GroundTexChain c = read_ground_tex_chain(obj);
    bool ok = true;
    auto fail = [&](const char* what) {
        std::printf("FAIL: [%s] %s\n", stage, what);
        ++g_failures;
        ok = false;
    };
    if (c.generators == 0) fail("no GroundMeshGenerator component — the game cannot rebuild the mesh");
    if (c.texture_mappings < 2)
        fail("fewer than two TextureMapping components (need surface + front)");
    if (c.surface_id == 0)
        fail("SurfaceTextureMappingId is 0 → the game draws the top untextured");
    if (c.front_id == 0)
        fail("FrontTextureMappingId is 0 → the game draws the cliff face untextured");

    auto name_of = [&](uint32_t id) -> std::string {
        auto it = c.tm_names.find(id);
        return it == c.tm_names.end() ? std::string() : it->second;
    };
    if (c.surface_id != 0) {
        const std::string n = name_of(c.surface_id);
        if (n.empty())
            fail("SurfaceTextureMappingId points at a missing or unnamed TextureMapping (dangling id)");
        else
            std::printf("[%s] surface tex: id=%u name=%s\n", stage, c.surface_id, n.c_str());
    }
    if (c.front_id != 0) {
        const std::string n = name_of(c.front_id);
        if (n.empty())
            fail("FrontTextureMappingId points at a missing or unnamed TextureMapping (dangling id)");
        else
            std::printf("[%s] front tex: id=%u name=%s\n", stage, c.front_id, n.c_str());
    }
    return ok;
}

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__);               \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// Synthesized input (raw events — QTest is not available in this Qt build).
static void send_move(QWidget* w, const QPoint& pos, Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent ev(QEvent::MouseMove, QPointF(pos), QPointF(pos),
                   Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}
static void send_press(QWidget* w, const QPoint& pos) {
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), QPointF(pos),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}
static void send_release(QWidget* w, const QPoint& pos) {
    QMouseEvent ev(QEvent::MouseButtonRelease, QPointF(pos), QPointF(pos),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}
static void send_key(QWidget* w, int key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(w, &release);
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    std::string src;
    if (argc >= 2) {
        src = argv[1];
    } else {
#ifdef SWORDIGO_SRC_DIR
        src = std::string(SWORDIGO_SRC_DIR) + "/assets/v3_Wasteland.scene";
#else
        src = "src/assets/v3_Wasteland.scene";
#endif
    }
    if (!fs::exists(src)) {
        std::printf("SKIP: scene file not found: %s\n", src.c_str());
        return 0;   // optional data file — skip, not fail
    }

    auto* viewport = new ruby::viewport::Viewport3DWidget();   // intentionally leaked
    viewport->resize(800, 600);
    viewport->show();
    app.processEvents();

    if (!viewport->load_scene(src)) {
        std::printf("FAIL: load_scene(%s) returned false\n", src.c_str());
        return 1;
    }
    app.processEvents();

    // ── Inject a ground-mesh object (boulder → bytes → scene_load) ─────────
    const std::vector<boulder::PolygonPoint> poly = {{0, 0}, {800, 0}, {800, 400}, {0, 400}};
    boulder::GroundMesh gm;
    gm.polygon = poly;
    gm.top_texture = "fire_grass";
    gm.bottom_texture = "graveyard_ground";
    const std::string swdm = boulder::serialize_swdm(gm);
    const std::string bin = boulder::generate_ground_mesh_object(swdm, "mesh_edit_test", 40.0);
    if (bin.empty()) {
        std::printf("FAIL: generate_ground_mesh_object returned empty\n");
        return 1;
    }
    std::vector<uint8_t> bytes(bin.begin(), bin.end());
    av::SceneData parsed;
    try {
        parsed = av::scene_load_bytes(bytes, src);
    } catch (const std::exception&) {
        std::printf("FAIL: generated ground object failed to parse\n");
        return 1;
    }
    if (parsed.objects.empty() || parsed.objects[0].ground_meshes.empty()) {
        std::printf("FAIL: generated object has no ground meshes\n");
        return 1;
    }

    const size_t n_objects = viewport->scene().objects.size();
    const int mesh_idx = viewport->add_ground_mesh_object(parsed.objects[0]);
    app.processEvents();
    CHECK(mesh_idx == (int)n_objects, "add_ground_mesh_object appends at the end");
    CHECK(viewport->scene().objects.size() == n_objects + 1, "scene gained one object");

    viewport->set_selected_object(mesh_idx);
    app.processEvents();
    CHECK(viewport->can_mesh_edit(), "can_mesh_edit() must be true on a ground-mesh object");

    viewport->set_mesh_edit(true);
    app.processEvents();
    CHECK(viewport->mesh_edit_active(), "Mesh Edit must arm after set_mesh_edit(true)");

    // ── Projection lock: pitch 0, yaw = -rot_y (0 here), polygon-centred ────
    {
        const auto cam = viewport->camera_state();
        CHECK(std::fabs(cam.pitch) < 1e-4f, "camera pitch locks to 0 (2D front view)");
        const float expect_yaw = -parsed.objects[0].rot_y * 180.0f / 3.14159265f;
        CHECK(std::fabs(cam.yaw - expect_yaw) < 1e-3f, "camera yaw locks to -rot_y");
    }

    // Snap is ON by default (25 units) — toggle it off so the drag delta maps
    // 1:1 from screen pixels to world units on the locked plane.
    send_key(viewport, Qt::Key_G);
    app.processEvents();

    // ── Drag vertex 0 (+30 px on screen → ~+44 world units) ────────────────
    // The locked camera is fitted to the 800x400 polygon; vertex 0 is (0,0)
    // in object-local space and the object sits at the origin, so project
    // (0,0,0) through the object matrix into the locked view.
    float obj_mat[16];
    swk::object_world_matrix(parsed.objects[0], obj_mat);
    const float v0[3] = { obj_mat[12], obj_mat[13], obj_mat[14] };
    const auto cam = viewport->camera_state();
    av::Camera ac;
    ac.yaw = cam.yaw; ac.pitch = cam.pitch; ac.distance = cam.dist;
    ac.target[0] = cam.target[0]; ac.target[1] = cam.target[1]; ac.target[2] = cam.target[2];
    ac.fov = 45.0f;
    ImVec2 v0_scr;
    if (!swk::world_to_screen(ac, 800, 600, ImVec2(0, 0), v0, v0_scr)) {
        std::printf("FAIL: vertex 0 does not project on screen\n");
        return 1;
    }
    const QPoint grab((int)v0_scr.x, (int)v0_scr.y);
    const QPoint target(grab.x() + 30, grab.y());

    send_move(viewport, grab);                    // hover arms the vertex target
    app.processEvents();
    send_press(viewport, grab);
    app.processEvents();
    for (int step = 1; step <= 6; ++step) {      // drag in small steps (live apply ticks)
        send_move(viewport, QPoint(grab.x() + step * 5, grab.y()), Qt::LeftButton);
        app.processEvents();
    }
    send_move(viewport, target, Qt::LeftButton);
    app.processEvents();
    send_release(viewport, target);
    app.processEvents();

    // ── Commit (Esc) and verify the regenerated outline moved ───────────────
    send_key(viewport, Qt::Key_Escape);
    app.processEvents();
    CHECK(!viewport->mesh_edit_active(), "Esc must end the mesh-edit session");

    const av::SceneData& after = viewport->scene();
    CHECK(after.objects.size() == n_objects + 1, "mesh edit must not change the object count");
    const av::SceneObject& edited = after.objects[mesh_idx];
    CHECK(edited.pos_z == 40.0f, "depth layer is preserved");
    const std::vector<float>& new_poly = edited.ground_polygon_points;
    CHECK(new_poly.size() >= 8, "regenerated GroundPolygon outline is present");
    if (new_poly.size() >= 2) {
        // Vertex 0 moved right by ~44 world units (30 px at the fitted zoom).
        const double dx = new_poly[0];
        std::printf("vertex 0 after drag: x=%.1f (was 0)\n", dx);
        CHECK(std::fabs(dx) > 10.0, "dragging a vertex regenerated the ground polygon");
    }

    // ── The texture chain the GAME reads must survive the edit ──────────────
    check_ground_tex_chain(edited, "after commit");
    CHECK(!edited.ground_mesh_textures.empty(),
          "baked ground mesh material kept its texture name (editor preview)");

    // ── …and a save → reload round trip ────────────────────────────────────
    {
        av::SceneData to_save = after;
        const std::string bytes = av::scene_serialize(to_save);
        CHECK(!bytes.empty(), "scene_serialize produced bytes");
        if (!bytes.empty()) {
            const std::vector<uint8_t> buf(bytes.begin(), bytes.end());
            av::SceneData reloaded;
            try {
                reloaded = av::scene_load_bytes(buf, src);
            } catch (const std::exception& e) {
                std::printf("FAIL: re-parse of saved scene threw: %s\n", e.what());
                ++g_failures;
            }
            if (reloaded.objects.size() == after.objects.size()) {
                check_ground_tex_chain(reloaded.objects[mesh_idx], "after save+reload");
                CHECK(!reloaded.objects[mesh_idx].ground_mesh_textures.empty(),
                      "saved file still carries the baked ground mesh material");
            } else {
                std::printf("FAIL: reloaded scene object count %zu != %zu\n",
                            reloaded.objects.size(), after.objects.size());
                ++g_failures;
            }
        }
    }

    std::fflush(stdout);
    if (g_failures == 0)
        std::printf("PASS: in-scene mesh edit (projection lock + vertex drag + boulder round trip)\n");
    else
        std::printf("FAIL: %d mesh-edit assertion(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}