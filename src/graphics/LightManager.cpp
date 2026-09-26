#include "LightManager.h"
#include <cmath>
#include <algorithm>

LightManager& LightManager::Instance() {
    static LightManager s_instance;
    return s_instance;
}

void LightManager::InterceptNativeLight(float x, float y, float z, float r, float g, float b, float radius, int sourceTag) {
    // Strip out native flat full-bright passes or flood washes
    if (r > 0.95f && g > 0.95f && b > 0.95f && radius > 1000.0f) {
        return; // Dropped native flat full-bright pass
    }

    if (activeLights.size() >= MAX_LIGHTS) {
        return;
    }

    EnginePointLight light;
    light.position[0] = x;
    light.position[1] = y;
    light.position[2] = z;
    light.radius = radius * 1.25f; // Enhanced modern attenuation falloff

    // Balance & tone raw saturated colors into modern physically accurate HDR values (sRGB -> Linear)
    light.color[0] = std::pow(std::max(0.0f, r), 2.2f);
    light.color[1] = std::pow(std::max(0.0f, g), 2.2f);
    light.color[2] = std::pow(std::max(0.0f, b), 2.2f);
    light.intensity = 2.5f;
    light.castsShadow = (sourceTag != 0) ? 1 : 0;

    activeLights.push_back(light);
}

void LightManager::ClearFrameLights() {
    activeLights.clear();
}
