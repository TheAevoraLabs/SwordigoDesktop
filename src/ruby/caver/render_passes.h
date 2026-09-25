#pragma once
// render_passes.h — Caver::Scene::Draw, the renderer, recovered from
// libswordigo v1.4.13.
//
// Source: OpenSwordigo/arm64_13/functions/Caver/Scene/00000000004F905C__Draw.c
// (`Caver::Scene::Draw(Caver::RenderingContext*, Caver::Matrix4 const&)`), plus
// Scene::DrawGroundMeshes 0x4F8CF4 and Scene::DrawModels 0x4F8E80 for the two
// bucket rules.
//
// There is no render queue, no sorting pass and no material system. The whole
// renderer is one function that walks `ComponentManager` collections in
// interface order and issues draw calls, changing GL state around them. The
// recovered order is therefore the *only* thing that decides what occludes what,
// and it is:
//
//   1. Setup         view * camera.projection, cull on, depth test on
//   2. Lights        collect point lights; pick the directional one; set ambient
//   3. LightOverlay  prepare (frame, point lights, camera)
//   4. Opaque        blending off, lighting on, program 1:
//                    DrawGroundMeshes(pass 0) then DrawModels(cull 0, pass 0)
//   5. Transparent   blending on, default blend func, *depth write off*:
//                    DrawGroundMeshes(pass 1) then DrawModels(cull 0, pass 1)
//   6. Water         WaterMeshComponent::Draw, tinted by the scene color
//   7. ShadowVolumes stencil increment/decrement, only with a directional light
//   8. LightOverlay  draw, under its own orthographic projection, depth test off
//   9. ModelsAgain   DrawModels(cull 1, pass 0) with glDepthFunc(GL_LEQUAL)
//  10. Background    orthographic square from the camera's half-extent
//  11. Shadows       batched sprite shadows via DrawArrays, cull off, program 3
//  12. WeaponTrails
//  13. Glows
//  14. Particles
//  15. Sprites
//  16. HealthBars
//  17. MagicExplosions
//  18. DebugDraw     only when the scene's debug byte is set — object bounds,
//                    the collision-magenta set, and the collision grid
//  19. Overlays      orthographic, last, and skipped by one scene flag
//
// Two properties of this order are worth stating because they are not guessable:
//
//   * **Ground meshes and models are drawn twice, and the split is a per
//     component byte, not an alpha test.** `DrawGroundMeshes` (0x4F8CF4) draws
//     the components whose `+184` equals the pass argument exactly —
//     `((component[184] != 0) ^ pass) & 1 == 0` — so a mesh is in the opaque pass
//     or the blended pass and never in both, and depth writing is what changes
//     between the passes, not the geometry.
//   * **The debug draw is part of the frame.** Objects whose collision byte
//     `+128` was set by this frame's narrowphase are drawn magenta, everything
//     else white, and the flag is *consumed* (cleared) as they are drawn. What
//     the player sees as "the collision debug overlay" is Scene::Draw, not a
//     separate tool.
//
// ── Scene / Camera fields recovered from these three functions ──────────────
//
//     Camera +16   Vector3   focus / target offset
//     Camera +44   Matrix4   view matrix
//     Camera +108  Matrix4   projection matrix
//     Camera +240  float     half-extent, for the orthographic UI square
//     Scene  +160  float     ambient light scalar
//     Scene  +464  LightOverlay
//     Scene  +496  Color     ambient light color
//     Scene  +500  Color     ambient light color (resolved)
//     Scene  +652  Rectangle cull bounds
//     Scene  +676  Rectangle camera z AABB
//     Scene  +720  bool      debug draw enabled
//     Scene  +792  SceneGrid collision shapes (the debug grid draw)
//     Scene  +856  int       shadow batch vertex count
//     Scene  +872  bool      "scene has a custom color"
//     Scene  +876  FloatColor scene tint (used by water, glows, particles)
//     Scene  +892  bool      overlays disabled
//     GroundMeshComponent +184 bool  blended-pass selector
//     SceneObject +128  bool  collided-this-frame (consumed by the debug draw)
//     SceneObject +416  bool  registered in the collision grid

#include <cstdint>
#include <vector>

namespace caver {
namespace render {

// ── Recovered GL constants (decimal as they appear in the decompilation) ────
//
// Named here rather than included from a GL header: this module is pure data and
// has to be testable without a GL context, and the point of the table is that
// these exact values are what the engine passes.
inline constexpr int kMaxLightsEnum = 3377;            // GL_MAX_LIGHTS
inline constexpr int kLightModelAmbientEnum = 2899;    // GL_LIGHT_MODEL_AMBIENT
inline constexpr int kLightPositionEnum = 4611;        // GL_POSITION
inline constexpr int kLightDiffuseEnum = 4609;         // GL_DIFFUSE
inline constexpr int kLightSpecularEnum = 4610;        // GL_SPECULAR
inline constexpr int kLight0EnableBase = 0x4000;       // GL_LIGHT0
inline constexpr int kStencilBufferBit = 1024;         // GL_STENCIL_BUFFER_BIT
inline constexpr int kStencilTestEnum = 2960;          // GL_STENCIL_TEST
inline constexpr int kAlwaysEnum = 519;                // GL_ALWAYS
inline constexpr int kEqualEnum = 513;                 // GL_EQUAL
inline constexpr int kKeepEnum = 7680;                 // GL_KEEP
inline constexpr int kIncrEnum = 7682;                 // GL_INCR
inline constexpr int kDecrEnum = 7683;                 // GL_DECR
inline constexpr int kFrontFaceEnum = 1029;            // GL_FRONT
inline constexpr int kBackFaceEnum = 1028;             // GL_BACK
inline constexpr int kLequalEnum = 514;                // GL_LEQUAL
inline constexpr int kLessEnum = 513;                  // GL_LESS
inline constexpr int kOneEnum = 1;                     // GL_ONE
inline constexpr int kOneMinusSrcAlphaEnum = 771;      // GL_ONE_MINUS_SRC_ALPHA

// Layer 3 ground meshes are drawn in the blended pass; the selector byte is
// GroundMeshComponent+184 and the rule is an equality test against the pass
// index, so the two passes partition the collection.
inline constexpr int kGroundMeshPassSelectorOffset = 184;

// Shader programs, by the index the engine passes to UseProgram: 0 is the plain
// color/glyph program (shadow volumes, debug draw), 1 the lit 3D program,
// 2 the untextured sprite/glow program, 3 the textured sprite/particle program.
inline constexpr int kProgramColor = 0;
inline constexpr int kProgramLit = 1;
inline constexpr int kProgramSprite = 2;
inline constexpr int kProgramTextured = 3;

// ── Light kinds ─────────────────────────────────────────────────────────────
//
// Two different discriminators, both recovered: LightComponent+96 selects the
// *GL light slot* role and +104 the *geometry* role.
enum class LightRole : int32_t {
    Other = 0,
    Ambient = 1,      // +104 == 1: contributes the GL_LIGHT_MODEL_AMBIENT term
    Directional = 2,  // +104 == 2: occupies a GL_LIGHT0+i slot, and shadows it
    Point = 3,        // +96  == 3: a point light handed to PointLightManager
    AmbientColor = 4, // +96  == 4: the scene's ambient color source
};

// ── The pass list ───────────────────────────────────────────────────────────
enum class Pass {
    Setup,
    Lights,
    LightOverlayPrepare,
    OpaqueGeometry,
    TransparentGeometry,
    Water,
    ShadowVolumes,
    LightOverlayDraw,
    ModelsSecondPass,
    Background,
    ShadowSprites,
    WeaponTrails,
    Glows,
    Particles,
    Sprites,
    HealthBars,
    MagicExplosions,
    DebugDraw,
    Overlays,
};

// The recovered order. Never reorder; a renderer that draws these out of order
// diverges from the engine in exactly the way that is hardest to see.
const std::vector<Pass>& pass_order();
const char* pass_name(Pass pass);
int pass_index(Pass pass);
bool draws_before(Pass a, Pass b);

// ── Pure policies recovered from the same three functions ───────────────────

// `if ((((component[184] != 0) ^ pass) & 1) == 0)` — DrawGroundMeshes 0x4F8CF4.
// A mesh belongs to the opaque pass when its selector byte is 0 and to the
// blended pass when it is not; the two never overlap.
bool ground_mesh_in_pass(int selector_byte, int pass_index);

// `if (v232 && *((_BYTE *)a2 + 72))` — a directional light must have been
// selected *and* the context must have stencil available.
bool uses_stencil_shadows(bool has_directional_light, bool context_has_stencil);

// `(component[184] != 0) ^ pass` for DrawModels: the model pass argument is a
// separate boolean (`a5`) and models are drawn in all three model passes.
// The scene's debug byte gates the whole of pass 18.
bool debug_draw_enabled(bool scene_debug_flag);
// `if (!*((_BYTE *)this + 892))` gates the overlay pass.
bool overlays_enabled(bool scene_overlays_disabled);

// The tint the water/glow/particle passes use: the scene color when the scene
// says it has one (+872), plain white otherwise.
struct FloatColor { float r, g, b, a; };
FloatColor scene_tint(bool scene_has_color, float r, float g, float b, float a);

// The opaque pass writes depth and does not blend; the blended pass blends with
// the default func and leaves the depth buffer alone.
struct PassState { bool blending, depth_write, depth_test; };
PassState opaque_pass_state();
PassState transparent_pass_state();

// The debug draw's recovered colors, as the engine's packed values.
inline constexpr uint32_t kDebugColorDefault = 0xFFFFFFFFu;   // -1
inline constexpr uint32_t kDebugColorCollided = 0xFF00FFFFu;  // -16776961, magenta
inline constexpr uint32_t kDebugColorObjectRect = 0x64646464u; // 1684300900
inline constexpr uint32_t kShadowColor = 0x50000000u;          // 1342177280
// The shadow batch's vertex layout, from the three SetVertexAttribPointer calls:
// stride 24, position 3 floats at 0, uv 2 floats at 12, color 4 bytes at 20.
inline constexpr int kShadowVertexStride = 24;
inline constexpr int kShadowPositionOffset = 0;
inline constexpr int kShadowUvOffset = 12;
inline constexpr int kShadowColorOffset = 20;

} // namespace render
} // namespace caver
