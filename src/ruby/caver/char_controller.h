#pragma once
// char_controller.h — CharControllerComponent, the layer that *drives* the
// character animation controller. char_anim.h is what plays; this is what says
// when. The two are separate components in the guest and the split matters: the
// anim controller has no opinion about the world, and the character controller
// never touches a blend node except through the anim controller's methods.
//
// Sources (all arm64_13, libswordigo v1.4.13):
//
//   CharControllerComponent::Update                          0x35A960
//   CharControllerComponent::StartMovingToDirection           0x35D578
//   CharControllerComponent::SetInitialAnimationControllerState 0x35DC68
//   CharControllerComponent::DropWithoutAnimation              (referenced by Update)
//
// ── The outlets Update reaches through ──────────────────────────────────────
//
// Update does almost nothing but resolve outlets and copy fields between them,
// so the offsets *are* the specification. Cached targets hang off the object, and
// the two-argument form `this + 104 / this + 128` is
// ComponentOutletBase::Connect, which is only called when the cache is empty:
//
//   base    cached  what it is                       fields read
//   +104    +120    the movement / ground component  +120 height above ground
//                                                    +132 ?, +156 jump speed,
//                                                    +168 written, +208 vertical
//                                                    speed
//   +128    +144    the animation controller          +352 action node
//                                                     +360 swing node
//   +272            the swingable weapon controller   vtable +264 = its current
//                                                     animation node
//   +248            the cast / gesture source
//   +320/+344       sound effects
//
// and `this + 392` is a second, directly-used strong ref to the anim controller
// (the outlet cache at +144 and +392 are kept in step by Update's resolve dance).
//
// ── The object's own fields ─────────────────────────────────────────────────
//
//   +134 float jump-animation accumulator
//   +141 float grounded time          (accumulated while +556 is set)
//   +142 float air time               (accumulated while +556 is clear)
//   +151 int   cleared on a ledge step-up
//   +146 int   air sub-state; 2 is set by the ledge step-up
//   +152 → as float at +608, the current run speed handed to SetCurrentRunSpeed
//   +154 float the ledge step-up's half jump speed
//   +188 float swing countdown        (the pre-swing delay, dt-decremented)
//   +448 bool  weapon hidden
//   +552 int   moving direction       (elsewhere +552/+556 are the pair Update
//                                      copies out of the movement state)
//   +556 bool  on the ground as far as the *animation* is concerned
//   +572 bool  jump grace             (cleared once air_time exceeds 0.2)
//   +584 int   air sub-state          (SpawnState::air_state; 3 = jump clip)
//   +596 bool  is falling             (set once, on the first falling frame)
//   +692 bool  a cast is in progress

#include <cstdint>

#include "ruby/caver/char_anim.h"

namespace caver {
namespace cchar {

// ── The recovered constants ────────────────────────────────────────────────
//
// Every one of these is a literal compare in Update, not a tuned value.

// `+572` is cleared when the *air* timer passes this, i.e. the animation-level
// coyote window: the jump grace the character keeps after leaving the ground.
inline constexpr float kJumpGraceTime = 0.2f;
// StopMoving fires when the anim controller still reports "moving" this long
// after the ground was lost — the run cycle is dropped in the air.
inline constexpr float kAirRunStopTime = 0.11f;
// The falling clip needs this much clearance before it is allowed: the height
// test is `ground outlet +120 > 0.15`, alongside `+208 < 0.0` (moving *down*).
inline constexpr float kFallClearance = 0.15f;
// Every action fade the driver asks for is the same number.
inline constexpr float kActionBlendTime = 0.08f;
// The "is a swing still running" test: `(duration * 0.9 - time) * speed *
// speed_multiplier > 0.01` on the swing node (+360).
inline constexpr float kSwingTailFraction = 0.9f;
inline constexpr float kSwingTailEpsilon = 0.01f;

// ── What the driver reads about the character ──────────────────────────────
//
// The guest reads these straight off the movement component each frame; the port
// has no such component, so they are passed in. Nothing here is the driver's own
// state — that is CharControllerState below.
// Note what is *absent*: whether the character is on the ground. That is +556 and
// the driver owns it as state — see start_moving_to_direction /
// stop_moving_to_direction, which are the only things that write it. Passing it in
// per frame would erase the one mechanism the ground stop is built on.
struct CharInput {
    // Movement component +120: clearance under the character.
    float height_above_ground = 0.0f;
    // +712, the movement behaviour, which gates the +548 latch copy below.
    int   movement_behavior = 1;     // MovementBehavior::Walk
    // Movement component +208: vertical speed, positive up.
    float vertical_speed = 0.0f;
    // Movement component +156, stored to +608 and handed to SetCurrentRunSpeed.
    // The component's own +624, copied to +608 by Update and then handed to
    // SetCurrentRunSpeed. It is a speed the component holds, not a movement-field
    // read, so a caller with no such field may pass the character's actual speed.
    float run_speed = 0.0f;
    // The jump input after the CanJump check that gates it upstream.
    bool  jump_pressed = false;
    // LineSegmentIntersectsGround came back true on the step-up probe: the
    // character is being lifted onto a ledge, which animates as a *ground* jump.
    bool  ledge_step_up = false;
    // The swingable weapon controller (+272) actually has a current animation
    // node for the driver to blend from. The port has no weapon controller, so it
    // says so instead of the driver pretending to resolve one.
    bool  swing_node_available = false;
    // The cast finished on this frame: the +692 flag went from set to clear.
    bool  cast_finished = false;
};

// ── The driver's own state ─────────────────────────────────────────────────
struct CharControllerState {
    // +556. Not a physics test: the *movement* path owns it. StartMovingToDirection
    // sets it, StopMovingToDirection clears it, and Update only reads it.
    bool  on_ground = true;
    bool  jump_grace = false;        // +572
    bool  is_falling = false;        // +596
    bool  cast_active = false;       // +692
    float ground_time = 0.0f;        // +141
    float air_time = 0.0f;           // +142
    float jump_anim_time = 0.0f;     // +134
    float swing_countdown = 0.0f;    // +188
    // +552, owned by the movement path: Start/StopMovingToDirection write it and
    // Update only reads it. 3 in the *air sub-state* (+584) selects the jump clip.
    int   moving_direction = 0;      // +552
    int   latched_direction = 0;     // +548, Update's gated copy of +552
    int   air_state = 0;             // +584
    float current_speed = 0.0f;      // +608, Update's copy of +624
    float ledge_jump_speed = 0.0f;   // +154
};

// ── The driver ─────────────────────────────────────────────────────────────
class CharController {
public:
    CharControllerState state;

    // CharControllerComponent::StartMovingToDirection (0x35D578). Calls
    // StartMoving() unconditionally once the anim controller resolves — the
    // direction argument does not gate it — then latches `+552 = dir` and raises
    // the ground flag. This and SetInitialAnimationControllerState are the only
    // two callers of StartMoving in the binary.
    void start_moving_to_direction(int direction, canim::CharAnimController& anim);

    // CharControllerComponent::StopMovingToDirection (0x35D6AC), and the whole
    // body is three stores:
    //
    //     if (+552 == dir) { +556 = 0; +568 = 0; +552 = 0; }
    //
    // It never calls into the animation controller. Stopping the run cycle is done
    // by *clearing the ground flag*, so the next Update takes the airborne branch
    // and the 0.11 s rule cuts the cycle itself. A ground stop and a mid-air stop
    // are therefore one code path, and a caller that reaches for the anim
    // controller directly to stop a run cycle gets a different (instant) result
    // from the engine's. `+568` is cleared but its meaning is unrecovered.
    void stop_moving_to_direction(int direction);

    // CharControllerComponent::Update (0x35A960). The order below is the order of
    // the body, and it is observable: the run-cycle stop is tested before the
    // jump, the falling clip is opened after the jump branch, and the swing
    // countdown is handled after both.
    //
    // Returns whether the anim controller was asked to change clip, so a caller
    // can log or test the decision without re-deriving it.
    bool update(float dt, const CharInput& in, canim::CharAnimController& anim);

    // CharControllerComponent::SetInitialAnimationControllerState (0x35DC68),
    // which is a thin wrapper: seed the driver's copied fields, then make the one
    // spawn-time call into the anim controller.
    void set_initial_animation_state(const canim::SpawnState& spawn,
                                     canim::CharAnimController& anim);

    // The `+188` countdown, armed when a swing begins. It is a *delay*, not a
    // duration: Update decrements it and only when it reaches <= 0.01 does the
    // weapon's node get blended in over 0.08 s.
    void begin_swing(float delay) { state.swing_countdown = delay; }

    // The `v12` test: is a swing still running on the anim controller's +360
    // node? `(duration * 0.9 - time) * speed * speed_multiplier > 0.01`. This is
    // the value Update passes to StartJumping as `air_jump != 1`. It reads the
    // node's *own* clock and playback rate, so a paused or rewound swing is not
    // "still running".
    static bool swing_still_running(const canim::CharAnimController& anim);
};

} // namespace cchar
} // namespace caver
