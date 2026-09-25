// ============================================================
//  gl_render_state.h
//  Live GL state extracted from the game's own GL calls via the
//  ARM64 JNI bridge.  Written by bridge hooks every frame;
//  consumed by fbo_scaler.cpp at composite / PostFX time.
//
//  This is the "handshake" layer that gives the FBO post-processing
//  pipeline accurate, per-frame knowledge of what the game engine is
//  actually doing — light positions, material colors, matrices, and
//  the current vertex/model color — so shaders can go beyond static
//  PostFX presets and react to the actual scene state.
// ============================================================
#pragma once

// ── Per-light state (GL_LIGHT0..GL_LIGHT7) ─────────────────────────────────
struct GuestLightState {
    float position[4];  // GL_POSITION (world space, w=0 directional, w=1 point)
    float ambient[4];   // GL_AMBIENT
    float diffuse[4];   // GL_DIFFUSE
    float specular[4];  // GL_SPECULAR
    bool  enabled;      // set by glEnable/glDisable(GL_LIGHT0+i)
};

// ── Material state (last glMaterialfv call) ────────────────────────────────
struct GuestMaterialState {
    float ambient[4];
    float diffuse[4];   // ← most important: drives model tint / boss color
    float specular[4];
    float emission[4];
    float shininess;
};

// ── Fog state (glFogf / glFogfv / glFogi + glEnable(GL_FOG)) ───────────────
// The guest renders with fixed-function GLES 1.1 fog. Any forward shader that
// replaces the fixed-function lighting evaluation must also reproduce fog,
// because GL_FOG does not apply to shader output — this is the state it needs.
struct GuestFogState {
    bool  enabled;      // glEnable/glDisable(GL_FOG)
    int   mode;         // GL_LINEAR (0x2601) / GL_EXP (0x0800) / GL_EXP2 (0x0801)
    float start;        // GL_FOG_START
    float end;          // GL_FOG_END
    float density;      // GL_FOG_DENSITY
    float color[4];     // GL_FOG_COLOR
};

// ── Fixed-function pipeline enables the forward pass cares about ───────────
// g_frame_lighting_enabled:
//   Caver::Scene::Draw and Caver::SkeletonInstance::Draw enable GL_LIGHTING
//   around the 3D passes; UI/2D draws leave it off. This is the gate that
//   keeps a forward lighting pass off the sprite/overlay geometry.
// g_frame_color_material_enabled:
//   GL_COLOR_MATERIAL — when set, the current/vertex color feeds
//   AMBIENT_AND_DIFFUSE instead of the glMaterialfv value (GL spec rule).
extern bool g_frame_lighting_enabled;
extern bool g_frame_color_material_enabled;

// GL client vertex arrays currently enabled (glEnableClientState).
extern bool g_frame_vertex_array_enabled;
extern bool g_frame_normal_array_enabled;
extern bool g_frame_color_array_enabled;
extern bool g_frame_texcoord_array_enabled;

// GL_TEXTURE_ENV_MODE for GL_TEXTURE0 (GL_MODULATE 0x2100, GL_REPLACE 0x1E01,
// GL_DECAL 0x2101, GL_ADD 0x0104, ...).
extern int g_frame_texenv_mode;

#ifdef __cplusplus
extern "C" {
#endif

// Eight shadow lights matching GL_LIGHT0..GL_LIGHT7
extern GuestLightState g_frame_lights[8];

// GLES1 also carries per-light attenuation (GL_CONSTANT_ATTENUATION,
// GL_LINEAR_ATTENUATION, GL_QUADRATIC_ATTENUATION). Caver::LightComponent::Enable
// sets GL_LINEAR_ATTENUATION=4616 from the component's range, so a faithful
// per-fragment replacement needs the triple.
extern float g_frame_light_attenuation[8][3];

// Fog state (see GuestFogState above)
extern GuestFogState g_frame_fog;

// GL_LIGHT_MODEL_AMBIENT — global ambient (from glLightModelfv)
extern float g_frame_light_model_ambient[4];

// Current material properties
extern GuestMaterialState g_frame_material;

// Current GL matrix shadows (column-major, same layout as OpenGL)
extern float g_current_modelview[16];
extern float g_current_projection[16];

// Current RGBA vertex/model color (normalised 0..1)
extern float g_current_color[4];

// Host-side cached hero position (synchronized from guest memory each frame)
#if defined(_MSC_VER)
// MSVC names namespace-scope array objects with a C (undecorated) symbol;
// the definition in jni_bridge_arm64.cpp emits the same plain name, so the
// declaration must use C linkage on Windows to match.
extern "C" float g_host_hero_pos[3];
#else
extern float g_host_hero_pos[3];
#endif

#ifdef __cplusplus
}
#endif
