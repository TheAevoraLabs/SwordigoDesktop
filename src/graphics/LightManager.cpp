#include "LightManager.h"
#include <cmath>
#include <algorithm>

LightManager& LightManager::Instance() {
    static LightManager s_instance;
    return s_instance;
}

void LightManager::InterceptNativeLight(float x, float y, float z, float r, float g, float b, float radius, int sourceTag,
                                       float kConstant, float kLinear, float kQuadratic) {
    // Strip out native flat full-bright passes or flood washes
    if (r > 0.95f && g > 0.95f && b > 0.95f && radius > 1000.0f) {
        return; // Dropped native flat full-bright pass
    }

    // A sourceTag identifies WHICH light this is (the bridge passes the GL light
    // slot + 1), so a light that is published more than once in a frame must
    // update its own entry rather than append a duplicate. Without this the
    // engine's repeated light setup - several calls per light per frame - would
    // fill all MAX_LIGHTS slots with copies of the first few lights while the
    // rest of the level's lights never got in at all.
    if (sourceTag != 0) {
        for (auto& existing : activeLights) {
            if (existing.sourceTag == sourceTag) {
                existing.position[0] = x;
                existing.position[1] = y;
                existing.position[2] = z;
                existing.radius = radius * 1.25f;
                existing.attenuation[0] = kConstant;
                existing.attenuation[1] = kLinear;
                existing.attenuation[2] = kQuadratic;
                existing.color[0] = std::pow(std::max(0.0f, r), 2.2f);
                existing.color[1] = std::pow(std::max(0.0f, g), 2.2f);
                existing.color[2] = std::pow(std::max(0.0f, b), 2.2f);
                return;
            }
        }
    }

    if (activeLights.size() >= MAX_LIGHTS) {
        return;
    }

    EnginePointLight light;
    light.position[0] = x;
    light.position[1] = y;
    light.position[2] = z;
    light.radius = radius * 1.25f; // Enhanced modern attenuation falloff
    light.attenuation[0] = kConstant;
    light.attenuation[1] = kLinear;
    light.attenuation[2] = kQuadratic;

    // Balance & tone raw saturated colors into modern physically accurate HDR values (sRGB -> Linear)
    light.color[0] = std::pow(std::max(0.0f, r), 2.2f);
    light.color[1] = std::pow(std::max(0.0f, g), 2.2f);
    light.color[2] = std::pow(std::max(0.0f, b), 2.2f);
    light.intensity = 2.5f;
    light.castsShadow = (sourceTag != 0) ? 1 : 0;
    light.sourceTag = sourceTag;

    activeLights.push_back(light);
}

void LightManager::ClearFrameLights() {
    // NOTE: the buffer is cleared at the top of every frame (fbo_begin_game) and
    // is expected to be repopulated by the bridge as the engine re-places each
    // light. If this clear ever runs with no producer attached again, the buffer
    // is silently empty rather than wrong - check that the glLightfv publish path
    // is still calling InterceptNativeLight before concluding anything from it.
    activeLights.clear();
}
