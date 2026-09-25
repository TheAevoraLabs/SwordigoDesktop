#pragma once
// ============================================================================
// roller.h — Zypher Gen 3 Procedural World Mesh Engine
//
// Generation lineage:
//   Gen 1: boulder.h  / boulder.cpp  (flat slab, vanilla parity template)
//   Gen 2: boulderx.h / boulderx.cpp (Zenith — decompiled engine parity)
//   Gen 3: roller.h   / roller.cpp   (Zypher — full 3D computational geometry)
//
// Zypher builds on the same 32-byte interleaved vertex format
// (pos xyz + normal xyz + uv) and the same Caver protobuf wire fields
// (GroundPolygon=110, GroundMesh=111, Generator=112, TextureMapping=113,
//  CollisionShape=120/121) as its predecessors, but generates true 3D
// geometry by sweeping parameterized bevel profiles along Catmull-Rom splines
// with Bishop moving frames, applying procedural fBm/Worley Z-displacement
// for rock strata, and computing exact CCW collision silhouettes via
// alpha-shape extraction.
//
// Namespace: zypher
// Depends on: zypher_math.h, platform/protobuf_reader.h, tools/boulderx.h
//             (boulderx::triangulate only — for FrontMesh cap)
//
// Build:
//   Compile roller.cpp into the caver library alongside boulderx.cpp.
// ============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "tools/zypher_math.h"

namespace zypher {

// ── Wire constants (verified against libswordigo / scene_schemas.cpp) ─────
static constexpr int kWireGroundPolygon  = 110;
static constexpr int kWireGroundMesh     = 111;
static constexpr int kWireGenerator      = 112;
static constexpr int kWireTextureMapping = 113;
static constexpr int kWireShape          = 120;
static constexpr int kWireShapeComp      = 121;

// ── Vertex format (32 bytes, matches engine's VertexData stream) ───────────
struct Vertex {
    float x = 0, y = 0, z = 0;    ///< World-space position
    float nx = 0, ny = 0, nz = 1; ///< Outward surface normal
    float u = 0, v = 0;           ///< Texture coordinates
};

// ── Submesh (one material group inside GroundMeshComponent) ───────────────
struct Submesh {
    std::string kind;        ///< "SurfaceMesh" (field 8) | "FrontMesh" (field 9)
    std::string texture;     ///< Texture base name (no extension)
    bool indexed = true;     ///< true = indexed (surface), false = strips (front)
    std::vector<Vertex>   vertices;
    std::vector<uint16_t> indices;
};

// ── Bevel profile preset (alias to zypher_math.h ProfileKind) ────────────
using BevelProfile = ProfileKind;

// ── Terrain subsystem: spline control node ───────────────────────────────
struct TerrainNode {
    double x = 0.0;            ///< World X
    double y = 0.0;            ///< World Y
    double height = 80.0;      ///< Thickness along +Z from top surface
    double front_depth = 50.0; ///< Front Z plane
    double back_depth = -50.0; ///< Back Z plane
    double taper = 0.0;        ///< 0=parallel walls, 1=fully tapered to a ridge
    double noise_scale = 1.0;  ///< Local fBm amplitude multiplier (1 = default)
};

// ── Terrain subsystem parameters ─────────────────────────────────────────
struct TerrainParams {
    BevelProfile bevel      = BevelProfile::SmoothFillet;
    int    bevel_steps      = 8;       ///< Profile sample count (more = smoother)
    float  hat_height       = 20.0f;   ///< Rim bevel height (dy)
    float  hat_width        = 8.0f;    ///< Rim bevel outset (dz)
    int    spline_segs      = 80;      ///< Base segments per spline interval
    float  angle_tol_deg    = 4.0f;   ///< Adaptive tessellation angular tolerance
    double surface_width    = 90.0;   ///< Z-depth (front/back half-extent)
    float  fbm_amplitude    = 12.0f;  ///< Max fBm Z displacement on cliff face
    float  fbm_freq         = 0.010f; ///< Noise base frequency
    int    fbm_octaves      = 3;      ///< Noise octave count
    float  worley_blend     = 0.20f;  ///< Worley noise blend ratio
    float  alpha_for_hull   = 0.0f;   ///< 0 = convex hull for collision; >0 = alpha-shape
    float  texture_scale    = 250.0f; ///< UV scale for texture tiling
    std::string surface_tex = "fire_grass";       ///< Top/walkable surface
    std::string cliff_tex   = "graveyard_ground"; ///< Cliff/front face
    uint32_t seed           = 0xDEADBEEFu;
    bool   rear_closure     = true;    ///< Seal rear wall for watertight solid volume
};

// ── Parametric primitive parameters ──────────────────────────────────────
struct SphereParams {
    double radius        = 200.0;
    int    rings         = 16;
    int    sectors       = 24;
    double surface_width = 200.0;
    float  texture_scale = 250.0f;
    std::string texture  = "graveyard_ground";
};

struct ArchParams {
    double span         = 400.0;  ///< Full width
    double height       = 350.0;  ///< Arch crown height
    double thickness    = 60.0;   ///< Wall thickness
    double depth        = 90.0;   ///< Z-depth (half-extent)
    int    arch_segs    = 20;     ///< Arc segments
    float  texture_scale = 250.0f;
    std::string tex_top  = "graveyard_ground";
    std::string tex_face = "graveyard_ground";
};

struct TorusParams {
    double major_radius  = 200.0;
    double minor_radius  = 60.0;
    int    major_segs    = 24;
    int    minor_segs    = 12;
    double surface_width = 60.0;
    float  texture_scale = 250.0f;
    std::string texture  = "graveyard_ground";
};

struct PillarParams {
    double width   = 80.0;
    double height  = 300.0;
    double depth   = 80.0;
    int    bevel_n = 6;     ///< Segments on each corner bevel
    double bevel_r = 10.0;  ///< Bevel radius
    float  texture_scale = 250.0f;
    std::string texture  = "graveyard_ground";
};

struct DomeParams {
    double radius        = 240.0;
    double skirt_height  = 40.0;
    int    rings         = 20;
    int    sectors       = 48;
    float  texture_scale = 250.0f;
    std::string texture  = "graveyard_ground";
};

struct BevelPlatformParams {
    double half_width    = 360.0;
    double half_height   = 45.0;
    double bevel_radius  = 30.0;
    double depth         = 95.0;
    int    bevel_segs    = 12;
    float  texture_scale = 250.0f;
    std::string texture  = "graveyard_ground";
};

// ── Component IDs (matches boulderx::ComponentIds layout) ────────────────
struct ComponentIds {
    int polygon_id    = 980;
    int mesh_id       = 981;
    int generator_id  = 982;
    int collision_id  = 983;
    int tm_surface_id = 984;
    int tm_front_id   = 985;
};

// ── Result mesh bundle ────────────────────────────────────────────────────
struct GeneratedMesh {
    std::vector<Submesh> surfaces;  ///< SurfaceMesh entries (field 8)
    std::vector<Submesh> fronts;    ///< FrontMesh  entries (field 9)
    std::vector<std::pair<double,double>> collision_outline; ///< 2D CCW hull
    double depth_z = 90.0;         ///< Half-extent for collision Z
    bool ok = false;
    std::string error;

    // Axis-aligned bounding box over all geometry
    double aabb_x = 0, aabb_y = 0, aabb_w = 0, aabb_h = 0;
    size_t vertex_count() const;
    size_t triangle_count() const;
};

// ── Generator selector ────────────────────────────────────────────────────
enum class GroundGenerator {
    Boulder,   ///< Gen 1 flat slab (src/tools/boulder.cpp)
    BoulderX,  ///< Gen 2 engine-parity (src/tools/boulderx.cpp)
    Zypher,    ///< Gen 3 full 3D computational geometry (this file)
};

/// Thread-local generator choice (env: RUBY_GROUND_GENERATOR=zypher)
GroundGenerator ground_generator();
void            set_ground_generator(GroundGenerator which);
const char*     ground_generator_id(GroundGenerator which);
GroundGenerator ground_generator_from_id(const std::string& id);

/// Reason the last Zypher call declined (empty = success)
const std::string& last_decline_reason();

// ─────────────────────────────────────────────────────────────────────────────
// ZypherTerrain — Subsystem 1: Spline-swept terrain with bevel profiles
// ─────────────────────────────────────────────────────────────────────────────
//
// Input: a closed ring of TerrainNodes defining the 2D top-surface outline.
// The generator:
//   1. Evaluates centripetal Catmull-Rom splines through the control ring.
//   2. Sweeps the chosen BevelProfile cross-section along each spline
//      segment using Bishop moving frames (twist-free).
//   3. Applies multi-octave fBm + Worley Z-displacement to the cliff face.
//   4. Classifies triangles by normal angle to partition grass top vs cliff face.
//   5. Triangulates the top cap with ear-clip (boulderx::triangulate).
//   6. Extracts a 2D CCW alpha-shape collision hull.
//
/// Generate terrain geometry from a closed ring of control nodes.
GeneratedMesh generate_terrain(const std::vector<TerrainNode>& nodes,
                               const TerrainParams& params);

/// Full protobuf scene object bytes (wraps generate_terrain).
std::string generate_terrain_object(const std::vector<TerrainNode>& nodes,
                                    const TerrainParams& params,
                                    const std::string& identifier,
                                    const ComponentIds* ids = nullptr);

// ─────────────────────────────────────────────────────────────────────────────
// ZypherParametric — Subsystem 2: 3D Primitives
// ─────────────────────────────────────────────────────────────────────────────

/// UV sphere with outward normals and equatorial collision hull.
GeneratedMesh generate_sphere(const SphereParams& p);
std::string   generate_sphere_object(const SphereParams& p,
                                     const std::string& identifier,
                                     const ComponentIds* ids = nullptr);

/// Hollow vaulted arch with passable opening and walkable top.
GeneratedMesh generate_arch(const ArchParams& p);
std::string   generate_arch_object(const ArchParams& p,
                                   const std::string& identifier,
                                   const ComponentIds* ids = nullptr);

/// 3D torus ring with circular cross-section.
GeneratedMesh generate_torus(const TorusParams& p);
std::string   generate_torus_object(const TorusParams& p,
                                    const std::string& identifier,
                                    const ComponentIds* ids = nullptr);

/// Solid pillar with beveled corners.
GeneratedMesh generate_pillar(const PillarParams& p);
std::string   generate_pillar_object(const PillarParams& p,
                                     const std::string& identifier,
                                     const ComponentIds* ids = nullptr);

/// Curved dome hemisphere with skirt and curved collision contour.
GeneratedMesh generate_dome(const DomeParams& p);
std::string   generate_dome_object(const DomeParams& p,
                                   const std::string& identifier,
                                   const ComponentIds* ids = nullptr);

/// Flat-top beveled platform with radial corners.
GeneratedMesh generate_bevel_platform(const BevelPlatformParams& p);
std::string   generate_bevel_platform_object(const BevelPlatformParams& p,
                                             const std::string& identifier,
                                             const ComponentIds* ids = nullptr);

// ─────────────────────────────────────────────────────────────────────────────
// ZypherModel — Subsystem 3: 3D Model → GroundMesh Compiler
// ─────────────────────────────────────────────────────────────────────────────
//
// [NOTE / WARNING]:
// EXPERIMENTAL — DO NOT WIRE TO GUI FOR NOW.
// Earlier experiments converting arbitrary complex character PODs (e.g. hiro.POD)
// into GroundMesh caused engine crashes in vanilla Swordigo.
// Deeper research is required on vanilla GroundMeshRenderer limitations
// (multi-texture mappings, shader texture unit limits, vertex buffer formats).
//
// Converts a loaded PODModel or OBJ into GroundMesh SceneObject with:
//   - Per-material SurfaceMesh chunks (auto-splits at 65,535 vertices)
//   - Two-sided indexed geometry
//   - 2D CCW convex hull collision contour

struct ModelImportParams {
    float  scale         = 1.0f;    ///< Uniform model scale
    bool   two_sided     = true;    ///< Duplicate triangles with reversed winding
    float  texture_scale = 200.0f;
    float  alpha         = 0.0f;    ///< 0 = convex hull collision
    std::string default_tex = "graveyard_ground"; ///< Fallback when mesh has no material
};

struct ModelSubmesh {
    std::string texture;
    std::vector<std::array<float, 8>> verts; ///< pos(3) + norm(3) + uv(2)
    std::vector<uint32_t>             indices;
};

/// Low-level: build GeneratedMesh from pre-split submeshes.
/// Caller is responsible for 16-bit index overflow splitting.
GeneratedMesh generate_model_mesh(const std::vector<ModelSubmesh>& submeshes,
                                  const ModelImportParams& params);

/// Convenience: generate from path to a .POD file (uses pod_loader).
std::string generate_model_object_from_pod(const std::string& pod_path,
                                           const ModelImportParams& params,
                                           const std::string& identifier,
                                           const ComponentIds* ids = nullptr);

// ─────────────────────────────────────────────────────────────────────────────
// ZypherEmitter — shared protobuf emission
// ─────────────────────────────────────────────────────────────────────────────
/// Pack a GeneratedMesh into a complete SceneObject protobuf blob.
/// Adds GroundPolygon, GroundMesh, GroundMeshGenerator, CollisionShape,
/// and TextureMapping components for each unique texture.
std::string emit_scene_object(const GeneratedMesh& mesh,
                              const std::string& identifier,
                              double pos_x, double pos_y,
                              const ComponentIds& ids,
                              const std::vector<std::string>& extra_textures = {});

} // namespace zypher
