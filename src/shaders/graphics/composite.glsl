#version 430 core
// ====================================================================
//  Swordigo Master Scene Composite Shader
//  Includes PBR shading, Screen-Space Shadows, Planar Water Reflections,
//  Foliage Subsurface Scattering, God Rays, Karis Bloom, and ACES Tonemapping
// ====================================================================

uniform sampler2D u_scene;      // original scene
uniform sampler2D u_ao;         // SSAO result (grayscale)
uniform sampler2D u_godrays;    // god rays (additive light)
uniform sampler2D u_bloom;      // bloom result (additive glow)
uniform sampler2D u_depth;      // depth buffer for shadows
uniform float u_ao_enabled;     // 0 or 1
uniform float u_gr_enabled;     // 0 or 1
uniform float u_bloom_enabled;  // 0 or 1
uniform float u_bloom_intensity; // bloom strength
uniform float u_gr_tint_r, u_gr_tint_g, u_gr_tint_b;  // god ray color tint

// === Shadows ===
uniform float u_shadow_enabled;  // 0 or 1
uniform float u_shadow_intensity;
uniform float u_shadow_softness;
uniform vec2  u_shadow_light_dir;  // 2D light direction in UV space
uniform float u_shadow_sun_z;      // virtual sun depth
uniform float u_shadow_near;
uniform float u_shadow_far;
uniform vec2  u_tex_size;          // resolution size

// === Background-driven atmosphere (from SRE) ===
uniform float u_bg_depth;       // distance to background plane
uniform float u_bg_scale;       // background scale
uniform float u_shadow_lift;    // how much to brighten shadows
uniform float u_ambient_r, u_ambient_g, u_ambient_b;  // sky ambient color

// === Game-Engine Light Handshake ===
uniform vec4  u_light0_pos;      // GL_LIGHT0 position
uniform vec4  u_light0_diffuse;  // GL_LIGHT0 diffuse color
uniform vec4  u_light0_ambient;  // GL_LIGHT0 ambient color
uniform vec4  u_light_model_amb; // glLightModel global ambient
uniform vec4  u_mat_diffuse;     // current material diffuse
uniform vec4  u_cur_color;       // current glColor4 vertex color
uniform vec3  u_player_world;    // hero world-space position
uniform mat4  u_modelview;       // current modelview matrix
uniform mat4  u_projection;      // current projection matrix

// === Remaster Uniforms ===
uniform float u_pbr_enabled;          // 0 or 1
uniform float u_reflections_enabled;  // 0 or 1
uniform float u_reflection_intensity; // 0-1
// Authoritative water colour for the CURRENT biome, straight from
// scene_v3_biomes.json (BiomeEntry::waterSurface). u_water_valid is 0 when the
// biome has no water or could not be resolved.
uniform vec3  u_water_surface;
uniform float u_water_valid;
uniform float u_time;                 // animated ripples
uniform float u_ui_pass;              // 0 or 1 (bypasses the whole pass for UI)
// The same stencil mask the bloom extract reads: a DEPTH24_STENCIL8 sample puts
// depth in .r and the stencil in .g, and the GL bridge stamps every orthographic
// (2D) draw - HUD, text, sprites - with stencil 1 and the 3D world with 0.
uniform sampler2D u_ui_mask;
uniform float u_ui_exempt;

// ── The level's own point lights, collected per frame ───────────────────────
// Fed from LightManager, which the GL bridge populates when a LightComponent
// places its light (see jni_bridge_arm64.cpp). xyz is the EYE-space position and
// w is the reach; u_light_atten carries the engine's own (kc, kl, kq), so the
// falloff evaluated below is the level's authored curve and not an inverse-square
// invented here.
// MAX_DYNAMIC_LIGHTS must match the constant of the same name in fbo_scaler.cpp:
// an array size is part of the uniform interface, and a mismatch means the
// uniform is not found and this whole path silently does nothing.
#define MAX_DYNAMIC_LIGHTS 8
uniform int  u_light_count;
uniform vec4 u_lights[MAX_DYNAMIC_LIGHTS];
uniform vec4 u_light_color[MAX_DYNAMIC_LIGHTS];
uniform vec3 u_light_atten[MAX_DYNAMIC_LIGHTS];

in vec2 v_uv;
out vec4 FragColor;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float linearize_depth(float d) {
    float n = (u_shadow_near > 0.1) ? u_shadow_near : 50.0;
    float f = (u_shadow_far > 100.0) ? u_shadow_far : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

vec2 compute_light_dir_ss() {
    if (u_light0_pos.w < 0.5) {
        return normalize(u_light0_pos.xy + vec2(0.0001));
    } else {
        vec4 light_clip  = u_projection * u_modelview * vec4(u_light0_pos.xyz, 1.0);
        vec4 player_clip = u_projection * u_modelview * vec4(u_player_world, 1.0);
        
        if (abs(light_clip.w) < 0.0001 || abs(player_clip.w) < 0.0001) {
            return u_shadow_light_dir;
        }
        
        vec2 light_ss  = (light_clip.xy  / light_clip.w)  * 0.5 + 0.5;
        vec2 player_ss = (player_clip.xy / player_clip.w) * 0.5 + 0.5;
        
        if (any(isnan(light_ss)) || any(isinf(light_ss)) || any(isnan(player_ss)) || any(isinf(player_ss))) {
            return u_shadow_light_dir;
        }
        
        vec2 d = light_ss - player_ss;
        float len = length(d);
        if (isnan(len) || isinf(len) || len <= 0.001) {
            return u_shadow_light_dir;
        }
        return d / len;
    }
}

// "Is this draw untinted?"
//
// AUDIT NOTE (unresolved deliberate heuristic): the 0.8 and 1.0 below have NO
// source in the game data. 0.8 is a mid-grey that the GLES1 default material
// happens to land on, and 1.0 is the identity colour, so this reads as "the
// engine left both the material and the current colour at their defaults".
// That is a plausible reading but it is not evidenced, so it is left as-is and
// flagged rather than replaced with a second guess. The tolerance 0.12 on a
// 3-channel L1 distance is likewise unexplained.
//
// What would settle it: confirm the defaults from the guest's own material
// init - Caver::RenderingContext::SetColor and the GLES1 default material- and
// replace these constants with the recorded values. Until then, treat any tint
// work here as unverified.
vec3 get_material_tint() {
    vec3 md = u_mat_diffuse.rgb;
    vec3 vc = u_cur_color.rgb;
    float md_diff = dot(abs(md - vec3(0.8)), vec3(1.0));
    float vc_diff = dot(abs(vc - vec3(1.0)), vec3(1.0));
    if (md_diff < 0.12 && vc_diff < 0.12) return vec3(1.0);
    return clamp(md * 0.7 + vc * 0.3, vec3(0.0), vec3(1.0));
}

struct Material {
    int id; // 0=default, 1=water, 2=lava/emissive, 3=gold/metallic, 4=foliage
    float roughness;
    float metalness;
    float emissive;
    vec3 F0;
};

// --- Water identification ----------------------------------------------------
// The game's water is NOT blue, and the old test assumed it was.
//
// scene_v3_biomes.json records the water colour per biome. Grasslands and Cave
// are RGB(0.000, 0.376, 0.256) - GREENER than they are blue - and Fire's water is
// lava at RGB(0.800, 0.200, 0.000). The previous test was
//     c.b > 0.45 && c.g > 0.25 && c.r < 0.35
// i.e. "clearly blue", which the real palette fails outright on b
// (0.376 < 0.45), so reflections never engaged on real water at all. It is also
// worse than a no-op: the foliage rule in get_material() below requires
// g > b * 1.15, and (0.000, 0.376, 0.256) satisfies that (0.376 > 0.294), so
// actual water was classified as GRASS and received subsurface scattering instead
// of a reflection.
//
// So: match the biome's recorded chroma when we have one, and only fall back to
// the blue heuristic when no biome is known. Chroma is compared rather than
// absolute level because a liquid's brightness swings with depth, lighting and
// whatever is behind it, while its hue is what identifies it.
bool is_water_chroma(vec3 c) {
    if (u_water_valid > 0.5) {
        vec3 a = normalize(max(c, vec3(0.0)) + vec3(1e-4));
        vec3 b = normalize(max(u_water_surface, vec3(0.0)) + vec3(1e-4));
        return dot(a, b) > 0.93;
    }
    return (c.b > 0.45 && c.g > 0.25 && c.r < 0.35);
}

Material get_material(vec3 color, float depth) {
    Material mat;
    mat.id = 0;
    mat.roughness = 0.8;
    mat.metalness = 0.0;
    mat.emissive = 0.0;
    mat.F0 = vec3(0.04);
    
    if (depth > 0.95) {
        return mat;
    }
    
    if (is_water_chroma(color)) {
        // Fire's recorded water is red-dominant, i.e. it is lava. Treat it as the
        // emissive surface it is instead of trying to mirror the scene in it.
        if (u_water_valid > 0.5 && u_water_surface.r > u_water_surface.b) {
            mat.id = 2;
            mat.emissive = 1.0;
            mat.roughness = 1.0;
            return mat;
        }
        mat.id = 1;
        mat.roughness = 0.08;
        mat.metalness = 0.1;
        mat.F0 = vec3(0.02);
        return mat;
    }
    // --- warm emitter / lava, matched against MEASURED anchors -------------
    //
    // The previous revision used two hand-invented threshold tests here:
    //     color.r > 0.80 && color.g > 0.22 && color.b < 0.20   // "lava"
    //     color.r > 0.70 && color.g > 0.50 && color.b < 0.30   // "gold"
    // and measured against the palette recorded in scene_v3_biomes.json (the
    // values dumped by tools/scene_material_report.cpp) BOTH are wrong:
    //
    //   * Lava. Fire's recorded water - the game's actual lava - is
    //     RGB(0.800, 0.200, 0.000). `r > 0.80` is a STRICT inequality that
    //     excludes exactly 0.800, and `g > 0.22` excludes 0.200. The lava rule
    //     therefore never fired on real lava, missing on two thresholds at once.
    //
    //   * "Gold". It matched NOTHING in the entire vanilla palette. The nearest
    //     warm colour, the torch/fire emitter RGB(1.000, 0.480, 0.000), misses
    //     `g > 0.50` by 0.02. Every other emitter (grasslands lantern
    //     1.000,0.850,0.500 / icecastle 0.600,0.800,1.000 / grove lavender
    //     0.822,0.724,1.000) is excluded on b or on the green/blue majority.
    //     A rule that can never fire is not harmlessly dead: it silently
    //     consumed the metallic branch, so nothing ever reached mat.id == 3.
    //
    // The anchors below are the real values, and the test is a CHROMA
    // comparison rather than a set of absolute thresholds. Chroma is the right
    // invariant because emitters vary in brightness with flicker, distance and
    // bloom while their hue is authored and constant.
    //
    // Source: all eight vanilla v3_*.scene files, every SceneLight colour and
    // every water block. Re-derive with tools/scene_material_report.cpp.
    vec3 lava_chroma    = normalize(vec3(0.800, 0.200, 0.000) + vec3(1e-4));
    vec3 torch_chroma   = normalize(vec3(1.000, 0.480, 0.000) + vec3(1e-4));
    vec3 lantern_chroma = normalize(vec3(1.000, 0.850, 0.500) + vec3(1e-4));
    vec3 c_chroma       = normalize(max(color, vec3(0.0)) + vec3(1e-4));

    if (dot(c_chroma, lava_chroma) > 0.995) {
        // Lava, and only lava. A tight threshold on purpose: 0.995 on unit
        // vectors is ~5.7 degrees. The measured separations are
        //     lava <-> torch emitter    11.5 deg  (dot 0.980)
        //     lava <-> Fire biome lamp   9.3 deg  (dot 0.987)
        //     lava <-> lantern          33.1 deg  (dot 0.838)
        // so 5.7 deg sits inside the narrowest gap with room for the brightness
        // swing the lava sheet shows under bloom, while still admitting lava
        // itself at exactly 1.0. Re-derive these before widening the threshold.
        mat.id = 2;
        mat.emissive = 1.0;
        mat.roughness = 1.0;
        return mat;
    }
    if (dot(c_chroma, torch_chroma) > 0.99 || dot(c_chroma, lantern_chroma) > 0.99) {
        // The warm point emitters the game actually uses - torches, campfires
        // and lanterns. This is the branch that was previously unreachable. It
        // is a warmed-metal look, NOT true gold: the palette contains no gold
        // colour, so the material is named for what the data supports.
        mat.id = 3;
        mat.roughness = 0.32;
        mat.metalness = 0.55;
        mat.F0 = color;
        return mat;
    }
    if (color.g > color.r * 1.15 && color.g > color.b * 1.15 && color.g > 0.20) {
        // Foliage. NOTE: real water RGB(0.000, 0.376, 0.256) also satisfies this
        // test (0.376 > 0.256 * 1.15). It is correct today only because
        // is_water_chroma() runs FIRST and returns above. Do not reorder these
        // two tests - doing so hands real water subsurface scattering instead of
        // a reflection, which is the bug documented above is_water_chroma.
        mat.id = 4;
        mat.roughness = 0.85;
        return mat;
    }
    return mat;
}

float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float num = a2;
    float denom = (NoH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;
    return num / max(denom, 0.0001);
}

float G_SchlickGGX(float NoV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float num = NoV;
    float denom = NoV * (1.0 - k) + k;
    return num / max(denom, 0.0001);
}

float G_Smith(float NoV, float NoL, float roughness) {
    float ggx2 = G_SchlickGGX(NoV, roughness);
    float ggx1 = G_SchlickGGX(NoL, roughness);
    return ggx1 * ggx2;
}

vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 compute_pbr_shading(vec3 albedo, vec3 normal, Material mat) {
    vec3 N = normal;
    vec3 V = vec3(0.0, 0.0, 1.0);
    
    vec2 live_dir = (u_light0_diffuse.r + u_light0_diffuse.g + u_light0_diffuse.b > 0.05)
                     ? compute_light_dir_ss() : u_shadow_light_dir;
    vec3 L = normalize(vec3(live_dir, 0.5));
    vec3 H = normalize(V + L);
    
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    
    float D = D_GGX(NoH, mat.roughness);
    float G = G_Smith(NoV, NoL, mat.roughness);
    vec3 F = F_Schlick(VoH, mat.F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - mat.metalness;
    
    vec3 light_color = u_light0_diffuse.rgb;
    float luma_light = dot(light_color, vec3(0.299, 0.587, 0.114));
    if (luma_light < 0.35) {
        light_color = mix(light_color, vec3(0.7), 0.7 * (1.0 - luma_light / 0.35));
    }
    
    vec3 diffuse = kD * albedo;
    vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 0.001);
    specular = clamp(specular, vec3(0.0), vec3(1.5));
    
    // ── Modulate. Do NOT replace. ────────────────────────────────────────
    // This used to return a whole replacement lighting solution, mixed 52% over
    // the rendered frame:
    //     final_color = mix(albedo * 0.95 + specular * 0.25,
    //                       (diffuse + specular * pi) * light_color * (0.28 + 0.72*NoL)
    //                       + ambient * 0.20,
    //                       0.52);
    // Two things were wrong with that, and together they are why the High preset
    // reads as blown plastic next to Medium, which never enables this path:
    //   * It re-lit pixels that already carry the guest's own lighting - the
    //     level's authored GL_LIGHT0..7 are applied by the translation layer and
    //     the level bakes light into its textures. That is the same stacking
    //     defect the non-PBR branch below was fixed for; switching PBR on just
    //     turned it back on for the presets that enable it.
    //   * The specular term was clamped to 1.5 and then multiplied by pi, so a
    //     single glint contributed up to 4.71 into a 0..1 image, and the 0.52 mix
    //     put about half of that onto every highlight. Mid-tones then sat near
    //     1.0 and everything bright ran into the tone curve's asymptote.
    // What the BRDF is allowed to contribute here is a bounded highlight: a
    // sheen whose strength comes from roughness (a smooth surface glints, a
    // rough one does not) scaled by metalness and tinted by the live light.
    // light_color is deliberately the luminance-floored tint computed above, so
    // a dimly lit cave still produces a visible sheen instead of nothing.
    // The diffuse term and the ambient term are intentionally unused: the frame
    // already has both of them, applied correctly and per pixel, from the level.
    vec3 highlight = specular * (0.35 * (1.0 - mat.roughness)) * (1.0 + mat.metalness);
    vec3 final_color = albedo + clamp(highlight, vec3(0.0), vec3(0.5)) * light_color * 0.30;
    
    if (mat.emissive > 0.5) {
        final_color = max(final_color, albedo * 1.35);
    }
    return final_color;
}

float compute_shadow_at(vec2 uv) {
    float my_depth = linearize_depth(texture(u_depth, uv).r);
    if (my_depth > 0.95) return 1.0;
    
    float shadow = 1.0;
    vec2 live_dir = (u_light0_diffuse.r + u_light0_diffuse.g + u_light0_diffuse.b > 0.05)
                     ? compute_light_dir_ss() : u_shadow_light_dir;
    vec3 ray_dir_3d = normalize(vec3(live_dir, u_shadow_sun_z));
    vec2 step_uv  = ray_dir_3d.xy * u_shadow_softness;
    float step_z   = ray_dir_3d.z  * u_shadow_softness;
    
    float dither = rand(uv);
    int steps = u_pbr_enabled > 0.5 ? 18 : 12;
    
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
    return shadow;
}

float compute_shadow_pcf() {
    if (u_pbr_enabled < 0.5) {
        float eps_x = 1.2 / u_tex_size.x;
        float eps_y = 1.2 / u_tex_size.y;
        float s_c = compute_shadow_at(v_uv);
        float s_r = compute_shadow_at(v_uv + vec2(eps_x, 0.0));
        float s_l = compute_shadow_at(v_uv - vec2(eps_x, 0.0));
        float s_u = compute_shadow_at(v_uv + vec2(0.0, eps_y));
        float s_d = compute_shadow_at(v_uv - vec2(0.0, eps_y));
        return (s_c * 4.0 + s_r + s_l + s_u + s_d) / 8.0;
    } else {
        float s_c = compute_shadow_at(v_uv);
        float eps_x = 2.0 * u_shadow_softness / u_tex_size.x;
        float eps_y = 2.0 * u_shadow_softness / u_tex_size.y;
        
        float angle = rand(v_uv) * 6.28318;
        vec2 dir = vec2(cos(angle), sin(angle)) * 0.7;
        
        float s_r = compute_shadow_at(v_uv + vec2(dir.x * eps_x, dir.y * eps_y));
        float s_l = compute_shadow_at(v_uv - vec2(dir.x * eps_x, dir.y * eps_y));
        float s_u = compute_shadow_at(v_uv + vec2(-dir.y * eps_x, dir.x * eps_y));
        float s_d = compute_shadow_at(v_uv - vec2(-dir.y * eps_x, dir.x * eps_y));
        
        return (s_c * 2.0 + s_r + s_l + s_u + s_d) / 6.0;
    }
}

vec3 compute_water_reflection(vec3 current_scene, float depth, vec3 color) {
    float water_level = v_uv.y;
    for (float offset = 0.002; offset < 0.12; offset += 0.004) {
        vec2 sample_uv = v_uv + vec2(0.0, offset);
        vec3 c = texture(u_scene, sample_uv).rgb;
        bool is_still_water = is_water_chroma(c);
        if (!is_still_water) {
            water_level = sample_uv.y;
            break;
        }
    }
    
    vec2 reflect_uv = vec2(v_uv.x, water_level + (water_level - v_uv.y) * 0.85);
    reflect_uv.x += sin(v_uv.y * 65.0 + u_time * 3.5) * 0.0025;
    reflect_uv.y += cos(v_uv.x * 65.0 + u_time * 3.5) * 0.0018;
    reflect_uv = clamp(reflect_uv, vec2(0.0), vec2(1.0));
    
    vec3 reflected_color = texture(u_scene, reflect_uv).rgb;
    float shoreline_dist = clamp((water_level - v_uv.y) * 12.0, 0.0, 1.0);
    float reflection_blend = u_reflection_intensity * shoreline_dist;
    
    return mix(current_scene, reflected_color, reflection_blend);
}

// ACES Filmic Tonemapping (Narkowicz 2015)
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// --- Tone curve discipline ---------------------------------------------------
// ACES Narkowicz above is a SCENE-referred curve, and it is being applied here
// to a DISPLAY-referred signal: the game framebuffer is already sRGB-encoded and
// already has its lighting baked in. Two consequences, both of which are visible
// as the complaints "dark is too dark" and "bright is too bright":
//
//  * The toe. For small x the fit collapses to (b*x)/e = 0.03x/0.14 ~ 0.21x, so
//    a pixel at 0.02 leaves at 0.012 and a cave floor loses ~80% of its value.
//    The game's unlit areas are exactly the pixels that live in that toe, so the
//    crush lands on the regions that were already darkest.
//  * The asymptote. The fit is past 0.98 by x=5 and reaches 1.0 near x=6, and the
//    additive terms below (god rays, and bloom * intensity * 2.5 on emissives)
//    can push a torch core far beyond that. Core and fringe then both pin to
//    white and the glow reads as a flat disc - "bloom is not filled" - because
//    there is no gradient left for the curve to resolve.
//
// So keep ACES for its shoulder and fade it out below the toe knee, and give the
// additive terms their own asymptotic shoulder before the curve (add_glow).
const float TONEMAP_TOE   = 0.18;  // below this input luma, stay linear
const float GLOW_HEADROOM = 1.60;  // additive 'stop' before the highlight rolloff

float tm_luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 tonemap(vec3 x) {
    vec3 curve  = ACESFilm(x * 1.06);
    float  knee = smoothstep(0.0, TONEMAP_TOE, tm_luma(x));
    return mix(x, curve, knee);
}

// Additive light is folded in with a shoulder instead of straight into the
// curve's asymptote: monotonic, so a brighter emitter still comes out brighter,
// but 1x and 6x glow no longer both arrive at 1.0.
vec3 add_glow(vec3 sum, vec3 glow) {
    glow = max(glow, vec3(0.0));
    return sum + glow / (1.0 + tm_luma(glow) / GLOW_HEADROOM);
}

// ── The level's lights, accumulated per pixel ───────────────────────────────
// The frame arriving here is ALREADY LIT (see the note in main()), so this must
// not shade it again - that stacking is what made the High preset look blown.
// What the level's lights add is their own glow: the part the guest's per-vertex
// pipeline cannot express at all, because a Gouraud light is evaluated at
// VERTICES. A torch's falloff across a large floor quad is simply not present in
// that data, which is why the glow around a flame reads as a hard-edged disc
// instead of a gradient. Reconstructing it per pixel is additive and is bounded
// by add_glow() below, so it fills the halo rather than flattening what is under
// it.
//
// Known limitation: there is no depth test against the light. A light whose
// reach extends past a wall will glow through it, because converting the light's
// clip-space depth and the shader's linearised depth into one comparable space
// needs a convention neither side currently pins down, and guessing one here
// would trade a visible glow-through for invisible wrong occlusion.
vec3 accumulate_engine_lights(vec2 uv) {
    if (u_light_count <= 0) return vec3(0.0);

    float aspect = u_tex_size.x / max(u_tex_size.y, 1.0);
    vec3 accum = vec3(0.0);

    for (int i = 0; i < MAX_DYNAMIC_LIGHTS; ++i) {
        if (i >= u_light_count) break;

        float reach = u_lights[i].w;
        if (reach <= 0.0) continue;

        // EYE space -> clip. No modelview: the stored position is already through
        // the one the fixed-function pipeline lit with.
        vec4 clip = u_projection * vec4(u_lights[i].xyz, 1.0);
        if (clip.w <= 1e-4) continue;
        vec2 light_uv = (clip.xy / clip.w) * 0.5 + 0.5;

        // Screen-space reach in UV, from the same projection scale the host used
        // to rank the light. Axis-aligned scale only, deliberately: a reach is a
        // sphere, so its screen span comes from the projection's scale terms and
        // not from any rotation.
        float r_uv = max(abs(u_projection[0][0] * reach / clip.w),
                         abs(u_projection[1][1] * reach / clip.w)) * 0.5;
        if (r_uv <= 1e-6) continue;

        // Normalised screen distance: 1.0 exactly at the light's reach.
        vec2 d = (uv - light_uv) * vec2(aspect, 1.0);
        float nd = length(d) / r_uv;
        if (nd >= 1.0) continue;

        // The level's OWN falloff, from the component's authored attenuation:
        // 1 / (kc + kl*wd + kq*wd^2), normalised to 1 at the centre, with wd the
        // world distance recovered from the screen distance. This is what makes a
        // light fall off over its authored range instead of an invented one.
        float kc = max(u_light_atten[i].x, 1e-4);
        float kl = u_light_atten[i].y;
        float kq = u_light_atten[i].z;
        float wd = nd * reach;
        float att = kc / (kc + kl * wd + kq * wd * wd);
        if (att <= 0.001) continue;

        // Soften the outer edge, so the cutoff cannot draw a visible rim where nd
        // crosses 1 - that rim is the hard-edged disc this replaces.
        att *= smoothstep(1.0, 0.72, nd);

        accum += u_light_color[i].rgb * (u_light_color[i].a * att);
    }
    return accum;
}

void main() {
    // ---- UI/HUD passthrough --------------------------------------------------
    // Two ways in. u_ui_pass bypasses the entire pass (a frame with no 3D draw
    // at all). The stencil mask does it per PIXEL inside a mixed frame, which is
    // what the split has to be: relighting a HUD sprite with the 3D light
    // handshake is wrong, and the UI has to be written here rather than left to
    // whatever the destination buffer already held - otherwise the region the
    // composite skips shows up as a stale rectangle exactly where the UI is.
    // The stencil is an 8-bit integer, so stencil 1 arrives normalised as
    // 1/255 = 0.0039, not 1.0; the test is "any non-zero stencil".
    if (u_ui_pass > 0.5) {
        FragColor = texture(u_scene, v_uv);
        return;
    }
    if (u_ui_exempt > 0.5 && texture(u_ui_mask, v_uv).g >= 0.002) {
        FragColor = texture(u_scene, v_uv);
        return;
    }
    float eps_x = 1.0 / u_tex_size.x;
    float eps_y = 1.0 / u_tex_size.y;
    
    float h_c  = dot(texture(u_scene, v_uv).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_r  = dot(texture(u_scene, v_uv + vec2(eps_x, 0.0)).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_l  = dot(texture(u_scene, v_uv - vec2(eps_x, 0.0)).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_u  = dot(texture(u_scene, v_uv + vec2(0.0, eps_y)).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_d  = dot(texture(u_scene, v_uv - vec2(0.0, eps_y)).rgb, vec3(0.2126, 0.7152, 0.0722));
    
    vec2 bump = vec2(h_r - h_l, h_u - h_d);
    vec2 displaced_uv = v_uv;
    
    vec3 albedo = texture(u_scene, v_uv).rgb;
    vec3 normal = normalize(vec3(-bump.x * 2.5, -bump.y * 2.5, 1.0));
    
    float d_c = linearize_depth(texture(u_depth, v_uv).r);
    Material mat = get_material(albedo, d_c);
    
    vec3 scene;
    if (u_pbr_enabled > 0.5) {
        scene = compute_pbr_shading(albedo, normal, mat);
    } else {
        // ── Do NOT re-light an already-lit frame ────────────────────────────
        // The pixels arriving here are lit already: the guest is fixed-function
        // GLES1, so its own lighting (the engine's real GL_LIGHT0..7 with real
        // positions and colours) is reproduced either by the translation layer
        // in ffp_forward.cpp or by the driver's fixed-function pipeline, and the
        // game bakes level lighting into the level textures on top of that.
        //
        // This branch used to apply a SECOND model to those pixels, and it was a
        // near-constant one: (0.88 + 0.24*diff) spans only 0.88..1.12, so it
        // could not produce lighting - all it did was multiply whatever the real
        // lighting had produced by a number close to 1, flattening the contrast
        // and washing out the shading. That is the "flat vanilla lighting" and
        // the double-lighting report: the upgrade path was lighting the scene
        // and this was flattening it straight afterwards, in the same frame.
        //
        // Pass the rendered frame through untouched. Relighting stays available,
        // but it is opt-in and explicit (u_pbr_enabled), never a default on top
        // of a frame that already has its light baked in.
        scene = albedo;
    }
    
    // Apply SSAO
    if (u_ao_enabled > 0.5 && mat.emissive < 0.5) {
        float ao = texture(u_ao, v_uv).r;
        ao = mix(ao, 1.0, u_shadow_lift);
        scene *= ao;
    }
    
    // Apply screen-space shadows
    if (u_shadow_enabled > 0.5 && mat.emissive < 0.5) {
        float shadow = compute_shadow_pcf();
        scene *= shadow;
    }
    
    // Water reflections
    if (u_reflections_enabled > 0.5 && mat.id == 1) {
        scene = compute_water_reflection(scene, d_c, albedo);
    }
    
    // Foliage SSS
    if (u_pbr_enabled > 0.5 && mat.id == 4) {
        vec2 live_dir = (u_light0_diffuse.r + u_light0_diffuse.g + u_light0_diffuse.b > 0.05)
                         ? compute_light_dir_ss() : u_shadow_light_dir;
        vec3 L = normalize(vec3(live_dir, 0.5));
        float sss = clamp(dot(normal, -L), 0.0, 1.0) * 0.12;
        scene += vec3(0.08, 0.16, 0.06) * sss * u_light0_diffuse.rgb;
    }
    
    // Ambient lighting
    float luma = dot(scene, vec3(0.2126, 0.7152, 0.0722));
    float shadow_mask = smoothstep(0.4, 0.1, luma);
    vec3 sky_ambient  = vec3(u_ambient_r, u_ambient_g, u_ambient_b);
    vec3 guest_ambient = u_light_model_amb.rgb;
    vec3 tot_ambient = mix(sky_ambient, sky_ambient * guest_ambient * 3.0, 0.35);
    scene += tot_ambient * shadow_mask * 0.10;
    
    // Material tint
    vec3 mat_tint = get_material_tint();
    float dark_mask = smoothstep(0.5, 0.1, luma);
    scene = mix(scene, scene * mat_tint, dark_mask * 0.25);
    
    // God rays
    if (u_gr_enabled > 0.5) {
        vec3 rays = texture(u_godrays, v_uv).rgb;
        vec3 tint = vec3(u_gr_tint_r, u_gr_tint_g, u_gr_tint_b);
        tint = mix(tint, u_light0_diffuse.rgb, 0.25);
        scene = add_glow(scene, rays * tint);
    }
    
    // Bloom
    if (u_bloom_enabled > 0.5) {
        vec3 bloom = texture(u_bloom, v_uv).rgb;
        float boost = mat.emissive > 0.5 ? 2.5 : 1.0;
        scene = add_glow(scene, bloom * u_bloom_intensity * boost);
    }

    // The level's own lights, filling in the glow the per-vertex pipeline cannot
    // express. Bounded by add_glow(), so this cannot blow the frame out.
    if (u_light_count > 0) {
        scene = add_glow(scene, accumulate_engine_lights(v_uv));
    }
    
    // Filmic tonemapping with the toe bypassed for display-referred input
    scene = tonemap(scene);
    
    // Saturation & Vibrance Tuning
    float luma2 = dot(scene, vec3(0.2126, 0.7152, 0.0722));
    scene = mix(vec3(luma2), scene, 1.08);
    
    FragColor = vec4(clamp(scene, 0.0, 1.0), 1.0);
}
