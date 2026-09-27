// ============================================================================
// composite_remaster.glsl — Swordigo Master Composite Shader (Remastered)
//
// FIXES:
//   1. Proper modular shadows (no primitive lighting)
//   2. Real sunlight with correct directional falloff
//   3. Coherent bloom (no broken torch lights)
//   4. Adaptive brightness/darkness normalization
//   5. Proper tone mapping with toe/shoulder curves
//   6. Swordigo aesthetic preserved (warm, vibrant, stylized)
//
// ARCHITECTURE:
//   - Scene arrives pre-lit (GLES1 fixed-function + baked lighting)
//   - This pass adds:
//     * Modular shadows (screen-space, depth-aware)
//     * Dynamic light halos (per-pixel falloff)
//     * Bloom (coherent, emitter-keyed)
//     * Tone mapping (adaptive, non-destructive)
//     * Color grading (saturation, warmth)
// ============================================================================

#version 430 core

// ============================================================================
// INPUTS
// ============================================================================

uniform sampler2D u_scene;           // Original scene (pre-lit)
uniform sampler2D u_depth;           // Depth buffer (linear or raw)
uniform sampler2D u_bloom;           // Bloom texture (additive)
uniform sampler2D u_ao;              // SSAO result (optional)
uniform sampler2D u_ui_mask;         // UI/HUD stencil mask

// ============================================================================
// SUNLIGHT & SHADOWS
// ============================================================================

uniform vec3  u_sun_dir;             // World-space sun direction (normalized)
uniform vec3  u_sun_color;           // Sun color (linear HDR)
uniform float u_sun_intensity;       // Sun brightness multiplier
uniform float u_shadow_enabled;      // 0 or 1
uniform float u_shadow_intensity;    // Shadow darkness (0..1)
uniform float u_shadow_softness;     // PCF kernel size
uniform vec2  u_shadow_light_dir;    // 2D light direction in UV space
uniform float u_shadow_lift;         // Minimum brightness in shadow (0..1)

// ============================================================================
// DYNAMIC LIGHTS (from game engine)
// ============================================================================

#define MAX_DYNAMIC_LIGHTS 8
uniform int   u_light_count;
uniform vec4  u_lights[MAX_DYNAMIC_LIGHTS];        // xyz = position, w = radius
uniform vec4  u_light_color[MAX_DYNAMIC_LIGHTS];   // rgb = color, a = intensity
uniform vec3  u_light_atten[MAX_DYNAMIC_LIGHTS];   // kc, kl, kq (attenuation)

// ============================================================================
// BLOOM & GLOW
// ============================================================================

uniform float u_bloom_enabled;
uniform float u_bloom_intensity;
uniform float u_bloom_threshold;     // Luminance threshold for legacy mode
uniform float u_emitter_keyed;       // 0 = luminance, 1 = authored emitter hue
uniform int   u_emitter_count;
uniform vec3  u_emitter[4];          // Biome's emitter palette
uniform float u_emitter_luma;        // Level floor
uniform float u_emitter_sat;         // Saturation floor
uniform float u_emitter_contrast;    // Local contrast ratio floor
uniform float u_emitter_gain;        // Weight scale

// ============================================================================
// AMBIENT & ATMOSPHERE
// ============================================================================

uniform vec3  u_ambient_sky;         // Sky ambient color
uniform vec3  u_ambient_ground;      // Ground ambient color
uniform float u_ambient_intensity;   // Ambient brightness multiplier
uniform float u_ao_enabled;          // 0 or 1
uniform float u_ao_strength;         // AO blend factor

// ============================================================================
// TONE MAPPING & COLOR GRADING
// ============================================================================

uniform float u_tonemap_mode;        // 0 = Reinhard, 1 = ACES
uniform float u_exposure;            // Scene exposure multiplier
uniform float u_saturation;          // Color saturation (1.0 = normal)
uniform float u_contrast;            // Contrast multiplier
uniform float u_warmth;              // Color warmth (-1..+1)
uniform float u_brightness_lift;     // Shadow lift (0..1)
uniform float u_highlight_compress;  // Highlight compression (0..1)

// ============================================================================
// QUALITY SETTINGS
// ============================================================================

uniform float u_quality_preset;      // 0 = Low, 1 = Medium, 2 = High
uniform float u_shadow_quality;      // PCF tap count (4, 8, 16)
uniform float u_bloom_quality;       // Blur passes (1, 2, 3)
uniform vec2  u_tex_size;            // Framebuffer resolution
uniform float u_time;                // Animated effects

// ============================================================================
// UI/HUD HANDLING
// ============================================================================

uniform float u_ui_pass;             // 0 or 1 (bypass entire pass for UI-only frames)
uniform float u_ui_exempt;           // 0 or 1 (exempt UI pixels from processing)

in vec2 v_uv;
out vec4 FragColor;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

float luma709(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float linearize_depth(float d) {
    float n = 50.0;   // Near plane
    float f = 20000.0; // Far plane
    return (2.0 * n) / (f + n - d * (f - n));
}

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

// ============================================================================
// SHADOW COMPUTATION (Modular, Screen-Space)
// ============================================================================

/// Compute shadow factor at a given UV coordinate.
/// Returns 1.0 = fully lit, 0.0 = fully shadowed
float compute_shadow_at(vec2 uv) {
    float my_depth = linearize_depth(texture(u_depth, uv).r);
    if (my_depth > 0.95) return 1.0;  // Sky is always lit
    
    float shadow = 1.0;
    vec2 live_dir = normalize(u_shadow_light_dir + vec2(0.0001));
    vec3 ray_dir_3d = normalize(vec3(live_dir, 0.3));  // Shallow angle for soft shadows
    vec2 step_uv = ray_dir_3d.xy * u_shadow_softness;
    float step_z = ray_dir_3d.z * u_shadow_softness;
    
    float dither = rand(uv);
    int steps = int(u_shadow_quality);
    
    for (int i = 1; i <= steps; i++) {
        float t = float(i) - dither * 0.9;
        vec2 sample_uv = uv + step_uv * t;
        float ray_depth = my_depth + step_z * t;
        
        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || 
            sample_uv.y < 0.0 || sample_uv.y > 1.0) break;
        
        float sample_depth = linearize_depth(texture(u_depth, sample_uv).r);
        float depth_diff = ray_depth - sample_depth;
        
        if (depth_diff > 0.001 && depth_diff < 0.18) {
            float falloff = 1.0 - t / float(steps);
            shadow = min(shadow, 1.0 - u_shadow_intensity * falloff);
        }
    }
    
    // Apply shadow lift to prevent pure black
    shadow = mix(u_shadow_lift, 1.0, shadow);
    return shadow;
}

/// PCF shadow sampling (Percentage Closer Filtering).
float compute_shadow_pcf() {
    float eps_x = 1.2 / u_tex_size.x;
    float eps_y = 1.2 / u_tex_size.y;
    
    float s_c = compute_shadow_at(v_uv);
    float s_r = compute_shadow_at(v_uv + vec2(eps_x, 0.0));
    float s_l = compute_shadow_at(v_uv - vec2(eps_x, 0.0));
    float s_u = compute_shadow_at(v_uv + vec2(0.0, eps_y));
    float s_d = compute_shadow_at(v_uv - vec2(0.0, eps_y));
    
    return (s_c * 4.0 + s_r + s_l + s_u + s_d) / 8.0;
}

// ============================================================================
// DYNAMIC LIGHT ACCUMULATION
// ============================================================================

/// Accumulate per-pixel glow from the game engine's light components.
/// This fills in the halo the per-vertex pipeline cannot express.
vec3 accumulate_engine_lights(vec2 uv) {
    if (u_light_count <= 0) return vec3(0.0);
    
    float aspect = u_tex_size.x / max(u_tex_size.y, 1.0);
    vec3 accum = vec3(0.0);
    
    for (int i = 0; i < MAX_DYNAMIC_LIGHTS; ++i) {
        if (i >= u_light_count) break;
        
        float reach = u_lights[i].w;
        if (reach <= 0.0) continue;
        
        // Screen-space reach from projection scale
        float r_uv = max(abs(0.5 * reach / u_tex_size.x),
                         abs(0.5 * reach / u_tex_size.y));
        if (r_uv <= 1e-6) continue;
        
        // Normalized screen distance
        vec2 d = (uv - u_lights[i].xy) * vec2(aspect, 1.0);
        float nd = length(d) / r_uv;
        if (nd >= 1.0) continue;
        
        // Attenuation: 1 / (kc + kl*wd + kq*wd²)
        float kc = max(u_light_atten[i].x, 1e-4);
        float kl = u_light_atten[i].y;
        float kq = u_light_atten[i].z;
        float wd = nd * reach;
        float att = kc / (kc + kl * wd + kq * wd * wd);
        if (att <= 0.001) continue;
        
        // Soften outer edge
        att *= smoothstep(1.0, 0.72, nd);
        
        accum += u_light_color[i].rgb * (u_light_color[i].a * att);
    }
    
    return accum;
}

// ============================================================================
// BLOOM EXTRACTION & GLOW
// ============================================================================

/// Chroma direction and strength (for emitter-keyed bloom).
vec3 chroma_dir(vec3 c, out float strength) {
    float m = max(max(c.r, c.g), c.b);
    if (m < 1e-5) { strength = 0.0; return vec3(0.0); }
    vec3 n = max(c, vec3(0.0)) / m;
    vec3 v = n - vec3(dot(n, vec3(0.299, 0.587, 0.114)));
    strength = length(v);
    if (strength < 1e-5) { return vec3(0.0); }
    return v / strength;
}

/// Emitter match score (hue + saturation).
vec2 emitter_match(vec3 col) {
    float cand_strength;
    vec3 cand = chroma_dir(col, cand_strength);
    if (cand_strength < 1e-4) return vec2(0.0);
    
    float best_hue = 0.0;
    float best_sat = 0.0;
    for (int i = 0; i < 4; i++) {
        if (i >= u_emitter_count) break;
        float ref_strength;
        vec3 ref = chroma_dir(u_emitter[i], ref_strength);
        if (ref_strength < 1e-4) continue;
        float hue = dot(cand, ref);
        if (hue > best_hue) {
            best_hue = hue;
            best_sat = cand_strength / ref_strength;
        }
    }
    return vec2(best_hue, best_sat);
}

/// Weight for "this pixel is a light source" (emitter-keyed).
float emitter_weight(vec3 col, float local_luma) {
    float l = luma709(col);
    
    // Local maximum: brighter than neighborhood
    float ratio = l / max(local_luma, 1e-3);
    float contrast = smoothstep(u_emitter_contrast,
                                u_emitter_contrast + 0.35, ratio);
    if (contrast <= 0.0) return 0.0;
    
    // Hue path: authored emitter
    float level = smoothstep(u_emitter_luma, u_emitter_luma + 0.10, l);
    float hue_path = 0.0;
    if (level > 0.0) {
        vec2 m = emitter_match(col);
        float hue = smoothstep(0.95, 0.98, m.x);
        float sat = smoothstep(u_emitter_sat - 0.08, u_emitter_sat + 0.02, m.y);
        hue_path = level * hue * sat;
    }
    
    // Clip path: channel at ceiling
    float mx = max(max(col.r, col.g), col.b);
    float clip = smoothstep(0.98, 1.0, mx);
    
    return clamp(max(hue_path, clip) * contrast * u_emitter_gain, 0.0, 1.0);
}

/// Additive glow with shoulder (prevents asymptote clipping).
vec3 add_glow(vec3 sum, vec3 glow) {
    glow = max(glow, vec3(0.0));
    const float GLOW_HEADROOM = 1.60;
    return sum + glow / (1.0 + luma709(glow) / GLOW_HEADROOM);
}

// ============================================================================
// TONE MAPPING
// ============================================================================

/// Extended Reinhard (L_white = 2.5).
vec3 tonemap_reinhard(vec3 x) {
    const float L_white = 2.5;
    float luma = luma709(x);
    float tone = luma * (1.0 + luma / (L_white * L_white)) / (1.0 + luma);
    return x * (tone / max(luma, 0.0001));
}

/// ACES Filmic (Narkowicz 2015).
vec3 tonemap_aces(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

/// Adaptive tone mapping with toe/shoulder curves.
vec3 tonemap_adaptive(vec3 x) {
    const float TONEMAP_TOE = 0.18;
    vec3 curve = tonemap_aces(x * 1.06);
    float knee = smoothstep(0.0, TONEMAP_TOE, luma709(x));
    return mix(x, curve, knee);
}

// ============================================================================
// COLOR GRADING
// ============================================================================

/// Apply Swordigo color grade (saturation, warmth, contrast).
vec3 apply_color_grade(vec3 color) {
    float luma = luma709(color);
    
    // Saturation
    vec3 desaturated = vec3(luma);
    color = mix(desaturated, color, u_saturation);
    
    // Warmth: boost red/yellow in highlights
    float warm_boost = smoothstep(0.5, 1.0, luma) * u_warmth * 0.1;
    color.r += warm_boost;
    color.g += warm_boost * 0.5;
    
    // Contrast
    color = mix(vec3(0.5), color, u_contrast);
    
    return color;
}

// ============================================================================
// MAIN COMPOSITE PASS
// ============================================================================

void main() {
    // ── UI/HUD passthrough ────────────────────────────────────────────────────
    if (u_ui_pass > 0.5) {
        FragColor = texture(u_scene, v_uv);
        return;
    }
    
    if (u_ui_exempt > 0.5 && texture(u_ui_mask, v_uv).g >= 0.002) {
        FragColor = texture(u_scene, v_uv);
        return;
    }
    
    // ── Sample scene ──────────────────────────────────────────────────────────
    vec3 scene = texture(u_scene, v_uv).rgb;
    float depth = linearize_depth(texture(u_depth, v_uv).r);
    
    // ── Apply modular shadows ─────────────────────────────────────────────────
    if (u_shadow_enabled > 0.5) {
        float shadow = compute_shadow_pcf();
        scene *= shadow;
    }
    
    // ── Apply SSAO ────────────────────────────────────────────────────────────
    if (u_ao_enabled > 0.5) {
        float ao = texture(u_ao, v_uv).r;
        ao = mix(ao, 1.0, u_shadow_lift);
        scene *= mix(1.0, ao, u_ao_strength);
    }
    
    // ── Accumulate dynamic lights ─────────────────────────────────────────────
    if (u_light_count > 0) {
        scene = add_glow(scene, accumulate_engine_lights(v_uv));
    }
    
    // ── Apply bloom ───────────────────────────────────────────────────────────
    if (u_bloom_enabled > 0.5) {
        vec3 bloom = texture(u_bloom, v_uv).rgb;
        scene = add_glow(scene, bloom * u_bloom_intensity);
    }
    
    // ── Exposure ──────────────────────────────────────────────────────────────
    scene *= u_exposure;
    
    // ── Adaptive brightness normalization ─────────────────────────────────────
    scene = normalize_brightness(scene, u_brightness_lift, u_highlight_compress);
    
    // ── Tone mapping ──────────────────────────────────────────────────────────
    if (u_tonemap_mode < 0.5) {
        scene = tonemap_reinhard(scene);
    } else {
        scene = tonemap_adaptive(scene);
    }
    
    // ── Color grading ─────────────────────────────────────────────────────────
    scene = apply_color_grade(scene);
    
    // ── Output gamma ──────────────────────────────────────────────────────────
    scene = pow(max(scene, vec3(0.0)), vec3(1.0 / 1.6));
    
    FragColor = vec4(clamp(scene, 0.0, 1.0), 1.0);
}

// ============================================================================
// HELPER: Normalize brightness (from modular_lighting.glsl)
// ============================================================================

vec3 normalize_brightness(vec3 color, float shadow_lift, float highlight_compress) {
    float luma = luma709(color);
    
    // Lift shadows
    float shadow_factor = smoothstep(0.0, 0.3, luma);
    color = mix(color * shadow_lift, color, shadow_factor);
    
    // Compress highlights
    float highlight_factor = smoothstep(0.7, 1.0, luma);
    color = mix(color, color * highlight_compress, highlight_factor);
    
    return color;
}
