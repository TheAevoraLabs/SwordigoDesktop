#pragma once
// frame_runner.h — Caver::Scene::Update, the frame runner, recovered from
// libswordigo v1.4.13.
//
// Source: OpenSwordigo/arm64_13/functions/Caver/Scene/00000000004F81AC__Update.c
// (`Caver::Scene::Update(float)`). This is the whole per-frame order of the
// shipped engine, and it is short enough to state exactly:
//
//   1. frame counter, dt, pause latch
//   2. **the program pass** — ProgramState::Update on the scene's root state
//   3. camera culling volume (-300..300 in z, outset 200)
//   4. the two scene grids' visible areas, then activate the objects the grid
//      reports near the camera (200)
//   5. **the object pass** — SceneObject::updateSpeedMultiplier, then
//      SceneObject::Update(obj, speed * dt, paused, flag)
//   6. collision broadphase: clear last frame's registrations, re-place every
//      live shape in the grid, collect the shapes into a pending array
//   7. ComponentManager::PurgeInactive  (+392)
//   8. collision narrowphase: for each pending shape, grid query within its
//      rect, rect test, shape test, CollisionPairSet::RegisterCollision, then
//      SceneObject::HandleMessage(7) on *both* objects
//   9. CollisionPairSet::Purge (+272) -- only when dt > 0.001 and not paused
//  10. ComponentManager::Purge  (+344)
//
// The two orderings worth spelling out, because both were wrong in this port:
//
//   * **Scripts run before components, not after.** Step 2 precedes step 5, so
//     a Lua program resumed this frame observes the component state its owner
//     had at the *end of the previous* frame. The port's "SCENE_OBJECT_UPDATE
//     then PROGRAM_UPDATE" was an assumption; the binary does the opposite.
//   * **Collision is three separate phases**, not a callback inside the object
//     pass: broadphase registration (6), narrowphase pairs (8), and a pair purge
//     that is skipped entirely while paused or on an almost-zero step (9). The
//     purge is what keeps the contact set from surviving a pause.
//
// ── Scene field offsets recovered from the same function ────────────────────
//
//     +32   int      m_pause_depth      `*((_DWORD *)this + 8)`
//     +36   float    m_dt               `*((float *)this + 9)`
//     +40   ProgramState m_root_state   `ProgramState::Update(this + 40)`
//     +128  bool     m_paused           `*((_BYTE *)this + 128) = pause_depth > 0`
//     +232  list     m_objects
//     +248  size_t   m_object_count
//     +256  int      m_pending_pair_shape_count
//     +264  array    m_pending_pair_shapes
//     +272  CollisionPairSet
//     +344  ComponentManager  (objects)
//     +392  ComponentManager  (groups)
//     +440  Camera*    m_camera
//     +648  int        m_frame_counter
//     +652  Rectangle  m_cull_bounds     (camera AABB outset by 200)
//     +668  float      m_cull_z_min      (-300)
//     +672  float      m_cull_z_max      (+300)
//     +676  Rectangle  m_camera_z_aabb
//     +728  SceneGrid  (objects)
//     +792  SceneGrid  (collision shapes)
//
// `ProgramState::Update(this + 40)` also pins ProgramState's size to 96: its own
// constructor allocates 0x60 bytes and the scene embeds one by value.

#include <cstdint>
#include <string>
#include <vector>

namespace caver {
namespace frame {

// ── Recovered tuning constants (Scene::Update 0x4F81AC) ─────────────────────
//
// The camera query is `Camera::AABBForZRange(camera, -300.0, 300.0)` written as
// the two-float literal 0x43960000C3960000, i.e. (-300, +300). The cull rect is
// `Rectangle::RectangleByOutsetting(&z_aabb, 200.0)`, and the activation query
// is `SceneGrid::GetObjectsNearCamera(grid, 200.0, &out)`.
inline constexpr float kCullZRange = 300.0f;
inline constexpr float kCullOutset = 200.0f;
inline constexpr float kActivateRadius = 200.0f;
// `if (a2 > 0.001 && *((int *)this + 8) <= 0)` guarding CollisionPairSet::Purge.
inline constexpr float kPairPurgeMinDt = 0.001f;
// `SceneObject::HandleMessage(obj, 7, &pair)` — 7 is the collision message.
inline constexpr int kCollisionMessage = 7;
// The one layer id the narrowphase treats specially: when both shapes are on
// layer 3 the *smaller* one is tested against the larger, and the pair order is
// swapped to keep the pair set canonical (`if (*(v85 + 104) == 3)`).
inline constexpr int kLayerSpecialCase = 3;

// ── The pass list ───────────────────────────────────────────────────────────
//
// Exposed as data so a caller can check its own frame against the recovered
// order instead of restating it in prose. `scripts_before_objects` is the single
// boolean that distinguishes a faithful frame from the port's old one.
enum class Pass {
    FramePrologue,        // counter, dt, pause latch
    ProgramUpdate,        //  0x4F81AC+0x60  ProgramState::Update(root, dt)
    CullVolume,           //  Camera::AABBForZRange + RectangleByOutsetting
    GridVisibleAreas,     //  SceneGrid::UpdateVisibleAreasWithCamera (x2)
    ActivateNearCamera,   //  SceneGrid::GetObjectsNearCamera + ActivateObject
    ObjectUpdate,         //  SceneObject::updateSpeedMultiplier + Update
    CollisionBroadphase,  //  grid placement + pending shape array
    PurgeInactive,        //  ComponentManager::PurgeInactive
    CollisionNarrowphase, //  GetObjectsInBounds → rect → shape → HandleMessage(7)
    PairPurge,            //  CollisionPairSet::Purge (dt > 0.001 and not paused)
    ComponentPurge,       //  ComponentManager::Purge
};

// The recovered order, in frame order. Never reorder this list; a front-end that
// wants a different order is a front-end that diverges from the engine, and
// `scripts_before_objects()` / `runs_before()` are the predicates that say so.
const std::vector<Pass>& frame_order();
// Stable short name per pass, for logs and tests.
const char* pass_name(Pass pass);

// Index of `pass` in the recovered order, or -1.
int pass_index(Pass pass);
// True when `a` runs before `b` in the engine's frame. This is the assertion the
// port's runtime should be able to make (and could not, before).
bool runs_before(Pass a, Pass b);

// Pass 2 really does precede pass 5: scripts see the previous frame's component
// state. Named rather than inlined so the claim is testable.
bool scripts_before_objects();

// ── Policies that are pure functions of the recovered expressions ───────────

// `dt > 0.001 && pause_depth <= 0`. A paused frame never purges the contact set,
// and neither does a frame with a step at or below a millisecond.
bool should_purge_pairs(float dt, int pause_depth);

// `pause_depth > 0`, computed once per frame into Scene+128 and passed to every
// SceneObject::Update.
bool is_paused(int pause_depth);

// The z slab the camera query returns, as (min, max).
struct ZRange { float min; float max; };
ZRange cull_z_range();

} // namespace frame
} // namespace caver
