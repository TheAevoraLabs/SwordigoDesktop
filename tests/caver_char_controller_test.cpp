// caver_char_controller_test — CharControllerComponent::Update (0x35A960),
// transcribed in src/ruby/caver/char_controller.{h,cpp}.
//
// The point of this test is the *thresholds and the order*, because every one of
// them is a literal compare in the binary and each is off-by-one-able:
//
//   * the run cycle is cut only once the air timer is strictly past 0.11 s, and
//     the test is against `IsMoving()` (a flag), not against the character's speed;
//   * the jump grace is cleared only once the air timer is strictly past 0.2 s;
//   * the falling clip needs strictly more than 0.15 of clearance *and* a
//     downward vertical speed, and it is entered once — not restarted per frame;
//   * a jump uses the air-jump clip unless a swing is still running, where
//     "still running" means `(duration * 0.9 - time) * speed * speed_multiplier`
//     is past 0.01 on the swing node;
//   * StartMoving is *never* called from Update — only from
//     SetInitialAnimationControllerState and StartMovingToDirection — and the
//     spawn path runs it and then the air call on top of it, because `moving` is
//     a separate `if` and not part of the if/else-if chain.
//
// Pure RAM: no guest, no GL, no Qt.

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "ruby/caver/anim_bones.h"
#include "ruby/caver/char_anim.h"
#include "ruby/caver/char_controller.h"

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

using caver::anim::Clip;
using caver::anim::KeysNode;
using caver::canim::CharAnimController;
using caver::cchar::CharController;
using caver::cchar::CharInput;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Six clips, because a node with no clip never advances and would make every
// "did the clock move" assertion vacuous (AnimKeysNode::Update returns first).
struct Fixture {
    Clip stand_clip, walk_clip, jump_clip, air_clip, fall_clip, cast_clip;
    KeysNode stand, walk, jump, air, fall, cast;
    CharAnimController anim;
    CharController driver;

    Fixture() {
        caver::anim::Clip* clips[6] = {&stand_clip, &walk_clip, &jump_clip,
                                       &air_clip, &fall_clip, &cast_clip};
        for (caver::anim::Clip* clip : clips) {
            clip->frame_count = 2;
            clip->fps = 10.0f;
            clip->duration = 1.0f;
            clip->frames.assign(2, caver::anim::FloatDataBuffer{});
            for (auto& frame : clip->frames) frame.data.assign(12, 0.0f);
        }
        KeysNode* nodes[6] = {&stand, &walk, &jump, &air, &fall, &cast};
        const int ids[6] = {CharAnimController::kStandAnimation, CharAnimController::kWalkAnimation,
                            CharAnimController::kJumpAnimation, CharAnimController::kAirJumpAnimation,
                            CharAnimController::kFallAnimation, CharAnimController::kCastAnimation};
        for (int i = 0; i < 6; ++i) {
            nodes[i]->init_with_animation(clips[i]);   // duration = clip.duration - 0.001
            anim.bind(ids[i], nodes[i]);
        }
        anim.prepare();
        // AnimationControllerComponent::Update returns immediately without a
        // model, so the driver's transitions would drive nothing without this.
        anim.base().model_present = true;
    }
};

static void swing_tail_running() {
    Fixture f;
    check(!CharController::swing_still_running(f.anim),
          "no swing node means no swing is running");

    f.anim.swing_with_animation(f.cast);   // +360 = cast clip: duration 0.999, time 0
    check(CharController::swing_still_running(f.anim),
          "a fresh swing has 90% of its clip left (0x35A960)");

    f.cast.time = 0.5f;
    check(CharController::swing_still_running(f.anim),
          "…and still has some left at half way");

    f.cast.time = 0.95f;
    check(!CharController::swing_still_running(f.anim),
          "past 90% of the clip the swing is over — 0.999*0.9 - 0.95 < 0.01");

    // The rate is multiplied in, so a stopped node is not "running" no matter how
    // much of its clip is left: `speed * speed_multiplier` is a factor, not a gate.
    f.cast.time = 0.0f;
    f.cast.speed = 0.0f;
    check(!CharController::swing_still_running(f.anim),
          "a node with zero playback rate is not progressing toward its end");
}

static void airborne_run_cycle_stop() {
    Fixture f;
    f.driver.start_moving_to_direction(1, f.anim);
    check(f.anim.is_moving(), "a character told to walk reports moving");

    // Leaving the ground is the movement path's stop, which only *clears the
    // ground flag* — the cycle itself is cut by the 0.11 s rule below.
    f.driver.stop_moving_to_direction(1);
    check(!f.driver.state.on_ground, "StopMovingToDirection clears +556");

    CharInput in;

    f.driver.update(0.05f, in, f.anim);
    check(near(f.driver.state.air_time, 0.05f), "the air timer accumulates while airborne");
    check(f.anim.is_moving(), "0.05 s airborne is not enough to cut the run cycle");

    f.driver.update(0.05f, in, f.anim);
    check(near(f.driver.state.air_time, 0.1f) && f.anim.is_moving(),
          "neither is 0.10 s — the test is strictly greater than 0.11");

    f.driver.update(0.05f, in, f.anim);
    check(!f.anim.is_moving(),
          "past 0.11 s airborne StopMoving fires, and IsMoving is !moveBlend.reverse");
    check(f.anim.move_blend().flags_word() == caver::anim::kBlendTowardLeft,
          "…by writing the backward flag pair, not by teleporting the weight");

    // The two timers are exclusive: a grounded frame zeroes the air timer, which
    // is what makes the 0.11 s window per-airtime and not cumulative. Landing is
    // the movement path raising the flag again.
    CharInput ground;
    f.driver.start_moving_to_direction(1, f.anim);
    f.driver.update(0.05f, ground, f.anim);
    check(near(f.driver.state.air_time, 0.0f), "landing clears the air timer");
    check(near(f.driver.state.ground_time, 0.05f), "…and starts the ground timer");
}

static void ground_stop_goes_through_the_air_rule() {
    Fixture f;
    f.driver.start_moving_to_direction(1, f.anim);
    check(f.driver.state.on_ground && f.anim.is_moving(),
          "starting to move latches the ground flag and the run cycle");
    check(f.driver.state.moving_direction == 1, "…and latches the direction (+552)");
    check(near(f.walk.time, f.walk.duration * 0.25f),
          "StartMoving's quarter is a *phase* of the clip, not a quarter of a second");

    // The stop does not touch the animation at all — it lies about the ground.
    f.driver.stop_moving_to_direction(1);
    check(!f.driver.state.on_ground, "StopMovingToDirection clears the ground flag (+556)");
    check(f.driver.state.moving_direction == 0, "…and the latched direction (+552)");
    check(f.anim.is_moving(), "…and deliberately leaves the run cycle running");

    CharInput in;
    for (int i = 0; i < 2; ++i) f.driver.update(0.05f, in, f.anim);
    check(f.anim.is_moving(), "the cycle survives the first 0.10 s of the lie");
    f.driver.update(0.05f, in, f.anim);
    check(!f.anim.is_moving(),
          "…and is then cut by the same 0.11 s air rule as a real mid-air stop — "
          "there is one stop path, not two");

    // The guard: a stop for a direction the character has already left is a no-op.
    Fixture g;
    g.driver.start_moving_to_direction(1, g.anim);
    g.driver.stop_moving_to_direction(-1);
    check(g.driver.state.on_ground, "a stale stop direction does not clear the ground flag");
}

static void grounded_never_cuts_the_cycle() {
    Fixture f;
    f.driver.start_moving_to_direction(1, f.anim);
    CharInput in;
    for (int i = 0; i < 40; ++i) f.driver.update(0.05f, in, f.anim);   // 2 s
    check(f.anim.is_moving(), "two seconds on the ground never triggers StopMoving");
    check(near(f.driver.state.air_time, 0.0f), "the air timer stays at zero");
}

static void jump_grace_expiry() {
    Fixture f;
    CharInput in;
    in.jump_pressed = true;
    f.driver.update(0.05f, in, f.anim);
    check(f.driver.state.jump_grace, "a jump arms the grace window (+572)");
    check(near(f.driver.state.air_time, 0.0f), "…without touching the air timer");

    // The jump path is what takes the character off the ground in the guest.
    f.driver.stop_moving_to_direction(f.driver.state.moving_direction);
    CharInput air;
    air.height_above_ground = 0.5f;
    air.vertical_speed = 1.0f;      // still rising, so no falling clip
    f.driver.update(0.15f, air, f.anim);
    check(near(f.driver.state.air_time, 0.15f) && f.driver.state.jump_grace,
          "0.15 s of coyote time is inside the window");
    f.driver.update(0.05f, air, f.anim);
    check(near(f.driver.state.air_time, 0.2f) && f.driver.state.jump_grace,
          "…and 0.20 s is still inside it — the clear is strictly greater");
    f.driver.update(0.05f, air, f.anim);
    check(!f.driver.state.jump_grace, "past 0.2 s the grace window is gone");
}

static void run_speed_every_frame() {
    Fixture f;
    f.anim.start_moving();

    CharInput in;
    in.run_speed = 250.0f;
    f.driver.update(0.016f, in, f.anim);
    check(near(f.walk.speed_multiplier, 2.5f),
          "SetCurrentRunSpeed writes speed/100 into the walk node, every frame");

    in.height_above_ground = 0.5f;    // the `+120 >= 0.1` branch zeroes the local
    f.driver.update(0.016f, in, f.anim);
    check(near(f.walk.speed_multiplier, 0.0f),
          "clear of the ground the same call passes 0 instead");
}

static void jump_picks_the_clip_by_swing_tail() {
    // No swing: the jump is the *air* jump.
    {
        Fixture f;
        CharInput in;
        in.jump_pressed = true;
        f.driver.update(0.016f, in, f.anim);
        check(f.anim.air_blend().from.keys == &f.air,
              "with no swing running the jump plays airJumpAnimation");
        check(f.anim.root_blend().flags_word() == caver::anim::kBlendTowardRight,
              "…and the root blend heads for the air tree");
    }
    // A swing with 90% of its clip left: the jump is the *ground* jump clip.
    {
        Fixture f;
        f.anim.swing_with_animation(f.cast);
        CharInput in;
        in.jump_pressed = true;
        f.driver.update(0.016f, in, f.anim);
        check(f.anim.air_blend().from.keys == &f.jump,
              "mid-swing the jump plays jumpAnimation instead — 0x35A960 passes "
              "`swing_still_running != 1` as the air flag");
    }
}

static void ledge_step_up_halves_the_launch() {
    Fixture f;
    CharInput in;
    in.vertical_speed = 400.0f;
    in.ledge_step_up = true;
    f.driver.update(0.016f, in, f.anim);
    check(near(f.driver.state.ledge_jump_speed, 200.0f),
          "the step-up keeps half the jump speed (+154 = jump_speed * 0.5)");
    check(f.driver.state.air_state == 2, "…and sets air sub-state 2 (+146)");
    check(f.anim.air_blend().from.keys == &f.jump,
          "…and animates as a *ground* jump: StartJumping(0), not the air jump");
}

static void falling_clip_is_entered_once() {
    Fixture f;
    f.driver.stop_moving_to_direction(f.driver.state.moving_direction);
    CharInput in;
    in.height_above_ground = 0.2f;
    in.vertical_speed = -1.0f;
    // `can_start_falling` is satisfied here because the root blend is still at
    // alpha 0 — the "nothing has been blended yet" branch.
    f.driver.update(0.016f, in, f.anim);
    check(f.driver.state.is_falling, "clear of the ground and moving down enters the fall");

    // Prove it is once: run the fall clip's clock forward, tick again with the
    // same input, and check the clock was not reset. A per-frame StartFalling
    // would re-phase it to 0 and freeze the animation on its first frame.
    f.fall.time = 0.4f;
    f.anim.update(0.016f);
    const float advanced = f.fall.time;
    check(advanced > 0.4f, "the fall clip's own clock runs");
    f.driver.update(0.016f, in, f.anim);
    check(near(f.fall.time, advanced),
          "+596 makes the falling clip an entry, not a per-frame restart");

    // The two gates, separately: clearance and direction.
    {
        Fixture g;
        g.driver.stop_moving_to_direction(g.driver.state.moving_direction);
        CharInput shallow;
        shallow.height_above_ground = 0.15f;   // the test is strictly greater
        shallow.vertical_speed = -1.0f;
        g.driver.update(0.016f, shallow, g.anim);
        check(!g.driver.state.is_falling, "0.15 of clearance exactly is not enough");

        Fixture h;
        h.driver.stop_moving_to_direction(h.driver.state.moving_direction);
        CharInput rising;
        rising.height_above_ground = 0.5f;
        rising.vertical_speed = 1.0f;
        h.driver.update(0.016f, rising, h.anim);
        check(!h.driver.state.is_falling, "being high up while still rising is not falling");
    }
}

static void cast_returns_to_locomotion_once() {
    Fixture f;
    // A cast in progress is an action blend heading for the cast clip, which is
    // what the hand-back then fades out of. Without it the derived override finds
    // `from == to == rootBlend` and returns — the hand-back of an action that
    // never started is deliberately nothing at all.
    f.anim.begin_casting();
    f.driver.state.cast_active = true;

    CharInput in;
    in.cast_finished = true;
    f.driver.update(0.016f, in, f.anim);
    check(!f.driver.state.cast_active, "a finished cast clears +692");
    // The hand-back lands on the *action blend*, not on m_currentNode: the
    // derived BlendToAnimationNode (0x358638) never writes m_currentNode — the
    // derived Update rewrites it once per frame instead — so a caller that
    // checked the base's slot here would see nothing and conclude it was a no-op.
    // What it really does is fade from the cast clip back to the move tree.
    check(f.anim.action_blend().from.keys == &f.cast,
          "…fading *from* the cast clip, not from a stand-in");
    check(f.anim.action_blend().to.mix == &f.anim.root_blend(),
          "…and *to* the move tree, over 0.08 s");
    check(near(f.anim.action_blend().blend_time, 0.08f),
          "…which is the driver's own 0.08, the same fade every action uses");

    // The flag gates it, so a second frame with the same input does nothing.
    const float alpha = f.anim.action_blend().alpha;
    f.driver.update(0.016f, in, f.anim);
    check(near(f.anim.action_blend().alpha, alpha),
          "the hand-back happens once, not every frame");
}

static void spawn_runs_moving_then_the_air_call() {
    // The near-miss this test exists for: `moving` is a separate `if`, so a
    // character that spawns both moving and jumping gets StartMoving *and* the
    // air call. Collapsing them into one chain silently loses the run cycle's
    // quarter-phase and the move blend's forward flag.
    Fixture f;
    caver::canim::SpawnState spawn;
    spawn.moving_direction = 1;
    spawn.air_state = 3;
    f.driver.set_initial_animation_state(spawn, f.anim);

    check(near(f.walk.time, f.walk.duration * 0.25f),
          "spawning moving re-phases the run cycle to a quarter of its clip");
    check(f.anim.air_blend().from.keys == &f.jump,
          "and the air call on top of it selects the *ground* jump clip (air = 0)");
    check(f.anim.move_blend().flags_word() == caver::anim::kBlendTowardRight,
          "the move blend is left heading for the run clip");
    check(f.anim.root_blend().flags_word() == caver::anim::kBlendTowardRight,
          "and the root blend heads for the air tree");

    // The falling spawn reaches for the fall clip instead.
    Fixture g;
    caver::canim::SpawnState fall_spawn;
    fall_spawn.is_falling = true;
    g.driver.set_initial_animation_state(fall_spawn, g.anim);
    check(g.anim.air_blend().flags_word() == caver::anim::kBlendTowardRight,
          "spawning in free fall moves the air blend to the fall clip");
}

// ── the port's binding (ClipBinding) ───────────────────────────────────────

static std::array<std::string, caver::canim::ClipBinding::kCount> clip_names() {
    return {"stand", "walk", "jump", "airJump", "fall", "cast"};
}

static std::string shown_name(const caver::canim::ClipBinding& rig) {
    return rig.name_at(rig.index_of(rig.controller().display_node().keys));
}

static void binding_runs_the_recovered_step() {
    caver::canim::ClipBinding rig;
    check(rig.configure(clip_names(), true), "a fully named rig prepares its tree");
    check(rig.ready(), "…and reports itself ready");

    // 0-based array indices throughout, matching index_of_name/index_of.
    check(rig.index_of(rig.node_at(1)) == 1, "a node maps back to its index");
    check(rig.index_of(rig.node_at(CharAnimController::kWalkAnimation)) == -1,
          "…and the outlet *id* (101) is not an index (1)");
    check(rig.index_of_name("fall") == 4, "and a clip name does too");
    check(rig.index_of_name("") == -1, "an empty name is not an outlet");
    check(rig.index_of(nullptr) == -1, "and neither is a null node");

    CharController driver;
    driver.start_moving_to_direction(1, rig.controller());
    rig.controller().set_current_run_speed(200.0f);   // walk +52 = 2.0
    rig.set_speed(1, 1.0f);
    const float before = rig.time_at(1);
    rig.advance(0.5f);
    check(near(rig.time_at(1) - before, 1.0f),
          "the step is speed * dt * speed_multiplier: 0.5 s at 1.0 * 2.0 = 1.0 s");
    check(rig.time_at(0) > 0.0f,
          "every outlet keeps its own clock, not just the displayed one");

    // A known duration is the seam that makes set_phase exact — and the clamp.
    rig.set_duration(1, 2.0f);
    rig.controller().start_moving();
    check(near(rig.time_at(1), 0.5f),
          "with a duration, the quarter-phase is a quarter of the clip");
    rig.advance(10.0f);
    check(near(rig.time_at(1), 2.0f),
          "an overrun holds at the duration, the way AnimKeysNode::Update clamps");
}

static void driver_and_rig_pick_the_clip_by_name() {
    caver::canim::ClipBinding rig;
    rig.configure(clip_names(), true);
    CharController driver;
    driver.set_initial_animation_state(caver::canim::SpawnState{}, rig.controller());

    auto tick = [&](float dt, const CharInput& in) {
        driver.update(dt, in, rig.controller());
        rig.advance(dt);
        rig.controller().update(dt);
    };

    CharInput ground;
    // The driver hands this to SetCurrentRunSpeed every frame, and that is what
    // scales the run cycle — so a stand-in for the character's speed has to be here
    // or the walk clip's playback rate is 0.
    ground.run_speed = 200.0f;
    tick(0.016f, ground);
    check(shown_name(rig) == "stand", "a spawned character displays the stand clip");

    // Walking: the movement path starts the run cycle, and the move blend's 0.1 s
    // ease carries the weight to the walk clip.
    driver.start_moving_to_direction(1, rig.controller());
    for (int i = 0; i < 20; ++i) tick(0.016f, ground);
    check(shown_name(rig) == "walk",
          "starting to move blends into the run clip — 0x0100 and 0.1 s of easing");
    check(rig.time_at(1) > 0.0f, "…on the walk outlet's own clock");

    // Stopping: the ground stop clears the flag, the 0.11 s air rule cuts the
    // cycle, and the same 0.1 s ease brings the stand clip back.
    driver.stop_moving_to_direction(1);
    for (int i = 0; i < 30; ++i) tick(0.016f, ground);
    check(shown_name(rig) == "stand", "stopping blends back to the stand clip");

    // Falling: the driver's own gate is height > 0.15 and a *downward* speed, and
    // the fall clip is only entered from it.
    CharInput falling;
    falling.height_above_ground = 0.5f;
    falling.vertical_speed = -1.0f;
    for (int i = 0; i < 4; ++i) tick(0.016f, falling);
    check(shown_name(rig) == "jump",
          "the air blend parks on the jump clip for the first frames of the fall");
    for (int i = 0; i < 60; ++i) tick(0.016f, falling);
    check(shown_name(rig) == "fall",
          "then the air blend's 0.35 s ease lands on the fall clip");
}

int main() {
    binding_runs_the_recovered_step();
    driver_and_rig_pick_the_clip_by_name();
    swing_tail_running();
    airborne_run_cycle_stop();
    ground_stop_goes_through_the_air_rule();
    grounded_never_cuts_the_cycle();
    jump_grace_expiry();
    run_speed_every_frame();
    jump_picks_the_clip_by_swing_tail();
    ledge_step_up_halves_the_launch();
    falling_clip_is_entered_once();
    cast_returns_to_locomotion_once();
    spawn_runs_moving_then_the_air_call();

    if (g_failures == 0) std::printf("PASS: caver char controller driver\n");
    return g_failures == 0 ? 0 : 1;
}
