#pragma once
// program_host.h — the real Lua 5.1 VM that runs the shipped Swordigo programs.
//
// The engine has no AI in C++: the authored behaviour lives in the .scl/.scene
// data as Lua programs (ProgramComponent.Program plus the handler fields OnKill,
// OnHurt, OnActivate, OnCollide, OnLoad, OnCast, OnTouch, OnPress, OnRelease,
// OnBreak, OnCollect, OnItemGet, OnCollisionEnd, OnReceiveDamage) and calls back
// into the component primitives implemented in behaviour.cpp.
//
// This host is that VM:
//   * one lua_State per preview, opened through the host Lua 5.1 runtime that
//     filerift already embeds (no second Lua, no external lua/luac process);
//   * `Program.Bytes` is loaded with luaL_loadbuffer — the exact call
//     Caver::Program::LoadIntoState makes — and falls back to compiling
//     `Program.String` only when the chunk is absent;
//   * every script runs as a coroutine whose suspension is a *stored wait*, not a
//     returned value: Program.Wait is a C function that writes the duration onto
//     the running state, and the scheduler revives it once the duration has been
//     counted down by the speed-scaled dt. `Program.SetKeepActive` decides whether
//     a program is started with its object; a script that returns instead of
//     looping is aborted, exactly as ProgramState::Resume (0x545624) does;
//     see program_state.h for the recovered state machine;
//   * one-shot handlers run only when fired, never at spawn;
//   * the API names and signatures are the ones the shipped corpus uses
//     (Program, EntityController, Entity, PhysicsObject, KeyframeAnimation,
//     AnimationController, CollisionShape, TransformController, Math, Vector3,
//     Scene, Game, SoundLibrary, Properties, ParticleEmitter, Spell, Skill).
//
// Object references cross the boundary as light userdata proxies: `self` inside a
// program, and the return value of Scene.Find / EntityController.Target. The
// proxy carries the object identifier, so a script can hold an old reference
// across a library hot-swap and still resolve it.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct lua_State;

namespace caver {

class RuntimeScene;
struct RuntimeObject;

class ProgramHost {
public:
    // One running (or stored) program. Public because the Lua bindings need it
    // (Program.SetKeepActive retargets the instance that is executing).
    struct Instance;

    ProgramHost();
    ~ProgramHost();

    ProgramHost(const ProgramHost&) = delete;
    ProgramHost& operator=(const ProgramHost&) = delete;

    // ── lifecycle ───────────────────────────────────────────────────────────
    bool init(std::string* error = nullptr);
    void shutdown();
    bool active() const { return L_ != nullptr; }
    lua_State* state() const { return L_; }

    // Start the keep-active programs of every object in the scene.
    void start(RuntimeScene& scene);
    // Start one object's keep-active programs (used by RuntimeScene::spawn).
    void start_object(RuntimeScene& scene, RuntimeObject& object);

    // Resume an object's runnable scripts. Called from the scene's program pass
    // (after every C++ component ticked): SCENE_OBJECT_UPDATE, then PROGRAM_UPDATE.
    void update_object(RuntimeScene& scene, RuntimeObject& object, float dt);

    // End-of-frame pass: deliver the events the C++ behaviours queued, then let
    // the handlers' spawns land. Never runs inline with an object iteration.
    void pump(RuntimeScene& scene);

    // Queue an event handler run on an object ("OnKill", "OnCollide", …).
    void fire(RuntimeScene& scene, RuntimeObject& object, const std::string& event,
              RuntimeObject* other);

    // Adopt the scene clock (the VM's Program.Wait timeline).
    void sync_clock(double clock) { clock_ = clock; }

    // ── observability ───────────────────────────────────────────────────────
    int running_scripts() const;
    const std::vector<std::string>& errors() const { return errors_; }
    void clear_errors() { errors_.clear(); }
    // API functions the scripts actually called, with counts — the honest answer
    // to "what does this scene need from the engine?".
    const std::vector<std::pair<std::string, uint64_t>>& api_usage() const { return api_usage_; }
    const std::vector<std::string>& sounds_requested() const { return sounds_; }
    double clock() const { return clock_; }

    // ── VM → host hooks (used by the Lua C bindings) ─────────────────────────
    void note_api(const char* name);
    void set_current(Instance* instance) { current_ = instance; }
    void set_keep_active(bool keep);
    // Program.Wait: Caver::ProgramState::Wait (0x5457AC) — store the duration on
    // the running instance. The resume itself is the scheduler's decision.
    void set_wait(double seconds);
    void execute_named(RuntimeObject* object, const std::string& name);
    void request_sound(const std::string& name);
    void begin_cast(RuntimeObject& caster);
    // Handler field names on an object (\"OnKill\" …) and which component owns them.
    std::vector<std::string> handler_names(const RuntimeObject& object) const;
    const char* handler_class_for(const RuntimeObject& object, const std::string& event) const;

private:
    void start_programs(RuntimeScene& scene, RuntimeObject& object);
    bool load_program(const std::string& field_name, const std::string& source,
                      const std::string& bytecode);
    void resume(RuntimeScene& scene, RuntimeObject& object, Instance& instance, float dt);
    void run_handler(RuntimeScene& scene, RuntimeObject& object, Instance& instance);
    void report(const RuntimeObject& object, const std::string& field, const std::string& message);

    lua_State* L_ = nullptr;
    std::vector<Instance*> instances_;
    std::vector<std::string> errors_;
    std::vector<std::pair<std::string, uint64_t>> api_usage_;
    std::vector<std::string> sounds_;
    double clock_ = 0.0;

    // The instance currently executing (Program.SetKeepActive target).
    Instance* current_ = nullptr;
    // Queued scripted events: object identifier, event field, other participant.
    std::vector<std::pair<std::pair<std::string, std::string>, std::string>> pending_;
};

// Implemented here because it needs the VM: the recovered behaviours call it when
// a scripted event happens (see behaviour.h).
void behaviour_fire_event(RuntimeScene& scene, RuntimeObject& object,
                          const std::string& event_field, RuntimeObject* other);

} // namespace caver
