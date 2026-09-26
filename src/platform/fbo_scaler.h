// ============================================================
//  SwordigoDesktop — FBO + GLSL Rendering Pipeline
//  Game renders to FBO, then through optional post-processing
//  passes (SSAO, God Rays, Color FX), then upscaled to window.
// ============================================================

#pragma once
#include <cstdint>

// Upscale filter modes
enum class FBOScale : int {
    SHARP_BILINEAR = 0,
    NEAREST        = 1,
    CRT_SCANLINE   = 2,
    FSR            = 3,   // AMD FidelityFX Super Resolution 1.0 (EASU)
};

// Graphics Pipeline Tiers (Hardware OpenGL 4.30+ rendering features)
enum class GraphicsTier : int {
    LOW = 0,
    MEDIUM,
    HIGH,
    SW_PLUS,
    SW_PLUS_HIGH,
    COUNT
};

// Stylistic Post-Effects Filters (Cosmetic backbuffer overlays, independent of graphics tier)
enum class PostEffectFilter : int {
    NONE = 0,
    RETRO_TV,       // CRT scanlines, barrel curvature, phosphor mask
    OUTLINE_MILD,   // Comic-book thin Sobel depth/normal edge contours
    OUTLINE_HEAVY,  // Anime thick dilated ink silhouettes
    VHS_GLITCH,     // Tape tracking jitter, chromatic drift, noise lines
    VIGNETTE_GRAIN, // Subtle photographic vignette & organic film grain
    COUNT
};

// Post-processing and engine rendering pipeline state
struct PostFXState {
    bool enabled = true;
    
    // Decoupled classifications
    GraphicsTier     graphics_tier = GraphicsTier::SW_PLUS;
    PostEffectFilter postfx_filter = PostEffectFilter::NONE;
    float            postfx_filter_intensity = 1.0f;

    // Procedural Detail Injection (PDI) settings
    bool  procedural_detail   = true;
    float procedural_strength = 1.0f;
    
    // --- Tier 1: Color / Stylistic effects ---
    bool vignette = false;
    float vignette_intensity = 0.3f;
    
    bool film_grain = false;
    float grain_intensity = 0.06f;
    
    bool chromatic_aberration = false;
    float ca_offset = 0.002f;
    
    bool color_adjust = false;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float brightness = 0.0f;
    float warmth = 0.0f;
    
    bool sharpen = false;
    float sharpen_strength = 0.4f;
    
    // --- Tier 2: Advanced effects ---
    bool god_rays = false;
    float god_rays_intensity = 0.4f;    // 0.0-1.0
    float god_rays_decay = 0.96f;       // per-sample falloff
    float sun_x = 0.5f;                 // sun position in UV (0-1)
    float sun_y = 0.85f;                // top of screen
    
    bool ssao = false;
    float ssao_radius = 0.02f;          // sample radius in UV
    float ssao_intensity = 1.2f;        // darkening strength
    
    bool volumetric_light = false;
    float volumetric_intensity = 0.3f;
    
    bool bloom = false;
    float bloom_threshold = 0.7f;       // brightness threshold for bloom extraction
    float bloom_intensity = 0.35f;      // additive bloom strength

    // --- Authored-emitter bloom key -----------------------------------------
    // ON (default): the extract keeps only pixels that match one of the CURRENT
    // biome's authored emitter hues (BiomeTable::kEmitterPalette - torches,
    // lantern pods, lava) AND that are brighter than their own neighbourhood.
    // bloom_threshold is unused in this mode; bright-but-flat surfaces (sky, ice
    // fields) no longer bloom at all, and genuine lights bloom at their authored
    // mid-level brightness. OFF: the legacy single luminance threshold, which is
    // what bloom_threshold above drives. See bloom_extract.glsl for the data
    // behind every constant.
    bool  bloom_emitter_key      = true;
    float bloom_emitter_luma     = 0.10f;  // level floor; the dimmest authored
                                           // emitter is grove purple at luma
                                           // 0.164, the torch at 0.556
    float bloom_emitter_sat      = 0.80f;  // fraction of the emitter's own chroma
                                           // strength a pixel must reach; a real
                                           // unclipped emitter measures 1.00
    float bloom_emitter_contrast = 1.12f;  // local-brightness ratio floor
    float bloom_emitter_gain     = 1.60f;  // weight scale (buffer clamps at 1.0)
    
    bool shadows = false;
    float shadow_intensity = 0.5f;       // shadow darkness (0=none, 1=pitch black)
    float shadow_softness = 0.003f;      // blur radius for soft shadow edges
    float shadow_light_x = 0.3f;         // light direction X (+right, normalized)
    float shadow_light_y = -0.8f;        // light direction Y (-down, normalized)
    float shadow_sun_z = 0.15f;          // virtual sun depth component for 3D shadow angle
                                         // >0 = player/camera side (shadows angle toward player)
                                         // 0  = purely 2D screen-space shadows
                                         // <0 = background side (shadows angle away from player)
    
    bool outlines = false;               // depth-based edge outlines (cel-shading style)
    float outline_thickness = 1.0f;      // pixel width of outline (1.0 = 1px, 2.0 = thicker)
    float outline_intensity = 0.7f;      // outline opacity (0-1)
    float outline_depth_threshold = 0.002f; // depth difference to detect an edge
    
    // --- Remaster (Tier 3) effects ---
    bool pbr_enabled = false;            // Enable Cook-Torrance BRDF PBR + LabPBR materials
    bool reflections_enabled = false;    // Enable dynamic wave reflections for water
    float reflection_intensity = 0.35f;  // Reflection blend strength (0-1)

    // Preset & Filter names (for display)
    const char* preset_name = "SW+";
    const char* filter_name = "None";
};

// Legacy preset enum for backward compatibility
enum class PostFXPreset : int {
    OFF = 0,
    SW_PLUS_MEDIUM, // Swordigo Plus - Medium settings
    SW_PLUS_HIGH,   // Swordigo Plus - High settings (full PBR + reflections)
    ATMOSPHERIC,    // SSAO + volumetric + vignette
    ETHEREAL,       // God rays + warm glow
    CINEMATIC,
    RETRO,
    FANTASY,
    NOIR,
    CUSTOM,
    COUNT
};

void graphics_apply_tier(PostFXState& state, GraphicsTier tier);
void postfx_apply_filter(PostFXState& state, PostEffectFilter filter, float intensity = 1.0f);

// The ONE authoritative boot path for the render pipeline.
//
// Why this exists as a function instead of two inline calls: the F6 menu splits
// the pipeline into (a) a hardware GraphicsTier and (b) a cosmetic
// PostEffectFilter, and it changes them through exactly two entry points
// (graphics_apply_tier / postfx_apply_filter). Booting has to use those same
// two entry points in the same order. The previous boot path called
// postfx_apply_preset(OFF), which assigns `state = PostFXState{}` and thereby
// WIPES every tier flag set by fbo_init() while leaving graphics_tier at its
// struct default of SW_PLUS. The F6 menu then displayed "SW+" while the
// renderer ran with the OFF preset's flags - which is why the tier only took
// effect after the user re-clicked the already-selected SW+ button.
void postfx_apply_boot_defaults(PostFXState& state);
const char* graphics_tier_name(GraphicsTier tier);
const char* postfx_filter_name(PostEffectFilter filter);

void postfx_apply_preset(PostFXState& state, PostFXPreset preset);
const char* postfx_preset_name(PostFXPreset p);

bool postfx_load_custom_json(PostFXState& state);
void postfx_save_default_json();

bool fbo_init(int game_w, int game_h);
void fbo_destroy();
void fbo_begin_game();
void fbo_end_game_and_blit(int win_w, int win_h, FBOScale mode = FBOScale::SHARP_BILINEAR, const PostFXState* postfx = nullptr);
unsigned int fbo_get_texture();
unsigned int fbo_get_fbo();
bool fbo_is_active();

// Render portal effect directly into the current framebuffer (for non-PostFX / vanilla mode).
// Call AFTER drawApplication() and BEFORE GUI overlay.
// Works with or without the FBO pipeline active.
void fbo_draw_portal_vanilla(int viewport_w, int viewport_h);
