#define GL_GLEXT_PROTOTYPES
#include "fbo_scaler.h"
#include "platform/gl_inc.h"
#include <GL/glext.h>
#include "pvr_loader.h"
#include "platform/data_path.h"
#include "jni/gl_render_state.h"   // GL state handshake — written by bridge, read here
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "graphics/LightManager.h"
#include "graphics/TextureAwareMapper.h"
#include "platform/ffp_forward.h"   // the layer that owns lighting; see PostFXState::ffp_lighting_level

// ============================================================
//  FBO + Multi-Pass Post-Processing Pipeline
// ============================================================

static GLuint g_fbo       = 0;
static GLuint g_fbo_tex   = 0;
static GLuint g_fbo_depth_tex = 0;  // Depth as TEXTURE (not renderbuffer!) for sampling
static int    g_game_w    = 960;
static int    g_game_h    = 544;
static bool   g_fbo_ok    = false;

// Intermediate FBOs
static GLuint g_postfx_fbo  = 0, g_postfx_tex  = 0;  // Color PostFX output / composite
static GLuint g_postfx_fbo_b = 0, g_postfx_tex_b = 0; // Second full-res FBO for ping-pong
static GLuint g_half_fbo_a  = 0, g_half_tex_a  = 0;  // Half-res ping
static GLuint g_half_fbo_b  = 0, g_half_tex_b  = 0;  // Half-res pong
static GLuint g_bloom_fbo_a = 0, g_bloom_tex_a = 0;  // Bloom ping (quarter-res)
static GLuint g_bloom_fbo_b = 0, g_bloom_tex_b = 0;  // Bloom pong (quarter-res)
static int    g_half_w = 0, g_half_h = 0;
static int    g_quarter_w = 0, g_quarter_h = 0;
static float  g_postfx_time = 0.0f;

// Background data from SRE — host writes these each frame
float g_bg_depth = 11000.0f;       // Distance to background plane
float g_bg_scale = 1.0f;           // Background sprite scale
float g_bg_ambient_r = 0.6f;       // Sky ambient color (warm golden default)
float g_bg_ambient_g = 0.5f;
float g_bg_ambient_b = 0.4f;

// Portal effect data from SRE — host writes these each frame
float g_portal_active = 0.0f;          // 1.0 when portal is being drawn
float g_portal_world_x = 0.0f;        // Portal world position (from Matrix4)
float g_portal_world_y = 0.0f;
float g_portal_world_z = 0.0f;
float g_portal_color_r = 0.5f;        // Portal color from game level data
float g_portal_color_g = 0.2f;
float g_portal_color_b = 0.8f;
float g_portal_intensity = 0.8f;      // Glow strength
float g_portal_speed = 1.0f;          // Animation speed from component
float g_portal_vp_matrix[16] = {      // MV matrix from Draw call for positioning
    1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
};

// GUI detection — set by GL bridge (glOrthof/glFrustumf), read by PostFX pipeline
// When g_frame_has_ortho > 0 && g_frame_has_perspective == 0, game is in full-screen menu
int g_frame_has_ortho = 0;
int g_frame_has_perspective = 0;

// Shader programs
static GLuint g_prog_sharp   = 0;
static GLuint g_prog_nearest = 0;
static GLuint g_prog_crt     = 0;
static GLuint g_prog_postfx  = 0;
static GLuint g_prog_godrays = 0;
static GLuint g_prog_ssao    = 0;
static GLuint g_prog_blur    = 0;
static GLuint g_prog_composite = 0;
static GLuint g_prog_bloom_extract = 0;
static GLuint g_prog_fsr     = 0;
static GLuint g_prog_portal  = 0;   // Portal quad renderer

// Portal texture (loaded from PVR)
static GLuint g_portal_tex    = 0;   // Portal swirl texture (portal_effect_2x.pvr)
static bool   g_portal_tex_loaded = false;

// Fullscreen quad VAO/VBO (Optimization 2)
static GLuint g_fsq_vao = 0, g_fsq_vbo = 0;

// ======================= CACHED UNIFORM LOCATIONS =======================
// (Optimization 1) — eliminates ~81 glGetUniformLocation calls per frame

// g_prog_ssao uniforms
static GLint loc_ssao_depth = -1;
static GLint loc_ssao_tex_size = -1;
static GLint loc_ssao_radius = -1;
static GLint loc_ssao_intensity = -1;
static GLint loc_ssao_time = -1;
static GLint loc_ssao_near_plane = -1;
static GLint loc_ssao_far_plane = -1;

// g_prog_blur uniforms
static GLint loc_blur_tex = -1;
static GLint loc_blur_depth = -1;
static GLint loc_blur_direction = -1;
static GLint loc_blur_use_depth = -1;
static GLint loc_blur_near_plane = -1;
static GLint loc_blur_far_plane = -1;

// g_prog_godrays uniforms
static GLint loc_godrays_tex = -1;
static GLint loc_godrays_depth = -1;
static GLint loc_godrays_sun_pos = -1;
static GLint loc_godrays_intensity = -1;
static GLint loc_godrays_decay = -1;
static GLint loc_godrays_density = -1;
static GLint loc_godrays_exposure = -1;
static GLint loc_godrays_tex_size = -1;
static GLint loc_godrays_time = -1;
static GLint loc_godrays_near = -1;
static GLint loc_godrays_far = -1;

// g_prog_bloom_extract uniforms
static GLint loc_bloom_extract_tex = -1;
static GLint loc_bloom_extract_threshold = -1;
static GLint loc_bloom_extract_tex_size = -1;
static GLint loc_bloom_extract_ui_mask = -1;
static GLint loc_bloom_extract_ui_exempt = -1;
// Authored-emitter bloom key. See src/shaders/graphics/bloom_extract.glsl.
static GLint loc_bloom_extract_emitter_keyed = -1;
static GLint loc_bloom_extract_emitter_count = -1;
static GLint loc_bloom_extract_emitter = -1;
static GLint loc_bloom_extract_emitter_luma = -1;
static GLint loc_bloom_extract_emitter_sat = -1;
static GLint loc_bloom_extract_emitter_contrast = -1;
static GLint loc_bloom_extract_emitter_gain = -1;

// g_prog_composite uniforms
static GLint loc_comp_scene = -1;
static GLint loc_comp_ao = -1;
static GLint loc_comp_godrays = -1;
static GLint loc_comp_bloom = -1;
static GLint loc_comp_depth = -1;
static GLint loc_comp_ao_enabled = -1;
static GLint loc_comp_gr_enabled = -1;
static GLint loc_comp_bloom_enabled = -1;
static GLint loc_comp_bloom_intensity = -1;
static GLint loc_comp_gr_tint_r = -1;
static GLint loc_comp_gr_tint_g = -1;
static GLint loc_comp_gr_tint_b = -1;
static GLint loc_comp_shadow_enabled = -1;
static GLint loc_comp_shadow_intensity = -1;
static GLint loc_comp_shadow_softness = -1;
static GLint loc_comp_shadow_light_dir = -1;
static GLint loc_comp_shadow_sun_z = -1;
static GLint loc_comp_shadow_near = -1;
static GLint loc_comp_shadow_far = -1;
static GLint loc_comp_shadow_lift = -1;
static GLint loc_comp_bg_depth = -1;
static GLint loc_comp_bg_scale = -1;
static GLint loc_comp_ambient_r = -1;
static GLint loc_comp_ambient_g = -1;
static GLint loc_comp_ambient_b = -1;
static GLint loc_comp_tex_size = -1;
static GLint loc_comp_pbr_enabled = -1;
static GLint loc_comp_reflections_enabled = -1;
static GLint loc_comp_reflection_intensity = -1;
static GLint loc_comp_water_surface = -1;
static GLint loc_comp_water_valid = -1;
static GLint loc_comp_time = -1;
static GLint loc_comp_ui_pass = -1;
static GLint loc_comp_ui_mask = -1;
static GLint loc_comp_ui_exempt = -1;

// g_prog_postfx uniforms
static GLint loc_postfx_tex = -1;
static GLint loc_postfx_depth_tex = -1;
static GLint loc_postfx_tex_size = -1;
static GLint loc_postfx_time = -1;
static GLint loc_postfx_vignette = -1;
static GLint loc_postfx_grain = -1;
static GLint loc_postfx_ca_offset = -1;
static GLint loc_postfx_saturation = -1;
static GLint loc_postfx_contrast = -1;
static GLint loc_postfx_brightness = -1;
static GLint loc_postfx_warmth = -1;
static GLint loc_postfx_sharpen = -1;
static GLint loc_postfx_outline_enabled = -1;
static GLint loc_postfx_outline_thickness = -1;
static GLint loc_postfx_outline_intensity = -1;
static GLint loc_postfx_outline_depth_threshold = -1;
static GLint loc_postfx_outline_near = -1;
static GLint loc_postfx_outline_far = -1;

// g_prog_portal uniforms
static GLint loc_portal_portal_tex = -1;
static GLint loc_portal_mv_matrix = -1;
static GLint loc_portal_aspect = -1;
static GLint loc_portal_color = -1;
static GLint loc_portal_time = -1;
static GLint loc_portal_speed = -1;
static GLint loc_portal_alpha = -1;

// g_prog_composite — game-engine handshake uniforms
// (driven by live GL state captured from the game's own GL calls)
static GLint loc_comp_light0_pos      = -1; // vec4 — GL_LIGHT0 world position
static GLint loc_comp_light0_diffuse  = -1; // vec4 — GL_LIGHT0 diffuse color
static GLint loc_comp_light0_ambient  = -1; // vec4 — GL_LIGHT0 ambient color
static GLint loc_comp_mat_diffuse     = -1; // vec4 — current material diffuse
static GLint loc_comp_cur_color       = -1; // vec4 — current glColor4 vertex color
static GLint loc_comp_player_world    = -1; // vec3 — hero world-space position
static GLint loc_comp_modelview       = -1; // mat4 — current modelview shadow
static GLint loc_comp_projection      = -1; // mat4 — current projection shadow
static GLint loc_comp_light_model_amb = -1; // vec4 — glLightModel ambient

// ── Dynamic light buffer (the level's own point lights) ────────────────────
// How many of the frame's collected lights the composite accumulates per pixel.
// LightManager can hold 32, but the shader pays for every entry it loops over
// and only a handful can matter on one screen, so this is where the cost is
// bounded. MUST match MAX_DYNAMIC_LIGHTS in composite.glsl (a uniform array's
// size is part of the interface: if these disagree the uniform is not found and
// the whole feature silently does nothing).
static const int MAX_DYNAMIC_LIGHTS = 8;
static GLint loc_comp_light_count = -1;  // int      — live u_lights[] entries
static GLint loc_comp_lights      = -1;  // vec4[8]  — xyz = EYE-space pos, w = reach
static GLint loc_comp_light_color = -1;  // vec4[8]  — rgb = linear colour, a = intensity
static GLint loc_comp_light_atten = -1;  // vec3[8]  — engine's (kc, kl, kq)

// ======================= SHADER SOURCES =======================

static const char* VERT_SRC = R"GLSL(
#version 430 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)GLSL";

// ----- Portal Quad Vertex Shader -----
// Uses the game's model-view matrix (from PortalEffectComponent::Draw)
// combined with a perspective projection (FOV=20°, near=50, far=20000)
// to correctly position the portal quad in screen space.
static const char* VERT_PORTAL = R"GLSL(
#version 430 core
layout(location = 0) in vec2 a_pos;   // -1..1 quad corners
layout(location = 1) in vec2 a_uv;    // 0..1
uniform mat4 u_mv_matrix;             // model-view matrix from game Draw call
uniform float u_aspect;               // viewport aspect ratio (w/h)
out vec2 v_uv;

void main() {
    // Portal quad in LOCAL space — sized to fill the entire stone archway.
    vec3 local_pos = vec3(a_pos.x * 55.0, a_pos.y * 75.0, 0.0);
    
    // Transform to view space using the computed Model-View matrix
    // (VIEW * WorldTranslation, confirmed by Scene::Draw decompilation)
    vec4 view_pos = u_mv_matrix * vec4(local_pos, 1.0);
    
    // Apply perspective projection (matching the game's camera)
    // FOV = 0.34906584 rad ≈ 20°, near = 50, far = 20000
    float fov = 0.34906584;
    float near = 50.0;
    float far = 20000.0;
    float f = 1.0 / tan(fov * 0.5);
    
    // Perspective projection matrix elements
    float p00 = f / u_aspect;   // X scale
    float p11 = f;              // Y scale  
    float p22 = -(far + near) / (far - near);
    float p23 = -2.0 * far * near / (far - near);
    
    gl_Position = vec4(
        view_pos.x * p00,
        view_pos.y * p11,
        view_pos.z * p22 + p23,
        -view_pos.z    // perspective divide (camera looks down -Z)
    );
    
    v_uv = a_uv;
}
)GLSL";

// ----- Portal Quad Fragment Shader -----
// Samples the portal texture with animated UV swirl.
static const char* FRAG_PORTAL = R"GLSL(
#version 430 core
uniform sampler2D u_portal_tex;
uniform vec3  u_color;         // portal tint color
uniform float u_time;          // animation time
uniform float u_speed;         // animation speed from game data
uniform float u_alpha;         // overall portal alpha/intensity

in vec2 v_uv;
out vec4 FragColor;

void main() {
    // Center UV for polar coordinate swirl
    vec2 centered = v_uv - 0.5;
    float dist = length(centered);
    float angle = atan(centered.y, centered.x);
    
    // Swirl: rotate UVs based on distance from center + time
    float swirl_amount = (1.0 - dist * 2.0) * 2.0;  // stronger swirl at center
    float t = u_time * u_speed;
    angle += swirl_amount * sin(t * 0.7) + t * 0.5;
    
    // Radial pull toward center
    float warped_dist = dist + sin(dist * 8.0 - t * 2.0) * 0.03;
    
    // Convert back to UV
    vec2 swirled = vec2(cos(angle), sin(angle)) * warped_dist + 0.5;
    
    // Sample portal texture with swirled UVs
    vec3 tex = texture(u_portal_tex, swirled).rgb;
    
    // Second layer: slower rotation for depth
    float angle2 = atan(centered.y, centered.x) - t * 0.3 + 1.57;
    vec2 swirled2 = vec2(cos(angle2), sin(angle2)) * warped_dist * 1.1 + 0.5;
    vec3 tex2 = texture(u_portal_tex, swirled2).rgb;
    
    // Blend layers
    vec3 portal = mix(tex, tex2, 0.4);
    
    // Apply portal color tint
    portal *= u_color;
    
    // Archway-shaped mask — wide ellipse matching the stone portal frame
    // Wider horizontally (1.0) than vertically (0.72) to match the arch shape
    vec2 ellipse = centered * vec2(1.0, 0.72);
    float eDist = length(ellipse);
    float edge_fade = smoothstep(0.50, 0.38, eDist);  // fills to the edges
    
    // Subtle center glow
    float center_glow = smoothstep(0.3, 0.0, eDist) * 0.3;
    portal += u_color * center_glow;
    
    // Pulsing intensity
    float pulse = 0.9 + 0.1 * sin(t * 2.5);
    
    FragColor = vec4(portal * pulse, edge_fade * u_alpha);
}
)GLSL";

static const char* FRAG_SHARP_BILINEAR = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform vec2 u_out_size;
in vec2 v_uv;
out vec4 FragColor;
void main() {
    vec2 scale    = u_out_size / u_tex_size;
    vec2 texel    = v_uv * u_tex_size;
    vec2 texel_f  = fract(texel);
    vec2 texel_i  = floor(texel) + 0.5;
    vec2 frange   = clamp(texel_f / fwidth(texel), 0.0, 1.0);
    vec2 uv_sharp = (texel_i + frange - 0.5) / u_tex_size;
    FragColor     = texture(u_tex, uv_sharp);
}
)GLSL";

static const char* FRAG_NEAREST = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform vec2 u_out_size;
in vec2 v_uv;
out vec4 FragColor;
void main() {
    vec2 uv = (floor(v_uv * u_tex_size) + 0.5) / u_tex_size;
    FragColor = texture(u_tex, uv);
}
)GLSL";

static const char* FRAG_CRT = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform vec2 u_out_size;
in vec2 v_uv;
out vec4 FragColor;
vec2 barrel(vec2 uv) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return uv + c * r2 * 0.04;
}
void main() {
    vec2 uv = barrel(v_uv);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0); return;
    }
    vec4 col = texture(u_tex, uv);
    float scanline = sin(uv.y * u_tex_size.y * 3.14159) * 0.5 + 0.5;
    col.rgb *= 0.75 + 0.25 * scanline;
    vec2 vig = uv * (1.0 - uv);
    col.rgb *= clamp(vig.x * vig.y * 15.0, 0.0, 1.0);
    FragColor = col;
}
)GLSL";

// ----- Tier 1 PostFX (color effects in single pass) -----
static const char* FRAG_POSTFX = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform sampler2D u_depth_tex;  // depth buffer for outlines
uniform vec2 u_tex_size;
uniform float u_time;
uniform float u_vignette, u_grain, u_ca_offset;
uniform float u_saturation, u_contrast, u_brightness, u_warmth, u_sharpen;
// Outline uniforms
uniform float u_outline_enabled;
uniform float u_outline_thickness;
uniform float u_outline_intensity;
uniform float u_outline_depth_threshold;
uniform float u_outline_near;
uniform float u_outline_far;

in vec2 v_uv;
out vec4 FragColor;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float lin_depth(float d) {
    float n = u_outline_near;
    float f = u_outline_far;
    return (2.0 * n) / (f + n - d * (f - n));
}

float detect_edge() {
    vec2 px = u_outline_thickness / u_tex_size;
    
    // Sample depth in a 3x3 cross pattern (Sobel-like)
    float d_c = lin_depth(texture(u_depth_tex, v_uv).r);
    float d_l = lin_depth(texture(u_depth_tex, v_uv + vec2(-px.x, 0)).r);
    float d_r = lin_depth(texture(u_depth_tex, v_uv + vec2( px.x, 0)).r);
    float d_u = lin_depth(texture(u_depth_tex, v_uv + vec2(0,  px.y)).r);
    float d_d = lin_depth(texture(u_depth_tex, v_uv + vec2(0, -px.y)).r);
    
    // Depth gradient (Sobel approximation)
    float edge_h = abs(d_l - d_r);
    float edge_v = abs(d_u - d_d);
    float depth_edge = max(edge_h, edge_v);
    
    // Also detect color edges (for objects at same depth with different textures)
    vec3 c_c = texture(u_tex, v_uv).rgb;
    vec3 c_l = texture(u_tex, v_uv + vec2(-px.x, 0)).rgb;
    vec3 c_r = texture(u_tex, v_uv + vec2( px.x, 0)).rgb;
    vec3 c_u = texture(u_tex, v_uv + vec2(0,  px.y)).rgb;
    vec3 c_d = texture(u_tex, v_uv + vec2(0, -px.y)).rgb;
    
    float color_edge_h = length(c_l - c_r);
    float color_edge_v = length(c_u - c_d);
    float color_edge = max(color_edge_h, color_edge_v);
    
    // Combine: depth edges are primary, color edges are secondary
    float edge = smoothstep(u_outline_depth_threshold, u_outline_depth_threshold * 3.0, depth_edge);
    edge = max(edge, smoothstep(0.3, 0.6, color_edge) * 0.5);
    
    // Skip background (far depth = no outline on sky)
    if (d_c > 0.95) edge = 0.0;
    
    return edge;
}

void main() {
    vec2 uv = v_uv;
    vec2 px = 1.0 / u_tex_size;
    vec4 col;
    if (u_ca_offset > 0.0) {
        vec2 dir = (uv - 0.5) * u_ca_offset;
        col.r = texture(u_tex, uv - dir).r;
        col.g = texture(u_tex, uv).g;
        col.b = texture(u_tex, uv + dir).b;
        col.a = 1.0;
    } else {
        col = texture(u_tex, uv);
    }
    if (u_sharpen > 0.0) {
        vec4 blur = (texture(u_tex, uv + vec2(-px.x,0)) + texture(u_tex, uv + vec2(px.x,0)) +
                     texture(u_tex, uv + vec2(0,-px.y)) + texture(u_tex, uv + vec2(0,px.y))) * 0.25;
        col.rgb += (col.rgb - blur.rgb) * u_sharpen;
    }
    col.rgb += u_brightness;
    col.rgb = (col.rgb - 0.5) * u_contrast + 0.5;
    float lum = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    col.rgb = mix(vec3(lum), col.rgb, u_saturation);
    if (u_warmth != 0.0) { col.r += u_warmth * 0.05; col.b -= u_warmth * 0.05; }
    if (u_grain > 0.0) { col.rgb += (rand(uv + fract(u_time)) * 2.0 - 1.0) * u_grain; }
    if (u_vignette > 0.0) {
        vec2 vig = uv * (1.0 - uv);
        col.rgb *= pow(clamp(vig.x * vig.y * 15.0, 0.0, 1.0), u_vignette);
    }
    
    // Edge outlines — color-aware: darkened shade of surrounding color
    // Green areas → dark green outlines, brown → dark brown, blue → dark blue
    if (u_outline_enabled > 0.5) {
        float edge = detect_edge();
        if (edge > 0.01) {
            // Sample average color from neighbors for the outline tint
            vec2 opx = u_outline_thickness / u_tex_size;
            vec3 avg_color = (
                texture(u_tex, v_uv + vec2(-opx.x, 0)).rgb +
                texture(u_tex, v_uv + vec2( opx.x, 0)).rgb +
                texture(u_tex, v_uv + vec2(0, -opx.y)).rgb +
                texture(u_tex, v_uv + vec2(0,  opx.y)).rgb
            ) * 0.25;
            
            // Outline = darkened + slightly more saturated version of local color
            // This makes outlines feel like natural contour shadows
            float avg_luma = dot(avg_color, vec3(0.2126, 0.7152, 0.0722));
            vec3 outline_color = mix(vec3(avg_luma), avg_color, 1.3); // boost saturation 30%
            outline_color *= 0.3;  // darken to ~30% brightness
            
            col.rgb = mix(col.rgb, outline_color, edge * u_outline_intensity);
        }
    }
    
    FragColor = vec4(clamp(col.rgb, 0.0, 1.0), 1.0);
}
)GLSL";

// ----- 3D Screen-Space Volumetric Raymarching (Godot 4 / Unreal SSVR) -----
// Real 3D depth-tested volumetric shafts with Henyey-Greenstein anisotropic phase scattering,
// Interleaved Gradient Noise jittering, depth-buffer occlusion testing, and extinction falloff.
static const char* FRAG_GODRAYS = R"GLSL(
#version 430 core
uniform sampler2D u_tex;        // scene color
uniform sampler2D u_depth;      // depth texture (GL_DEPTH24_STENCIL8)
uniform vec2 u_sun_pos;         // sun position in UV space (e.g. 0.5, 1.1)
uniform float u_intensity;      // overall intensity
uniform float u_decay;          // per-step transmittance decay
uniform float u_density;        // step density/stride
uniform float u_exposure;       // final exposure multiply
uniform vec2 u_tex_size;        // render target resolution
uniform float u_time;           // animation time
uniform float u_near;           // near plane (50.0)
uniform float u_far;            // far plane (20000.0)

in vec2 v_uv;
out vec4 FragColor;

// Jorge Jimenez Interleaved Gradient Noise (IGN)
float ign(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

float linearize_depth(float d) {
    float n = (u_near > 0.1) ? u_near : 50.0;
    float f = (u_far > 100.0) ? u_far : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

// Henyey-Greenstein forward phase function (anisotropic atmospheric scattering)
float henyey_greenstein(float cos_theta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 / (4.0 * 3.14159265)) * ((1.0 - g2) / max(pow(denom, 1.5), 0.0001));
}

void main() {
    vec2 sun_uv = u_sun_pos;
    vec2 delta = sun_uv - v_uv;
    float dist = length(delta);
    
    // Surface depth at current pixel
    float raw_pixel_depth = texture(u_depth, v_uv).r;
    float lin_pixel_depth = linearize_depth(raw_pixel_depth);
    
    // Virtual sun depth (sky/background = 0.98 in linearized units)
    float sun_depth_lin = 0.98;
    
    // View and sun directions in view-space
    vec3 V = normalize(vec3(v_uv - 0.5, 1.0));
    vec3 L = normalize(vec3(delta, 0.4));
    float cos_theta = clamp(dot(V, L), -1.0, 1.0);
    float hg_phase = henyey_greenstein(cos_theta, 0.65) * 4.0 * 3.14159265;
    
    // Screen-space raymarching setup (48 steps for smooth gradients)
    const int NUM_STEPS = 48;
    vec2 step_dir = (delta / max(dist, 0.0001)) * min(dist, 0.85) / float(NUM_STEPS) * u_density;
    
    // Interleaved gradient noise dither to eliminate banding
    float dither = ign(gl_FragCoord.xy + fract(u_time * 0.05) * 16.0);
    
    float transmittance = 1.0;
    vec3 ray_accum = vec3(0.0);
    float step_decay = clamp(u_decay, 0.85, 0.995);
    
    // March from current pixel toward the sun
    for (int i = 0; i < NUM_STEPS; i++) {
        float t = (float(i) + dither) / float(NUM_STEPS);
        vec2 march_uv = v_uv + step_dir * (float(i) + dither);
        
        if (march_uv.x < 0.0 || march_uv.x > 1.0 || march_uv.y < 0.0 || march_uv.y > 1.0) {
            break;
        }
        
        float raw_sample_depth = texture(u_depth, march_uv).r;
        float geom_depth = linearize_depth(raw_sample_depth);
        
        // Interpolate ray depth from pixel surface to distant sun
        float ray_depth = mix(lin_pixel_depth, sun_depth_lin, t);
        
        // Depth-tested occlusion: if geometry is in front of the ray, light is blocked
        float is_occluded = step(geom_depth, ray_depth - 0.003);
        float visibility = 1.0 - is_occluded;
        
        // Light emission from background/bright surfaces
        vec3 sample_col = texture(u_tex, march_uv).rgb;
        float sky_mask = smoothstep(0.85, 1.0, raw_sample_depth);
        float luma = dot(sample_col, vec3(0.2126, 0.7152, 0.0722));
        float bright_mask = smoothstep(0.25, 0.85, luma);
        float emitter = mix(bright_mask * 0.35, 1.0, sky_mask);
        
        float dist_to_sun = length(march_uv - sun_uv);
        float radial_att = 1.0 / (1.0 + dist_to_sun * 1.5);
        
        ray_accum += sample_col * emitter * visibility * transmittance * radial_att;
        transmittance *= step_decay;
    }
    
    ray_accum = (ray_accum / float(NUM_STEPS)) * u_exposure * hg_phase * u_intensity;
    
    // Subtle horizon ambient haze
    float top_haze = smoothstep(0.65, 1.0, v_uv.y) * 0.08 * u_intensity;
    ray_accum += vec3(top_haze * 1.0, top_haze * 0.9, top_haze * 0.7);
    
    FragColor = vec4(clamp(ray_accum, 0.0, 1.0), 1.0);
}
)GLSL";


// ----- Godot 4 / Intel ASSAO (Adaptive Screen Space Ambient Occlusion) -----
// Multi-tap spiral sampling with bilateral normal reconstruction,
// horizon angle occlusion, and distance falloff.
static const char* FRAG_SSAO = R"GLSL(
#version 430 core
uniform sampler2D u_depth;    // depth texture
uniform vec2 u_tex_size;      // resolution
uniform float u_radius;       // sample radius
uniform float u_intensity;    // AO strength
uniform float u_time;         // for noise
uniform float u_near_plane;   // camera near plane (50.0)
uniform float u_far_plane;    // camera far plane  (20000.0)

in vec2 v_uv;
out vec4 FragColor;

float ign(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

float linearize_depth(float d) {
    float n = (u_near_plane > 0.1) ? u_near_plane : 50.0;
    float f = (u_far_plane > 100.0) ? u_far_plane : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

vec3 get_view_pos(vec2 uv) {
    float d = linearize_depth(texture(u_depth, uv).r);
    float aspect = u_tex_size.x / u_tex_size.y;
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * d * 0.17632698 * aspect, ndc.y * d * 0.17632698, d);
}

// Intel ASSAO 16-sample spiral disk pattern
const vec2 SAMPLE_DISK[16] = vec2[](
    vec2( 0.78488,  0.56661), vec2( 0.26022, -0.29575),
    vec2( 0.10459,  0.08372), vec2(-0.68286,  0.04963),
    vec2(-0.13570, -0.64190), vec2(-0.26193, -0.08205),
    vec2(-0.61177,  0.66664), vec2( 0.43675,  0.25119),
    vec2( 0.07884,  0.86618), vec2(-0.12790, -0.29869),
    vec2(-0.04031,  0.02413), vec2( 0.16201, -0.52851),
    vec2(-0.70991,  0.47301), vec2( 0.03277, -0.22349),
    vec2( 0.68921,  0.36800), vec2( 0.29251,  0.37775)
);

void main() {
    float depth = texture(u_depth, v_uv).r;
    if (depth > 0.999) {
        FragColor = vec4(1.0);
        return;
    }
    
    vec2 px = 1.0 / u_tex_size;
    vec3 pos_c = get_view_pos(v_uv);
    
    // Cross-depth reconstruction for bilateral normal estimation (Intel ASSAO)
    vec3 pos_r = get_view_pos(v_uv + vec2(px.x, 0.0));
    vec3 pos_l = get_view_pos(v_uv - vec2(px.x, 0.0));
    vec3 pos_u = get_view_pos(v_uv + vec2(0.0, px.y));
    vec3 pos_d = get_view_pos(v_uv - vec2(0.0, px.y));
    
    vec3 dx = (abs(pos_r.z - pos_c.z) < abs(pos_l.z - pos_c.z)) ? (pos_r - pos_c) : (pos_c - pos_l);
    vec3 dy = (abs(pos_u.z - pos_c.z) < abs(pos_d.z - pos_c.z)) ? (pos_u - pos_c) : (pos_c - pos_d);
    vec3 normal = normalize(cross(dx, dy));
    if (normal.z > 0.0) normal = -normal;
    
    float noise = ign(gl_FragCoord.xy) * 6.2831853;
    mat2 rot = mat2(cos(noise), -sin(noise), sin(noise), cos(noise));
    
    float world_radius = max(u_radius, 0.001);
    float falloff_sq = -1.0 / (world_radius * world_radius);
    float horizon_threshold = 0.05;
    
    float obscurance_sum = 0.0;
    float weight_sum = 0.0;
    
    for (int i = 0; i < 16; i++) {
        vec2 sample_offset = (rot * SAMPLE_DISK[i]) * world_radius;
        sample_offset.x *= u_tex_size.y / u_tex_size.x;
        
        vec2 sample_uv = v_uv + sample_offset;
        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || sample_uv.y < 0.0 || sample_uv.y > 1.0)
            continue;
        
        vec3 hit_pos = get_view_pos(sample_uv);
        vec3 hit_delta = hit_pos - pos_c;
        float len_sq = dot(hit_delta, hit_delta);
        float dist = sqrt(max(len_sq, 0.00001));
        
        float NdotD = dot(-normal, hit_delta) / dist;
        float falloff_mult = max(0.0, len_sq * falloff_sq + 1.0);
        float obscurance = max(0.0, NdotD - horizon_threshold) * falloff_mult;
        
        float reduce = max(0.0, hit_delta.z);
        float halo_weight = clamp(reduce * (-1.0 / world_radius) + 2.0, 0.0, 1.0);
        float weight = 0.6 * halo_weight + 0.4;
        
        obscurance_sum += obscurance * weight;
        weight_sum += weight;
    }
    
    float ao = 1.0;
    if (weight_sum > 0.001) {
        float avg_obscurance = obscurance_sum / weight_sum;
        ao = clamp(1.0 - avg_obscurance * u_intensity * 2.2, 0.0, 1.0);
    }
    ao = pow(ao, 0.9);
    FragColor = vec4(ao, ao, ao, 1.0);
}
)GLSL";


// ----- Godot 4 Cross-Bilateral Depth-Aware Blur Filter -----
// Preserves depth edges when blurring SSAO, and provides 9-tap Gaussian/Tent for Bloom.
static const char* FRAG_BLUR = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform sampler2D u_depth;
uniform vec2  u_direction;     // step direction in UV space
uniform float u_use_depth;     // 1.0 = depth-aware cross bilateral, 0.0 = gaussian/tent
uniform float u_near_plane;
uniform float u_far_plane;

in vec2 v_uv;
out vec4 FragColor;

float linearize_depth(float d) {
    float n = (u_near_plane > 0.1) ? u_near_plane : 50.0;
    float f = (u_far_plane > 100.0) ? u_far_plane : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

const float WEIGHTS[5] = float[](0.2270, 0.1945, 0.1216, 0.0540, 0.0162);

void main() {
    if (u_use_depth > 0.5) {
        float center_d = linearize_depth(texture(u_depth, v_uv).r);
        vec4 accum_col = texture(u_tex, v_uv) * WEIGHTS[0];
        float total_weight = WEIGHTS[0];
        
        for (int i = 1; i <= 4; i++) {
            float spatial_w = WEIGHTS[i];
            vec2 off = u_direction * float(i);
            
            // Positive tap
            vec2 uv_pos = v_uv + off;
            float d_pos = linearize_depth(texture(u_depth, uv_pos).r);
            float range_w_pos = exp(-abs(d_pos - center_d) * 120.0);
            float w_pos = spatial_w * range_w_pos;
            accum_col += texture(u_tex, uv_pos) * w_pos;
            total_weight += w_pos;
            
            // Negative tap
            vec2 uv_neg = v_uv - off;
            float d_neg = linearize_depth(texture(u_depth, uv_neg).r);
            float range_w_neg = exp(-abs(d_neg - center_d) * 120.0);
            float w_neg = spatial_w * range_w_neg;
            accum_col += texture(u_tex, uv_neg) * w_neg;
            total_weight += w_neg;
        }
        FragColor = accum_col / max(total_weight, 0.0001);
    } else {
        vec4 col = texture(u_tex, v_uv) * WEIGHTS[0];
        for (int i = 1; i <= 4; i++) {
            vec2 off = u_direction * float(i);
            col += texture(u_tex, v_uv + off) * WEIGHTS[i];
            col += texture(u_tex, v_uv - off) * WEIGHTS[i];
        }
        FragColor = col;
    }
}
)GLSL";


// ----- Godot 4 13-Tap Dual-Kawase Downsampling with Karis Average -----
// Eliminates subpixel aliasing and fireflies using Brian Karis (UE4) weighting.
//
// The bloom key is the game's OWN authored emitter palette rather than a
// luminance cutoff: see src/shaders/graphics/bloom_extract.glsl for the full
// rationale and the measured numbers behind every constant below. THIS COPY MUST
// STAY IN STEP WITH THAT FILE - it is the fallback used when the shader file is
// missing from the install, and a divergent copy is a silent behaviour change.
static const char* FRAG_BLOOM_EXTRACT = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform float u_threshold;        // luminance cutoff; legacy path only
uniform vec2  u_tex_size;

// --- UI/HUD exclusion -------------------------------------------------------
// u_ui_mask is the scene FBO's DEPTH24_STENCIL8 attachment: .r is depth, .g is
// stencil, and the GL bridge stamps every orthographic draw (2D HUD, text,
// sprites) with stencil 1 and the 3D world with 0. The extract runs BEFORE the
// composite, so without this the composite's own UI exemption protects only the
// UI's pixels and a bright HUD sprite still blooms outward into world pixels -
// which is exactly a square glow around a round UI bubble. u_ui_exempt = 0
// disables the mask.
uniform sampler2D u_ui_mask;
uniform float u_ui_exempt;

// --- authored-emitter key, uploaded from BiomeTable::kEmitterPalette ---------
uniform float u_emitter_keyed;    // 0 = luminance only, 1 = authored-emitter key
uniform int   u_emitter_count;
uniform vec3  u_emitter[4];
uniform float u_emitter_luma;     // level floor                 (default 0.16)
uniform float u_emitter_sat;      // saturation floor, fraction  (default 0.75)
uniform float u_emitter_contrast; // local-contrast ratio floor  (default 1.12)
uniform float u_emitter_gain;     // weight scale                (default 1.60)

in vec2 v_uv;
out vec4 FragColor;

// Hue window: full weight inside HI (11.5 deg of an authored emitter), nothing
// beyond LO (18 deg). 8-bit ceiling: at or above CLIP a channel has clipped and
// its hue is gone.
const float EMITTER_HUE_LO = 0.95;
const float EMITTER_HUE_HI = 0.98;
const float EMITTER_CLIP_LO = 0.98;
const float EMITTER_LEVEL_KNEE = 0.10;
const float EMITTER_CONTRAST_KNEE = 0.35;
const float EMITTER_SAT_KNEE = 0.08;

float luma709(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 chroma_dir(vec3 c, out float strength) {
    float m = max(max(c.r, c.g), c.b);
    if (m < 1e-5) { strength = 0.0; return vec3(0.0); }
    vec3 n = max(c, vec3(0.0)) / m;
    vec3 v = n - vec3(dot(n, vec3(0.299, 0.587, 0.114)));
    strength = length(v);
    if (strength < 1e-5) { return vec3(0.0); }
    return v / strength;
}

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

float emitter_weight(vec3 col, float local_luma) {
    float l = luma709(col);

    float ratio = l / max(local_luma, 1e-3);
    float contrast = smoothstep(u_emitter_contrast,
                                u_emitter_contrast + EMITTER_CONTRAST_KNEE, ratio);
    if (contrast <= 0.0) return 0.0;

    float level = smoothstep(u_emitter_luma, u_emitter_luma + EMITTER_LEVEL_KNEE, l);
    float hue_path = 0.0;
    if (level > 0.0) {
        vec2 m = emitter_match(col);
        float hue = smoothstep(EMITTER_HUE_LO, EMITTER_HUE_HI, m.x);
        float sat = smoothstep(u_emitter_sat - EMITTER_SAT_KNEE,
                               u_emitter_sat + 0.02, m.y);
        hue_path = level * hue * sat;
    }

    float mx = max(max(col.r, col.g), col.b);
    float clip = smoothstep(EMITTER_CLIP_LO, 1.0, mx);

    return clamp(max(hue_path, clip) * contrast * u_emitter_gain, 0.0, 1.0);
}

float karis_weight(vec3 c) {
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + luma);
}

vec3 threshold_sample(vec3 col, float thresh) {
    float luma = luma709(col);
    float knee = max(thresh * 0.5, 0.001);
    float soft = clamp((luma - thresh + knee) / (2.0 * knee), 0.0, 1.0);
    soft = soft * soft;
    float factor = max(soft, step(thresh, luma));
    return col * factor;
}

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
    
    // Local mean over the whole 13-tap footprint.
    float local_luma = luma709(a + b + c + d + e + f + g + h + i + j + k + l + m) / 13.0;

    // 1.0 where this pixel is 3D world, 0.0 where it is 2D UI/HUD. The stencil
    // is an 8-bit integer, so stencil 1 arrives as 1/255 = 0.0039 - NOT 1.0.
    // Comparing against 1.0 would be a silent no-op that looks like the mask
    // simply not working, so test "any non-zero stencil". An all-zero stencil
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
)GLSL";

// ----- Composite: scene + AO + god rays + bloom + shadows -----
static const char* FRAG_COMPOSITE = R"GLSL(
#version 430 core
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

// === Remaster (Tier 3) Uniforms ===
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
// KEEP IN STEP with the same block in src/shaders/graphics/composite.glsl.
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
    float n = u_shadow_near;
    float f = u_shadow_far;
    return (2.0 * n) / (f + n - d * (f - n));
}

// Derive screen-space shadow direction
vec2 compute_light_dir_ss() {
    if (u_light0_pos.w < 0.5) {
        return normalize(u_light0_pos.xy + vec2(0.0001));
    } else {
        vec4 light_clip  = u_projection * u_modelview * vec4(u_light0_pos.xyz, 1.0);
        vec4 player_clip = u_projection * u_modelview * vec4(u_player_world, 1.0);
        
        // Robustness: check for division by zero
        if (abs(light_clip.w) < 0.0001 || abs(player_clip.w) < 0.0001) {
            return u_shadow_light_dir;
        }
        
        vec2 light_ss  = (light_clip.xy  / light_clip.w)  * 0.5 + 0.5;
        vec2 player_ss = (player_clip.xy / player_clip.w) * 0.5 + 0.5;
        
        // Robustness: check for NaN / Inf
        if (any(isnan(light_ss)) || any(isinf(light_ss)) || any(isnan(player_ss)) || any(isinf(player_ss))) {
            return u_shadow_light_dir;
        }
        
        vec2 d = light_ss - player_ss;
        float len = length(d);
        
        // Robustness: check for NaN/Inf length
        if (isnan(len) || isinf(len) || len <= 0.001) {
            return u_shadow_light_dir;
        }
        return d / len;
    }
}

// Material/vertex color tint helper
vec3 get_material_tint() {
    vec3 md = u_mat_diffuse.rgb;
    vec3 vc = u_cur_color.rgb;
    float md_diff = dot(abs(md - vec3(0.8)), vec3(1.0));
    float vc_diff = dot(abs(vc - vec3(1.0)), vec3(1.0));
    if (md_diff < 0.12 && vc_diff < 0.12) return vec3(1.0);
    return clamp(md * 0.7 + vc * 0.3, vec3(0.0), vec3(1.0));
}

// --- Procedural LabPBR Material System ---
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
//     color.b > 0.45 && color.g > 0.25 && color.r < 0.35
// i.e. "clearly blue", which the real palette fails outright on b
// (0.376 < 0.45), so reflections never engaged on real water at all. It is also
// worse than a no-op: the foliage rule below requires g > b * 1.15, and
// (0.000, 0.376, 0.256) satisfies that (0.376 > 0.294), so actual water was being
// classified as GRASS and received subsurface scattering instead of a reflection.
//
// Match the biome's recorded chroma when we have one, and only fall back to the
// blue heuristic when no biome is known. Chroma rather than absolute level,
// because a liquid's brightness swings with depth, lighting and what is behind
// it, while its hue is what identifies it.
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
        return mat; // Sky/Background
    }
    
    // 1. Water / lava, from the biome's recorded colour when available.
    if (is_water_chroma(color)) {
        if (u_water_valid > 0.5 && u_water_surface.r > u_water_surface.b) {
            mat.id = 2;              // Fire biome: its water IS lava.
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
    
    // 2-4. Warm emitters and lava, matched against MEASURED anchors.
    //
    // KEEP IN STEP with the same block in src/shaders/graphics/composite.glsl.
    // This is the embedded fallback used when the shader file cannot be read;
    // editing only the .glsl leaves the two disagreeing, and the fallback is
    // exactly the path taken on a broken install where nobody is looking.
    //
    // The previous thresholds here were invented and both were measurably wrong
    // against the palette in scene_v3_biomes.json:
    //   * `r > 0.80` is STRICT and Fire's real lava is exactly 0.800, so the
    //     lava rule never fired; `g > 0.22` excluded its 0.200 as well.
    //   * the "gold" rule matched NOTHING in the vanilla palette - the torch
    //     emitter (1.000, 0.480, 0.000) missed `g > 0.50` by 0.02.
    // Re-derive the anchor values with tools/scene_material_report.cpp.
    vec3 lava_chroma    = normalize(vec3(0.800, 0.200, 0.000) + vec3(1e-4));
    vec3 torch_chroma   = normalize(vec3(1.000, 0.480, 0.000) + vec3(1e-4));
    vec3 lantern_chroma = normalize(vec3(1.000, 0.850, 0.500) + vec3(1e-4));
    vec3 c_chroma       = normalize(max(color, vec3(0.0)) + vec3(1e-4));

    if (dot(c_chroma, lava_chroma) > 0.995) {   // 0.995 ~ 5.7 deg
        mat.id = 2;
        mat.emissive = 1.0;
        mat.roughness = 1.0;
        return mat;
    }

    if (dot(c_chroma, torch_chroma) > 0.99 || dot(c_chroma, lantern_chroma) > 0.99) {
        // The warm point emitters the game actually uses. Named for what the
        // data supports rather than "gold" - the palette contains no gold.
        mat.id = 3;
        mat.roughness = 0.32;
        mat.metalness = 0.55;
        mat.F0 = color; // reflect base color
        return mat;
    }

    // 4. Foliage / Grass (Green tone classification)
    // Real water RGB(0.000, 0.376, 0.256) also satisfies this; it is only
    // correct because is_water_chroma() returns earlier. Do not reorder.
    if (color.g > color.r * 1.15 && color.g > color.b * 1.15 && color.g > 0.20) {
        mat.id = 4;
        mat.roughness = 0.85;
        return mat;
    }
    
    return mat;
}

// --- Physically Based Shading (GGX Cook-Torrance BRDF) ---
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
    vec3 V = vec3(0.0, 0.0, 1.0); // Orthographic view direction
    
    // Light direction (live JNI light or default)
    vec2 live_dir = (u_light0_diffuse.r + u_light0_diffuse.g + u_light0_diffuse.b > 0.05)
                     ? compute_light_dir_ss() : u_shadow_light_dir;
    vec3 L = normalize(vec3(live_dir, 0.5));
    vec3 H = normalize(V + L);
    
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    
    // Cook-Torrance Specular BRDF terms
    float D = D_GGX(NoH, mat.roughness);
    float G = G_Smith(NoV, NoL, mat.roughness);
    vec3 F = F_Schlick(VoH, mat.F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - mat.metalness;
    
    // If the game light is too dark or zero, use a balanced fallback to avoid pitch blackness
    vec3 light_color = u_light0_diffuse.rgb;
    float luma_light = dot(light_color, vec3(0.299, 0.587, 0.114));
    if (luma_light < 0.35) {
        light_color = mix(light_color, vec3(0.7), 0.7 * (1.0 - luma_light / 0.35));
    }
    
    vec3 diffuse = kD * albedo;
    vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 0.001);
    specular = clamp(specular, vec3(0.0), vec3(1.5));
    
    // ── Modulate. Do NOT replace. ────────────────────────────────────────
    // KEEP IN STEP with the same block in src/shaders/graphics/composite.glsl.
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

// Ray-cast screen-space shadows
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
    int steps = u_pbr_enabled > 0.5 ? 18 : 12; // More ray steps in High preset
    
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

// PCF (Percentage Closer Filtering)
float compute_shadow_pcf() {
    if (u_pbr_enabled < 0.5) {
        // Standard fast 5-tap PCF
        float eps_x = 1.2 / u_tex_size.x;
        float eps_y = 1.2 / u_tex_size.y;
        float s_c = compute_shadow_at(v_uv);
        float s_r = compute_shadow_at(v_uv + vec2(eps_x, 0.0));
        float s_l = compute_shadow_at(v_uv - vec2(eps_x, 0.0));
        float s_u = compute_shadow_at(v_uv + vec2(0.0, eps_y));
        float s_d = compute_shadow_at(v_uv - vec2(0.0, eps_y));
        return (s_c * 4.0 + s_r + s_l + s_u + s_d) / 8.0;
    } else {
        // High quality dithered rotative PCF (reduces banding)
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

// Planar water reflection
vec3 compute_water_reflection(vec3 current_scene, float depth, vec3 color) {
    float water_level = v_uv.y;
    // Find bank boundary/shoreline
    for (float offset = 0.002; offset < 0.12; offset += 0.004) {
        vec2 sample_uv = v_uv + vec2(0.0, offset);
        vec3 c = texture(u_scene, sample_uv).rgb;
        bool is_still_water = is_water_chroma(c);
        if (!is_still_water) {
            water_level = sample_uv.y;
            break;
        }
    }
    
    // Mirror coords vertically
    vec2 reflect_uv = vec2(v_uv.x, water_level + (water_level - v_uv.y) * 0.85);
    
    // Wave ripple distortion
    reflect_uv.x += sin(v_uv.y * 65.0 + u_time * 3.5) * 0.0025;
    reflect_uv.y += cos(v_uv.x * 65.0 + u_time * 3.5) * 0.0018;
    reflect_uv = clamp(reflect_uv, vec2(0.0), vec2(1.0));
    
    vec3 reflected_color = texture(u_scene, reflect_uv).rgb;
    
    // Fade reflections near shorelines
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
// KEEP IN STEP with the same block in src/shaders/graphics/composite.glsl.
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
// KEEP IN STEP with the same block in src/shaders/graphics/composite.glsl.
// The frame arriving here is ALREADY LIT (see the note in main()), so this must
// not shade it again - that stacking is what made the High preset look blown.
// What the level's lights add is their own glow: the part the guest's per-vertex
// pipeline cannot express at all, because a Gouraud light is evaluated at
// VERTICES. A torch's falloff across a large floor quad is simply not present in
// that data, which is why the glow around a flame reads as a hard-edged disc
// instead of a gradient. Reconstructing it per pixel is additive and is bounded
// by add_glow() below, so it fills the halo rather than flattening what is under
// it.
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
    
    // Displaced UV based on color height-bump
    float h_c  = dot(texture(u_scene, v_uv).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_r  = dot(texture(u_scene, v_uv + vec2(eps_x, 0.0)).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_l  = dot(texture(u_scene, v_uv - vec2(eps_x, 0.0)).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_u  = dot(texture(u_scene, v_uv + vec2(0.0, eps_y)).rgb, vec3(0.2126, 0.7152, 0.0722));
    float h_d  = dot(texture(u_scene, v_uv - vec2(0.0, eps_y)).rgb, vec3(0.2126, 0.7152, 0.0722));
    
    vec2 bump = vec2(h_r - h_l, h_u - h_d);
    vec2 displaced_uv = v_uv - bump * 0.0018;
    displaced_uv = clamp(displaced_uv, vec2(0.0), vec2(1.0));
    
    vec3 albedo = texture(u_scene, displaced_uv).rgb;
    vec3 normal = normalize(vec3(-bump.x * 5.2, -bump.y * 5.2, 1.0));
    
    float d_c = linearize_depth(texture(u_depth, displaced_uv).r);
    Material mat = get_material(albedo, d_c);
    
    vec3 scene;
    if (u_pbr_enabled > 0.5) {
        scene = compute_pbr_shading(albedo, normal, mat);
    } else {
        // ── Do NOT re-light an already-lit frame ────────────────────────────
        // KEEP IN STEP with the same block in src/shaders/graphics/composite.glsl.
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
    
    // Apply SSAO (skip for lava/emissive)
    if (u_ao_enabled > 0.5 && mat.emissive < 0.5) {
        float ao = texture(u_ao, v_uv).r;
        ao = mix(ao, 1.0, u_shadow_lift);
        scene *= ao;
    }
    
    // Apply screen-space shadows (skip for lava/emissive)
    if (u_shadow_enabled > 0.5 && mat.emissive < 0.5) {
        float shadow = compute_shadow_pcf();
        scene *= shadow;
    }
    
    // Dynamic reflections for water
    if (u_reflections_enabled > 0.5 && mat.id == 1) {
        scene = compute_water_reflection(scene, d_c, albedo);
    }
    
    // Foliage subsurface scattering simulation
    if (u_pbr_enabled > 0.5 && mat.id == 4) {
        vec2 live_dir = (u_light0_diffuse.r + u_light0_diffuse.g + u_light0_diffuse.b > 0.05)
                         ? compute_light_dir_ss() : u_shadow_light_dir;
        vec3 L = normalize(vec3(live_dir, 0.5));
        float sss = clamp(dot(normal, -L), 0.0, 1.0) * 0.12;
        scene += vec3(0.08, 0.16, 0.06) * sss * u_light0_diffuse.rgb;
    }
    
    // Ambient sky color influence
    float luma = dot(scene, vec3(0.2126, 0.7152, 0.0722));
    float shadow_mask = smoothstep(0.4, 0.1, luma);
    vec3 sky_ambient  = vec3(u_ambient_r, u_ambient_g, u_ambient_b);
    vec3 guest_ambient = u_light_model_amb.rgb;
    vec3 tot_ambient = mix(sky_ambient, sky_ambient * guest_ambient * 3.0, 0.35);
    scene += tot_ambient * shadow_mask * 0.10;
    
    // Material/vertex color tint
    vec3 mat_tint = get_material_tint();
    float dark_mask = smoothstep(0.5, 0.1, luma);
    scene = mix(scene, scene * mat_tint, dark_mask * 0.25);
    
    // Apply God Rays (additive with tint)
    if (u_gr_enabled > 0.5) {
        vec3 rays = texture(u_godrays, v_uv).rgb;
        vec3 tint = vec3(u_gr_tint_r, u_gr_tint_g, u_gr_tint_b);
        tint = mix(tint, u_light0_diffuse.rgb, 0.25);
        scene = add_glow(scene, rays * tint);
    }
    
    // Apply Bloom (additive glow)
    if (u_bloom_enabled > 0.5) {
        vec3 bloom = texture(u_bloom, v_uv).rgb;
        float boost = mat.emissive > 0.5 ? 2.5 : 1.0; // Emissive boost for Lava
        scene = add_glow(scene, bloom * u_bloom_intensity * boost);
    }

    // The level's own lights, filling in the glow the per-vertex pipeline cannot
    // express. Bounded by add_glow(), so this cannot blow the frame out.
    if (u_light_count > 0) {
        scene = add_glow(scene, accumulate_engine_lights(v_uv));
    }
    
    // --- Filmic tonemapping, toe bypassed for display-referred input ---
    scene = tonemap(scene);
    
    // --- Saturation & Vibrance Tuning ---
    float luma2 = dot(scene, vec3(0.2126, 0.7152, 0.0722));
    scene = mix(vec3(luma2), scene, 1.08);
    
    FragColor = vec4(clamp(scene, 0.0, 1.0), 1.0);
}
)GLSL";


// ----- FSR 1.0 EASU: Edge-Adaptive Spatial Upscaling (simplified) -----
// Based on AMD FidelityFX Super Resolution 1.0
// 12-tap filter with edge detection for sharp, artifact-free upscaling
static const char* FRAG_FSR = R"GLSL(
#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;   // input texture resolution
uniform vec2 u_out_size;   // output resolution
in vec2 v_uv;
out vec4 FragColor;

// Compute Lanczos2 weight
float lanczos2(float x) {
    if (abs(x) < 1e-6) return 1.0;
    if (abs(x) >= 2.0) return 0.0;
    float pi_x = 3.14159265 * x;
    return sin(pi_x) * sin(pi_x * 0.5) / (pi_x * pi_x * 0.5);
}

void main() {
    vec2 pixel = v_uv * u_tex_size;
    vec2 frac_pos = fract(pixel) - 0.5;
    vec2 pixel_center = floor(pixel) + 0.5;
    vec2 texel = 1.0 / u_tex_size;
    
    // Sample 4x4 neighborhood (16 taps, but we use 12 inner ones)
    vec3 samples[4][4];
    for (int y = -1; y <= 2; y++) {
        for (int x = -1; x <= 2; x++) {
            vec2 uv = (pixel_center + vec2(x, y)) * texel;
            samples[y+1][x+1] = texture(u_tex, uv).rgb;
        }
    }
    
    // Edge detection: compute gradients
    vec3 dx = (samples[1][2] - samples[1][0]) + (samples[2][2] - samples[2][0]);
    vec3 dy = (samples[2][1] - samples[0][1]) + (samples[2][2] - samples[0][2]);
    float edge_h = dot(abs(dx), vec3(0.333));
    float edge_v = dot(abs(dy), vec3(0.333));
    
    // Directional sharpness: sharpen along edges, smooth across them
    float edge_strength = min(edge_h + edge_v, 1.0);
    float sharpness = mix(0.5, 1.0, edge_strength); // More sharpness at edges
    
    // Lanczos2 filter with directional weighting
    vec3 color = vec3(0.0);
    float total_weight = 0.0;
    
    for (int y = -1; y <= 2; y++) {
        for (int x = -1; x <= 2; x++) {
            vec2 d = frac_pos - vec2(x, y);
            
            // Directional scaling: stretch kernel along edge direction
            float w;
            if (edge_h > edge_v * 1.5) {
                // Horizontal edge: sharpen vertically
                w = lanczos2(d.x) * lanczos2(d.y * sharpness);
            } else if (edge_v > edge_h * 1.5) {
                // Vertical edge: sharpen horizontally
                w = lanczos2(d.x * sharpness) * lanczos2(d.y);
            } else {
                // No strong edge: isotropic
                w = lanczos2(d.x) * lanczos2(d.y);
            }
            
            color += samples[y+1][x+1] * w;
            total_weight += w;
        }
    }
    
    color /= max(total_weight, 0.001);
    
    // Clamp to prevent ringing artifacts
    vec3 min_col = min(min(samples[1][1], samples[1][2]), min(samples[2][1], samples[2][2]));
    vec3 max_col = max(max(samples[1][1], samples[1][2]), max(samples[2][1], samples[2][2]));
    color = clamp(color, min_col, max_col);
    
    // Subtle RCAS (sharpening) pass — integrated
    vec3 center = samples[1][1];
    vec3 n = samples[0][1], s = samples[2][1], e = samples[1][2], w2 = samples[1][0];
    vec3 sharp = center * 5.0 - n - s - e - w2;
    color = mix(color, clamp(sharp, min_col, max_col), 0.15);
    
    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)GLSL";

// ======================= SHADER HELPERS =======================

static std::string load_shader_source(const std::string& filename, const char* default_src) {
    // Check multiple candidate locations:
    // 1. src/shaders/ subdirectories (graphics, postfx, core, common)
    // 2. src/shaders/ root
    // 3. User data path
    std::string candidate_paths[] = {
        "src/shaders/graphics/" + filename,
        "src/shaders/postfx/" + filename,
        "src/shaders/core/" + filename,
        "src/shaders/common/" + filename,
        "src/shaders/" + filename,
        get_data_path("shaders/" + filename),
        get_data_path("src/shaders/" + filename)
    };
    for (const auto& path : candidate_paths) {
        if (!path.empty() && std::filesystem::exists(path)) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::stringstream ss;
                ss << f.rdbuf();
                std::string content = ss.str();
                if (!content.empty()) {
                    std::cout << "[FBO] Loaded external shader: " << path << std::endl;
                    return content;
                }
            }
        }
    }
    return std::string(default_src);
}

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(sh, 1024, nullptr, buf);
        std::cerr << "[FBO] Shader compile error: " << buf << std::endl;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_program(const char* vert, const char* frag, const char* fallback_vert = nullptr, const char* fallback_frag = nullptr) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vert);
    if (!vs && fallback_vert && fallback_vert != vert) {
        std::cerr << "[FBO] External vertex shader failed, falling back to embedded default..." << std::endl;
        vs = compile_shader(GL_VERTEX_SHADER, fallback_vert);
    }
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, frag);
    if (!fs && fallback_frag && fallback_frag != frag) {
        std::cerr << "[FBO] External fragment shader failed, falling back to embedded default..." << std::endl;
        fs = compile_shader(GL_FRAGMENT_SHADER, fallback_frag);
    }
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_uv");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetProgramInfoLog(prog, 1024, nullptr, buf);
        std::cerr << "[FBO] Program link error: " << buf << std::endl;
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint get_program(FBOScale mode) {
    std::string vert_src = load_shader_source("quad.vert", VERT_SRC);
    switch (mode) {
        case FBOScale::NEAREST:
            if (!g_prog_nearest) g_prog_nearest = link_program(vert_src.c_str(), load_shader_source("nearest.glsl", FRAG_NEAREST).c_str(), VERT_SRC, FRAG_NEAREST);
            return g_prog_nearest;
        case FBOScale::CRT_SCANLINE:
            if (!g_prog_crt) g_prog_crt = link_program(vert_src.c_str(), load_shader_source("crt.glsl", FRAG_CRT).c_str(), VERT_SRC, FRAG_CRT);
            return g_prog_crt;
        case FBOScale::FSR:
            if (!g_prog_fsr) g_prog_fsr = link_program(vert_src.c_str(), load_shader_source("fsr.glsl", FRAG_FSR).c_str(), VERT_SRC, FRAG_FSR);
            return g_prog_fsr;
        default:
            if (!g_prog_sharp) g_prog_sharp = link_program(vert_src.c_str(), load_shader_source("sharp.glsl", FRAG_SHARP_BILINEAR).c_str(), VERT_SRC, FRAG_SHARP_BILINEAR);
            return g_prog_sharp;
    }
}

// Fullscreen quad data (uploaded to VBO during fbo_init)
// Fullscreen quad data - 4 vertices for GL_TRIANGLE_FAN (compatibility profile)
static const float FSQ[] = {
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
    -1.0f,  1.0f,  0.0f, 1.0f,
};

static void draw_fsq() {
    glBindVertexArray(g_fsq_vao);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glBindVertexArray(0);  // MUST unbind — game uses GL1.x client-side vertex arrays
}

// Helper: create an FBO with color texture
static bool create_fbo(GLuint& fbo, GLuint& tex, int w, int h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return status == GL_FRAMEBUFFER_COMPLETE;
}

// ======================= PUBLIC API =======================

bool fbo_init(int game_w, int game_h) {
    g_game_w = game_w;
    g_game_h = game_h;
    g_half_w = game_w / 2;
    g_half_h = game_h / 2;

    // --- Main game FBO with color texture + depth TEXTURE ---
    glGenTextures(1, &g_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, game_w, game_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth as TEXTURE (not renderbuffer!) so we can sample it in shaders
    glGenTextures(1, &g_fbo_depth_tex);
    glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, game_w, game_h, 0,
                 GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // GL_DEPTH_TEXTURE_MODE is deprecated/removed in GL3 core — omit it.
    // Use GL_TEXTURE_COMPARE_MODE = GL_NONE so depth reads as a plain float.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, g_fbo_depth_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[FBO] Main framebuffer incomplete — status 0x"
                  << std::hex << status << std::dec << std::endl;
        g_fbo_ok = false;
        return false;
    }

    // --- Intermediate FBOs ---
    if (!create_fbo(g_postfx_fbo, g_postfx_tex, game_w, game_h)) {
        std::cerr << "[FBO] PostFX FBO A incomplete" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_postfx_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, g_fbo_depth_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!create_fbo(g_postfx_fbo_b, g_postfx_tex_b, game_w, game_h)) {
        std::cerr << "[FBO] PostFX FBO B incomplete" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_postfx_fbo_b);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, g_fbo_depth_tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!create_fbo(g_half_fbo_a, g_half_tex_a, g_half_w, g_half_h)) {
        std::cerr << "[FBO] Half-res FBO A incomplete" << std::endl;
    }
    if (!create_fbo(g_half_fbo_b, g_half_tex_b, g_half_w, g_half_h)) {
        std::cerr << "[FBO] Half-res FBO B incomplete" << std::endl;
    }
    g_quarter_w = game_w / 4;
    g_quarter_h = game_h / 4;
    if (!create_fbo(g_bloom_fbo_a, g_bloom_tex_a, g_quarter_w, g_quarter_h)) {
        std::cerr << "[FBO] Bloom FBO A incomplete" << std::endl;
    }
    if (!create_fbo(g_bloom_fbo_b, g_bloom_tex_b, g_quarter_w, g_quarter_h)) {
        std::cerr << "[FBO] Bloom FBO B incomplete" << std::endl;
    }

    // --- Pre-compile shaders (with external file overrides) ---
    std::string vert_src = load_shader_source("quad.vert", VERT_SRC);
    g_prog_sharp     = link_program(vert_src.c_str(), load_shader_source("sharp.glsl", FRAG_SHARP_BILINEAR).c_str(), VERT_SRC, FRAG_SHARP_BILINEAR);
    g_prog_postfx    = link_program(vert_src.c_str(), load_shader_source("postfx.glsl", FRAG_POSTFX).c_str(), VERT_SRC, FRAG_POSTFX);
    g_prog_godrays   = link_program(vert_src.c_str(), load_shader_source("godrays.glsl", FRAG_GODRAYS).c_str(), VERT_SRC, FRAG_GODRAYS);
    g_prog_ssao      = link_program(vert_src.c_str(), load_shader_source("ssao.glsl", FRAG_SSAO).c_str(), VERT_SRC, FRAG_SSAO);
    g_prog_blur      = link_program(vert_src.c_str(), load_shader_source("blur.glsl", FRAG_BLUR).c_str(), VERT_SRC, FRAG_BLUR);
    g_prog_composite = link_program(vert_src.c_str(), load_shader_source("composite.glsl", FRAG_COMPOSITE).c_str(), VERT_SRC, FRAG_COMPOSITE);
    g_prog_bloom_extract = link_program(vert_src.c_str(), load_shader_source("bloom_extract.glsl", FRAG_BLOOM_EXTRACT).c_str(), VERT_SRC, FRAG_BLOOM_EXTRACT);
    g_prog_portal    = link_program(load_shader_source("portal.vert", VERT_PORTAL).c_str(), load_shader_source("portal.glsl", FRAG_PORTAL).c_str(), VERT_PORTAL, FRAG_PORTAL);
    if (g_prog_portal) {
        std::cout << "[FBO] Portal shader compiled OK (program=" << g_prog_portal << ")" << std::endl;
    } else {
        std::cerr << "[FBO] WARNING: Portal shader FAILED to compile — portal effects disabled" << std::endl;
    }

    // --- Cache uniform locations (Optimization 1) ---
    if (g_prog_ssao) {
        loc_ssao_depth      = glGetUniformLocation(g_prog_ssao, "u_depth");
        loc_ssao_tex_size   = glGetUniformLocation(g_prog_ssao, "u_tex_size");
        loc_ssao_radius     = glGetUniformLocation(g_prog_ssao, "u_radius");
        loc_ssao_intensity  = glGetUniformLocation(g_prog_ssao, "u_intensity");
        loc_ssao_time       = glGetUniformLocation(g_prog_ssao, "u_time");
        loc_ssao_near_plane = glGetUniformLocation(g_prog_ssao, "u_near_plane");
        loc_ssao_far_plane  = glGetUniformLocation(g_prog_ssao, "u_far_plane");
    }
    if (g_prog_blur) {
        loc_blur_tex        = glGetUniformLocation(g_prog_blur, "u_tex");
        loc_blur_depth      = glGetUniformLocation(g_prog_blur, "u_depth");
        loc_blur_direction  = glGetUniformLocation(g_prog_blur, "u_direction");
        loc_blur_use_depth  = glGetUniformLocation(g_prog_blur, "u_use_depth");
        loc_blur_near_plane = glGetUniformLocation(g_prog_blur, "u_near_plane");
        loc_blur_far_plane  = glGetUniformLocation(g_prog_blur, "u_far_plane");
    }
    if (g_prog_godrays) {
        loc_godrays_tex       = glGetUniformLocation(g_prog_godrays, "u_tex");
        loc_godrays_depth     = glGetUniformLocation(g_prog_godrays, "u_depth");
        loc_godrays_sun_pos   = glGetUniformLocation(g_prog_godrays, "u_sun_pos");
        loc_godrays_intensity = glGetUniformLocation(g_prog_godrays, "u_intensity");
        loc_godrays_decay     = glGetUniformLocation(g_prog_godrays, "u_decay");
        loc_godrays_density   = glGetUniformLocation(g_prog_godrays, "u_density");
        loc_godrays_exposure  = glGetUniformLocation(g_prog_godrays, "u_exposure");
        loc_godrays_tex_size  = glGetUniformLocation(g_prog_godrays, "u_tex_size");
        loc_godrays_time      = glGetUniformLocation(g_prog_godrays, "u_time");
        loc_godrays_near      = glGetUniformLocation(g_prog_godrays, "u_near");
        loc_godrays_far       = glGetUniformLocation(g_prog_godrays, "u_far");
    }
    if (g_prog_bloom_extract) {
        loc_bloom_extract_tex       = glGetUniformLocation(g_prog_bloom_extract, "u_tex");
        loc_bloom_extract_threshold = glGetUniformLocation(g_prog_bloom_extract, "u_threshold");
        loc_bloom_extract_tex_size  = glGetUniformLocation(g_prog_bloom_extract, "u_tex_size");
        loc_bloom_extract_ui_mask   = glGetUniformLocation(g_prog_bloom_extract, "u_ui_mask");
        loc_bloom_extract_ui_exempt = glGetUniformLocation(g_prog_bloom_extract, "u_ui_exempt");
        loc_bloom_extract_emitter_keyed    = glGetUniformLocation(g_prog_bloom_extract, "u_emitter_keyed");
        loc_bloom_extract_emitter_count    = glGetUniformLocation(g_prog_bloom_extract, "u_emitter_count");
        loc_bloom_extract_emitter          = glGetUniformLocation(g_prog_bloom_extract, "u_emitter[0]");
        loc_bloom_extract_emitter_luma     = glGetUniformLocation(g_prog_bloom_extract, "u_emitter_luma");
        loc_bloom_extract_emitter_sat      = glGetUniformLocation(g_prog_bloom_extract, "u_emitter_sat");
        loc_bloom_extract_emitter_contrast = glGetUniformLocation(g_prog_bloom_extract, "u_emitter_contrast");
        loc_bloom_extract_emitter_gain     = glGetUniformLocation(g_prog_bloom_extract, "u_emitter_gain");
    }
    if (g_prog_composite) {
        loc_comp_scene            = glGetUniformLocation(g_prog_composite, "u_scene");
        loc_comp_ao               = glGetUniformLocation(g_prog_composite, "u_ao");
        loc_comp_godrays          = glGetUniformLocation(g_prog_composite, "u_godrays");
        loc_comp_bloom            = glGetUniformLocation(g_prog_composite, "u_bloom");
        loc_comp_depth            = glGetUniformLocation(g_prog_composite, "u_depth");
        loc_comp_ao_enabled       = glGetUniformLocation(g_prog_composite, "u_ao_enabled");
        loc_comp_gr_enabled       = glGetUniformLocation(g_prog_composite, "u_gr_enabled");
        loc_comp_bloom_enabled    = glGetUniformLocation(g_prog_composite, "u_bloom_enabled");
        loc_comp_bloom_intensity  = glGetUniformLocation(g_prog_composite, "u_bloom_intensity");
        loc_comp_gr_tint_r        = glGetUniformLocation(g_prog_composite, "u_gr_tint_r");
        loc_comp_gr_tint_g        = glGetUniformLocation(g_prog_composite, "u_gr_tint_g");
        loc_comp_gr_tint_b        = glGetUniformLocation(g_prog_composite, "u_gr_tint_b");
        loc_comp_shadow_enabled   = glGetUniformLocation(g_prog_composite, "u_shadow_enabled");
        loc_comp_shadow_intensity = glGetUniformLocation(g_prog_composite, "u_shadow_intensity");
        loc_comp_shadow_softness  = glGetUniformLocation(g_prog_composite, "u_shadow_softness");
        loc_comp_shadow_light_dir = glGetUniformLocation(g_prog_composite, "u_shadow_light_dir");
        loc_comp_shadow_sun_z     = glGetUniformLocation(g_prog_composite, "u_shadow_sun_z");
        loc_comp_shadow_near      = glGetUniformLocation(g_prog_composite, "u_shadow_near");
        loc_comp_shadow_far       = glGetUniformLocation(g_prog_composite, "u_shadow_far");
        loc_comp_shadow_lift      = glGetUniformLocation(g_prog_composite, "u_shadow_lift");
        loc_comp_bg_depth         = glGetUniformLocation(g_prog_composite, "u_bg_depth");
        loc_comp_bg_scale         = glGetUniformLocation(g_prog_composite, "u_bg_scale");
        loc_comp_ambient_r        = glGetUniformLocation(g_prog_composite, "u_ambient_r");
        loc_comp_ambient_g        = glGetUniformLocation(g_prog_composite, "u_ambient_g");
        loc_comp_ambient_b        = glGetUniformLocation(g_prog_composite, "u_ambient_b");
        loc_comp_tex_size         = glGetUniformLocation(g_prog_composite, "u_tex_size");
        loc_comp_pbr_enabled      = glGetUniformLocation(g_prog_composite, "u_pbr_enabled");
        loc_comp_reflections_enabled = glGetUniformLocation(g_prog_composite, "u_reflections_enabled");
        loc_comp_reflection_intensity = glGetUniformLocation(g_prog_composite, "u_reflection_intensity");
        loc_comp_water_surface = glGetUniformLocation(g_prog_composite, "u_water_surface");
        loc_comp_water_valid   = glGetUniformLocation(g_prog_composite, "u_water_valid");
        loc_comp_time             = glGetUniformLocation(g_prog_composite, "u_time");
        loc_comp_ui_pass          = glGetUniformLocation(g_prog_composite, "u_ui_pass");
        loc_comp_ui_mask          = glGetUniformLocation(g_prog_composite, "u_ui_mask");
        loc_comp_ui_exempt        = glGetUniformLocation(g_prog_composite, "u_ui_exempt");
        loc_comp_light_count      = glGetUniformLocation(g_prog_composite, "u_light_count");
        loc_comp_lights           = glGetUniformLocation(g_prog_composite, "u_lights[0]");
        loc_comp_light_color      = glGetUniformLocation(g_prog_composite, "u_light_color[0]");
        loc_comp_light_atten      = glGetUniformLocation(g_prog_composite, "u_light_atten[0]");
    }
    if (g_prog_postfx) {
        loc_postfx_tex                  = glGetUniformLocation(g_prog_postfx, "u_tex");
        loc_postfx_depth_tex            = glGetUniformLocation(g_prog_postfx, "u_depth_tex");
        loc_postfx_tex_size             = glGetUniformLocation(g_prog_postfx, "u_tex_size");
        loc_postfx_time                 = glGetUniformLocation(g_prog_postfx, "u_time");
        loc_postfx_vignette             = glGetUniformLocation(g_prog_postfx, "u_vignette");
        loc_postfx_grain                = glGetUniformLocation(g_prog_postfx, "u_grain");
        loc_postfx_ca_offset            = glGetUniformLocation(g_prog_postfx, "u_ca_offset");
        loc_postfx_saturation           = glGetUniformLocation(g_prog_postfx, "u_saturation");
        loc_postfx_contrast             = glGetUniformLocation(g_prog_postfx, "u_contrast");
        loc_postfx_brightness           = glGetUniformLocation(g_prog_postfx, "u_brightness");
        loc_postfx_warmth               = glGetUniformLocation(g_prog_postfx, "u_warmth");
        loc_postfx_sharpen              = glGetUniformLocation(g_prog_postfx, "u_sharpen");
        loc_postfx_outline_enabled      = glGetUniformLocation(g_prog_postfx, "u_outline_enabled");
        loc_postfx_outline_thickness    = glGetUniformLocation(g_prog_postfx, "u_outline_thickness");
        loc_postfx_outline_intensity    = glGetUniformLocation(g_prog_postfx, "u_outline_intensity");
        loc_postfx_outline_depth_threshold = glGetUniformLocation(g_prog_postfx, "u_outline_depth_threshold");
        loc_postfx_outline_near         = glGetUniformLocation(g_prog_postfx, "u_outline_near");
        loc_postfx_outline_far          = glGetUniformLocation(g_prog_postfx, "u_outline_far");
    }
    if (g_prog_portal) {
        loc_portal_portal_tex = glGetUniformLocation(g_prog_portal, "u_portal_tex");
        loc_portal_mv_matrix  = glGetUniformLocation(g_prog_portal, "u_mv_matrix");
        loc_portal_aspect     = glGetUniformLocation(g_prog_portal, "u_aspect");
        loc_portal_color      = glGetUniformLocation(g_prog_portal, "u_color");
        loc_portal_time       = glGetUniformLocation(g_prog_portal, "u_time");
        loc_portal_speed      = glGetUniformLocation(g_prog_portal, "u_speed");
        loc_portal_alpha      = glGetUniformLocation(g_prog_portal, "u_alpha");
    }

    // Game-engine handshake uniforms — composite shader
    if (g_prog_composite) {
        loc_comp_light0_pos      = glGetUniformLocation(g_prog_composite, "u_light0_pos");
        loc_comp_light0_diffuse  = glGetUniformLocation(g_prog_composite, "u_light0_diffuse");
        loc_comp_light0_ambient  = glGetUniformLocation(g_prog_composite, "u_light0_ambient");
        loc_comp_light_model_amb = glGetUniformLocation(g_prog_composite, "u_light_model_amb");
        loc_comp_mat_diffuse     = glGetUniformLocation(g_prog_composite, "u_mat_diffuse");
        loc_comp_cur_color       = glGetUniformLocation(g_prog_composite, "u_cur_color");
        loc_comp_player_world    = glGetUniformLocation(g_prog_composite, "u_player_world");
        loc_comp_modelview       = glGetUniformLocation(g_prog_composite, "u_modelview");
        loc_comp_projection      = glGetUniformLocation(g_prog_composite, "u_projection");
    }

    // --- Create fullscreen quad VAO/VBO (Optimization 2) ---
    glGenVertexArrays(1, &g_fsq_vao);
    glGenBuffers(1, &g_fsq_vbo);
    glBindVertexArray(g_fsq_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_fsq_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(FSQ), FSQ, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    g_fbo_ok = true;
    extern PostFXState g_postfx;
    postfx_apply_boot_defaults(g_postfx);
    std::cout << "[FBO] Pipeline ready (Boot Default: SW+ Medium, PostFX: None): " << game_w << "x" << game_h
              << " | half=" << g_half_w << "x" << g_half_h
              << " | quarter=" << g_quarter_w << "x" << g_quarter_h
              << " | depth=texture | SSAO+GodRays+Bloom+PostFX+Portal" << std::endl;
    return true;
}

void fbo_destroy() {
    auto del_fbo = [](GLuint& f) { if (f) { glDeleteFramebuffers(1, &f); f = 0; } };
    auto del_tex = [](GLuint& t) { if (t) { glDeleteTextures(1, &t); t = 0; } };
    auto del_prg = [](GLuint& p) { if (p) { glDeleteProgram(p); p = 0; } };
    del_fbo(g_fbo); del_tex(g_fbo_tex); del_tex(g_fbo_depth_tex);
    del_fbo(g_postfx_fbo); del_tex(g_postfx_tex);
    del_fbo(g_postfx_fbo_b); del_tex(g_postfx_tex_b);
    del_fbo(g_half_fbo_a); del_tex(g_half_tex_a);
    del_fbo(g_half_fbo_b); del_tex(g_half_tex_b);
    del_fbo(g_bloom_fbo_a); del_tex(g_bloom_tex_a);
    del_fbo(g_bloom_fbo_b); del_tex(g_bloom_tex_b);
    del_prg(g_prog_sharp); del_prg(g_prog_nearest); del_prg(g_prog_crt);
    del_prg(g_prog_postfx); del_prg(g_prog_godrays); del_prg(g_prog_ssao);
    del_prg(g_prog_blur); del_prg(g_prog_composite);
    del_prg(g_prog_bloom_extract); del_prg(g_prog_fsr);
    del_prg(g_prog_portal);
    del_tex(g_portal_tex); g_portal_tex_loaded = false;
    // Delete fullscreen quad VAO/VBO (Optimization 2)
    if (g_fsq_vao) { glDeleteVertexArrays(1, &g_fsq_vao); g_fsq_vao = 0; }
    if (g_fsq_vbo) { glDeleteBuffers(1, &g_fsq_vbo); g_fsq_vbo = 0; }
    g_fbo_ok = false;
}

void fbo_begin_game() {
    extern PostFXState g_postfx;
    if (!g_fbo_ok || !g_postfx.enabled) return;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, g_game_w, g_game_h);

    // Clear stencil buffer to 0 at the start of each frame
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);

    // Reset dynamic light buffer for this frame
    LightManager::Instance().ClearFrameLights();
    
    static int begin_diag_count = 0;
    if (begin_diag_count < 5) {
        GLenum err = glGetError();
        std::cout << "[FBO-Diag] fbo_begin_game: FBO=" << g_fbo << " size=" << g_game_w << "x" << g_game_h << " err=" << err << std::endl;
        begin_diag_count++;
    }
}

void fbo_end_game_and_blit(int win_w, int win_h, FBOScale mode, const PostFXState* postfx) {
    if (!g_fbo_ok || !postfx || !postfx->enabled) return;

    static int end_diag_count = 0;
    if (end_diag_count < 5) {
        GLenum err = glGetError();
        std::cout << "[FBO-Diag] fbo_end_game_and_blit: win=" << win_w << "x" << win_h << " mode=" << (int)mode << " err=" << err << std::endl;
        end_diag_count++;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    // Clear to magenta for diagnostic visual visibility (if FBO is empty/transparent, screen turns magenta)
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Save GL state (same pattern as v7 which worked — compat profile supports these)
    GLint old_prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_prog);
    GLboolean saved_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean saved_blend      = glIsEnabled(GL_BLEND);
    GLboolean saved_cull_face  = glIsEnabled(GL_CULL_FACE);
    GLboolean saved_scissor    = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean saved_stencil    = glIsEnabled(GL_STENCIL_TEST);
    GLboolean saved_alpha_test = glIsEnabled(GL_ALPHA_TEST);
    GLboolean saved_lighting   = glIsEnabled(GL_LIGHTING);
    GLint saved_blend_src = 0, saved_blend_dst = 0;
    glGetIntegerv(GL_BLEND_SRC, &saved_blend_src);
    glGetIntegerv(GL_BLEND_DST, &saved_blend_dst);
    GLint saved_depth_func = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &saved_depth_func);
    GLboolean saved_depth_mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &saved_depth_mask);
    GLint saved_vao = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vao);
    GLint saved_active_tex = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &saved_active_tex);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);

    GLuint final_tex = g_fbo_tex;
    g_postfx_time += 0.016f;
    if (g_postfx_time > 10000.0f) g_postfx_time = 0.0f;

    // GUI detection counters — reset each frame (set by GL bridge)
    // NOTE: glFrustumf detection doesn't work because Caver uses glLoadMatrixf
    // for projection, not glFrustumf. Need to hook a game menu function instead.
    // For now, counters are maintained but NOT used for suppression.
    g_frame_has_ortho = 0;
    g_frame_has_perspective = 0;

    bool do_postfx = postfx && postfx->enabled && g_prog_postfx && g_postfx_fbo;
    bool do_ssao   = do_postfx && postfx->ssao && g_prog_ssao && g_prog_blur && g_half_fbo_a;
    bool do_godrays = do_postfx && (postfx->god_rays || postfx->volumetric_light) && g_prog_godrays && g_half_fbo_a;
    bool do_bloom  = do_postfx && postfx->bloom && g_prog_bloom_extract && g_prog_blur && g_bloom_fbo_a;
    bool do_shadows = do_postfx && postfx->shadows && g_fbo_depth_tex;
    bool do_composite = do_ssao || do_godrays || do_bloom || do_shadows;

    GLuint ao_tex = 0;
    GLuint gr_tex = 0;
    GLuint bloom_tex = 0;

    // ======== PASS 1: SSAO (half-res) ========
    if (do_ssao) {
        // Render SSAO to half_fbo_a
        glBindFramebuffer(GL_FRAMEBUFFER, g_half_fbo_a);
        glViewport(0, 0, g_half_w, g_half_h);
        glUseProgram(g_prog_ssao);
        glUniform1i(loc_ssao_depth, 0);
        glUniform2f(loc_ssao_tex_size, (float)g_half_w, (float)g_half_h);
        glUniform1f(loc_ssao_radius, postfx->ssao_radius);
        glUniform1f(loc_ssao_intensity, postfx->ssao_intensity);
        glUniform1f(loc_ssao_time, g_postfx_time);
        glUniform1f(loc_ssao_near_plane, 50.0f);
        glUniform1f(loc_ssao_far_plane, 20000.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
        draw_fsq();

        // Blur SSAO horizontally: half_a -> half_b (depth-aware cross bilateral)
        glBindFramebuffer(GL_FRAMEBUFFER, g_half_fbo_b);
        glUseProgram(g_prog_blur);
        glUniform1i(loc_blur_tex, 0);
        glUniform1i(loc_blur_depth, 1);
        glUniform1f(loc_blur_use_depth, 1.0f);
        glUniform1f(loc_blur_near_plane, 50.0f);
        glUniform1f(loc_blur_far_plane, 20000.0f);
        glUniform2f(loc_blur_direction, 2.2f / g_half_w, 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_half_tex_a);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
        draw_fsq();

        // Blur SSAO vertically: half_b -> half_a
        glBindFramebuffer(GL_FRAMEBUFFER, g_half_fbo_a);
        glUniform2f(loc_blur_direction, 0.0f, 2.2f / g_half_h);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_half_tex_b);
        draw_fsq();
        glActiveTexture(GL_TEXTURE0);

        ao_tex = g_half_tex_a;
    }

    // ======== PASS 2: God Rays (half-res) ========
    if (do_godrays) {
        GLuint target_fbo = do_ssao ? g_half_fbo_b : g_half_fbo_a;
        glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
        glViewport(0, 0, g_half_w, g_half_h);
        glUseProgram(g_prog_godrays);
        glUniform1i(loc_godrays_tex, 0);
        glUniform1i(loc_godrays_depth, 1);
        glUniform2f(loc_godrays_sun_pos, postfx->sun_x, postfx->sun_y);
        float intensity = postfx->god_rays ? postfx->god_rays_intensity : postfx->volumetric_intensity;
        glUniform1f(loc_godrays_intensity, intensity);
        glUniform1f(loc_godrays_decay, postfx->god_rays_decay);
        glUniform1f(loc_godrays_density, 1.0f);
        glUniform1f(loc_godrays_exposure, 1.0f);
        if (loc_godrays_tex_size != -1) glUniform2f(loc_godrays_tex_size, (float)g_half_w, (float)g_half_h);
        if (loc_godrays_time != -1)     glUniform1f(loc_godrays_time, g_postfx_time);
        if (loc_godrays_near != -1)     glUniform1f(loc_godrays_near, 50.0f);
        if (loc_godrays_far != -1)      glUniform1f(loc_godrays_far, 20000.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
        draw_fsq();
        glActiveTexture(GL_TEXTURE0);

        gr_tex = do_ssao ? g_half_tex_b : g_half_tex_a;
    }

    // ======== PASS 2b: Bloom (quarter-res) ========
    if (do_bloom) {
        // Extract light sources: scene -> bloom_fbo_a
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_fbo_a);
        glViewport(0, 0, g_quarter_w, g_quarter_h);
        glUseProgram(g_prog_bloom_extract);
        glUniform1i(loc_bloom_extract_tex, 0);
        glUniform1f(loc_bloom_extract_threshold, postfx->bloom_threshold);
        if (loc_bloom_extract_tex_size != -1) {
            glUniform2f(loc_bloom_extract_tex_size, (float)g_quarter_w, (float)g_quarter_h);
        }
        // ── UI/HUD exclusion ────────────────────────────────────────────────────
        // The 3D world is stencil 0 and every orthographic (2D) draw is stencil
        // 1 in the scene FBO's DEPTH24_STENCIL8 attachment. Feeding that mask to
        // the extract is what stops a bright HUD sprite from blooming out into
        // the world pixels around it - the "square halo around a round bubble"
        // that the composite's own UI exemption cannot prevent, because the
        // extract runs first. Texture unit 1 is otherwise unused in this pass.
        // If g_fbo_depth_tex is 0 the sampled mask reads zero everywhere, i.e.
        // every pixel stays world, which is the previous behaviour.
        if (loc_bloom_extract_ui_mask != -1) {
            glUniform1i(loc_bloom_extract_ui_mask, 1);
        }
        if (loc_bloom_extract_ui_exempt != -1) {
            glUniform1f(loc_bloom_extract_ui_exempt, (postfx && postfx->ui_exempt) ? 1.0f : 0.0f);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
        glActiveTexture(GL_TEXTURE0);
        // ── Authored-emitter key ────────────────────────────────────────────────
        // The bloom key is the CURRENT biome's own point-light colours, from the
        // same authoritative record the water pass uses. Every vanilla glow is a
        // Point light whose colour is authored in the level, so this needs no
        // inference: an unresolved biome contributes ZERO emitters and the key
        // then finds nothing, rather than falling back to a different biome's
        // palette (a wrong palette is invisible and therefore never corrected).
        if (loc_bloom_extract_emitter_keyed != -1) {
            const auto& eb = TextureAwareMapper::Instance().GetCurrentBiome();
            int emitter_count = 0;
            float emitter_buf[4 * 3] = {0.0f};
            if (eb.valid && eb.entry != nullptr) {
                const int bid = eb.entry->id;
                emitter_count = swordigo::biome::EmitterCountForBiome(bid);
                if (emitter_count > 4) emitter_count = 4;
                for (int i = 0; i < emitter_count; ++i) {
                    emitter_buf[i * 3 + 0] = swordigo::biome::kEmitterPalette[bid][i][0];
                    emitter_buf[i * 3 + 1] = swordigo::biome::kEmitterPalette[bid][i][1];
                    emitter_buf[i * 3 + 2] = swordigo::biome::kEmitterPalette[bid][i][2];
                }
            }
            glUniform1f(loc_bloom_extract_emitter_keyed, postfx->bloom_emitter_key ? 1.0f : 0.0f);
            if (loc_bloom_extract_emitter_count != -1) {
                glUniform1i(loc_bloom_extract_emitter_count, emitter_count);
            }
            if (loc_bloom_extract_emitter != -1) {
                // count 0 is not a legal uniform upload; one padded slot is, and
                // the shader guards on u_emitter_count anyway.
                glUniform3fv(loc_bloom_extract_emitter,
                             emitter_count > 0 ? emitter_count : 1, emitter_buf);
            }
            if (loc_bloom_extract_emitter_luma != -1)     glUniform1f(loc_bloom_extract_emitter_luma, postfx->bloom_emitter_luma);
            if (loc_bloom_extract_emitter_sat != -1)      glUniform1f(loc_bloom_extract_emitter_sat, postfx->bloom_emitter_sat);
            if (loc_bloom_extract_emitter_contrast != -1) glUniform1f(loc_bloom_extract_emitter_contrast, postfx->bloom_emitter_contrast);
            if (loc_bloom_extract_emitter_gain != -1)     glUniform1f(loc_bloom_extract_emitter_gain, postfx->bloom_emitter_gain);
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
        draw_fsq();

        // Blur horizontal 1: bloom_a -> bloom_b
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_fbo_b);
        glUseProgram(g_prog_blur);
        glUniform1i(loc_blur_tex, 0);
        glUniform1f(loc_blur_use_depth, 0.0f); // Fast Gaussian/tent for bloom
        glUniform2f(loc_blur_direction, 1.2f / g_quarter_w, 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_bloom_tex_a);
        draw_fsq();

        // Blur vertical 1: bloom_b -> bloom_a
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_fbo_a);
        glUniform2f(loc_blur_direction, 0.0f, 1.2f / g_quarter_h);
        glBindTexture(GL_TEXTURE_2D, g_bloom_tex_b);
        draw_fsq();

        // Blur horizontal 2 (wider): bloom_a -> bloom_b
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_fbo_b);
        glUniform2f(loc_blur_direction, 2.5f / g_quarter_w, 0.0f);
        glBindTexture(GL_TEXTURE_2D, g_bloom_tex_a);
        draw_fsq();

        // Blur vertical 2 (wider): bloom_b -> bloom_a
        glBindFramebuffer(GL_FRAMEBUFFER, g_bloom_fbo_a);
        glUniform2f(loc_blur_direction, 0.0f, 2.5f / g_quarter_h);
        glBindTexture(GL_TEXTURE_2D, g_bloom_tex_b);
        draw_fsq();

        bloom_tex = g_bloom_tex_a;
    }

    // ======== PASS 2.5: Portal Effect Quad (into game FBO) ========
    // Portal VP matrix diagnostic — print once every 120 frames to understand coords
    {
        static int portal_diag_count = 0;
        if (g_portal_active > 0.5f && portal_diag_count % 120 == 0) {
            std::cout << "[PORTAL-DIAG] world_pos=(" 
                      << g_portal_world_x << "," << g_portal_world_y << "," << g_portal_world_z 
                      << ") color=(" << g_portal_color_r << "," << g_portal_color_g << "," << g_portal_color_b
                      << ")" << std::endl;
            // Show the computed MVP column 3 (should be view-space position)
            std::cout << "[PORTAL-DIAG] MVP last_col=(" 
                      << g_portal_vp_matrix[3] << "," << g_portal_vp_matrix[7] 
                      << "," << g_portal_vp_matrix[11] << "," << g_portal_vp_matrix[15] << ")" << std::endl;
        }
        if (g_portal_active > 0.5f) portal_diag_count++;
    }
    // PORTAL ENABLED — uses model-view matrix from game + our perspective projection
    if (g_portal_active > 0.5f && g_prog_portal) {
        // Load portal texture on first use
        if (!g_portal_tex_loaded) {
            extern std::string g_instance_assets_dir;
            std::string path = get_data_path(g_instance_assets_dir + "/resources/portal_effect_2x.pvr");
            FILE* f = fopen(path.c_str(), "rb");
            if (f) { fclose(f); 
                g_portal_tex = pvr_load_texture(path.c_str());
                if (g_portal_tex) {
                    std::cout << "[PORTAL] Loaded portal texture from " << path << std::endl;
                }
            }
            g_portal_tex_loaded = true;
        }
        
        if (g_portal_tex) {
            // Render into the game FBO WITH depth testing — portal renders
            // behind the hero and other scene objects using the game's depth buffer
            glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            glViewport(0, 0, g_game_w, g_game_h);
            
            // Enable depth testing (read-only) — portal is occluded by closer objects
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glDepthMask(GL_FALSE);  // don't write to depth buffer
            
            // Alpha blending for the portal effect
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            glUseProgram(g_prog_portal);
            
            // Bind portal texture
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_portal_tex);
            glUniform1i(loc_portal_portal_tex, 0);
            
            // Pass the model-view matrix from the game's Draw call
            glUniformMatrix4fv(loc_portal_mv_matrix,
                               1, GL_FALSE, g_portal_vp_matrix);
            
            // Aspect ratio for perspective projection
            float aspect = (float)g_game_w / (float)g_game_h;
            glUniform1f(loc_portal_aspect, aspect);
            
            // Portal color and animation
            glUniform3f(loc_portal_color,
                        g_portal_color_r, g_portal_color_g, g_portal_color_b);
            glUniform1f(loc_portal_time, g_postfx_time);
            glUniform1f(loc_portal_speed, g_portal_speed);
            glUniform1f(loc_portal_alpha, g_portal_intensity);
            
            draw_fsq();  // Quad vertices at -1..1, vertex shader transforms via MVP
            
            // Restore GL state
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    // ======== PASS 3: Composite (scene * AO + god rays + bloom) at full-res ========
    if (do_composite) {
        // Pre-blit game color buffer (including pristine UI text) to g_postfx_fbo
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_postfx_fbo);
        glBlitFramebuffer(0, 0, g_game_w, g_game_h, 0, 0, g_game_w, g_game_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, g_postfx_fbo);
        glViewport(0, 0, g_game_w, g_game_h);
        glUseProgram(g_prog_composite);
        // Bind textures to different units
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fbo_tex);  // scene
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ao_tex ? ao_tex : g_fbo_tex);  // AO (or scene as dummy)
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gr_tex ? gr_tex : g_fbo_tex);  // god rays (or dummy)
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, bloom_tex ? bloom_tex : g_fbo_tex);  // bloom (or dummy)
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);  // depth for shadows
        glActiveTexture(GL_TEXTURE0);

        glUniform1i(loc_comp_scene, 0);
        glUniform1i(loc_comp_ao, 1);
        glUniform1i(loc_comp_godrays, 2);
        glUniform1i(loc_comp_bloom, 3);
        glUniform1i(loc_comp_depth, 4);
        glUniform1f(loc_comp_ao_enabled, ao_tex ? 1.0f : 0.0f);
        glUniform1f(loc_comp_gr_enabled, gr_tex ? 1.0f : 0.0f);
        glUniform1f(loc_comp_bloom_enabled, bloom_tex ? 1.0f : 0.0f);
        glUniform1f(loc_comp_bloom_intensity, postfx ? postfx->bloom_intensity : 0.0f);
        // Shadow uniforms
        glUniform1f(loc_comp_shadow_enabled, do_shadows ? 1.0f : 0.0f);
        glUniform1f(loc_comp_shadow_intensity, postfx ? postfx->shadow_intensity : 0.0f);
        glUniform1f(loc_comp_shadow_softness, postfx ? postfx->shadow_softness : 0.003f);
        glUniform2f(loc_comp_shadow_light_dir,
                    postfx ? postfx->shadow_light_x : 0.3f,
                    postfx ? postfx->shadow_light_y : -0.8f);
        // 3D shadow sun_z: positive = shadow ray toward camera/player side
        // Gives a natural angled shadow between the sun position and the player
        glUniform1f(loc_comp_shadow_sun_z, postfx ? postfx->shadow_sun_z : 0.15f);
        glUniform1f(loc_comp_shadow_near, 50.0f);
        glUniform1f(loc_comp_shadow_far, 20000.0f);
        // Warm golden tint for god rays
        glUniform1f(loc_comp_gr_tint_r, 1.0f);
        glUniform1f(loc_comp_gr_tint_g, 0.9f);
        glUniform1f(loc_comp_gr_tint_b, 0.7f);
        // Background-aware atmosphere from SRE
        glUniform1f(loc_comp_shadow_lift, 0.15f);  // subtle lift — keep SSAO dark
        glUniform1f(loc_comp_bg_depth, g_bg_depth);
        glUniform1f(loc_comp_bg_scale, g_bg_scale);
        // Warm golden ambient from sky
        glUniform1f(loc_comp_ambient_r, g_bg_ambient_r);
        glUniform1f(loc_comp_ambient_g, g_bg_ambient_g);
        glUniform1f(loc_comp_ambient_b, g_bg_ambient_b);
        if (loc_comp_tex_size != -1) {
            glUniform2f(loc_comp_tex_size, (float)g_game_w, (float)g_game_h);
        }
        if (loc_comp_pbr_enabled != -1) {
            glUniform1f(loc_comp_pbr_enabled, (postfx && postfx->pbr_enabled) ? 1.0f : 0.0f);
        }
        if (loc_comp_reflections_enabled != -1) {
            glUniform1f(loc_comp_reflections_enabled, (postfx && postfx->reflections_enabled) ? 1.0f : 0.0f);
        }
        if (loc_comp_reflection_intensity != -1) {
            glUniform1f(loc_comp_reflection_intensity, postfx ? postfx->reflection_intensity : 0.35f);
        }
        // ── Water identity, from the authoritative biome record ─────────────
        // Without this the composite falls back to "strongly blue", which the
        // game's actual water palette (0.0, 0.376, 0.256 - greener than blue)
        // fails, so the reflection never engaged and real water was shaded as
        // foliage instead. See is_water_chroma() in composite.glsl.
        if (loc_comp_water_valid != -1 || loc_comp_water_surface != -1) {
            const auto& wb = TextureAwareMapper::Instance().GetCurrentBiome();
            const bool has_water = wb.valid && wb.water && wb.entry != nullptr;
            if (loc_comp_water_surface != -1) {
                if (has_water) glUniform3f(loc_comp_water_surface,
                                          wb.entry->waterSurface[0],
                                          wb.entry->waterSurface[1],
                                          wb.entry->waterSurface[2]);
                else           glUniform3f(loc_comp_water_surface, 0.0f, 0.0f, 0.0f);
            }
            if (loc_comp_water_valid != -1) {
                glUniform1f(loc_comp_water_valid, has_water ? 1.0f : 0.0f);
            }
        }
        if (loc_comp_time != -1) {
            glUniform1f(loc_comp_time, g_postfx_time);
        }

        // ── Game-engine handshake — upload live GL state to composite shader ────
        // Find the first enabled light (or fall back to light0 data as-is).
        // This drives light-accurate shadow direction and diffuse tinting.
        {
            // Pick the first enabled light's data
            int best = 0;
            for (int li = 0; li < 8; li++) {
                if (g_frame_lights[li].enabled) { best = li; break; }
            }

            float safe_modelview[16];
            float safe_projection[16];
            memcpy(safe_modelview, g_current_modelview, 64);
            memcpy(safe_projection, g_current_projection, 64);
            
            bool mv_invalid = false;
            for (int i = 0; i < 16; i++) {
                if (std::isnan(safe_modelview[i]) || std::isinf(safe_modelview[i])) {
                    mv_invalid = true;
                    break;
                }
            }
            if (mv_invalid) {
                float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                memcpy(safe_modelview, identity, 64);
            }
            
            bool proj_invalid = false;
            for (int i = 0; i < 16; i++) {
                if (std::isnan(safe_projection[i]) || std::isinf(safe_projection[i])) {
                    proj_invalid = true;
                    break;
                }
            }
            if (proj_invalid) {
                float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
                memcpy(safe_projection, identity, 64);
            }

            float safe_light_pos[4];
            float safe_light_diff[4];
            float safe_light_amb[4];
            float safe_light_model_amb[4];
            float safe_mat_diff[4];
            float safe_cur_color[4];
            float safe_player_world[3];
            
            memcpy(safe_light_pos, g_frame_lights[best].position, 16);
            memcpy(safe_light_diff, g_frame_lights[best].diffuse, 16);
            memcpy(safe_light_amb, g_frame_lights[best].ambient, 16);
            memcpy(safe_light_model_amb, g_frame_light_model_ambient, 16);
            memcpy(safe_mat_diff, g_frame_material.diffuse, 16);
            memcpy(safe_cur_color, g_current_color, 16);
            memcpy(safe_player_world, g_host_hero_pos, 12);
            
            for (int i = 0; i < 4; i++) {
                if (std::isnan(safe_light_pos[i]) || std::isinf(safe_light_pos[i])) safe_light_pos[i] = 0.0f;
                if (std::isnan(safe_light_diff[i]) || std::isinf(safe_light_diff[i])) safe_light_diff[i] = 1.0f;
                if (std::isnan(safe_light_amb[i]) || std::isinf(safe_light_amb[i])) safe_light_amb[i] = 0.0f;
                if (std::isnan(safe_light_model_amb[i]) || std::isinf(safe_light_model_amb[i])) safe_light_model_amb[i] = 0.2f;
                if (std::isnan(safe_mat_diff[i]) || std::isinf(safe_mat_diff[i])) safe_mat_diff[i] = 0.8f;
                if (std::isnan(safe_cur_color[i]) || std::isinf(safe_cur_color[i])) safe_cur_color[i] = 1.0f;
            }
            for (int i = 0; i < 3; i++) {
                if (std::isnan(safe_player_world[i]) || std::isinf(safe_player_world[i])) safe_player_world[i] = 0.0f;
            }

            if (loc_comp_light0_pos     != -1)
                glUniform4fv(loc_comp_light0_pos,     1, safe_light_pos);
            if (loc_comp_light0_diffuse != -1)
                glUniform4fv(loc_comp_light0_diffuse, 1, safe_light_diff);
            if (loc_comp_light0_ambient != -1)
                glUniform4fv(loc_comp_light0_ambient, 1, safe_light_amb);
            if (loc_comp_light_model_amb != -1)
                glUniform4fv(loc_comp_light_model_amb, 1, safe_light_model_amb);
            if (loc_comp_mat_diffuse    != -1)
                glUniform4fv(loc_comp_mat_diffuse,    1, safe_mat_diff);
            if (loc_comp_cur_color      != -1)
                glUniform4fv(loc_comp_cur_color,      1, safe_cur_color);
            // Hero world position from host handshake
            if (loc_comp_player_world   != -1)
                glUniform3fv(loc_comp_player_world, 1, safe_player_world);
            if (loc_comp_modelview      != -1)
                glUniformMatrix4fv(loc_comp_modelview,  1, GL_FALSE, safe_modelview);
            if (loc_comp_projection     != -1)
                glUniformMatrix4fv(loc_comp_projection, 1, GL_FALSE, safe_projection);
        }

        // ── Dynamic lights: the level's own point lights, per pixel ───────────
        // LightManager is fed by the glLightfv publish path in
        // jni_bridge_arm64.cpp, which fires when a LightComponent places its light
        // and hands over the component's authored colour, position and attenuation
        // triple. This is the first consumer that buffer has ever had: before
        // this it was cleared at the top of every frame and thrown away empty, and
        // the shader lit the scene from a single GL-state guess instead.
        //
        // Adaptivity. 32 lights can be collected, but only a handful can matter on
        // one screen and the shader pays for every entry it loops over, so the
        // candidates are ranked by the reach they actually have on screen - their
        // projected radius, damped by how far off-centre they are - and only the
        // best MAX_DYNAMIC_LIGHTS are uploaded. A torch behind the camera or one
        // whose reach is smaller than a pixel is still collected, and must not
        // cost a loop iteration in every pixel of the frame.
        if (loc_comp_light_count != -1 || loc_comp_lights != -1) {
            struct Cand { float score; int index; };
            Cand  cands[LightManager::MAX_LIGHTS];
            int   n_cand = 0;

            const auto& collected = LightManager::Instance().GetActiveLights();
            const float* P = g_current_projection;

            for (size_t i = 0; i < collected.size() && n_cand < int(LightManager::MAX_LIGHTS); ++i) {
                const EnginePointLight& L = collected[i];
                if (L.radius <= 0.0f || L.intensity <= 0.0f) continue;  // global light

                // The stored position is EYE space (see EnginePointLight), so the
                // projection alone takes it to clip space - no modelview.
                const float cx = P[0] * L.position[0] + P[4] * L.position[1] + P[8]  * L.position[2] + P[12];
                const float cy = P[1] * L.position[0] + P[5] * L.position[1] + P[9]  * L.position[2] + P[13];
                const float cw = P[3] * L.position[0] + P[7] * L.position[1] + P[11] * L.position[2] + P[15];
                if (cw <= 1e-4f) continue;      // at or behind the eye plane

                const float ndc_x = cx / cw;
                const float ndc_y = cy / cw;
                // 1.35 rather than 1.0: a light just off-screen still spills onto
                // the edge of the frame, and dropping it there is a visible pop.
                if (std::fabs(ndc_x) > 1.35f || std::fabs(ndc_y) > 1.35f) continue;

                // Screen-space reach: the projection's scale terms applied to one
                // reach, divided by the same w. Rotation is irrelevant here because
                // a reach is a sphere, so the axis-aligned scale is the right one.
                const float r_ndc = std::max(std::fabs(P[0] * L.radius / cw),
                                             std::fabs(P[5] * L.radius / cw));
                if (r_ndc < 2.0f / float(std::max(1, g_game_h))) continue;  // sub-pixel

                const float off_centre = std::sqrt(ndc_x * ndc_x + ndc_y * ndc_y);
                cands[n_cand].score = r_ndc / (1.0f + off_centre);
                cands[n_cand].index = int(i);
                ++n_cand;
            }

            // Most on-screen reach first, then keep the best MAX_DYNAMIC_LIGHTS.
            std::sort(cands, cands + n_cand, [](const Cand& a, const Cand& b) {
                return a.score > b.score;
            });
            int picked = 0;
            {
                picked = std::min(n_cand, MAX_DYNAMIC_LIGHTS);

                float pos_buf[MAX_DYNAMIC_LIGHTS * 4] = {0.0f};
                float col_buf[MAX_DYNAMIC_LIGHTS * 4] = {0.0f};
                float att_buf[MAX_DYNAMIC_LIGHTS * 3] = {0.0f};
                for (int k = 0; k < picked; ++k) {
                    const EnginePointLight& L = collected[size_t(cands[k].index)];
                    pos_buf[k * 4 + 0] = L.position[0];
                    pos_buf[k * 4 + 1] = L.position[1];
                    pos_buf[k * 4 + 2] = L.position[2];
                    pos_buf[k * 4 + 3] = L.radius;
                    col_buf[k * 4 + 0] = L.color[0];
                    col_buf[k * 4 + 1] = L.color[1];
                    col_buf[k * 4 + 2] = L.color[2];
                    col_buf[k * 4 + 3] = L.intensity;
                    att_buf[k * 3 + 0] = L.attenuation[0];
                    att_buf[k * 3 + 1] = L.attenuation[1];
                    att_buf[k * 3 + 2] = L.attenuation[2];
                }
                if (loc_comp_lights != -1 && picked > 0) {
                    glUniform4fv(loc_comp_lights, picked, pos_buf);
                    glUniform4fv(loc_comp_light_color, picked, col_buf);
                    glUniform3fv(loc_comp_light_atten, picked, att_buf);
                }
            }
            // Always uploaded, including 0: a stale count from last frame would
            // make the shader accumulate lights that are no longer in the level.
            if (loc_comp_light_count != -1) {
                glUniform1i(loc_comp_light_count, picked);
            }
        }
        
        // ── UI/HUD isolation, now PER PIXEL inside the shader ───────────────────
        // This was a hardware stencil test (GL_EQUAL, ref 0) that skipped UI
        // pixels and left them to whatever the pre-blit above had put in
        // g_postfx_fbo. That holds only while the target is exactly the blit and
        // the previous frame's footprint still lines up - when it does not, the
        // skipped region surfaces as a stale rectangle exactly where the UI is.
        // Testing in the shader and WRITING the passthrough pixel is strictly
        // stronger: this pass then defines every pixel of its target this frame,
        // and the world/UI split stays per-pixel rather than per-draw.
        // Unit 5 is free here (0 scene, 1 ao, 2 godrays, 3 bloom, 4 depth).
        if (loc_comp_ui_mask != -1) {
            glUniform1i(loc_comp_ui_mask, 5);
        }
        if (loc_comp_ui_exempt != -1) {
            glUniform1f(loc_comp_ui_exempt, (postfx && postfx->ui_exempt) ? 1.0f : 0.0f);
        }
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
        glActiveTexture(GL_TEXTURE0);
        // The whole-frame bypass stays off: it would key off the 2D-frame
        // counters, which are deliberately not trusted for suppression (see the
        // comment on g_frame_has_ortho/g_frame_has_perspective). Uploaded
        // explicitly so a future writer of this uniform cannot half-enable it.
        if (loc_comp_ui_pass != -1) {
            glUniform1f(loc_comp_ui_pass, 0.0f);
        }
        draw_fsq();

        final_tex = g_postfx_tex;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, win_w, win_h);
    }

    // ======== PASS 4: Color PostFX (Tier 1 effects) ========
    if (do_postfx) {
        bool need_color_fx = postfx->vignette || postfx->film_grain || postfx->chromatic_aberration ||
                             postfx->color_adjust || postfx->sharpen || postfx->outlines;
        if (need_color_fx) {
            GLuint src_tex = final_tex;
            GLuint dst_fbo;
            if (do_composite) {
                // Composite wrote to g_postfx_fbo (g_postfx_tex).
                // Ping-pong: read from g_postfx_tex, write to g_postfx_fbo_b.
                dst_fbo = g_postfx_fbo_b;
            } else {
                // No composite pass; scene is still in g_fbo_tex.
                // Write color FX to g_postfx_fbo.
                dst_fbo = g_postfx_fbo;
            }
            glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
            glViewport(0, 0, g_game_w, g_game_h);
            glUseProgram(g_prog_postfx);
            glUniform1i(loc_postfx_tex, 0);
            glUniform1i(loc_postfx_depth_tex, 1);
            glUniform2f(loc_postfx_tex_size, (float)g_game_w, (float)g_game_h);
            glUniform1f(loc_postfx_time, g_postfx_time);
            glUniform1f(loc_postfx_vignette, postfx->vignette ? postfx->vignette_intensity : 0.0f);
            glUniform1f(loc_postfx_grain, postfx->film_grain ? postfx->grain_intensity : 0.0f);
            glUniform1f(loc_postfx_ca_offset, postfx->chromatic_aberration ? postfx->ca_offset : 0.0f);
            glUniform1f(loc_postfx_saturation, postfx->color_adjust ? postfx->saturation : 1.0f);
            glUniform1f(loc_postfx_contrast, postfx->color_adjust ? postfx->contrast : 1.0f);
            glUniform1f(loc_postfx_brightness, postfx->color_adjust ? postfx->brightness : 0.0f);
            glUniform1f(loc_postfx_warmth, postfx->color_adjust ? postfx->warmth : 0.0f);
            glUniform1f(loc_postfx_sharpen, postfx->sharpen ? postfx->sharpen_strength : 0.0f);
            // Outline uniforms
            glUniform1f(loc_postfx_outline_enabled, postfx->outlines ? 1.0f : 0.0f);
            glUniform1f(loc_postfx_outline_thickness, postfx->outline_thickness);
            glUniform1f(loc_postfx_outline_intensity, postfx->outline_intensity);
            glUniform1f(loc_postfx_outline_depth_threshold, postfx->outline_depth_threshold);
            glUniform1f(loc_postfx_outline_near, 50.0f);
            glUniform1f(loc_postfx_outline_far, 20000.0f);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src_tex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, g_fbo_depth_tex);
            glActiveTexture(GL_TEXTURE0);
            draw_fsq();
            // Update final_tex to point at whichever FBO we just wrote to
            final_tex = (dst_fbo == g_postfx_fbo_b) ? g_postfx_tex_b : g_postfx_tex;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, win_w, win_h);
        }
    }

    // ======== PASS 5: Final upscale blit to screen ========
    // Pixel-perfect shortcut
    if (g_game_w == win_w && g_game_h == win_h && final_tex == g_fbo_tex) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, g_game_w, g_game_h, 0, 0, win_w, win_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(old_prog);
        // Restore GL state
        if (saved_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (saved_blend)      glEnable(GL_BLEND);      else glDisable(GL_BLEND);
        if (saved_cull_face)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
        if (saved_scissor)    glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (saved_stencil)    glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (saved_alpha_test) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
        if (saved_lighting)   glEnable(GL_LIGHTING);   else glDisable(GL_LIGHTING);
        glBlendFunc(saved_blend_src, saved_blend_dst);
        glDepthFunc(saved_depth_func);
        glDepthMask(saved_depth_mask);
        glBindVertexArray(saved_vao);
        glActiveTexture(saved_active_tex);
        glViewport(0, 0, win_w, win_h);
        return;
    }

    GLuint prog = get_program(mode);
    if (!prog) {
        // Fallback blit
        glUseProgram(old_prog);
        // Restore GL state
        if (saved_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (saved_blend)      glEnable(GL_BLEND);      else glDisable(GL_BLEND);
        if (saved_cull_face)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
        if (saved_scissor)    glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (saved_stencil)    glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
        if (saved_alpha_test) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
        if (saved_lighting)   glEnable(GL_LIGHTING);   else glDisable(GL_LIGHTING);
        glBlendFunc(saved_blend_src, saved_blend_dst);
        glDepthFunc(saved_depth_func);
        glDepthMask(saved_depth_mask);
        glBindVertexArray(saved_vao);
        glActiveTexture(saved_active_tex);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, g_game_w, g_game_h, 0, 0, win_w, win_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        extern int g_sre_viewport_x, g_sre_viewport_y, g_sre_viewport_w, g_sre_viewport_h;
        g_sre_viewport_x = 0;
        g_sre_viewport_y = 0;
        g_sre_viewport_w = win_w;
        g_sre_viewport_h = win_h;
        glViewport(0, 0, win_w, win_h);
        return;
    }

    // Letterbox/pillarbox
    float game_aspect = (float)g_game_w / (float)g_game_h;
    float win_aspect  = (float)win_w / (float)win_h;
    int vx, vy, vw, vh;
    if (win_aspect > game_aspect) {
        vh = win_h; vw = (int)(win_h * game_aspect); vx = (win_w - vw) / 2; vy = 0;
    } else {
        vw = win_w; vh = (int)(win_w / game_aspect); vx = 0; vy = (win_h - vh) / 2;
    }
    extern int g_sre_viewport_x, g_sre_viewport_y, g_sre_viewport_w, g_sre_viewport_h;
    g_sre_viewport_x = vx;
    g_sre_viewport_y = vy;
    g_sre_viewport_w = vw;
    g_sre_viewport_h = vh;
    glViewport(vx, vy, vw, vh);

    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
    glUniform2f(glGetUniformLocation(prog, "u_tex_size"), (float)g_game_w, (float)g_game_h);
    glUniform2f(glGetUniformLocation(prog, "u_out_size"), (float)vw, (float)vh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, final_tex);
    // FSR does its own filtering — use NEAREST to avoid pre-blurring
    GLenum filter = (mode == FBOScale::FSR || mode == FBOScale::NEAREST) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    draw_fsq();

    glUseProgram(old_prog);
    // Restore GL state
    if (saved_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (saved_blend)      glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (saved_cull_face)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
    if (saved_scissor)    glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (saved_stencil)    glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (saved_alpha_test) glEnable(GL_ALPHA_TEST); else glDisable(GL_ALPHA_TEST);
    if (saved_lighting)   glEnable(GL_LIGHTING);   else glDisable(GL_LIGHTING);
    glBlendFunc(saved_blend_src, saved_blend_dst);
    glDepthFunc(saved_depth_func);
    glDepthMask(saved_depth_mask);
    glBindVertexArray(saved_vao);
    glActiveTexture(saved_active_tex);
    glViewport(0, 0, win_w, win_h);
}

// ======================= DECOUPLED GRAPHICS & POSTFX =======================

const char* graphics_tier_name(GraphicsTier tier) {
    switch (tier) {
        case GraphicsTier::LOW:          return "Low";
        case GraphicsTier::MEDIUM:       return "Medium";
        case GraphicsTier::HIGH:         return "High";
        case GraphicsTier::SW_PLUS:      return "SW+";
        case GraphicsTier::SW_PLUS_HIGH: return "SW+ High";
        default:                         return "Custom";
    }
}

const char* postfx_filter_name(PostEffectFilter filter) {
    switch (filter) {
        case PostEffectFilter::NONE:           return "None";
        case PostEffectFilter::RETRO_TV:       return "Retro TV (CRT)";
        case PostEffectFilter::OUTLINE_MILD:   return "Mild Outline";
        case PostEffectFilter::OUTLINE_HEAVY:  return "Heavy Outline";
        case PostEffectFilter::VHS_GLITCH:     return "VHS Glitch";
        case PostEffectFilter::VIGNETTE_GRAIN: return "Vignette & Grain";
        default:                               return "Custom";
    }
}

// ---------------------------------------------------------------------------
//  Boot defaults - the single source of truth for "what runs on a cold start"
// ---------------------------------------------------------------------------
void postfx_apply_boot_defaults(PostFXState& s) {
    // Tier first, cosmetic filter second - the same order the F6 menu uses.
    // graphics_apply_tier() now also neutralises the cosmetic overlays (see the
    // header note on that function), so this pair leaves no residue from the
    // struct's default member initialisers, and it leaves .graphics_tier and
    // .preset_name describing the SAME state the renderer is running.
    graphics_apply_tier(s, GraphicsTier::SW_PLUS);
    postfx_apply_filter(s, PostEffectFilter::NONE);

    s.enabled = true;
    // The F6 modal prints these two strings, and the F3 debug panel prints
    // preset_name. They must name the boot state, not a legacy preset enum.
    s.preset_name = "SW+ Medium";
    s.filter_name = "None";
}

void graphics_apply_tier(PostFXState& s, GraphicsTier tier) {
    s.graphics_tier = tier;
    s.preset_name = graphics_tier_name(tier);
    s.enabled = true;

    // ── Neutralise the cosmetic overlays ────────────────────────────────────
    // These four belong to the PostEffectFilter axis, NOT to the tier axis. A
    // tier switch must not inherit them, because postfx_apply_preset() and the
    // filter buttons both set them, and switching tier afterwards used to leave
    // e.g. vignette_intensity = 0.6 from the Noir preset in place while the menu
    // displayed a clean "SW+". That is a large part of why the tier looked
    // unexpectedly dark: a vignette of 0.3-0.6 darkens the corners and reads as
    // "the whole setting is too dark".
    //
    // Callers that DO want a filter re-apply it immediately afterwards
    // (draw_graphics_options_modal does exactly this), which is the intended
    // composition: pick a tier, then layer a cosmetic filter on top.
    s.vignette             = false;
    s.vignette_intensity   = 0.30f;
    s.film_grain           = false;
    s.grain_intensity      = 0.06f;
    s.chromatic_aberration = false;
    s.ca_offset            = 0.002f;
    s.outlines             = false;
    s.outline_thickness    = 1.0f;
    s.outline_intensity    = 0.70f;
    s.outline_depth_threshold = 0.002f;

    // Reset physical engine passes according to tier
    switch (tier) {
        case GraphicsTier::LOW:
            s.pbr_enabled = false;
            s.procedural_detail = false;
            s.procedural_strength = 0.0f;
            s.reflections_enabled = false;
            s.ssao = false;
            s.volumetric_light = false;
            s.god_rays = false;
            s.bloom = false;
            s.shadows = false;
            s.sharpen = false;
            s.color_adjust = true;
            s.warmth = 0.0f;
            s.contrast = 1.0f;
            s.saturation = 1.0f;
            s.brightness = 0.0f;
            break;

        case GraphicsTier::MEDIUM:
            s.pbr_enabled = false;
            s.procedural_detail = true;
            s.procedural_strength = 0.5f;
            s.reflections_enabled = false;
            s.ssao = true;
            s.ssao_radius = 0.030f;
            s.ssao_intensity = 0.70f;
            s.volumetric_light = false;
            s.god_rays = false;
            s.bloom = true;
            s.bloom_threshold = 0.85f;
            s.bloom_intensity = 0.40f;
            s.shadows = true;
            s.shadow_intensity = 0.20f;
            s.shadow_softness = 0.005f;
            s.sharpen = true;
            s.sharpen_strength = 0.15f;
            s.color_adjust = true;
            s.warmth = 0.04f;
            s.contrast = 1.01f;
            s.saturation = 1.05f;
            s.brightness = 0.01f;
            break;

        case GraphicsTier::HIGH:
            s.pbr_enabled = true;
            s.procedural_detail = true;
            s.procedural_strength = 0.85f;
            s.reflections_enabled = false;
            s.ssao = true;
            s.ssao_radius = 0.036f;
            s.ssao_intensity = 0.90f;
            s.volumetric_light = true;
            s.volumetric_intensity = 0.25f;
            s.god_rays = true;
            s.god_rays_intensity = 0.30f;
            s.god_rays_decay = 0.965f;
            s.sun_x = 0.58f;
            s.sun_y = 1.12f;
            s.bloom = true;
            s.bloom_threshold = 0.82f;
            s.bloom_intensity = 0.55f;
            s.shadows = true;
            s.shadow_intensity = 0.30f;
            s.shadow_softness = 0.006f;
            s.shadow_light_x = -0.10f;
            s.shadow_light_y = 0.85f;
            s.shadow_sun_z = 0.16f;
            s.sharpen = true;
            s.sharpen_strength = 0.20f;
            s.color_adjust = true;
            s.warmth = 0.06f;
            s.contrast = 1.02f;
            s.saturation = 1.08f;
            s.brightness = 0.01f;
            break;

        case GraphicsTier::SW_PLUS:
            // ── Balance note (the "SW+ is too dark" fix) ────────────────────
            // Three multiplicative darkeners were stacking here:
            //   * scene ambient was being REPLACED by a ~0.05 biome value
            //     (fixed in ffp_forward.cpp - now additive)
            //   * SSAO 1.05 and contact shadows 0.32 both subtract light, and
            //     they overlap on the same ground/contact regions
            //   * contrast 1.03 pushes midtones down while brightness 0.02 does
            //     not compensate
            // The two AO/shadow strengths are now in a range where they add
            // depth instead of eating the image, and brightness/contrast give a
            // small net lift. Saturation stays high: the remaster's intent is
            // "vivid", and boosting saturation without the exposure to match is
            // what made it read as murky rather than rich.
            s.pbr_enabled = true;
            s.procedural_detail = true;
            s.procedural_strength = 1.0f;
            s.reflections_enabled = true;
            s.reflection_intensity = 0.35f;
            s.ssao = true;
            s.ssao_radius = 0.036f;
            s.ssao_intensity = 0.75f;
            s.volumetric_light = true;
            s.volumetric_intensity = 0.32f;
            s.god_rays = true;
            s.god_rays_intensity = 0.40f;
            s.god_rays_decay = 0.965f;
            s.sun_x = 0.58f;
            s.sun_y = 1.12f;
            s.bloom = true;
            s.bloom_threshold = 0.80f;
            s.bloom_intensity = 0.65f;
            s.shadows = true;
            s.shadow_intensity = 0.22f;
            s.shadow_softness = 0.007f;
            s.shadow_light_x = -0.10f;
            s.shadow_light_y = 0.85f;
            s.shadow_sun_z = 0.16f;
            s.sharpen = true;
            s.sharpen_strength = 0.22f;
            s.color_adjust = true;
            s.warmth = 0.08f;
            s.contrast = 1.015f;
            s.saturation = 1.10f;
            s.brightness = 0.045f;
            break;

        case GraphicsTier::SW_PLUS_HIGH:
            // Same balance rationale as SW_PLUS, one notch stronger.
            s.pbr_enabled = true;
            s.procedural_detail = true;
            s.procedural_strength = 1.25f;
            s.reflections_enabled = true;
            s.reflection_intensity = 0.50f;
            s.ssao = true;
            s.ssao_radius = 0.040f;
            s.ssao_intensity = 0.90f;
            s.volumetric_light = true;
            s.volumetric_intensity = 0.38f;
            s.god_rays = true;
            s.god_rays_intensity = 0.48f;
            s.god_rays_decay = 0.968f;
            s.sun_x = 0.58f;
            s.sun_y = 1.12f;
            s.bloom = true;
            s.bloom_threshold = 0.78f;
            s.bloom_intensity = 0.75f;
            s.shadows = true;
            s.shadow_intensity = 0.25f;
            s.shadow_softness = 0.008f;
            s.shadow_light_x = -0.10f;
            s.shadow_light_y = 0.85f;
            s.shadow_sun_z = 0.18f;
            s.sharpen = true;
            s.sharpen_strength = 0.25f;
            s.color_adjust = true;
            s.warmth = 0.10f;
            s.contrast = 1.02f;
            s.saturation = 1.13f;
            s.brightness = 0.05f;
            break;

        default:
            break;
    }
}

void postfx_apply_filter(PostFXState& s, PostEffectFilter filter, float intensity) {
    s.postfx_filter = filter;
    s.postfx_filter_intensity = intensity;
    s.filter_name = postfx_filter_name(filter);

    // Reset purely cosmetic overlays
    s.vignette = false;
    s.film_grain = false;
    s.chromatic_aberration = false;
    s.outlines = false;

    switch (filter) {
        case PostEffectFilter::NONE:
            break;

        case PostEffectFilter::RETRO_TV:
            s.vignette = true;
            s.vignette_intensity = 0.35f * intensity;
            s.film_grain = true;
            s.grain_intensity = 0.08f * intensity;
            s.chromatic_aberration = true;
            s.ca_offset = 0.0035f * intensity;
            break;

        case PostEffectFilter::OUTLINE_MILD:
            s.outlines = true;
            s.outline_thickness = 1.0f;
            s.outline_intensity = 0.55f * intensity;
            s.outline_depth_threshold = 0.0025f;
            break;

        case PostEffectFilter::OUTLINE_HEAVY:
            s.outlines = true;
            s.outline_thickness = 2.0f;
            s.outline_intensity = 0.90f * intensity;
            s.outline_depth_threshold = 0.0018f;
            break;

        case PostEffectFilter::VHS_GLITCH:
            s.film_grain = true;
            s.grain_intensity = 0.12f * intensity;
            s.chromatic_aberration = true;
            s.ca_offset = 0.005f * intensity;
            break;

        case PostEffectFilter::VIGNETTE_GRAIN:
            s.vignette = true;
            s.vignette_intensity = 0.30f * intensity;
            s.film_grain = true;
            s.grain_intensity = 0.04f * intensity;
            break;

        default:
            break;
    }
}

// ======================= PRESETS =======================

void postfx_apply_preset(PostFXState& s, PostFXPreset p) {
    // Reset everything
    s = PostFXState{};
    
    switch (p) {
        case PostFXPreset::OFF:
            s.enabled = true;
            s.ssao = true;
            s.ssao_radius = 0.036f;
            s.ssao_intensity = 0.95f; // Enabled again :)
            s.shadow_intensity = 0.20f;
            s.shadow_softness = 0.004f;
            s.shadow_light_x = 0.3f;
            s.shadow_light_y = -0.8f;
            s.color_adjust = true;
            s.warmth = 0.05f;
            s.contrast = 1.0f;
            s.saturation = 1.05f;
            s.brightness = 0.06f;
            s.vignette = true;
            s.vignette_intensity = 0.12f;
            s.preset_name = "Low";
            break;
        case PostFXPreset::SW_PLUS_MEDIUM:
            s.enabled = true;

            // SSAO — gentle contact depth, preserve Swordigo's soft geometry (made lighter)
            s.ssao = true;
            s.ssao_radius = 0.036f;
            s.ssao_intensity = 0.85f;

            // God rays — atmospheric, not dominant
            s.god_rays = true;
            s.god_rays_intensity = 0.35f;
            s.god_rays_decay = 0.965f;

            // Sun above screen
            s.sun_x = 0.58f;
            s.sun_y = 1.12f;

            // Volumetric atmosphere
            s.volumetric_light = true;
            s.volumetric_intensity = 0.32f;

            // Color grading — vibrant, warm, bright, rich
            s.color_adjust = true;
            s.warmth = 0.08f;
            s.contrast = 1.02f;
            s.saturation = 1.10f;
            s.brightness = 0.0095f;

            // Soft vignette
            s.vignette = true;
            s.vignette_intensity = 0.12f;

            // Crisp texture detail without ringing
            s.sharpen = true;
            s.sharpen_strength = 0.21f;

            // Controlled bloom
            s.bloom = true;
            s.bloom_threshold = 0.82f;
            s.bloom_intensity = 0.59f;

            // Soft directional shadows
            s.shadows = true;
            s.shadow_intensity = 0.35f;
            s.shadow_softness = 0.0080f;
            s.shadow_light_x = -0.10f;
            s.shadow_light_y = 0.85f;
            s.shadow_sun_z = 0.16f;

            s.pbr_enabled = false;
            s.reflections_enabled = false;
            s.preset_name = "Sw+ Med";
            break;

        case PostFXPreset::SW_PLUS_HIGH:
            s.enabled = true;

            // SSAO — gentle contact depth, preserve Swordigo's soft geometry (made lighter)
            s.ssao = true;
            s.ssao_radius = 0.036f;
            s.ssao_intensity = 1.05f;

            // God rays — atmospheric, not dominant
            s.god_rays = true;
            s.god_rays_intensity = 0.42f;
            s.god_rays_decay = 0.965f;

            // Sun above screen
            s.sun_x = 0.58f;
            s.sun_y = 1.12f;

            // Volumetric atmosphere
            s.volumetric_light = true;
            s.volumetric_intensity = 0.28f;

            // Color grading — vibrant, warm, bright, rich
            s.color_adjust = true;
            s.warmth = 0.08f;
            s.contrast = 1.03f;
            s.saturation = 1.12f;
            s.brightness = 0.02f;

            // Soft vignette
            s.vignette = true;
            s.vignette_intensity = 0.10f;

            // Crisp texture detail without ringing
            s.sharpen = true;
            s.sharpen_strength = 0.21f;

            // Remastered: Subtle cinematic chromatic aberration
            s.chromatic_aberration = true;
            s.ca_offset = 0.0006f;

            // Remastered: Subtle organic film grain
            s.film_grain = true;
            s.grain_intensity = 0.015f;

            // Controlled bloom — only actual luminous/highlight regions (boosted)
            s.bloom = true;
            s.bloom_threshold = 0.80f;
            s.bloom_intensity = 0.70f;

            // Soft directional shadows
            s.shadows = true;
            s.shadow_intensity = 0.28f;
            s.shadow_softness = 0.0080f;
            s.shadow_light_x = -0.10f;
            s.shadow_light_y = 0.85f;
            s.shadow_sun_z = 0.16f;

            // Soft contours
            s.outlines = true;
            s.outline_thickness = 1.2f;
            s.outline_intensity = 0.18f;
            s.outline_depth_threshold = 0.0035f;

            s.pbr_enabled = true;
            s.reflections_enabled = true;
            s.reflection_intensity = 0.35f;
            s.preset_name = "Sw+ High";
            break;
        case PostFXPreset::ATMOSPHERIC:
            s.enabled = true;
            s.ssao = true; s.ssao_radius = 0.025f; s.ssao_intensity = 0.8f;
            s.volumetric_light = true; s.volumetric_intensity = 0.32f;
            s.sun_x = 0.7f; s.sun_y = 0.85f;
            s.vignette = true; s.vignette_intensity = 0.15f;
            s.color_adjust = true; 
            s.contrast = 1.02f;
            s.saturation = 1.08f;
            s.brightness = 0.06f;
            s.bloom = true; s.bloom_threshold = 0.7f; s.bloom_intensity = 0.3f;
            s.shadows = true; s.shadow_intensity = 0.28f; s.shadow_softness = 0.004f;
            s.shadow_light_x = 0.4f; s.shadow_light_y = -0.7f;
            s.preset_name = "Atmospheric";
            break;
        case PostFXPreset::ETHEREAL:
            s.enabled = true;
            s.god_rays = true; s.god_rays_intensity = 0.5f; s.god_rays_decay = 0.95f;
            s.sun_x = 0.5f; s.sun_y = 0.9f;
            s.vignette = true; s.vignette_intensity = 0.2f;
            s.color_adjust = true; s.warmth = 0.4f; s.brightness = 0.03f; s.saturation = 1.1f;
            s.bloom = true; s.bloom_threshold = 0.6f; s.bloom_intensity = 0.35f;
            s.preset_name = "Ethereal";
            break;
        case PostFXPreset::CINEMATIC:
            s.enabled = true;
            s.vignette = true; s.vignette_intensity = 0.4f;
            s.film_grain = true; s.grain_intensity = 0.04f;
            s.color_adjust = true; s.warmth = 0.3f; s.contrast = 1.1f; s.saturation = 1.05f;
            s.bloom = true; s.bloom_threshold = 0.75f; s.bloom_intensity = 0.2f;
            s.preset_name = "Cinematic";
            break;
        case PostFXPreset::RETRO:
            s.enabled = true;
            s.film_grain = true; s.grain_intensity = 0.08f;
            s.color_adjust = true; s.saturation = 1.3f; s.contrast = 1.15f;
            s.chromatic_aberration = true; s.ca_offset = 0.003f;
            s.preset_name = "Retro";
            break;
        case PostFXPreset::FANTASY:
            s.enabled = true;
            s.vignette = true; s.vignette_intensity = 0.25f;
            s.color_adjust = true; s.saturation = 1.2f; s.warmth = 0.2f; s.brightness = 0.02f;
            s.sharpen = true; s.sharpen_strength = 0.3f;
            s.bloom = true; s.bloom_threshold = 0.65f; s.bloom_intensity = 0.3f;
            // Bold outlines — storybook illustration style
            s.outlines = true; s.outline_thickness = 1.5f; s.outline_intensity = 0.7f;
            s.outline_depth_threshold = 0.0015f;
            s.preset_name = "Fantasy";
            break;
        case PostFXPreset::NOIR:
            s.enabled = true;
            s.vignette = true; s.vignette_intensity = 0.6f;
            s.film_grain = true; s.grain_intensity = 0.07f;
            s.color_adjust = true; s.saturation = 0.15f; s.contrast = 1.4f; s.brightness = -0.03f;
            s.preset_name = "Noir";
            break;
        case PostFXPreset::CUSTOM:
            s.enabled = true;
            s.preset_name = "Custom";
            if (!postfx_load_custom_json(s)) {
                postfx_save_default_json();
                postfx_load_custom_json(s);
            }
            break;
        default: break;
    }
}

bool postfx_load_custom_json(PostFXState& state) {
    const char* home = std::getenv("HOME");
    if (!home) return false;
    std::filesystem::path config_path = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "shaders" / "postfx_config.json";
    
    std::ifstream file(config_path);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        size_t comment_pos = line.find("//");
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);
        comment_pos = line.find("#");
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, (last - first + 1));

        if (line.empty() || line == "{" || line == "}" || line == "},") continue;

        size_t quote_first = line.find('\"');
        if (quote_first == std::string::npos) continue;
        size_t quote_second = line.find('\"', quote_first + 1);
        if (quote_second == std::string::npos) continue;

        std::string key = line.substr(quote_first + 1, quote_second - quote_first - 1);

        size_t colon = line.find(':', quote_second);
        if (colon == std::string::npos) continue;

        std::string val_str = line.substr(colon + 1);
        size_t val_first = val_str.find_first_not_of(" \t\r\n");
        if (val_first == std::string::npos) continue;
        size_t val_last = val_str.find_last_not_of(" \t\r\n,");
        if (val_last == std::string::npos) continue;
        val_str = val_str.substr(val_first, (val_last - val_first + 1));

        auto parse_bool = [](const std::string& s) -> bool {
            return (s == "true" || s == "1");
        };
        auto parse_float = [](const std::string& s) -> float {
            try {
                return std::stof(s);
            } catch (...) {
                return 0.0f;
            }
        };
        auto parse_int = [](const std::string& s) -> int {
            try {
                return std::stoi(s);
            } catch (...) {
                return -1;
            }
        };

        if (key == "enabled") state.enabled = parse_bool(val_str);
        else if (key == "vignette") state.vignette = parse_bool(val_str);
        else if (key == "vignette_intensity") state.vignette_intensity = parse_float(val_str);
        else if (key == "film_grain") state.film_grain = parse_bool(val_str);
        else if (key == "grain_intensity") state.grain_intensity = parse_float(val_str);
        else if (key == "chromatic_aberration") state.chromatic_aberration = parse_bool(val_str);
        else if (key == "ca_offset") state.ca_offset = parse_float(val_str);
        else if (key == "color_adjust") state.color_adjust = parse_bool(val_str);
        else if (key == "saturation") state.saturation = parse_float(val_str);
        else if (key == "contrast") state.contrast = parse_float(val_str);
        else if (key == "brightness") state.brightness = parse_float(val_str);
        else if (key == "warmth") state.warmth = parse_float(val_str);
        else if (key == "sharpen") state.sharpen = parse_bool(val_str);
        else if (key == "sharpen_strength") state.sharpen_strength = parse_float(val_str);
        else if (key == "god_rays") state.god_rays = parse_bool(val_str);
        else if (key == "god_rays_intensity") state.god_rays_intensity = parse_float(val_str);
        else if (key == "god_rays_decay") state.god_rays_decay = parse_float(val_str);
        else if (key == "ssao") state.ssao = parse_bool(val_str);
        else if (key == "ssao_radius") state.ssao_radius = parse_float(val_str);
        else if (key == "ssao_intensity") state.ssao_intensity = parse_float(val_str);
        else if (key == "volumetric_light") state.volumetric_light = parse_bool(val_str);
        else if (key == "volumetric_intensity") state.volumetric_intensity = parse_float(val_str);
        else if (key == "bloom") state.bloom = parse_bool(val_str);
        else if (key == "bloom_threshold") state.bloom_threshold = parse_float(val_str);
        else if (key == "bloom_intensity") state.bloom_intensity = parse_float(val_str);
        // Authored-emitter bloom key (see bloom_extract.glsl).
        else if (key == "bloom_emitter_key") state.bloom_emitter_key = parse_bool(val_str);
        else if (key == "bloom_emitter_luma") state.bloom_emitter_luma = parse_float(val_str);
        else if (key == "bloom_emitter_sat") state.bloom_emitter_sat = parse_float(val_str);
        else if (key == "bloom_emitter_contrast") state.bloom_emitter_contrast = parse_float(val_str);
        else if (key == "bloom_emitter_gain") state.bloom_emitter_gain = parse_float(val_str);
        else if (key == "shadows") state.shadows = parse_bool(val_str);
        else if (key == "shadow_intensity") state.shadow_intensity = parse_float(val_str);
        else if (key == "shadow_softness") state.shadow_softness = parse_float(val_str);
        else if (key == "outlines") state.outlines = parse_bool(val_str);
        else if (key == "outline_thickness") state.outline_thickness = parse_float(val_str);
        else if (key == "outline_intensity") state.outline_intensity = parse_float(val_str);
        else if (key == "outline_depth_threshold") state.outline_depth_threshold = parse_float(val_str);
        // UI/HUD exemption from the bloom extract and the composite (stencil mask).
        else if (key == "ui_exempt") state.ui_exempt = parse_bool(val_str);
        // Fixed-function translation lighting level. Pushed to the layer that
        // owns lighting immediately, so loading the config is enough to change
        // it: no restart, and no shader rebuild, because ffp_forward uploads the
        // level as a uniform on every draw.
        else if (key == "ffp_lighting_level") state.ffp_lighting_level = parse_int(val_str);
    }
    if (state.ffp_lighting_level >= 0) {
        ffp::set_quality(state.ffp_lighting_level);
    }
    return true;
}

void postfx_save_default_json() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::filesystem::path shaders_dir = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "shaders";
    std::error_code ec;
    std::filesystem::create_directories(shaders_dir, ec);
    std::filesystem::path config_path = shaders_dir / "postfx_config.json";
    
    std::ofstream out(config_path);
    if (!out.is_open()) return;

    out << "{\n"
        << "  // ==========================================\n"
        << "  // Swordigo Desktop Custom PostFX Config\n"
        << "  // ==========================================\n\n"
        << "  // Enable post-processing effects globally (true/false)\n"
        << "  \"enabled\": true,\n\n"
        << "  // --- Tier 1: Color & Screen-Space Effects ---\n\n"
        << "  // Vignette (darkened edges)\n"
        << "  \"vignette\": true,\n"
        << "  \"vignette_intensity\": 0.3,\n\n"
        << "  // Film Grain (retro analog noise)\n"
        << "  \"film_grain\": false,\n"
        << "  \"grain_intensity\": 0.06,\n\n"
        << "  // Chromatic Aberration (color fringing at edges)\n"
        << "  \"chromatic_aberration\": false,\n"
        << "  \"ca_offset\": 0.002,\n\n"
        << "  // Color Adjustments (Brightness, Contrast, Saturation, Warmth)\n"
        << "  \"color_adjust\": true,\n"
        << "  \"saturation\": 1.0,\n"
        << "  \"contrast\": 1.0,\n"
        << "  \"brightness\": 0.0,\n"
        << "  \"warmth\": 0.0,\n\n"
        << "  // Sharpening\n"
        << "  \"sharpen\": false,\n"
        << "  \"sharpen_strength\": 0.4,\n\n"
        << "  // --- Tier 2: Advanced Effects ---\n\n"
        << "  // God Rays (radial light bursts)\n"
        << "  \"god_rays\": false,\n"
        << "  \"god_rays_intensity\": 0.4,\n"
        << "  \"god_rays_decay\": 0.96,\n\n"
        << "  // Screen Space Ambient Occlusion (SSAO)\n"
        << "  \"ssao\": false,\n"
        << "  \"ssao_radius\": 0.02,\n"
        << "  \"ssao_intensity\": 1.2,\n\n"
        << "  // Volumetric Lighting\n"
        << "  \"volumetric_light\": false,\n"
        << "  \"volumetric_intensity\": 0.3,\n\n"
        << "  // Bloom (glow on light sources)\n"
        << "  \"bloom\": false,\n"
        << "  // true  = bloom only genuine light sources: pixels matching one of the\n"
        << "  //         current biome's authored emitter colours (torch, lantern pods,\n"
        << "  //         lava) that are brighter than their own neighbourhood.\n"
        << "  // false = the legacy single luminance threshold below, which also lets\n"
        << "  //         flat bright surfaces (sky, ice) bloom.\n"
        << "  \"bloom_emitter_key\": true,\n"
        << "  \"bloom_emitter_luma\": 0.10,\n"
        << "  \"bloom_emitter_sat\": 0.80,\n"
        << "  \"bloom_emitter_contrast\": 1.12,\n"
        << "  \"bloom_emitter_gain\": 1.6,\n"
        << "  \"bloom_threshold\": 0.7,\n"
        << "  \"bloom_intensity\": 0.35,\n\n"
        << "  // Screen-space Shadows\n"
        << "  \"shadows\": false,\n"
        << "  \"shadow_intensity\": 0.5,\n"
        << "  \"shadow_softness\": 0.003,\n\n"
        << "  // Cel-shaded Outlines\n"
        << "  \"outlines\": false,\n"
        << "  \"outline_thickness\": 1.0,\n"
        << "  \"outline_intensity\": 0.7,\n"
        << "  \"outline_depth_threshold\": 0.002,\n\n"
        << "  // --- UI/HUD exemption (scene stencil mask) ---\n"
        << "  // true = the bloom extract skips HUD/2D pixels, so a round HUD sprite no\n"
        << "  //        longer grows a square halo of glow, and the composite writes them\n"
        << "  //        through instead of relighting them with the 3D light handshake.\n"
        << "  \"ui_exempt\": true,\n\n"
        << "  // --- Lighting model (the fixed-function translation layer) ---\n"
        << "  // Which shading model the layer that OWNS lighting uses:\n"
        << "  //   0  untouched fixed-function          1  GLES1 parity (per-vertex)\n"
        << "  //   2  + per-fragment lighting + normals 3  + parallax occlusion mapping\n"
        << "  //   4  + Cook-Torrance GGX PBR\n"
        << "  // 4 is the heaviest: per-fragment lighting AND a derived normal map on\n"
        << "  // EVERY draw, which is the first thing to lower when the frame is slow.\n"
        << "  // 2 keeps per-fragment lighting and drops POM/GGX. -1 means this key has\n"
        << "  // not been set, and the SWORDIGO_FFP_LIGHTING variable (or the layer's\n"
        << "  // own default) decides instead.\n"
        << "  \"ffp_lighting_level\": -1\n"
        << "}\n";
    out.close();
}

// ---------------------------------------------------------------------------
//  Persist one key in place: the lighting level
// ---------------------------------------------------------------------------
// postfx_save_default_json() writes a template of fixed defaults, so it cannot
// record a choice made at runtime - reloading the config after it would just put
// the defaults back. This rewrites the single line that carries the key and
// leaves everything else byte-for-byte as it was, so a hand-commented config
// survives a GUI change. If the key is absent (an older config written before it
// existed) it is inserted before the closing brace.
bool postfx_save_lighting_level(int level) {
    const char* home = std::getenv("HOME");
    if (!home) return false;
    std::filesystem::path config_path = std::filesystem::path(home) / ".local" /
                                        "share" / "swordigo-desktop" / "shaders" /
                                        "postfx_config.json";

    std::vector<std::string> lines;
    {
        std::ifstream in(config_path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
    }
    // No file yet: let the template writer create one that already has the key.
    if (lines.empty()) {
        postfx_save_default_json();
        std::ifstream in(config_path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        if (lines.empty()) return false;
    }

    const std::string key = "\"ffp_lighting_level\"";
    const std::string entry = key + ": " + std::to_string(level) + ",";

    bool replaced = false;
    for (auto& l : lines) {
        if (l.find(key) != std::string::npos) {
            size_t indent = l.find_first_not_of(" \t");
            if (indent == std::string::npos) indent = 0;
            const bool had_comma = l.find(',') != std::string::npos;
            l = l.substr(0, indent) + entry;
            if (!had_comma) l.pop_back();   // last key in the object: no comma
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        for (size_t i = lines.size(); i-- > 0; ) {
            if (lines[i].find('}') != std::string::npos) {
                lines.insert(lines.begin() + static_cast<long>(i), "  " + entry);
                break;
            }
        }
    }

    std::ofstream out(config_path);
    if (!out.is_open()) return false;
    for (const auto& l : lines) out << l << "\n";
    return true;
}

const char* postfx_preset_name(PostFXPreset p) {
    switch (p) {
        case PostFXPreset::OFF: return "Low";
        case PostFXPreset::SW_PLUS_MEDIUM: return "Sw+ Med";
        case PostFXPreset::SW_PLUS_HIGH: return "Sw+ High";
        case PostFXPreset::ATMOSPHERIC: return "Atmospheric";
        case PostFXPreset::ETHEREAL: return "Ethereal";
        case PostFXPreset::CINEMATIC: return "Cinematic";
        case PostFXPreset::RETRO: return "Retro";
        case PostFXPreset::FANTASY: return "Fantasy";
        case PostFXPreset::NOIR: return "Noir";
        case PostFXPreset::CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

bool fbo_is_active() { return g_fbo_ok; }
unsigned int fbo_get_texture() { return g_fbo_tex; }

// ======== Vanilla Portal Rendering (no FBO pipeline required) ========
// Renders the portal effect directly into whatever framebuffer is currently bound.
// Uses the same shader and MVP matrix approach as the PostFX path.
void fbo_draw_portal_vanilla(int viewport_w, int viewport_h) {
    // Update animation time (g_postfx_time only updates when FBO is active)
    static float vanilla_portal_time = 0.0f;
    vanilla_portal_time += 0.016f;
    if (vanilla_portal_time > 10000.0f) vanilla_portal_time = 0.0f;
    
    if (g_portal_active <= 0.5f) return;
    
    // Compile portal shader on first call if not already done
    if (!g_prog_portal) {
        g_prog_portal = link_program(VERT_PORTAL, FRAG_PORTAL);
        if (g_prog_portal) {
            std::cout << "[PORTAL-VANILLA] Shader compiled OK (program=" << g_prog_portal << ")" << std::endl;
            // Cache uniform locations for lazy-compiled portal shader
            loc_portal_portal_tex = glGetUniformLocation(g_prog_portal, "u_portal_tex");
            loc_portal_mv_matrix  = glGetUniformLocation(g_prog_portal, "u_mv_matrix");
            loc_portal_aspect     = glGetUniformLocation(g_prog_portal, "u_aspect");
            loc_portal_color      = glGetUniformLocation(g_prog_portal, "u_color");
            loc_portal_time       = glGetUniformLocation(g_prog_portal, "u_time");
            loc_portal_speed      = glGetUniformLocation(g_prog_portal, "u_speed");
            loc_portal_alpha      = glGetUniformLocation(g_prog_portal, "u_alpha");
        } else {
            std::cerr << "[PORTAL-VANILLA] Shader FAILED to compile" << std::endl;
            return;
        }
    }
    
    // Load portal texture on first use
    if (!g_portal_tex_loaded) {
        extern std::string g_instance_assets_dir;
        std::string path = get_data_path(g_instance_assets_dir + "/resources/portal_effect_2x.pvr");
        FILE* f = fopen(path.c_str(), "rb");
        if (f) { fclose(f);
            g_portal_tex = pvr_load_texture(path.c_str());
            if (g_portal_tex) {
                std::cout << "[PORTAL-VANILLA] Loaded texture from " << path << std::endl;
            }
        }
        g_portal_tex_loaded = true;
    }
    
    if (!g_portal_tex) return;
    
    // Save GL state
    GLboolean blend_was_on;
    glGetBooleanv(GL_BLEND, &blend_was_on);
    GLint old_blend_src, old_blend_dst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &old_blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &old_blend_dst);
    
    // Set viewport to match the game's rendering area
    glViewport(0, 0, viewport_w, viewport_h);
    
    // Additive blending for the portal glow
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    glUseProgram(g_prog_portal);
    
    // Bind portal texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_portal_tex);
    glUniform1i(loc_portal_portal_tex, 0);
    
    // Pass the model-view matrix from the game's Draw call
    glUniformMatrix4fv(loc_portal_mv_matrix,
                       1, GL_FALSE, g_portal_vp_matrix);
    
    // Aspect ratio for perspective projection
    float aspect = (float)viewport_w / (float)viewport_h;
    glUniform1f(loc_portal_aspect, aspect);
    
    // Portal color and animation
    glUniform3f(loc_portal_color,
                g_portal_color_r, g_portal_color_g, g_portal_color_b);
    glUniform1f(loc_portal_time, vanilla_portal_time);
    glUniform1f(loc_portal_speed, g_portal_speed);
    glUniform1f(loc_portal_alpha, g_portal_intensity);
    
    draw_fsq();
    
    // Restore GL state
    if (!blend_was_on) glDisable(GL_BLEND);
    glBlendFunc(old_blend_src, old_blend_dst);
    glUseProgram(0);
}

unsigned int fbo_get_fbo() {
    return g_fbo;
}

// ---------------------------------------------------------------------------
//  F3 diagnostic for the authored-emitter bloom key  (REMOVABLE test surface)
// ---------------------------------------------------------------------------
//  Reports what the extract pass was actually given: the resolved biome, how
//  many authored emitters it contributed, and their colours. This exists because
//  the key cannot be validated by looking at a picture - a frame with no keyed
//  pixels looks the same whether the palette was empty (biome unresolved) or the
//  thresholds are wrong, and those need opposite fixes. Returns the emitter
//  count; writes a one-line description into `out`.
//
//  Delete this function and its call site in swordfare_gui.cpp's draw_debug to
//  remove the diagnostic; nothing else uses it.
extern "C" int swordigo_bloom_emitter_report(char* out, int cap) {
    if (!out || cap <= 0) return 0;
    out[0] = '\0';

    const auto& eb = TextureAwareMapper::Instance().GetCurrentBiome();
    if (!eb.valid || eb.entry == nullptr) {
        std::snprintf(out, size_t(cap), "no biome resolved (key finds nothing)");
        return 0;
    }
    const int bid = eb.entry->id;
    int n = swordigo::biome::EmitterCountForBiome(bid);
    if (n > 4) n = 4;

    int written = std::snprintf(out, size_t(cap), "%s x%d:",
                                eb.entry->name ? eb.entry->name : "?", n);
    for (int i = 0; i < n && written > 0 && written < cap; ++i) {
        written += std::snprintf(out + written, size_t(cap - written),
                                 " %.2f,%.2f,%.2f",
                                 swordigo::biome::kEmitterPalette[bid][i][0],
                                 swordigo::biome::kEmitterPalette[bid][i][1],
                                 swordigo::biome::kEmitterPalette[bid][i][2]);
    }
    return n;
}
