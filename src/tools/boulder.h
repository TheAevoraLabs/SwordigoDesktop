#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace boulder {

    struct PolygonPoint {
        double x, y;
    };

    struct Vector3 {
        double x, y, z;
    };

    struct Vector2 {
        double x, y;
    };

    // A "round hat" dome (Caver::GroundMeshGenerator::InsertRoundHatVertices /
    // InsertCapForRoundHat): a rounded bump standing on the surface of the
    // ground mesh. The footprint is a circle of radius r centered at (x,y) in
    // sketch space; the dome rises `height` above the polygon's top edge.
    struct Hat {
        double x = 0.0, y = 0.0;
        double radius = 60.0;
        double height = 40.0;
    };

    struct GroundMesh {
        std::vector<PolygonPoint> polygon;
        std::vector<Hat> hats;               // round-hat domes on the surface
        double min_depth = -45.0;
        double max_depth = 45.0;
        double top_angle = 20.0;
        bool generate_top = true;
        double z = 40.0;                     // constant Z (world depth) for the whole mesh
        std::string top_texture = "fire_grass";
        std::string bottom_texture = "graveyard_ground";

        // ── CollisionShapeComponent (extension 121) fields ─────────────────
        //
        // Field numbers verified three ways for libswordigo 1.4.13 arm64:
        //   * Caver::Proto::protobuf_AddDesc_Scene_2eproto (sub_41A8DC) gives
        //     the component extensions (Shape=120, CollisionShape=121);
        //   * CollisionShapeComponent::MergePartialFromCodedStream (0x3EB32C)
        //     gives each field's number AND wire type (2/3/4/5 varint,
        //     6/7 32-bit float, 8 varint, 9/10/12 length-delimited,
        //     11 varint, 13 float, 14 varint);
        //   * the repo's FileRift schema in src/tools/filerift.cpp names them
        //     (2 IsGround, 3 Collides, 4 ReceivesDamage, 5 InflictsDamage,
        //     6 MinDepth, 7 MaxDepth, 8 SpecialType, 11 Enabled).
        //
        // What the SHIPPED DATA does is the part worth being careful about.
        // Measured across all 202 decoded scenes (6,588 CollisionShapeComponent
        // messages): the terrain pattern is IsGround=1, MinDepth, MaxDepth,
        // Enabled=1 with Collides/ReceivesDamage/SpecialType ABSENT — 5,345
        // messages, and 5,390 of the 5,398 ground shapes omit SpecialType
        // entirely.  The trio appears only on breakable props, always all
        // three together: box, box_big, keybox, boulder, sign,
        // pushable_ground, inbreakablebox (SpecialType 7) and questvase
        // (SpecialType 5).
        //
        // So they are OPT-IN.  Vanilla never marks plain terrain breakable or
        // damageable, and generating it that way would hand the level's ground
        // to the breakable-object code path.  Set these on a prop-like mesh.
        bool collides         = false;  // field 3  (absent = runtime default)
        bool receives_damage  = false;  // field 4
        bool inflicts_damage  = false;  // field 5
        bool has_special_type = false;  // field 8 — enum, so 0 is meaningful
        int  special_type     = 0;      // 7 = breakable box, 5 = breakable item
        bool enabled          = true;   // field 11
        double surface_width = 80.0;
        double hat_height = 25.0;
        double hat_width_offset_1 = 5.0;
        double hat_width_offset_2 = 5.0;
        double texture_scale = 250.0;
        uint32_t random_seed = 1291618994u;
    };

    struct GroundComponentIds {
        int polygon_id = 980;
        int mesh_id = 981;
        int generator_id = 982;
        int collision_id = 983;
        int tm_surface_id = 984;
        int tm_front_id = 985;
    };
    // 2D polygon orientation helpers (enforces counter-clockwise winding for Boulder meshing)
    double polygon_area(const std::vector<PolygonPoint>& pts);
    void ensure_ccw(std::vector<PolygonPoint>& pts);

    // Parses a .gmesh / .swdm file content and generates FileRift-compatible
    // GroundMesh markup. Returns empty string on failure.
    std::string generate_ground_mesh(const std::string& gmesh_content);

    // Parse .gmesh / .swdm content into a GroundMesh struct (exposed for the editor).
    //
    // A BoulderX sheet (format v2, per-node depths) parses to an EMPTY GroundMesh:
    // this format stores one depth for the whole sheet, so reading a v2 file here
    // would silently flatten relief a modder authored. last_parse_error() says so.
    GroundMesh parse_ground_mesh(const std::string& content);

    // True when `content` is a BoulderX (.swdm v2) sheet — the same predicate
    // boulderx::swdm_is_boulderx() exposes. Declared here so callers that link
    // boulder but not boulderx (the Qt studio, the ImGui sketcher) can tell the
    // user which generator to switch to instead of reporting a generic failure.
    bool is_boulderx_swdm(const std::string& content);

    // Why the most recent parse_ground_mesh() / generate_ground_mesh_object()
    // produced nothing. Cleared on every call, so it always describes the most
    // recent one. Empty means no failure.
    const std::string& last_parse_error();
    void clear_parse_error();

    // Serialize a GroundMesh to .swdm text (round-trips with parse_ground_mesh).
    // Always the v1 dialect: boulder has no per-node depth to write, and a v1
    // sheet loads in BoulderX as well (it upgrades to a uniform slab), so its
    // output stays readable by every generator.
    std::string serialize_swdm(const GroundMesh& gm);

    // Build a COMPLETE GroundMesh scene object directly as protobuf binary bytes
    // (Scene field 1 = single Object). This bypasses the lossy markup->recode
    // text pipeline that corrupts embedded binary mesh data. The generated object
    // carries GroundPolygon + GroundMesh (SurfaceMesh/FrontMesh) + Generator +
    // TextureMapping components, exactly like boulder's reference markup.
    // Returns empty string on failure (polygon < 3 points, etc).
    std::string generate_ground_mesh_object(const std::string& gmesh_content,
                                            const std::string& identifier,
                                            double depth,
                                            const GroundComponentIds* ids = nullptr);

} // namespace boulder
