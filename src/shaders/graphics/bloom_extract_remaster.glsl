// ============================================================================
// bloom_extract_remaster.glsl — Swordigo Bloom Extraction (Remastered)
//
// FIXES:
//   1. Coherent bloom (no broken torch lights with separated halos)
//   2. Proper emitter detection (hue + saturation + contrast)
//   3. Clipped pixel handling (8-bit ceiling detection)
//   4. UI/HUD exclusion (prevents bloom halos around UI)
//   5. Karis weighting for smooth falloff
//
// KEY INSIGHT:
//   The bloom key is the game's OWN EMITTER PALETTE, not a luma cutoff.
//   A luminance threshold cannot tell a torch from a sunlit orange wall.
//   Swordigo declares its lights: every glowing thing is a Point light with
//   an authored RGB tint. We match that tint via chroma comparison.
// ============================================================================

#version 430 core

uniform sampler2D u_tex;
uniform sampler2D u_ui_mask;
uniform vec2  u_tex_size;

// ── Bloom extraction mode ─────────────────────────────────────────────────
uniform float u_emitter_keyed;       // 0 = luminance only, 1 = authored-emitter key
uniform float u_threshold;           // Legacy luminance threshold

// ── Authored emitter palette (from BiomeTable::kEmitterPalette) ──────────
uniform int   u_emitter_count;
uniform vec3  u_emitter[4];
uniform float u_emitter_luma;        // Level floor (default 0.10)
uniform float u_emitter_sat;         // Saturation floor (default 0.80)
uniform float u_emitter_contrast;    // Local contrast ratio floor (default 1.12)
uniform float u_emitter_gain;        // Weight scale (default 1.60)

// ── UI handling ───────────────────────────────────────────────────────────
uniform float u_ui_exempt;           // 0 or 1

in vec2 v_uv;
out vec4 FragColor;

// ============================================================================
// CONSTANTS
// ============================================================================

// Hue window: full weight inside EMITTER_HUE_HI (11.5 deg), nothing beyond EMITTER_HUE_LO (18 deg)
const float EMITTER_HUE_LO = 0.95;
const float EMITTER_HUE_HI = 0.98;

// 8-bit ceiling: at or above this a channel has clipped
const float EMITTER_CLIP_LO = 0.98;

// Soft-knee widths
const float EMITTER_LEVEL_KNEE = 0.10;
const float EMITTER_CONTRAST_KNEE = 0.35;
const float EMITTER_SAT_KNEE = 0.08;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

float luma709(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

/// Chroma direction and strength in max-normalised space.
/// Invariant to brightness and additive background.
vec3 chroma_dir(vec3 c, out float strength) {
    float m = max(max(c.r, c.g), c.b);
    if (m < 1e-5) { strength = 0.0; return vec3(0.0); }
    vec3 n = max(c, vec3(0.0)) / m;
    vec3 v = n - vec3(dot(n, vec3(0.299, 0.587, 0.114)));
    strength = length(v);
    if (strength < 1e-5) { return vec3(0.0); }
    return v / strength;
}

/// Best hue/saturation agreement with the current biome's authored emitters.
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

/// Weight in [0,1] for "this sample is one of the game's own light sources".
float emitter_weight(vec3 col, float local_luma) {
    float l = luma709(col);
    
    // Local maximum: how much brighter than its own neighbourhood
    float ratio = l / max(local_luma, 1e-3);
    float contrast = smoothstep(u_emitter_contrast,
                                u_emitter_contrast + EMITTER_CONTRAST_KNEE, ratio);
    if (contrast <= 0.0) return 0.0;
    
    // Path A: the authored emitter hue, at or above the level floor
    float level = smoothstep(u_emitter_luma, u_emitter_luma + EMITTER_LEVEL_KNEE, l);
    float hue_path = 0.0;
    if (level > 0.0) {
        vec2 m = emitter_match(col);
        float hue = smoothstep(EMITTER_HUE_LO, EMITTER_HUE_HI, m.x);
        float sat = smoothstep(u_emitter_sat - EMITTER_SAT_KNEE,
                               u_emitter_sat + 0.02, m.y);
        hue_path = level * hue * sat;
    }
    
    // Path B: a clipped channel (hue is gone, light is not)
    float mx = max(max(col.r, col.g), col.b);
    float clip = smoothstep(EMITTER_CLIP_LO, 1.0, mx);
    
    return clamp(max(hue_path, clip) * contrast * u_emitter_gain, 0.0, 1.0);
}

/// Karis weighting for smooth falloff.
float karis_weight(vec3 c) {
    float luma = luma709(c);
    return 1.0 / (1.0 + luma);
}

/// Legacy luminance key: soft-knee threshold on luma.
vec3 threshold_sample(vec3 col, float thresh) {
    float luma = luma709(col);
    float knee = max(thresh * 0.5, 0.001);
    float soft = clamp((luma - thresh + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft;
    float factor = max(soft, step(thresh, luma));
    return col * factor;
}

/// Authored-emitter key: keeps the pixel's own colour so bloom carries the hue.
vec3 emitter_sample(vec3 col, float local_luma) {
    return col * emitter_weight(col, local_luma);
}

// ============================================================================
// MAIN EXTRACTION
// ============================================================================

void main() {
    vec2 px = 1.0 / max(u_tex_size, vec2(1.0));
    
    // ── 13-tap Dual-Kawase / Jimenez layout ───────────────────────────────────
    // 4 inner diagonal samples
    vec3 d = texture(u_tex, v_uv + vec2(-0.5, -0.5) * px).rgb;
    vec3 e = texture(u_tex, v_uv + vec2( 0.5, -0.5) * px).rgb;
    vec3 i = texture(u_tex, v_uv + vec2(-0.5,  0.5) * px).rgb;
    vec3 j = texture(u_tex, v_uv + vec2( 0.5,  0.5) * px).rgb;
    
    // 4 outer corner samples
    vec3 a = texture(u_tex, v_uv + vec2(-1.0, -1.0) * px).rgb;
    vec3 c = texture(u_tex, v_uv + vec2( 1.0, -1.0) * px).rgb;
    vec3 k = texture(u_tex, v_uv + vec2(-1.0,  1.0) * px).rgb;
    vec3 m = texture(u_tex, v_uv + vec2( 1.0,  1.0) * px).rgb;
    
    // 4 cross axis samples
    vec3 b = texture(u_tex, v_uv + vec2( 0.0, -1.0) * px).rgb;
    vec3 f = texture(u_tex, v_uv + vec2(-1.0,  0.0) * px).rgb;
    vec3 g = texture(u_tex, v_uv).rgb;
    vec3 h = texture(u_tex, v_uv + vec2( 1.0,  0.0) * px).rgb;
    vec3 l = texture(u_tex, v_uv + vec2( 0.0,  1.0) * px).rgb;
    
    // ── Local mean over the whole 13-tap footprint ────────────────────────────
    float local_luma = luma709(a + b + c + d + e + f + g + h + i + j + k + l + m) / 13.0;
    
    // ── UI/HUD exclusion ──────────────────────────────────────────────────────
    // Stencil 1 = UI/HUD (2D), stencil 0 = 3D world
    // Stencil is 8-bit, so 1 arrives as 1/255 = 0.0039, not 1.0
    float ui_needed = 1.0;
    if (u_ui_exempt > 0.5) {
        ui_needed = (texture(u_ui_mask, v_uv).g < 0.002) ? 1.0 : 0.0;
    }
    
    // ── Threshold each box ────────────────────────────────────────────────────
    vec3 box0 = (d + e + i + j) * 0.25 * ui_needed;
    vec3 box1 = (a + b + g + f) * 0.25 * ui_needed;
    vec3 box2 = (b + c + h + g) * 0.25 * ui_needed;
    vec3 box3 = (f + g + l + k) * 0.25 * ui_needed;
    vec3 box4 = (g + h + m + l) * 0.25 * ui_needed;
    
    // ── Apply extraction mode ─────────────────────────────────────────────────
    if (u_emitter_keyed > 0.5) {
        // Authored-emitter key: hue + saturation + contrast
        box0 = emitter_sample(box0, local_luma);
        box1 = emitter_sample(box1, local_luma);
        box2 = emitter_sample(box2, local_luma);
        box3 = emitter_sample(box3, local_luma);
        box4 = emitter_sample(box4, local_luma);
    } else {
        // Legacy luminance key
        box0 = threshold_sample(box0, u_threshold);
        box1 = threshold_sample(box1, u_threshold);
        box2 = threshold_sample(box2, u_threshold);
        box3 = threshold_sample(box3, u_threshold);
        box4 = threshold_sample(box4, u_threshold);
    }
    
    // ── Karis weighted combination ────────────────────────────────────────────
    float w0 = 0.5   * karis_weight(box0);
    float w1 = 0.125 * karis_weight(box1);
    float w2 = 0.125 * karis_weight(box2);
    float w3 = 0.125 * karis_weight(box3);
    float w4 = 0.125 * karis_weight(box4);
    
    vec3 bloom = (box0 * w0 + box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4) 
                 / (w0 + w1 + w2 + w3 + w4 + 0.0001);
    
    FragColor = vec4(bloom, 1.0);
}
