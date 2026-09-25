// char_anim.cpp — the recovered character animation controller (see char_anim.h
// for the evidence, the addresses and the offsets).
//
// Everything here is a transcription of a named function; where a guard is added
// that the guest does not have, the comment says so and why.

#include "ruby/caver/char_anim.h"

#include <algorithm>
#include <vector>

namespace caver {
namespace canim {

using anim::kBlendHighThreshold;
using anim::kBlendLowThreshold;
using anim::KeysNode;

// ── node evaluation and update dispatch ─────────────────────────────────────

anim::FloatDataBuffer* evaluate_node(NodeRef node) {
    if (node.is_keys()) {
        // AnimKeysNode::Evaluate (0x32559C) fills the node's own buffer; the
        // recovered rule is "zero it, then accumulate the current frame at
        // (1 - fraction) and the next at fraction".
        node.keys->evaluate();
        return &node.keys->result_buffer();
    }
    if (node.is_mix()) {
        anim::FloatDataBuffer* from = evaluate_node(node.mix->from);
        anim::FloatDataBuffer* to   = evaluate_node(node.mix->to);
        return anim::crossfade(from, to, node.mix->alpha);
    }
    return nullptr;
}

bool MixNode::update(float dt) {
    // The guest returns "no change" before touching alpha when either child is
    // missing (0x324E9C), so the guard is part of the transcription.
    if (from.null() || to.null()) return false;

    const bool was_blending = blending;
    anim::advance_blend(blending, reverse, alpha, blend_time, blend_in_time, dt);

    // Both children are updated *after* alpha, and both always — the tree's
    // clips all keep their own clocks. The two booleans are then folded into one
    // "did anything move", which is what lets an idle character skip re-skinning
    // while a running one cannot.
    const bool from_changed = update_node(from, dt);
    const bool to_changed   = update_node(to, dt);

    if (was_blending) return true;
    // `(alpha < 0.001) & ~from_changed` in the binary: the only case that
    // reports nothing while the left child is being returned untouched.
    if (alpha < kBlendLowThreshold && !from_changed) return false;
    return to_changed || alpha <= kBlendHighThreshold;
}

bool update_node(NodeRef node, float dt) {
    if (node.is_keys()) return node.keys->update(dt);
    if (node.is_mix()) return node.mix->update(dt);
    return false;
}

namespace {

struct Leaf {
    KeysNode* node = nullptr;
    float weight = 0.0f;
};

// Every leaf's share of the result, by walking the blend tree: a mix contributes
// (1 - alpha) to its left subtree and alpha to its right. That is not an
// approximation of the guest's branch structure, it *is* the branch structure —
// at alpha 0.001 the right share is 0.001 and at 0.999 the left share is 0.001 —
// which is why the port adapter below reproduces it exactly.
void collect_leaves(NodeRef node, float scale, std::vector<Leaf>& out) {
    if (node.is_keys()) {
        out.push_back(Leaf{node.keys, scale});
        return;
    }
    if (!node.is_mix()) return;
    const MixNode& m = *node.mix;
    collect_leaves(m.from, scale * (1.0f - m.alpha), out);
    collect_leaves(m.to,   scale * m.alpha, out);
}

Leaf heaviest_leaf(NodeRef root) {
    std::vector<Leaf> leaves;
    collect_leaves(root, 1.0f, leaves);
    Leaf best;
    for (const Leaf& leaf : leaves)
        if (leaf.node && leaf.weight > best.weight) best = leaf;
    return best;
}

} // namespace

// ── AnimationControllerComponent — the base (0x32C604, 0x32CC18, 0x32CE28) ──

void AnimationController::prepare(NodeRef default_animation) {
    // Prepare (0x32CC18): the class has exactly one seed node, and it comes from
    // the "defaultAnimation" outlet. Everything else is expressed relative to it.
    current_node = default_animation;
}

void AnimationController::blend_to_animation_node(NodeRef node, float blend_time) {
    // 0x32CF38. Note the asymmetric guard: with no current node there is nothing
    // to blend *from*, so nothing happens and m_currentNode is not even assigned.
    if (current_node.null()) return;

    transient_blend = MixNode{};
    transient_blend.from = current_node;           // AnimBlendNode::left
    transient_blend.to = node;                     // AnimBlendNode::right
    transient_blend.blend_time = blend_time;       // +36
    transient_blend.blend_in_time = blend_time;    // +40 — the same value, unlike
                                                   // the derived class's -1.0
    transient_blend.alpha = 0.0f;
    transient_blend.reverse = false;
    transient_blend.blending = true;               // +45 = 1
    current_node = node;
}

void AnimationController::update(float dt) {
    // 0x32CE28. The guard is the model outlet: with no model there is nothing to
    // pose, so neither slot is advanced.
    if (!model_present) {
        driven_node = NodeRef{};
        return;
    }

    // The transient blend is "present" when it has children: the port owns the
    // node, so the guest's `+168 != null` becomes this, and the base class has no
    // Prepared/not-Prepared distinction to model.
    if (!transient_blend.from.null() && !transient_blend.to.null()) {
        driven_node = NodeRef::mix_of(&transient_blend);
        transient_blend.update(dt);
        // The blend is dropped the frame *after* it completes: the test is
        // `alpha > 0.999`, strictly, and it runs after the node's own Update.
        if (transient_blend.alpha > kBlendHighThreshold) transient_blend = MixNode{};
        return;
    }

    driven_node = current_node;
    update_node(current_node, dt);
}

// ── CharAnimControllerComponent ────────────────────────────────────────────

const char* CharAnimController::outlet_name(int outlet_id) {
    // The constructor's own default strings (0x356898). The lengths are
    // confirmed by the size byte it writes, which is `size << 1` for a libc++
    // short string: 28, 26, 26, 32, 26, 26.
    switch (outlet_id) {
        case kStandAnimation:   return "standAnimation";
        case kWalkAnimation:    return "walkAnimation";
        case kJumpAnimation:    return "jumpAnimation";
        case kAirJumpAnimation: return "airJumpAnimation";
        case kFallAnimation:    return "fallAnimation";
        case kCastAnimation:    return "castAnimation";
        default:                return "<unknown>";
    }
}

bool CharAnimController::prepare() {
    // Prepare (0x357090): three AnimBlendNodes, in this order, and nothing else.
    // Each is 0x30 bytes and each is intrusive_ptr'd into +320/+328/+336.
    //
    //   moveBlend @320 = AnimBlendNode(stand, walk, 0.1, 0.1)
    //   airBlend  @328 = AnimBlendNode(jump,  fall, 0.35, 0.35)
    //   rootBlend @336 = AnimBlendNode(moveBlend, airBlend, 0.1, 0.1)
    //
    // The four float literals are the constructor arguments, in the order
    // (blend_time, blend_in_time). The tree is built unconditionally — the guest
    // does not test whether an outlet resolved, it calls the outlet accessor and
    // passes whatever comes back, null included, into the blend node.
    KeysNode* stand   = outlet(kStandAnimation);
    KeysNode* walk    = outlet(kWalkAnimation);
    KeysNode* jump    = outlet(kJumpAnimation);
    KeysNode* fall    = outlet(kFallAnimation);

    move_ = MixNode{};
    move_.from = NodeRef::keys_of(stand);
    move_.to = NodeRef::keys_of(walk);
    move_.blend_time = kMoveBlendTime;
    move_.blend_in_time = kMoveBlendTime;

    air_ = MixNode{};
    air_.from = NodeRef::keys_of(jump);
    air_.to = NodeRef::keys_of(fall);
    air_.blend_time = kAirBlendTime;
    air_.blend_in_time = kAirBlendTime;

    root_ = MixNode{};
    root_.from = NodeRef::mix_of(&move_);
    root_.to = NodeRef::mix_of(&air_);
    root_.blend_time = kMoveBlendTime;
    root_.blend_in_time = kMoveBlendTime;

    tree_ready_ = true;

    // A tree with a hole would evaluate to zeroes rather than to a warning, so
    // the port reports it even though the guest does not care.
    return stand && walk && jump && fall;
}

void CharAnimController::reset() {
    // Reset (0x3574B4). Only `blending` and `alpha` are cleared — `reverse` is
    // deliberately left standing, which is why IsMoving() still answers the way
    // it did before the reset until the next Start/StopMoving.
    move_.blending = false;
    move_.alpha = 0.0f;
    air_.blending = false;
    air_.alpha = 0.0f;
    root_.blending = false;
    root_.alpha = 0.0f;
}

bool CharAnimController::bind(int outlet_id, KeysNode* node) {
    const int index = outlet_id - kStandAnimation;
    if (index < 0 || index >= kOutletCount) return false;
    outlets_[index] = node;
    return true;
}

void CharAnimController::set_outlet_identifier(int outlet_id, int identifier) {
    // LoadFromProtobufMessage (0x356DBC), once per proto field: a *changed*
    // identifier drops the cached target, an unchanged one keeps it.
    const int index = outlet_id - kStandAnimation;
    if (index < 0 || index >= kOutletCount) return;
    if (identifiers_[index] == identifier) return;
    identifiers_[index] = identifier;
    outlets_[index] = nullptr;
}

int CharAnimController::outlet_identifier(int outlet_id) const {
    const int index = outlet_id - kStandAnimation;
    if (index < 0 || index >= kOutletCount) return 0;
    return identifiers_[index];
}

KeysNode* CharAnimController::outlet(int outlet_id) const {
    const int index = outlet_id - kStandAnimation;
    if (index < 0 || index >= kOutletCount) return nullptr;
    return outlets_[index];
}

// ── transitions ─────────────────────────────────────────────────────────────

void CharAnimController::start_moving() {
    // 0x3578D4. The guard is the *move blend*, not the walk outlet, and the walk
    // clip enters at a quarter of its own duration — a phase, not 0.25 seconds,
    // which is why it has to be `set_phase` and not an assignment of 0.25.
    if (!tree_ready_) return;
    if (KeysNode* w = walk()) w->set_phase(0.25f);
    move_.set_flags_word(anim::kBlendTowardRight);
}

void CharAnimController::stop_moving() {
    // 0x35799C. The idle clip restarts from its first frame, which is what makes
    // the character look like it settles rather than resumes.
    if (!tree_ready_) return;
    if (KeysNode* stand = outlet(kStandAnimation)) stand->time = 0.0f;
    move_.set_flags_word(anim::kBlendTowardLeft);
}

void CharAnimController::set_current_run_speed(float speed) {
    // 0x35782C: `speed_multiplier = speed / 100`. The node's own `speed` is a
    // separate factor (+48) and is not touched; AnimKeysNode::Update multiplies
    // the two, so this scales the run cycle with how fast the character moves.
    if (KeysNode* w = walk()) w->speed_multiplier = speed / 100.0f;
}

bool CharAnimController::is_moving() const {
    // 0x3578B4. Two things worth spelling out: this is a *flag* test and not a
    // velocity test, and it is `reverse == 0` — "the move blend is heading for
    // the run clip", not "the move blend is on the run clip".
    return tree_ready_ && !move_.reverse;
}

void CharAnimController::start_jumping(bool air_jump) {
    // 0x357A20. The air blend's left child is *swapped* between the two jump
    // clips, so a double jump is a different clip in the same slot rather than a
    // third node.
    if (!tree_ready_) return;

    KeysNode* jump = air_jump ? outlet(kAirJumpAnimation) : outlet(kJumpAnimation);
    if (jump) {
        jump->time = 0.0f;
        air_.from = NodeRef::keys_of(jump);
    }

    root_.set_flags_word(anim::kBlendTowardRight);   // toward the air blend
    // Alpha 0 plus a *backward* flag: the air blend retreats to its left child
    // immediately and clamps there with `blending` cleared, which is the
    // engine's way of saying "park on the jump clip" without a separate reset.
    air_.alpha = 0.0f;
    air_.set_flags_word(anim::kBlendTowardLeft);
}

bool CharAnimController::is_ready_to_jump() const {
    // 0x357D28: the jump clip is past half its own length.
    const KeysNode* jump = outlet(kJumpAnimation);
    return jump && jump->time > jump->duration * 0.5f;
}

bool CharAnimController::can_start_falling() const {
    // 0x357C08. Three ways to be allowed to fall, and the third is the one that
    // matters: the jump clip's own remaining time, which is what stops a jump
    // from being cut short. `time_to_finish` is 0 while looping.
    if (!tree_ready_) return true;           // the guest dereferences unconditionally
    if (root_.alpha < kBlendLowThreshold) return true;
    if (air_.alpha > kBlendHighThreshold) return true;
    if (!air_.from.is_keys()) return true;   // unreachable for a prepared tree
    return air_.from.keys->time_to_finish() < 0.1f;
}

void CharAnimController::start_falling() {
    // 0x357C84. The fall clip restarts *without* touching the air blend's alpha,
    // so the jump → fall crossfade continues from wherever it had got to.
    if (!tree_ready_) return;
    if (KeysNode* fall = outlet(kFallAnimation)) fall->time = 0.0f;
    root_.set_flags_word(anim::kBlendTowardRight);
    air_.set_flags_word(anim::kBlendTowardRight);
}

void CharAnimController::stand_on_ground() {
    // 0x357D14: one flag write, and it is the backward one, so landing is a fade
    // back to the move blend over rootBlend's own 0.1 s.
    if (tree_ready_) root_.set_flags_word(anim::kBlendTowardLeft);
}

// ── the action slot ────────────────────────────────────────────────────────

bool CharAnimController::action_nearly_finished() const {
    // 0x3574D8. The guest dereferences +352 unconditionally here; the port
    // guards, because "no action" answering false is the same answer a caller
    // would act on.
    return action_ && action_->time > action_->duration * kActionReturnThreshold;
}

void CharAnimController::blend_from_animation_node(KeysNode& node, float blend_time) {
    // 0x3583DC. The blend is parked: alpha 0 and `blending` off, so the action
    // clip is shown *unmixed* and no fade happens until the countdown expires.
    // The countdown is armed here and nowhere else.
    action_ = nullptr;                            // release +352

    action_blend_ = MixNode{};
    action_blend_.from = NodeRef::keys_of(&node);
    action_blend_.to = NodeRef::mix_of(&root_);
    action_blend_.blend_time = blend_time;
    action_blend_.blend_in_time = -1.0f;          // "use blend_time when reversing"
    action_blend_.alpha = 0.0f;
    action_blend_.reverse = false;
    action_blend_.blending = false;               // +45 = 0
    action_blend_present_ = true;

    float remaining = node.duration - node.time;
    float timer = remaining - blend_time;
    if (timer < kActionTimerFloor) timer = kActionTimerFloor;
    action_timer_ = timer;
}

void CharAnimController::blend_to_animation_node(NodeRef node, float blend_time) {
    // 0x358638 — the derived override, and not the base's version. `from` is the
    // previous *destination* (the existing action blend's right child, which is
    // the move tree once the first action has faded in), so a chain of
    // transitions each fades from where the character visually was.
    //
    // The override also never touches m_currentNode: the derived Update writes it
    // once per frame instead. That is the reason the override exists.
    NodeRef from = action_blend_present_ ? action_blend_.to : NodeRef::mix_of(&root_);
    if (from.null()) return;
    if (from.same(node)) return;                  // already heading there

    action_blend_ = MixNode{};
    action_blend_.from = from;
    action_blend_.to = node;
    action_blend_.blend_time = blend_time;
    action_blend_.blend_in_time = -1.0f;
    action_blend_.alpha = 0.0f;
    action_blend_.reverse = false;
    action_blend_.blending = true;                // +45 = 1 → the fade runs
    action_blend_present_ = true;
}

void CharAnimController::blend_to_move_animation(float blend_time) {
    // 0x3587B0: the same call as BlendToAnimationNode(rootBlend, t), made through
    // the vtable — so it goes to this class's override, not the base's.
    blend_to_animation_node(NodeRef::mix_of(&root_), blend_time);
}

void CharAnimController::swing_with_animation(KeysNode& node) {
    // 0x357D80: +360 and +352 both take the node, the gate is armed, the clip
    // restarts, and the 0.08 s fade-in is a BlendToAnimationNode.
    swing_node_ = &node;
    action_ = &node;
    action_gate_ = true;
    node.time = 0.0f;
    blend_to_animation_node(NodeRef::keys_of(&node), kActionBlendTime);
}

void CharAnimController::begin_casting() {
    // 0x357EF0. Casting clears the gate: the cast's lifetime is decided by the
    // skill component, which calls FinishCasting, so the 95% auto-return must
    // not fire first.
    KeysNode* cast = outlet(kCastAnimation);
    if (!cast) return;                            // the guest would not survive this
    cast->time = 0.0f;
    blend_to_animation_node(NodeRef::keys_of(cast), kActionBlendTime);
    action_ = cast;
    action_gate_ = false;
}

void CharAnimController::finish_casting(KeysNode* node) {
    // 0x358090. The null branch is the interesting one: with no clip supplied it
    // re-phases the *run* cycle to half and blends to the move tree, i.e. it ends
    // the cast by returning to locomotion, not by returning to idle.
    if (node) {
        action_ = node;
        action_gate_ = false;
        node->time = 0.0f;
        blend_to_animation_node(NodeRef::keys_of(node), kActionBlendTime);
        return;
    }
    if (tree_ready_) {
        if (KeysNode* w = walk()) w->set_phase(0.5f);
        blend_to_animation_node(NodeRef::mix_of(&root_), kActionBlendTime);
    }
}

void CharAnimController::stop_action() {
    // 0x358298: release both action slots and blend straight back to the move
    // tree. Used by Die and Hurt, which is why a death is a fade and not a cut.
    action_ = nullptr;
    swing_node_ = nullptr;
    blend_to_animation_node(NodeRef::mix_of(&root_), kActionBlendTime);
}

void CharAnimController::update(float dt) {
    // CharAnimControllerComponent::Update (0x357500). Three steps, then the base.

    // 1. An action that auto-returns and has reached 95% of its clip: re-phase
    //    the run cycle to half, fade to the move tree over 0.1 s, and release the
    //    action slots. The release compares the action against +360 to decide
    //    whether the swing slot also goes.
    if (action_gate_ && action_ &&
        action_->time > action_->duration * kActionReturnThreshold) {
        KeysNode* finished = action_;
        if (tree_ready_) {
            if (KeysNode* w = walk()) w->set_phase(0.5f);
            blend_to_animation_node(NodeRef::mix_of(&root_), kReturnBlendTime);
        }
        if (finished == swing_node_) swing_node_ = nullptr;
        action_ = nullptr;
    }

    // 2. The countdown. It only runs while an action blend exists and is not
    //    already blending, and expiring it is what starts the single alpha sweep
    //    that fades the action back out — `blending` alone, `reverse` untouched,
    //    so the sweep goes toward blend_time and therefore toward the move tree.
    if (action_timer_ > 0.001f && action_blend_present_) {
        if (!action_blend_.blending) {
            action_timer_ -= dt;
            if (action_timer_ <= 0.001f) action_blend_.blending = true;
        }
    }

    // 3. The output slot, rewritten every frame. This is the line that makes the
    //    derived override of BlendToAnimationNode necessary. With neither an
    //    action blend nor a tree, the slot is nulled rather than left stale.
    if (action_blend_present_) {
        base_.current_node = NodeRef::mix_of(&action_blend_);
    } else if (tree_ready_) {
        base_.current_node = NodeRef::mix_of(&root_);
    } else {
        base_.current_node = NodeRef{};
    }

    base_.update(dt);
}

NodeRef CharAnimController::display_node() const {
    const Leaf best = heaviest_leaf(base_.current_node);
    return best.node ? NodeRef::keys_of(best.node) : NodeRef{};
}

float CharAnimController::display_weight() const {
    return heaviest_leaf(base_.current_node).weight;
}

// ── the driver ─────────────────────────────────────────────────────────────

void set_initial_animation_state(CharAnimController& controller, const SpawnState& spawn) {
    // CharControllerComponent::SetInitialAnimationControllerState (0x35DC68).
    // The three tests are in this order and the last two are an `else if`, so a
    // character that spawns moving *and* airborne gets the jump clip and not the
    // run cycle.
    controller.reset();
    if (spawn.moving_direction != 0) controller.start_moving();
    if (spawn.air_state == 3) {
        controller.start_jumping(false);
    } else if (spawn.is_falling) {
        controller.start_falling();
    }
}

// ── ClipBinding — the port's rig ────────────────────────────────────────────

bool ClipBinding::configure(const std::array<std::string, kCount>& names, bool model_present) {
    names_ = names;
    for (int i = 0; i < kCount; ++i) {
        // A fresh node per outlet: no clip (see the class note), speed 1, its own
        // clock driven by advance().
        nodes_[i] = anim::KeysNode{};
        nodes_[i].speed = 1.0f;
        controller_.bind(CharAnimController::kStandAnimation + i, &nodes_[i]);
    }
    configured_ = true;
    controller_.base().model_present = model_present;
    const bool complete = controller_.prepare();
    controller_.reset();
    // `complete` false means an outlet is unbound — the guest's null outlet, which
    // builds the same tree and evaluates nothing for that leaf. Reported, not
    // refused, because the guest does not refuse it either.
    return complete;
}

int ClipBinding::index_of(const anim::KeysNode* node) const {
    if (!node) return -1;
    for (int i = 0; i < kCount; ++i)
        if (&nodes_[i] == node) return i;
    return -1;
}

int ClipBinding::index_of_name(const std::string& name) const {
    if (name.empty()) return -1;
    for (int i = 0; i < kCount; ++i)
        if (names_[i] == name) return i;
    return -1;
}

const std::string& ClipBinding::name_at(int index) const {
    static const std::string empty;
    if (index < 0 || index >= kCount) return empty;
    return names_[index];
}

anim::KeysNode* ClipBinding::node_at(int index) {
    if (index < 0 || index >= kCount) return nullptr;
    return &nodes_[index];
}

void ClipBinding::set_speed(int index, float speed) {
    if (anim::KeysNode* node = node_at(index)) node->speed = speed;
}

void ClipBinding::set_duration(int index, float seconds) {
    // The node stores `clip.duration - 0.001` (0x325364); with no clip there is
    // nothing to subtract from, so the stored duration is what is asked for.
    if (anim::KeysNode* node = node_at(index)) node->duration = seconds;
}

void ClipBinding::advance(float dt) {
    for (anim::KeysNode& node : nodes_) {
        const float step = (node.speed * dt) * node.speed_multiplier;   // +48, dt, +52
        node.time += node.reverse ? -step : step;
        if (node.duration > 0.0f) {
            // The node's own clamp, applied here because the node has no clip to
            // run its Update: wrap when looping, otherwise hold at the end.
            if (node.time > node.duration) node.time = node.loops ? 0.0f : node.duration;
            else if (node.time < 0.0f) node.time = node.loops ? node.duration : 0.0f;
        } else if (node.time < 0.0f) {
            // No duration means nothing to wrap onto, and a negative clock would
            // make `time_to_finish` lie in the permissive direction.
            node.time = 0.0f;
        }
    }
}

float ClipBinding::time_at(int index) const {
    if (index < 0 || index >= kCount) return 0.0f;
    return nodes_[index].time;
}

} // namespace canim
} // namespace caver
