#pragma once
#include <vector>
#include <cstdint>

struct EnginePointLight {
    float position[3];   // EYE space: already through the modelview the
                         // fixed-function pipeline lights with (see the publish
                         // path in jni_bridge_arm64.cpp), so a consumer that
                         // needs screen space projects it with u_projection alone.
    float radius;        // reach derived from the engine's own attenuation triple;
                         // 0 means the light had no distance term (a global one)
    // The engine's own attenuation triple, straight from the LightComponent that
    // placed the light: GL attenuates as 1/(kc + kl*d + kq*d^2) with d in world
    // units. Carried through so a consumer can evaluate the level's actual
    // falloff curve instead of inventing an inverse-square of its own.
    float attenuation[3];
    float color[3];      // linear (converted from the guest's sRGB values)
    float intensity;
    int castsShadow;
    // Which light this is, so republishing it in the same frame updates its own
    // entry instead of appending a duplicate. 0 = unattributed.
    int sourceTag = 0;
};

class LightManager {
public:
    static constexpr size_t MAX_LIGHTS = 32;
    std::vector<EnginePointLight> activeLights;

    static LightManager& Instance();

    // Called when the native Swordigo engine sets up or updates a light instance.
    // The live caller is the glLightfv publish path in jni_bridge_arm64.cpp, which
    // fires on GL_POSITION - the call Caver::LightComponent makes when it places
    // its light - and passes the GL light slot + 1 as sourceTag.
    void InterceptNativeLight(float x, float y, float z, float r, float g, float b, float radius, int sourceTag = 0,
                              float kConstant = 1.0f, float kLinear = 0.0f, float kQuadratic = 0.0f);

    void ClearFrameLights();

    const std::vector<EnginePointLight>& GetActiveLights() const { return activeLights; }
};
