// caver_char_anim_test — the recovered character animation controller.
//
// Pins the things about CharAnimControllerComponent (0x356898 … 0x3587B0) and its
// base AnimationControllerComponent (0x32C604 … 0x32CF38) that are decidable from
// the binary and wrong in every reasonable guess:
//
//   * the six outlets are bound by *identifier* (100..105) and the constructor's
//     default names are stand/walk/jump/airJump/fall/cast Animation;
//   * Prepare builds three blend nodes, and the third one's children are the
//     other two — so the tree is a decision over two decisions, not a clip list;
//   * every transition is one 16-bit flag write, and both spellings occur:
//     0x0100 toward the right child, 0x0101 toward the *left* (both bytes set);
//   * IsMoving is `!moveBlend.reverse`, i.e. a flag and not a speed, and Reset
//     deliberately does not clear that flag;
//   * the clip entry points are *phases* — 0.25 of the run cycle on StartMoving,
//     0.5 when an action hands back — so they scale with the clip's duration;
//   * SetCurrentRunSpeed writes `speed / 100` into the node's +52 factor and
//     leaves its own +48 speed alone; the two are multiplied by the node's Update;
//   * the derived BlendToAnimationNode *replaces* the base's and never touches
//     m_currentNode — the derived Update writes it once per frame instead, and
//     that is the whole reason the override exists;
//   * the action slot parks at alpha 0 (BlendFromAnimationNode) and the *single*
//     alpha sweep back out is what the countdown expires into;
//   * an action auto-returns at `time > duration * 0.95`, but only while the +344
//     gate is set — casting clears it, because the skill decides when a cast ends.
//
// Pure RAM: no guest, no GL, no Qt.

#include <cmath>
#include <cstdio>
#include <string>

#include "ruby/caver/anim_bones.h"
#include "ruby/caver/char_anim.h"

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

using caver::anim::KeysNode;
using caver::canim::CharAnimController;
using caver::canim::MixNode;
using caver::canim::NodeRef;
using caver::canim::SpawnState;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A clip standing in for one of the six KeyframeAnimation clips. The nodes need a
// real one, because AnimKeysNode::Update (0x3254B0) returns before touching its
// clock when the clip is missing — so a node without a clip never advances and
// would silently make every "did it move" assertion vacuous.
static void set_clip(caver::anim::Clip& clip, float duration, float fps = 10.0f) {
    clip.frame_count = 2;
    clip.fps = fps;
    clip.duration = duration;
    clip.frames.assign(2, caver::anim::FloatDataBuffer{});
    for (auto& frame : clip.frames) frame.data.assign(12, 0.0f);
}

static KeysNode make_node(caver::anim::Clip& clip) {
    KeysNode node;
    node.init_with_animation(&clip);   // duration = clip.duration - 0.001 (0x325364)
    node.speed = 1.0f;
    node.speed_multiplier = 1.0f;
    return node;
}

// The full six-clip fixture the tree needs, plus the swing action clip.
struct Fixture {
    caver::anim::Clip stand_clip, walk_clip, jump_clip, air_jump_clip, fall_clip, cast_clip, swing_clip;
    KeysNode stand, walk, jump, air_jump, fall, cast, swing;

    CharAnimController controller;

    Fixture() {
        set_clip(stand_clip, 1.0f);
        set_clip(walk_clip, 2.0f);
        set_clip(jump_clip, 0.6f);
        set_clip(air_jump_clip, 0.6f);
        set_clip(fall_clip, 1.2f);
        set_clip(cast_clip, 0.8f);
        set_clip(swing_clip, 0.4f);
        stand = make_node(stand_clip);
        walk = make_node(walk_clip);
        jump = make_node(jump_clip);
        air_jump = make_node(air_jump_clip);
        fall = make_node(fall_clip);
        cast = make_node(cast_clip);
        swing = make_node(swing_clip);

        controller.bind(CharAnimController::kStandAnimation, &stand);
        controller.bind(CharAnimController::kWalkAnimation, &walk);
        controller.bind(CharAnimController::kJumpAnimation, &jump);
        controller.bind(CharAnimController::kAirJumpAnimation, &air_jump);
        controller.bind(CharAnimController::kFallAnimation, &fall);
        controller.bind(CharAnimController::kCastAnimation, &cast);
        controller.base().model_present = true;
        controller.prepare();
    }
};

// The two bits of the flag word, named so the assertions read as what they test.
static constexpr uint16_t kReverseBit = caver::anim::kBlendReverseBit;    // byte 44
static constexpr uint16_t kBlendingBit = caver::anim::kBlendBlendingBit;  // byte 45

int main() {
    // ── the outlets are identified by number, and named by the constructor ──
    check(std::string(CharAnimController::outlet_name(100)) == "standAnimation",
          "outlet 100 is standAnimation (0x356898)");
    check(std::string(CharAnimController::outlet_name(101)) == "walkAnimation",
          "outlet 101 is walkAnimation");
    check(std::string(CharAnimController::outlet_name(102)) == "jumpAnimation",
          "outlet 102 is jumpAnimation");
    check(std::string(CharAnimController::outlet_name(103)) == "airJumpAnimation",
          "outlet 103 is airJumpAnimation");
    check(std::string(CharAnimController::outlet_name(104)) == "fallAnimation",
          "outlet 104 is fallAnimation");
    check(std::string(CharAnimController::outlet_name(105)) == "castAnimation",
          "outlet 105 is castAnimation");

    {
        // LoadFromProtobufMessage (0x356DBC): a changed identifier drops the
        // cached target, an unchanged one keeps it.
        Fixture f;
        check(f.controller.outlet_identifier(CharAnimController::kWalkAnimation) == 101,
              "the ctor's identifier for walkAnimation is 101");
        f.controller.set_outlet_identifier(CharAnimController::kWalkAnimation, 101);
        check(f.controller.outlet(CharAnimController::kWalkAnimation) == &f.walk,
              "an unchanged identifier keeps the resolved outlet");
        f.controller.set_outlet_identifier(CharAnimController::kWalkAnimation, 214);
        check(f.controller.outlet(CharAnimController::kWalkAnimation) == nullptr,
              "a changed identifier drops the resolved outlet");
    }

    // ── Prepare: three nodes, and the root's children are the other two ──
    {
        Fixture f;
        const MixNode& move = f.controller.move_blend();
        const MixNode& air = f.controller.air_blend();
        const MixNode& root = f.controller.root_blend();

        check(move.from.is_keys() && move.from.keys == &f.stand && move.to.keys == &f.walk,
              "moveBlend = AnimBlendNode(stand, walk) (0x357090)");
        check(near(move.blend_time, 0.1f) && near(move.blend_in_time, 0.1f),
              "moveBlend blends in and out over 0.1 s");

        check(air.from.keys == &f.jump && air.to.keys == &f.fall,
              "airBlend = AnimBlendNode(jump, fall)");
        check(near(air.blend_time, 0.35f) && near(air.blend_in_time, 0.35f),
              "airBlend blends in and out over 0.35 s");

        // The decision over two decisions: rootBlend's children are blend nodes.
        check(root.from.is_mix() && root.from.mix == &f.controller.move_blend(),
              "rootBlend's left child is moveBlend, not a clip");
        check(root.to.is_mix() && root.to.mix == &f.controller.air_blend(),
              "rootBlend's right child is airBlend, not a clip");
        check(near(root.blend_time, 0.1f), "rootBlend blends in and out over 0.1 s");

        // The tree is not being blended yet, and Reset is what parks it.
        check(near(move.alpha, 0.0f) && near(air.alpha, 0.0f) && near(root.alpha, 0.0f),
              "Prepare leaves every alpha at 0");
    }

    // ── the flag words, and IsMoving as a flag rather than a speed ──
    {
        Fixture f;
        f.controller.start_moving();
        check(f.controller.move_blend().flags_word() == caver::anim::kBlendTowardRight,
              "StartMoving writes the word 0x0100 to the move blend (0x3578D4)");
        check((f.controller.move_blend().flags_word() & kBlendingBit) != 0,
              "0x0100 sets the *high* byte, which is `blending` (byte 45)");
        check((f.controller.move_blend().flags_word() & kReverseBit) == 0,
              "…and clears the low byte, which is `reverse` (byte 44)");
        check(f.controller.is_moving(), "IsMoving is true once the move blend heads right");
        // The walk clip enters at a quarter of its *own* duration, so ~0.5 s for a
        // 2 s clip — a phase, not 0.25 s.
        check(near(f.walk.time, f.walk.duration * 0.25f),
              "StartMoving phases the run cycle to 0.25 of its duration");
        check(f.walk.time > 0.4f && f.walk.time < 0.6f,
              "…which is set_phase(0.25), not a fixed 0.25 s");

        f.controller.stop_moving();
        check(f.controller.move_blend().flags_word() == caver::anim::kBlendTowardLeft,
              "StopMoving writes 0x0101 (0x35799C)");
        check((f.controller.move_blend().flags_word() & kBlendingBit) != 0,
              "0x0101 sets *both* bytes: the backward move is a fade, not a teleport");
        check(!f.controller.is_moving(), "IsMoving is false once the move blend heads left");
        check(near(f.stand.time, 0.0f), "StopMoving restarts the idle clip from frame 0");

        // Reset clears only `blending` and alpha; `reverse` — and therefore
        // IsMoving — survives it. That is why the word after a Reset is 0x0001
        // and not the parked 0x0000.
        f.controller.reset();
        check((f.controller.move_blend().flags_word() & kBlendingBit) == 0,
              "Reset clears `blending` (0x3574B4)");
        check(near(f.controller.move_blend().alpha, 0.0f), "Reset clears alpha");
        check((f.controller.move_blend().flags_word() & kReverseBit) != 0,
              "…and leaves `reverse` standing, so the word is 0x0001, not 0x0000");
        check(f.controller.move_blend().flags_word() != caver::anim::kBlendParked,
              "…i.e. Reset does *not* park the pair");
        check(!f.controller.is_moving(),
              "IsMoving still reflects the last direction, not the reset");
    }

    // ── jump, fall, land: three flag writes and two clip restarts ──
    {
        Fixture f;
        f.controller.start_jumping(false);
        check(f.controller.air_blend().from.keys == &f.jump,
              "a single jump puts the jump clip in the air blend's left slot (0x357A20)");
        check(near(f.jump.time, 0.0f), "…and restarts it");
        check(near(f.controller.air_blend().alpha, 0.0f),
              "the air blend is parked at alpha 0");
        check(f.controller.air_blend().flags_word() == caver::anim::kBlendTowardLeft,
              "…by a backward sweep that clamps immediately");
        check(f.controller.root_blend().flags_word() == caver::anim::kBlendTowardRight,
              "and the root blend fades toward the air");

        f.controller.start_jumping(true);
        check(f.controller.air_blend().from.keys == &f.air_jump,
              "a double jump swaps the left slot to the air-jump clip, not a third node");
        check(near(f.air_jump.time, 0.0f), "…restarting that clip too");

        // CanStartFalling (0x357C08): still on the ground animation, or fully in
        // the air animation, or the jump clip is nearly over.
        f.controller.reset();
        check(f.controller.can_start_falling(),
              "CanStartFalling is true while the root blend is still on the ground");
        f.controller.root_blend().alpha = 1.0f;
        f.controller.air_blend().alpha = 1.0f;
        check(f.controller.can_start_falling(),
              "CanStartFalling is true once the air blend is fully in");

        // Neither of the first two clauses, so the third decides — and it reads
        // the clip in the air blend's *left* slot, which the previous block
        // swapped to the air-jump clip.
        f.controller.air_blend().alpha = 0.5f;
        f.air_jump.time = 0.0f;
        check(!f.controller.can_start_falling(),
              "a jump clip with its whole length left has not finished, so no falling");
        f.air_jump.time = f.air_jump.duration - 0.05f;
        check(f.controller.can_start_falling(),
              "…but one within 0.1 s of its end is finished enough (timeToFinish)");
        f.air_jump.loops = true;
        f.air_jump.time = 0.0f;
        check(f.controller.can_start_falling(),
              "a looping jump clip reports 0 remaining and never blocks a fall");

        f.controller.start_falling();
        check(near(f.fall.time, 0.0f), "StartFalling restarts the fall clip (0x357C84)");
        check(f.controller.air_blend().flags_word() == caver::anim::kBlendTowardRight,
              "…and fades the air blend toward it");
        check(!near(f.controller.air_blend().alpha, 0.0f),
              "StartFalling does not reset the air blend's alpha; the fade continues");

        f.controller.stand_on_ground();
        check(f.controller.root_blend().flags_word() == caver::anim::kBlendTowardLeft,
              "StandOnGround is one backward flag write on the root blend (0x357D14)");
    }

    // ── run speed lands on the node's second factor ──
    {
        Fixture f;
        f.walk.speed = 1.0f;          // the clip's own factor (+48)
        f.controller.set_current_run_speed(250.0f);
        check(near(f.walk.speed_multiplier, 2.5f),
              "SetCurrentRunSpeed writes speed / 100 into the +52 factor (0x35782C)");
        check(near(f.walk.speed, 1.0f), "…and leaves the node's own +48 speed alone");
        check(f.walk.time == 0.0f, "…without touching the clip clock");
    }

    // ── the action chain: `from` is the previous destination ──
    {
        Fixture f;
        f.controller.swing_with_animation(f.swing);
        check(f.controller.action() == &f.swing && f.controller.swing_node() == &f.swing,
              "SwingWithAnimation takes both the action and the swing slot (0x357D80)");
        check(f.controller.action_auto_returns(),
              "…and arms the +344 auto-return gate");
        check(near(f.swing.time, 0.0f), "…and restarts the swing clip");
        check(near(f.controller.action_blend().blend_time, 0.08f),
              "…with the 0.08 s fade-in from the literal at the call site");
        check(near(f.controller.action_blend().blend_in_time, -1.0f),
              "-1.0 blend_in_time is the \"use blend_time when reversing\" sentinel");
        check(f.controller.action_blend().from.mix == &f.controller.root_blend(),
              "the first action fades *from* the move tree");
        check(f.controller.action_blend().to.keys == &f.swing, "…and to the swing clip");
        check(f.controller.action_blend().blending, "and the fade is running");

        // A second action chains from the previous destination, not from the tree.
        f.controller.begin_casting();
        check(!f.controller.action_auto_returns(),
              "BeginCasting clears the gate: the skill decides when a cast ends (0x357EF0)");
        check(f.controller.action() == &f.cast, "…and installs the cast clip as the action");
        check(f.controller.action_blend().from.keys == &f.swing,
              "the cast fades from the *swing* clip, i.e. from where the character was");
        check(f.controller.action_blend().to.keys == &f.cast, "…to the cast clip");

        // BlendToMoveAnimation goes through the derived override, so it chains too.
        f.controller.blend_to_move_animation(0.08f);
        check(f.controller.action_blend().to.mix == &f.controller.root_blend(),
              "BlendToMoveAnimation fades to the move tree (0x3587B0)");
        check(f.controller.action_blend().from.keys == &f.cast,
              "…from the cast clip, because the override chains destinations");
    }

    // ── the override never touches m_currentNode; Update writes it ──
    {
        Fixture f;
        f.controller.swing_with_animation(f.swing);
        check(f.controller.base().current_node.null(),
              "BlendToAnimationNode (derived, 0x358638) does not set m_currentNode");
        check(f.controller.base().transient_blend.from.null(),
              "…and leaves the base's transient blend unused");
        // At alpha 0 a blend returns its *from* child, so the action slot's
        // alpha-0 state is the move tree — the swing clip fades in from there.
        check(near(f.controller.action_blend().alpha, 0.0f),
              "a fresh action blend starts at alpha 0");
        f.controller.update(1.0f / 60.0f);
        check(f.controller.current_node().mix == &f.controller.action_blend(),
              "Update points m_currentNode at the action blend (0x357500 step 3)");
        check(f.controller.display_node().keys == &f.stand,
              "…so the pose early in the fade is still the move tree's idle clip");
        for (int i = 0; i < 6; ++i) f.controller.update(1.0f / 60.0f);
        check(near(f.controller.action_blend().alpha, 1.0f),
              "…and 0.08 s later the swing clip has faded fully in");
        check(f.controller.display_node().keys == &f.swing,
              "…so the pose is the swing clip");

        // With no action, the slot is the tree, and the tree's own alpha decides.
        f.controller.stop_action();
        f.controller.update(1.0f / 60.0f);
        check(f.controller.current_node().mix == &f.controller.action_blend(),
              "StopAction fades to the tree through the action slot, so the slot stays");
    }

    // ── the single alpha sweep is the fade back out ──
    {
        Fixture f;
        f.swing.time = 0.0f;
        f.controller.swing_with_animation(f.swing);
        // Run the swing clip past 95% of its length. The 95% test is the first
        // thing Update does, so one further update is needed for it to fire.
        for (int i = 0; i < 100 && f.swing.time <= f.swing.duration * 0.95f; ++i) {
            f.controller.update(1.0f / 60.0f);
        }
        f.controller.update(1.0f / 60.0f);
        check(f.swing.time > f.swing.duration * 0.95f, "the swing clip reached 95%");
        check(f.controller.action() == nullptr,
              "…and the action slot was released by the auto-return (0x357500 step 1)");
        check(f.controller.swing_node() == nullptr, "…along with the swing slot");
        // The re-phase happens in step 1 of the same update whose step 3 then
        // advances every clock, so the observable value is half the clip plus
        // one frame — a tolerance far below the quarter/half difference.
        check(near(f.walk.time, f.walk.duration * 0.5f, 1.0f / 60.0f + 1e-3f),
              "…after re-phasing the run cycle to *half*, not the quarter StartMoving uses");
        check(f.walk.time > f.walk.duration * 0.4f,
              "…which is nowhere near the quarter phase StartMoving applies");
        check(f.controller.action_blend().to.mix == &f.controller.root_blend(),
              "…and the fade back to the move tree is what the slot now holds");
        check(f.controller.action_blend().from.keys == &f.swing,
              "…fading *from* the swing clip, since that was the previous destination");
        check(f.controller.current_node().mix == &f.controller.action_blend(),
              "the slot is still the action blend while that fade runs, not the tree");

        // And the fade completes: 0.1 s at 60 Hz.
        for (int i = 0; i < 12; ++i) f.controller.update(1.0f / 60.0f);
        check(near(f.controller.action_blend().alpha, 1.0f),
              "the sweep reaches alpha 1, which puts the move tree fully in");
        check(f.controller.display_node().keys == &f.stand,
              "…and the display adapter resolves through it to the idle clip");
    }

    // ── the countdown, and the gate that bypasses it ──
    {
        Fixture f;
        f.swing.time = 0.15f;                    // 0.399 s clip, so 0.249 s remain
        f.controller.blend_from_animation_node(f.swing, 0.08f);
        check(f.controller.action_blend_present(), "BlendFromAnimationNode installs a blend (0x3583DC)");
        check(near(f.controller.action_blend().alpha, 0.0f),
              "…parked at alpha 0, so the action clip shows unmixed");
        check(!f.controller.action_blend().blending,
              "…with blending off: the action plays before the fade back starts");
        check(near(f.controller.action_blend().blend_in_time, -1.0f),
              "…and the reversing sentinel in blend_in_time");
        // timer = max((0.399 - 0.15) - 0.08, 0.01) = 0.169
        check(near(f.controller.action_timer(), 0.169f, 1e-3f),
              "the countdown is (duration - time) - blendTime (0x3583DC tail)");

        // The countdown does not run while the blend is already animating.
        f.controller.action_blend().blending = true;
        const float before = f.controller.action_timer();
        f.controller.update(0.05f);
        check(near(f.controller.action_timer(), before),
              "the countdown is suspended while the blend is animating");
        f.controller.action_blend().blending = false;

        // It runs down, and expiring it is what starts the sweep.
        for (int i = 0; i < 20 && !f.controller.action_blend().blending; ++i)
            f.controller.update(1.0f / 60.0f);
        check(f.controller.action_blend().blending,
              "the countdown expiring sets `blending`, which *is* the fade back");
        check(f.controller.action_blend().reverse == false,
              "…with reverse untouched, so the sweep heads for the move tree");
        check(f.controller.action_timer() <= 0.001f, "…and the countdown is spent");
    }

    // ── casting: the gate off means no auto-return even at the very end ──
    {
        Fixture f;
        f.controller.begin_casting();
        f.cast.time = f.cast.duration;           // 100% of the clip
        f.controller.update(1.0f / 60.0f);
        check(f.controller.action() == &f.cast,
              "an action finished past 100% does *not* auto-return while the gate is off");
        check(f.controller.action_nearly_finished(),
              "…even though ActionNearlyFinished is true (0x3574D8)");
        check(f.controller.swing_node() == nullptr, "begin_casting leaves the swing slot alone");

        // FinishCasting with a clip hands over to that clip; without one it
        // re-phases the run cycle to half and blends to the move tree.
        f.controller.finish_casting(&f.swing);
        check(f.controller.action() == &f.swing, "FinishCasting(node) installs the clip (0x358090)");
        check(!f.controller.action_auto_returns(), "…still with the gate off");
        check(near(f.swing.time, 0.0f), "…restarting it");

        f.controller.finish_casting(nullptr);
        check(f.controller.action() == &f.swing,
              "FinishCasting(null) does not clear the action slot; it blends to the tree");
        check(near(f.walk.time, f.walk.duration * 0.5f),
              "…after re-phasing the run cycle to half");
        check(f.controller.action_blend().to.mix == &f.controller.root_blend(),
              "…toward the move tree");

        f.controller.stop_action();
        check(f.controller.action() == nullptr && f.controller.swing_node() == nullptr,
              "StopAction releases both action slots (0x358298)");
    }

    // ── the blend node drives its children's clocks ──
    {
        Fixture f;
        MixNode& move = f.controller.move_blend();
        move.update(0.1f);
        check(near(f.stand.time, 0.1f) && near(f.walk.time, 0.1f),
              "MixNode::update advances *both* children's clocks by the full dt");
        check(near(move.alpha, 0.0f), "…without touching its own alpha while not blending");

        // The composite answer. With the children's clocks frozen they report no
        // change, which is the one case a parked blend reports nothing.
        Fixture g;
        MixNode& parked = g.controller.move_blend();
        g.stand.update_time = false;
        g.walk.update_time = false;
        check(!parked.update(0.1f),
              "a parked blend over unchanged children reports nothing (0x324E9C)");
        parked.blending = true;
        parked.alpha = 0.5f;
        check(parked.update(0.1f), "a blending node reports a change");
        check(parked.alpha > 0.5f, "…because its own alpha moved");
        check(!parked.blending || parked.alpha <= 1.0f, "…and the sweep stays in range");
    }

    // ── the spawn state's precedence ──
    {
        Fixture f;
        SpawnState spawn;
        spawn.moving_direction = 1;
        caver::canim::set_initial_animation_state(f.controller, spawn);
        check(f.controller.is_moving(), "spawning while moving starts the run cycle");

        spawn.air_state = 3;
        caver::canim::set_initial_animation_state(f.controller, spawn);
        check(f.controller.air_blend().from.keys == &f.jump,
              "state 3 beats the moving direction: the jump clip wins (0x35DC68)");
        check(f.controller.move_blend().flags_word() == caver::anim::kBlendTowardRight,
              "…and the run cycle was still started first, not skipped");

        spawn.air_state = 0;
        spawn.is_falling = true;
        caver::canim::set_initial_animation_state(f.controller, spawn);
        check(near(f.fall.time, 0.0f), "falling without state 3 starts the fall clip");
        check(f.controller.root_blend().flags_word() == caver::anim::kBlendTowardRight,
              "…fading toward the air");
    }

    // ── the base class is genuinely replaced, not extended ──
    {
        caver::canim::AnimationController base;
        base.model_present = true;
        caver::anim::Clip a_clip, b_clip;
        set_clip(a_clip, 1.0f);
        set_clip(b_clip, 1.0f);
        KeysNode a{make_node(a_clip)};
        KeysNode b{make_node(b_clip)};
        base.prepare(NodeRef::keys_of(&a));
        base.blend_to_animation_node(NodeRef::keys_of(&b), 0.2f);
        check(base.current_node.keys == &b,
              "the *base* BlendToAnimationNode makes the new node current (0x32CF38)");
        check(base.transient_blend.from.keys == &a && base.transient_blend.to.keys == &b,
              "…by crossfading from the old current node");
        check(near(base.transient_blend.blend_time, 0.2f) &&
                  near(base.transient_blend.blend_in_time, 0.2f),
              "…over the same time in both directions, unlike the derived class's -1.0");

        base.update(0.1f);
        check(base.driven_node.mix == &base.transient_blend,
              "the transient blend is what gets posed while it runs");
        for (int i = 0; i < 10; ++i) base.update(0.1f);
        check(base.transient_blend.from.null() && base.transient_blend.to.null(),
              "…and it is dropped once alpha passes 0.999");
        check(base.driven_node.keys == &b, "…after which the current node is posed");

        // No current node means there is nothing to blend from, so nothing
        // happens at all — including the assignment.
        caver::canim::AnimationController fresh;
        fresh.model_present = true;
        fresh.blend_to_animation_node(NodeRef::keys_of(&a), 0.1f);
        check(fresh.current_node.null(), "with no current node the base blend is a no-op");
    }

    if (g_failures == 0) std::printf("PASS: caver char anim controller\n");
    return g_failures == 0 ? 0 : 1;
}
