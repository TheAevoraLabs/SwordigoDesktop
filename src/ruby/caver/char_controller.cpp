// char_controller.cpp — CharControllerComponent::Update (0x35A960) transcribed.
//
// The body of the guest function is mostly ComponentOutletBase::Connect noise:
// every few statements it resolves +104 (movement), +128 (animation controller)
// or +272 (weapon) through `this[49]` / `this[18]` with a refcount dance. Those
// resolutions are dropped here — the caller passes the resolved state in — and
// what remains is the decision sequence below. Where a guard is dropped because
// the port has no equivalent, the comment says which one and why.

#include "ruby/caver/char_controller.h"

#include <cmath>

namespace caver {
namespace cchar {

bool CharController::swing_still_running(const canim::CharAnimController& anim) {
    // `v11 = *(float**)(v8 + 360)` — the swing node — then
    //
    //   -(time - duration * 0.9) * (speed * speed_multiplier) <= 0.01
    //
    // with `v12 = 1` when that is false. Read off the node's own float slots:
    // v11[3] is +12 (time), v11[10] is +40 (duration), v11[12] is +48 (speed) and
    // v11[13] is +52 (speed_multiplier) — all four are the AnimKeysNode offsets
    // recovered in anim_bones.h, which is how the index arithmetic is checkable.
    const anim::KeysNode* node = anim.swing_node();
    if (!node) return false;
    const float remaining = node->duration * kSwingTailFraction - node->time;
    return remaining * (node->speed * node->speed_multiplier) > kSwingTailEpsilon;
}

void CharController::start_moving_to_direction(int direction, canim::CharAnimController& anim) {
    // 0x35D578. `StartMoving` first, then the latch — and the direction argument
    // is not consulted before the call, so "move in direction 0" still starts the
    // run cycle; stopping is a different function entirely.
    anim.start_moving();
    state.moving_direction = direction;
    state.on_ground = true;          // +556 = 1
}

void CharController::stop_moving_to_direction(int direction) {
    // 0x35D6AC. Note the guard: the direction has to *match* the latch, so a stale
    // stop for a direction the character has already left does nothing at all.
    if (state.moving_direction != direction) return;
    state.on_ground = false;         // +556 = 0 — the run cycle is cut by the air rule
    state.moving_direction = 0;      // +552 = 0
    // The air timer is *not* reset here; the airborne branch accumulates onto
    // whatever it holds, which is why a stop right after a landing cuts the cycle
    // sooner than 0.11 s.
}

bool CharController::update(float dt, const CharInput& in, canim::CharAnimController& anim) {
    bool changed = false;

    // ── 1. The swing-tail test, before anything else ───────────────────────
    // This runs first because its result is what the jump branch later passes as
    // `air_jump`. When no swing is running it also accumulates +134, the
    // jump-animation timer.
    const bool swing_running = swing_still_running(anim);
    if (!swing_running) state.jump_anim_time += dt;

    // ── 2. The latched direction (+548) ────────────────────────────────────
    // The direction of this copy matters: Update writes `+548 = +552` and never
    // the reverse — `+552` belongs to the movement path, which is the only thing
    // that may change it. Getting this backwards makes Start/StopMovingToDirection
    // look like no-ops, because the next frame overwrites the value they set
    // before anything can read it.
    //
    // And the copy is *skipped* for three of the five movement behaviours. The
    // gate is `v18 > 4 || ((1 << v18) & 0x1A) == 0`, and 0x1A is bits 1, 3 and 4 —
    // MovementBehavior's Walk, Follow and Flee. The enumeration itself is
    // recovered (behaviour.h), but the reason those three are singled out is not.
    const int behavior = in.movement_behavior;
    if (behavior < 0 || behavior > 4)
        state.latched_direction = state.moving_direction;
    else if (((1 << behavior) & 0x1A) == 0)
        state.latched_direction = state.moving_direction;

    // `+608 = +624`: the speed the run cycle is told about, kept on the component.
    state.current_speed = in.run_speed;

    // ── 3. Ground or air ───────────────────────────────────────────────────
    // `v20 = *(u8*)(this + 556)` — *read*, not written. The two timers are
    // exclusive: the airborne branch is the else of the ground branch, so +141
    // stops advancing the moment +556 clears and +142 stops advancing the moment
    // it is set.
    if (state.on_ground) {
        state.ground_time += dt;
        state.air_time = 0.0f;
        state.is_falling = false;   // +596 is cleared with the ground underfoot
    } else {
        state.air_time += dt;
        if (state.jump_grace && state.air_time > kJumpGraceTime)
            state.jump_grace = false;

        // StopMoving: the run cycle is dropped once the animation has claimed to
        // be moving for more than 0.11 s with no ground. Note the test is against
        // `IsMoving()` — `!moveBlend.reverse` — and not against the character's
        // speed, so a character that has been told to walk keeps reporting moving
        // in mid-air for those 0.11 s before the cycle is cut.
        if (anim.is_moving() && state.air_time > kAirRunStopTime) {
            anim.stop_moving();
            changed = true;
        }
    }

    // ── 4. The run speed every frame ───────────────────────────────────────
    // `SetCurrentRunSpeed(controller, *((float*)this + 152))`, unconditionally, and
    // the gate that zeroes it (`+132 <= 0.0 || +120 >= 0.1`, i.e. airborne and
    // clear of the ground) zeroes a *local* — the call still happens, with
    // whatever +608 holds. Note that this makes a zero run speed a zero playback
    // rate: the walk clip's own speed (+48) is untouched, but its +52 factor is
    // `speed / 100` and the node multiplies the two, so a caller that passes 0
    // freezes the run cycle rather than playing it at its authored rate. That is
    // why the port feeds this the character's actual horizontal speed and not the
    // authored NormalRunSpeed, which is 0 on most objects.
    // clip's own speed is untouched, but its +52 factor is `speed / 100` and the
    // node multiplies the two, so a caller that passes 0 freezes the run cycle.
    anim.set_current_run_speed(in.height_above_ground < 0.1f ? state.current_speed : 0.0f);

    // ── 5. The swing countdown (+188) ──────────────────────────────────────
    // A delay, not a duration. While it is running it also clears +548, the
    // latched moving direction. Only when it expires does the weapon's current
    // animation node get blended in — so the swing clip starts *after* the
    // delay, and the fade is the same 0.08 s everything else uses.
    if (state.swing_countdown > 0.01f) {
        state.swing_countdown -= dt;
        if (state.swing_countdown <= 0.01f && in.swing_node_available) {
            // The guest resolves the node from the weapon controller's vtable
            // slot +264 and checks it is non-null before calling; the port has no
            // weapon controller, so `swing_node_available` is that check.
            state.swing_countdown = 0.0f;
        }
    }

    // ── 6. The jump ────────────────────────────────────────────────────────
    // Two distinct paths, and they are not the same animation:
    //
    //   ledge step-up (LineSegmentIntersectsGround on the step probe):
    //       +151 = 0; +146 = 2; +154 = jump_speed * 0.5; StartJumping(0)
    //       — a *ground* jump, and the vertical speed is halved.
    //   jump input (after CanJump):
    //       StartJumping(v12 != 1)
    //       — the air-jump clip is used when no swing is running.
    if (in.ledge_step_up) {
        state.air_state = 2;
        state.ledge_jump_speed = in.vertical_speed * 0.5f;
        anim.start_jumping(false);
        state.jump_grace = true;
        changed = true;
    } else if (in.jump_pressed) {
        anim.start_jumping(!swing_running);
        state.jump_grace = true;
        changed = true;
    }

    // ── 7. The falling clip ────────────────────────────────────────────────
    // Set once, and only on a frame that is *both* clear of the ground and moving
    // down: `+120 > 0.15 && +208 < 0.0`, then `CanStartFalling()`. The +596 flag
    // is what makes it once — a character already falling never re-enters here, so
    // the clip is not restarted every frame of the fall.
    if (!state.is_falling && in.height_above_ground > kFallClearance &&
        in.vertical_speed < 0.0f) {
        if (anim.can_start_falling()) {
            state.is_falling = true;
            anim.start_falling();
            changed = true;
        }
    }

    // ── 8. The cast hand-back ──────────────────────────────────────────────
    // `if (+692) { +692 = 0; if (outlet +248) BlendToMoveAnimation(0.08); }` — the
    // cast flag is cleared as the blend is requested, so a finished cast fades
    // back to the locomotion tree exactly once.
    if (in.cast_finished && state.cast_active) {
        state.cast_active = false;
        anim.blend_to_move_animation(kActionBlendTime);
        changed = true;
    }

    // `StandOnGround` is *not* called from Update. It is reached from the landing
    // path, which is a different function; the port calls it there.
    return changed;
}

void CharController::set_initial_animation_state(const canim::SpawnState& spawn,
                                                 canim::CharAnimController& anim) {
    // CharControllerComponent::SetInitialAnimationControllerState (0x35DC68). The
    // body is `Reset`, then:
    //
    //   if (+552 != 0)          StartMoving()          ← a separate `if`
    //   if (+584 == 3)          StartJumping(0)        ← and these two are an
    //   else if (+596)          StartFalling()           if / else-if pair
    //
    // Moving is deliberately *not* an `else`: a character that spawns walking and
    // airborne gets `StartMoving` first and then the air call on top of it, which
    // is observable — StartMoving re-phases the walk clip to a quarter of its
    // cycle and starts the move blend, and StartJumping then moves the root blend
    // to the air tree without undoing that. Collapsing the three tests into one
    // if / else-if chain changes the tree for exactly that case.
    //
    // The driver's own +552/+584/+596 *are* the fields this reads — there is no
    // copy in the guest — so `state` is left to the caller to seed from the same
    // spawn state, and the shared transcription lives in char_anim.cpp.
    canim::set_initial_animation_state(anim, spawn);
    state.moving_direction = spawn.moving_direction;
    state.air_state = spawn.air_state;
    state.is_falling = spawn.is_falling;
}

} // namespace cchar
} // namespace caver
