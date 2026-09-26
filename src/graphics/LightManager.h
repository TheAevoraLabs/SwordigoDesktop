#pragma once
#include <vector>
#include <cstdint>

struct EnginePointLight {
    float position[3];
    float radius;
    float color[3];
    float intensity;
    int castsShadow;
};

class LightManager {
public:
    static constexpr size_t MAX_LIGHTS = 32;
    std::vector<EnginePointLight> activeLights;

    static LightManager& Instance();

    // Called when native Swordigo engine sets up or updates a light instance
    void InterceptNativeLight(float x, float y, float z, float r, float g, float b, float radius, int sourceTag = 0);

    void ClearFrameLights();

    const std::vector<EnginePointLight>& GetActiveLights() const { return activeLights; }
};
