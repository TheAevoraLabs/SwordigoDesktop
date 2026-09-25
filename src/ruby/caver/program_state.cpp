// program_state.cpp — the recovered ProgramState rules (see program_state.h for
// the field layout and the address of every claim).

#include "ruby/caver/program_state.h"

#include <algorithm>

namespace caver {
namespace prog {

// ── Value marshalling ───────────────────────────────────────────────────────
// IsIntAtStackIndex 0x5458A8 and IsFloatAtStackIndex 0x5458E0 both compile to
// `lua_isnumber(L, i) != 0`, so both accept any Lua *number* and neither accepts
// a bool, a numeric string, or a userdata. Written as two functions rather than
// one shared helper because the duplication is the finding: the engine really
// does have two identical predicates under two names.
bool is_int_at_stack_index(ValueKind kind) { return kind == ValueKind::Number; }
bool is_float_at_stack_index(ValueKind kind) { return kind == ValueKind::Number; }

// IsBoolAtStackIndex 0x54591C: `lua_type(L, i) == 1`.
bool is_bool_at_stack_index(ValueKind kind) { return kind == ValueKind::Boolean; }

// BoolAtStackIndex 0x54593C: type check *and* lua_toboolean.
bool bool_at_stack_index(ValueKind kind, bool truthy) {
    return kind == ValueKind::Boolean && truthy;
}

// Pop 0x54575C. `lua_settop` takes an absolute index, so popping n values means
// targeting `-(n+1)`; the engine instead computes `~n`, which is the same number
// for n >= 1 and is why Pop(0) is *not* a no-op — `~0` is -1, which pops
// nothing but also nothing else. Kept exactly as recovered.
int pop_settop_argument(int n) { return ~n; }

// ── Resuming ────────────────────────────────────────────────────────────────
// Resume 0x545624 and friends: `if (lua_resume(L) != 1) abort_requested = true;`
ResumeOutcome resume_outcome(int lua_resume_result) {
    return lua_resume_result == kLuaYield ? ResumeOutcome::Yielded : ResumeOutcome::Aborted;
}

// Execute 0x5456E0: `if (*(this + 8)) resume else lua_pcall`.
ExecutePath execute_path(bool has_parent_state) {
    return has_parent_state ? ExecutePath::ResumeCoroutine : ExecutePath::CallOnStack;
}

// ── Scheduler ───────────────────────────────────────────────────────────────

// Wait 0x5457AC:
//   *((float *)this + 21) = seconds;   // +84
//   *((_DWORD *)this + 20) = 1;        // +80 = Waiting
// Note there is no clamping: a negative or zero duration is stored verbatim, and
// the next Update ends the wait because `remaining - scaled < 0`.
void Scheduler::wait(Node& node, float seconds) {
    node.wait_remaining = seconds;
    node.status = Status::Waiting;
}

// Abort 0x5457BC:
//   *((_BYTE *)this + 91) = 1;         // +91
//   *((_DWORD *)this + 20) = 2;        // +80 = Aborted
// Aborting does not stop the state here; the sweep in Update removes it.
void Scheduler::abort(Node& node) {
    node.abort_requested = true;
    node.status = Status::Aborted;
}

// Update 0x545480, the two-step dt scale. The owner multiplier comes from the
// scene object (`SceneObject::updateSpeedMultiplier`), the second factor is the
// state's own +92.
float Scheduler::scaled_dt(float dt, float owner_speed_multiplier, float time_scale) {
    return (dt * owner_speed_multiplier) * time_scale;
}

// `if (*(this + 89) | *(this + 90))` — a bitwise or of two bytes, so either flag
// alone is enough.
bool Scheduler::state_updates(const Node& node) {
    return node.runs || node.runs_when_gated;
}

// `if (!*(this + 88) || *(child + 90))` from the child loop.
bool Scheduler::child_steps(const Node& parent, const Node& child) {
    return !parent.gates_children || child.runs_when_gated;
}

// `remaining -= scaled; if (remaining < 0.0f)`. Strictly less than zero, so a
// wait that lands exactly on zero runs one more frame.
bool Scheduler::tick_wait(float& remaining, float scaled) {
    remaining -= scaled;
    return remaining < 0.0f;
}

bool Scheduler::tick_wait(Node& node, float scaled) {
    return tick_wait(node.wait_remaining, scaled);
}

// The sweep: unlink every child that requested an abort. The engine frees the
// list node and drops the owner reference here; the caller owns the Node, so all
// this does is detach it, which is the part the scheduler is responsible for.
int Scheduler::sweep_aborted(Node& node) {
    const size_t before = node.children.size();
    node.children.erase(
        std::remove_if(node.children.begin(), node.children.end(),
                       [](Node* child) { return child && child->abort_requested; }),
        node.children.end());
    return static_cast<int>(before - node.children.size());
}

void Scheduler::update(Node& root, float dt, const ResumeFn& resume, SchedulerStats* stats) {
    // The gate is checked before anything else, including the owner lookup — a
    // gated state does not even pay for the speed multiplier.
    if (!state_updates(root)) {
        if (stats) ++stats->gated;
        return;
    }
    if (stats) ++stats->visited;

    const float scaled = scaled_dt(dt, root.owner_speed_multiplier, root.time_scale);

    // Only a state that is *waiting* is ever resumed from here. A running state
    // is resumed by whoever called Execute()/Resume() explicitly.
    if (root.status == Status::Waiting) {
        if (stats) ++stats->waits_ticked;
        if (tick_wait(root, scaled)) {
            root.status = Status::Running;
            const ResumeOutcome outcome = resume ? resume(root) : ResumeOutcome::Yielded;
            if (stats) ++stats->resumed;
            if (outcome == ResumeOutcome::Aborted) root.abort_requested = true;
        }
    }

    // Children, in list order, with the same scaled dt. The sweep is interleaved
    // with the walk exactly as the engine's linked-list loop is, so a child that
    // aborts itself this frame is already out of the list when its next sibling
    // is visited.
    for (size_t i = 0; i < root.children.size();) {
        Node* child = root.children[i];
        if (!child) {
            root.children.erase(root.children.begin() + static_cast<long>(i));
            continue;
        }
        if (!child_steps(root, *child)) {
            if (stats) ++stats->children_skipped;
            ++i;
            continue;
        }
        update(*child, scaled, resume, stats);
        if (child->abort_requested) {
            root.children.erase(root.children.begin() + static_cast<long>(i));
            if (stats) ++stats->aborted;
            continue;   // do not advance: the next child slid into this slot
        }
        ++i;
    }
}

} // namespace prog
} // namespace caver
