#include "ruby/caver/program_host.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

// This tree's lua.h lacks the usual extern "C" guard, so wrap it manually (same
// approach as src/tools/scene_lua.cpp). The runtime linked here is the host Lua
// 5.1 that filerift already compiles — no second Lua copy, no luac subprocess.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "ruby/caver/behaviour.h"
#include "ruby/caver/library_manager.h"
#include "ruby/caver/program_state.h"
#include "ruby/caver/runtime.h"

namespace caver {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr const char* kMtObject = "caver.object";
constexpr const char* kMtVec3   = "caver.vec3";

ProgramHost* g_host = nullptr;

// lua_pushcfunction is a macro in Lua 5.1 (lua_pushcclosure(L, f, 0)), and a
// macro argument is split on any comma outside parentheses — a lambda body has
// plenty. Go through a real function so captureless lambdas convert to
// lua_CFunction without the preprocessor seeing their insides.
void push_fn(lua_State* L, lua_CFunction fn) { lua_pushcclosure(L, fn, 0); }

struct Vec3 { float x = 0, y = 0, z = 0; };

// An object reference crossing the Lua boundary. Deliberately just the
// identifier: a script may hold it across an .scl hot-swap and still resolve.
struct ObjectRef {
    char identifier[128];
};

bool is_vec3(lua_State* L, int idx) {
    if (!lua_isuserdata(L, idx)) return false;
    if (!lua_getmetatable(L, idx)) return false;
    luaL_getmetatable(L, kMtVec3);
    const bool eq = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return eq;
}

Vec3* to_vec3(lua_State* L, int idx) { return static_cast<Vec3*>(lua_touserdata(L, idx)); }

void push_vec3(lua_State* L, float x, float y, float z) {
    Vec3* v = static_cast<Vec3*>(lua_newuserdata(L, sizeof(Vec3)));
    v->x = x; v->y = y; v->z = z;
    luaL_getmetatable(L, kMtVec3);
    lua_setmetatable(L, -2);
}

// The scene/self currently executing. One VM per preview, so a thread-local
// current frame is the same simplification the shipped host makes.
RuntimeScene* g_scene = nullptr;
RuntimeObject* g_self = nullptr;

RuntimeObject* resolve(lua_State* L, int idx) {
    if (!g_scene) return nullptr;
    if (lua_isuserdata(L, idx)) {
        ObjectRef* ref = static_cast<ObjectRef*>(lua_touserdata(L, idx));
        if (ref && ref->identifier[0]) return g_scene->object(ref->identifier);
        return nullptr;
    }
    if (lua_isstring(L, idx)) {
        const char* name = lua_tostring(L, idx);
        if (!name) return nullptr;
        if (std::strcmp(name, "self") == 0) return g_self;
        return g_scene->object(name);
    }
    return nullptr;
}

void push_object(lua_State* L, const RuntimeObject* object) {
    if (!object) { lua_pushnil(L); return; }
    ObjectRef* ref = static_cast<ObjectRef*>(lua_newuserdata(L, sizeof(ObjectRef)));
    std::snprintf(ref->identifier, sizeof(ref->identifier), "%s", object->identifier.c_str());
    luaL_getmetatable(L, kMtObject);
    lua_setmetatable(L, -2);
}

BehaviourState* behaviour_of(lua_State* L, int idx) {
    RuntimeObject* object = resolve(L, idx);
    return object ? &object->behaviour : nullptr;
}

float sign_of(lua_State* L, int idx) {
    if (lua_isnumber(L, idx)) return lua_tonumber(L, idx) < 0.0f ? -1.0f : 1.0f;
    if (is_vec3(L, idx)) { Vec3* v = to_vec3(L, idx); return (v && v->x < 0.0f) ? -1.0f : 1.0f; }
    return g_self ? static_cast<float>(g_self->behaviour.facing) : 1.0f;
}

void set_action(BehaviourState* b, EntityAction a, float duration) {
    if (!b) return;
    b->action = a;
    b->action_time = 0.0f;
    b->action_duration = duration;
}

// Parses the action name EntityController.PerformAction takes. The shipped
// corpus uses both numeric ids and these names.
EntityAction action_from(lua_State* L, int idx) {
    if (lua_isnumber(L, idx)) {
        switch (static_cast<int>(lua_tonumber(L, idx))) {
            case 1: return EntityAction::Walk;
            case 2: return EntityAction::Attack;
            case 3: return EntityAction::Jump;
            case 4: return EntityAction::Cast;
            case 5: return EntityAction::Hurt;
            case 6: return EntityAction::Death;
            default: return EntityAction::Idle;
        }
    }
    const char* name = lua_tostring(L, idx);
    if (!name) return EntityAction::Idle;
    const std::string s(name);
    if (s == "walk"    || s == "Walk")     return EntityAction::Walk;
    if (s == "attack"  || s == "Attack"    || s == "swing") return EntityAction::Attack;
    if (s == "jump"    || s == "Jump")     return EntityAction::Jump;
    if (s == "fall"    || s == "Fall")     return EntityAction::Fall;
    if (s == "land"    || s == "Land")     return EntityAction::Land;
    if (s == "cast"    || s == "Cast")     return EntityAction::Cast;
    if (s == "hurt"    || s == "Hurt")     return EntityAction::Hurt;
    if (s == "death"   || s == "Death"     || s == "die") return EntityAction::Death;
    if (s == "roam"    || s == "Roam")     return EntityAction::Roam;
    if (s == "turn"    || s == "Turn")     return EntityAction::Turn;
    return EntityAction::Idle;
}

int api(lua_State* L, const char* name) { (void)L; if (g_host) g_host->note_api(name); return 0; }

} // namespace

// ── host internals ──────────────────────────────────────────────────────────

struct ProgramHost::Instance {
    std::string object;           // owning object identifier
    std::string field;            // "Program", "OnKill", "OnCollide" …
    std::string owner_class;      // component class that owns the field
    int  thread = LUA_NOREF;      // coroutine ref in the registry
    bool keep_active = false;     // started with its object (Program.SetKeepActive)
    bool one_shot = false;        // handler: runs only when fired
    bool started = false;
    bool finished = false;
    // Recovered ProgramState state (program_state.h): +84 m_wait_remaining and
    // +80 m_status. Program.Wait is a C function that *stores* the duration and
    // yields no values; ProgramState::Update counts it down by the speed-scaled
    // dt. The port used to read the duration back off the yielded stack, which
    // ProgramState::Wait (0x5457AC) makes impossible.
    float wait_remaining = 0.0f;
    bool  waiting = false;
    uint64_t runs = 0;
};

void ProgramHost::note_api(const char* name) {
    for (auto& entry : api_usage_) {
        if (entry.first == name) { ++entry.second; return; }
    }
    api_usage_.emplace_back(name, 1);
}

ProgramHost::ProgramHost() = default;

ProgramHost::~ProgramHost() { shutdown(); }

bool ProgramHost::init(std::string* error) {
    if (L_) return true;
    L_ = luaL_newstate();
    if (!L_) {
        if (error) *error = "luaL_newstate() failed";
        return false;
    }
    luaL_openlibs(L_);
    g_host = this;

    // ── Vec3 ────────────────────────────────────────────────────────────────
    luaL_newmetatable(L_, kMtVec3);
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* v = to_vec3(L, 1);
        lua_pushnumber(L, v ? v->x : 0.0); return 1;
    }); lua_setfield(L_, -2, "x");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* v = to_vec3(L, 1);
        lua_pushnumber(L, v ? v->y : 0.0); return 1;
    }); lua_setfield(L_, -2, "y");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* v = to_vec3(L, 1);
        lua_pushnumber(L, v ? v->z : 0.0); return 1;
    }); lua_setfield(L_, -2, "z");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* v = to_vec3(L, 1);
        lua_pushnumber(L, v ? std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z) : 0.0); return 1;
    }); lua_setfield(L_, -2, "length");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* a = to_vec3(L, 1); Vec3* b = to_vec3(L, 2);
        if (!a || !b) { push_vec3(L, 0, 0, 0); return 1; }
        push_vec3(L, a->x + b->x, a->y + b->y, a->z + b->z); return 1;
    }); lua_setfield(L_, -2, "__add");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* a = to_vec3(L, 1); Vec3* b = to_vec3(L, 2);
        if (!a || !b) { push_vec3(L, 0, 0, 0); return 1; }
        push_vec3(L, a->x - b->x, a->y - b->y, a->z - b->z); return 1;
    }); lua_setfield(L_, -2, "__sub");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* a = to_vec3(L, 1);
        const float k = static_cast<float>(luaL_checknumber(L, 2));
        if (!a) { push_vec3(L, 0, 0, 0); return 1; }
        push_vec3(L, a->x * k, a->y * k, a->z * k); return 1;
    }); lua_setfield(L_, -2, "__mul");
    push_fn(L_, [](lua_State* L) -> int {
        Vec3* a = to_vec3(L, 1); Vec3* b = to_vec3(L, 2);
        lua_pushboolean(L, a && b && a->x == b->x && a->y == b->y && a->z == b->z); return 1;
    }); lua_setfield(L_, -2, "__eq");

    // ── object proxy ────────────────────────────────────────────────────────
    luaL_newmetatable(L_, kMtObject);
    lua_pushvalue(L_, -1); lua_setfield(L_, -2, "__index");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o) push_vec3(L, o->pos[0], o->pos[1], o->pos[2]);
        else push_vec3(L, 0, 0, 0);
        return 1;
    }); lua_setfield(L_, -2, "position");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o && is_vec3(L, 2)) { Vec3* v = to_vec3(L, 2); o->pos[0] = v->x; o->pos[1] = v->y; o->pos[2] = v->z; }
        else if (o && lua_isnumber(L, 2)) { o->pos[0] = static_cast<float>(lua_tonumber(L, 2)); }
        return api(L, "self:setPosition");
    }); lua_setfield(L_, -2, "setPosition");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o && is_vec3(L, 2)) { Vec3* v = to_vec3(L, 2); o->vel[0] = v->x; o->vel[1] = v->y; o->vel[2] = v->z; }
        return api(L, "self:setVelocity");
    }); lua_setfield(L_, -2, "setVelocity");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        lua_pushstring(L, o ? o->identifier.c_str() : ""); return 1;
    }); lua_setfield(L_, -2, "identifier");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o) { o->rot[1] = static_cast<float>(luaL_checknumber(L, 2)); }
        return api(L, "self:setRotation");
    }); lua_setfield(L_, -2, "setRotation");
    push_fn(L_, [](lua_State* L) -> int { push_vec3(L, 0, 0, 0); return 1; });
    lua_setfield(L_, -2, "rotation");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o) o->activated = lua_toboolean(L, 2) != 0;
        return api(L, "self:setActive");
    }); lua_setfield(L_, -2, "setActive");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o) {
            o->hidden = lua_toboolean(L, 2) != 0 ? false : o->hidden;
            o->activated = true;
        }
        return api(L, "self:setAlwaysActive");
    }); lua_setfield(L_, -2, "setAlwaysActive");

    // ── Program ─────────────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        const double t = lua_isnumber(L, 1) ? lua_tonumber(L, 1) : 0.0;
        // Caver::ProgramState::Wait (0x5457AC) stores the duration and sets
        // status Waiting; the yield carries nothing. Resuming is the scheduler's
        // job (ProgramState::Update 0x545480), not the returned value's.
        if (g_host) g_host->set_wait(t);
        return lua_yield(L, 0);
    }); lua_setfield(L_, -2, "Wait");
    push_fn(L_, [](lua_State* L) -> int {
        const int n = lua_gettop(L);
        std::string line = "[lua]";
        for (int i = 1; i <= n; ++i) {
            if (const char* s = lua_tostring(L, i)) line += std::string(" ") + s;
        }
        std::fprintf(stderr, "%s\n", line.c_str());
        return 0;
    }); lua_setfield(L_, -2, "Print");
    push_fn(L_, [](lua_State* L) -> int {
        // Program.SetKeepActive(true): the program stays loaded; a script that
        // returns is restarted on the next frame instead of being dropped.
        if (g_host) g_host->set_keep_active(lua_toboolean(L, 2) != 0);
        return api(L, "Program.SetKeepActive");
    }); lua_setfield(L_, -2, "SetKeepActive");
    push_fn(L_, [](lua_State* L) -> int {
        // Program.Execute(name, ...): run a named handler once, immediately.
        RuntimeObject* self = resolve(L, 1);
        const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
        if (self && name && g_host) g_host->execute_named(self, name);
        return api(L, "Program.Execute");
    }); lua_setfield(L_, -2, "Execute");
    lua_setglobal(L_, "Program");

    // ── EntityController ────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* self = resolve(L, 1);
        RuntimeObject* target = nullptr;
        if (g_scene && self) {
            if (self->behaviour.has_target) target = g_scene->object(self->behaviour.target_object);
            if (!target) target = g_scene->hero();
            if (target == self) target = nullptr;
        }
        push_object(L, target);
        return api(L, "EntityController.Target"), 1;
    }); lua_setfield(L_, -2, "Target");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        const bool idle = !b || b->action == EntityAction::Idle || b->action == EntityAction::Walk;
        lua_pushboolean(L, idle);
        return api(L, "EntityController.IsIdle"), 1;
    }); lua_setfield(L_, -2, "IsIdle");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        const EntityAction a = action_from(L, 2);
        set_action(b, a, a == EntityAction::Attack ? 1.1f : 0.6f);
        if (b) b->idle = (a == EntityAction::Idle);
        return api(L, "EntityController.PerformAction");
    }); lua_setfield(L_, -2, "PerformAction");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->facing = sign_of(L, 2) < 0.0f ? -1 : 1;
        return api(L, "EntityController.SetFacingDirection");
    }); lua_setfield(L_, -2, "SetFacingDirection");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) {
            if (is_vec3(L, 2)) { Vec3* v = to_vec3(L, 2); b->move_dir[0] = v->x; b->move_dir[1] = v->y; }
            else { b->move_dir[0] = static_cast<float>(luaL_checknumber(L, 2));
                   b->move_dir[1] = static_cast<float>(luaL_optnumber(L, 3, 0.0)); }
            if (std::fabs(b->move_dir[0]) > 0.001f) b->facing = b->move_dir[0] < 0.0f ? -1 : 1;
        }
        return api(L, "EntityController.SetMoveDirection");
    }); lua_setfield(L_, -2, "SetMoveDirection");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        const float s = static_cast<float>(luaL_checknumber(L, 2));
        if (b) { b->move_speed = s; if (b->move_speed <= 0.0f) b->idle = true; }
        return api(L, "EntityController.SetMoveSpeed");
    }); lua_setfield(L_, -2, "SetMoveSpeed");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) {
            if (lua_isnumber(L, 2)) b->movement = static_cast<MovementBehavior>(static_cast<int>(lua_tonumber(L, 2)));
            else if (const char* s = lua_tostring(L, 2)) {
                const std::string v(s);
                b->movement = v == "walk" ? MovementBehavior::Walk
                            : v == "roam" ? MovementBehavior::Roam
                            : v == "follow" ? MovementBehavior::Follow
                            : v == "flee" ? MovementBehavior::Flee
                                          : MovementBehavior::Stand;
            }
        }
        return api(L, "EntityController.SetMovementBehavior");
    }); lua_setfield(L_, -2, "SetMovementBehavior");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->accel = static_cast<float>(luaL_checknumber(L, 2));
        return api(L, "EntityController.SetAcceleration");
    }); lua_setfield(L_, -2, "SetAcceleration");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) {
            if (const char* name = lua_tostring(L, 2)) b->walk_animation = name;
            else if (lua_isnumber(L, 2)) {
                RuntimeObject* o = resolve(L, 1);
                const RuntimeComponent* c = o ? o->find(static_cast<int32_t>(lua_tonumber(L, 2))) : nullptr;
                if (c && c->field("Name")) b->walk_animation = c->field("Name")->bytes_value;
            }
        }
        return api(L, "EntityController.SetMoveAnimation");
    }); lua_setfield(L_, -2, "SetMoveAnimation");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        lua_pushnumber(L, b && b->default_move_speed > 0.0f ? b->default_move_speed : 60.0);
        return api(L, "EntityController.DefaultMoveSpeed"), 1;
    }); lua_setfield(L_, -2, "DefaultMoveSpeed");
    push_fn(L_, [](lua_State* L) -> int {
        lua_pushboolean(L, 0);
        return api(L, "EntityController.IsActionCancelled"), 1;
    }); lua_setfield(L_, -2, "IsActionCancelled");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        set_action(b, EntityAction::Attack, 0.9f);
        return api(L, "EntityController.StartSwing");
    }); lua_setfield(L_, -2, "StartSwing");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        lua_pushnumber(L, b ? b->accel : 0.0);
        return api(L, "EntityController.Acceleration"), 1;
    }); lua_setfield(L_, -2, "Acceleration");
    push_fn(L_, [](lua_State* L) -> int {
        // EntityController.TargetingDistance / DefaultMoveSpeed setters used by
        // scripts that widen their aggro range at runtime.
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->targeting_distance = static_cast<float>(luaL_checknumber(L, 2));
        return api(L, "EntityController.SetTargetingDistance");
    }); lua_setfield(L_, -2, "SetTargetingDistance");
    lua_setglobal(L_, "EntityController");

    // ── Entity ──────────────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->physics.entity_physics = lua_toboolean(L, 2) != 0;
        return api(L, "Entity.SetPhysicsEnabled");
    }); lua_setfield(L_, -2, "SetPhysicsEnabled");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        lua_pushboolean(L, o && o->behaviour.physics.on_ground);
        return api(L, "Entity.IsOnGround"), 1;
    }); lua_setfield(L_, -2, "IsOnGround");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        lua_pushnumber(L, b ? b->facing : 1.0);
        return api(L, "Entity.GetFacingDirection"), 1;
    }); lua_setfield(L_, -2, "GetFacingDirection");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->facing = sign_of(L, 2) < 0.0f ? -1 : 1;
        return api(L, "Entity.SetFacingDirection");
    }); lua_setfield(L_, -2, "SetFacingDirection");
    lua_setglobal(L_, "Entity");

    // ── PhysicsObject ───────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b && is_vec3(L, 2)) {
            Vec3* v = to_vec3(L, 2);
            const float len = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
            if (len > 1e-5f) {
                const float mag = b->physics.gravity_magnitude;
                b->physics.gravity_dir[0] = v->x / len * (mag > 0 ? 1.0f : 1.0f);
                b->physics.gravity_dir[1] = v->y / len;
            }
        }
        return api(L, "PhysicsObject.SetGravityDirection");
    }); lua_setfield(L_, -2, "SetGravityDirection");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) {
            const float m = static_cast<float>(luaL_checknumber(L, 2));
            b->physics.gravity_magnitude = std::fabs(m);
            if (b->physics.gravity_dir[0] == 0.0f && b->physics.gravity_dir[1] == 0.0f)
                b->physics.gravity_dir[1] = -1.0f;
        }
        return api(L, "PhysicsObject.SetGravityMagnitude");
    }); lua_setfield(L_, -2, "SetGravityMagnitude");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->physics.air_deceleration = static_cast<float>(luaL_checknumber(L, 2));
        return api(L, "PhysicsObject.SetDecelerationForce");
    }); lua_setfield(L_, -2, "SetDecelerationForce");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->physics.max_speed = static_cast<float>(luaL_checknumber(L, 2));
        return api(L, "PhysicsObject.SetMaxSpeed");
    }); lua_setfield(L_, -2, "SetMaxSpeed");
    lua_setglobal(L_, "PhysicsObject");

    // ── KeyframeAnimation / AnimationController ─────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        const int value_index = lua_gettop(L) >= 3 ? 3 : 2;   // (self, id, time) or (self, time)
        if (b) { b->anim.time = static_cast<float>(luaL_checknumber(L, value_index)); }
        return api(L, "KeyframeAnimation.SetCurrentTime");
    }); lua_setfield(L_, -2, "SetCurrentTime");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->anim.running = lua_toboolean(L, 2) != 0;
        return api(L, "KeyframeAnimation.SetRunning");
    }); lua_setfield(L_, -2, "SetRunning");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        lua_pushnumber(L, b ? static_cast<double>(b->anim.duration - b->anim.time) : 0.0);
        return api(L, "KeyframeAnimation.TimeToCompletion"), 1;
    }); lua_setfield(L_, -2, "TimeToCompletion");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        lua_pushnumber(L, b ? b->anim.time : 0.0);
        return api(L, "KeyframeAnimation.TimeToFrame"), 1;
    }); lua_setfield(L_, -2, "TimeToFrame");
    lua_setglobal(L_, "KeyframeAnimation");

    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) {
            b->anim.previous = b->anim.current;
            if (lua_isstring(L, 2)) b->anim.current = lua_tostring(L, 2);
            b->anim.blend_time = static_cast<float>(luaL_optnumber(L, 3, 0.2));
            b->anim.blend = 0.0f;
        }
        return api(L, "AnimationController.BlendToAnimation");
    }); lua_setfield(L_, -2, "BlendToAnimation");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b && lua_isstring(L, 2)) b->anim.current = lua_tostring(L, 2);
        b->anim.running = true;
        return api(L, "AnimationController.PlayAnimation");
    }); lua_setfield(L_, -2, "PlayAnimation");
    lua_setglobal(L_, "AnimationController");

    // ── CollisionShape ──────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        const bool on = lua_toboolean(L, 2) != 0;
        if (b) {
            b->health.shape_enabled = on;
            b->health.collides = on;
            b->health.reenable_timer = on ? 0.0f : 0.0f;
        }
        return api(L, "CollisionShape.SetEnabled");
    }); lua_setfield(L_, -2, "SetEnabled");
    lua_setglobal(L_, "CollisionShape");

    // ── TransformController ─────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o && is_vec3(L, 2)) { Vec3* v = to_vec3(L, 2); o->pos[0] += v->x; o->pos[1] += v->y; o->pos[2] += v->z; }
        return api(L, "TransformController.TranslateBy");
    }); lua_setfield(L_, -2, "TranslateBy");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o) { const float k = static_cast<float>(luaL_checknumber(L, 2)); o->scale[0] = k; o->scale[1] = k; o->scale[2] = k; }
        return api(L, "TransformController.ScaleTo");
    }); lua_setfield(L_, -2, "ScaleTo");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        if (o && is_vec3(L, 2)) { Vec3* v = to_vec3(L, 2); o->pos[0] = v->x; o->pos[1] = v->y; o->pos[2] = v->z; }
        return api(L, "TransformController.SetPosition");
    }); lua_setfield(L_, -2, "SetPosition");
    lua_setglobal(L_, "TransformController");

    // ── Math ────────────────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        const double a = luaL_optnumber(L, 1, 0.0), b = luaL_optnumber(L, 2, 1.0);
        lua_pushnumber(L, a + (b - a) * (static_cast<double>(std::rand()) / RAND_MAX));
        return api(L, "Math.RandomFloat"), 1;
    }); lua_setfield(L_, -2, "RandomFloat");
    push_fn(L_, [](lua_State* L) -> int {
        const int a = static_cast<int>(luaL_optnumber(L, 1, 0)), b = static_cast<int>(luaL_optnumber(L, 2, 1));
        lua_pushnumber(L, b > a ? a + std::rand() % (b - a + 1) : a);
        return api(L, "Math.RandomInt"), 1;
    }); lua_setfield(L_, -2, "RandomInt");
    push_fn(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, std::fabs(luaL_checknumber(L, 1)));
        return api(L, "Math.Abs"), 1;
    }); lua_setfield(L_, -2, "Abs");
    push_fn(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, std::min(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
        return api(L, "Math.Min"), 1;
    }); lua_setfield(L_, -2, "Min");
    push_fn(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, std::max(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
        return api(L, "Math.Max"), 1;
    }); lua_setfield(L_, -2, "Max");
    push_fn(L_, [](lua_State* L) -> int {
        lua_pushnumber(L, std::sqrt(luaL_checknumber(L, 1)));
        return api(L, "Math.Sqrt"), 1;
    }); lua_setfield(L_, -2, "Sqrt");
    lua_pushnumber(L_, kPi); lua_setfield(L_, -2, "Pi");
    lua_setglobal(L_, "Math");

    // ── Vector3 ─────────────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        push_vec3(L, static_cast<float>(luaL_optnumber(L, 1, 0.0)),
                     static_cast<float>(luaL_optnumber(L, 2, 0.0)),
                     static_cast<float>(luaL_optnumber(L, 3, 0.0)));
        return api(L, "Vector3.New"), 1;
    }); lua_setfield(L_, -2, "New");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* from = resolve(L, 1);
        RuntimeObject* to = resolve(L, 2);
        if (from && to) {
            const float dx = to->pos[0] - from->pos[0];
            const float dy = to->pos[1] - from->pos[1];
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-5f) { push_vec3(L, dx / len, dy / len, 0.0f); return api(L, "DirectionToTargetFromPosition"), 1; }
        }
        push_vec3(L, 0, 0, 0);
        return api(L, "DirectionToTargetFromPosition"), 1;
    }); lua_setfield(L_, -2, "DirectionToTargetFromPosition");
    lua_setglobal(L_, "Vector3");

    // ── Scene ───────────────────────────────────────────────────────────────
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        // Scene.CreateObject(templateName, identifier, parentOrSelf) — resolves
        // through the ObjectLibrary registry, exactly like SceneObject::
        // InitWithTemplate.
        const char* tmpl = luaL_checkstring(L, 1);
        const char* ident = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
        RuntimeObject* parent = resolve(L, 3);
        // SceneObject::InitWithTemplate through the ObjectLibrary registry. The
        // object exists immediately so the script can use the return value.
        RuntimeObject* created = g_scene
            ? g_scene->spawn(tmpl, ident ? ident : tmpl, parent ? parent->pos : nullptr)
            : nullptr;
        push_object(L, created);
        return api(L, "Scene.CreateObject"), 1;
    }); lua_setfield(L_, -2, "CreateObject");
    push_fn(L_, [](lua_State* L) -> int {
        push_object(L, resolve(L, 1));
        return api(L, "Scene.Find"), 1;
    }); lua_setfield(L_, -2, "Find");
    push_fn(L_, [](lua_State* L) -> int {
        push_object(L, g_scene ? g_scene->hero() : nullptr);
        return api(L, "Scene.GetHero"), 1;
    }); lua_setfield(L_, -2, "GetHero");
    lua_setglobal(L_, "Scene");

    // ── Game / SoundLibrary / Properties / ParticleEmitter / Skill / Spell ───
    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int { (void)L; return api(L, "Game.SetCinematicMode"); })
        ; lua_setfield(L_, -2, "SetCinematicMode");
    push_fn(L_, [](lua_State* L) -> int { lua_pushnumber(L, g_host ? g_host->clock() : 0.0); return api(L, "Game.Time"), 1; })
        ; lua_setfield(L_, -2, "Time");
    lua_setglobal(L_, "Game");

    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        const char* name = luaL_checkstring(L, 1);
        if (g_host) g_host->request_sound(name);
        return api(L, "SoundLibrary.PlayEffect");
    }); lua_setfield(L_, -2, "PlayEffect");
    lua_setglobal(L_, "SoundLibrary");

    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        // Properties.GetProperty(self, name): the object's own property bag.
        RuntimeObject* o = resolve(L, 1);
        const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
        if (o) {
            if (RuntimeComponent* props = o->find_class("PropertiesComponent")) {
                if (RuntimeComponent* entity = o->find_class("EntityComponent")) {
                    lua_pushnumber(L, entity->number(name, 0.0f));
                    return api(L, "Properties.GetProperty"), 1;
                }
                (void)props;
            }
        }
        lua_pushnumber(L, 0.0);
        return api(L, "Properties.GetProperty"), 1;
    }); lua_setfield(L_, -2, "GetProperty");
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* o = resolve(L, 1);
        const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
        if (o) {
            if (RuntimeComponent* entity = o->find_class("EntityComponent")) {
                if (const RuntimeField* f = entity->field(name)) {
                    lua_pushnumber(L, static_cast<double>(f->varint_value));
                    return api(L, "Properties.SetProperty"), 1;
                }
            }
            const double v = luaL_optnumber(L, 3, 0.0);
            (void)v;
        }
        return api(L, "Properties.SetProperty"), 0;
    }); lua_setfield(L_, -2, "SetProperty");
    lua_setglobal(L_, "Properties");

    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
        const float value = static_cast<float>(luaL_optnumber(L, 3, 0.0));
        if (b) {
            const int slot = std::max(0, std::min(7, index));
            b->emitter.origin_offset[slot % 3] = value;
        }
        return api(L, "ParticleEmitter.SetParameterAtIndex");
    }); lua_setfield(L_, -2, "SetParameterAtIndex");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->emitter.playing = true;
        return api(L, "ParticleEmitter.Start");
    }); lua_setfield(L_, -2, "Start");
    push_fn(L_, [](lua_State* L) -> int {
        BehaviourState* b = behaviour_of(L, 1);
        if (b) b->emitter.playing = false;
        return api(L, "ParticleEmitter.Stop");
    }); lua_setfield(L_, -2, "Stop");
    lua_setglobal(L_, "ParticleEmitter");

    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        RuntimeObject* caster = resolve(L, 1);
        if (g_host && caster) g_host->begin_cast(*caster);
        return api(L, "Skill.BeginCasting");
    }); lua_setfield(L_, -2, "BeginCasting");
    lua_setglobal(L_, "Skill");

    lua_newtable(L_);
    push_fn(L_, [](lua_State* L) -> int {
        // Spell.CasterObject: the object whose SpellComponent OnCast is running.
        push_object(L, g_self);
        return api(L, "Spell.CasterObject"), 1;
    }); lua_setfield(L_, -2, "CasterObject");
    lua_setglobal(L_, "Spell");

    return true;
}

void ProgramHost::shutdown() {
    if (!L_) return;
    for (Instance* instance : instances_) {
        if (instance->thread != LUA_NOREF) luaL_unref(L_, LUA_REGISTRYINDEX, instance->thread);
        delete instance;
    }
    instances_.clear();
    pending_.clear();
    current_ = nullptr;
    lua_close(L_);
    L_ = nullptr;
    if (g_host == this) g_host = nullptr;
    g_scene = nullptr;
    g_self = nullptr;
}

int ProgramHost::running_scripts() const {
    int n = 0;
    for (const Instance* instance : instances_)
        if (!instance->finished) ++n;
    return n;
}

void ProgramHost::report(const RuntimeObject& object, const std::string& field,
                         const std::string& message) {
    errors_.push_back(object.identifier + ":" + (field.empty() ? "Program" : field) + ": " + message);
}

bool ProgramHost::load_program(const std::string& field_name, const std::string& source,
                               const std::string& bytecode) {
    // Engine parity: Program::LoadIntoState feeds Program.Bytes straight into
    // luaL_loadbuffer and does not look at Program.String. The source is only a
    // fallback for data that has no compiled chunk at all.
    if (!bytecode.empty() && bytecode.size() > 4 && bytecode.compare(0, 4, "\x1bLua") == 0) {
        if (luaL_loadbuffer(L_, bytecode.data(), bytecode.size(), ("scl:" + field_name).c_str()) == 0)
            return true;
        const std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "bytecode load failed";
        lua_pop(L_, 1);
        if (!source.empty()) {
            if (luaL_loadstring(L_, source.c_str()) == 0) return true;
            lua_pop(L_, 1);
        }
        errors_.push_back("Program:" + field_name + ": " + err);
        return false;
    }
    if (source.empty()) return false;
    if (luaL_loadstring(L_, source.c_str()) == 0) return true;
    const std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "load failed";
    lua_pop(L_, 1);
    errors_.push_back("Program:" + field_name + ": " + err);
    return false;
}

void ProgramHost::start_programs(RuntimeScene& scene, RuntimeObject& object) {
    if (!L_) return;
    for (const RuntimeProgram& program : object.programs) {
        const bool is_main = program.field_name == "Program";
        if (!is_main && !program.keep_active) {
            // Event handler: stored, fired when the event happens.
            Instance* handler = new Instance();
            handler->object = object.identifier;
            handler->field = program.field_name;
            handler->owner_class = program.owner_class;
            handler->one_shot = true;
            handler->finished = true;   // never resumed by the clock
            instances_.push_back(handler);
            continue;
        }
        Instance* instance = new Instance();
        instance->object = object.identifier;
        instance->field = program.field_name;
        instance->owner_class = program.owner_class;
        instance->keep_active = is_main || program.keep_active;
        instances_.push_back(instance);

        lua_State* thread = lua_newthread(L_);
        instance->thread = luaL_ref(L_, LUA_REGISTRYINDEX);
        if (!load_program(program.field_name, program.source, program.bytecode)) {
            instance->finished = true;
            continue;
        }
        // Move the loaded chunk onto the coroutine and start it with (self).
        lua_xmove(L_, thread, 1);
        push_object(thread, &object);
        lua_resume(thread, 1);
        instance->started = true;
        instance->runs = 1;
    }
    (void)scene;
}

void ProgramHost::start(RuntimeScene& scene) {
    if (!init()) return;
    g_scene = &scene;
    for (RuntimeObject& object : scene.mutable_objects())
        start_programs(scene, object);
}

void ProgramHost::start_object(RuntimeScene& scene, RuntimeObject& object) {
    if (!L_) return;
    g_scene = &scene;
    start_programs(scene, object);
}

void ProgramHost::update_object(RuntimeScene& scene, RuntimeObject& object, float dt) {
    if (!L_) return;
    g_scene = &scene;
    g_self = &object;

    const std::string identifier = object.identifier;
    // `instances_` can grow during a resume (Scene.CreateObject starts the new
    // object's scripts), so iterate by snapshot count and re-resolve the object
    // pointer every time — a spawn may reallocate `objects_`.
    const size_t count = instances_.size();
    for (size_t i = 0; i < count; ++i) {
        Instance* instance = instances_[i];
        if (instance->object != identifier) continue;
        // A one-shot handler is only skipped while it has never started; a handler
        // that suspended on Program.Wait has a live coroutine the scheduler owns.
        if (instance->finished || (instance->one_shot && !instance->started)) continue;

        // ProgramState::Update 0x545480: a waiting state is revived by its own
        // countdown — `remaining -= scaled; if (remaining < 0.0f) resume` — where
        // the step is the frame dt scaled by the owner's speed multiplier and then
        // by the state's time scale. The port compared an absolute clock deadline
        // instead, which ignores slow-motion and hitstop entirely.
        //
        // The owner multiplier is 1.0 here: RuntimeObject has no recovered
        // `SceneObject::updateSpeedMultiplier` equivalent yet (see the status
        // doc), and inventing one would be worse than naming the gap.
        if (instance->waiting) {
            const float scaled = caver::prog::Scheduler::scaled_dt(dt, 1.0f, 1.0f);
            if (!caver::prog::Scheduler::tick_wait(instance->wait_remaining, scaled)) continue;
            instance->waiting = false;
        }

        RuntimeObject* live = scene.object(identifier);
        if (!live) break;
        set_current(instance);
        resume(scene, *live, *instance, dt);
        set_current(nullptr);
        g_self = scene.object(identifier);
    }
    g_self = nullptr;
}

void ProgramHost::resume(RuntimeScene& scene, RuntimeObject& object, Instance& instance, float dt) {
    lua_State* thread = nullptr;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, instance.thread);
    thread = lua_tothread(L_, -1);
    if (!thread) { lua_pop(L_, 1); instance.finished = true; return; }

    if (!instance.started) {
        // Restart a keep-active program that returned (its `while true` loop
        // exited) — Swordigo re-runs keep-active programs.
        const RuntimeProgram* program = nullptr;
        for (const RuntimeProgram& p : object.programs)
            if (p.field_name == instance.field) { program = &p; break; }
        if (program && load_program(program->field_name, program->source, program->bytecode)) {
            lua_xmove(L_, thread, 1);
            push_object(thread, &object);
            lua_resume(thread, 1);
            instance.started = true;
            ++instance.runs;
        } else {
            instance.finished = true;
        }
        lua_pop(L_, 1);
        return;
    }

    // The wait that brought us here is spent (ProgramState::Update sets status
    // Running before resuming); Program.Wait inside the resume may set a new one.
    instance.waiting = false;
    instance.wait_remaining = 0.0f;
    const int status = lua_resume(thread, 0);

    // ProgramState::Resume 0x545624 / Update 0x545480: the result must be exactly
    // LUA_YIELD(1). Anything else — a clean return included — sets the abort flag
    // and the state is unlinked. A keep-alive script therefore has to loop; it is
    // never restarted on return.
    if (caver::prog::resume_outcome(status) == caver::prog::ResumeOutcome::Aborted) {
        if (status != LUA_YIELD && status != 0) {
            const char* message = lua_tostring(thread, -1);
            report(object, instance.field, message ? message : "runtime error");
        }
        instance.finished = true;
    } else if (!instance.waiting) {
        // Yielded with no Program.Wait behind it. The engine leaves such a state
        // Running, and Update only ever resumes a Waiting one — so there would be
        // no wait left to wake it. Reported instead of hanging silently.
        report(object, instance.field,
               "yielded outside Program.Wait — the scheduler has no wait to resume from");
        instance.finished = true;
    }
    lua_pop(L_, 1);
    (void)scene;
    (void)dt;
}

void ProgramHost::run_handler(RuntimeScene& scene, RuntimeObject& object, Instance& instance) {
    if (!L_) return;
    g_scene = &scene;
    g_self = &object;

    const RuntimeProgram* program = nullptr;
    for (const RuntimeProgram& p : object.programs)
        if (p.field_name == instance.field) { program = &p; break; }
    if (!program) return;

    lua_State* thread = lua_newthread(L_);
    const int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    if (load_program(program->field_name, program->source, program->bytecode)) {
        lua_xmove(L_, thread, 1);
        push_object(thread, &object);
        instance.waiting = false;
        instance.wait_remaining = 0.0f;
        const int status = lua_resume(thread, 1);
        if (status == LUA_YIELD) {
            if (instance.waiting) {
                // The handler suspended on Program.Wait, so under the recovered
                // rule (ProgramState::Update 0x545480 revives only a Waiting
                // state) the coroutine still has a future. Hand it to the
                // scheduler and keep the registry ref alive; unref-ing here is
                // what used to make a waiting handler vanish.
                instance.thread = ref;
                instance.started = true;
                instance.finished = false;
                ++instance.runs;
                g_self = nullptr;
                return;
            }
            report(object, instance.field,
                   "handler yielded outside Program.Wait — no wait to resume from");
        } else if (status != 0) {
            const char* message = lua_tostring(thread, -1);
            report(object, instance.field, message ? message : "handler error");
        }
        // A handler that returned or errored is done: the engine aborts (and so
        // unlinks) the state on any non-yield resume result.
        instance.finished = true;
    }
    luaL_unref(L_, LUA_REGISTRYINDEX, ref);
    ++instance.runs;
    g_self = nullptr;
}

void ProgramHost::fire(RuntimeScene& scene, RuntimeObject& object, const std::string& event,
                       RuntimeObject* other) {
    if (!L_) return;
    // Queue only. The engine delivers events at the program pass, after every
    // C++ component has ticked, so a handler can never run mid-iteration.
    pending_.push_back({{object.identifier, event}, other ? other->identifier : std::string()});
    (void)scene;
}

void ProgramHost::pump(RuntimeScene& scene) {
    if (!L_) return;
    g_scene = &scene;

    // Deliver the events the C++ behaviours raised this frame. Snapshot the
    // instance list: a handler may spawn an object, which registers new scripts.
    auto events = pending_;
    pending_.clear();
    for (const auto& event : events) {
        const std::string& identifier = event.first.first;
        const std::string& name = event.first.second;
        RuntimeObject* target = scene.object(identifier);
        if (!target) continue;
        scene.record_event(SceneEvent{name, identifier, event.second,
                                      handler_class_for(*target, name), clock_});

        const size_t count = instances_.size();
        for (size_t i = 0; i < count; ++i) {
            Instance* instance = instances_[i];
            if (instance->object != identifier || instance->field != name) continue;
            RuntimeObject* live = scene.object(identifier);
            if (!live) break;
            run_handler(scene, *live, *instance);
        }
    }
    g_scene = nullptr;
}

std::vector<std::string> ProgramHost::handler_names(const RuntimeObject& object) const {
    std::vector<std::string> names;
    for (const RuntimeProgram& program : object.programs) {
        if (program.field_name == "Program") continue;
        names.push_back(program.field_name);
    }
    return names;
}

const char* ProgramHost::handler_class_for(const RuntimeObject& object,
                                           const std::string& event) const {
    for (const RuntimeProgram& program : object.programs)
        if (program.field_name == event) return program.owner_class.c_str();
    return "";
}

void ProgramHost::request_sound(const std::string& name) {
    if (name.empty()) return;
    if (std::find(sounds_.begin(), sounds_.end(), name) == sounds_.end()) sounds_.push_back(name);
}

void behaviour_fire_event(RuntimeScene& scene, RuntimeObject& object,
                          const std::string& event_field, RuntimeObject* other) {
    if (ProgramHost* host = scene.program_host()) host->fire(scene, object, event_field, other);
}

void ProgramHost::begin_cast(RuntimeObject& caster) {
    // Skill.BeginCasting: enter the Cast action; the SkillComponent's cast object
    // (CastObjectTemplateName) is spawned by the component tick that follows.
    set_action(&caster.behaviour, EntityAction::Cast, 0.8f);
    if (!caster.behaviour.cast_animation.empty())
        caster.behaviour.anim.current = caster.behaviour.cast_animation;
}

void ProgramHost::set_keep_active(bool keep) {
    if (current_) current_->keep_active = keep;
}

// Caver::ProgramState::Wait (0x5457AC), reached from the Program.Wait C function.
void ProgramHost::set_wait(double seconds) {
    if (!current_) return;
    current_->wait_remaining = static_cast<float>(seconds);
    current_->waiting = true;
}

void ProgramHost::execute_named(RuntimeObject* object, const std::string& name) {
    if (!object || !L_ || !g_scene) return;
    const size_t count = instances_.size();
    for (size_t i = 0; i < count; ++i) {
        Instance* instance = instances_[i];
        if (instance->object != object->identifier || instance->field != name) continue;
        run_handler(*g_scene, *object, *instance);
        return;
    }
}

} // namespace caver
