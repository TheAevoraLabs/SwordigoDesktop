#include "ruby/caver/behaviour.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "ruby/caver/library_manager.h"
#include "ruby/caver/runtime.h"

namespace caver {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Nested Vector2/Vector3 payloads are fixed32 triples (X, Y[, Z]) — the same
// encoding runtime.cpp decodes for reference and program fields.
void read_vec(const std::string& bytes, float* out, int want) {
    for (int i = 0; i < want; ++i) out[i] = 0.0f;
    if (bytes.empty()) return;
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        int n = 0;
        while (reader.read_field(field) && n < want) {
            if (field.wire_type == proto::WIRE_I32)      out[n++] = field.float_val;
            else if (field.wire_type == proto::WIRE_I64) out[n++] = static_cast<float>(field.double_val);
        }
    } catch (...) {}
}

void read_vec_field(const RuntimeComponent& c, const char* name, float* out, int want) {
    const RuntimeField* f = c.field(name);
    if (f) read_vec(f->bytes_value, out, want);
}

bool flag(const RuntimeComponent& c, const char* name, bool fallback) {
    const RuntimeField* f = c.field(name);
    return f ? (f->varint_value != 0) : fallback;
}

// Deterministic-per-object RNG so a replayed frame matches (Caver::fastrandom is
// intentionally replaced: the recovered behaviour only needs a bounded wander
// pick, and determinism is worth more than bit-fidelity here).
uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float hash_unit(uint32_t seed) { return static_cast<float>(hash32(seed) & 0xffffffu) / 16777215.0f; }

// A named animation reference is stored as a component identifier; the readable
// name lives on the referenced KeyframeAnimationComponent.
std::string animation_name_for(RuntimeObject& object, int32_t component_identifier) {
    const RuntimeComponent* c = object.find(component_identifier);
    if (!c) return std::string();
    const RuntimeField* name = c->field("Name");
    return name ? name->bytes_value : std::string();
}

void set_action(BehaviourState& b, EntityAction action, float duration) {
    b.action = action;
    b.action_time = 0.0f;
    b.action_duration = duration;
}

bool action_finished(const BehaviourState& b) {
    return b.action_duration > 0.0f && b.action_time >= b.action_duration;
}

// ── entity physics ──────────────────────────────────────────────────────────
void tick_physics_object(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                         BehaviourState& b, float dt) {
    // An ElevatorController drives its own object: it writes the position AND the
    // velocity (this is the engine's own structure -- see level_mechanics.h), so
    // integrating that velocity here as well would move the platform twice as far
    // as it should. The flag is set every frame the controller is driving.
    if (b.trigger.lift_driving) return;
    PhysicsState& p = b.physics;
    p.enabled = flag(c, "PhysicsEnabled", true);
    read_vec_field(c, "GravityDirection", p.gravity_dir, 2);
    p.gravity_magnitude = c.number("GravityMagnitude", 0.0f);
    p.ground_deceleration = c.number("GroundDeceleration", 0.0f);
    p.air_deceleration = c.number("AirDeceleration", 0.0f);
    p.max_speed = c.number("MaxSpeed", 0.0f);
    p.elasticity = c.number("Elasticity", 0.0f);
    p.allow_rotation = flag(c, "AllowRotation", false);
    if (!p.enabled || !p.entity_physics) return;

    const float gx = p.gravity_dir[0] * p.gravity_magnitude;
    const float gy = p.gravity_dir[1] * p.gravity_magnitude;
    object.vel[0] += gx * dt;
    object.vel[1] += gy * dt;

    // Deceleration applies to the axis the gravity does not own.
    const float decel = (p.on_ground ? p.ground_deceleration : p.air_deceleration);
    if (decel > 0.0f) {
        const float drop = decel * dt;
        for (int axis = 0; axis < 3; ++axis) {
            if (axis == 1 && std::fabs(p.gravity_dir[1]) > 0.5f) continue;
            if (object.vel[axis] > 0.0f) object.vel[axis] = std::max(0.0f, object.vel[axis] - drop);
            else if (object.vel[axis] < 0.0f)     object.vel[axis] = std::min(0.0f, object.vel[axis] + drop);
        }
    }

    if (p.max_speed > 0.0f) {
        const float speed = std::sqrt(object.vel[0] * object.vel[0] +
                                      object.vel[1] * object.vel[1] +
                                      object.vel[2] * object.vel[2]);
        if (speed > p.max_speed) {
            const float k = p.max_speed / speed;
            for (float& v : object.vel) v *= k;
        }
    }

    object.pos[0] += object.vel[0] * dt;
    object.pos[1] += object.vel[1] * dt;
    object.pos[2] += object.vel[2] * dt;

    // Grounding comes from the world query when one is installed (the av::
    // collision layer); without it objects are treated as airborne so gravity
    // still resolves visually.
    const float ground = scene.ground_height(object.pos[0], object.pos[2]);
    if (object.pos[1] <= ground) {
        object.pos[1] = ground;
        if (object.vel[1] < 0.0f) object.vel[1] = -object.vel[1] * p.elasticity;
        if (std::fabs(object.vel[1]) < 0.25f) object.vel[1] = 0.0f;
        p.on_ground = true;
        p.air_time = 0.0f;
    } else {
        p.on_ground = false;
        p.air_time += dt;
    }
    object.grounded = p.on_ground;
}

// ── animation ───────────────────────────────────────────────────────────────
const BehaviourState::AnimClip* clip_named(const BehaviourState& b, const std::string& name) {
    for (const auto& clip : b.anim_clips)
        if (clip.name == name) return &clip;
    return nullptr;
}

// CharAnimControllerComponent + CharControllerComponent.
//
// This no longer chooses a clip. The choice is the recovered controller's
// (char_anim.h): a three-node blend tree the *driver* moves through the engine's
// own transitions (char_controller.h), and this function is only the binding —
// build the driver's input out of what the port can observe, tick the tree, and
// publish whichever clip the tree's weights favour.
//
// What the port can observe is the limiting factor, and it is worth being exact
// about it rather than papering over:
//
//   * there is no input layer here, so `jump_pressed` and `ledge_step_up` are
//     false. The jump never fires; the run/stand/fall decisions all do.
//   * there is no continuous ground clearance — the port's ground test is a feet
//     contact — so the driver's 0.15 clearance gate is reported as satisfied
//     whenever the feet are clear. The threshold itself is pinned by
//     caver_char_controller_test.
//   * the +584 air sub-state enumeration is unrecovered (only the value 3 is),
//     so 0 is passed.
//   * StartMoving is not part of Update at all — it belongs to the movement path
//     (StartMovingToDirection, 0x35D578) — so the port synthesises that call from
//     the character's own horizontal motion, and it synthesises the *stop* through
//     StopMovingToDirection (0x35D6AC), which cuts the run cycle by clearing the
//     ground flag and letting the driver's 0.11 s air rule do it. Calling the anim
//     controller's StopMoving directly here would be an instant cut the engine
//     never performs.
void tick_char_animation(RuntimeScene& scene, RuntimeObject& object, BehaviourState& b, float dt) {
    (void)scene;
    BehaviourState::CharAnimState& c = b.char_anim;
    if (!c.active || !b.anim_rig || !b.anim_rig->ready()) return;

    canim::CharAnimController& anim = b.anim_rig->controller();
    cchar::CharControllerState& ds = b.char_driver.state;

    const bool airborne = !b.physics.on_ground && !object.grounded;
    if (airborne) c.air_time += dt; else c.air_time = 0.0f;

    // ── the movement path, synthesised ──────────────────────────────────────
    // Only on a *change*: StartMoving re-phases the run clip (a quarter of its
    // cycle), so calling it every frame would pin the run cycle to that phase.
    const int32_t dir = (std::fabs(object.vel[0]) > 1.0f)
                            ? (object.vel[0] > 0.0f ? 1 : -1)
                            : 0;
    if (airborne) {
        if (ds.on_ground || ds.moving_direction != 0)
            b.char_driver.stop_moving_to_direction(ds.moving_direction);
    } else if (dir != 0 && ds.moving_direction != dir) {
        b.char_driver.start_moving_to_direction(dir, anim);
    } else if (dir == 0 && ds.moving_direction != 0) {
        b.char_driver.stop_moving_to_direction(ds.moving_direction);
    }

    // ── the action slot ─────────────────────────────────────────────────────
    // A cast is the recovered one: BeginCasting puts the cast clip in the action
    // slot with the 0.08 s fade, and the driver's hand-back releases it on the
    // frame the action ends. Hurt and die are *not* recovered from
    // CharControllerComponent — they are entered from the damage/entity layer —
    // so the port maps "something wants this clip played" onto the recovered
    // BlendFromAnimationNode, which is the engine's way of fading from where the
    // character visually was into a new clip.
    const bool casting = (b.action == EntityAction::Cast);
    if (casting && !ds.cast_active) {
        ds.cast_active = true;
        anim.begin_casting();
    }
    const bool reacting = (b.action == EntityAction::Hurt) || (b.action == EntityAction::Death);
    const std::string& react_clip = (b.action == EntityAction::Hurt) ? c.hurt : c.die;
    if (reacting && !c.in_action) {
        const int idx = b.anim_rig->index_of_name(react_clip);
        if (anim::KeysNode* node = b.anim_rig->node_at(idx))
            anim.blend_from_animation_node(*node, cchar::kActionBlendTime);
    }
    c.in_action = reacting || casting;

    // ── the driver ──────────────────────────────────────────────────────────
    cchar::CharInput in;
    in.height_above_ground = airborne ? 1.0f : 0.0f;   // see the note above
    in.vertical_speed = object.vel[1];
    // The component's own speed, which the driver copies to +608 and hands to
    // SetCurrentRunSpeed as `speed / 100`. It has to be a *speed*: passing the
    // authored NormalRunSpeed instead would freeze the run cycle on the objects
    // that leave it at 0 (c.run_speed is kept below as the authored value/fallback).
    in.run_speed = std::fabs(object.vel[0]) > 1.0f ? std::fabs(object.vel[0]) : c.run_speed;
    in.movement_behavior = static_cast<int>(b.movement);
    in.jump_pressed = false;          // no input layer in a scene player
    in.ledge_step_up = false;
    in.swing_node_available = false;
    in.cast_finished = ds.cast_active && !casting;
    b.char_driver.update(dt, in, anim);

    // The clocks, then the tree: every outlet advances, because the engine's blend
    // node recurses into both children, and then the controller walks the tree once.
    b.anim_rig->advance(dt);
    anim.update(dt);

    // ── publish ─────────────────────────────────────────────────────────────
    // The clip the weight favours, and *that clip's own clock* — each outlet keeps
    // one, so a switch is a different clock rather than a reset of a shared one.
    const int shown = b.anim_rig->index_of(anim.display_node().keys);
    if (shown >= 0 && !b.anim_rig->name_at(shown).empty()) c.current = b.anim_rig->name_at(shown);
    c.time = b.anim_rig->time_at(shown);

    b.anim.current = c.current;
    b.anim.time = c.time;
    b.anim.speed = 1.0f;      // the outlet clocks are already rate-scaled
    b.anim.repeating = true;
    b.anim.running = !c.current.empty();
}

void tick_animation(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    // A character's clips are played by its own controller (tick_char_animation),
    // not by the per-component KeyframeAnimation clock: otherwise every clip on
    // the archetype would advance the one clock and overwrite the current name.
    if (b.char_anim.active &&
        (c.is("KeyframeAnimationComponent") || c.is("AnimationControllerComponent") ||
         c.is("BlendAnimationComponent") || c.is("CharAnimControllerComponent")))
        return;
    if (c.is("KeyframeAnimationComponent")) {
        b.anim.current = c.field("Name") ? c.field("Name")->bytes_value : b.anim.current;
        b.anim.speed = c.number("SpeedMultiplier", 1.0f);
        b.anim.repeating = flag(c, "Repeating", true);
        b.anim.running = flag(c, "Running", true);
        if (b.anim.running) b.anim.time += dt * b.anim.speed;
    } else if (c.is("AnimationControllerComponent")) {
        // SelfUpdate: the controller owns its clock and drives its model's
        // DefaultAnimationId child unless a script blended away from it.
        if (flag(c, "SelfUpdate", true) && b.anim.blend <= 0.0f) {
            const int32_t default_id = static_cast<int32_t>(c.number("DefaultAnimationId", 0.0f));
            const std::string name = animation_name_for(object, default_id);
            if (!name.empty()) b.anim.current = name;
            b.anim.running = true;
            b.anim.time += dt * b.anim.speed;
        }
    } else if (c.is("BlendAnimationComponent")) {
        const float blend_time = c.number("BlendTime", 0.0f);
        if (b.anim.blend < 1.0f && blend_time > 0.0f)
            b.anim.blend = std::min(1.0f, b.anim.blend + dt / blend_time);
    } else if (c.is("CharAnimControllerComponent")) {
        if (const RuntimeField* f = c.field("StandAnimationId"))
            b.stand_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (const RuntimeField* f = c.field("WalkAnimationId"))
            b.walk_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (const RuntimeField* f = c.field("JumpAnimationId"))
            b.jump_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (const RuntimeField* f = c.field("FallAnimationId"))
            b.fall_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (const RuntimeField* f = c.field("CastAnimationId"))
            b.cast_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (b.anim.current.empty()) b.anim.current = b.stand_animation;
    }
}

// ── entity / entity controller ──────────────────────────────────────────────
void tick_entity(RuntimeComponent& c, BehaviourState& b) {
    const float facing = c.number("FacingDirection", 0.0f);
    if (facing != 0.0f) b.facing = facing < 0.0f ? -1 : 1;
    b.physics.entity_physics = flag(c, "PhysicsEnabled", true);
}

void controller_init(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b) {
    b.default_move_speed = c.number("DefaultMoveSpeed", 0.0f);
    b.default_acceleration = c.number("DefaultAcceleration", 0.0f);
    b.targeting_distance = c.number("TargetingDistance", 0.0f);
    b.movement = static_cast<MovementBehavior>(static_cast<int>(c.number("MovementBehavior", 0.0f)));
    b.move_speed = b.default_move_speed;
    b.accel = b.default_acceleration;

    if (const RuntimeField* f = c.field("DefaultMoveAnimationId"))
        b.default_move_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
    if (const RuntimeField* f = c.field("AnimationControllerId")) {
        // The controller's animation child also names its default animation.
        const int32_t id = static_cast<int32_t>(f->varint_value);
        const RuntimeComponent* anim = object.find(id);
        if (anim && anim->is("AnimationControllerComponent")) {
            const std::string name =
                animation_name_for(object, static_cast<int32_t>(anim->number("DefaultAnimationId", 0.0f)));
            if (!name.empty()) b.stand_animation = name;
        }
    }
    if (const RuntimeField* f = c.field("RoamAreaId")) {
        if (const RuntimeComponent* area = object.find(static_cast<int32_t>(f->varint_value))) {
            const RuntimeField* circle = area->field("Circle");
            if (circle) read_vec(circle->bytes_value, b.roam_origin, 2);
            const RuntimeField* rect = area->field("Rectangle");
            if (rect) {
                float r[4] = {0, 0, 0, 0};
                read_vec(rect->bytes_value, r, 4);
                b.roam_origin[0] = r[0];
                b.roam_origin[1] = r[1];
                b.roam_radius = std::max(r[2] - r[0], r[3] - r[1]) * 0.5f;
            }
            if (circle) {
                float cc[3] = {0, 0, 0};
                read_vec(circle->bytes_value, cc, 3);
                b.roam_radius = cc[2];
            }
        }
    }
    // A monster controller that declares WalkSpeed overrides the entity speed.
    if (c.is("MonsterControllerComponent")) {
        const float walk = c.number("WalkSpeed", 0.0f);
        if (walk > 0.0f) b.move_speed = walk;
        b.movement = MovementBehavior::Follow;
    }
}

// Movement + action state machine. `aggro_range` > 0 turns it into a chaser;
// 0 keeps it passive (the static/walking variants).
void tick_controller(RuntimeScene& scene, RuntimeObject& object, BehaviourState& b, float dt) {
    b.action_time += dt;
    if (action_finished(b)) {
        set_action(b, EntityAction::Idle, 0.0f);
        b.idle = true;
    }

    // Target acquisition: EntityController.TargetingDistance gates the hero.
    const RuntimeObject* hero = scene.hero();
    if (hero && b.targeting_distance > 0.0f) {
        const float dx = hero->pos[0] - object.pos[0];
        const float dy = hero->pos[1] - object.pos[1];
        b.target_distance = std::sqrt(dx * dx + dy * dy);
        const bool in_range = b.target_distance <= b.targeting_distance;
        if (in_range && !b.has_target) {
            b.has_target = true;
            b.target_object = hero->identifier;
        } else if (!in_range && b.has_target && b.target_distance > b.targeting_distance * 1.25f) {
            b.has_target = false;
            b.target_object.clear();
        }
    }

    const bool busy = b.action != EntityAction::Idle && b.action != EntityAction::Walk &&
                      b.action != EntityAction::Roam;

    if (busy && b.action != EntityAction::Jump && b.action != EntityAction::Fall) {
        b.move_dir[0] = 0.0f;
        b.move_dir[1] = 0.0f;
    } else if (b.has_target && hero) {
        // Face and move toward the target.
        const float dx = hero->pos[0] - object.pos[0];
        b.facing = dx < 0.0f ? -1 : 1;
        const float len = std::max(1e-4f, std::fabs(dx));
        b.move_dir[0] = dx / len;
        b.move_dir[1] = 0.0f;
        set_action(b, EntityAction::Walk, 0.0f);
        b.idle = false;
    } else if (b.movement == MovementBehavior::Roam) {
        // RoamUpdate: walk to a point inside the roam area, wait, repeat.
        b.roam_wait -= dt;
        if (b.roam_wait <= 0.0f) {
            const uint32_t seed = hash32(static_cast<uint32_t>(object.identifier.size() * 2654435761u) ^
                                         static_cast<uint32_t>(object.pos[0] * 100.0f) ^
                                         static_cast<uint32_t>(b.state_timer * 1000.0f));
            const float angle = hash_unit(seed) * kTwoPi;
            const float radius = b.roam_radius > 0.0f ? b.roam_radius : 2.0f;
            b.roam_target[0] = b.roam_origin[0] + std::cos(angle) * radius * hash_unit(seed + 1);
            b.roam_target[1] = b.roam_origin[1] + std::sin(angle) * radius * hash_unit(seed + 2);
            b.roam_wait = 1.0f + hash_unit(seed + 3) * 2.0f;
        }
        const float dx = b.roam_target[0] - object.pos[0];
        if (std::fabs(dx) > 0.1f) {
            b.move_dir[0] = dx < 0.0f ? -1.0f : 1.0f;
            b.facing = dx < 0.0f ? -1 : 1;
            set_action(b, EntityAction::Roam, 0.0f);
            b.idle = false;
        } else {
            b.move_dir[0] = 0.0f;
            b.idle = true;
        }
        b.move_dir[1] = 0.0f;
    } else if (b.movement == MovementBehavior::Walk) {
        // Plain back-and-forth walker: reverse at the roam radius, else patrol.
        const float limit = b.roam_radius > 0.0f ? b.roam_radius : 3.0f;
        if (object.pos[0] > b.roam_origin[0] + limit) b.facing = -1;
        if (object.pos[0] < b.roam_origin[0] - limit) b.facing = 1;
        b.move_dir[0] = static_cast<float>(b.facing);
        b.move_dir[1] = 0.0f;
        set_action(b, EntityAction::Walk, 0.0f);
        b.idle = false;
    } else if (b.action == EntityAction::Walk || b.action == EntityAction::Roam) {
        set_action(b, EntityAction::Idle, 0.0f);
        b.idle = true;
    }

    // Integrate the controller's own motion into the physics velocity. Scripts
    // that also call EntityController.SetMoveSpeed write here, which is why the
    // controller — not PhysicsObject — owns the horizontal velocity.
    const float speed = b.move_speed > 0.0f ? b.move_speed : 0.0f;
    object.vel[0] = b.move_dir[0] * speed;
    object.vel[2] = b.move_dir[1] * speed;
    if (!b.physics.on_ground && std::fabs(object.vel[1]) > 0.0f && !b.physics.enabled)
        object.pos[1] += object.vel[1] * dt;

    // Animation follows the action.
    if (b.idle && !b.stand_animation.empty())        b.anim.current = b.stand_animation;
    else if (!b.idle && !b.walk_animation.empty())   b.anim.current = b.walk_animation;
    else if (!b.default_move_animation.empty() && !b.idle) b.anim.current = b.default_move_animation;
    b.state_timer += dt;
}

// ── monster controllers ─────────────────────────────────────────────────────
// Every MonsterController* class shares MonsterControllerComponent's core
// (WalkSpeed / AnimationControllerId / EntityId / RoamAreaId) and differs only in
// which animation it plays and how it attacks. That is exactly the split in the
// decompilation, so the shared core lives here and each class contributes traits.
struct MonsterTraits {
    const char* animation_field;   // component field naming the locomotion clip
    bool  gravity = true;
    float attack_range = 1.2f;
    float attack_cooldown = 1.5f;
    float hop_speed = 0.0f;        // > 0 => hopping locomotion (Bouncing)
    float hop_angle = 1.0f;
    bool  periodic_attack = false; // attacks on cooldown rather than on contact
};

MonsterTraits traits_for(const RuntimeComponent& c) {
    MonsterTraits t{};
    if (c.is("BatMonsterControllerComponent"))        { t.animation_field = "FlyAnimationId";    t.gravity = false; t.attack_range = 1.0f; }
    else if (c.is("BouncingMonsterControllerComponent")) { t.animation_field = "JumpAnimationId"; t.hop_speed = c.number("JumpSpeed", 3.0f); t.hop_angle = c.number("JumpAngle", 1.0f); }
    else if (c.is("LeapingMonsterControllerComponent"))  { t.animation_field = "WalkAnimationId"; t.periodic_attack = true; t.attack_cooldown = 2.0f; t.attack_range = 3.0f; }
    else if (c.is("ShootingMonsterControllerComponent")) { t.animation_field = "WalkAnimationId"; t.periodic_attack = true; }
    else if (c.is("SnappingMonsterControllerComponent")) { t.animation_field = "StandAnimationId"; t.attack_range = 1.8f; t.attack_cooldown = 2.2f; }
    else if (c.is("SkellyMonsterControllerComponent"))   { t.animation_field = "WalkAnimationId"; t.attack_range = 1.6f; }
    else if (c.is("StaticMonsterControllerComponent"))   { t.animation_field = "AnimationId";     t.attack_cooldown = 3.0f; }
    else if (c.is("ChargingMonsterControllerComponent")) { t.animation_field = "WalkAnimationId"; t.attack_range = 2.4f; }
    else if (c.is("WalkingMonsterControllerComponent"))  { t.animation_field = "WalkAnimationId"; }
    else if (c.is("GenericMonsterControllerComponent"))  { t.animation_field = "WalkAnimationId"; }
    else                                                 { t.animation_field = nullptr; }
    return t;
}

void tick_monster_controller(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                             BehaviourState& b, float dt) {
    const MonsterTraits t = traits_for(c);
    b.physics.enabled = t.gravity;

    if (t.animation_field) {
        const RuntimeField* f = c.field(t.animation_field);
        if (f) {
            const std::string name = animation_name_for(object, static_cast<int32_t>(f->varint_value));
            if (!name.empty()) {
                b.walk_animation = name;
                if (b.anim.current.empty()) b.anim.current = name;
            }
        }
    }
    if (c.is("ShootingMonsterControllerComponent")) {
        if (const RuntimeField* f = c.field("ShootAnimationId"))
            b.shoot_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
    }
    if (c.is("SnappingMonsterControllerComponent")) {
        if (const RuntimeField* f = c.field("AttackAnimationId"))
            b.attack_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (const RuntimeField* f = c.field("BlendAnimationId"))
            b.blend_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
    }

    b.attack_cooldown = std::max(0.0f, b.attack_cooldown - dt);
    const bool in_range = b.has_target && b.target_distance <= t.attack_range;

    if (t.hop_speed > 0.0f) {
        // BouncingMonsterController: re-launch along JumpAngle whenever grounded.
        if (b.physics.on_ground && b.has_target) {
            const float dir = b.facing != 0 ? static_cast<float>(b.facing) : 1.0f;
            object.vel[0] = std::cos(t.hop_angle) * t.hop_speed * dir;
            object.vel[1] = std::sin(t.hop_angle) * t.hop_speed;
            b.physics.on_ground = false;
            set_action(b, EntityAction::Jump, 0.5f);
        }
    } else if (t.periodic_attack && in_range && b.attack_cooldown <= 0.0f) {
        b.attack_cooldown = t.attack_cooldown;
        set_action(b, EntityAction::Attack, 0.6f);
        if (!b.attack_animation.empty()) b.anim.current = b.attack_animation;
        if (!b.shoot_animation.empty())  b.anim.current = b.shoot_animation;
        behaviour_fire_event(scene, object, "OnAttack", nullptr);
    } else if (!t.periodic_attack && in_range && b.attack_cooldown <= 0.0f) {
        b.attack_cooldown = t.attack_cooldown;
        set_action(b, EntityAction::Attack, 0.5f);
        if (!b.attack_animation.empty()) b.anim.current = b.attack_animation;
    }
}

// ── attack / health / damage ────────────────────────────────────────────────
void tick_attack(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                 BehaviourState& b, float dt, bool controller_started) {
    const float interval = c.number("AttackInterval", 0.0f);
    const float duration = c.number("AttackDuration", 0.0f);
    const float damage_start = c.number("DamageStartTime", 0.0f);
    const float damage_end = c.number("DamageEndTime", 0.0f);
    const int32_t shape_id = static_cast<int32_t>(c.number("CollisionShapeId", 0.0f));

    if (const RuntimeField* f = c.field("AnimationId"))
        b.attack_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));

    // The damage window is a collision-shape enable window — the same mechanism
    // CollisionShape.SetEnabled uses from Lua. AttackComponent.CollisionShapeId
    // names the shape that inflicts the hit during [DamageStartTime, DamageEndTime].
    const bool in_window = b.action == EntityAction::Attack &&
                           b.action_time >= damage_start && b.action_time <= damage_end;
    b.damage_dealt_this_swing = in_window;
    if (in_window && shape_id != 0)
        b.health.shape_enabled = true;

    if (b.action == EntityAction::Attack && b.action_duration <= 0.0f && duration > 0.0f)
        b.action_duration = duration;
    (void)controller_started;
    (void)interval;
}

void tick_health(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                 BehaviourState& b, float dt) {
    HealthState& h = b.health;
    h.max_health = c.number("MaxHealth", h.max_health);
    if (h.health <= 0.0f && h.max_health > 0.0f) h.health = h.max_health;
    h.hurt_timer = std::max(0.0f, h.hurt_timer - dt);
    h.invuln_timer = std::max(0.0f, h.invuln_timer - dt);
    if (h.hurt_timer <= 0.0f && b.action == EntityAction::Hurt) set_action(b, EntityAction::Idle, 0.0f);
    if (h.health <= 0.0f && h.max_health > 0.0f) h.dead = true;
    (void)object;
    (void)scene;
}

void tick_damage(RuntimeComponent& c, BehaviourState& b) {
    b.health.damage_min = c.number("MinDamage", 0.0f);
    b.health.damage_max = c.number("MaxDamage", b.health.damage_min);
}

void tick_collision_shape(RuntimeComponent& c, BehaviourState& b) {
    // `Enabled` is both the initial state and what CollisionShape.SetEnabled
    // writes at runtime; a pending re-enable is resolved by behaviour_tick. A
    // controller that has forced the shape OFF (a door while it is open) wins over
    // the authored value, which is the whole point of SetEnabled.
    if (b.health.shape_forced_off) b.health.shape_enabled = false;
    else if (b.health.reenable_timer <= 0.0f) b.health.shape_enabled = flag(c, "Enabled", true);
    b.health.collides = flag(c, "Collides", true);
    b.health.is_ground = flag(c, "IsGround", false);
    b.health.receives_damage = flag(c, "ReceivesDamage", false);
    b.health.inflicts_damage = flag(c, "InflictsDamage", false);
    b.physics.ground_friction = c.number("Friction", 1.0f);
    b.physics.unsafe_ground = flag(c, "UnsafeGround", false);
}

// ── triggers / doors / collectables ─────────────────────────────────────────
void tick_pressure_trigger(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                           BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    t.max_height_offset = c.number("MaxHeightOffset", 0.0f);
    t.stay_pressed = flag(c, "StayPressed", false);

    const RuntimeObject* hero = scene.hero();
    bool on_plate = false;
    if (hero) {
        const float dx = std::fabs(hero->pos[0] - object.pos[0]);
        const float dz = std::fabs(hero->pos[2] - object.pos[2]);
        const float dy = hero->pos[1] - object.pos[1];
        on_plate = dx < 0.75f && dz < 0.75f && dy >= -0.25f && dy <= std::max(0.5f, t.max_height_offset);
    }
    if (on_plate && !t.pressed) {
        if (t.stay_pressed && t.press_timer > 0.0f) { /* stays latched */ }
        else { t.pressed = true; t.press_timer = 0.0f; behaviour_fire_event(scene, object, "OnPress", nullptr); }
    } else if (!on_plate && t.pressed) {
        if (t.stay_pressed) return;
        t.pressed = false;
        behaviour_fire_event(scene, object, "OnRelease", nullptr);
    }
    t.press_timer += dt;
}

void tick_touchable(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                    BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    t.touch_radius = c.number("TouchRadius", 0.0f);
    const RuntimeObject* hero = scene.hero();
    if (!hero || t.touch_radius <= 0.0f) return;
    const float dx = hero->pos[0] - object.pos[0];
    const float dy = hero->pos[1] - object.pos[1];
    const float dz = hero->pos[2] - object.pos[2];
    const bool inside = dx * dx + dy * dy + dz * dz <= t.touch_radius * t.touch_radius;
    if (inside && !t.touched) {
        t.touched = true;
        behaviour_fire_event(scene, object, "OnTouch", nullptr);
    } else if (!inside) {
        t.touched = false;
    }
    (void)dt;
}

void tick_door(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    // `Open` is the component's own protobuf field (Proto::DoorControllerComponent-
    // ::kOpenFieldNumber) and binding index 4 ("open"), and both read the same
    // byte, so the field and the binding are one value.
    const bool want_open = flag(c, "Open", false);

    // `SetOpen` seeks the animation's time field to the clip's open frame (clip
    // +48), read through `KeyframeAnimationComponent::LoadAnimation`. The ANI
    // loader is not wired yet (CAVER_BACKEND_STATUS.md open item 2), so there is
    // no clip to read that frame out of; a scene that supplies one is honoured.
    // This is the single input the recovered transition cannot derive itself.
    const int open_frame = static_cast<int>(c.number("OpenFrame", 0.0f));

    // `Open()`/`Close()` only play the open/close SoundEffect when
    // `*(*(animationController + 120) + 326) != 0`. The port has no
    // AnimationController playback-state object for that byte to live on yet, so
    // the gate is open; recorded rather than silently dropped.
    const bool sound_gate = true;

    mechanics::DoorTransition tr = t.door.set_open(want_open, open_frame, sound_gate);
    t.open = t.door.is_open();
    t.door_sound = tr.sound;
    // `Open()` walks the owning object's components and `SetEnabled(false)` on
    // every one that implements `CollisionShapeComponent::Interface`; `Close()`
    // re-enables them. That is the entire reason a closed door blocks and an open
    // one does not -- and the port previously had no door/collision link at all.
    b.health.shape_forced_off = !tr.collision_enabled;

    // `t.open_amount` stays 0/unknown on purpose: the door's travel IS the clip,
    // and the clip has no loaded duration yet. `DoorController::open_amount()` is
    // where the real answer will come from once the ANI loader lands -- it already
    // refuses to invent a number.
    (void)dt;

    if (const RuntimeField* f = c.field("AnimationControllerId"))
        b.anim.current = animation_name_for(object, static_cast<int32_t>(f->varint_value));
    if (const RuntimeField* f = c.field("AnimationId")) {
        const std::string name = animation_name_for(object, static_cast<int32_t>(f->varint_value));
        if (!name.empty()) b.anim.current = name;
    }
}

void tick_elevator(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    mechanics::ElevatorController& lift = t.lift;

    // `Mode` is the component's own payload field, and it is 1-BASED:
    // `LoadFromProtobufMessage` (0x3A117C) writes `Mode - 1` into +144, and only
    // when the field is present. An absent field leaves the internal default, 1.
    if (const RuntimeField* f = c.field("Mode")) {
        lift.mode = mechanics::ElevatorController::mode_from_payload(
            static_cast<int>(f->varint_value));
    }

    if (!t.lift_prepared) {
        // `Prepare()` derives the travel offset from the bounds of the component
        // bound by the outlet named "elevationShape" (`Shape::Bounds`). No shape,
        // no travel: the platform cannot move, and refusing is the honest outcome
        // rather than inventing a reach from a radius.
        if (const RuntimeField* f = c.field("ElevationShapeId")) {
            if (const RuntimeComponent* shape =
                    object.find(static_cast<int32_t>(f->varint_value))) {
                Rect bounds{};
                bool have = false;
                if (const RuntimeField* rect = shape->field("Rectangle")) {
                    float r[4] = {0, 0, 0, 0};
                    read_vec(rect->bytes_value, r, 4);
                    bounds = Rect{r[0], r[1], r[2] - r[0], r[3] - r[1]};
                    have = true;
                } else if (const RuntimeField* circle = shape->field("Circle")) {
                    float cc[3] = {0, 0, 0};
                    read_vec(circle->bytes_value, cc, 3);
                    bounds = Rect{cc[0] - cc[2], cc[1] - cc[2], cc[2] * 2.0f, cc[2] * 2.0f};
                    have = true;
                }
                if (have) {
                    lift.prepare(bounds);
                    t.lift_prepared = true;
                }
            }
        }
    }

    if (t.lift_prepared) {
        // The recovered controller integrates the OWNER's position and velocity
        // itself, exactly as the engine does: it aims the velocity at the target,
        // steps by `vel * dt`, snaps onto the target, and zeroes the velocity on
        // arrival. So the port hands it the object's own body and takes the result
        // back -- there is no second, invented rate and no separate travel scalar
        // for the render side to disagree with.
        float pos[2] = {object.pos[0], object.pos[1]};
        float vel[2] = {object.vel[0], object.vel[1]};
        mechanics::ElevatorStep step = lift.update(dt, pos, vel);
        object.pos[0] = pos[0];
        object.pos[1] = pos[1];
        object.vel[0] = vel[0];
        object.vel[1] = vel[1];
        t.mode = static_cast<int>(lift.mode);
        t.travel = object.pos[1] - object.base_pos[1];
        // The velocity has already been integrated above, so the physics step must
        // not integrate it again.
        t.lift_driving = true;
        (void)step;
    } else {
        t.lift_driving = false;
    }
}

void tick_bush(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    t.wobble_timer = std::max(0.0f, t.wobble_timer - dt);
    if (t.wobble_timer <= 0.0f && t.wobbling) t.wobbling = false;
    if (const RuntimeField* f = c.field("WobbleAnimationId"))
        b.anim.current = animation_name_for(object, static_cast<int32_t>(f->varint_value));
}

void tick_breakable(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                    BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    t.hits_to_break = static_cast<int>(c.number("NumHitsToBreak", 0.0f));
    t.breaks_on_impact = flag(c, "BreaksOnImpact", false);
    (void)dt;
    if (t.hits_to_break > 0 && t.hits_taken >= t.hits_to_break && !t.broken) {
        t.broken = true;
        object.hidden = true;
        behaviour_fire_event(scene, object, "OnBreak", nullptr);
    }
}

void tick_collectable(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                      BehaviourState& b, float dt) {
    TriggerState& t = b.trigger;
    t.value = static_cast<int>(c.number("Value", 0.0f));
    if (t.collected) { object.hidden = true; return; }
    const RuntimeObject* hero = scene.hero();
    if (!hero) return;
    const float dx = hero->pos[0] - object.pos[0];
    const float dy = hero->pos[1] - object.pos[1];
    if (dx * dx + dy * dy <= 1.0f) {
        t.collected = true;
        behaviour_fire_event(scene, object, "OnCollect", nullptr);
    }
    (void)dt;
}

void tick_item_drop(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                    BehaviourState& b, float dt) {
    // ItemDropComponent: roll each DropEntry once, then hand the template names to
    // the SCL runtime (Scene::CreateObject parity).
    if (b.trigger.collected) return;
    b.trigger.collected = true;   // reuse as "rolled" — objects drop once
    const RuntimeField* entry = c.field("DropEntry");
    if (!entry) return;
    try {
        proto::Reader reader(entry->bytes_value);
        proto::Field field;
        std::string template_name;
        float chance = 1.0f;
        int count = 1;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN && field.field_number == 10) template_name = field.bytes_val;
            if (field.wire_type == proto::WIRE_I32 && field.field_number == 29) chance = field.float_val;
            if (field.wire_type == proto::WIRE_VARINT && field.field_number == 32) count = static_cast<int>(field.varint_val);
        }
        if (!template_name.empty() && hash_unit(hash32(static_cast<uint32_t>(object.identifier.size()))) <= chance) {
            for (int i = 0; i < std::max(1, count); ++i) {
                const std::string id = object.identifier + "_drop" + std::to_string(i);
                if (scene.spawn(template_name, id, object.pos)) break;
            }
        }
    } catch (...) {}
    (void)dt;
}

// ── spells / projectiles ────────────────────────────────────────────────────
void tick_magic_bolt(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                     BehaviourState& b, float dt) {
    ProjectileState& p = b.projectile;
    p.speed = c.number("Speed", p.speed);
    p.max_life = p.max_life > 0.0f ? p.max_life : 3.0f;
    const float dir = b.facing != 0 ? static_cast<float>(b.facing) : 1.0f;
    object.vel[0] = dir * p.speed;
    object.vel[1] = 0.0f;
    p.life += dt;
    if (p.life >= p.max_life && !p.exploded) {
        p.exploded = true;
        object.hidden = true;
        behaviour_fire_event(scene, object, "OnExplode", nullptr);
    }
}

void tick_magic_bomb(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                     BehaviourState& b, float dt) {
    ProjectileState& p = b.projectile;
    p.max_life = p.max_life > 0.0f ? p.max_life : 4.0f;
    b.physics.enabled = true;
    if (b.physics.gravity_magnitude <= 0.0f) b.physics.gravity_magnitude = 9.8f;
    p.life += dt;
    if (b.physics.on_ground || p.life >= p.max_life) {
        if (!p.exploded) {
            p.exploded = true;
            object.hidden = true;
            behaviour_fire_event(scene, object, "OnExplode", nullptr);
        }
    }
    (void)c;
}

void tick_magic_explosion(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                          BehaviourState& b, float dt) {
    ProjectileState& p = b.projectile;
    p.radius = c.number("Radius", 1.0f);
    p.max_life = c.number("Duration", 0.4f);
    p.life += dt;
    // Damage everything inside the radius once.
    if (!p.exploded) {
        p.exploded = true;
        for (const auto& other : scene.objects()) {
            if (other.identifier == object.identifier) continue;
            if (!other.behaviour.health.receives_damage && other.behaviour.health.max_health <= 0.0f) continue;
            const float dx = other.pos[0] - object.pos[0];
            const float dy = other.pos[1] - object.pos[1];
            if (dx * dx + dy * dy <= p.radius * p.radius) {
                behaviour_fire_event(scene, const_cast<RuntimeObject&>(other), "OnReceiveDamage", &object);
            }
        }
    }
    if (p.life >= p.max_life) object.hidden = true;
}

void tick_projectile_controller(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    ProjectileState& p = b.projectile;
    p.align_rotation = flag(c, "AlignObjectRotation", true);
    p.break_on_ground = flag(c, "BreakOnGroundCollision", false);
    p.life += dt;
    if (p.align_rotation && std::fabs(object.vel[0]) > 1e-4f)
        object.rot[1] = object.vel[0] < 0.0f ? kTwoPi * 0.5f : 0.0f;
    if (p.break_on_ground && b.physics.on_ground) object.hidden = true;
}

void tick_skill(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                BehaviourState& b, float dt) {
    if (const RuntimeField* f = c.field("CastObjectTemplateName")) {
        const std::string tmpl = f->bytes_value;
        if (!tmpl.empty() && b.projectile.spawned_object.empty()) {
            b.projectile.spawned_object = tmpl;
            const std::string id = object.identifier + "_cast";
            scene.spawn(tmpl, id, object.pos);
        }
    }
    if (const RuntimeField* f = c.field("CastFinishAnimationId"))
        b.cast_animation = animation_name_for(object, static_cast<int32_t>(f->varint_value));
    (void)dt;
}

void tick_swingable_weapon(RuntimeScene& scene, RuntimeObject& object, RuntimeComponent& c,
                           BehaviourState& b, float dt) {
    if (const RuntimeField* f = c.field("ModelId"))
        b.projectile.spawned_object = animation_name_for(object, static_cast<int32_t>(f->varint_value));
    // The controller object is spawned by SwingableWeaponControllerComponent at
    // attack time; the damage window is owned by the SwingComponent's frames.
    (void)scene;
    (void)object;
    (void)dt;
}

// ── transforms, lights, emitters ────────────────────────────────────────────
void tick_model_transform(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    const float speed = c.number("RotationSpeed", 0.0f);
    float axis[3] = {0, 1, 0};
    read_vec_field(c, "RotationAxis", axis, 3);
    b.model_rotation += speed * dt;
    if (axis[1] != 0.0f && axis[0] == 0.0f && axis[2] == 0.0f) object.rot[1] = b.model_rotation;
    else if (axis[0] != 0.0f) object.rot[0] = b.model_rotation;
    else if (axis[2] != 0.0f) object.rot[2] = b.model_rotation;
    (void)dt;
}

void tick_orbit(RuntimeObject& object, RuntimeComponent& c, BehaviourState& b, float dt) {
    const float speed = c.number("RotationSpeed", 0.0f);
    const float distance = c.number("OrbitDistance", 0.0f);
    float axis[3] = {0, 1, 0};
    read_vec_field(c, "RotationAxis", axis, 3);
    b.orbit_angle += speed * dt;
    // OrbitControllerComponent orbits the object's own placement, so the circle
    // is centred on the object's authored position captured at load time.
    const float cx = b.roam_origin[0] != 0.0f ? b.roam_origin[0] : object.pos[0];
    const float cy = b.roam_origin[1] != 0.0f ? b.roam_origin[1] : object.pos[1];
    object.pos[0] = cx + std::cos(b.orbit_angle) * distance;
    object.pos[1] = cy + std::sin(b.orbit_angle) * distance * std::fabs(axis[1]);
    (void)dt;
}

void tick_light(RuntimeComponent& c, BehaviourState& b) {
    b.light_intensity = c.number("Intensity", b.light_intensity);
    b.light_radius = c.number("Radius", b.light_radius);
    const RuntimeField* colour = c.field("Color");
    if (colour) read_vec(colour->bytes_value, b.light_color, 3);
    read_vec_field(c, "Offset", b.light_offset, 3);
}

void tick_simple_glow(RuntimeComponent& c, BehaviourState& b, float dt) {
    b.glow_pulse_amount = c.number("PulseAmount", 0.0f);
    b.glow_pulse_time = c.number("PulseTime", 1.0f);
    if (b.glow_pulse_amount != 0.0f && b.glow_pulse_time > 0.0f) b.emitter.time += dt;
}

void tick_emitter(RuntimeComponent& c, BehaviourState& b, float dt) {
    EmitterState& e = b.emitter;
    e.max_particles = static_cast<int>(c.number("MaxParticles", 0.0f));
    e.destroy_when_finished = flag(c, "DestroyWhenFinished", false);
    e.local_system = flag(c, "LocalSystem", false);
    read_vec_field(c, "Gravity", e.gravity, 3);
    read_vec_field(c, "Rotation", e.rotation, 3);
    // Emission cadence: ParticleInterval/ParticleMaxAge are authored per
    // FireEmitter. For the generic ParticleEmitter the cadence is derived by the
    // particle Type (unverified from the decompilation so far), so the neutral
    // 20 Hz default is used and recorded in emitter.interval.
    if (const RuntimeField* f = c.field("ParticleInterval")) e.interval = f->float_value > 0.0f ? f->float_value : e.interval;
    if (const RuntimeField* f = c.field("ParticleMaxAge"))  e.particle_age_max = f->float_value > 0.0f ? f->float_value : e.particle_age_max;
    if (e.interval <= 0.0f) e.interval = 0.05f;

    e.time += dt;
    e.accumulator += dt;
    while (e.accumulator >= e.interval) {
        e.accumulator -= e.interval;
        ++e.spawned;
        if (e.max_particles > 0 && e.spawned > e.max_particles && e.destroy_when_finished) {
            e.playing = false;
            break;
        }
    }
}

void tick_water(RuntimeComponent& c, BehaviourState& b, float dt) {
    b.water_time += dt;
    (void)c;
}

// Component class -> tick table. Kept explicit so `behaviour_has_tick` and the
// coverage report can never disagree with the dispatch.
const char* const kTickedClasses[] = {
    "SpriteComponent", "ModelComponent", "KeyframeAnimationComponent", "BlendAnimationComponent",
    "ModelTransformControllerComponent", "GroundPolygonComponent", "GroundMeshComponent",
    "WaterMeshComponent", "ShapeComponent", "CollisionShapeComponent", "DamageComponent",
    "HealthComponent", "BoneControlledCollisionShapeComponent", "ObjectLinkControllerComponent",
    "LightComponent", "ShadowComponent", "SoundEffectComponent", "AnimationControllerComponent",
    "CharAnimControllerComponent", "CharControllerComponent", "EntityComponent",
    "BushControllerComponent", "ElevatorControllerComponent", "PressureTriggerComponent",
    "DoorControllerComponent", "ProgramComponent", "MonsterEntityComponent",
    "PhysicsObjectComponent", "BreakableObjectComponent", "EntityControllerComponent",
    "EntityActionComponent", "PhysicsPlatformComponent", "EntityInfoComponent",
    "HeroEntityComponent", "PropertiesComponent", "ParticleEmitterComponent", "ParticleComponent",
    "FireEmitterComponent", "SimpleGlowComponent", "ParticleObjectComponent",
    "OrbitControllerComponent", "MonsterControllerComponent", "WalkingMonsterControllerComponent",
    "ChargingMonsterControllerComponent", "SnappingMonsterControllerComponent", "AttackComponent",
    "LeapingMonsterControllerComponent", "SkellyMonsterControllerComponent",
    "StaticMonsterControllerComponent", "ShootingMonsterControllerComponent",
    "BatMonsterControllerComponent", "BouncingMonsterControllerComponent",
    "MonsterDeathControllerComponent", "GenericMonsterControllerComponent",
    "SwingableWeaponComponent", "SwingableWeaponControllerComponent", "SwingComponent",
    "WeaponGlowComponent", "WeaponTrailComponent", "PortalComponent", "SpawnPointComponent",
    "CollectableItemComponent", "TouchableComponent", "ItemDropComponent", "OverlayTextComponent",
    "PortalEffectComponent", "MagicBoltComponent", "MagicExplosionComponent", "SkillComponent",
    "MagicSpellCastComponent", "FireBreathComponent", "ProjectileControllerComponent",
    "MagicBombComponent", "MagicHookshotComponent", "SpellComponent", "TextureMappingComponent",
    "GroundMeshGeneratorComponent", "BackgroundComponent", "Program", "ObjectLinkController",
};

} // namespace

bool behaviour_has_tick(const std::string& class_name) {
    for (const char* name : kTickedClasses)
        if (class_name == name) return true;
    // Short forms shipping data uses as ClassName.
    static const char* const kShort[] = {
        "Sprite", "Model", "KeyframeAnimation", "BlendAnimation", "ModelTransformController",
        "GroundPolygon", "GroundMesh", "WaterMesh", "Shape", "UtilityShape", "CollisionShape",
        "Damage", "Health", "BoneControlledCollisionShape", "ObjectLinkController", "Light",
        "Shadow", "SoundEffect", "AnimationController", "CharAnimController", "CharController",
        "Entity", "BushController", "ElevatorController", "PressureTrigger", "DoorController",
        "Program", "MonsterEntity", "PhysicsObject", "BreakableObject", "EntityController",
        "EntityAction", "PhysicsPlatform", "EntityInfo", "HeroEntity", "Properties",
        "ParticleEmitter", "Particle", "FireEmitter", "SimpleGlow", "Glow", "ParticleObject",
        "OrbitController", "MonsterController", "WalkingMonsterController",
        "ChargingMonsterController", "SnappingMonsterController", "Attack",
        "LeapingMonsterController", "SkellyMonsterController", "StaticMonsterController",
        "ShootingMonsterController", "BatMonsterController", "BouncingMonsterController",
        "MonsterDeathController", "GenericMonsterController", "SwingableWeapon",
        "SwingableWeaponController", "Swing", "WeaponGlow", "WeaponTrail", "Portal",
        "SpawnPoint", "CollectableItem", "Touchable", "ItemDrop", "OverlayText",
        "PortalEffect", "MagicBolt", "MagicExplosion", "Skill", "MagicSpellCast", "FireBreath",
        "ProjectileController", "MagicBomb", "MagicHookshot", "Spell", "TextureMapping",
        "GroundMeshGenerator", "Background",
    };
    for (const char* name : kShort)
        if (class_name == name) return true;
    return false;
}

void char_anim_init(RuntimeObject& object, BehaviourState& b);

void behaviour_init(const RuntimeScene& scene, RuntimeObject& object) {
    BehaviourState& b = object.behaviour;
    b.health.health = 0.0f;

    for (auto& c : object.components) {
        if (!behaviour_has_tick(c.class_name) && !(c.type && behaviour_has_tick(c.type->class_name)))
            continue;
        b.active.push_back(c.class_name);
        if (c.is("OrbitControllerComponent")) {
            // Remember the authored centre before the orbit moves the object.
            b.roam_origin[0] = object.pos[0];
            b.roam_origin[1] = object.pos[1];
        }

        if (c.is("EntityControllerComponent")) controller_init(object, c, b);
        else if (c.is("MonsterControllerComponent")) {
            controller_init(object, c, b);
            b.movement = MovementBehavior::Follow;
        } else if (c.is("WalkingMonsterControllerComponent") || c.is("GenericMonsterControllerComponent"))
            b.movement = MovementBehavior::Walk;
        else if (c.is("EntityComponent")) tick_entity(c, b);
        else if (c.is("PhysicsObjectComponent")) {
            b.physics.enabled = flag(c, "PhysicsEnabled", true);
            b.physics.gravity_magnitude = c.number("GravityMagnitude", 0.0f);
            b.physics.max_speed = c.number("MaxSpeed", 0.0f);
            read_vec_field(c, "GravityDirection", b.physics.gravity_dir, 2);
        } else if (c.is("HealthComponent")) {
            b.health.max_health = c.number("MaxHealth", 0.0f);
            b.health.health = b.health.max_health;
        } else if (c.is("DamageComponent")) tick_damage(c, b);
        else if (c.is("LightComponent")) tick_light(c, b);
        else if (c.is("KeyframeAnimationComponent") || c.is("AnimationControllerComponent")) {
            tick_animation(object, c, b, 0.0f);
            if (b.stand_animation.empty()) b.stand_animation = b.anim.current;
        }        else if (c.is("CharAnimControllerComponent")) tick_animation(object, c, b, 0.0f);
    }
    char_anim_init(object, b);
    if (b.move_speed <= 0.0f) b.move_speed = b.default_move_speed;
    (void)scene;
}

// Collect the archetype's clips and the character controller's own wiring. Run
// after every component has been seen, because the CharAnimController addresses
// its clips by component identifier and those live on KeyframeAnimation
// components that may appear anywhere in the object.
void char_anim_init(RuntimeObject& object, BehaviourState& b) {
    for (auto& c : object.components) {
        if (c.is("KeyframeAnimationComponent")) {
            const RuntimeField* name = c.field("Name");
            if (!name || name->bytes_value.empty()) continue;
            BehaviourState::AnimClip clip;
            clip.name    = name->bytes_value;
            clip.speed   = c.number("SpeedMultiplier", 1.0f);
            clip.repeating = flag(c, "Repeating", true);
            clip.running   = flag(c, "Running", true);
            b.anim_clips.push_back(std::move(clip));
        }
    }

    const RuntimeComponent* controller = nullptr;
    for (auto& c : object.components)
        if (c.is("CharAnimControllerComponent")) { controller = &c; break; }
    if (!controller) return;

    BehaviourState::CharAnimState& c = b.char_anim;
    c.active = true;
    auto clip_name = [&](const char* field_name) {
        const RuntimeField* f = controller->field(field_name);
        if (!f) return std::string();
        return animation_name_for(object, static_cast<int32_t>(f->varint_value));
    };
    c.stand    = clip_name("StandAnimationId");
    c.walk     = clip_name("WalkAnimationId");
    c.jump     = clip_name("JumpAnimationId");
    c.fall     = clip_name("FallAnimationId");
    c.air_jump = clip_name("AirJumpAnimationId");
    c.cast     = clip_name("CastAnimationId");

    if (const RuntimeComponent* cc = object.find_class("CharControllerComponent")) {
        c.hurt  = animation_name_for(object, static_cast<int32_t>(cc->number("HurtAnimationId", 0.0f)));
        c.die   = animation_name_for(object, static_cast<int32_t>(cc->number("DieAnimationId", 0.0f)));
        c.push  = animation_name_for(object, static_cast<int32_t>(cc->number("PushAnimationId", 0.0f)));
        c.run_speed  = cc->number("NormalRunSpeed", 0.0f);
        c.jump_speed = cc->number("JumpSpeed", 0.0f);
    }

    // ── the recovered controller tree ───────────────────────────────────────
    // Six outlets in constructor order (100..105). An empty name is the guest's
    // null outlet: the tree is still built and that leaf simply evaluates nothing.
    std::array<std::string, canim::ClipBinding::kCount> outlets{
        c.stand, c.walk, c.jump, c.air_jump, c.fall, c.cast};
    b.anim_rig = std::make_shared<canim::ClipBinding>();
    b.anim_rig->configure(outlets, /*model_present=*/true);
    for (int i = 0; i < canim::ClipBinding::kCount; ++i) {
        if (const BehaviourState::AnimClip* clip = clip_named(b, outlets[i])) {
            b.anim_rig->set_speed(i, clip->speed);   // the node's own +48 factor
        }
    }

    // The spawn state (+552/+584/+596). The port has no input layer, so an object
    // spawns standing: `set_initial_animation_state` then does `Reset` and nothing
    // else, which leaves the root blend on the stand clip — the same place the
    // guessed version put it, but now it is the engine's own route there.
    canim::SpawnState spawn;
    b.char_driver.set_initial_animation_state(spawn, b.anim_rig->controller());

    c.current = c.stand;
    c.time = 0.0f;
    if (const BehaviourState::AnimClip* clip = clip_named(b, c.current))
        c.rate = clip->speed;
    b.anim.current = c.current;
    b.anim.time = 0.0f;
    b.anim.running = !c.current.empty();
}

void behaviour_tick(RuntimeScene& scene, RuntimeObject& object, float dt) {
    BehaviourState& b = object.behaviour;
    if (!object.activated || object.hidden) return;

    for (auto& c : object.components) {
        if (!behaviour_has_tick(c.class_name) &&
            !(c.type && behaviour_has_tick(c.type->class_name)))
            continue;

        if (c.is("PhysicsObjectComponent"))          tick_physics_object(scene, object, c, b, dt);
        else if (c.is("EntityComponent"))            tick_entity(c, b);
        else if (c.is("EntityControllerComponent"))  tick_controller(scene, object, b, dt);
        else if (c.is("KeyframeAnimationComponent") || c.is("AnimationControllerComponent") ||
                 c.is("BlendAnimationComponent") || c.is("CharAnimControllerComponent"))
                                                     tick_animation(object, c, b, dt);
        else if (c.is("MonsterControllerComponent") || c.is("WalkingMonsterControllerComponent") ||
                 c.is("ChargingMonsterControllerComponent") ||
                 c.is("SnappingMonsterControllerComponent") ||
                 c.is("LeapingMonsterControllerComponent") ||
                 c.is("SkellyMonsterControllerComponent") ||
                 c.is("StaticMonsterControllerComponent") ||
                 c.is("ShootingMonsterControllerComponent") ||
                 c.is("BatMonsterControllerComponent") ||
                 c.is("BouncingMonsterControllerComponent") ||
                 c.is("GenericMonsterControllerComponent"))
                                                     tick_monster_controller(scene, object, c, b, dt);
        else if (c.is("AttackComponent"))            tick_attack(scene, object, c, b, dt, true);
        else if (c.is("HealthComponent"))            tick_health(scene, object, c, b, dt);
        else if (c.is("DamageComponent"))            tick_damage(c, b);
        else if (c.is("CollisionShapeComponent"))    tick_collision_shape(c, b);
        else if (c.is("PressureTriggerComponent"))   tick_pressure_trigger(scene, object, c, b, dt);
        else if (c.is("TouchableComponent"))         tick_touchable(scene, object, c, b, dt);
        else if (c.is("DoorControllerComponent"))    tick_door(object, c, b, dt);
        else if (c.is("ElevatorControllerComponent"))tick_elevator(object, c, b, dt);
        else if (c.is("BushControllerComponent"))    tick_bush(object, c, b, dt);
        else if (c.is("BreakableObjectComponent"))   tick_breakable(scene, object, c, b, dt);
        else if (c.is("CollectableItemComponent"))   tick_collectable(scene, object, c, b, dt);
        else if (c.is("ItemDropComponent"))          tick_item_drop(scene, object, c, b, dt);
        else if (c.is("MagicBoltComponent"))         tick_magic_bolt(scene, object, c, b, dt);
        else if (c.is("MagicBombComponent"))         tick_magic_bomb(scene, object, c, b, dt);
        else if (c.is("MagicExplosionComponent"))    tick_magic_explosion(scene, object, c, b, dt);
        else if (c.is("ProjectileControllerComponent")) tick_projectile_controller(object, c, b, dt);
        else if (c.is("SkillComponent"))             tick_skill(scene, object, c, b, dt);
        else if (c.is("SwingableWeaponComponent") || c.is("SwingableWeaponControllerComponent") ||
                 c.is("SwingComponent"))             tick_swingable_weapon(scene, object, c, b, dt);
        else if (c.is("ModelTransformControllerComponent")) tick_model_transform(object, c, b, dt);
        else if (c.is("OrbitControllerComponent"))   tick_orbit(object, c, b, dt);
        else if (c.is("LightComponent"))             tick_light(c, b);
        else if (c.is("SimpleGlowComponent"))        tick_simple_glow(c, b, dt);
        else if (c.is("ParticleEmitterComponent") || c.is("FireEmitterComponent") ||
                 c.is("WeaponGlowComponent") || c.is("WeaponTrailComponent") ||
                 c.is("PortalEffectComponent") || c.is("FireBreathComponent") ||
                 c.is("MagicSpellCastComponent"))
                                                     tick_emitter(c, b, dt);
        else if (c.is("WaterMeshComponent"))         tick_water(c, b, dt);
        else if (c.is("MonsterDeathControllerComponent")) {
            if (b.health.dead && !b.trigger.collected) {
                b.trigger.collected = true;
                if (const RuntimeField* f = c.field("ParticleEmitterId")) {
                    if (const RuntimeComponent* emitter = object.find(static_cast<int32_t>(f->varint_value))) {
                        (void)emitter;   // the death burst renders through the emitter
                    }
                }
                behaviour_fire_event(scene, object, "OnKill", nullptr);
            }
        }
    }

    // Controller motion is written back into the object each tick, so the
    // render state observes the same values the physics just integrated.
    b.action_time += 0.0f;
    if (b.health.reenable_timer > 0.0f) {
        b.health.reenable_timer -= dt;
        if (b.health.reenable_timer <= 0.0f) b.health.shape_enabled = true;
    }
    object.facing = b.facing;
    object.on_ground = b.physics.on_ground;

    // The character's clip selection runs last: it reads the state every other
    // component (entity, physics, collision, actions) has just written.
    tick_char_animation(scene, object, b, dt);
}

} // namespace caver
