// render_passes.cpp — Caver::Scene::Draw (0x4F905C), transcribed.

#include "ruby/caver/render_passes.h"

#include <algorithm>

namespace caver {
namespace render {

const std::vector<Pass>& pass_order() {
    // Exactly the call sequence in Scene::Draw, top to bottom.
    static const std::vector<Pass> kOrder = {
        Pass::Setup,
        Pass::Lights,
        Pass::LightOverlayPrepare,
        Pass::OpaqueGeometry,
        Pass::TransparentGeometry,
        Pass::Water,
        Pass::ShadowVolumes,
        Pass::LightOverlayDraw,
        Pass::ModelsSecondPass,
        Pass::Background,
        Pass::ShadowSprites,
        Pass::WeaponTrails,
        Pass::Glows,
        Pass::Particles,
        Pass::Sprites,
        Pass::HealthBars,
        Pass::MagicExplosions,
        Pass::DebugDraw,
        Pass::Overlays,
    };
    return kOrder;
}

const char* pass_name(Pass pass) {
    switch (pass) {
        case Pass::Setup:               return "setup";
        case Pass::Lights:              return "lights";
        case Pass::LightOverlayPrepare: return "light-overlay-prepare";
        case Pass::OpaqueGeometry:      return "opaque-geometry";
        case Pass::TransparentGeometry: return "transparent-geometry";
        case Pass::Water:               return "water";
        case Pass::ShadowVolumes:       return "shadow-volumes";
        case Pass::LightOverlayDraw:    return "light-overlay-draw";
        case Pass::ModelsSecondPass:    return "models-second-pass";
        case Pass::Background:          return "background";
        case Pass::ShadowSprites:       return "shadow-sprites";
        case Pass::WeaponTrails:        return "weapon-trails";
        case Pass::Glows:               return "glows";
        case Pass::Particles:           return "particles";
        case Pass::Sprites:             return "sprites";
        case Pass::HealthBars:          return "health-bars";
        case Pass::MagicExplosions:     return "magic-explosions";
        case Pass::DebugDraw:           return "debug-draw";
        case Pass::Overlays:            return "overlays";
    }
    return "unknown";
}

int pass_index(Pass pass) {
    const std::vector<Pass>& order = pass_order();
    const auto it = std::find(order.begin(), order.end(), pass);
    return it == order.end() ? -1 : static_cast<int>(it - order.begin());
}

bool draws_before(Pass a, Pass b) {
    const int ia = pass_index(a);
    const int ib = pass_index(b);
    return ia >= 0 && ib >= 0 && ia < ib;
}

// DrawGroundMeshes 0x4F8CF4:
//   if ( (((*((_BYTE *)*v11 + 184) != 0) ^ a4) & 1) == 0 ) { ... draw ... }
// where a4 is the pass argument. So the component's byte must *equal* the pass
// index: byte 0 draws in pass 0 only, byte != 0 in pass 1 only.
bool ground_mesh_in_pass(int selector_byte, int pass) {
    const bool selector_set = selector_byte != 0;
    const bool pass_wants_set = pass != 0;
    return selector_set == pass_wants_set;
}

bool uses_stencil_shadows(bool has_directional_light, bool context_has_stencil) {
    return has_directional_light && context_has_stencil;
}

bool debug_draw_enabled(bool scene_debug_flag) { return scene_debug_flag; }

bool overlays_enabled(bool scene_overlays_disabled) { return !scene_overlays_disabled; }

FloatColor scene_tint(bool scene_has_color, float r, float g, float b, float a) {
    if (!scene_has_color) return FloatColor{1.0f, 1.0f, 1.0f, 1.0f};
    return FloatColor{r, g, b, a};
}

PassState opaque_pass_state() {
    // SetBlendingEnabled(0), lighting on, depth write left enabled.
    return PassState{false, true, true};
}

PassState transparent_pass_state() {
    // SetBlendingEnabled(1), SetDefaultBlendFunc(), SetDepthWriteEnabled(0).
    return PassState{true, false, true};
}

} // namespace render
} // namespace caver
