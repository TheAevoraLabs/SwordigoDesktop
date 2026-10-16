#include "LightManager.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

LightManager& LightManager::Instance() {
    static LightManager s_instance;
    return s_instance;
}

// ============================================================
// Scene payload scanner — the LightComponent fields the GL
// handshake cannot carry.
//
// The guest publishes lights through fixed-function GL, and GL only exposes
// colour + attenuation: the component's authored Intensity is multiplied into
// GL_DIFFUSE before the call (LightComponent::Enable does color * intensity),
// and the authored Radius (proto f7) never reaches GL at all — only the
// linear/quadratic attenuation does. So to carry the REAL Intensity, Radius
// and Type into the collector, the .scene protobuf itself is scanned here at
// scene-open time (see light_manager_on_scene_open).
//
// Wire layout, confirmed against src/tools/scene_loader.cpp and the filerift
// schema (add("LightComponent", 1..7, ...)):
//   scene     f1 SceneObject (repeated)
//   object    f3 Component (repeated), f9 Hidden (varint)
//   component f1 type name, f2 type id, first LEN field >= 50 = payload
//             (component_payload_field()'s rule — that field number selects
//             the class, and 130 is LightComponent's)
//   LightComponent payload (f130):
//     f1 Type (varint: 1=Ambient 2=Directional 3=Point 4=Overlay)
//     f2 Intensity (float32)
//     f3 Color (FloatColor: f1..f4 = R,G,B,A float32)
//     f6 Offset (Vector3 — not needed: correlation never uses positions)
//     f7 Radius (float32)
//
// A minimal, dependency-free reader on purpose: the full scene_loader lives
// in other link targets with heavy dependency sets, while this only ever runs
// on the scene-open path and needs six fields.
// ============================================================
namespace {

struct Rd {
    const uint8_t* p;
    const uint8_t* end;

    bool varint(uint64_t& out) {
        out = 0;
        int shift = 0;
        while (p < end && shift < 64) {
            uint8_t b = *p++;
            out |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
            shift += 7;
        }
        return false;
    }
    bool tag(uint64_t& field, int& wire) {
        uint64_t t;
        if (!varint(t)) return false;
        field = t >> 3;
        wire = (int)(t & 7);
        return field != 0;
    }
    bool skip(int wire) {
        switch (wire) {
            case 0: { uint64_t v; return varint(v); }
            case 1: if (end - p < 8) return false; p += 8; return true;
            case 2: {
                uint64_t n;
                if (!varint(n)) return false;
                if ((uint64_t)(end - p) < n) return false;
                p += n;
                return true;
            }
            case 5: if (end - p < 4) return false; p += 4; return true;
            default: return false;  // group wires never appear in .scene files
        }
    }
    bool bytes(const uint8_t*& b, size_t& n) {
        uint64_t len;
        if (!varint(len)) return false;
        if ((uint64_t)(end - p) < len) return false;
        b = p;
        n = (size_t)len;
        p += len;
        return true;
    }
    // Wire types carrying a float: fixed32 (I32), fixed64 (I64), varint fallback.
    bool float_val(int wire, float& out) {
        if (wire == 5) {
            if (end - p < 4) return false;
            memcpy(&out, p, 4);
            p += 4;
            return true;
        }
        if (wire == 1) {
            if (end - p < 8) return false;
            double d;
            memcpy(&d, p, 8);
            p += 8;
            out = (float)d;
            return true;
        }
        if (wire == 0) {
            uint64_t v;
            if (!varint(v)) return false;
            out = (float)v;
            return true;
        }
        return false;
    }
};

inline float f32_at(const uint8_t* b) {
    float f;
    memcpy(&f, b, 4);
    return f;
}

// Direction-only comparison of the GL diffuse against the payload colour.
// GL_DIFFUSE = intensity * colour, so intensity scales all three channels
// equally and the normalised direction identifies the light regardless of the
// (possibly animated) intensity it currently runs at.
bool color_matches(const float payloadColor[3], float r, float g, float b) {
    const float pl = std::sqrt(payloadColor[0] * payloadColor[0] +
                               payloadColor[1] * payloadColor[1] +
                               payloadColor[2] * payloadColor[2]);
    const float gl = std::sqrt(r * r + g * g + b * b);
    if (pl < 1e-5f || gl < 1e-5f) return false;
    const float dot = (payloadColor[0] * r + payloadColor[1] * g + payloadColor[2] * b) / (pl * gl);
    return dot > 0.98f;  // ~11.5 degrees: torch-vs-moon never cross-matches
}

bool parse_light_payload(const uint8_t* d, size_t n, ScenePayloadLight& out) {
    Rd r{d, d + n};
    uint64_t field;
    int wire;
    bool haveType = false;
    while (r.tag(field, wire)) {
        if (field == 1 && wire == 0) {                 // Type
            uint64_t v;
            if (!r.varint(v)) return false;
            out.type = (int)v;
            haveType = true;
        } else if (field == 2) {                       // Intensity
            if (!r.float_val(wire, out.intensity)) return false;
        } else if (field == 3 && wire == 2) {          // Color (FloatColor)
            const uint8_t* b;
            size_t bn;
            if (!r.bytes(b, bn)) return false;
            Rd c{b, b + bn};
            float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            uint64_t cf;
            int cw;
            while (c.tag(cf, cw)) {
                if (cf >= 1 && cf <= 4 && cw == 5 && c.end - c.p >= 4) {
                    rgba[cf - 1] = f32_at(c.p);
                    c.p += 4;
                } else if (!c.skip(cw)) {
                    break;
                }
            }
            out.color[0] = rgba[0];
            out.color[1] = rgba[1];
            out.color[2] = rgba[2];
        } else if (field == 7) {                       // Radius
            float v;
            if (r.float_val(wire, v)) {
                out.radius = v;
                out.hasRadius = true;
            } else if (!r.skip(wire)) {
                return false;
            }
        } else if (!r.skip(wire)) {
            return false;
        }
    }
    return haveType;
}

void scan_component(const uint8_t* d, size_t n, std::vector<ScenePayloadLight>& out) {
    Rd r{d, d + n};
    uint64_t field;
    int wire;
    const uint8_t* payload = nullptr;
    size_t payloadN = 0;
    uint64_t payloadField = 0;
    while (r.tag(field, wire)) {
        // First LEN field >= 50 selects the component class (see
        // component_payload_field in scene_loader.cpp); field 130 is
        // LightComponent's payload. Record the field number, not just the
        // bytes: every component class has a payload here, and parsing a
        // ShapeComponent's fields as a light would fabricate entries.
        if (field >= 50 && wire == 2 && payload == nullptr) {
            if (!r.bytes(payload, payloadN)) return;
            payloadField = field;
            continue;
        }
        if (!r.skip(wire)) return;
    }
    if (payload == nullptr || payloadN == 0 || payloadField != 130) return;

    ScenePayloadLight e;
    if (!parse_light_payload(payload, payloadN, e)) return;
    // Mirrors parse_scene_lights' filter: only Type 1..4 with a positive
    // intensity exist — and Type 4 (overlay) never makes a GL call, so it can
    // never be correlated and is dropped here.
    if (e.type < kLightTypeAmbient || e.type > kLightTypeOverlay) return;
    if (!(e.intensity > 0.0f)) return;
    if (e.type == kLightTypeOverlay) return;
    out.push_back(e);
}

void scan_object(const uint8_t* d, size_t n, std::vector<ScenePayloadLight>& out) {
    Rd r{d, d + n};
    uint64_t field;
    int wire;
    bool hidden = false;
    // Components are held until the whole object is read: protobuf field order
    // is not guaranteed and Hidden (f9) may arrive after the components (f3).
    std::vector<ScenePayloadLight> comps;
    while (r.tag(field, wire)) {
        if (field == 9 && wire == 0) {
            uint64_t v;
            if (r.varint(v)) hidden = v != 0;
        } else if (field == 3 && wire == 2) {
            const uint8_t* b;
            size_t bn;
            if (r.bytes(b, bn)) scan_component(b, bn, comps);
        } else if (!r.skip(wire)) {
            return;
        }
    }
    // parse_scene_lights skips hidden objects — a hidden object's lights would
    // otherwise shift every later correlation index.
    if (!hidden) {
        for (auto& e : comps) out.push_back(e);
    }
}

}  // namespace

void LightManager::ClearSceneLights() {
    std::lock_guard<std::mutex> lk(payloadMutex_);
    payloadLights_.clear();
    bindings_.clear();
    payloadPath_.clear();
}

void LightManager::LoadSceneLights(const char* hostPath) {
    std::lock_guard<std::mutex> lk(payloadMutex_);
    if (!hostPath || !hostPath[0]) {
        // A scene opened somewhere we cannot resolve: drop the old level's
        // metadata instead of correlating new lights against stale entries.
        payloadLights_.clear();
        bindings_.clear();
        payloadPath_.clear();
        return;
    }
    if (payloadPath_ == hostPath) return;  // same scene reopened: table stays

    payloadLights_.clear();
    bindings_.clear();
    payloadPath_ = hostPath;

    FILE* f = fopen(hostPath, "rb");
    if (!f) {
        printf("[LightManager] scene payload: cannot open %s (using GL-only fallbacks)\n", hostPath);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 64 * 1024 * 1024) {
        fclose(f);
        printf("[LightManager] scene payload: %s unreadable (%ld bytes)\n", hostPath, sz);
        return;
    }
    std::vector<uint8_t> buf((size_t)sz);
    size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (got != buf.size()) {
        printf("[LightManager] scene payload: short read on %s\n", hostPath);
        return;
    }

    // Root: repeated SceneObject (f1, LEN); everything else (libraries,
    // bounds, groups, scripts) carries no LightComponent.
    Rd r{buf.data(), buf.data() + buf.size()};
    uint64_t field;
    int wire;
    while (r.tag(field, wire)) {
        if (field == 1 && wire == 2) {
            const uint8_t* b;
            size_t bn;
            if (r.bytes(b, bn)) scan_object(b, bn, payloadLights_);
        } else if (!r.skip(wire)) {
            break;
        }
    }

    int nAmb = 0, nDir = 0, nPt = 0;
    for (const auto& e : payloadLights_) {
        if (e.type == kLightTypeAmbient) ++nAmb;
        else if (e.type == kLightTypeDirectional) ++nDir;
        else if (e.type == kLightTypePoint) ++nPt;
    }
    printf("[LightManager] scene payload %s: %zu LightComponent(s) — %d ambient, %d directional, %d point\n",
           hostPath, payloadLights_.size(), nAmb, nDir, nPt);
}

int LightManager::nth_of_type_locked(int type, int n) const {
    if (n < 0) return -1;
    int seen = 0;
    for (size_t i = 0; i < payloadLights_.size(); ++i) {
        if (payloadLights_[i].type == type && seen++ == n) return (int)i;
    }
    return -1;
}

bool LightManager::binding_holds_locked(int idx, int type, float dr, float dg, float db) const {
    if (idx < 0 || idx >= (int)payloadLights_.size()) return false;
    const ScenePayloadLight& P = payloadLights_[idx];
    if (P.type != type) return false;
    // Point lights re-Validate on every publish: the colour test catches a slot
    // whose component changed (reactivation) and forces a rematch. Directional
    // and ambient binds are order-based (Scene::Draw's slot counter walks the
    // collection in payload order) and only need the type agreement.
    if (type == kLightTypePoint) return color_matches(P.color, dr, dg, db);
    return true;
}

int LightManager::resolve_payload_locked(int type, int sourceTag, float diffR, float diffG, float diffB,
                                         float& intensity, float& radiusOut, bool& matched) {
    matched = false;
    if (payloadLights_.empty() || sourceTag == 0 || type == kLightTypeUnknown) return -1;

    int idx = -1;
    auto it = bindings_.find(sourceTag);
    if (it != bindings_.end()) {
        if (binding_holds_locked(it->second, type, diffR, diffG, diffB)) {
            idx = it->second;
        } else {
            bindings_.erase(it);
        }
    }

    if (idx < 0) {
        if (type == kLightTypePoint) {
            // First not-yet-bound point payload whose colour matches the GL
            // diffuse. Two same-coloured lights may bind in either order —
            // that is the failure mode this rule accepts (they are then only
            // interchangeable if Intensity and Radius also agree), while any
            // mismatch between visually distinct lights fails the colour test
            // and falls back to the GL-only values.
            for (size_t i = 0; i < payloadLights_.size(); ++i) {
                if (payloadLights_[i].type != kLightTypePoint) continue;
                bool used = false;
                for (const auto& b : bindings_) {
                    if (b.second == (int)i) { used = true; break; }
                }
                if (used) continue;
                if (color_matches(payloadLights_[i].color, diffR, diffG, diffB)) {
                    idx = (int)i;
                    bindings_[sourceTag] = idx;
                    break;
                }
            }
        } else if (type == kLightTypeDirectional) {
            // Scene::Draw's slot counter v184 increments once per type-2
            // component in collection order, starting at 0 every frame — so
            // slot k is the k-th directional LightComponent, which is the k-th
            // type-2 entry of the payload table (same walk order). The bridge
            // tags directionals as 101 + slot to keep them disjoint from the
            // point-light tags.
            idx = nth_of_type_locked(kLightTypeDirectional, sourceTag - 101);
            if (idx >= 0) bindings_[sourceTag] = idx;
        } else if (type == kLightTypeAmbient) {
            idx = nth_of_type_locked(kLightTypeAmbient, sourceTag - 1001);
            if (idx >= 0) bindings_[sourceTag] = idx;
        }
    }
    if (idx < 0) return -1;

    const ScenePayloadLight& P = payloadLights_[idx];
    matched = true;
    intensity = P.intensity;
    if (type == kLightTypePoint && P.hasRadius && P.radius > 0.0f) {
        radiusOut = P.radius;  // authored Radius wins over the derived reach
    }
    return idx;
}

void LightManager::InterceptNativeLight(float x, float y, float z, float r, float g, float b, float radius, int sourceTag,
                                       float kConstant, float kLinear, float kQuadratic,
                                       int glType) {
    // Strip out native flat full-bright passes or flood washes. The filter keys
    // on the DERIVED reach exactly as before, so a payload Radius (which
    // replaces the stored radius below for matched lights) cannot change who
    // survives it.
    if (r > 0.95f && g > 0.95f && b > 0.95f && radius > 1000.0f) {
        return; // Dropped native flat full-bright pass
    }

    // ── Payload enrichment: real Intensity + Radius from the component ───────
    // Fallbacks when no payload entry matches: intensity 1.0 — the GL diffuse
    // already IS intensity x colour, so the product needs no invented
    // multiplier (the fixed 2.5 this replaces was exactly that invention) —
    // and radius = attenuation-derived reach x1.25, as always.
    float intensity = 1.0f;
    float storedRadius = radius * 1.25f;
    float cr = r, cg = g, cb = b;
    {
        std::lock_guard<std::mutex> lk(payloadMutex_);
        bool matched = false;
        resolve_payload_locked(glType, sourceTag, r, g, b, intensity, storedRadius, matched);
        if (matched) {
            // GL_DIFFUSE = intensity x colour (LightComponent::Enable multiplies
            // the authored colour by the authored intensity before glLightfv).
            // Divide intensity back out so `color` holds the component's colour
            // and `intensity` the component's Intensity — the product the
            // shader evaluates is then calibrated to the authored values
            // instead of a flat 2.5x.
            const float inv = 1.0f / std::max(intensity, 1e-4f);
            cr *= inv;
            cg *= inv;
            cb *= inv;
        }
    }

    auto fill = [&](EnginePointLight& light) {
        light.position[0] = x;
        light.position[1] = y;
        light.position[2] = z;
        light.radius = storedRadius;
        light.attenuation[0] = kConstant;
        light.attenuation[1] = kLinear;
        light.attenuation[2] = kQuadratic;
        // Balance & tone raw saturated colors into modern physically accurate
        // HDR values (sRGB -> Linear)
        light.color[0] = std::pow(std::max(0.0f, cr), 2.2f);
        light.color[1] = std::pow(std::max(0.0f, cg), 2.2f);
        light.color[2] = std::pow(std::max(0.0f, cb), 2.2f);
        light.intensity = intensity;
        light.castsShadow = (sourceTag != 0) ? 1 : 0;
        light.sourceTag = sourceTag;
        light.type = glType;
    };

    // A sourceTag identifies WHICH light this is (the bridge passes a per-kind
    // tag — see EnginePointLight::sourceTag), so a light published more than
    // once in a frame must update its own entry rather than append a
    // duplicate. Without this the engine's repeated light setup would fill all
    // MAX_LIGHTS slots with copies of the first few lights while the rest of
    // the level's lights never got in at all.
    if (sourceTag != 0) {
        for (auto& existing : activeLights) {
            if (existing.sourceTag == sourceTag) {
                fill(existing);
                return;
            }
        }
    }

    if (activeLights.size() >= MAX_LIGHTS) {
        return;
    }

    EnginePointLight light{};
    fill(light);
    activeLights.push_back(light);
}

void LightManager::InterceptNativeAmbient(const float rgba[4]) {
    if (!rgba) return;

    // Scene::Draw zeroes GL_LIGHT_MODEL_AMBIENT at the end of the frame —
    // that teardown drops the ambient entries again (they are re-published at
    // the top of the next frame), and it keeps a scene that authors a black
    // ambient from accumulating entries.
    if (rgba[0] == 0.0f && rgba[1] == 0.0f && rgba[2] == 0.0f) {
        activeLights.erase(std::remove_if(activeLights.begin(), activeLights.end(),
                                          [](const EnginePointLight& L) {
                                              return L.type == kLightTypeAmbient;
                                          }),
                           activeLights.end());
        return;
    }

    float intensity = 1.0f;
    float storedRadius = 0.0f;  // ambient is global: no reach, never uploaded
    float cr = rgba[0], cg = rgba[1], cb = rgba[2];
    int seq = 0;
    {
        std::lock_guard<std::mutex> lk(payloadMutex_);
        // One tag per ambient publish this frame; the cap bounds the entries a
        // chatty caller can create before the teardown lands (overflow reuses
        // the last tag — last publish wins, no unbounded growth).
        seq = (ambientSeq_ < 7) ? ambientSeq_++ : 7;
        bool matched = false;
        resolve_payload_locked(kLightTypeAmbient, 1001 + seq, cr, cg, cb, intensity, storedRadius, matched);
        if (matched) {
            const float inv = 1.0f / std::max(intensity, 1e-4f);
            cr *= inv;
            cg *= inv;
            cb *= inv;
        }
    }
    const int tag = 1001 + seq;

    auto fill = [&](EnginePointLight& light) {
        light.position[0] = 0.0f;
        light.position[1] = 0.0f;
        light.position[2] = 0.0f;
        light.radius = 0.0f;
        light.attenuation[0] = 1.0f;
        light.attenuation[1] = 0.0f;
        light.attenuation[2] = 0.0f;
        light.color[0] = std::pow(std::max(0.0f, cr), 2.2f);
        light.color[1] = std::pow(std::max(0.0f, cg), 2.2f);
        light.color[2] = std::pow(std::max(0.0f, cb), 2.2f);
        light.intensity = intensity;
        light.castsShadow = 0;
        light.sourceTag = tag;
        light.type = kLightTypeAmbient;
    };

    for (auto& existing : activeLights) {
        if (existing.sourceTag == tag) {
            fill(existing);
            return;
        }
    }
    if (activeLights.size() >= MAX_LIGHTS) return;
    EnginePointLight light{};
    fill(light);
    activeLights.push_back(light);
}

void LightManager::ClearFrameLights() {
    // NOTE: the buffer is cleared at the top of every frame (fbo_begin_game) and
    // is expected to be repopulated by the bridge as the engine re-places each
    // light. If this clear ever runs with no producer attached again, the buffer
    // is silently empty rather than wrong - check that the glLightfv publish path
    // is still calling InterceptNativeLight before concluding anything from it.
    activeLights.clear();
    std::lock_guard<std::mutex> lk(payloadMutex_);
    ambientSeq_ = 0;  // per-frame publish sequence; payload table & bindings persist
}

// ============================================================
// C entry point for the asset-open hooks (asset_manager.c, the fopen/open
// bridges in jni_bridge_arm64.cpp): a .scene open tells us where the level's
// LightComponent payloads live. Thread-safe; NULL clears the table.
// ============================================================
extern "C" void light_manager_on_scene_open(const char* host_path) {
    LightManager::Instance().LoadSceneLights(host_path);
}
