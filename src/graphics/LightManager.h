#pragma once
#include <vector>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// ── LightComponent payload Type (proto field 1 of the LightComponent payload,
// the f130 nested message) ───────────────────────────────────────────────────
// 1=Ambient, 2=Directional, 3=Point, 4=Overlay. The guest's LightComponent
// carries this value and branches on it in LightComponent::Enable and
// Scene::Draw: an ambient light is published with glLightModelfv(
// GL_LIGHT_MODEL_AMBIENT), a directional light with glLightfv(GL_POSITION,
// w=0), a point light with glLightfv(GL_POSITION, w=1), and an overlay light
// never touches GL at all (it drives Caver::LightOverlay). The collector
// therefore recovers Type exactly from WHICH GL call arrived with which w, and
// can cross-check it against the parsed scene payload.
enum : int {
    kLightTypeUnknown   = 0,  // no GL signal yet (never published)
    kLightTypeAmbient   = 1,
    kLightTypeDirectional = 2,
    kLightTypePoint     = 3,
    kLightTypeOverlay   = 4,  // never reaches the collector (no GL call)
};

struct EnginePointLight {
    float position[3];   // EYE space: already through the modelview the
                         // fixed-function pipeline lights with (see the publish
                         // path in jni_bridge_arm64.cpp), so a consumer that
                         // needs screen space projects it with u_projection alone.
    float radius;        // reach of the light: the LightComponent's authored
                         // Radius (proto f7) when the payload entry could be
                         // matched, otherwise the distance derived from the
                         // engine's attenuation triple (0 = no distance term,
                         // i.e. a global light). World units, same space as
                         // position.
    // The engine's own attenuation triple, straight from the LightComponent that
    // placed the light: GL attenuates as 1/(kc + kl*d + kq*d^2) with d in world
    // units. Carried through so a consumer can evaluate the level's actual
    // falloff curve instead of inventing an inverse-square of its own.
    float attenuation[3];
    float color[3];      // linear (converted from the guest's sRGB values)
    float intensity;     // the LightComponent's authored Intensity (proto f2)
                         // when the payload entry was matched, else 1.0. The
                         // GL diffuse already contains intensity x colour, so
                         // a matched entry divides intensity back OUT of the
                         // colour channel: color = linear(diffuse / intensity),
                         // intensity = authored value — the product the shader
                         // evaluates stays the engine's authored product.
    int castsShadow;
    // Which light this is, so republishing it in the same frame updates its own
    // entry instead of appending a duplicate. 0 = unattributed.
    // Tag scheme (set by the bridge, disjoint per kind so a GL slot shared by a
    // point and a directional light cannot cross-update the two entries):
    //   point        1..8    = GL slot + 1
    //   directional  101..108 = GL slot + 1 + 100
    //   ambient      1001..   = per-frame ambient publish sequence
    int sourceTag = 0;
    // LightComponent payload Type (kLightType*). 0 = no GL signal interpreted;
    // consumers should treat 0 as "unknown, assume point".
    int type = kLightTypeUnknown;
};

// One LightComponent payload as parsed from the .scene file (see the scanner
// in LightManager.cpp), kept in scene registration order - objects in file
// order, components in field order - which is the same order the engine's
// LightComponent collection walks, and therefore the order every correlation
// rule below relies on.
struct ScenePayloadLight {
    int   type      = kLightTypePoint;
    float intensity = 1.0f;
    float color[3]  = {1.0f, 1.0f, 1.0f};
    float radius    = 0.0f;
    bool  hasRadius = false;  // proto f7 was present (absent => derived reach)
};

class LightManager {
public:
    static constexpr size_t MAX_LIGHTS = 32;
    std::vector<EnginePointLight> activeLights;

    static LightManager& Instance();

    // Called when the native Swordigo engine sets up or updates a light instance.
    // The live caller is the glLightfv publish path in jni_bridge_arm64.cpp,
    // which fires on GL_POSITION - the call Caver::LightComponent makes when it
    // places its light - and passes:
    //   radius   the reach DERIVED from the attenuation triple (used for the
    //            flood filter and as fallback when no payload entry matches),
    //   sourceTag see the scheme on EnginePointLight::sourceTag,
    //   glType   the Type inferred from the transformed GL_POSITION w
    //            (kLightTypeDirectional when w==0, kLightTypePoint otherwise).
    // When a scene payload table is loaded (LoadSceneLights), the entry is
    // matched against the component it came from and carries that component's
    // real Type, Intensity and Radius instead of the derived/fixed stand-ins.
    void InterceptNativeLight(float x, float y, float z, float r, float g, float b, float radius, int sourceTag = 0,
                              float kConstant = 1.0f, float kLinear = 0.0f, float kQuadratic = 0.0f,
                              int glType = kLightTypeUnknown);

    // Scene ambient (payload Type 1): Scene::Draw publishes it with
    // glLightModelfv(GL_LIGHT_MODEL_AMBIENT) and zeroes it again at the end of
    // the frame, so rgba all-zero is the engine's teardown and drops the
    // ambient entries. Ambient entries are collected with Type=kLightTypeAmbient
    // and a zero reach; consumers separate them by type rather than by
    // accidentally falling out of a radius filter.
    void InterceptNativeAmbient(const float rgba[4]);

    // Clears the frame's collected lights AND the ambient publish sequence.
    // Called at the top of every frame (fbo_begin_game). The scene payload
    // table and its bindings survive: they describe the loaded level, not the
    // frame.
    void ClearFrameLights();

    // Parse the LightComponent payloads (Type/Intensity/Color/Radius) out of a
    // host-resolved .scene file into the payload table used to enrich the GL
    // publishes. Passing NULL (or an unparseable file) clears the table, so a
    // scene we cannot read degrades to the GL-only fallbacks rather than
    // keeping the previous level's metadata. Re-parsing the same path is a
    // no-op. Thread-safe: the scene-open hooks run off the render thread.
    void LoadSceneLights(const char* hostPath);
    void ClearSceneLights();

    const std::vector<EnginePointLight>& GetActiveLights() const { return activeLights; }

private:
    // Resolve the payload entry for a publish (or -1), applying the per-kind
    // correlation rules; fills intensity/radius/colour-split from it.
    // Called with payloadMutex_ held.
    int  resolve_payload_locked(int type, int sourceTag, float diffR, float diffG, float diffB,
                               float& intensity, float& radiusOut, bool& matched);
    int  nth_of_type_locked(int type, int n) const;   // -1 when out of range
    bool binding_holds_locked(int idx, int type, float dr, float dg, float db) const;

    mutable std::mutex payloadMutex_;
    std::vector<ScenePayloadLight> payloadLights_;
    std::string payloadPath_;                     // last parsed path (idempotence)
    std::unordered_map<int, int> bindings_;       // sourceTag -> payload index
    int ambientSeq_ = 0;                          // per-frame ambient publish count
};

// C entry point for the asset-open hooks (asset_manager.c, and the fopen/open
// bridges in jni_bridge_arm64.cpp): a .scene open tells the collector where the
// level's LightComponent payloads live. Pass NULL to clear the table.
extern "C" void light_manager_on_scene_open(const char* host_path);
