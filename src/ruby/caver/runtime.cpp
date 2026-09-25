#include "ruby/caver/runtime.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "ruby/caver/library_manager.h"
#include "ruby/caver/program_host.h"
#include "tools/scene_schemas.h"

namespace caver {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Nested Vector2/Vector3 payloads are fixed32 triples (X, Y[, Z]).
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

// A payload field is a cross-component reference when the recovered schema names
// it `<something>Id` and it is an integer.
bool is_reference_field(const std::string& name, proto::WireType wire) {
    if (wire != proto::WIRE_VARINT) return false;
    return name.size() > 2 && name.compare(name.size() - 2, 2, "Id") == 0;
}

// Program.String / Program.Bytes — field 1 and field 2 of the Program message.
bool split_program(const std::string& program_bytes, std::string* source, std::string* bytecode) {
    try {
        proto::Reader reader(program_bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN) continue;
            if (field.field_number == 1) *source = field.bytes_val;
            else if (field.field_number == 2) *bytecode = field.bytes_val;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool looks_like_ai_loop(const std::string& source) {
    std::string compact;
    compact.reserve(source.size());
    for (char c : source)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') compact.push_back(c);
    return compact.find("whiletrue") != std::string::npos;
}

} // namespace

// ── RuntimeComponent ────────────────────────────────────────────────────────
bool RuntimeComponent::is(const std::string& cls) const {
    if (cls == class_name) return true;
    if (type && cls == type->class_name) return true;
    if (type && cls == type->short_name) return true;
    return false;
}

const RuntimePayload* RuntimeComponent::payload_for(uint32_t f) const {
    for (const auto& payload : payloads)
        if (payload.field == f) return &payload;
    return nullptr;
}

const RuntimePayload* RuntimeComponent::primary_payload() const {
    for (const auto& payload : payloads)
        if (payload.is_primary) return &payload;
    return payloads.empty() ? nullptr : &payloads.front();
}

const RuntimeField* RuntimeComponent::field(const std::string& name) const {
    for (const auto& f : fields)
        if (f.name == name) return &f;
    return nullptr;
}

float RuntimeComponent::number(const std::string& name, float fallback) const {
    const RuntimeField* f = field(name);
    if (!f) return fallback;
    switch (f->wire) {
        case proto::WIRE_VARINT: return static_cast<float>(f->varint_value);
        case proto::WIRE_I32:    return f->float_value;
        case proto::WIRE_I64:    return static_cast<float>(f->double_value);
        default:                 return fallback;
    }
}

// ── RuntimeObject ───────────────────────────────────────────────────────────
RuntimeComponent* RuntimeObject::find(int32_t component_identifier) {
    for (auto& c : components)
        if (c.identifier == component_identifier) return &c;
    return nullptr;
}

const RuntimeComponent* RuntimeObject::find(int32_t component_identifier) const {
    return const_cast<RuntimeObject*>(this)->find(component_identifier);
}

RuntimeComponent* RuntimeObject::find_class(const std::string& class_name) {
    for (auto& c : components)
        if (c.is(class_name)) return &c;
    return nullptr;
}

const RuntimeComponent* RuntimeObject::find_class(const std::string& class_name) const {
    return const_cast<RuntimeObject*>(this)->find_class(class_name);
}

// ── RuntimeScene ────────────────────────────────────────────────────────────
void RuntimeScene::clear() {
    objects_.clear();
    by_identifier_.clear();
    render_.clear();
    programs_.clear();
    warnings_.clear();
    events_.clear();
    clock_ = 0.0;
}

bool RuntimeScene::load(const av::SceneData& scene) {
    clear();

    // Head-room for scripted spawns (Scene.CreateObject, drops, casts): a growth
    // reallocation mid-program-pass would invalidate object pointers.
    objects_.reserve(scene.objects.size() + 256);
    for (size_t i = 0; i < scene.objects.size(); ++i)
        build_object(scene.objects[i], i);

    for (auto& object : objects_) {
        resolve_references(object);
        // Recovered behaviour state (controllers, physics, health, triggers …).
        behaviour_init(*this, object);
        // Programs are owned by the scene (one ProgramState each), not the object.
        for (const auto& program : object.programs) programs_.push_back(program);
    }

    render_.resize(objects_.size());
    for (size_t i = 0; i < objects_.size(); ++i) {
        ObjectRenderState& state = render_[i];
        RuntimeObject& object = objects_[i];
        state.identifier = object.identifier;
        state.pos[0] = object.pos[0];
        state.pos[1] = object.pos[1];
        state.pos[2] = object.pos[2];
        state.rot_y = object.rot[1];
        state.scale = object.scale[0];
        state.hidden = object.hidden;
        state.facing = object.behaviour.facing;
        state.on_ground = object.behaviour.physics.on_ground;
        state.light_radius = object.behaviour.light_radius;
        state.light_intensity = object.behaviour.light_intensity;
        state.light_color[0] = object.behaviour.light_color[0];
        state.light_color[1] = object.behaviour.light_color[1];
        state.light_color[2] = object.behaviour.light_color[2];
        object.base_pos[0] = object.pos[0];
        object.base_pos[1] = object.pos[1];
        object.base_pos[2] = object.pos[2];
    }

    // Start the scene's Lua programs on the real VM, if a host is attached.
    if (program_host_) program_host_->start(*this);
    return true;
}

void RuntimeScene::reindex() {
    by_identifier_.clear();
    for (size_t i = 0; i < objects_.size(); ++i)
        by_identifier_[objects_[i].identifier] = i;
}

RuntimeObject* RuntimeScene::hero() {
    auto it = by_identifier_.find(hero_identifier_);
    if (it != by_identifier_.end()) return &objects_[it->second];
    // A scene without an object literally named "hero" still has one: the object
    // carrying HeroEntityComponent.
    for (auto& object : objects_)
        if (object.find_class("HeroEntityComponent")) return &object;
    return nullptr;
}

const RuntimeObject* RuntimeScene::hero() const { return const_cast<RuntimeScene*>(this)->hero(); }

RuntimeObject* RuntimeScene::spawn(const std::string& template_name, const std::string& identifier,
                                   const float position[3]) {
    if (!libraries_) {
        warnings_.push_back("spawn('" + template_name + "') without an ObjectLibrary registry");
        return nullptr;
    }
    av::SclTemplateEntry entry;
    std::string owner;
    if (!libraries_->find_template(template_name, &entry, &owner)) {
        warnings_.push_back("spawn('" + template_name + "'): no template in any loaded library");
        return nullptr;
    }

    RuntimeObject object;
    object.identifier = identifier;
    object.template_name = template_name;
    object.template_scaling = entry.scaling;
    if (position) {
        object.pos[0] = position[0];
        object.pos[1] = position[1];
        object.pos[2] = position[2];
    }
    instantiate(entry.object, object);
    object.activated = !object.components.empty();

    if (by_identifier_.count(object.identifier)) {
        warnings_.push_back("spawn('" + template_name + "'): duplicate identifier '" + identifier + "'");
        return nullptr;
    }
    objects_.push_back(std::move(object));
    const size_t index = objects_.size() - 1;
    by_identifier_[objects_[index].identifier] = index;
    resolve_references(objects_[index]);
    behaviour_init(*this, objects_[index]);
    for (const auto& program : objects_[index].programs) programs_.push_back(program);
    render_.resize(objects_.size());
    render_[index].identifier = objects_[index].identifier;
    if (program_host_) program_host_->start_object(*this, objects_[index]);
    return &objects_[index];
}

void RuntimeScene::instantiate(const av::SceneObject& src, RuntimeObject& object) {
    // Same reconcile as build_object: the instance's own components with template
    // components folded in (SceneObject::InitWithTemplate).
    const auto& source = src.resolved_components.empty() ? src.components : src.resolved_components;
    for (const auto& component : source)
        decode_component(component, object);
    if (object.pos[0] == 0.0f && object.pos[1] == 0.0f && object.pos[2] == 0.0f) {
        object.pos[0] = src.pos_x; object.pos[1] = src.pos_y; object.pos[2] = src.pos_z;
        object.rot[1] = src.rot_y;
        object.scale[0] = src.scale_x;
    }
}

void RuntimeScene::build_object(const av::SceneObject& src, size_t index) {
    RuntimeObject object;
    object.identifier = src.name.empty() ? ("obj#" + std::to_string(index)) : src.name;
    object.template_name = src.template_name;
    object.pos[0] = src.pos_x; object.pos[1] = src.pos_y; object.pos[2] = src.pos_z;
    object.rot[0] = src.rot_x; object.rot[1] = src.rot_y; object.rot[2] = src.rot_z;
    object.scale[0] = src.scale_x; object.scale[1] = src.scale_y; object.scale[2] = src.scale_z;
    object.template_scaling = src.template_scaling;
    object.hidden = src.hidden;
    object.base_pos[0] = object.pos[0];
    object.base_pos[1] = object.pos[1];
    object.base_pos[2] = object.pos[2];

    instantiate(src, object);

    // An object with a template link is active once it has components; objects
    // whose template failed to resolve stay inactive (never ticked), which is
    // how the engine treats a missing ObjectLibrary.
    object.activated = !object.components.empty();
    if (object.components.empty() && !src.template_name.empty())
        warnings_.push_back("object '" + object.identifier + "': template '" +
                            src.template_name + "' resolved to no components");

    if (by_identifier_.count(object.identifier))
        warnings_.push_back("duplicate object identifier '" + object.identifier + "'");
    by_identifier_[object.identifier] = objects_.size();
    objects_.push_back(std::move(object));
}

// Component fields 1-4 are ClassName / Identifier / Label / ParentComponentIdentifier;
// everything from here up is a payload extension slot (Sprite=100, MonsterEntity=158 …).
constexpr int kFirstPayloadField = 8;

const av::SchemaClass* schema_for_class(const std::string& class_name) {
    auto it = av::g_schemas.find(class_name);
    return it == av::g_schemas.end() ? nullptr : &it->second;
}

// The schema class that owns a Component *slot* (field number), e.g. 120 ->
// ShapeComponent, 158 -> MonsterEntityComponent. Several classes can share a
// slot, in which case the first table row wins — that matches the .proto, where
// one extension number has exactly one message type.
std::string schema_class_for_payload_field(uint32_t field_number) {
    if (const ComponentType* t = component_type_by_field(field_number)) return t->class_name;
    // Not in the recovered table (a newer class): fall back on the Component
    // schema itself, which names every payload slot the game knows about.
    auto it = av::g_schemas.find("Component");
    if (it != av::g_schemas.end()) {
        auto f = it->second.fields.find((field_number << 3) | 2);
        if (f != it->second.fields.end() && !f->second.class_name.empty())
            return f->second.class_name;
    }
    return std::string();
}

void RuntimeScene::decode_component(const av::SceneComponent& src, RuntimeObject& owner) {
    RuntimeComponent component;
    component.class_name = src.type_name;
    component.raw = src.raw_data;
    component.type = component_type_by_short(src.type_name);

    // Pass 1 — split the Component message into its ClassName, Identifier and
    // every payload submessage. The payloads are a *set*: a CollisionShape
    // writes slots 120 and 121, a MonsterEntity 152 and 158. The old code kept
    // only the one slot named below, which threw away the other half of every
    // such component (monster movement, collision friction, spell data …).
    std::vector<uint32_t> payload_fields;
    std::vector<std::pair<std::string, std::string>> handler_fields;
    try {
        proto::Reader reader(src.raw_data);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN) {
                if (field.field_number == 2) component.identifier = static_cast<int32_t>(field.varint_val);
                else if (field.field_number == 4) component.parent_identifier = static_cast<int32_t>(field.varint_val);
                continue;
            }
            if (field.field_number == 1) {
                component.class_name = field.bytes_val;
            } else if (field.field_number == 3) {
                component.label = field.bytes_val;
            } else if (field.field_number >= kFirstPayloadField) {
                RuntimePayload payload;
                payload.field = static_cast<uint32_t>(field.field_number);
                payload.class_name = schema_class_for_payload_field(payload.field);
                payload.type = component_type_by_field(payload.field);
                payload.bytes = field.bytes_val;
                component.payloads.push_back(std::move(payload));
                payload_fields.push_back(static_cast<uint32_t>(field.field_number));
            }
        }
    } catch (...) {
        warnings_.push_back("object '" + owner.identifier + "': malformed component bytes");
    }

    // The ClassName identifies the component even when its own slot was not
    // written (Transform, TransformController and friends have no slot at all),
    // so it decides the primary payload rather than the slot order in the file.
    if (const ComponentType* by_name = component_type_by_short(component.class_name))
        component.type = by_name;

    const uint32_t own_field = (component.type && component.type->payload_tag)
                                   ? component.type->payload_tag >> 3
                                   : (payload_fields.empty() ? 0 : payload_fields.front());
    for (auto& payload : component.payloads) {
        if (payload.field == own_field) { payload.is_primary = true; break; }
    }
    if (!component.payloads.empty() && !component.payload_for(own_field)) {
        // ClassName's own slot is absent (usually an empty dimension/spell part):
        // the first slot present is what the file actually carries.
        component.payloads.front().is_primary = true;
    }
    if (const RuntimePayload* primary = component.primary_payload())
        component.payload = primary->bytes;

    // Pass 2 — decode every payload against its own schema, so a field's name is
    // always resolved in the right message. Unknown fields stay in `fields` with
    // an empty name and are never dropped.
    for (const auto& payload : component.payloads) {
        const av::SchemaClass* schema = schema_for_class(payload.class_name);
        if (payload.bytes.empty()) continue;
        try {
            proto::Reader reader(payload.bytes);
            proto::Field field;
            while (reader.read_field(field)) {
                RuntimeField decoded;
                decoded.tag = (field.field_number << 3) | static_cast<uint32_t>(field.wire_type);
                decoded.wire = field.wire_type;
                decoded.varint_value = field.varint_val;
                decoded.double_value = field.double_val;
                decoded.float_value = field.float_val;
                decoded.bytes_value = field.bytes_val;
                decoded.payload_field = payload.field;
                decoded.payload_class = payload.class_name;
                if (schema) {
                    auto it = schema->fields.find(decoded.tag);
                    if (it != schema->fields.end()) {
                        decoded.name = it->second.name;
                        // Handler fields carry a nested Program message; remember
                        // it so pass 3 can collect the script.
                        if (it->second.class_name == "Program")
                            handler_fields.emplace_back(decoded.name, decoded.bytes_value);
                    }
                }
                if (is_reference_field(decoded.name, field.wire_type)) {
                    ComponentRef ref;
                    ref.tag = decoded.tag;
                    ref.field_name = decoded.name;
                    ref.target = static_cast<int32_t>(field.varint_val);
                    component.refs.push_back(ref);
                }
                component.fields.push_back(std::move(decoded));
            }
        } catch (...) {
            warnings_.push_back("object '" + owner.identifier + "': malformed " +
                                payload.class_name + " payload");
        }
    }

    // Pass 3 — collect embedded Lua: ProgramComponent's `Program`, plus every
    // handler field (OnKill / OnHurt / OnActivate / OnCollide / OnLoad / OnCast
    // …) found in ANY payload, which is what makes a MonsterEntity's OnKill
    // discoverable even though it lives in the Entity-component slot.
    for (const auto& handler : handler_fields)
        collect_program(owner, component, handler.first, handler.second);

    // The object's own OnLoad program (SceneObject field 10) is handled by the
    // loader, not the component decode; nothing to do here.

    if (!component.type)
        warnings_.push_back("object '" + owner.identifier + "': unknown component class '" +
                            component.class_name + "' (newer game version?)");

    owner.components.push_back(std::move(component));
}

void RuntimeScene::collect_program(RuntimeObject& owner, const RuntimeComponent& comp,
                                   const std::string& field_name, const std::string& program_bytes) {
    RuntimeProgram program;
    program.owner_identifier = comp.identifier;
    program.owner_class = comp.class_name;
    program.field_name = field_name;
    if (!split_program(program_bytes, &program.source, &program.bytecode)) return;
    if (program.source.empty() && program.bytecode.empty()) return;
    program.keep_active = looks_like_ai_loop(program.source);
    owner.programs.push_back(std::move(program));
}

void RuntimeScene::resolve_references(RuntimeObject& object) {
    for (auto& component : object.components) {
        for (auto& ref : component.refs) {
            // A Reference field written as 0 means "unset", not "points at slot 0":
            // the serialiser emits every scalar field, including zero, so treating 0
            // as a broken edge reports a dangling reference on almost every object
            // in the game (ParentEmitterId 0, ModelBindingId 0, AnimationId 0 …).
            if (ref.target == 0) { ref.resolved = false; continue; }
            ref.resolved = object.find(ref.target) != nullptr;
            if (!ref.resolved)
                warnings_.push_back("object '" + object.identifier + "': " + component.class_name +
                                    "." + ref.field_name + " -> " + std::to_string(ref.target) +
                                    " is dangling");
        }
    }
}

void RuntimeScene::tick(RuntimeObject& object, float dt) {
    if (!object.activated) return;

    // The recovered behaviours (behaviour.cpp) own every per-component clock and
    // transition; this function only drives them and republishes render state.
    // Scripts run in a separate pass (RuntimeScene::update) so this loop can
    // never be re-entered by a spawn.
    behaviour_tick(*this, object, dt);

    ObjectRenderState* state = nullptr;
    for (auto& s : render_)
        if (s.identifier == object.identifier) { state = &s; break; }
    if (!state) return;
    state->pos[0] = object.pos[0];
    state->pos[1] = object.pos[1];
    state->pos[2] = object.pos[2];
    state->rot_y = object.rot[1];
    state->scale = object.scale[0] * object.template_scaling;
    state->hidden = object.hidden;
    state->facing = object.behaviour.facing;
    state->on_ground = object.behaviour.physics.on_ground;
    state->anim_name = object.behaviour.anim.current;
    if (object.behaviour.anim.running) state->anim_time = object.behaviour.anim.time;
    state->emitter_time = object.behaviour.emitter.time;
    state->water_time = object.behaviour.water_time;
    state->light_radius = object.behaviour.light_radius;
    state->light_intensity = object.behaviour.light_intensity;
    for (int i = 0; i < 3; ++i) state->light_color[i] = object.behaviour.light_color[i];
    for (int i = 0; i < 3; ++i) state->light_offset[i] = object.behaviour.light_offset[i];

    // SimpleGlow pulse: 1 + PulseAmount * sin(2π t / PulseTime) at PulseTime > 0.
    const float amount = object.behaviour.glow_pulse_amount;
    const float period = object.behaviour.glow_pulse_time;
    state->light_pulse = (amount != 0.0f && period > 0.0f)
        ? 1.0f + amount * std::sin(kTwoPi * object.behaviour.emitter.time / period)
        : 1.0f;
}

void RuntimeScene::update(float dt) {
    clock_ += dt;

    // The pass order is Caver::Scene::Update (0x4F81AC), whose first act after the
    // frame counter is `ProgramState::Update(root, dt)` — the script pass — and
    // whose object pass (`SceneObject::updateSpeedMultiplier` + `SceneObject::
    // Update`) follows the camera/grid work. So *scripts run before components*,
    // which means a resumed program observes the component state its owner had at
    // the end of the previous frame. This port used to run components first, on
    // the assumption that the scene ticks and then the scripts react; the binary
    // does the opposite. See caver/frame_runner.h for the full recovered order.
    if (program_host_) {
        program_host_->sync_clock(clock_);
        // `count` is snapshotted so the collection cannot be mutated mid-iteration.
        const size_t count = objects_.size();
        for (size_t i = 0; i < count; ++i) {
            RuntimeObject* object = this->object(objects_[i].identifier);
            if (object) program_host_->update_object(*this, *object, dt);
        }
    }

    // The object pass — every C++ component behaviour (behaviour.cpp).
    for (size_t i = 0; i < objects_.size(); ++i)
        tick(objects_[i], dt);

    // The event pass. The engine delivers queued events after the object pass has
    // finished, so a handler never runs midway through an object iteration.
    if (program_host_) program_host_->pump(*this);
}

RuntimeObject* RuntimeScene::object(const std::string& identifier) {
    auto it = by_identifier_.find(identifier);
    return it == by_identifier_.end() ? nullptr : &objects_[it->second];
}

const RuntimeObject* RuntimeScene::object(const std::string& identifier) const {
    return const_cast<RuntimeScene*>(this)->object(identifier);
}

size_t RuntimeScene::component_count() const {
    size_t total = 0;
    for (const auto& object : objects_) total += object.components.size();
    return total;
}

std::vector<std::string> RuntimeScene::unrecovered_classes() const {
    std::unordered_set<std::string> missing;
    for (const auto& object : objects_) {
        for (const auto& component : object.components) {
            if (!component.type) { missing.insert(component.class_name); continue; }
            if (!has(component.type->stages, Stage::Updated))
                missing.insert(component.type->class_name);
        }
    }
    std::vector<std::string> out(missing.begin(), missing.end());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace caver
