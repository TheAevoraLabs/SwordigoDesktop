// ============================================================================
// quality_presets.h — Swordfare Graphics Quality Presets
//
// FIXES:
//   - Inverted quality (SW+ High looked worse than Low)
//   - Normalized settings across all presets
//   - Proper shadow, bloom, and lighting quality scaling
//   - Adaptive performance based on target framerate
// ============================================================================

#pragma once

namespace ruby::render {

/// Graphics quality preset levels.
enum class QualityPreset {
    Low,      // Mobile-friendly: minimal effects, fast
    Medium,   // Balanced: core effects, good performance
    High,     // Desktop: all effects, best quality
    Ultra,    // Future: advanced effects (reserved)
};

/// Per-preset graphics configuration.
struct QualityConfig {
    // ── Shadows ───────────────────────────────────────────────────────────────
    bool   shadow_enabled;
    float  shadow_intensity;      // 0.0 = no shadow, 1.0 = full shadow
    float  shadow_softness;       // PCF kernel size
    int    shadow_quality;        // 4, 8, 16 taps
    
    // ── Bloom & Glow ──────────────────────────────────────────────────────────
    bool   bloom_enabled;
    float  bloom_intensity;       // Glow strength multiplier
    int    bloom_quality;         // 1, 2, 3 blur passes
    bool   emitter_keyed;         // Use authored emitter palette vs luminance
    
    // ── Ambient Occlusion ─────────────────────────────────────────────────────
    bool   ao_enabled;
    float  ao_strength;           // 0.0 = no AO, 1.0 = full AO
    int    ao_quality;            // 8, 16, 32 samples
    
    // ── Tone Mapping & Color Grading ──────────────────────────────────────────
    int    tonemap_mode;          // 0 = Reinhard, 1 = ACES
    float  exposure;              // Scene brightness multiplier
    float  saturation;            // 1.0 = normal, 1.15 = Swordigo default
    float  contrast;              // 1.0 = normal
    float  warmth;                // -1..+1 color temperature
    
    // ── Brightness Normalization ──────────────────────────────────────────────
    float  brightness_lift;       // Shadow minimum brightness (0..1)
    float  highlight_compress;    // Highlight compression (0..1)
    
    // ── Dynamic Lights ────────────────────────────────────────────────────────
    int    max_dynamic_lights;    // 4, 8, 16
    bool   light_halos_enabled;   // Per-pixel light glow
    
    // ── Particles & Effects ───────────────────────────────────────────────────
    int    particle_quality;      // 0 = low, 1 = medium, 2 = high
    bool   particle_lighting;     // Per-particle lighting
    bool   particle_shadows;      // Particles cast shadows
    
    // ── Performance Targets ───────────────────────────────────────────────────
    int    target_fps;            // 30, 60, 120
    float  resolution_scale;      // 0.5 = 50%, 1.0 = 100%, 2.0 = 200%
};

/// Get the configuration for a given quality preset.
inline QualityConfig get_quality_config(QualityPreset preset) {
    QualityConfig cfg;
    
    switch (preset) {
        case QualityPreset::Low:
            // ── Mobile-friendly, minimal effects ──────────────────────────────
            cfg.shadow_enabled = false;
            cfg.shadow_intensity = 0.0f;
            cfg.shadow_softness = 0.5f;
            cfg.shadow_quality = 4;
            
            cfg.bloom_enabled = false;
            cfg.bloom_intensity = 0.0f;
            cfg.bloom_quality = 1;
            cfg.emitter_keyed = false;
            
            cfg.ao_enabled = false;
            cfg.ao_strength = 0.0f;
            cfg.ao_quality = 8;
            
            cfg.tonemap_mode = 0;  // Reinhard (cheaper)
            cfg.exposure = 1.0f;
            cfg.saturation = 1.0f;
            cfg.contrast = 1.0f;
            cfg.warmth = 0.0f;
            
            cfg.brightness_lift = 0.1f;
            cfg.highlight_compress = 1.0f;
            
            cfg.max_dynamic_lights = 4;
            cfg.light_halos_enabled = false;
            
            cfg.particle_quality = 0;
            cfg.particle_lighting = false;
            cfg.particle_shadows = false;
            
            cfg.target_fps = 30;
            cfg.resolution_scale = 1.0f;
            break;
            
        case QualityPreset::Medium:
            // ── Balanced: core effects, good performance ──────────────────────
            cfg.shadow_enabled = true;
            cfg.shadow_intensity = 0.4f;
            cfg.shadow_softness = 1.0f;
            cfg.shadow_quality = 8;
            
            cfg.bloom_enabled = true;
            cfg.bloom_intensity = 1.0f;
            cfg.bloom_quality = 2;
            cfg.emitter_keyed = true;
            
            cfg.ao_enabled = true;
            cfg.ao_strength = 0.5f;
            cfg.ao_quality = 16;
            
            cfg.tonemap_mode = 0;  // Reinhard
            cfg.exposure = 1.1f;
            cfg.saturation = 1.15f;  // Swordigo default
            cfg.contrast = 1.05f;
            cfg.warmth = 0.2f;
            
            cfg.brightness_lift = 0.15f;
            cfg.highlight_compress = 0.95f;
            
            cfg.max_dynamic_lights = 8;
            cfg.light_halos_enabled = true;
            
            cfg.particle_quality = 1;
            cfg.particle_lighting = true;
            cfg.particle_shadows = false;
            
            cfg.target_fps = 60;
            cfg.resolution_scale = 1.0f;
            break;
            
        case QualityPreset::High:
            // ── Desktop: all effects, best quality ────────────────────────────
            cfg.shadow_enabled = true;
            cfg.shadow_intensity = 0.6f;
            cfg.shadow_softness = 1.5f;
            cfg.shadow_quality = 16;
            
            cfg.bloom_enabled = true;
            cfg.bloom_intensity = 1.3f;
            cfg.bloom_quality = 3;
            cfg.emitter_keyed = true;
            
            cfg.ao_enabled = true;
            cfg.ao_strength = 0.8f;
            cfg.ao_quality = 32;
            
            cfg.tonemap_mode = 1;  // ACES (better quality)
            cfg.exposure = 1.15f;
            cfg.saturation = 1.20f;
            cfg.contrast = 1.10f;
            cfg.warmth = 0.3f;
            
            cfg.brightness_lift = 0.20f;
            cfg.highlight_compress = 0.90f;
            
            cfg.max_dynamic_lights = 16;
            cfg.light_halos_enabled = true;
            
            cfg.particle_quality = 2;
            cfg.particle_lighting = true;
            cfg.particle_shadows = true;
            
            cfg.target_fps = 60;
            cfg.resolution_scale = 1.0f;
            break;
            
        case QualityPreset::Ultra:
            // ── Future: advanced effects (reserved) ───────────────────────────
            cfg.shadow_enabled = true;
            cfg.shadow_intensity = 0.8f;
            cfg.shadow_softness = 2.0f;
            cfg.shadow_quality = 32;
            
            cfg.bloom_enabled = true;
            cfg.bloom_intensity = 1.5f;
            cfg.bloom_quality = 4;
            cfg.emitter_keyed = true;
            
            cfg.ao_enabled = true;
            cfg.ao_strength = 1.0f;
            cfg.ao_quality = 64;
            
            cfg.tonemap_mode = 1;  // ACES
            cfg.exposure = 1.20f;
            cfg.saturation = 1.25f;
            cfg.contrast = 1.15f;
            cfg.warmth = 0.4f;
            
            cfg.brightness_lift = 0.25f;
            cfg.highlight_compress = 0.85f;
            
            cfg.max_dynamic_lights = 32;
            cfg.light_halos_enabled = true;
            
            cfg.particle_quality = 2;
            cfg.particle_lighting = true;
            cfg.particle_shadows = true;
            
            cfg.target_fps = 120;
            cfg.resolution_scale = 1.0f;
            break;
    }
    
    return cfg;
}

} // namespace ruby::render
