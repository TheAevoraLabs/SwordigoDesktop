// caver_program_state_test — the recovered ProgramState and the frame runner.
//
// Two things are pinned here, both read off the arm64_13 decompilation and
// neither of them invented:
//
//  1. `Caver::ProgramState` is a *scheduler node*, not a script wrapper. Its
//     Update (0x545480) is the whole reason a script runs when it does: a gate,
//     a dt scale, a wait countdown, the child recursion, and an abort sweep. The
//     resume rule is unforgiving — ProgramState::Resume (0x545624) aborts unless
//     lua_resume returned exactly LUA_YIELD(1) — and the only way a coroutine is
//     ever revived is a *stored* wait (`Program.Wait` → ProgramState::Wait
//     0x5457AC), never a value returned across the yield.
//
//     The port used to do the opposite of both: it read the wait duration off the
//     yielded stack, and it treated "returned" as a restartable keep-active
//     program. Those two guesses are why a scene's scripts could drift out of
//     step with its components.
//
//  2. `Caver::Scene::Update` (0x4F81AC) runs the **program pass before the object
//     pass**, then three separate collision phases. The port ran components first
//     and merged collision into the object tick.
//
// Pure RAM: no guest, no GL, no Qt.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ruby/caver/frame_runner.h"
#include "ruby/caver/program_state.h"
#include "ruby/caver/render_passes.h"

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

using namespace caver;
using namespace caver::prog;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A scheduler node with the recovered constructor defaults (0x544C68):
// +89 = 1, +90 = 1, +88 = 0, +91 = 0, +92 = 1.0f, status Running, no wait.
static Scheduler::Node make_node() { return Scheduler::Node{}; }

int main() {
    std::printf("caver program state + frame runner\n");

    // ── Value marshalling: the engine's own type discipline ─────────────────
    {
        // IsIntAtStackIndex 0x5458A8 and IsFloatAtStackIndex 0x5458E0 are the
        // same predicate. Both are lua_isnumber, so neither distinguishes an
        // integer from a float — that distinction exists only when *pushing*.
        check(is_int_at_stack_index(ValueKind::Number),
              "IsIntAtStackIndex accepts a Lua number");
        check(is_float_at_stack_index(ValueKind::Number),
              "IsFloatAtStackIndex accepts a Lua number");
        check(is_int_at_stack_index(ValueKind::Number) == is_float_at_stack_index(ValueKind::Number),
              "IsInt and IsFloat are the same predicate (both lua_isnumber)");
        check(!is_int_at_stack_index(ValueKind::Boolean) &&
              !is_float_at_stack_index(ValueKind::Boolean),
              "neither number predicate accepts a bool");
        check(!is_int_at_stack_index(ValueKind::String) &&
              !is_float_at_stack_index(ValueKind::UserData),
              "neither number predicate coerces a string or userdata");

        // IsBoolAtStackIndex 0x54591C is a strict type test, and
        // BoolAtStackIndex 0x54593C additionally requires lua_toboolean.
        check(is_bool_at_stack_index(ValueKind::Boolean), "IsBoolAtStackIndex accepts a bool");
        check(!is_bool_at_stack_index(ValueKind::Number),
              "IsBoolAtStackIndex rejects a number, even a truthy one");
        check(bool_at_stack_index(ValueKind::Boolean, true), "BoolAtStackIndex reads a true bool");
        check(!bool_at_stack_index(ValueKind::Boolean, false),
              "BoolAtStackIndex rejects a false bool");
        check(!bool_at_stack_index(ValueKind::Number, true),
              "BoolAtStackIndex does not accept a truthy number where a bool is wanted");

        // Pop 0x54575C computes `~n`, which is the same as -(n+1) for n >= 1.
        check(pop_settop_argument(1) == -2, "Pop(1) is lua_settop(L, -2)");
        check(pop_settop_argument(2) == -3, "Pop(2) is lua_settop(L, -3)");
        check(pop_settop_argument(0) == -1,
              "Pop(0) is lua_settop(L, -1) — not a no-op, exactly as the engine computes it");

        // Geometry userdata: ProgramMath::PushVector3 0x545ECC / PushRectangle
        // 0x545F98 allocate and name the payloads.
        check(kVector3Bytes == 12, "Vector3 userdata payload is 12 bytes");
        check(kRectangleBytes == 16, "Rectangle userdata payload is 16 bytes");
        check(std::string(kVector3Metatable) == "Vector3" &&
              std::string(kRectangleMetatable) == "Rectangle",
              "the userdata metatable names are the recovered strings");

        check(kRegistryPseudoIndex == -10000 && kGlobalsPseudoIndex == -10002,
              "ProgramTable's two pseudo-indices are LUA_REGISTRYINDEX / LUA_GLOBALSINDEX");
        check(kDefaultTimeScale == 1.0f, "a new state's time scale (+92) is 1.0f");
    }

    // ── Resume semantics ────────────────────────────────────────────────────
    {
        check(kLuaYield == 1, "LUA_YIELD is 1, the value the engine compares against");
        check(resume_outcome(kLuaYield) == ResumeOutcome::Yielded,
              "a yield keeps the state alive");
        check(resume_outcome(0) == ResumeOutcome::Aborted,
              "a clean return aborts the state — keep-alive scripts must loop");
        check(resume_outcome(2) == ResumeOutcome::Aborted,
              "a runtime error aborts the state");

        // Execute 0x5456E0 branches on whether the state has a parent.
        check(execute_path(true) == ExecutePath::ResumeCoroutine,
              "a child state is a coroutine, so Execute resumes it");
        check(execute_path(false) == ExecutePath::CallOnStack,
              "the root state is an ordinary lua_State, so Execute pcalls it");
    }

    // ── dt scaling: two multiplications in a fixed order ────────────────────
    {
        check(near(Scheduler::scaled_dt(0.5f, 0.5f, 2.0f), 0.5f),
              "dt is scaled by the owner's speed multiplier then the state's time scale");
        check(near(Scheduler::scaled_dt(0.1f, 1.0f, 0.25f), 0.025f),
              "a quarter-speed state sees a quarter dt");
        check(near(Scheduler::scaled_dt(0.016f, 1.0f, kDefaultTimeScale), 0.016f),
              "the default time scale leaves dt alone");
    }

    // ── The gate ────────────────────────────────────────────────────────────
    {
        Scheduler::Node node = make_node();
        check(Scheduler::state_updates(node),
              "a fresh state ticks (the constructor sets +89 and +90 to 1)");

        SchedulerStats stats;
        int resumes = 0;
        Scheduler::update(node, 0.016f, [&](Scheduler::Node&) { ++resumes; return ResumeOutcome::Yielded; }, &stats);
        check(stats.visited == 1 && stats.gated == 0, "an ungated state is visited");

        // ProgramComponent::HandleMessage (0x33D778+205) is the only write to +89
        // in the whole binary: Program.SetKeepActive(false).
        node.runs = false;
        node.runs_when_gated = false;
        stats = SchedulerStats{};
        Scheduler::update(node, 0.016f, [&](Scheduler::Node&) { ++resumes; return ResumeOutcome::Yielded; }, &stats);
        check(stats.gated == 1 && stats.visited == 0,
              "clearing both flags gates the state out of the frame entirely");

        node.runs = false;
        node.runs_when_gated = true;
        check(Scheduler::state_updates(node), "either flag alone is enough to tick");
    }

    // ── The wait countdown, including its strict inequality ─────────────────
    {
        Scheduler::Node node = make_node();
        int resumes = 0;
        Scheduler::wait(node, 0.2f);
        check(node.status == Status::Waiting, "Wait sets status Waiting (+80 = 1)");

        Scheduler::update(node, 0.1f, [&](Scheduler::Node&) { ++resumes; return ResumeOutcome::Yielded; });
        check(resumes == 0, "0.2s of wait does not expire on a 0.1s step");
        check(near(node.wait_remaining, 0.1f), "the wait is counted down, not replaced");

        Scheduler::update(node, 0.1f, [&](Scheduler::Node&) { ++resumes; return ResumeOutcome::Yielded; });
        check(resumes == 0,
              "a countdown that lands exactly on 0.0f does not resume — the test is `< 0.0`");
        check(near(node.wait_remaining, 0.0f), "the remainder is kept, not clamped");

        Scheduler::update(node, 0.1f, [&](Scheduler::Node&) { ++resumes; return ResumeOutcome::Yielded; });
        check(resumes == 1, "once the remainder goes negative the coroutine is resumed");
        check(node.status == Status::Running, "resuming clears the Waiting status");
        check(!node.abort_requested, "a yielding resume leaves the state alive");
    }

    // ── A state that is not Waiting is never resumed by the scheduler ───────
    // This is the recovered reason a yield outside Program.Wait hangs: Update
    // only enters the resume path for status == Waiting.
    {
        Scheduler::Node node = make_node();
        int resumes = 0;
        node.status = Status::Running;
        Scheduler::update(node, 0.016f, [&](Scheduler::Node&) { ++resumes; return ResumeOutcome::Yielded; });
        check(resumes == 0, "a Running state is not resumed by Update (only a wait revives one)");
    }

    // ── Abort on a non-yield resume ─────────────────────────────────────────
    {
        Scheduler::Node node = make_node();
        Scheduler::wait(node, 0.0f);
        Scheduler::update(node, 0.1f, [&](Scheduler::Node&) { return ResumeOutcome::Aborted; });
        check(node.abort_requested, "a resume that is not a yield sets the abort flag (+91)");
        // Resume 0x545624 sets the flag and nothing else: it clears status to
        // Running immediately before lua_resume and never writes Aborted. Only
        // ProgramState::Abort (0x5457BC) writes 2, and only an external caller
        // reaches it. So the flag is the abort, not the status.
        check(node.status == Status::Running,
              "a failed resume leaves status Running and flags the abort instead");
        check(!node.abort_requested == false, "the flag is observable to the sweep");
    }

    // ── Children: same scaled dt, gate, and the abort sweep ─────────────────
    {
        Scheduler::Node root = make_node();
        root.owner_speed_multiplier = 0.5f;   // e.g. a slowed owner
        root.time_scale = 1.0f;

        Scheduler::Node child = make_node();
        Scheduler::wait(child, 0.1f);
        root.children.push_back(&child);

        int child_resumes = 0;
        // The child's own steps are the parent's scaled dt: 0.016 * 0.5 = 0.008.
        Scheduler::update(root, 0.016f, [&](Scheduler::Node& n) {
            if (&n == &child) ++child_resumes;
            return ResumeOutcome::Yielded;
        });
        check(near(child.wait_remaining, 0.092f),
              "a child's wait is counted down with the parent's scaled dt, not the frame dt");
        check(child_resumes == 0, "the child has not expired yet");

        // Enough frames to cross zero.
        for (int i = 0; i < 12; ++i)
            Scheduler::update(root, 0.016f, [&](Scheduler::Node& n) {
                if (&n == &child) ++child_resumes;
                return ResumeOutcome::Yielded;
            });
        check(child_resumes == 1, "the child resumes once its wait crosses zero");

        // The parent's +88 gates children that are not +90.
        Scheduler::Node gated_child = make_node();
        gated_child.runs_when_gated = false;
        Scheduler::Node gating_root = make_node();
        gating_root.gates_children = true;
        gating_root.children.push_back(&gated_child);
        check(!Scheduler::child_steps(gating_root, gated_child),
              "a gating parent skips a child that does not run when gated");
        check(Scheduler::child_steps(root, gated_child),
              "a non-gating parent steps it anyway");

        // The sweep unlinks an aborted child without disturbing its siblings.
        // Only a *waiting* child is ever resumed, so the doomed one has to be
        // waiting for its abort to be reachable at all — the same reason a
        // resume is the only thing that can kill a state.
        Scheduler::Node keep = make_node();
        Scheduler::Node doomed = make_node();
        Scheduler::wait(doomed, 0.0f);
        Scheduler::Node root2 = make_node();
        root2.children = {&keep, &doomed};
        SchedulerStats stats;
        Scheduler::update(root2, 0.016f, [&](Scheduler::Node& n) {
            if (&n == &doomed) return ResumeOutcome::Aborted;
            return ResumeOutcome::Yielded;
        }, &stats);
        check(root2.children.size() == 1 && root2.children[0] == &keep,
              "an aborted child is unlinked from its parent's list");
        check(stats.aborted == 1, "the sweep reports the removal");
        check(root2.children.size() + stats.aborted == 2, "no child is lost or double-counted");

        // A child that is not waiting is never resumed, so a resume callback that
        // would abort it never runs and the child survives. This is the other half
        // of "only a wait revives a state": it also means only a wait can end one.
        Scheduler::Node quiet = make_node();
        Scheduler::Node root3 = make_node();
        root3.children = {&quiet};
        SchedulerStats idle;
        Scheduler::update(root3, 0.016f, [&](Scheduler::Node&) { return ResumeOutcome::Aborted; }, &idle);
        check(root3.children.size() == 1 && !quiet.abort_requested,
              "a child that is never resumed is never swept");
        check(idle.aborted == 0 && idle.resumed == 0, "and it is not counted as either");
    }

    // ══ The frame runner ════════════════════════════════════════════════════
    using namespace caver::frame;

    {
        const std::vector<Pass>& order = frame_order();
        check(order.size() == 11, "the recovered frame has eleven passes");

        // Uniqueness and index agreement: a duplicated pass would make
        // runs_before() silently meaningless.
        bool unique = true;
        for (size_t i = 0; i < order.size(); ++i) {
            if (pass_index(order[i]) != static_cast<int>(i)) unique = false;
            for (size_t j = i + 1; j < order.size(); ++j)
                if (order[i] == order[j]) unique = false;
        }
        check(unique, "every pass appears exactly once, in index order");

        bool named = true;
        for (Pass p : order)
            if (std::string(pass_name(p)) == "unknown") named = false;
        check(named, "every pass has a stable name");
    }

    {
        // The two orderings this port had wrong.
        check(scripts_before_objects(),
              "the program pass precedes the object pass (Scene::Update 0x4F81AC)");
        check(runs_before(Pass::ProgramUpdate, Pass::ObjectUpdate),
              "ProgramUpdate runs before ObjectUpdate");
        check(runs_before(Pass::ObjectUpdate, Pass::CollisionBroadphase),
              "collision broadphase follows the object pass");
        check(runs_before(Pass::CollisionBroadphase, Pass::PurgeInactive),
              "PurgeInactive sits between the collision phases");
        check(runs_before(Pass::PurgeInactive, Pass::CollisionNarrowphase),
              "narrowphase follows PurgeInactive");
        check(runs_before(Pass::CollisionNarrowphase, Pass::PairPurge),
              "the pair purge is the last collision phase");
        check(runs_before(Pass::PairPurge, Pass::ComponentPurge),
              "ComponentManager::Purge is the last thing in the frame");
        check(runs_before(Pass::FramePrologue, Pass::ProgramUpdate),
              "the counter/dt/pause prologue opens the frame");
        check(!runs_before(Pass::ObjectUpdate, Pass::ProgramUpdate),
              "the old components-first order is not what the engine does");
    }

    {
        // `if (dt > 0.001 && pause_depth <= 0) CollisionPairSet::Purge(...)`
        check(should_purge_pairs(0.016f, 0), "an ordinary frame purges the pair set");
        check(!should_purge_pairs(0.016f, 1), "a paused frame keeps its contact set");
        check(!should_purge_pairs(0.016f, 3), "a nested pause keeps it too");
        check(!should_purge_pairs(0.001f, 0),
              "a step at exactly the 0.001 threshold does not purge (`>` not `>=`)");
        check(!should_purge_pairs(0.0f, 0), "a zero step does not purge");
        check(should_purge_pairs(0.0011f, -1), "a negative pause depth is not paused");

        check(!is_paused(0), "pause depth 0 is running");
        check(is_paused(1), "pause depth 1 is paused");
        check(!is_paused(-1), "the test is `pause_depth > 0`, so a negative depth runs");

        check(near(cull_z_range().min, -300.0f) && near(cull_z_range().max, 300.0f),
              "the camera culls a -300..300 z slab");
        check(near(kCullOutset, 200.0f),
              "the cull rectangle is the camera AABB outset by 200");
        check(near(kActivateRadius, 200.0f),
              "objects are activated within 200 of the camera");
        check(kCollisionMessage == 7, "collision is SceneObject message 7");
        check(kLayerSpecialCase == 3,
              "layer 3 is the one layer the narrowphase reconciles explicitly");
    }

    // ══ The renderer ════════════════════════════════════════════════════════
    using namespace caver::render;
    // The enum is qualified because caver::frame::Pass exists too; the rest of
    // the render helpers are used unqualified via the using-directive above.
    namespace R = caver::render;

    {
        const std::vector<R::Pass>& order = pass_order();
        check(order.size() == 19, "the recovered renderer has nineteen passes");

        bool unique = true, named = true;
        for (size_t i = 0; i < order.size(); ++i) {
            if (pass_index(order[i]) != static_cast<int>(i)) unique = false;
            if (std::string(pass_name(order[i])) == "unknown") named = false;
            for (size_t j = i + 1; j < order.size(); ++j)
                if (order[i] == order[j]) unique = false;
        }
        check(unique, "every render pass appears exactly once, in index order");
        check(named, "every render pass has a stable name");

        // The order that decides occlusion. Each of these is a claim from
        // Scene::Draw 0x4F905C, not a preference.
        check(draws_before(R::Pass::Setup, R::Pass::Lights), "state setup precedes the light pass");
        check(draws_before(R::Pass::Lights, R::Pass::OpaqueGeometry),
              "lights are resolved before any geometry is drawn");
        check(draws_before(R::Pass::OpaqueGeometry, R::Pass::TransparentGeometry),
              "the opaque pass is drawn before the blended one");
        check(draws_before(R::Pass::TransparentGeometry, R::Pass::Water), "water draws after both mesh passes");
        check(draws_before(R::Pass::Water, R::Pass::ShadowVolumes), "stencil shadows follow the water");
        check(draws_before(R::Pass::ShadowVolumes, R::Pass::LightOverlayDraw),
              "the light overlay is drawn after the shadow volumes");
        check(draws_before(R::Pass::LightOverlayDraw, R::Pass::ModelsSecondPass),
              "the second model pass comes after the light overlay, not before");
        check(draws_before(R::Pass::ModelsSecondPass, R::Pass::Background),
              "the background is drawn after the second model pass");
        check(draws_before(R::Pass::Background, R::Pass::ShadowSprites),
              "sprite shadows draw over the background");
        check(draws_before(R::Pass::ShadowSprites, R::Pass::WeaponTrails), "weapon trails follow the shadows");
        check(draws_before(R::Pass::Glows, R::Pass::Particles), "glows are drawn before particles");
        check(draws_before(R::Pass::Particles, R::Pass::Sprites), "sprites are drawn after particles");
        check(draws_before(R::Pass::Sprites, R::Pass::HealthBars), "health bars are drawn over sprites");
        check(draws_before(R::Pass::HealthBars, R::Pass::MagicExplosions),
              "magic explosions are the last of the world-space draws");
        check(draws_before(R::Pass::MagicExplosions, R::Pass::DebugDraw), "the debug draw is second to last");
        check(draws_before(R::Pass::DebugDraw, R::Pass::Overlays), "overlays are the last pass in the frame");
        check(!draws_before(R::Pass::Overlays, R::Pass::Setup), "and nothing follows them");
    }

    {
        // The ground-mesh split is an equality test against the pass index, so
        // the two passes partition the collection — a mesh is never in both.
        check(ground_mesh_in_pass(0, 0), "a ground mesh with selector 0 draws in the opaque pass");
        check(!ground_mesh_in_pass(0, 1),
              "...and not in the blended pass");
        check(ground_mesh_in_pass(1, 1), "selector 1 draws in the blended pass");
        check(!ground_mesh_in_pass(1, 0), "...and not in the opaque pass");
        bool partitioned = true;
        for (int selector = 0; selector <= 1; ++selector) {
            int in = 0;
            for (int p = 0; p <= 1; ++p)
                if (ground_mesh_in_pass(selector, p)) ++in;
            if (in != 1) partitioned = false;
        }
        check(partitioned, "every selector byte lands in exactly one of the two mesh passes");

        // Pass state: the blended pass does not write depth, and that is the only
        // difference that matters between the two.
        const PassState opaque = opaque_pass_state();
        const PassState blended = transparent_pass_state();
        check(!opaque.blending && !blended.blending == false, "the opaque pass does not blend");
        check(blended.blending, "the blended pass blends");
        check(opaque.depth_write, "the opaque pass writes depth");
        check(!blended.depth_write, "the blended pass does not");
        check(opaque.depth_test && blended.depth_test, "both passes test depth");

        // Stencil shadows need a directional light *and* a stencil buffer.
        check(uses_stencil_shadows(true, true), "a directional light enables stencil shadows");
        check(!uses_stencil_shadows(false, true), "with no directional light they are skipped");
        check(!uses_stencil_shadows(true, false), "and with no stencil buffer they are skipped");

        check(debug_draw_enabled(true) && !debug_draw_enabled(false),
              "the debug draw follows the scene's own byte");
        check(overlays_enabled(false) && !overlays_enabled(true),
              "the overlay pass is gated by the scene's disable byte");

        const FloatColor white = scene_tint(false, 0.1f, 0.2f, 0.3f, 0.4f);
        check(white.r == 1.0f && white.g == 1.0f && white.b == 1.0f && white.a == 1.0f,
              "with no scene color the tint is white, not the scene's stale values");
        const FloatColor tinted = scene_tint(true, 0.1f, 0.2f, 0.3f, 0.4f);
        check(tinted.r == 0.1f && tinted.a == 0.4f, "with one, the tint is the scene's color");

        check(kDebugColorCollided == 0xFF00FFFFu, "colliding objects are drawn magenta");
        check(kDebugColorDefault == 0xFFFFFFFFu, "everything else is drawn white");
        check(kProgramLit == 1 && kProgramTextured == 3,
              "the lit and textured program indices are the ones the engine passes to UseProgram");
        check(kMaxLightsEnum == 3377, "GL_MAX_LIGHTS is queried lazily with 3377");
        check(kShadowVertexStride == 24 && kShadowUvOffset == 12 && kShadowColorOffset == 20,
              "the shadow batch is a 24-byte stride: 3f position, 2f uv, 4b color");
    }

    if (g_failures == 0) std::printf("PASS: caver program state + frame runner\n");
    else std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
