#pragma once
// char_anim.h — the *character animation controller*: the animation outlets, the
// three-node blend tree they are wired into, the transitions that move the blend
// weights, and the action / casting slot. This is the layer that *chooses* what
// plays; anim_bones.h is the layer that samples and skins it.
//
// Sources (all arm64_13, libswordigo v1.4.13):
//
//   CharAnimControllerComponent::ctor                    0x356898
//   CharAnimControllerComponent::Prepare                 0x357090
//   CharAnimControllerComponent::Reset                   0x3574B4
//   CharAnimControllerComponent::ActionNearlyFinished    0x3574D8
//   CharAnimControllerComponent::Update                  0x357500
//   CharAnimControllerComponent::SetCurrentRunSpeed      0x35782C
//   CharAnimControllerComponent::IsMoving                0x3578B4
//   CharAnimControllerComponent::StartMoving             0x3578D4
//   CharAnimControllerComponent::StopMoving              0x35799C
//   CharAnimControllerComponent::StartJumping            0x357A20
//   CharAnimControllerComponent::CanStartFalling         0x357C08
//   CharAnimControllerComponent::StartFalling            0x357C84
//   CharAnimControllerComponent::StandOnGround           0x357D14
//   CharAnimControllerComponent::IsReadyToJump           0x357D28
//   CharAnimControllerComponent::SwingWithAnimation      0x357D80
//   CharAnimControllerComponent::BeginCasting            0x357EF0
//   CharAnimControllerComponent::FinishCasting           0x358090
//   CharAnimControllerComponent::StopAction              0x358298
//   CharAnimControllerComponent::BlendFromAnimationNode  0x3583DC
//   CharAnimControllerComponent::BlendToAnimationNode    0x358638
//   CharAnimControllerComponent::BlendToMoveAnimation    0x3587B0
//   CharAnimControllerComponent::LoadFromProtobufMessage 0x356DBC
//   AnimationControllerComponent::ctor                   0x32C604
//   AnimationControllerComponent::Prepare                0x32CC18
//   AnimationControllerComponent::Update                 0x32CE28
//   AnimationControllerComponent::BlendToAnimationNode   0x32CF38
//   CharControllerComponent::SetInitialAnimationControllerState  0x35DC68
//   CharControllerComponent::StartMovingToDirection      0x35D578
//   CharControllerComponent::Update                      0x35A960  (the anim calls)
//
// ── The outlets ─────────────────────────────────────────────────────────────
//
// The constructor (0x356898) binds six outlets by *identifier*, not by name, and
// the proto message carries those six identifiers as six int fields
// (0x356DBC reads `result[3]` through `result[8]` and, for each, drops the cached
// target when the identifier changed). So an outlet is "the component this
// object's <id> field points at", and the names below are only the constructor's
// defaults:
//
//   id   ctor name            outlet base   cached target   role
//   100  "standAnimation"     176           192             idle clip
//   101  "walkAnimation"      200           216             run cycle
//   102  "jumpAnimation"      224           240             jump clip
//   103  "airJumpAnimation"   248           264             the double jump
//   104  "fallAnimation"      272           288             falling clip
//   105  "castAnimation"      296           312             the spell cast
//
// (The name lengths are independently confirmed by the constructor's own size
// byte: it stores `size << 1` for a libc++ short string, so `0x20` on
// "airJumpAnimation" is 2 × 16.)
//
// ── The tree ────────────────────────────────────────────────────────────────
//
// `Prepare` (0x357090) builds exactly three blend nodes and nothing else:
//
//   moveBlend @320 = AnimBlendNode(stand, walk, 0.1, 0.1)
//   airBlend  @328 = AnimBlendNode(jump,  fall, 0.35, 0.35)
//   rootBlend @336 = AnimBlendNode(moveBlend, airBlend, 0.1, 0.1)
//
// Note that rootBlend's children are the other two blends, and that each child
// therefore has to evaluate before its parent can — rootBlend is not a list of
// clips, it is the ground/air decision over two sub-decisions.
//
// The last argument is `blend_in_time`, and it matters: 0.1/0.1 and 0.35/0.35 are
// symmetric, so easing *and* un-easing take that long. moveBlend blends idle↔run,
// airBlend blends jump↔fall, and rootBlend blends "on the ground" against "in the
// air" — which is why the same tree serves standing, running, jumping and falling.
//
// ── The transitions, and what they actually are ─────────────────────────────
//
// Every one of them is a two-flag write plus, sometimes, a clip phase reset. The
// flags are the blend node's `reverse` and `blending` bytes, written together:
// 0x0100 advances toward the *right* child, 0x0101 retreats toward the *left*
// (see anim_bones.h — 0x0101 sets both bytes).
//
//   StartMoving (0x3578D4)      walk.time = walk.duration × 0.25; move → right
//   StopMoving  (0x35799C)      stand.time = 0;                    move → left
//   StartJumping(air) (0x357A20) airBlend.left = air ? airJump : jump;
//                                that node's time = 0; airBlend.alpha = 0;
//                                airBlend → left (self-clearing, so the air blend
//                                is parked on the jump clip); root → right
//   StartFalling (0x357C84)     fall.time = 0; root → right; air → right
//   StandOnGround (0x357D14)    root → left
//
// Two consequences worth stating plainly, because both look wrong from the
// outside and both are exactly what the binary does:
//
//   * `IsMoving` (0x3578B4) is `moveBlend != null && moveBlend.reverse == 0`. It
//     is not a velocity test and never reads the character's speed.
//   * `Reset` (0x3574B4) clears `blending` and `alpha` on all three nodes and
//     deliberately leaves `reverse` alone.
//
// The clip entering points are phases, not seconds: 0.25 of the run cycle on
// StartMoving (mid-stride), 0.5 when an action hands back to the run (0x357500
// and FinishCasting's null branch), 0 on every other clip.
//
// ── The action slot ────────────────────────────────────────────────────────
//
// Above the tree sits one more blend node (+368) and a few book-keeping fields:
//
//   +344 bool  "auto-return" gate      SwingWithAnimation sets it, casting clears it
//   +352 node  m_action                the clip the *character* is committed to
//   +360 node  m_swingNode             the same pointer, kept separately
//   +368 node  the action blend        what actually reaches the model
//   +376 float the action countdown    seconds until the fade back begins
//
// `BlendFromAnimationNode` (0x3583DC) parks the action blend on the action clip —
// alpha 0, blending off, blend_in_time -1.0 — and arms the countdown with
// `max((node.duration - node.time) - blendTime, 0.01)`. The action plays at alpha
// 0; when the countdown expires, `Update` sets `blending` and the single alpha
// sweep 0 → 1 *is* the fade back to the move animation. Then rootBlend takes over
// and the slot is a passthrough.
//
// `BlendToAnimationNode` (0x358638) is the derived class's override, and it is
// not an extension of the base's version (0x32CF38) — it replaces it, and the
// difference is the whole design:
//
//   base:   transient blend = AnimBlendNode(m_currentNode, node, t, t);
//           m_currentNode = node;
//   char:   from = actionBlend ? actionBlend.right : rootBlend;
//           if (from == node) return;
//           actionBlend = AnimBlendNode(from, node, t, -1.0);
//           // m_currentNode is NOT touched here
//
// Because `from` is always the previous *destination*, chained transitions
// (swing → cast → stop) each fade from wherever the character visually was, and
// the base's `m_currentNode` (+152) is instead rewritten by the derived `Update`
// once per frame to `actionBlend ? actionBlend : rootBlend`. That rewrite is the
// reason the override exists at all: the base's version would fight it.
//
// ── What is NOT recovered ──────────────────────────────────────────────────
//
// The Lua library `AnimationController` that AnimationControllerComponent::
// Prepare registers has its function table at `off_6194E0` in .data.rel.ro, and
// these text-only dumps do not carry data sections — so the script-facing method
// names are not recovered (only the library name is). The editor-facing
// `GetBindings` / `GetEnumValuesForBindedProperty` pair (0x357060, 0x35706C) is
// likewise an index-into-a-table, and the table is not in the dumps.

#include <cstdint>
#include <string>

#include <array>
#include <string>

#include "ruby/caver/anim_bones.h"

namespace caver {
namespace canim {

// ── NodeRef — which node an AnimNode reference points at ────────────────────
//
// The engine's slots hold `boost::intrusive_ptr<AnimNode>`, and this controller
// genuinely uses the polymorphism: `BlendToAnimationNode` puts the *root blend*
// into the action slot, and `BlendFromAnimationNode` builds a blend whose left
// child is a KeysNode and whose right child is that same tree. `anim_bones::
// BlendNode`'s children are `KeysNode*`, which is the right type for the tree
// itself and too narrow for the action slot, so references are tagged here rather
// than cast.
struct NodeRef {
    enum class Kind : uint8_t { None, Keys, Mix };

    Kind kind = Kind::None;
    anim::KeysNode* keys = nullptr;
    struct MixNode* mix = nullptr;

    static NodeRef keys_of(anim::KeysNode* node) { return NodeRef{Kind::Keys, node, nullptr}; }
    static NodeRef mix_of(MixNode* node) { return NodeRef{Kind::Mix, nullptr, node}; }

    bool null() const { return kind == Kind::None; }
    bool is_keys() const { return kind == Kind::Keys && keys != nullptr; }
    bool is_mix() const { return kind == Kind::Mix && mix != nullptr; }
    bool same(const NodeRef& other) const {
        return kind == other.kind && keys == other.keys && mix == other.mix;
    }
};

// The engine has one AnimBlendNode class, and this is it. `anim_bones::BlendNode`
// is a keys-typed *specialisation* of the same node — its children are
// `KeysNode*`, which is all the standalone `AnimKeysNode`/`AnimBlendNode` pair
// needs — and it is not reused here because the character controller's tree needs
// the general form: `rootBlend`'s children are `moveBlend` and `airBlend`, both
// blends. The alpha law (`anim::advance_blend`) and the crossfade
// (`anim::crossfade`) are shared with it, so the only thing this type adds is the
// wider child type.
struct MixNode {
    NodeRef from;                  // AnimBlendNode::left   (+16)
    NodeRef to;                    // AnimBlendNode::right  (+24)
    float alpha = 0.0f;            // +32
    // +36/+40. The defaults are the guest's 0-argument constructor (0x324DD0),
    // which loads 0xBF8000003F800000 into the pair: blend_time 1.0,
    // blend_in_time -1.0. "No blend node here" is spelled as null children, not
    // as a sentinel in these fields — -1.0 in blend_in_time is a *meaningful*
    // value ("use blend_time when reversing").
    float blend_time = 1.0f;       // +36
    float blend_in_time = -1.0f;   // +40
    bool reverse = false;          // +44
    bool blending = false;         // +45

    // The pair as one 16-bit store, which is how the guest writes it. Note that
    // the *low* byte is `reverse` — see anim_bones.h's note on the flags.
    uint16_t flags_word() const { return anim::blend_flags(reverse, blending); }
    void set_flags_word(uint16_t word) {
        reverse = (word & anim::kBlendReverseBit) != 0;
        blending = (word & anim::kBlendBlendingBit) != 0;
    }
    // AnimBlendNode::Update (0x324E9C): the guest returns "no change" before
    // touching alpha when either child is missing, so the guard is here.
    //
    // The recursion is real and it is easy to miss: the blend node updates both
    // children with the same dt and folds their two booleans into its own
    // answer, so *every* clip in the tree keeps its own clock running all the
    // time and only the weights change. That is why StartMoving can re-phase the
    // walk clip without first having to start it.
    bool update(float dt);
};

// AnimNode::Update's dispatch: a KeysNode advances its own clock and reports
// whether its (frame, fraction) pair moved (anim_bones::KeysNode::update); a
// MixNode does the above.
bool update_node(NodeRef node, float dt);

// AnimNode::Evaluate's dispatch, then AnimBlendNode::Evaluate (0x324FB8).
// A KeysNode fills its own buffer and answers with it; a MixNode crossfades its
// children and answers with the buffer that won.
anim::FloatDataBuffer* evaluate_node(NodeRef node);

// ── AnimationControllerComponent — the base ────────────────────────────────
//
//   ctor     0x32C604 binds outlet 0 "model" (outlet base 104, cached 120) and
//            outlet 1 "defaultAnimation" (base 128, cached 144)
//   +152 node m_currentNode     seeded from the defaultAnimation outlet in Prepare
//   +168 node m_blendNode       a transient crossfade, dropped once it completes
struct AnimationController {
    NodeRef current_node;      // +152
    MixNode transient_blend;   // +168
    bool model_present = false;

    // AnimationControllerComponent::Prepare (0x32CC18): `m_currentNode` is the
    // defaultAnimation outlet's node. Everything else the class does is relative
    // to that one node.
    void prepare(NodeRef default_animation);

    // AnimationControllerComponent::BlendToAnimationNode (0x32CF38). Note the
    // symmetric `(t, t)`: the base eases in and out over the same time.
    void blend_to_animation_node(NodeRef node, float blend_time);

    // AnimationControllerComponent::Update (0x32CE28). Drives the transient blend
    // while there is one and drops it once `alpha > 0.999`, otherwise drives
    // `m_currentNode`. The node it drives is reported rather than skinned here —
    // see driven_node().
    void update(float dt);

    // Which node this frame's pose comes from, or null.
    NodeRef driven_node;
};

// ── CharAnimControllerComponent ────────────────────────────────────────────
class CharAnimController {
public:
    // ── the six outlets (ctor 0x356898) ──
    enum OutletId : int {
        kStandAnimation   = 100,
        kWalkAnimation    = 101,
        kJumpAnimation    = 102,
        kAirJumpAnimation = 103,
        kFallAnimation    = 104,
        kCastAnimation    = 105,
    };
    static constexpr int kOutletCount = 6;
    // The constructor's own default name for an identifier, for diagnostics.
    static const char* outlet_name(int outlet_id);
    // The blend times, all four of them literals at their call sites. The first
    // two are the tree's own, baked in by Prepare; the other two are passed to
    // BlendToAnimationNode.
    static constexpr float kMoveBlendTime   = 0.1f;   // rootBlend, 0x357090
    static constexpr float kAirBlendTime    = 0.35f;  // airBlend itself, 0x357090
    static constexpr float kActionBlendTime = 0.08f;  // SwingWithAnimation (0x357D80), casting (0x357EF0)
    static constexpr float kReturnBlendTime = 0.1f;   // the 95% auto-return, 0x357500
    // The action-return threshold, strictly greater: `time > duration * 0.95`
    // (0x3574D8 and the opening test of 0x357500).
    static constexpr float kActionReturnThreshold = 0.95f;
    // The countdown floor in BlendFromAnimationNode.
    static constexpr float kActionTimerFloor = 0.01f;

    // ── preparation ──

    // Prepare (0x357090): build the three-node tree out of the five clips the
    // tree needs. Returns false if any of them is unbound, because a tree with a
    // hole in it would evaluate to zeroes rather than to a warning.
    bool prepare();

    // Reset (0x3574B4): alpha 0 and blending off on all three nodes, `reverse`
    // deliberately untouched.
    void reset();

    // ── outlets ──
    //
    // The port resolves an outlet by identifier, which is what the proto load
    // (0x356DBC) and ComponentOutlet::setIdentifier do.
    bool bind(int outlet_id, anim::KeysNode* node);
    // LoadFromProtobufMessage's rule: a *changed* identifier drops the cached
    // target, an unchanged one keeps it.
    void set_outlet_identifier(int outlet_id, int identifier);
    int outlet_identifier(int outlet_id) const;
    anim::KeysNode* outlet(int outlet_id) const;

    // ── transitions ──
    void start_moving();                              // 0x3578D4
    void stop_moving();                               // 0x35799C
    void set_current_run_speed(float speed);          // 0x35782C
    bool is_moving() const;                           // 0x3578B4
    void start_jumping(bool air_jump);                // 0x357A20
    bool is_ready_to_jump() const;                    // 0x357D28
    bool can_start_falling() const;                   // 0x357C08
    void start_falling();                             // 0x357C84
    void stand_on_ground();                           // 0x357D14

    // ── the action slot ──
    //
    // The three signatures that name `AnimKeysNode` are typed as such, because
    // that is what the binary declares and because it matters: the engine reads
    // `+12`/`+40` off the action node as a clock and a duration, and those offsets
    // mean something else entirely on a blend node.
    bool action_nearly_finished() const;                        // 0x3574D8
    void blend_from_animation_node(anim::KeysNode& node, float blend_time);  // 0x3583DC
    // 0x358638 (derived) / 0x32CF38 (base) — this one really is any AnimNode.
    void blend_to_animation_node(NodeRef node, float blend_time);
    void blend_to_move_animation(float blend_time);                  // 0x3587B0
    void swing_with_animation(anim::KeysNode& node);            // 0x357D80
    void begin_casting();                                       // 0x357EF0
    void finish_casting(anim::KeysNode* node);                   // 0x358090
    void stop_action();                                         // 0x358298

    // Update (0x357500): the three steps, then the base's Update.
    void update(float dt);

    // ── state, for tests and for the port's driver ──
    MixNode& move_blend() { return move_; }
    MixNode& air_blend() { return air_; }
    MixNode& root_blend() { return root_; }
    const MixNode& move_blend() const { return move_; }
    const MixNode& air_blend() const { return air_; }
    const MixNode& root_blend() const { return root_; }

    // The action slot's blend node, which is where the 0.08 s fades and the
    // countdown's alpha sweep live.
    const MixNode& action_blend() const { return action_blend_; }
    // Mutable, because the slot's `blending`/`alpha` are state the *engine*
    // reaches on its own (the countdown sets `blending`, the node clears it when
    // the sweep clamps) and a driver that wants to inspect or stage that state
    // has no other way in. Nothing in this class reads it back inconsistently.
    MixNode& action_blend() { return action_blend_; }
    anim::KeysNode* action() const { return action_; }
    anim::KeysNode* swing_node() const { return swing_node_; }
    float action_timer() const { return action_timer_; }
    // The +344 gate: does the action return on its own at 95%?
    bool action_auto_returns() const { return action_gate_; }
    bool action_blend_present() const { return action_blend_present_; }

    // `m_currentNode`, rewritten every Update to `actionBlend ? actionBlend :
    // rootBlend`. This is the node the model is posed from.
    NodeRef current_node() const { return base_.current_node; }
    const AnimationController& base() const { return base_; }
    AnimationController& base() { return base_; }

    // The pose source after Update, from the base's own rule.
    NodeRef driven_node() const { return base_.driven_node; }

    // ── port adapter, not a recovery ───────────────────────────────────────
    //
    // The engine mixes and hands the model one crossfaded buffer. The port's
    // render stream carries a single clip name, so something has to pick, and
    // `display_node` picks the way the engine's own `evaluate` does once alpha
    // has left the middle band: the child the weight favours. It reproduces the
    // guest's branch structure exactly — it just resolves the 0.001..0.999 band
    // by weight instead of by blending, because there is one name to give.
    NodeRef display_node() const;
    // The weight `display_node` won with, in 0..1. Unrecovered: the engine has no
    // such scalar, it has a whole buffer.
    float display_weight() const;

    // Whether Prepare has built the tree. Every guard in the guest is this test —
    // `+320 != null`, `+328 != null`, `+336 != null` — and *not* "the clips
    // resolved": an object with an unbound stand clip still writes its blend
    // flags, and the difference is observable through IsMoving().
    bool tree_ready() const { return tree_ready_; }

private:
    anim::KeysNode* walk() const { return outlet(kWalkAnimation); }

    // All three tree nodes are MixNodes, not anim::BlendNode: see the note on
    // MixNode above. move_ and air_ happen to have keys children, root_ does not.
    MixNode move_, air_, root_;
    anim::KeysNode* outlets_[kOutletCount] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    int identifiers_[kOutletCount] = {kStandAnimation, kWalkAnimation, kJumpAnimation,
                                      kAirJumpAnimation, kFallAnimation, kCastAnimation};

    // The action slot. `action_blend_present_` is the guest's "+368 != null": the
    // port owns the node, so "present" is tracked separately from its contents,
    // and clearing it is what StopAction and the 95% return do.
    MixNode action_blend_;
    bool action_blend_present_ = false;
    // +352 and +360. Both are AnimKeysNode, not AnimNode: nothing that writes
    // them takes anything wider, and the 95% test reads a clip clock off them.
    anim::KeysNode* action_ = nullptr;
    anim::KeysNode* swing_node_ = nullptr;
    float action_timer_ = 0.0f;
    bool action_gate_ = false;
    bool tree_ready_ = false;

    AnimationController base_;
};

// ── the driver: CharControllerComponent ────────────────────────────────────
//
// The controller above has no opinion about *when*; the character controller is
// what moves it. The three fields SetInitialAnimationControllerState (0x35DC68)
// reads are the spawn state:
//
//   +552 int  moving direction     non-zero selects the run cycle
//   +584 int  an air sub-state; 3 selects the jump clip
//   +596 bool is-falling           selects the fall clip otherwise
//
// The +584 enumeration is *not* recovered — only the value 3 is, as a literal
// compare — so the field is named for what it does and not for what it means.
struct SpawnState {
    int moving_direction = 0;
    int air_state = 0;
    bool is_falling = false;
};

// CharControllerComponent::SetInitialAnimationControllerState (0x35DC68):
// `Reset`, then a *separate* `if` for moving followed by an if/else-if pair for
// jumping and falling — see char_controller.cpp for why that is not one chain.
void set_initial_animation_state(CharAnimController& controller, const SpawnState& spawn);

// ── ClipBinding — the port's rig, not a recovery ────────────────────────────
//
// In the guest the six outlets are ComponentOutlet references to six
// KeyframeAnimation components, and each of those owns its clip. The port has no
// component graph at this layer — it has clip *names* — so something has to own
// six AnimKeysNodes with stable addresses for the controller's pointers to point
// at, and map a node back to the name the render stream carries. That is all this
// is: the recovered controller above, wired to real storage.
//
// One degradation is deliberate and named rather than hidden. An AnimKeysNode
// owns its own clock, but AnimKeysNode::Update (0x3254B0) can only run that clock
// when it has a clip — the step needs the clip's fps and the clamp needs its
// duration — and the port's runtime layer has neither (the *renderer* turns
// seconds into frames from the pod's fps). So the nodes here carry no clip, and
// this class runs the recovered step itself:
//
//     time += speed * dt * speed_multiplier
//
// which is the same expression, clamped the same way when a duration is known.
// The visible consequence of not knowing one: `set_phase(p)` in the recovered
// controller writes `time = duration * p`, and with duration 0 that is a restart
// from the first frame instead of a quarter-phase entry. set_duration() is the
// seam for a caller that does know — the viewer's pod loader does — and until it
// is called the phases degrade to restarts, which is the honest behaviour.
class ClipBinding {
public:
    static constexpr int kCount = CharAnimController::kOutletCount;

    // Bind six clip names (empty = unbound outlet, the guest's null outlet) and
    // build the tree. `model_present` is the model outlet: without it the base
    // class's Update poses nothing at all.
    bool configure(const std::array<std::string, kCount>& names, bool model_present);

    bool ready() const { return configured_; }
    CharAnimController& controller() { return controller_; }
    const CharAnimController& controller() const { return controller_; }

    // Which outlet a node is, or -1. This is the name lookup: the controller
    // reports a *node*, and the render stream needs a clip name.
    int index_of(const anim::KeysNode* node) const;
    // The other direction, for a caller that has a clip *name* (the port's hurt /
    // die reactions name a clip, not an outlet).
    int index_of_name(const std::string& name) const;
    const std::string& name_at(int index) const;
    anim::KeysNode* node_at(int index);

    // KeyframeAnimationComponent.SpeedMultiplier, per outlet: the node's +48.
    void set_speed(int index, float speed);
    // See the class note. 0 (the default) degrades set_phase to a restart.
    void set_duration(int index, float seconds);

    // The recovered step, per outlet, for every outlet — the tree's clips all
    // keep their own clock (AnimBlendNode::Update recurses into *both* children).
    void advance(float dt);
    float time_at(int index) const;

private:
    std::array<anim::KeysNode, kCount> nodes_;
    std::array<std::string, kCount> names_;
    CharAnimController controller_;
    bool configured_ = false;
};

} // namespace canim
} // namespace caver
