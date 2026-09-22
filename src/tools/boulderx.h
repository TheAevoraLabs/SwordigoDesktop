#pragma once
// ============================================================================
// boulderx.h — Caver-faithful ground-mesh generator (Boulder, 2nd generation)
//
// boulder (src/tools/boulder.cpp) is DanielSpaniel's original port and it is
// *kept* — the ImGui `ruby` target still builds on it.  boulderx exists because
// boulder transcribes one vanilla mesh's vertex template rather than
// implementing the engine's generator, and that template cannot express most
// shipped terrain.  See
// docs/formats_and_schemas/scenecreator/13_groundmesh_engine_parity_and_boulderx.md
// for the evidence behind every rule below.
//
// What is implemented from the decompiled engine (libswordigo 1.4.13):
//   GroundMeshGeneratorComponent::GenerateMesh            @ 0x2D516C (arm32)
//   GroundMeshGenerator::GenerateFrontMesh                @ 0x2D4858
//   GroundMeshGenerator::GenerateSurfaceMesh              @ 0x2D3690
//   GroundMeshGenerator::InsertRoundHatVertices           @ 0x2D3BDC
//   Caver::IsConvexVertex / PointInsideTriangle / IsAnEar @ 0x2D6C78 / 0x2D6E52 / 0x2D6EEC
//   GroundMeshGenerator::InitializeMeshBuilder            @ 0x2D3980
//
// Every geometry claim is additionally cross-checked against the baked meshes
// that ship inside the 202 decoded scenes in Scener/data/decoded_scenes
// (2,538 ground objects, 70% of them concave).  tests/boulderx_engine_parity_test
// pins that agreement.
// ============================================================================

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace boulderx {

// ── Sketch-space outline ────────────────────────────────────────────────────
// boulder stores PolygonPoint{x,y} plus a SCALAR min/max depth for the whole
// mesh. The engine does not: GenerateMesh builds per-vertex depth arrays
// (`frand()`-jittered by HorizNoise) and GenerateFrontMesh reads one depth per
// triangle corner. Per-node depth is what makes terrain 3D and what boulder
// cannot express.
struct Node {
    double x = 0.0;
    double y = 0.0;
    double front_depth = 45.0;   // +z, toward the camera
    double back_depth = -45.0;   // -z
};

// ── Generator parameters: GroundMeshGeneratorComponent, field for field ─────
struct Params {
    // Field 7. 0 = plain ribbon only; 1 = plain ribbon plus the rounded rim
    // (InsertRoundHatVertices). 81% of shipped objects are MeshType 1.
    int mesh_type = 1;
    double surface_width = 200.0;        // field 8
    double hat_height = 25.0;            // field 9  — rim depth (inward extent)
    double hat_width_offset_1 = 5.0;     // field 10 — rim bevel width
    double hat_width_offset_2 = 5.0;     // field 11
    double horiz_noise = 0.0;            // field 6  — per-node depth jitter
    uint32_t random_seed = 1291618994u;  // field 5  — the editor's default seed
    double texture_scale = 250.0;
    std::string surface_texture = "fire_grass";         // walkable surface
    std::string front_texture = "graveyard_ground";     // camera-facing face
};

struct Vertex {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
};

// One mesh the engine writes into GroundMeshComponent. `indexed` mirrors
// InitializeMeshBuilder(bool): SurfaceMesh is indexed, FrontMesh is not.
struct Submesh {
    std::string kind;      // "SurfaceMesh" (field 8) | "FrontMesh" (field 9)
    std::string texture;
    bool indexed = true;
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};

struct Mesh {
    std::vector<Submesh> surfaces;   // field 8, repeated — order is significant
    std::vector<Submesh> fronts;     // field 9, repeated
    bool ok = false;
    std::string error;

    size_t vertex_count() const;
    size_t triangle_count() const;
    // Outermost |z| over every submesh. The engine writes this back into
    // GroundPolygon.MinDepth/-MaxDepth, which is what collision uses.
    double max_abs_z() const;
    // LocalAabb, from the geometry the engine would produce.
    void bounds(double& x, double& y, double& w, double& h) const;
};

struct ComponentIds {
    int polygon_id = 980;
    int mesh_id = 981;
    int generator_id = 982;
    int collision_id = 983;
    int tm_surface_id = 984;
    int tm_front_id = 985;
};

// ── Polygon helpers ─────────────────────────────────────────────────────────
double signed_area(const std::vector<Node>& outline);
// Reverses in place when the winding is not counter-clockwise. The engine's
// polygons are CCW (`Convex : 0`, `Closed : 1` in every decoded scene).
void ensure_ccw(std::vector<Node>& outline);

// Sets every node to +/- half_depth. Convenience for the common flat slab.
void set_uniform_depth(std::vector<Node>& outline, double half_depth);

// Outline construction — the capability boulder lacks entirely. A boulder
// "polygon" is only usable if its top edges are within top_angle of
// horizontal; arcs and slopes produce no surface at all.
void append_rect(std::vector<Node>& outline, double x, double y, double w, double h,
                 double front_depth, double back_depth);
void append_polyline(std::vector<Node>& outline,
                     const std::vector<std::pair<double, double>>& xy,
                     double front_depth, double back_depth);
// Samples a circular arc. Angles in degrees, CCW, start->end.
void append_arc(std::vector<Node>& outline, double cx, double cy, double radius,
                double start_deg, double end_deg, int segments,
                double front_depth, double back_depth);

// ── Triangulation ───────────────────────────────────────────────────────────
// Ear clipping with Caver's own predicates. Emits 3*(n-2) indices forming
// valid non-overlapping CCW triangles, which is what makes concave outlines
// (70% of shipped ground) work. The engine's ear *order* is an implementation
// detail; the triangle set and total area are not.
bool triangulate(const std::vector<std::pair<double, double>>& polygon,
                 std::vector<uint16_t>& out_indices);

// ── Generation ──────────────────────────────────────────────────────────────
// Builds the GroundMesh submeshes the engine's GenerateMesh would build for
// this outline + parameter bundle.
bool generate(const std::vector<Node>& outline, const Params& params, Mesh& out);

// ── .swdm (Ruby Canvas) round-trip ──────────────────────────────────────────
// Text dialect shared with boulder so the editors, CLI and MCP paths keep
// working; carries per-node depth (boulder's dialect carries a scalar).
struct SwdmDocument {
    std::string identifier = "ground";
    Params params;
    std::vector<Node> outline;

    // Dialect the sheet was READ from. True means a legacy boulder v1 sheet,
    // where every node's depth was filled in from the sheet's single scalar
    // MinDepth/MaxDepth pair. Writing always produces v2 (see serialize_swdm),
    // so this is a read-side fact only.
    bool from_boulder_dialect = false;
    // A v1 sheet may carry a `Hat[` block — dome hats, which have no per-node
    // representation and are not emitted yet. Recorded rather than dropped:
    // generate_ground_mesh_object_swdm() refuses the whole sheet when set, the
    // same way the editors' adapters refuse `has_dome_hats`.
    bool has_dome_hats = false;
};

// Writes a v2 (boulderx) sheet. A boulderx sheet is NOT downgradable: its
// per-node depths have no v1 representation, so boulder::parse_gmesh() refuses
// one instead of flattening it. A v1 sheet, by contrast, loads here — see
// parse_swdm().
std::string serialize_swdm(const SwdmDocument& doc);

// Parses EITHER dialect into the same per-node document. A v1 sheet becomes a
// uniform slab at its own MinDepth/MaxDepth, and sets from_boulder_dialect (and
// has_dome_hats when it carries a `Hat[` block).
bool parse_swdm(const std::string& text, SwdmDocument& out);

// True when `text` carries the per-node (v2) dialect marker. Same predicate as
// boulder::is_boulderx_swdm(), for callers that link boulderx but not boulder.
bool swdm_is_boulderx(const std::string& text);

// ── Binary emission ─────────────────────────────────────────────────────────
// A complete GroundMesh scene object: GroundPolygon + GroundMesh (SurfaceMesh/
// FrontMesh) + GroundMeshGenerator + CollisionShape + 2x TextureMapping.
// Empty string on failure.
std::string generate_ground_mesh_object(const std::vector<Node>& outline,
                                        const Params& params,
                                        const std::string& identifier,
                                        const ComponentIds* ids = nullptr);

// Parses .swdm text (either dialect) and emits the scene object in one step.
// Refuses a sheet whose dome hats would otherwise be silently dropped.
std::string generate_ground_mesh_object_swdm(const std::string& swdm_text,
                                             const std::string& identifier,
                                             const ComponentIds* ids = nullptr);

// ── Generator choice ────────────────────────────────────────────────────────
// Ruby GG (src/ruby/viewport/viewport_3d_widget.cpp) and Ruby Touch
// (src/ruby/android/ruby_quick_viewport.cpp) consult this before regenerating a
// ground mesh. The ImGui `ruby` target has no call site at all — boulder stays
// its generator, by design, because that front-end is deprecated.
//
// This is deliberately a named two-value enum rather than a bool. The two
// generators are alternatives, and the type makes the call sites read the same
// way as the settings control that drives them, so "which generator" is never
// inferred from a bare `true`.
//
// The environment variable RUBY_GROUND_GENERATOR=boulderx sets the *default*
// (handy for A/B-ing a scene without a rebuild). A saved user choice in the GUI
// takes precedence over it; see ruby_settings_bridge.cpp.
enum class GroundGenerator {
    Boulder,    // the original generator (src/tools/boulder.cpp)
    BoulderX,   // engine-parity generator; declines what it cannot reproduce
};

GroundGenerator ground_generator();
void set_ground_generator(GroundGenerator which);

// Stable identifiers, used for persistence and by the settings GUI.
// ground_generator_from_id() is total: anything unrecognised maps to Boulder, so
// a stale or hand-edited config can never select a generator that isn't there.
const char* ground_generator_id(GroundGenerator which);
GroundGenerator ground_generator_from_id(const std::string& id);

// ── Why boulderx declined ───────────────────────────────────────────────────
// Every boulderx entry point that returns an empty string records the reason
// first, and every entry point clears it on entry, so the reason always
// describes the most recent call. The editors fall back to boulder on an empty
// result, and a fallback that cannot be told apart from success is how a mesh
// silently loses geometry — so the reason is part of the API, not a log line.
//
// Not thread-safe; the editors generate ground meshes on the GUI thread.
const std::string& last_decline_reason();
void clear_decline_reason();

// ── Adapter for the editors' existing state ─────────────────────────────────
// Both editors carry a boulder::GroundMesh (flat polygon + ONE scalar depth
// pair + a separate dome-hat list). This converts that into boulderx terms.
//
// Returns an empty string — rather than a partial mesh — when the request uses
// a feature boulderx does not yet reproduce faithfully, so the caller can fall
// back to boulder. Currently that is dome hats (boulder's `hats`, which come
// from InsertCapForRoundHat); the signature takes the flag explicitly so the
// refusal is visible at the call site instead of silent.
//
// Component ids arrive as six plain ints in boulder::GroundComponentIds order
// (polygon, mesh, generator, collision, tm_surface, tm_front) so this header
// never has to include boulder.h. Pass nullptr for the defaults.
std::string generate_ground_mesh_object_flat(
    const std::vector<std::pair<double, double>>& polygon_xy,
    int mesh_type, double half_depth,
    const std::string& surface_texture, const std::string& front_texture,
    const std::string& identifier, bool has_dome_hats,
    const int* ids6 = nullptr);

// Per-node version, and the one the editors should prefer.
//
// A uniform half-depth is not how the engine builds terrain: GenerateMesh reads
// one depth per node and GenerateFrontMesh reads one depth per triangle corner.
// So an edit routed through the flat overload necessarily FLATTENS vanilla
// relief — the ground becomes a constant-thickness slab, which is the "boulderx
// strips the Z" symptom. This overload carries the outline through with each
// node's own front/back depth, plus the generator parameters recovered from the
// object (seed, HorizNoise, texture scale) rather than only its mesh type.
std::string generate_ground_mesh_object_nodes(
    const std::vector<Node>& outline, const Params& params,
    const std::string& identifier, bool has_dome_hats,
    const int* ids6 = nullptr);

// Gives an outline relief, deterministically.
//
// Ground sheets are drafted in 2D, so there is no way to draw Z — every node
// gets the same depth and the result reads as a flat plate. This assigns each
// node its own front/back depth within the object's own declared range, so the
// mesh keeps the thickness it already had while gaining the variation that
// makes terrain look like terrain.
//
// `seed` makes it reproducible: the same seed and outline always produce the
// same mesh, so a modder who likes a result can type the seed back in and get it
// again. `front_max` / `back_max` are absolute magnitudes (both positive).
void randomise_node_depths(std::vector<Node>& outline, double front_max, double back_max,
                           uint32_t seed);

} // namespace boulderx
