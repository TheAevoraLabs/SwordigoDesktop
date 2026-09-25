#pragma once
// program_state.h — Caver::ProgramState, recovered from libswordigo v1.4.13.
//
// `ProgramState` is the engine's *game state* object: one per Lua program, and
// the thing that decides when a script runs. It is not a script wrapper — it is
// a scheduler node. Everything here is transcribed from the decompiled
// functions in OpenSwordigo/arm64_13/functions/Caver/ProgramState/ and
// .../ProgramTable/, with the address of each claim in a comment.
//
// ── The recovery, in one paragraph ──────────────────────────────────────────
// A `ProgramState` is a Lua *thread* under a shared root state. The root (the
// one a Scene embeds) owns a real `lua_State`; every other one is created with
// `lua_newthread` on its parent's state and registered in the parent's registry
// under a lightuserdata key, which is how a Lua C callback finds its state back
// (`FromLuaState`). `Update(dt)` is the whole scheduler: it scales dt, counts
// down a wait, resumes the coroutine when the wait runs out, recurses into
// children with the *same* scaled dt, and unlinks any child that asked to abort.
// Resuming is deliberately unforgiving — anything other than "yielded" means
// abort — and the only sanctioned way to yield is `Program.Wait`, because a
// coroutine that yields by any other route leaves no wait pending and is never
// resumed again.
//
// ── Field layout (arm64; `sizeof == 96`, from `operator new(0x60u)`) ────────
//
//     +0   lua_State*            m_state            ctor 0x544C68
//     +8   ProgramState*         m_parent_state     ctor 0x544C68 (null ⇒ root)
//     +16  intrusive list head   m_children.next    ctor 0x544C68
//     +24  intrusive list head   m_children.prev    ctor 0x544C68
//     +32  size_t                m_child_count      Update 0x545480 (`v15 - 1`)
//     +40  SceneObject*          m_owner            SetParentObject 0x545210
//     +48  ProgramTable          m_globals   {ptr, tag}  ctor 0x544C68
//     +56  int                   m_globals.tag = -10002  (LUA_GLOBALSINDEX)
//     +64  ProgramTable          m_registry  {ptr, tag}  ctor 0x544C68
//     +72  int                   m_registry.tag = -10000 (LUA_REGISTRYINDEX)
//     +80  Status                m_status            Wait 0x5457AC / Abort 0x5457BC
//     +84  float                 m_wait_remaining    Wait 0x5457AC
//     +88  bool                  m_gates_children    ctor 0x544C68 (= 0)
//     +89  bool                  m_runs              ctor 0x544C68 (= 1)
//     +90  bool                  m_runs_when_gated   ctor 0x544C68 (= 1)
//     +91  bool                  m_abort_requested   Resume 0x545624
//     +92  float                 m_time_scale        ctor 0x544C68 (= 1.0f)
//
// The names in the middle column are ours; the engine's own names are gone with
// the symbols. The offsets and the values written at them are not.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace caver {
namespace prog {

// ── Recovered constants ─────────────────────────────────────────────────────

// ProgramState::m_status (+80). Only three values are ever written.
//   Wait 0x5457AC writes 1, Abort 0x5457BC writes 2, Resume 0x545624 and
//   Update 0x545480 write 0. Nothing else touches +80.
enum class Status : int32_t {
    Running = 0,
    Waiting = 1,
    Aborted = 2,
};

// Ctor 0x544C68 writes 1065353216 (0x3F800000) at +92.
inline constexpr float kDefaultTimeScale = 1.0f;

// Ctor 0x544C68 stores these in the two ProgramTable descriptors at +56 / +72.
// -10000 and -10002 are Lua 5.1's LUA_REGISTRYINDEX and LUA_GLOBALSINDEX, so a
// ProgramTable is not a container of its own: it is a {lua_State, pseudo-index}
// view onto a table Lua already has.
inline constexpr int kRegistryPseudoIndex = -10000;
inline constexpr int kGlobalsPseudoIndex = -10002;

// lua_resume returns LUA_YIELD (1) when the coroutine suspended. Every recovery
// site compares against exactly 1 (Resume 0x545624, Execute 0x54566C,
// ExecuteString 0x54566C, Update 0x545480).
inline constexpr int kLuaYield = 1;

// ProgramMath::PushVector3 0x545ECC allocates 12 bytes and names them
// "Vector3"; PushRectangle 0x545F98 allocates 16 and names them "Rectangle".
// The payload sizes are the struct sizes, so the script-visible Vector2-free
// Vector3 is 3 floats and Rectangle is 4.
inline constexpr int kVector3Bytes = 12;
inline constexpr int kRectangleBytes = 16;
inline constexpr const char* kVector3Metatable = "Vector3";
inline constexpr const char* kRectangleMetatable = "Rectangle";

// libc++'s small-string boundary, seen in StringAtStackIndex 0x5457D0: below
// 23 bytes the string lives inline (the first byte holds 2*len), at or above it
// the heap block is `(len | 15) + 1`. Recorded because it is the only place the
// engine's string handling depends on the STL implementation.
inline constexpr size_t kLibcppSmallStringCapacity = 23;

// ── Value marshalling ───────────────────────────────────────────────────────
//
// The recovered rules are *not* symmetric and not obvious, which is why they
// are functions here rather than a comment. `lua_type` codes are Lua 5.1's:
// 0 nil, 1 boolean, 2 lightuserdata, 3 number, 4 string, 5 table, 6 function,
// 7 userdata, 8 thread.
enum class ValueKind : int32_t {
    Nil = 0,
    Boolean = 1,
    LightUserData = 2,
    Number = 3,
    String = 4,
    Table = 5,
    Function = 6,
    UserData = 7,
    Thread = 8,
};

// IsIntAtStackIndex 0x5458A8: `lua_isnumber(L, i)`.
//
// It is *not* an integer test. "IsInt" and "IsFloat" are the same predicate,
// because both are Lua numbers — the engine's int/float distinction is a
// *pushing* concern (lua_pushinteger vs lua_pushnumber), never a reading one.
bool is_int_at_stack_index(ValueKind kind);
bool is_float_at_stack_index(ValueKind kind);   // 0x5458E0 — identical to the above, deliberately

// IsBoolAtStackIndex 0x54591C: `lua_type(L, i) == 1`. Bools are the one value
// class the engine type-checks exactly rather than coercing.
bool is_bool_at_stack_index(ValueKind kind);

// BoolAtStackIndex 0x54593C: `lua_type(L, i) == LUA_TBOOLEAN && lua_toboolean(L, i)`.
// Note the `&&`: a truthy number is *not* accepted where a bool is wanted.
bool bool_at_stack_index(ValueKind kind, bool truthy);

// IntAtStackIndex 0x5458C8 → lua_tointeger; FloatAtStackIndex 0x545900 →
// lua_tonumber. Both coerce from any Lua number; neither is guarded, so a
// caller that wants a number must ask is_int/is_float first.
// Pop 0x54575C: `lua_settop(L, ~n)` — the pop count arrives pre-negated.
// Pop(1) is settop(-2), Pop(2) is settop(-3).
int pop_settop_argument(int n);

// ── Resuming ────────────────────────────────────────────────────────────────

// Resume 0x545624, Update 0x545480, ExecuteString 0x54566C:
//   status = Running; r = lua_resume(L); if (r != LUA_YIELD) abort_requested = true;
// So a coroutine that *returns* is aborted exactly like one that errors. A
// keep-alive script therefore has to loop (`while true do … Program.Wait(t) … end`);
// one that falls off the end is removed.
enum class ResumeOutcome { Yielded, Aborted };
ResumeOutcome resume_outcome(int lua_resume_result);

// Execute 0x5456E0 picks its path on whether the state has a parent (+8): a
// child state is a coroutine, so it is resumed; a root state is an ordinary
// lua_State, so the function on its stack is called with lua_pcall. On error
// the root path leaves one value (`lua_settop(L, 0xFFFFFFFE)` = -2).
enum class ExecutePath { ResumeCoroutine, CallOnStack };
ExecutePath execute_path(bool has_parent_state);

// ── The scheduler ───────────────────────────────────────────────────────────
//
// One node is one ProgramState. `caver::prog::Scheduler::update()` is
// ProgramState::Update 0x545480 transcribed including its child recursion, so
// the ordering rules below are executable and testable without a Lua VM:
//
//   1. Gate. The state does nothing unless `runs || runs_when_gated` (+89|+90).
//      Both default to true, so a fresh state ticks; the only write to +89 in
//      the whole binary is ProgramComponent::HandleMessage (0x33D778+205), which
//      is `Program.SetKeepActive(false)`.
//   2. dt scaling. The owner object's speed multiplier is applied first
//      (`SceneObject::updateSpeedMultiplier`), then the state's own time scale
//      (+92). Children inherit the *scaled* value, not the frame dt, so a
//      slowed owner slows every script it owns by the same factor.
//   3. Wait. Only when status == Waiting: `remaining -= scaled; if (remaining < 0)`
//      resume. A state that yielded without setting a wait is never resumed by
//      Update — that is why every yield in the shipped corpus is a Program.Wait.
//   4. Children, in list order, with the same scaled dt, skipped when the parent
//      has gates_children set and the child is not runs_when_gated.
//   5. Abort sweep. A child with abort_requested set is unlinked from the list
//      and its owner reference dropped. The sweep happens *inside* the same
//      iteration that recursed into it, so a child aborted this frame is gone
//      before the next child is visited.

struct SchedulerStats {
    int visited = 0;      // states whose body ran this frame
    int gated = 0;        // states skipped by the +89|+90 gate
    int resumed = 0;      // coroutines actually resumed
    int waits_ticked = 0; // countdowns advanced
    int children_skipped = 0;
    int aborted = 0;      // states unlinked by the sweep
};

class Scheduler {
public:
    // A live state. `user` is the caller's handle (the Lua thread, a host
    // instance, an index — the scheduler never looks at it).
    struct Node {
        Status status = Status::Running;
        float  wait_remaining = 0.0f;
        float  time_scale = kDefaultTimeScale;
        float  owner_speed_multiplier = 1.0f;
        bool   runs = true;                 // +89
        bool   runs_when_gated = true;      // +90
        bool   gates_children = false;      // +88
        bool   abort_requested = false;     // +91
        void*  user = nullptr;
        std::vector<Node*> children;
    };

    // Called for a state whose wait expired. The return value is the recovered
    // verdict on that resume; a caller with no VM returns Yielded.
    using ResumeFn = std::function<ResumeOutcome(Node&)>;

    // ProgramState::Wait 0x5457AC.
    static void wait(Node& node, float seconds);
    // ProgramState::Abort 0x5457BC.
    static void abort(Node& node);
    // ProgramState::Update 0x545480.
    static void update(Node& root, float dt, const ResumeFn& resume, SchedulerStats* stats = nullptr);

    // The dt a state's own body and its children run with: the two recovered
    // multiplications, in the recovered order (0x545480).
    static float scaled_dt(float dt, float owner_speed_multiplier, float time_scale);
    // The gate at the top of Update (0x545480).
    static bool state_updates(const Node& node);
    // `if (!parent+88 || child+90)` from the child loop (0x545480).
    static bool child_steps(const Node& parent, const Node& child);
    // `remaining -= scaled; return remaining < 0.0f` (0x545480). The float
    // overload is the one a host with its own per-instance storage wants; the
    // Node overload is this file's scheduler.
    static bool tick_wait(float& remaining, float scaled);
    static bool tick_wait(Node& node, float scaled);
    // The abort sweep: unlink the children that asked for it, drop their owner
    // reference, and return how many went.
    static int sweep_aborted(Node& node);
};

} // namespace prog
} // namespace caver
