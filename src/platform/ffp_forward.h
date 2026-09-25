// ============================================================================
//  ffp_forward.h — host-side GLES1 -> OpenGL 3.3 translation layer
//
//  WHAT THE GUEST ACTUALLY IS (verified from the 1.4.13 binary)
//  -----------------------------------------------------------
//  `Java_com_touchfoo_swordigo_Native_setupApplication` @ arm64_13 0x507DF4
//  constructs `RenderingContext(ctx, 0)` — RenderingAPI == 0, the legacy
//  fixed-function GLES 1.1 path. The engine's four GLES2 programs are inert
//  metadata: `RenderingProgramShader`'s ctor makes no GL calls,
//  `RenderingProgram::LinkWithShaders` @ 0x516DD8 is literally `return 1;`,
//  `RenderingProgram::AddVertexAttribute` @ 0x516C64 only pushes a GL enum into
//  a vector (the attribute-name string is never used), and a 2.7 MB runtime
//  trace shows glCreateShader / glCompileShader / glUseProgram /
//  glBindAttribLocation all at zero. There is no ES2 renderer in the binary to
//  "switch on": the guest is a fixed-function *scene describer*, nothing more.
//
//  So the guest is the scene describer and this module is the renderer. Every
//  GL call the engine makes already lands in the host bridge
//  (src/jni/jni_bridge_arm64.cpp), which means the fixed-function command
//  stream can be transcoded wholesale into a modern GL 3.3 pipeline — the same
//  architecture ANGLE and Zink implement, and the reason the guest's own
//  GLES2 path (or SRE13 guest-side patching) is not needed: there is nothing to
//  patch, the traffic is already ours.
//
//  THE CONTRACT
//  ------------
//   1. *Complete* state shadow. Every piece of GLES1 state that affects the
//      result of a draw is mirrored here: materials (front + back), lights
//      (position in eye space, ambient/diffuse/specular/attenuation), light
//      model, current colour, colour-material mode, texture env (mode + env
//      colour), alpha test, fog, per-array client pointers/enables, and the
//      matrix attributes (read from the driver, see 3).
//   2. *Every* draw is translated, not only the lit ones. The fixed-function
//      path's behaviour when GL_LIGHTING is off (primary colour = current or
//      vertex colour) is reproduced too. The old light-only pass claimed a
//      handful of draws and left the rest on a different pipeline, which is
//      what made the game look off: background, effects and overlays were
//      shaded by two different models in the same frame.
//   3. State is *seeded from the driver* at init (glGetMaterialfv, glGetLightfv,
//      glGetTexEnviv, glGetFloatv, glIsEnabled, ...) instead of being assumed.
//      Anything the guest never touches then matches vanilla by construction,
//      and the matrices are queried from the driver per draw rather than
//      re-derived from a shadow that host-side code can desynchronise.
//   4. Parity is the default. Level 1 (`TRANSLATE`) reproduces the GLES1
//      lighting equation **per vertex** (Gouraud), which is what the hardware
//      fixed-function path does, so captures of level 1 must match vanilla.
//      Per-fragment lighting, derived normal maps, parallax and GGX are opt-in
//      levels on top of that verified base.
//   5. Every draw is counted with the reason it was not translated, so a
//      regression is a log line with a number on it rather than a screenshot.
//
//  TOGGLES
//  -------
//    SWORDIGO_FFP_LIGHTING = 0  vanilla fixed-function (no translation)
//                            1  TRANSLATE, GLES1 parity (Gouraud lighting)  [default]
//                            2  + per-fragment lighting + derived normal maps
//                            3  + parallax occlusion mapping
//                            4  + Cook-Torrance GGX PBR
//    SWORDIGO_FFP_SCOPE=lit    only translate lit 3D geometry (escape hatch;
//                              default "all" translates every draw)
//    SWORDIGO_FFP_TONEMAP      none (default) | aces
//    SWORDIGO_FFP_EXPOSURE     exposure before tonemap (default 1.0)
//    SWORDIGO_FFP_BUMP         derived-bump strength (default 1.0)
//    SWORDIGO_FFP_POM_SCALE    parallax depth scale (default 0.02)
//    SWORDIGO_FFP_POM_STEPS    parallax steps (default 16, max 64)
//    SWORDIGO_FFP_HDR=1        emit unclamped linear (needs a float target)
//    SWORDIGO_FFP_TRACE=1      per-draw census        SWORDIGO_FFP_DEBUG=1
//    SWORDIGO_FFP_STATS=1      summary at exit
// ============================================================================
#pragma once

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include "platform/gl_inc.h"
#include <GL/glext.h>
#include <cstdint>
#include <cstddef>

namespace ffp {

enum Quality {
    OFF       = 0,  // vanilla fixed-function
    TRANSLATE = 1,  // full GLES1 -> GL 3.3 translation, parity-grade (default)
    PERFRAG   = 2,  // + per-fragment lighting + derived tangent-space normals
    POM       = 3,  // + parallax occlusion mapping
    PBR       = 4,  // + Cook-Torrance GGX on derived roughness/metallic
};

void init_from_env();
void set_quality(int quality);
int  quality();
bool available();
const char* status();

// ── State shadow: fixed-function enables ───────────────────────────────────
// The bridge forwards every glEnable/glDisable here. The module keeps its own
// copy so it never has to query the driver inside a draw, and so state the
// driver cannot express (e.g. GL_ALPHA_TEST under a core profile) still exists.
void note_capability(GLenum cap, bool enabled);

// ── State shadow: materials / current colour ───────────────────────────────
void note_materialfv(GLenum face, GLenum pname, const float* v);
void note_materialf(GLenum face, GLenum pname, float v);
void note_color_material_mode(GLenum face, GLenum mode);
void note_color4(float r, float g, float b, float a);

// ── State shadow: lights and light model ───────────────────────────────────
// GL_POSITION is transformed into eye space with the modelview that is current
// at the time of the call, exactly like the fixed-function pipeline.
void note_lightfv(int index, GLenum pname, const float* v);
void note_lightf(int index, GLenum pname, float v);
void note_light_enabled(int index, bool enabled);
void note_light_model_fv(GLenum pname, const float* v);
void note_light_model_f(GLenum pname, float v);

// ── State shadow: fog / texture env / alpha test / misc ────────────────────
void note_fogf(GLenum pname, float v);
void note_fogi(GLenum pname, int mode);
void note_fogfv(GLenum pname, const float* v);
void note_texenv(GLenum pname, int iparam, const float* fparam);
void note_alpha_func(GLenum func, float ref);
void note_shade_model(GLenum mode);
void note_point_size(float size);

// ── Matrix shadow ─────────────────────────────────────────────────────────
// The bridge forwards every fixed-function matrix call. The shadow is used for
// eye-space light positions and as a fallback matrix source; the authoritative
// matrices come from the driver (see g_matrix_source in the implementation).
void note_matrix_mode(int mode);
void note_load_identity();
void note_load_matrix(int mode, const float* m);
void note_mult_matrix(int mode, const float* m);
void note_push_matrix();
void note_pop_matrix();
void note_translate(float x, float y, float z);
void note_rotate(float angle_deg, float x, float y, float z);
void note_scale(float x, float y, float z);
void note_ortho(float l, float r, float b, float t, float near_z, float far_z);
void note_frustum(float l, float r, float b, float t, float near_z, float far_z);

// ── Guest vertex arrays ────────────────────────────────────────────────────
// Snapshot of the client-side arrays the guest has bound. `memory` is the host
// address of guest offset 0 and `arena_size` bounds every read we perform.
struct GuestArrays {
    const uint8_t* memory = nullptr;
    size_t  arena_size = 0;

    uint32_t vptr = 0; int vsize = 3; uint32_t vtype = 0x1406; int vstride = 0;  // GL_FLOAT
    uint32_t nptr = 0; int nsize = 3; uint32_t ntype = 0x1406; int nstride = 0;
    uint32_t cptr = 0; int csize = 4; uint32_t ctype = 0x1401; int cstride = 0;  // GL_UNSIGNED_BYTE
    uint32_t tptr = 0; int tsize = 2; uint32_t ttype = 0x1406; int tstride = 0;

    bool v_enabled = false, n_enabled = false, c_enabled = false, t_enabled = false;
};

void note_arrays(const GuestArrays& arrays);

// ── Per-draw entry point ───────────────────────────────────────────────────
// Returns true when the draw has been issued by this module; the caller must
// then NOT issue its own draw call, and must not touch GL state for that draw.
//
//   mode            GL primitive
//   texture         currently bound GL_TEXTURE_2D in unit 0 (0 = none)
//   first, count    glDrawArrays range (first = 0 for indexed draws)
//   indices_host    host pointer to the guest's index array, or null
//   indices_type    GL_UNSIGNED_BYTE / GL_UNSIGNED_SHORT / GL_UNSIGNED_INT
bool draw(GLenum mode, GLuint texture, int first, int count,
          const void* indices_host, GLenum indices_type);

// Census of the last frame's draws: translated vs handed back to vanilla, with
// the reason for each hand-back. Reset by reset_census(); dumped by print_stats().
void reset_census();

void dump_next_draw();
void print_stats();

} // namespace ffp
