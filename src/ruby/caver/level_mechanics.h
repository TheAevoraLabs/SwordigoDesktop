#pragma once
// level_mechanics.h — Caver's DoorController and ElevatorController, recovered.
//
// These two used to be *guessed* in `behaviour.cpp` (`tick_door` moved the door
// at a hardcoded 1.5 units/s, `tick_elevator` at 2.0 units/s with a travel limit
// scraped out of the elevation shape). Neither constant exists in the engine.
// This is the transcription of what it actually does, read off the arm64_13
// decompilation:
//
//   Caver::DoorControllerComponent::Update                     0x3A0C00
//   Caver::DoorControllerComponent::SetOpen(bool)              0x3A05BC
//   Caver::DoorControllerComponent::Open()                     0x3A08E8
//   Caver::DoorControllerComponent::Close()                    0x3A0AB0
//   Caver::DoorControllerComponent::Prepare()                  0x3A0690
//   Caver::DoorControllerComponent::GetBindings(v)             0x3A0460
//   Caver::DoorControllerComponent::ValueForBindedProperty     0x3A0578
//   Caver::DoorControllerComponent::SetValueForBindedProperty  0x3A05A4
//
//   Caver::ElevatorControllerComponent::Prepare()              0x3A1790
//   Caver::ElevatorControllerComponent::StartElevating()       0x3A19D4
//   Caver::ElevatorControllerComponent::IsAtTargetPosition()   0x3A1AA0
//   Caver::ElevatorControllerComponent::ReturnToOriginalPosition() 0x3A1AD8
//   Caver::ElevatorControllerComponent::Update(float)          0x3A1B8C
//   Caver::ElevatorControllerComponent::HandleMessage(int, f*) 0x3A1EB4
//   Caver::ElevatorControllerComponent::GetBindings(v)         0x3A1374
//
// ## The door has no motion law at all
//
// `DoorControllerComponent::Update` is a *forwarder*: it resolves the
// AnimationController outlet and tail-calls its virtual `Update(dt)`. There is no
// rate, no distance, no lerp. `SetOpen(bool)` does four things and nothing else:
//
//   1. `Open()` or `Close()`, which write the animation's playback scale
//      (+52: 0.5f opening, 1.0f closing) and a 16-bit animation flag
//      (+56: 0x01 opening, 0x0101 closing), play the open/close SoundEffect, set
//      the component's `isOpen` byte (+200), and enable/disable the collision
//      shapes on the owning SceneObject.
//   2. Seek the animation's time field (+12) to the clip's *open frame*
//      (clip +48) when opening, or to 0 when closing.
//
// So the door's travel IS the clip. Until the ANI loader is wired
// (`CAVER_BACKEND_STATUS.md` open item 2) there is no honest 0..1 "open amount";
// `DoorController::open_amount()` reports exactly that, rather than a number
// produced by a fabricated constant.
//
// ## The elevator is velocity-driven, and integrates its own position
//
// The elevator is not a lerp either. `Prepare()` derives a *travel offset* from
// the trigger shape's bounds; `StartElevating()` captures the owner's position as
// the origin and sets `target = origin + travel`, then points the **owner's own
// velocity** (+120/+124) along `normalize(target - pos) * speed`. `Update()`
// integrates that velocity into the owner's position, snapping to the target and
// zeroing the velocity when the remaining distance is under one frame's step.
//
// The state machine is `{ 0 idle, 1 elevating, 2 returning }` at +148, and `Mode`
// at +144 selects who may trigger it:
//
//   0  inert     — never self-starts and `HandleMessage` ignores the hero
//   1  hero-      — the data default (`GetBindings`: "mode", default 1). A hero
//      triggered   landing with a contact direction aligned with the travel axis
//                  (|dot| > 0.9) starts it. `HandleMessage` runs every frame the
//                  hero is still on it and resets the hold timer each time, so
//                  the platform keeps rising while ridden and returns to the
//                  origin one second after the LAST trigger — it does not wait a
//                  second at the far end.
//   2  autonomous — self-starts from idle, and returns on arrival.
//
// The 1 s hold and the arrival epsilon are the engine's own literals (`1.0`,
// `0.0001`), not tunables.

#include <cstdint>

#include "ruby/caver/collision.h"   // caver::Rect, the same Rectangle the engine uses

namespace caver {
namespace mechanics {

// ── Door ─────────────────────────────────────────────────────────────────────

// Binding table, `DoorControllerComponent::GetBindings` (0x3A0460): exactly one
// binding beyond Component's own, index 4, name "open", 4 bytes wide, default 0
// (closed). `ValueForBindedProperty`/`SetValueForBindedProperty` both special-case
// index 4 and nothing else.
constexpr int kDoorOpenBindingIndex = 4;
constexpr int kDoorOpenBindingSize = 4;
constexpr int kDoorOpenBindingDefault = 0;

// `Open()` 0x3A08E8 / `Close()` 0x3A0AB0 write these into the animation.
constexpr float kDoorOpenPlaybackScale = 0.5f;     // 0x3F000000
constexpr float kDoorClosedPlaybackScale = 1.0f;   // 0x3F800000

// Which SoundEffect the transition asks for. `Open()` plays the outlet at
// component +176, `Close()` the one at +152; both are gated on a byte at
// `*(animationController + 120) + 326` being non-zero.
enum class DoorSound { None, Open, Close };

// What a single `SetOpen()` decided. The caller owns the object, the animation
// and the sound component, so this is the decision, not the side effect.
struct DoorTransition {
    bool  changed = false;          // false when the request matched the current state
    bool  open = false;             // the component's isOpen byte (+200)
    bool  collision_enabled = true; // collision shapes: disabled on open, enabled on close
    float playback_scale = kDoorClosedPlaybackScale;
    bool  animation_playing = false;
    bool  animation_reversed = false;
    int   animation_seek_frame = 0; // the animation's time field (+12)
    DoorSound sound = DoorSound::None;
};

// One DoorControllerComponent. `open_frame` is the clip's open frame (clip +48),
// which the engine reads through
// `KeyframeAnimationComponent::LoadAnimation` and the component's clip pointer at
// +152 — the caller supplies it because the ANI loader is not wired yet.
class DoorController {
public:
    // `SetOpen(bool)` 0x3A05BC, with `Open()`/`Close()` folded in. `sound_gate`
    // is the engine's `*(*(animationController + 120) + 326) != 0` check.
    DoorTransition set_open(bool want, int open_frame, bool sound_gate);

    // A hero landing on the door (`PerformBindedAction` is empty; the door reacts
    // only through its binding), i.e. binding 4 arriving from Lua or a message.
    DoorTransition apply_binding_value(int index, bool value, int open_frame,
                                       bool sound_gate) {
        if (index != kDoorOpenBindingIndex) return DoorTransition{};
        return set_open(value, open_frame, sound_gate);
    }

    bool is_open() const { return open_; }
    bool binding_value(int index) const {
        return index == kDoorOpenBindingIndex ? open_ : false;
    }
    // True once `set_open` has run at least once, matching `Prepare()`, which
    // ends by calling `SetOpen(this, this->isOpen)` to seat the initial state.
    bool prepared() const { return prepared_; }
    void mark_prepared() { prepared_ = true; }

    // `Update(float)` 0x3A0C00 is a tail-call to the bound AnimationController's
    // virtual Update. It advances nothing here on its own, so what it means for
    // the port is "the clip's own clock advances, nothing else".
    void update(float /*dt*/) {}

    // The door's own `Open()`/`Close()` walk the owning SceneObject's component
    // list and `SetEnabled(false)`/`(true)` every component that implements
    // `CollisionShapeComponent::Interface`. Opening a door therefore *takes its
    // collision away*; this is the whole reason a closed door blocks and an open
    // one does not.
    bool collision_enabled() const { return !open_; }

    // 0..1 travel, in the engine's terms: the animation's normalised time. Returns
    // false when it cannot be known, which is the case whenever the clip's
    // duration is not loaded — the honest answer, and deliberately not a 0/1 step.
    bool open_amount(const float* animation_time, float clip_duration,
                     float* out) const;

private:
    bool open_ = false;
    bool prepared_ = false;
};

// ── Elevator ─────────────────────────────────────────────────────────────────

// Binding table, `ElevatorControllerComponent::GetBindings` (0x3A1374): one
// binding, index 1, name "mode", 2 bytes, default 1.
constexpr int kElevatorModeBindingIndex = 1;
constexpr int kElevatorModeBindingSize = 2;
constexpr int kElevatorModeBindingDefault = 1;

// The component constructor (0x3A0FB4) writes `*((_DWORD *)this + 43) =
// 1127481344` — bits 0x43340000, i.e. **180.0**. There is no Speed field: the
// payload is only `Mode` and `ElevationShapeId` (Proto::ElevatorController-
// Component has exactly those two k*FieldNumber globals), and nothing but the
// constructor ever writes +172. So every elevator in the game moves at 180
// units/s, and the previous port's 2.0 units/s was off by two orders of
// magnitude.
constexpr float kElevatorDefaultSpeed = 180.0f;   // 0x43340000

// `LoadFromProtobufMessage` (0x3A117C) stores `payload.Mode - 1` into +144, and
// only when the field is present (presence bit `& 2`). The payload is therefore
// 1-BASED (1..3); the internal enum below is what the engine's switch reads.
constexpr int kElevatorPayloadModeBase = 1;

// `Update` compares squared distances against 1e-4 in five places.
constexpr float kElevatorArriveEpsilonSq = 0.0001f;
// `case 1`, mode 1: keep elevating while the hold timer is <= 1.0. The timer is
// zeroed by every `HandleMessage`, so this is "time since the last trigger".
constexpr float kElevatorHoldSeconds = 1.0f;
// LABEL_37: `settle_timer` accumulates only while `hold_timer` is below 0.1,
// otherwise it restarts from 0 — i.e. it is "has this trigger just begun".
constexpr float kElevatorTimerRestartBelow = 0.1f;
// The contact direction must be aligned with the travel axis (`|dot| > 0.9`).
constexpr float kElevatorTriggerDot = 0.9f;

enum class ElevatorMode : int {
    Inert = 0,          // never self-starts; HandleMessage ignores the hero
    HeroTriggered = 1,  // the shipped default
    Autonomous = 2,     // self-starts from idle; returns on arrival
};

enum class ElevatorState : int { Idle = 0, Elevating = 1, Returning = 2 };

enum class ElevatorSound { None, Start };
enum class ElevatorHold { None, StopAtTarget };

// What one step decided, for the caller to apply to the object.
struct ElevatorStep {
    float position[2] = {0, 0};     // the owner's position after integration
    float velocity[2] = {0, 0};     // the owner's velocity (+120/+124) after
    bool  moved = false;            // position changed by more than 1e-4
    bool  bounds_dirty = false;     // `RegisterForWorldBoundsUpdate` fired
    ElevatorState state = ElevatorState::Idle;
    ElevatorHold hold = ElevatorHold::None;
};

class ElevatorController {
public:
    // `Prepare()` 0x3A1790: derive the travel offset from the trigger shape's
    // bounds (`Shape::Bounds` on the component's shape), then seat the initial
    // AABB. It deliberately does NOT touch the origin, the target or the state:
    // those are the constructor's, and Prepare runs on every re-init. `speed`
    // defaults to the constructor's 180.0 because nothing else sets it.
    void prepare(const Rect& shape_bounds, float speed = kElevatorDefaultSpeed);

    // `StartElevating()` 0x3A19D4. Writes the owner's velocity (+120/+124), which
    // is the ONLY thing this controller ever does to the platform — Update then
    // integrates it. Pass the owner's velocity array in as `owner_velocity`.
    void start_elevating(const float owner_pos[2], float owner_velocity[2]);

    // `payload.Mode` (1-based) → the internal mode (+144). Absent stays default 1.
    static ElevatorMode mode_from_payload(int payload_mode) {
        const int internal = payload_mode - kElevatorPayloadModeBase;
        if (internal < 0 || internal > 2) return ElevatorMode::HeroTriggered;
        return static_cast<ElevatorMode>(internal);
    }

    // `ReturnToOriginalPosition()` 0x3A1AD8. Same contract as start_elevating.
    void return_to_original(const float owner_pos[2], float owner_velocity[2]);

    // `IsAtTargetPosition()` 0x3A1AA0.
    bool at_target(const float owner_pos[2]) const;

    // `HandleMessage(7, msg)` 0x3A1EB4 — the hero landing. `contact_normal` is
    // the message's vector; `hero_on_object` is the sender's flag byte (+419).
    // Returns true when it started an ascent, in which case `owner_velocity` has
    // been pointed at the target.
    bool handle_hero_landing(const float contact_normal[2], const float owner_pos[2],
                             bool hero_on_object, float owner_velocity[2]);

    // `Update(float)` 0x3A1B8C. `owner_pos`/`owner_velocity` are in/out: the
    // controller integrates the position itself, exactly as the engine does.
    ElevatorStep update(float dt, float owner_pos[2], float owner_velocity[2]);

    ElevatorMode mode = ElevatorMode::HeroTriggered;
    ElevatorState state = ElevatorState::Idle;
    bool  origin_captured = false;
    float origin[2] = {0, 0};       // +156/+160
    float travel[2] = {0, 0};       // +164/+168
    float speed = kElevatorDefaultSpeed;   // +172
    float target[2] = {0, 0};       // +176/+180
    // The constructor writes the pair at +192 as 0x7F7FFFFF00000000: settle_timer
    // = 0 and hold_timer = FLT_MAX, so an untriggered elevator reads as "the last
    // trigger was an eternity ago" rather than as "just triggered".
    float hold_timer = 3.4028235e38f;   // +196, FLT_MAX
    float settle_timer = 0.0f;      // +192
    Rect  local_bounds;             // +128..+143, the swept AABB from Prepare
    // The owner SceneObject's `+196` byte: set the first time the platform moves,
    // cleared when the world-bounds pass consumes it. It exists so a moving
    // platform registers for a bounds update once per move, not once per frame.
    bool  bounds_flag = false;

    // The binding is "mode"; the engine's own default is 1.
    void set_binding_value(int index, int value) {
        if (index != kElevatorModeBindingIndex) return;
        mode = static_cast<ElevatorMode>(value);
    }
    int binding_value(int index) const {
        return index == kElevatorModeBindingIndex ? static_cast<int>(mode)
                                                  : kElevatorModeBindingDefault;
    }
};

}  // namespace mechanics
}  // namespace caver
