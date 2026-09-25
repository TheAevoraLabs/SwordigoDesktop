// level_mechanics.cpp — see level_mechanics.h for the evidence this transcribes.

#include "ruby/caver/level_mechanics.h"

#include <cmath>

namespace caver {
namespace mechanics {
namespace {

// Caver::Rectangle::ExpandToIncludePoint (0x539454). Grows `r` so the point is
// inside it. Note the engine's own quirk, reproduced deliberately: when the point
// is left of the rectangle, `w` is recomputed as `(x0 + w0) - p.x` — i.e. it keeps
// the ORIGINAL right edge and moves the left one, which widens the rectangle. A
// naive min/max rewrite gives the same result here, but the moment `w` is already
// 0 (which is exactly the door/elevator case, where Prepare zeroes it first) the
// two differ, and only the engine's form is right.
void expand_to_include_point(Rect& r, float px, float py) {
    {
        const float x = r.x;
        const float w = r.w;
        if (px < x) {
            r.x = px;
            r.w = (x + w) - px;
        }
        if (px > x + r.w) r.w = px - x;
    }
    {
        const float y = r.y;
        const float h = r.h;
        if (py < y) {
            r.y = py;
            r.h = (y + h) - py;
        }
        if (py > y + r.h) r.h = py - y;
    }
}

float sq_len2(float x, float y) { return x * x + y * y; }

}  // namespace

// ── Door ─────────────────────────────────────────────────────────────────────

DoorTransition DoorController::set_open(bool want, int open_frame, bool sound_gate) {
    // `SetOpen` is idempotent in effect but not in side effects: it re-seats the
    // animation every call, because `Prepare()` ends by calling it with the
    // component's own stored `isOpen` byte. `changed` reports whether the state
    // actually moved, so the caller can skip the sound and the collision walk.
    DoorTransition t;
    t.changed = (want != open_);

    // `Open()` 0x3A08E8 / `Close()` 0x3A0AB0 — the constants, verbatim.
    t.open = want;
    t.playback_scale = want ? kDoorOpenPlaybackScale : kDoorClosedPlaybackScale;
    t.animation_playing = true;      // the 0x01 low byte written at animation +56
    t.animation_reversed = !want;    // its high byte: 0 opening, 1 closing
    t.collision_enabled = !want;     // Open() SetEnabled(false), Close() SetEnabled(true)
    t.sound = sound_gate ? (want ? DoorSound::Open : DoorSound::Close) : DoorSound::None;

    // The seek, from `SetOpen`: opening goes to the clip's open frame, closing to 0.
    // Note the asymmetry — `Open()` also reads the frame through
    // `KeyframeAnimationComponent::LoadAnimation` if the clip is not loaded yet,
    // which is why the caller passes it in rather than the controller mining it.
    t.animation_seek_frame = want ? open_frame : 0;

    open_ = want;
    return t;
}

bool DoorController::open_amount(const float* animation_time, float clip_duration,
                                 float* out) const {
    // The door's travel is the animation, so the amount is normalised clip time.
    // With no clip length there is no answer: say so instead of returning the
    // 0/1 step the old fabricated-rate model produced, which read as a door that
    // teleported because the animation clock was never wired to it.
    if (!out || !animation_time || !(clip_duration > 0.0f)) return false;
    const float t = *animation_time / clip_duration;
    *out = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return true;
}

// ── Elevator ─────────────────────────────────────────────────────────────────

void ElevatorController::prepare(const Rect& shape_bounds, float speed_in) {
    // `Prepare()` 0x3A1790, transcribed statement for statement. The locals keep
    // the decompiler's names because the data flow is three conditionals writing
    // the same two variables, and rewriting it as min/max hides the engine's
    // actual choice — which axis it decides the travel is on, and the fact that
    // the third branch can overwrite the second.
    const float x = shape_bounds.x;
    const float y = shape_bounds.y;
    const float w = shape_bounds.w;
    const float h = shape_bounds.h;

    float v6 = y + h;              // candidate travel.y
    float v7 = v6;                 // running "far edge" used by the later tests
    float v9 = 0.0f;               // candidate rect.x
    travel[0] = 0.0f;
    travel[1] = v6;

    if (v6 < -y) {
        v6 = y;
        v7 = -y;
        travel[0] = 0.0f;
        travel[1] = y;
    }

    const float xw = x + w;
    if (xw > v7) {
        v6 = 0.0f;
        v9 = xw;
        travel[0] = xw;
        v7 = xw;
        travel[1] = 0.0f;
    }

    float v11 = -x;
    if (v7 >= -x) {
        v11 = -v9;
    } else {
        v6 = 0.0f;
        v9 = x;
        travel[0] = x;
        travel[1] = 0.0f;
    }

    // The swept AABB: from the point the local shape occupies to the point it
    // occupies after travelling, inclusive.
    local_bounds = Rect{v9, v6, 0.0f, 0.0f};   // x, y, w = h = 0
    expand_to_include_point(local_bounds, v11, -v6);

    // `*((_QWORD *)this + 17) = 0` clears the RECTANGLE's w/h (offsets 136..143),
    // not the origin/target pair — which is why the origin capture and the state
    // are left exactly as they were. A re-init must not teleport a platform that
    // is mid-cycle.
    speed = speed_in;
}

// Point the owner's velocity at `to` from `from`, at the component's speed. The
// engine's `v4[15] = normalize(target - pos) * speed` appears in three places
// (StartElevating, ReturnToOriginalPosition, Update/LABEL_25) with identical
// semantics, so it lives here once.
static void aim_velocity(const float from[2], const float to[2], float speed,
                         float out[2]) {
    const float dx = to[0] - from[0];
    const float dy = to[1] - from[1];
    const float len = std::sqrt(sq_len2(dx, dy));
    if (len <= 0.0f) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        return;
    }
    out[0] = (dx / len) * speed;
    out[1] = (dy / len) * speed;
}

void ElevatorController::start_elevating(const float owner_pos[2],
                                         float owner_velocity[2]) {
    // `StartElevating()` 0x3A19D4.
    state = ElevatorState::Elevating;
    if (!origin_captured) {
        origin_captured = true;
        origin[0] = owner_pos[0];
        origin[1] = owner_pos[1];
    }
    target[0] = origin[0] + travel[0];
    target[1] = origin[1] + travel[1];
    if (!at_target(owner_pos)) aim_velocity(owner_pos, target, speed, owner_velocity);
}

void ElevatorController::return_to_original(const float owner_pos[2],
                                            float owner_velocity[2]) {
    // `ReturnToOriginalPosition()` 0x3A1AD8. Note it sets the target to the origin
    // BEFORE checking arrival, and it recomputes the direction from the *current*
    // position, so a platform that has drifted still returns.
    target[0] = origin[0];
    target[1] = origin[1];
    state = ElevatorState::Returning;
    if (!at_target(owner_pos)) aim_velocity(owner_pos, target, speed, owner_velocity);
}

bool ElevatorController::at_target(const float owner_pos[2]) const {
    // `IsAtTargetPosition()` 0x3A1AA0: strictly less than 1e-4 squared distance.
    return sq_len2(target[0] - owner_pos[0], target[1] - owner_pos[1]) <
           kElevatorArriveEpsilonSq;
}

bool ElevatorController::handle_hero_landing(const float contact_normal[2],
                                             const float owner_pos[2],
                                             bool hero_on_object,
                                             float owner_velocity[2]) {
    // `HandleMessage(msg == 7)` 0x3A1EB4.
    if (!hero_on_object) return false;

    // The trigger is directional: the contact normal is dotted against the
    // NORMALISED travel axis and must be within ~25 degrees of it. A hero landing
    // on the side of a platform does not start it.
    const float tlen = std::sqrt(sq_len2(travel[0], travel[1]));
    if (tlen <= 0.0f) return false;
    const float dx = travel[0] / tlen;
    const float dy = travel[1] / tlen;
    const float dot = contact_normal[0] * dx + contact_normal[1] * dy;
    // The engine proceeds when `dot < -0.9 || dot > 0.9` — i.e. when the contact
    // direction is ALIGNED with the travel axis (a hero standing on a rising
    // platform pushes straight along it). A landing on the side does not trigger.
    if (dot >= -kElevatorTriggerDot && dot <= kElevatorTriggerDot) return false;

    // Mode 2 is autonomous: it is already running its own cycle and ignores the
    // hero. Mode 0 with the machine idle is inert. Anything else starts an ascent.
    if (mode == ElevatorMode::Autonomous) {
        hold_timer = 0.0f;
        return false;
    }
    if (mode == ElevatorMode::Inert && state == ElevatorState::Idle) {
        hold_timer = 0.0f;
        return false;
    }
    if (state == ElevatorState::Elevating) {
        hold_timer = 0.0f;
        return false;
    }
    start_elevating(owner_pos, owner_velocity);
    hold_timer = 0.0f;
    return true;
}

ElevatorStep ElevatorController::update(float dt, float owner_pos[2],
                                        float owner_velocity[2]) {
    // `Update(float)` 0x3A1B8C. The switch sets a direction, then the tail of the
    // function integrates the owner's velocity into the owner's position. Both
    // halves are reproduced in that order, because the integration uses the
    // velocity the switch just wrote.
    ElevatorStep out;
    float dir[2] = {0.0f, 0.0f};
    bool have_dir = false;
    bool skip_integration = false;

    switch (state) {
        case ElevatorState::Idle: {
            if (mode != ElevatorMode::Autonomous) { skip_integration = true; break; }
            state = ElevatorState::Elevating;
            if (!origin_captured) {
                origin_captured = true;
                origin[0] = owner_pos[0];
                origin[1] = owner_pos[1];
            }
            target[0] = origin[0] + travel[0];
            target[1] = origin[1] + travel[1];
            if (at_target(owner_pos)) break;   // already there: no velocity
            dir[0] = target[0] - owner_pos[0];
            dir[1] = target[1] - owner_pos[1];
            have_dir = true;
            break;
        }
        case ElevatorState::Returning: {
            if (mode != ElevatorMode::Autonomous) {
                if (at_target(owner_pos)) {
                    state = ElevatorState::Idle;
                    skip_integration = true;
                }
                break;
            }
            if (!at_target(owner_pos)) break;
            // Mode 2 reached the origin: start the next cycle straight away.
            state = ElevatorState::Elevating;
            if (!origin_captured) {
                origin_captured = true;
                origin[0] = owner_pos[0];
                origin[1] = owner_pos[1];
            }
            target[0] = origin[0] + travel[0];
            target[1] = origin[1] + travel[1];
            if (at_target(owner_pos)) break;
            dir[0] = target[0] - owner_pos[0];
            dir[1] = target[1] - owner_pos[1];
            have_dir = true;
            break;
        }
        case ElevatorState::Elevating: {
            if (mode == ElevatorMode::HeroTriggered) {
                // Keep elevating while the trigger is being refreshed. The timer
                // is reset by every `HandleMessage` (the hero still standing on
                // the platform), so it only starts counting once the rider steps
                // off — which makes this "return one second after the LAST
                // trigger", not "hold for a second at the far end". Measured
                // against the engine's own `<= 1.0` comparison and its reset in
                // HandleMessage; the earlier reading of this branch was wrong and
                // would have had the platform turn round mid-flight.
                if (hold_timer <= kElevatorHoldSeconds) break;
            } else if (mode == ElevatorMode::Autonomous) {
                if (!at_target(owner_pos)) break;
            }
            // Modes 0 and 1 (and 2 on arrival) head home.
            state = ElevatorState::Returning;
            target[0] = origin[0];
            target[1] = origin[1];
            if (at_target(owner_pos)) break;
            dir[0] = target[0] - owner_pos[0];
            dir[1] = target[1] - owner_pos[1];
            have_dir = true;
            break;
        }
    }

    if (have_dir) {
        const float len = std::sqrt(sq_len2(dir[0], dir[1]));
        if (len > 0.0f) {
            owner_velocity[0] = (dir[0] / len) * speed;
            owner_velocity[1] = (dir[1] / len) * speed;
        }
    }
    (void)have_dir;
    out.state = state;

    if (!skip_integration && state == ElevatorState::Idle) skip_integration = true;
    if (skip_integration) {
        out.position[0] = owner_pos[0];
        out.position[1] = owner_pos[1];
        out.velocity[0] = owner_velocity[0];
        out.velocity[1] = owner_velocity[1];
        // LABEL_37 still runs: the hold timer keeps counting even when idle.
        const float restart = hold_timer >= kElevatorTimerRestartBelow ? 0.0f
                                                                      : settle_timer + dt;
        settle_timer = restart;
        hold_timer += dt;
        return out;
    }

    // ── Integration ──────────────────────────────────────────────────────────
    // `if (distSq >= stepSq)` → move by the full step and KEEP the velocity;
    // otherwise snap to the target and ZERO it. The zeroing is what stops the
    // platform from jittering on arrival, and it is the reason the velocity is
    // written back even on the frames the position did not move.
    const float nx = owner_pos[0];
    const float ny = owner_pos[1];
    float tx = target[0];
    float ty = target[1];
    const float dx = tx - nx;
    const float dy = ty - ny;
    const float step_x = owner_velocity[0] * dt;
    const float step_y = owner_velocity[1] * dt;

    float keep_x = 0.0f;
    float keep_y = 0.0f;
    float moved_x = 0.0f;
    if (sq_len2(dx, dy) >= sq_len2(step_x, step_y)) {
        tx = nx + step_x;
        ty = ny + step_y;
        keep_x = owner_velocity[0];
        keep_y = owner_velocity[1];
        moved_x = (nx + step_x) - nx;
    }

    const float moved_y = ty - ny;
    if (std::fabs(moved_x) > kElevatorArriveEpsilonSq ||
        std::fabs(moved_y) > kElevatorArriveEpsilonSq) {
        owner_pos[0] = tx;
        owner_pos[1] = ty;
        out.moved = true;
        // `SceneObject::RegisterForWorldBoundsUpdate` is only called on the first
        // move after the flag was cleared, so a platform does not spam the
        // world-bounds pass every frame.
        out.bounds_dirty = !bounds_flag;
        bounds_flag = true;
    }
    owner_velocity[0] = keep_x;
    owner_velocity[1] = keep_y;

    out.position[0] = owner_pos[0];
    out.position[1] = owner_pos[1];
    out.velocity[0] = keep_x;
    out.velocity[1] = keep_y;

    // LABEL_37.
    const float restart = hold_timer >= kElevatorTimerRestartBelow ? 0.0f
                                                                  : settle_timer + dt;
    settle_timer = restart;
    hold_timer += dt;
    return out;
}

}  // namespace mechanics
}  // namespace caver
