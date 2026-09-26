#version 430 core
// ====================================================================
//  Swordigo 13-Tap Dual-Kawase Bloom Downsampler
//  Adapted from Godot 4 (Jimenez 2014 & Brian Karis / UE4 weighting)
//
//  ------------------------------------------------------------------
//  THE BLOOM KEY IS THE GAME'S OWN EMITTER PALETTE, NOT A LUMA CUTOFF
//  ------------------------------------------------------------------
//  A luminance threshold cannot tell a torch from a sunlit orange wall, and it
//  cannot see a torch at all: the torch is authored at RGB(1.00,0.48,0.00),
//  luma709 = 0.556, while every SW+ tier threshold is 0.78-0.85. Vanilla
//  Swordigo, however, *declares* its lights: every glowing thing it draws is a
//  Point light with `glow` set and an authored RGB tint that the glow sprite is
//  modulated by. There are 143 of them across the eight vanilla levels, in ten
//  distinct colours, recorded in src/graphics/BiomeTable.h (kEmitterPalette)
//  together with the tool that re-derives them. Fire's lava sheet is the one
//  emissive surface that is not a point light, and it is in that table too.
//
//  WHY HUE CAN BE COMPARED EXACTLY - the invariant that makes this work:
//  the chroma direction used below (divide by the largest channel, then remove
//  the luma axis, then normalise) is invariant BOTH to scaling the colour and
//  to adding the same amount to all three channels. Both are proved by
//  construction: scaling cancels in the normalisation, and adding a constant is
//  a shift that the mean-removal annihilates. That is exactly the geometry of an
//  emitter: the authored tint times some brightness, drawn ADDITIVELY over
//  whatever is behind it. So an emitter pixel that has not clipped matches its
//  authored hue at dot = 1.0000 - measured, at every scale from 0.2x to 6x -
//  and the only thing that moves it is the game's own COLOURED lighting tint
//  (measured worst case: the torch under Fire's directional light 1.00,0.70,0.50,
//  dot 0.9830 = 10.6 degrees). Hence a window of 0.95..0.98 (11.5-18 degrees):
//  full weight inside 11.5 deg, zero beyond 18 deg.
//
//  WHY THERE IS A SECOND PATH FOR CLIPPED PIXELS:
//  the scene buffer is 8-bit, so a glow that is bright enough is per-channel
//  clamped and its hue really is destroyed. Measured on the real palette: the
//  pale emitters (grove lavender, icecastle, florennum, grasslands, grove blue,
//  the cave/forest pods) have two channels at the ceiling by 1.4x-2.0x and
//  collapse to white - chroma strength 0.000 at 1.6x. No hue test can recover
//  that, so a pixel with any channel at the ceiling is treated as a light source
//  on its own terms.
//
//  WHY CONTRAST IS NOT OPTIONAL:
//  hue alone cannot separate an emitter from terrain, and this is measured, not
//  assumed. Ordinary surfaces whose albedo deviates from grey in the same
//  direction as a torch match it almost exactly:
//
//      brown wood   (0.40,0.25,0.12)  -> torch       dot 0.9998 (1.2 deg)
//      orange rock  (0.70,0.40,0.15)  -> torch       dot 0.9995 (1.9 deg)
//      cave walls   (0.45,0.35,0.28)  -> cave pod    dot 0.9998 (1.2 deg)
//      Fire's directional light colour (1.00,0.70,0.50) -> wasteland pod, 1.8 deg
//      Florennum's directional light colour (1.00,0.95,0.80) -> grasslands pod, 3.2 deg
//
//  They are the same hue because they are the same warm palette. What separates
//  them from a light is that a light is a LOCAL MAXIMUM: it is added over its
//  surroundings, so it is brighter than its own neighbourhood, while a lit wall
//  is flat. Terrain that is both warm and locally bright is not distinguishable
//  from a warm light by any per-pixel test - see the note on saturation below
//  for the one class that is, and note that the definitive fix is per-DRAW
//  identity (the glow sprites have their own texture names, and the SRE13
//  texture channel already reports them).
//
//  The three terms, all measured:
//    1. HUE or CLIP - see above.
//    2. SATURATION  - the pixel must be at least ~80% as saturated as that
//                     emitter (chroma strength ratio, which is exactly 1.0 for
//                     an unclipped emitter at any brightness and 0.94..1.31 for
//                     emitters under coloured light). This is what rejects
//                     washed-out warm surfaces whose direction matches:
//                     brown wood 0.70, cave walls 0.54, warm stone 0.33,
//                     orange rock 0.78.
//    3. CONTRAST    - the pixel must be brighter than its own neighbourhood
//                     (luma / local mean luma). This is the term that carries the
//                     flat-terrain cases, and the first knob to loosen if
//                     emitters are being missed.
//
//  Set u_emitter_keyed = 0 for the previous luminance-only behaviour.
// ====================================================================

uniform sampler2D u_tex;
uniform float u_threshold;        // luminance cutoff; legacy path only
uniform vec2  u_tex_size;

// --- UI/HUD exclusion -------------------------------------------------------
// u_ui_mask is the scene FBO's DEPTH24_STENCIL8 attachment. Sampling a
// depth-stencil texture returns the DEPTH in .r and the STENCIL in .g, and the
// GL bridge marks every draw whose projection is orthographic with stencil 1
// (2D HUD, text, sprites) and the 3D world with stencil 0.
//
// WHY THE BLOOM HAS TO SEE THIS, when the composite already exempts the UI:
// the extract runs BEFORE the composite. The composite's exemption can only
// protect the UI's own pixels, so a bright HUD sprite still blooms outwards and
// leaves its halo in the WORLD pixels around it - which is what a square glow
// around a round UI bubble is. Removing the UI from the extract removes the halo
// at its source. u_ui_exempt = 0 disables the mask for comparison.
uniform sampler2D u_ui_mask;
uniform float u_ui_exempt;

// --- authored-emitter key, uploaded from BiomeTable::kEmitterPalette ---------
// u_emitter[] holds the CURRENT biome's emitters; unset slots are (0,0,0) and
// u_emitter_count says how many are real. A slot of (0,0,0) has no chroma and so
// can never match, but the count is the authoritative bound.
uniform float u_emitter_keyed;    // 0 = luminance only, 1 = authored-emitter key
uniform int   u_emitter_count;
uniform vec3  u_emitter[4];
uniform float u_emitter_luma;     // level floor                 (default 0.10)
uniform float u_emitter_sat;      // saturation floor, fraction  (default 0.80)
uniform float u_emitter_contrast; // local-contrast ratio floor  (default 1.12)
uniform float u_emitter_gain;     // weight scale                (default 1.60)

in vec2 v_uv;
out vec4 FragColor;

// Hue window: full weight inside EMITTER_HUE_HI (11.5 deg of an authored
// emitter), nothing beyond EMITTER_HUE_LO (18 deg).
const float EMITTER_HUE_LO = 0.95;
const float EMITTER_HUE_HI = 0.98;
// 8-bit ceiling. At or above this a channel has clipped and its hue is gone.
const float EMITTER_CLIP_LO = 0.98;
// Soft-knee widths.
const float EMITTER_LEVEL_KNEE = 0.10;
const float EMITTER_CONTRAST_KNEE = 0.35;
const float EMITTER_SAT_KNEE = 0.08;

float luma709(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Chroma direction and strength, in max-normalised space:
//   * dividing by the largest channel makes the vector invariant to overall
//     brightness, so a dim torch and a bright one have identical chroma;
//   * removing the luma axis makes it invariant to adding a constant, i.e. to
//     the additive background the glow is drawn over, and collapses every
//     achromatic colour (white, grey, a clipped core) to a zero vector, so
//     the dot products below cannot match a hue by accident.
// `strength` is the length before normalisation, i.e. the saturation measure.
vec3 chroma_dir(vec3 c, out float strength) {
    float m = max(max(c.r, c.g), c.b);
    if (m < 1e-5) { strength = 0.0; return vec3(0.0); }
    vec3 n = max(c, vec3(0.0)) / m;
    vec3 v = n - vec3(dot(n, vec3(0.299, 0.587, 0.114)));
    strength = length(v);
    if (strength < 1e-5) { return vec3(0.0); }
    return v / strength;
}

// Best hue/saturation agreement with the current biome's authored emitters.
// Returns hue match in x and saturation ratio in y.
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

// Weight in [0,1] for "this sample is one of the game's own light sources".
// `local_luma` is the mean luma of the surrounding footprint.
float emitter_weight(vec3 col, float local_luma) {
    float l = luma709(col);

    // Local maximum: how much brighter than its own neighbourhood. Applied to
    // both paths, because it is the only term that separates a flat lit surface
    // from a light drawn over one.
    float ratio = l / max(local_luma, 1e-3);
    float contrast = smoothstep(u_emitter_contrast,
                                u_emitter_contrast + EMITTER_CONTRAST_KNEE, ratio);
    if (contrast <= 0.0) return 0.0;

    // Path A: the authored emitter hue, at or above the level floor.
    float level = smoothstep(u_emitter_luma, u_emitter_luma + EMITTER_LEVEL_KNEE, l);
    float hue_path = 0.0;
    if (level > 0.0) {
        vec2 m = emitter_match(col);
        float hue = smoothstep(EMITTER_HUE_LO, EMITTER_HUE_HI, m.x);
        float sat = smoothstep(u_emitter_sat - EMITTER_SAT_KNEE,
                               u_emitter_sat + 0.02, m.y);
        hue_path = level * hue * sat;
    }

    // Path B: a clipped channel. The hue is gone, the light is not.
    float mx = max(max(col.r, col.g), col.b);
    float clip = smoothstep(EMITTER_CLIP_LO, 1.0, mx);

    return clamp(max(hue_path, clip) * contrast * u_emitter_gain, 0.0, 1.0);
}

float karis_weight(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

// Legacy luminance key: soft-knee threshold on luma, unchanged.
vec3 threshold_sample(vec3 col, float thresh) {
    float luma = luma709(col);
    float knee = max(thresh * 0.5, 0.001);
    float soft = clamp((luma - thresh + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft;
    float factor = max(soft, step(thresh, luma));
    return col * factor;
}

// Authored-emitter key. Keeps the pixel's own colour so the bloom carries the
// emitter's hue, scaled by how confident the key is.
vec3 emitter_sample(vec3 col, float local_luma) {
    return col * emitter_weight(col, local_luma);
}

void main() {
    vec2 px = 1.0 / max(u_tex_size, vec2(1.0));

    // 13-tap Dual-Kawase / Jimenez layout (Jimenez 2014, Godot 4 blur_raster.glsl)
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

    // Local mean over the whole 13-tap footprint - the "neighbourhood" the
    // contrast term compares each box against.
    float local_luma = luma709(a + b + c + d + e + f + g + h + i + j + k + l + m) / 13.0;

    // 1.0 where this pixel is 3D world, 0.0 where it is 2D UI/HUD.
    // The stencil is an 8-bit integer, so stencil 1 arrives normalised as
    // 1/255 = 0.0039 - NOT 1.0. Comparing against 1.0 here would be a silent
    // no-op that looks like the mask simply not working, so the floor is well
    // under one stencil step and the comparison is on "any non-zero stencil".
    // An all-zero stencil (postfx off, or no orthographic draw seen this frame)
    // leaves every pixel as world, i.e. exactly the previous behaviour.
    float ui_needed = 1.0;
    if (u_ui_exempt > 0.5) {
        ui_needed = (texture(u_ui_mask, v_uv).g < 0.002) ? 1.0 : 0.0;
    }

    // Threshold each individual box
    vec3 box0 = (d + e + i + j) * 0.25 * ui_needed;
    vec3 box1 = (a + b + g + f) * 0.25 * ui_needed;
    vec3 box2 = (b + c + h + g) * 0.25 * ui_needed;
    vec3 box3 = (f + g + l + k) * 0.25 * ui_needed;
    vec3 box4 = (g + h + m + l) * 0.25 * ui_needed;

    if (u_emitter_keyed > 0.5) {
        box0 = emitter_sample(box0, local_luma);
        box1 = emitter_sample(box1, local_luma);
        box2 = emitter_sample(box2, local_luma);
        box3 = emitter_sample(box3, local_luma);
        box4 = emitter_sample(box4, local_luma);
    } else {
        box0 = threshold_sample(box0, u_threshold);
        box1 = threshold_sample(box1, u_threshold);
        box2 = threshold_sample(box2, u_threshold);
        box3 = threshold_sample(box3, u_threshold);
        box4 = threshold_sample(box4, u_threshold);
    }

    // Karis weighted combination
    float w0 = 0.5   * karis_weight(box0);
    float w1 = 0.125 * karis_weight(box1);
    float w2 = 0.125 * karis_weight(box2);
    float w3 = 0.125 * karis_weight(box3);
    float w4 = 0.125 * karis_weight(box4);

    vec3 bloom = (box0 * w0 + box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4) / (w0 + w1 + w2 + w3 + w4 + 0.0001);
    FragColor = vec4(bloom, 1.0);
}
