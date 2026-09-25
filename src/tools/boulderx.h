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
//   GroundMeshGenerator::InsertCapForRoundHat             @ 0x2D3BDC (rim seam)
//   Caver::IsConvexVertex / PointInsideTriangle / IsAnEar @ 0x2D6C78 / 0x2D6E52 / 0x2D6EEC
//   GroundMeshGenerator::InitializeMeshBuilder            @ 0x2D3980
//
// Both round-hat functions are the RIM, not domes: see the note on `Hat` below.
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
    // HorizNoise written back into the generator component, when that differs
    // from the value applied above. It has to be able to: a mesh rebuilt from the
    // depths recovered out of a vanilla bake must NOT have the noise applied a
    // second time (the bake already contains it), yet the object's own generator
    // parameters must survive the edit unchanged. Negative means "same as
    // horiz_noise", which is the case for everything authored from scratch.
    double emitted_horiz_noise = -1.0;
    uint32_t random_seed = 1291618994u;  // field 5  — the editor's default seed

    // Where the per-node depth comes from.
    //
    // The engine does not read a per-node depth from anywhere: GenerateMesh
    // (0x2D516C) *computes* one per node from SurfaceWidth and HorizNoise alone,
    // with a seeded PRNG — see engine_node_depths() below, which reproduces a
    // shipped mesh's depths exactly. So `Engine` is the faithful setting and the
    // default, and a per-node depth on the outline is the port's own extension
    // (a modder dragging relief in the mesh editor) rather than an engine input.
    //
    // Callers that carry genuine per-node relief — the editors' mesh-edit path —
    // set `Outline`; callers that have no relief of their own
    // (`generate_ground_mesh_object_flat`, a fresh sheet) get `Engine` and
    // therefore produce what the engine would.
    enum class DepthSource {
        Engine,    // (frand() - 0.5) * HorizNoise + SurfaceWidth/2, per node
        Outline,   // the node's own front_depth/back_depth, noise added if any
    };
    DepthSource depth_source = DepthSource::Engine;
    double texture_scale = 250.0;
    std::string surface_texture = "fire_grass";         // walkable surface
    std::string front_texture = "graveyard_ground";     // camera-facing face
};

// ── Dome hats ───────────────────────────────────────────────────────────────
// A dome standing ON the surface: a circular footprint of `radius` at (x, y)
// rising `height` above the polygon's own top edge, spanning the sheet's full
// depth so the hero can walk over it.
//
// Read the naming carefully, because the engine's own symbols point the other
// way. `InsertRoundHatVertices` / `InsertCapForRoundHat` do NOT build this: in
// GenerateSurfaceMeshWithRoundHat they build the ROUNDED RIM (the "brim") of a
// MeshType 1 slab, one profile ring per outline node, and `MeshType 1` is what
// the generator component calls "round-hat surfaces". Shipped data agrees with
// the rim reading and not with the dome reading: across all 2,538 ground objects
// in the 202 decoded scenes the largest amount any baked vertex rises above its
// own polygon's top edge is **2.04**, and every one of those 154 vertices sits
// **exactly 3.000** from a convex node — the rim's corner shift. A `HatHeight`
// dome would rise 20-40. (.scratch/ground_dome_probe.py is that measurement.)
//
// So there is nothing in the engine to port here, and boulder's dome is an
// authored feature with no engine counterpart. It is emulated in this file
// because boulder writes it into scenes: a `.swdm` sheet may carry a `Hat[`
// block, and a generator that silently dropped it would delete domes from a
// level on a generator switch. boulderx reproduces boulder's geometry (see
// build_hat()) rather than refusing.
struct Hat {
    double x = 0.0, y = 0.0;
    double radius = 60.0;
    double height = 40.0;
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

// ── The engine's per-node depth law, and its PRNG ───────────────────────────
//
// GenerateMesh (0x2D516C) writes the polygon's MinDepth/MaxDepth and then builds
// two float arrays, one entry per outline node, with two frand() draws each:
//
//     seed_fastrandom(RandomSeed);
//     for (i = 0; i < node_count; ++i) {
//         front[i] = (frand() - 0.5) * HorizNoise + SurfaceWidth * 0.5;
//         back[i]  = -SurfaceWidth * 0.5 - (frand() - 0.5) * HorizNoise;
//     }
//
// so the depth is centred on SurfaceWidth/2 and the object's per-node variation
// is entirely HorizNoise. Note what is *not* an input: the polygon's
// MinDepth/MaxDepth are written BY this, not read from it — for MeshType 1 they
// become +/-(SurfaceWidth/2 + HatWidthOffset1) because the rim's outer edge sits
// at SurfaceWidth/2 + W (see generate()'s rim profile).
//
// The PRNG is POSIX nrand48-shaped, and every piece below is a transcription:
//
//   Caver::seed_fastrandom  @ 0x37F464   x0 = (seed << 16) | 0x330E, x1 = seed >> 16
//   Caver::fastrandom       @ 0x37F48C   the 48-bit LCG step and the 31-bit output
//   Caver::frand            @ 0x2B25FC   fastrandom() / FASTRANDOM_MAX (0x7FFFFFFF)
//
// Verified against the shipped bake, not just against the decompile: for
// thecave_part1 obj1 (seed 0, SurfaceWidth 100, HorizNoise 20) this sequence's
// every-other draw reproduces all 31 of that object's baked front depths to the
// float, and tools/ground_mesh_truth.py verify checks the same claim over a
// corpus.
class Frand {
public:
    explicit Frand(uint32_t seed) {
        x0_ = ((seed << 16) | 0x330Eu) & 0xFFFFFFFFu;
        x1_ = (seed >> 16) & 0xFFFFu;
    }
    // Caver::fastrandom: the next 31-bit draw.
    uint32_t next_u31() {
        const uint64_t carry = (0x5DEECE66Dull * x0_ + 11ull) >> 32;
        const uint32_t x1_next = static_cast<uint32_t>(
            (-6547 * static_cast<int32_t>(x1_) + static_cast<int32_t>(carry)) & 0xFFFF);
        x0_ = (-554899859u * x0_ + 11u);      // 0xDEECE66D * x0 + 11, wrapping
        x1_ = x1_next;
        return (x0_ >> 17) | (x1_ << 15);
    }
    // Caver::frand: the same draw as a float in [0, 1).
    double next() { return static_cast<double>(next_u31()) / 2147483647.0; }

private:
    uint32_t x0_ = 0;
    uint32_t x1_ = 0;
};

// The engine's own per-node depth arrays, in outline order. This is what the
// editors should use *instead of* recovering depths out of a baked mesh: the law
// needs the object's generator parameters (which a ground object always carries)
// and nothing else, so there is no XY matching to fail and no node can collapse
// onto another's depth. `recover_node_depths` remains for objects whose
// generator is gone or whose bake was hand-edited.
void engine_node_depths(const Params& params, size_t node_count,
                        std::vector<double>& front_out, std::vector<double>& back_out);

// ── Recovering per-node depth from a baked mesh (the FALLBACK) ──────────────
// Prefer engine_node_depths(): GenerateMesh *computes* the array from the
// generator's parameters, so where those exist there is nothing to recover and
// nothing that can fail to match. This is for a bake whose generator is gone.
//
// It returns the PLAIN plane per node — the shallowest positive z and the
// deepest negative z on that node's own XY — because the plain plane is what the
// rest of the generator measures from: the ribbon and the cap sit on it, and
// build_rim's bevel and inner wall are offsets FROM it.
//
// "Highest/lowest z on the node" (what this used to do) is a different array on
// a MeshType 1 object, and a wrong one: the rim rings its node with vertices at
// `plain +- HatWidthOffset1`, so the extremes are the RIM's planes, and feeding
// those to build_rim adds HatWidthOffset1 a second time. Measured on
// thecave_crypt2 obj5 (SurfaceWidth 180, W1 5): envelope +-100 becomes +-105.
//
// It is still not exact at a rim node, and cannot be: the ribbon yields that
// node to the rim, so the bake carries no plain-plane BACK vertex there and the
// deepest non-positive z is W1 too far out. Only the law knows that value.
//
// `positions` is the mesh's interleaved x,y,z floats (what
// av::SceneGroundMesh::positions carries and what the editor renders from).
//
// Returns false when fewer than 3 nodes matched, i.e. when there is nothing to
// recover — callers then keep their uniform depth rather than inventing relief.
bool recover_node_depths(const std::vector<Node>& outline,
                        const std::vector<float>& positions,
                        double tolerance,
                        std::vector<double>& front_out,
                        std::vector<double>& back_out);

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
//
// `hats` adds one dome SurfaceMesh per hat, after the rim and the ribbon — the
// order doc 09 §3 records for a shipped GroundMesh and the order boulder emits.
// Pass nullptr (the default) for the engine-only geometry.
bool generate(const std::vector<Node>& outline, const Params& params, Mesh& out,
              const std::vector<Hat>* hats = nullptr);

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
    // A `Hat[` block: one dome per line, `x y radius height`. Read from either
    // dialect (boulder writes the block into v1 sheets, boulderx writes it into
    // v2 ones) and carried through generation, so a sheet round-trips its domes
    // instead of losing them.
    std::vector<Hat> hats;
};

// Writes a v2 (boulderx) sheet. A boulderx sheet is NOT downgradable: its
// per-node depths have no v1 representation, so boulder::parse_gmesh() refuses
// one instead of flattening it. A v1 sheet, by contrast, loads here — see
// parse_swdm().
std::string serialize_swdm(const SwdmDocument& doc);

// Parses EITHER dialect into the same per-node document. A v1 sheet becomes a
// uniform slab at its own MinDepth/MaxDepth and sets from_boulder_dialect; a
// `Hat[` block in either dialect becomes dome geometry in `hats`.
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
                                        const ComponentIds* ids = nullptr,
                                        const std::vector<Hat>* hats = nullptr);

// Parses .swdm text (either dialect) and emits the scene object in one step,
// dome hats included.
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
// **Boulder is the default.** BoulderX is opt-in: it is the engine-parity
// generator and the one that keeps a vanilla object's per-node relief, but it
// declines what it cannot reproduce, so the shipped default stays the generator
// that always produces a mesh. The environment variable
// RUBY_GROUND_GENERATOR=boulderx (or =boulder) overrides the initial value only —
// handy for A/B-ing a scene without a rebuild — and a saved user choice in the
// GUI takes precedence over it; see ruby_settings_bridge.cpp.
enum class GroundGenerator {
    Boulder,    // the original generator (src/tools/boulder.cpp)
    BoulderX,   // engine-parity generator; declines what it cannot reproduce
    Zypher,     // Gen 3 modern procedural 3D world geometry generator
};

GroundGenerator ground_generator();
void set_ground_generator(GroundGenerator which);

// Stable identifiers, used for persistence and by the settings GUI.
// ground_generator_from_id() is total and biased like the default: only an
// explicit "boulderx" selects BoulderX, so a stale or hand-edited config can never
// select a generator that isn't there and never silently switches a scene to the
// non-default one either.
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
// The hat list is real geometry, not a flag: it is passed through and emitted,
// so the generators agree on what an object looks like. It used to be a bool
// that only ever meant "refuse", which is how a dome used to vanish on a
// generator switch.
//
// Component ids arrive as six plain ints in boulder::GroundComponentIds order
// (polygon, mesh, generator, collision, tm_surface, tm_front) so this header
// never has to include boulder.h. Pass nullptr for the defaults.
std::string generate_ground_mesh_object_flat(
    const std::vector<std::pair<double, double>>& polygon_xy,
    int mesh_type, double half_depth,
    const std::string& surface_texture, const std::string& front_texture,
    const std::string& identifier, const std::vector<Hat>& hats,
    const int* ids6 = nullptr);

// Per-node version, and the one the editors should prefer.
//
// A uniform half-depth is not how the engine builds terrain: GenerateMesh reads
// one depth per node and GenerateFrontMesh reads one depth per triangle corner.
// So an edit routed through the flat overload necessarily FLATTENS vanilla
// relief — the ground becomes a constant-thickness slab, which is the "boulderx
// strips the Z" symptom. This overload carries the outline through with each
// node's own front/back depth, plus the generator parameters recovered from the
// object (seed, HorizNoise, texture scale) rather than only its mesh type, and
// the object's dome hats, which are emitted rather than refused.
std::string generate_ground_mesh_object_nodes(
    const std::vector<Node>& outline, const Params& params,
    const std::string& identifier, const std::vector<Hat>& hats,
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
