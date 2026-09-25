// caver_level_mechanics_test — the recovered DoorController / ElevatorController.
//
// Both of these used to be invented in `behaviour.cpp`: a door that moved at
// 1.5 units/s and a platform that moved at 2.0 units/s, neither of which exists
// anywhere in the engine. This pins the transcription of what the engine really
// does (src/ruby/caver/level_mechanics.{h,cpp}, read off the arm64_13
// decompilation) so a fabricated constant cannot come back:
//
//   * the door has NO motion law — SetOpen writes an animation scale, a
//     direction flag, a seek frame, a sound, and a collision toggle, and that is
//     the whole transition;
//   * the elevator is velocity-driven: it captures an origin, aims the owner's
//     velocity at `origin + travel` at the component's speed, and integrates
//     that velocity itself, snapping exactly onto the target and zeroing the
//     velocity on arrival;
//   * arrival is `distance² < 1e-4`, so the arrival frame is exact, and the
//     travel time is `distance / speed` — the property the invented constants
//     got wrong in a way you can see in game (a platform that ignores how far
//     away its floor is).
//
// Pure RAM: no guest, no GL, no Qt.

#include <cmath>
#include <cstdio>
#include <string>

#include "ruby/caver/level_mechanics.h"

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

using namespace caver;
using namespace caver::mechanics;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
    std::printf("caver level mechanics: DoorController + ElevatorController\n");

    // ── Door: the transition is the animation, not a rate ───────────────────
    {
        DoorController door;
        check(!door.is_open(), "a door starts closed (the component's isOpen byte is 0)");
        check(door.collision_enabled(), "a closed door carries collision");

        const int kOpenFrame = 12;
        DoorTransition open = door.set_open(true, kOpenFrame, /*sound_gate=*/true);
        check(open.changed, "opening reports a state change");
        check(open.open, "opening sets isOpen");
        check(near(open.playback_scale, kDoorOpenPlaybackScale),
              "Open() writes 0.5f into the animation's playback scale (+52)");
        check(open.animation_playing, "Open() marks the animation playing (+56 low byte = 1)");
        check(!open.animation_reversed,
              "Open() leaves +56's high byte clear (0x01, not 0x0101)");
        check(open.animation_seek_frame == kOpenFrame,
              "Open() seeks the animation's time (+12) to the clip's open frame (clip +48)");
        check(!open.collision_enabled,
              "Open() SetEnabled(false) on every CollisionShape on the object");
        check(open.sound == DoorSound::Open, "Open() plays the +176 SoundEffect");
        check(door.is_open(), "the controller latched open");
        check(door.binding_value(kDoorOpenBindingIndex),
              "binding 4 (\"open\", from GetBindings) reads back the open byte");
        check(!door.binding_value(3), "only binding 4 is the door's own");

        DoorTransition close = door.set_open(false, kOpenFrame, /*sound_gate=*/true);
        check(close.changed, "closing reports a state change");
        check(near(close.playback_scale, kDoorClosedPlaybackScale),
              "Close() writes 1.0f into the playback scale (+52)");
        check(close.animation_reversed,
              "Close() writes 0x0101: playing, reversed");
        check(close.animation_seek_frame == 0, "Close() seeks the animation back to 0");
        check(close.collision_enabled, "Close() SetEnabled(true) restores collision");
        check(close.sound == DoorSound::Close, "Close() plays the +152 SoundEffect");

        // Opening again is not a no-op for the animation -- SetOpen re-seats it
        // every call, which is how Prepare() establishes the initial pose -- but
        // it is reported as "no change" so the caller can skip the sound.
        DoorTransition again = door.set_open(false, kOpenFrame, /*sound_gate=*/true);
        check(!again.changed, "re-requesting the current state reports no change");

        // The sound gate is `*(*(animationController + 120) + 326) != 0`.
        DoorTransition muted = door.set_open(true, kOpenFrame, /*sound_gate=*/false);
        check(muted.sound == DoorSound::None, "the sound gate suppresses the sound");
        check(!muted.collision_enabled, "...but the collision toggle still happens");

        // Binding entry point: only index 4 does anything.
        DoorController d2;
        DoorTransition ignored = d2.apply_binding_value(0, true, 3, true);
        check(!ignored.changed && !d2.is_open(), "binding index 0 is not the door's");
        DoorTransition bound = d2.apply_binding_value(kDoorOpenBindingIndex, true, 3, true);
        check(bound.changed && d2.is_open(), "binding 4 == SetOpen");
    }

    // ── Door: 0..1 travel is only answerable with a clip ────────────────────
    {
        DoorController door;
        float amount = -1.0f;
        const float t = 1.0f;
        check(!door.open_amount(&t, /*clip_duration=*/0.0f, &amount),
              "without a clip duration the open amount is unknown, not 0 or 1");
        check(door.open_amount(&t, /*clip_duration=*/2.0f, &amount) && near(amount, 0.5f),
              "with a clip the amount is the animation's normalised time");
        const float past = 5.0f;
        check(door.open_amount(&past, 2.0f, &amount) && near(amount, 1.0f),
              "...clamped at 1");
        check(!door.open_amount(nullptr, 2.0f, &amount), "no animation clock, no answer");
    }

    // ── Elevator: velocity, not a lerp ──────────────────────────────────────
    {
        ElevatorController lift;
        // A 4 x 2 trigger shape at the origin. Prepare() derives the travel from
        // the shape's own bounds, so the platform's reach is authored in the
        // scene, not hardcoded.
        lift.prepare(Rect{0.0f, 0.0f, 4.0f, 2.0f}, /*speed=*/8.0f);
        check(lift.speed == 8.0f, "Prepare() takes the component's speed");
        check(!lift.origin_captured, "the origin is captured lazily, not at Prepare");
        check(lift.state == ElevatorState::Idle, "Prepare() leaves the machine idle");
        const float travel_len =
            std::sqrt(lift.travel[0] * lift.travel[0] + lift.travel[1] * lift.travel[1]);
        check(travel_len > 0.0f, "Prepare() derived a non-zero travel offset from the bounds");
        std::printf("  (travel from a 4x2 shape: %.3f, %.3f  |=%.3f)\n", lift.travel[0],
                    lift.travel[1], travel_len);

        // The constructor pair at +192 is `0x7F7FFFFF00000000`: settle_timer 0 and
        // hold_timer FLT_MAX, i.e. "the last trigger was an eternity ago".
        check(lift.hold_timer > 1.0e38f, "hold_timer starts at FLT_MAX, not 0");
        {
            ElevatorController fresh;   // Prepare() has not overridden the speed yet
            check(near(fresh.speed, kElevatorDefaultSpeed),
                  "the default speed is the constructor's 180.0 (there is no Speed field)");
        }
        check(near(kElevatorDefaultSpeed, 180.0f), "0x43340000 is 180.0");
        // The payload's Mode is 1-based; LoadFromProtobufMessage stores Mode-1.
        check(ElevatorController::mode_from_payload(1) == ElevatorMode::Inert,
              "payload Mode 1 -> internal 0 (inert)");
        check(ElevatorController::mode_from_payload(2) == ElevatorMode::HeroTriggered,
              "payload Mode 2 -> internal 1 (the default)");
        check(ElevatorController::mode_from_payload(3) == ElevatorMode::Autonomous,
              "payload Mode 3 -> internal 2 (autonomous)");
        check(ElevatorController::mode_from_payload(0) == ElevatorMode::HeroTriggered,
              "an out-of-range payload mode falls back to the default");

        // A mode-1 lift whose hold timer was NOT reset by a trigger turns round on
        // the first Update -- `hold_timer <= 1.0` is false at FLT_MAX. That is the
        // engine's own behaviour (HandleMessage resets the timer, StartElevating
        // does not), and it is exactly why the test below has to simulate the
        // trigger rather than just call StartElevating.
        {
            ElevatorController untriggered;
            untriggered.prepare(Rect{0.0f, 0.0f, 4.0f, 2.0f}, 8.0f);
            float up[2] = {10.0f, 0.0f};
            float uv[2] = {0.0f, 0.0f};
            untriggered.start_elevating(up, uv);
            const float before = up[0];
            untriggered.update(1.0f / 60.0f, up, uv);
            check(untriggered.state == ElevatorState::Returning && near(up[0], before),
                  "mode 1 turns round immediately when no trigger reset the timer");
        }

        float pos[2] = {100.0f, 50.0f};
        float vel[2] = {0.0f, 0.0f};
        lift.start_elevating(pos, vel);
        // `HandleMessage` zeroes the hold timer when the hero steps on; that reset
        // is the trigger's job, not StartElevating's.
        lift.hold_timer = 0.0f;
        check(lift.state == ElevatorState::Elevating, "StartElevating() enters state 1");
        check(lift.origin_captured && near(lift.origin[0], 100.0f) && near(lift.origin[1], 50.0f),
              "StartElevating() captures the current position as the origin");
        check(near(lift.target[0], 100.0f + lift.travel[0]) &&
                  near(lift.target[1], 50.0f + lift.travel[1]),
              "the target is origin + travel");
        const float speed_now = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
        check(near(speed_now, 8.0f), "StartElevating() aims the owner's velocity at `speed`");
        // Direction, not just magnitude.
        const float want_x = (lift.target[0] - pos[0]) / travel_len;
        const float want_y = (lift.target[1] - pos[1]) / travel_len;
        check(near(vel[0], want_x * 8.0f) && near(vel[1], want_y * 8.0f),
              "the velocity direction is normalize(target - position)");

        // Integrate to arrival. 60 Hz, and the arrival must be EXACT: the engine
        // snaps when the remaining distance is under one frame's step.
        const float dt = 1.0f / 60.0f;
        const float distance = travel_len;
        const float expected_seconds = distance / 8.0f;
        int frames = 0;
        bool arrived = false;
        for (; frames < 600; ++frames) {
            ElevatorStep s = lift.update(dt, pos, vel);
            if (lift.at_target(pos)) {
                arrived = true;
                (void)s;
                break;
            }
        }
        check(arrived, "the platform reaches its target");
        check(near(pos[0], lift.target[0]) && near(pos[1], lift.target[1]),
              "arrival is exact -- it snaps onto the target instead of stopping a step short");
        const float elapsed = frames * dt;
        check(std::fabs(elapsed - expected_seconds) < 2.0f * dt,
              "the travel time is distance / speed (the fabricated constant ignored distance)");
        std::printf("  (%.3f units at 8 u/s: %.3f s of travel vs %.3f s expected)\n",
                    distance, elapsed, expected_seconds);

        // On arrival the velocity is zeroed rather than left pointing at a target
        // it already reached -- that is what stops the platform jittering. The
        // engine snaps and zeroes on the frame AFTER the one that lands exactly on
        // the target (that frame takes the `distSq >= stepSq` branch and keeps its
        // velocity), so the zeroing is asserted after one more step.
        lift.update(dt, pos, vel);
        float vlen = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
        check(near(vlen, 0.0f), "arrival zeroes the owner's velocity");
        check(near(pos[0], lift.target[0]) && near(pos[1], lift.target[1]),
              "...without drifting off the target");

        // And a frame later it stays put: no overshoot, no oscillation.
        const float before[2] = {pos[0], pos[1]};
        lift.update(dt, pos, vel);
        check(near(pos[0], before[0]) && near(pos[1], before[1]),
              "the platform holds position once it has arrived");
    }

    // ── Elevator: Modes 0 / 1 / 2 ───────────────────────────────────────────
    {
        const float dt = 1.0f / 60.0f;

        // Mode 1 (the shipped default): a hero landing along the travel axis
        // starts an ascent; a hero landing across it does not.
        ElevatorController lift;
        lift.prepare(Rect{0.0f, 0.0f, 1.0f, 6.0f}, 4.0f);
        check(lift.mode == ElevatorMode::HeroTriggered,
              "the default mode is 1 (\"mode\", default 1 in GetBindings)");
        const float axis_len =
            std::sqrt(lift.travel[0] * lift.travel[0] + lift.travel[1] * lift.travel[1]);
        const float along[2] = {lift.travel[0] / axis_len, lift.travel[1] / axis_len};
        const float across[2] = {-along[1], along[0]};
        float pos[2] = {0.0f, 0.0f};
        float vel[2] = {0.0f, 0.0f};

        check(!lift.handle_hero_landing(across, pos, true, vel),
              "a contact normal across the travel axis does not start the lift");
        check(!lift.handle_hero_landing(along, pos, false, vel),
              "the sender's grounded flag gates the trigger");
        check(lift.handle_hero_landing(along, pos, true, vel),
              "a hero landing along the travel axis starts the lift");

        // Mode 1's hold timer is reset by EVERY HandleMessage, i.e. by the hero
        // still standing on the platform. So the platform must keep rising while
        // ridden, and only start counting down once the rider steps off. Drive a
        // sustained trigger and assert exactly that.
        int ride_frames = 0;
        for (; ride_frames < 600 && lift.state == ElevatorState::Elevating; ++ride_frames) {
            lift.handle_hero_landing(along, pos, true, vel);   // the hero is still on it
            lift.update(dt, pos, vel);
        }
        check(lift.state == ElevatorState::Elevating && lift.at_target(pos),
              "while ridden, mode 1 keeps elevating and reaches its target");
        check(near(pos[0], lift.target[0]) && near(pos[1], lift.target[1]),
              "the platform arrived (a mid-flight turn-round would fail this)");

        // Rider steps off: it must sit still, then return ~1 s later -- not before.
        float still_seconds = 0.0f;
        while (lift.state == ElevatorState::Elevating && still_seconds < 0.5f) {
            lift.update(dt, pos, vel);
            still_seconds += dt;
        }
        check(lift.state == ElevatorState::Elevating &&
                  still_seconds >= 0.5f,
              "mode 1 does not turn round immediately when the rider steps off");
        while (lift.state == ElevatorState::Elevating && still_seconds < 2.0f) {
            lift.update(dt, pos, vel);
            still_seconds += dt;
        }
        check(lift.state == ElevatorState::Returning,
              "mode 1 returns one second after the LAST trigger");
        // The timer is reset at the END of the last ridden update, so the count
        // here can be one frame either side of the literal 1.0.
        check(still_seconds >= kElevatorHoldSeconds - 2.0f * dt &&
                  still_seconds <= kElevatorHoldSeconds + 4.0f * dt,
              "...and the one second is the engine's own literal, not a guess");
        std::printf("  (mode 1: %d frames ridden, then %.3f s of dwell before returning)\n",
                    ride_frames, still_seconds);

        // Mode 0: inert. It neither self-starts nor reacts to the hero.
        ElevatorController inert;
        inert.prepare(Rect{0.0f, 0.0f, 1.0f, 6.0f}, 4.0f);
        inert.set_binding_value(kElevatorModeBindingIndex, 0);
        check(inert.mode == ElevatorMode::Inert, "binding 1 sets the mode");
        float ipos[2] = {0.0f, 0.0f};
        float ivel[2] = {0.0f, 0.0f};
        const float ialong[2] = {inert.travel[0] / 6.0f, inert.travel[1] / 6.0f};
        check(!inert.handle_hero_landing(ialong, ipos, true, ivel),
              "mode 0 ignores a hero landing");
        for (int i = 0; i < 120; ++i) inert.update(dt, ipos, ivel);
        check(near(ipos[0], 0.0f) && near(ipos[1], 0.0f), "mode 0 never self-starts");

        // Mode 2: autonomous -- self-starts from idle, ignores the hero.
        ElevatorController auto_lift;
        auto_lift.prepare(Rect{0.0f, 0.0f, 1.0f, 6.0f}, 4.0f);
        auto_lift.set_binding_value(kElevatorModeBindingIndex, 2);
        float apos[2] = {0.0f, 0.0f};
        float avel[2] = {0.0f, 0.0f};
        check(!auto_lift.handle_hero_landing(ialong, apos, true, avel),
              "mode 2 ignores a hero landing (it is already cycling)");
        auto_lift.update(dt, apos, avel);
        check(auto_lift.state == ElevatorState::Elevating,
              "mode 2 self-starts from idle without any trigger");
    }

    // ── Elevator: ReturnToOriginalPosition ──────────────────────────────────
    {
        ElevatorController lift;
        lift.prepare(Rect{0.0f, 0.0f, 2.0f, 2.0f}, 5.0f);
        float pos[2] = {10.0f, 20.0f};
        float vel[2] = {0.0f, 0.0f};
        lift.start_elevating(pos, vel);
        lift.hold_timer = 0.0f;
        // Wander a little, then call it home.
        pos[0] += 1.0f;
        pos[1] -= 1.0f;
        lift.return_to_original(pos, vel);
        check(lift.state == ElevatorState::Returning, "ReturnToOriginalPosition() enters state 2");
        check(near(lift.target[0], lift.origin[0]) && near(lift.target[1], lift.origin[1]),
              "the return target is the captured origin");
        const float dx = lift.target[0] - pos[0];
        const float dy = lift.target[1] - pos[1];
        const float len = std::sqrt(dx * dx + dy * dy);
        check(near(vel[0], (dx / len) * 5.0f) && near(vel[1], (dy / len) * 5.0f),
              "the return velocity is re-aimed from the current position, not the old one");

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 600 && !lift.at_target(pos); ++i) lift.update(dt, pos, vel);
        check(near(pos[0], lift.origin[0]) && near(pos[1], lift.origin[1]),
              "it lands back on the origin");
    }

    // ── The world-bounds registration fires once per move, not per frame ────
    {
        ElevatorController lift;
        lift.prepare(Rect{0.0f, 0.0f, 1.0f, 8.0f}, 4.0f);
        float pos[2] = {0.0f, 0.0f};
        float vel[2] = {0.0f, 0.0f};
        lift.start_elevating(pos, vel);
        lift.hold_timer = 0.0f;   // the trigger's reset, as above
        const float dt = 1.0f / 60.0f;
        int dirty = 0, moved = 0;
        for (int i = 0; i < 300; ++i) {
            ElevatorStep s = lift.update(dt, pos, vel);
            if (s.moved) ++moved;
            if (s.bounds_dirty) ++dirty;
            if (lift.at_target(pos)) break;
        }
        check(moved > 1, "the platform moved across many frames");
        check(dirty == 1,
              "RegisterForWorldBoundsUpdate fires once, not once per frame of travel");
    }

    std::fflush(stdout);
    if (g_failures == 0)
        std::printf("PASS: caver level mechanics (door transition + elevator state machine)\n");
    else
        std::printf("FAIL: %d assertion(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
