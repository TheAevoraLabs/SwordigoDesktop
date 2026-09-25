// ============================================================================
//  ffp_forward.cpp — host-side GLES1 (fixed-function) -> OpenGL 3.3
//                    translation layer for Swordigo 1.4.13.
//
//  ARCHITECTURE (and where the code comes from)
//  -------------------------------------------------------------------------
//  The guest runs RenderingAPI == 0: it is a pure fixed-function GLES 1.1
//  *scene describer* (see ffp_forward.h for the binary evidence — no ES2
//  renderer exists in it at all). Every one of its GL calls already lands in
//  the host bridge, so the complete command stream can be transcoded into a
//  modern GL 3.3 pipeline. This module is that transcoder.
//
//  The shader itself is NOT hand-written: it is ANGLE's GLES1 emulation shader
//  (src/libANGLE/GLES1Shaders.inc, BSD-3, vendored verbatim in
//  platform/angle_gles1/), which is the implementation the GLES1 conformance
//  suite validates. ANGLE compiles a specialised program per state combination
//  and bakes the state in as #defines; here the same source is compiled once
//  per lighting-era and the state is fed in as uniforms instead, because this
//  layer has to be able to change state between any two draws.
//
//  What the transcoder has to own for that shader to be correct:
//
//    * the complete fixed-function state shadow (materials front+back, 8 lights
//      with eye-space positions and attenuation, light model, current colour,
//      colour material, texenv mode/env colour, alpha test, fog, shade model,
//      normalize/rescale, per-array client pointers), and
//    * a packed vertex stream built out of the guest's client arrays, in the
//      attribute layout ANGLE's vertex shader expects
//      (pos4 normal3 colour4 pointsize1 texcoord0_4).
//
//  Every draw is translated — lit or unlit — because the fixed-function path
//  treats those cases differently (unlit = primary colour is the vertex/current
//  colour) and a frame shaded by two different models is exactly what made the
//  game "look off" before.
//
//  Beyond the translation, additions (tonemap / bloom / the whole post chain)
//  belong after it, on the finished frame — that is where Godot's effect
//  sources plug in, not inside this program.
// ============================================================================
#define GL_GLEXT_PROTOTYPES
#include "ffp_forward.h"

#include "angle_gles1/GLES1Shaders.inc"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>

namespace {

// ============================================================================
//  ANGLE shader assembly
// ============================================================================
// Desktop GLSL 3.30 has no precision qualifiers and wants #version first, so
// the vendored text is passed through a very small normaliser: precision
// statements are dropped and precision keywords removed. Nothing else is
// rewritten — the maths stays byte-for-byte ANGLE's.
bool starts_with(const std::string& s, size_t i, const char* w) {
    size_t n = strlen(w);
    if (i + n > s.size()) return false;
    return s.compare(i, n, w) == 0;
}

bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string normalize_angle_glsl(const char* src) {
    std::string in(src ? src : ""), out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        char c = in[i];
        // Drop "#version ..." — the assembled source declares its own.
        if (c == '#' && i + 8 < in.size() && in.compare(i, 8, "#version") == 0) {
            while (i < in.size() && in[i] != '\n') i++;
            continue;
        }
        bool word_start = (i == 0) || !is_word_char(in[i - 1]);
        if (word_start) {
            if (in.compare(i, 9, "precision") == 0 && (i + 9 >= in.size() || !is_word_char(in[i + 9]))) {
                while (i < in.size() && in[i] != ';') i++;
                if (i < in.size()) i++;
                continue;
            }
            static const char* quals[] = {"mediump", "highp", "lowp"};
            bool stripped = false;
            for (const char* q : quals) {
                size_t n = strlen(q);
                if (in.compare(i, n, q) == 0 && (i + n >= in.size() || !is_word_char(in[i + n]))) {
                    i += n;
                    while (i < in.size() && (in[i] == ' ' || in[i] == '\t')) i++;
                    stripped = true;
                    break;
                }
            }
            if (stripped) continue;
        }
        out += c;
        i++;
    }
    return out;
}

// State the vendored shader expects to have been baked in with #define. Here it
// is uniform state, so one program covers every draw.
static const char* VS_STATE =
    "uniform bool enable_draw_texture;\n"
    "uniform bool enable_lighting;\n"
    "uniform bool enable_color_material;\n"
    "uniform bool enable_normalize;\n"
    "uniform bool enable_rescale_normal;\n"
    "uniform bool point_rasterization;\n"
    "uniform bool shade_model_flat;\n"
    "uniform bool light_model_two_sided;\n"
    "uniform bool light_enables[kMaxLights];\n";

// texture_format is always kRGBA: the host driver has already converted the
// guest's GL_ALPHA / GL_LUMINANCE uploads into the sampled value the
// fixed-function path would use, so ANGLE's per-format compensation would
// double-apply. That keeps this path byte-comparable with vanilla.
static const char* FS_STATE =
    "#define kMaxLights 8u\n"
    "uniform bool enable_clip_planes;\n"
    "uniform bool clip_plane_enables[kMaxClipPlanes];\n"
    "uniform bool enable_alpha_test;\n"
    "uniform bool enable_fog;\n"
    "uniform bool enable_lighting;\n"
    "uniform bool enable_draw_texture;\n"
    "uniform bool enable_texture_2d[4];\n"
    "uniform bool enable_texture_cube_map[4];\n"
    "uniform bool light_enables[kMaxLights];\n"
    "uniform bool shade_model_flat;\n"
    "uniform bool point_rasterization;\n"
    "uniform bool point_sprite_enabled;\n"
    "uniform bool point_sprite_coord_replace[4];\n"
    "uniform uint texture_format[4];\n"
    "uniform uint texture_env_mode[4];\n"
    "uniform uint combine_rgb[4];\n"
    "uniform uint combine_alpha[4];\n"
    "uniform uint src0_rgb[4];\n"
    "uniform uint src0_alpha[4];\n"
    "uniform uint src1_rgb[4];\n"
    "uniform uint src1_alpha[4];\n"
    "uniform uint src2_rgb[4];\n"
    "uniform uint src2_alpha[4];\n"
    "uniform uint op0_rgb[4];\n"
    "uniform uint op0_alpha[4];\n"
    "uniform uint op1_rgb[4];\n"
    "uniform uint op1_alpha[4];\n"
    "uniform uint op2_rgb[4];\n"
    "uniform uint op2_alpha[4];\n"
    "uniform uint alpha_func;\n"
    "uniform uint fog_mode;\n";

// Defined so the ANGLE ES1 conformance values are used; the guest only ever
// exercises unit 0 (GLClientActiveTexture is never called with another unit).
static const char* TEX_UNITS_DEFINE = "#define kTexUnits 1u\n";

std::string build_vertex_shader() {
    return std::string("#version 330\n") + TEX_UNITS_DEFINE +
           normalize_angle_glsl(kGLES1DrawVShaderHeader) + VS_STATE +
           normalize_angle_glsl(kGLES1DrawVShader);
}

std::string build_fragment_shader() {
    return std::string("#version 330\n") + TEX_UNITS_DEFINE +
           normalize_angle_glsl(kGLES1DrawFShaderHeader) +
           normalize_angle_glsl(kGLES1DrawFShaderUniformDefs) + FS_STATE +
           normalize_angle_glsl(kGLES1DrawFShaderFunctions) +
           normalize_angle_glsl(kGLES1DrawFShaderLogicOpFramebufferFetchDisabled) +
           normalize_angle_glsl(kGLES1DrawFShaderMultitexturing) +
           normalize_angle_glsl(kGLES1DrawFShaderOutputDef) +
           normalize_angle_glsl(kGLES1DrawFShaderMain);
}

// ============================================================================
//  Fixed-function state shadow
// ============================================================================
enum { F_AMBIENT = 0, F_DIFFUSE, F_SPECULAR, F_EMISSION, F_SHININESS };
enum { FACE_FRONT = 0, FACE_BACK = 1 };

// GL enum literals used below, so the module does not depend on the host
// headers defining the ES1-only ones.
enum {
    CAP_LIGHTING = 0x0B50, CAP_COLOR_MATERIAL = 0x0B57, CAP_TEXTURE_2D = 0x0DE1,
    CAP_ALPHA_TEST = 0x0BC0, CAP_FOG = 0x0B60, CAP_NORMALIZE = 0x0BA1,
    CAP_RESCALE_NORMAL = 0x803A, CAP_CLIP_PLANE0 = 0x3000,
    LIGHT0 = 0x4000,
    ENV_MODULATE = 0x2100, ENV_COMBINE = 0x8570,
    ALWAYS = 0x0207, SHADE_FLAT = 0x1D00, FOG_EXP = 0x0800,
    MODE_LINEAR = 0x2601,
};

struct LightState {
    bool  enabled  = false;
    float pos[4]   = {0.0f, 0.0f, 1.0f, 0.0f};   // eye space, w = 0 directional
    float ambient[4]  = {0.0f, 0.0f, 0.0f, 1.0f};
    float diffuse[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
    float specular[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float direction[3] = {0.0f, 0.0f, -1.0f};    // eye space (GL_SPOT_DIRECTION)
    float atten[3]    = {1.0f, 0.0f, 0.0f};
    float spot_cutoff = 180.0f;
    float spot_exponent = 0.0f;
};

struct GlState {
    bool  lit = false;
    bool  color_material = false;
    int   color_material_mode = 0x1602;      // GL_AMBIENT_AND_DIFFUSE
    bool  texture2d = true;                  // GLES1 default
    bool  alpha_test = false;
    int   alpha_func = ALWAYS;
    float alpha_ref = 0.0f;
    bool  fog = false;
    int   fog_mode = FOG_EXP;
    float fog_start = 0.0f, fog_end = 1.0f, fog_density = 1.0f;
    float fog_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool  normalize = false, rescale_normal = false;
    bool  clip_planes = false;
    bool  clip_plane_enable[6] = {};
    int   shade_model = 0x1D01;              // GL_SMOOTH
    float point_size = 1.0f;
    float mat[2][5][4] = {};
    float cur_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float light_model_ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    bool  two_sided = false;
    LightState light[8];

    // Texture environment for unit 0, including the whole GL_COMBINE state.
    // Swordigo's UI atlas draws use GL_COMBINE (a greyscale sprite tinted by
    // GL_TEXTURE_ENV_COLOR through INTERPOLATE / ADD), so treating COMBINE as
    // "MODULATE" is not a small approximation: it is what turned red hearts
    // magenta, deleted the tinted gear icon and flattened the effects.
    int   texenv_mode = ENV_MODULATE;
    float texenv_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int   combine_rgb = ENV_MODULATE, combine_alpha = ENV_MODULATE;
    int   src0_rgb = 0x1702, src1_rgb = 0x8578, src2_rgb = 0x8576;   // TEXTURE, PREVIOUS, CONSTANT
    int   src0_alpha = 0x1702, src1_alpha = 0x8578, src2_alpha = 0x8576;
    int   op0_rgb = 0x0300, op1_rgb = 0x0300, op2_rgb = 0x0300;     // SRC_COLOR
    int   op0_alpha = 0x0302, op1_alpha = 0x0302, op2_alpha = 0x0302;  // SRC_ALPHA
    float rgb_scale = 1.0f, alpha_scale = 1.0f;
};

GlState g_st;

// Matrix shadow: mirrors the guest's own matrix commands. Used to transform
// light positions into eye space at the instant glLightfv(GL_POSITION) is
// called (what the fixed-function pipeline does) and as a fallback matrix
// source. Templates for the draw itself are read from the driver.
const int MV_STACK = 32, PROJ_STACK = 8, TEX_STACK = 8;

typedef float Mat4[16];
void mat_identity(Mat4 m) { memset(m, 0, sizeof(Mat4)); m[0] = m[5] = m[10] = m[15] = 1.0f; }
void mat_copy(Mat4 dst, const float* src) { memcpy(dst, src, sizeof(Mat4)); }
void mat_mul_t(Mat4 out, const float* a, const float* b) {   // out = a * b
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    memcpy(out, t, sizeof(t));
}

struct MatrixShadow {
    int mode = 0x1700;                       // GL_MODELVIEW
    Mat4 modelview, projection, texture;
    Mat4 mv_stack[MV_STACK];  int mv_depth = 0;
    Mat4 pr_stack[PROJ_STACK]; int pr_depth = 0;
    Mat4 tx_stack[TEX_STACK];  int tx_depth = 0;

    Mat4* current() {
        if (mode == 0x1701) return &projection;
        if (mode == 0x1702) return &texture;
        return &modelview;
    }
    void push() {
        Mat4* m = current();
        Mat4* st; int* depth;
        if (mode == 0x1701) { st = pr_stack; depth = &pr_depth; }
        else if (mode == 0x1702) { st = tx_stack; depth = &tx_depth; }
        else { st = mv_stack; depth = &mv_depth; }
        int cap = (mode == 0x1701) ? PROJ_STACK : (mode == 0x1702 ? TEX_STACK : MV_STACK);
        if (*depth >= cap) return;
        memcpy(st[*depth], *m, sizeof(Mat4));
        (*depth)++;
    }
    void pop() {
        Mat4* m = current();
        Mat4* st; int* depth;
        if (mode == 0x1701) { st = pr_stack; depth = &pr_depth; }
        else if (mode == 0x1702) { st = tx_stack; depth = &tx_depth; }
        else { st = mv_stack; depth = &mv_depth; }
        if (*depth <= 0) return;
        (*depth)--;
        memcpy(*m, st[*depth], sizeof(Mat4));
    }
};

MatrixShadow g_mat;
ffp::GuestArrays g_arr;

// ============================================================================
//  Module state
// ============================================================================
int  g_quality = -1;
bool g_debug = false, g_stats = false, g_trace = false, g_lit_scope = false;
bool g_attempted = false;
int  g_matrix_source = -1;        // -1 unknown, 0 driver, 1 shadow
bool g_matrix_checked = false;
char g_status[240] = "translation: not initialised";

GLuint g_prog = 0, g_vao = 0, g_vbo = 0;
size_t g_vbo_bytes = 0;
std::vector<float> g_scratch;

struct Unis {
    // vertex
    GLint projection, modelview, modelview_invtr, texture_matrix;
    GLint point_size_min, point_size_max, point_distance_attenuation;
    GLint material_ambient, material_diffuse, material_specular, material_emissive;
    GLint material_specular_exponent, light_model_scene_ambient;
    GLint light_ambients, light_diffuses, light_speculars, light_positions, light_directions;
    GLint light_spotlight_exponents, light_spotlight_cutoff_angles;
    GLint light_attenuation_consts, light_attenuation_linears, light_attenuation_quadratics;
    GLint draw_texture_coords, draw_texture_dims, draw_texture_normalized_crop_rect;
    // fragment
    GLint tex_sampler0, tex_cube_sampler0;
    GLint texture_env_color, texture_env_rgb_scale, texture_env_alpha_scale, texture_env_lod_bias;
    GLint alpha_test_ref, fog_density, fog_start, fog_end, fog_color;
    GLint clip_planes, logic_op;
    // state
    GLint st_enable_draw_texture, st_enable_lighting, st_enable_color_material;
    GLint st_enable_normalize, st_enable_rescale_normal, st_point_rasterization, st_shade_model_flat;
    GLint st_light_enables;
    GLint st_enable_clip_planes, st_clip_plane_enables, st_enable_alpha_test, st_enable_fog;
    GLint st_enable_texture_2d, st_enable_texture_cube_map;
    GLint st_texture_format, st_texture_env_mode;
    GLint st_combine_rgb, st_combine_alpha;
    GLint st_src0_rgb, st_src0_alpha, st_src1_rgb, st_src1_alpha, st_src2_rgb, st_src2_alpha;
    GLint st_op0_rgb, st_op0_alpha, st_op1_rgb, st_op1_alpha, st_op2_rgb, st_op2_alpha;
    GLint st_alpha_func, st_fog_mode, st_point_sprite_enabled, st_point_sprite_coord_replace;
} U = {};

long g_draws_translated = 0, g_draws_vanilla = 0, g_verts = 0;
long g_skip_off = 0, g_skip_empty = 0, g_skip_noprogram = 0, g_skip_foreign = 0;
long g_skip_no_varray = 0, g_skip_scope = 0, g_skip_no_texcoord = 0, g_skip_prim = 0;
long g_skip_fetch = 0, g_skip_indices = 0, g_skip_big = 0;
long g_nonfloat_arrays = 0;
long g_untextured_draws = 0;
int  g_untextured_reports = 0;
int  g_trace_reports = 0;
int  g_big_reports = 0;
int  g_drv_reports = 0;
int  g_dump_next = 0;

// ── Texture probe ────────────────────────────────────────────────────────
//  "The 3D went white" has exactly two causes, and they need different
//  fixes: the shader is not sampling at all, or it is sampling and the
//  texel it gets is white.  Guessing between them is how you waste days.
//  Read the bound level back once per texture (bounded, debug only) and
//  report the texel the first vertex actually picks.
struct TexProbe {
    int w = 0, h = 0;
    bool valid = false;
    float white_frac = -1.0f;
    std::vector<unsigned char> px;
    bool texel(float u, float v, unsigned char out[4]) const {
        if (!valid || w <= 0 || h <= 0 || px.empty()) return false;
        int x = (int)floorf(u * (float)w) % w; if (x < 0) x += w;
        int y = (int)floorf(v * (float)h) % h; if (y < 0) y += h;
        const unsigned char* p = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        return true;
    }
};
std::map<GLuint, TexProbe> g_tex_probes;

const TexProbe* probe_texture(GLuint id) {
    if (!id) return nullptr;
    auto it = g_tex_probes.find(id);
    if (it == g_tex_probes.end()) {
        if (g_tex_probes.size() >= 4) return nullptr;   // bounded: debug only
        GLint prev = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
        glBindTexture(GL_TEXTURE_2D, id);
        TexProbe p;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &p.w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &p.h);
        if (p.w > 0 && p.h > 0 && (double)p.w * p.h <= 4.0 * 1024.0 * 1024.0) {
            p.px.resize((size_t)p.w * (size_t)p.h * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.px.data());
            size_t white = 0;
            for (size_t i = 0; i < p.px.size(); i += 4)
                if (p.px[i] > 240 && p.px[i + 1] > 240 && p.px[i + 2] > 240) white++;
            p.white_frac = p.px.empty() ? -1.0f : (float)white / (float)(p.px.size() / 4);
            p.valid = true;
        }
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
        it = g_tex_probes.emplace(id, p).first;
    }
    return &it->second;
}

GLint g_saved_prog = 0, g_saved_vao = 0, g_saved_vbo = 0;

// ============================================================================
//  Vertex fetch: guest client arrays -> ANGLE's attribute layout
//    [0..3] pos(x,y,z,1)  [4..6] normal  [7..10] colour  [11] pointsize
//    [12..15] texcoord0(s,t,0,1)
// ============================================================================
const int FPV = 16;

inline bool in_arena(uint64_t off, uint64_t bytes) {
    return g_arr.memory && off <= g_arr.arena_size && bytes <= g_arr.arena_size - off;
}

int type_size(uint32_t t) {
    switch (t) {
        case 0x1400: return 1;  // GL_BYTE
        case 0x1401: return 1;  // GL_UNSIGNED_BYTE
        case 0x1402: return 2;  // GL_SHORT
        case 0x1403: return 2;  // GL_UNSIGNED_SHORT
        case 0x1404: return 4;  // GL_INT
        case 0x1406: return 4;  // GL_FLOAT
        case 0x140C: return 4;  // GL_FIXED
        default: return 0;
    }
}

// GLES1's vertex-array conversion (ES 1.1 spec, vertex array tables).
float read_comp(const uint8_t* p, uint32_t type) {
    switch (type) {
        case 0x1406: return *(const float*)p;
        case 0x1400: return (float)(*(const int8_t*)p) / 127.0f;
        case 0x1401: return (float)p[0] / 255.0f;
        case 0x1402: return (float)(*(const int16_t*)p) / 32767.0f;
        case 0x1403: return (float)(*(const uint16_t*)p) / 65535.0f;
        case 0x140C: return (float)(*(const int32_t*)p) / 65536.0f;
        case 0x1404: return (float)(*(const int32_t*)p);
        default: return 0.0f;
    }
}

bool fetch_vertex(int idx, float* out) {
    if (!g_arr.memory || !g_arr.v_enabled || !g_arr.vptr) return false;

    int vts = type_size(g_arr.vtype);
    if (!vts || g_arr.vsize < 1) return false;
    int vcomps = g_arr.vsize;
    int vstride = g_arr.vstride ? g_arr.vstride : vcomps * vts;
    if (vstride < vcomps * vts) return false;

    uint64_t off = (uint64_t)g_arr.vptr + (uint64_t)idx * (uint64_t)vstride;
    if (!in_arena(off, (uint64_t)vcomps * vts)) return false;
    const uint8_t* p = g_arr.memory + off;
    out[0] = read_comp(p, g_arr.vtype);
    out[1] = vcomps > 1 ? read_comp(p + vts, g_arr.vtype) : 0.0f;
    out[2] = vcomps > 2 ? read_comp(p + 2 * vts, g_arr.vtype) : 0.0f;
    out[3] = 1.0f;

    // Normal array off → the fixed pipeline uses the current normal, which the
    // guest never changes (there is no glNormal3f handler: the call is not in
    // the engine's GL set), so the GL default (0,0,1) is what vanilla uses.
    out[4] = 0.0f; out[5] = 0.0f; out[6] = 1.0f;
    if (g_arr.n_enabled && g_arr.nptr) {
        int nts = type_size(g_arr.ntype);
        if (nts) {
            int ns = g_arr.nstride ? g_arr.nstride : 3 * nts;
            uint64_t no = (uint64_t)g_arr.nptr + (uint64_t)idx * (uint64_t)ns;
            if (in_arena(no, (uint64_t)3 * nts)) {
                const uint8_t* n = g_arr.memory + no;
                out[4] = read_comp(n, g_arr.ntype);
                out[5] = read_comp(n + nts, g_arr.ntype);
                out[6] = read_comp(n + 2 * nts, g_arr.ntype);
            }
        }
    }

    // Colour array off → glColor (last glColor4f/glColor4ub), which is exactly
    // what the fixed pipeline feeds the shader as the primary colour.
    if (g_arr.c_enabled && g_arr.cptr) {
        int cts = type_size(g_arr.ctype);
        if (cts) {
            int ccomps = g_arr.csize > 0 ? g_arr.csize : 4;
            int cs = g_arr.cstride ? g_arr.cstride : ccomps * cts;
            uint64_t co = (uint64_t)g_arr.cptr + (uint64_t)idx * (uint64_t)cs;
            if (in_arena(co, (uint64_t)4 * cts)) {
                const uint8_t* c = g_arr.memory + co;
                out[7]  = read_comp(c, g_arr.ctype);
                out[8]  = ccomps > 1 ? read_comp(c + cts, g_arr.ctype) : 1.0f;
                out[9]  = ccomps > 2 ? read_comp(c + 2 * cts, g_arr.ctype) : 1.0f;
                out[10] = ccomps > 3 ? read_comp(c + 3 * cts, g_arr.ctype) : 1.0f;
            }
        }
    } else {
        out[7] = g_st.cur_color[0]; out[8] = g_st.cur_color[1];
        out[9] = g_st.cur_color[2]; out[10] = g_st.cur_color[3];
    }

    // ANGLE feeds the current glPointSize in as a per-vertex attribute.
    out[11] = g_st.point_size;

    out[12] = 0.0f; out[13] = 0.0f; out[14] = 0.0f; out[15] = 1.0f;
    if (g_arr.t_enabled && g_arr.tptr) {
        int tts = type_size(g_arr.ttype);
        if (tts) {
            int tcomps = g_arr.tsize > 0 ? g_arr.tsize : 2;
            int ts = g_arr.tstride ? g_arr.tstride : tcomps * tts;
            uint64_t to = (uint64_t)g_arr.tptr + (uint64_t)idx * (uint64_t)ts;
            if (in_arena(to, (uint64_t)tcomps * tts)) {
                const uint8_t* t = g_arr.memory + to;
                out[12] = read_comp(t, g_arr.ttype);
                out[13] = tcomps > 1 ? read_comp(t + tts, g_arr.ttype) : 0.0f;
            }
        }
    }
    return true;
}

// ============================================================================
//  Program
// ============================================================================
bool compile_shader(GLenum type, const std::string& src, GLuint* out) {
    GLuint sh = glCreateShader(type);
    if (!sh) return false;
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? len : 1, '\0');
        glGetShaderInfoLog(sh, len, nullptr, &log[0]);
        std::cerr << "[FFP] " << (type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                  << " shader compile failed:\n" << log << std::endl;
        glDeleteShader(sh);
        return false;
    }
    *out = sh;
    return true;
}

#define FFP_GET(loc, name) (loc) = glGetUniformLocation(g_prog, (name))

void query_uniforms() {
    glUseProgram(g_prog);
    FFP_GET(U.projection, "projection");
    FFP_GET(U.modelview, "modelview");
    FFP_GET(U.modelview_invtr, "modelview_invtr");
    FFP_GET(U.texture_matrix, "texture_matrix");
    FFP_GET(U.point_size_min, "point_size_min");
    FFP_GET(U.point_size_max, "point_size_max");
    FFP_GET(U.point_distance_attenuation, "point_distance_attenuation");
    FFP_GET(U.material_ambient, "material_ambient");
    FFP_GET(U.material_diffuse, "material_diffuse");
    FFP_GET(U.material_specular, "material_specular");
    FFP_GET(U.material_emissive, "material_emissive");
    FFP_GET(U.material_specular_exponent, "material_specular_exponent");
    FFP_GET(U.light_model_scene_ambient, "light_model_scene_ambient");
    FFP_GET(U.light_ambients, "light_ambients");
    FFP_GET(U.light_diffuses, "light_diffuses");
    FFP_GET(U.light_speculars, "light_speculars");
    FFP_GET(U.light_positions, "light_positions");
    FFP_GET(U.light_directions, "light_directions");
    FFP_GET(U.light_spotlight_exponents, "light_spotlight_exponents");
    FFP_GET(U.light_spotlight_cutoff_angles, "light_spotlight_cutoff_angles");
    FFP_GET(U.light_attenuation_consts, "light_attenuation_consts");
    FFP_GET(U.light_attenuation_linears, "light_attenuation_linears");
    FFP_GET(U.light_attenuation_quadratics, "light_attenuation_quadratics");
    FFP_GET(U.draw_texture_coords, "draw_texture_coords");
    FFP_GET(U.draw_texture_dims, "draw_texture_dims");
    FFP_GET(U.draw_texture_normalized_crop_rect, "draw_texture_normalized_crop_rect");
    FFP_GET(U.tex_sampler0, "tex_sampler0");
    FFP_GET(U.tex_cube_sampler0, "tex_cube_sampler0");
    FFP_GET(U.texture_env_color, "texture_env_color");
    FFP_GET(U.texture_env_rgb_scale, "texture_env_rgb_scale");
    FFP_GET(U.texture_env_alpha_scale, "texture_env_alpha_scale");
    FFP_GET(U.texture_env_lod_bias, "texture_env_lod_bias");
    FFP_GET(U.alpha_test_ref, "alpha_test_ref");
    FFP_GET(U.fog_density, "fog_density");
    FFP_GET(U.fog_start, "fog_start");
    FFP_GET(U.fog_end, "fog_end");
    FFP_GET(U.fog_color, "fog_color");
    FFP_GET(U.clip_planes, "clip_planes");
    FFP_GET(U.logic_op, "logic_op");
    FFP_GET(U.st_enable_draw_texture, "enable_draw_texture");
    FFP_GET(U.st_enable_lighting, "enable_lighting");
    FFP_GET(U.st_enable_color_material, "enable_color_material");
    FFP_GET(U.st_enable_normalize, "enable_normalize");
    FFP_GET(U.st_enable_rescale_normal, "enable_rescale_normal");
    FFP_GET(U.st_point_rasterization, "point_rasterization");
    FFP_GET(U.st_shade_model_flat, "shade_model_flat");
    FFP_GET(U.st_light_enables, "light_enables");
    FFP_GET(U.st_enable_clip_planes, "enable_clip_planes");
    FFP_GET(U.st_clip_plane_enables, "clip_plane_enables");
    FFP_GET(U.st_enable_alpha_test, "enable_alpha_test");
    FFP_GET(U.st_enable_fog, "enable_fog");
    FFP_GET(U.st_enable_texture_2d, "enable_texture_2d");
    FFP_GET(U.st_enable_texture_cube_map, "enable_texture_cube_map");
    FFP_GET(U.st_texture_format, "texture_format");
    FFP_GET(U.st_texture_env_mode, "texture_env_mode");
    FFP_GET(U.st_combine_rgb, "combine_rgb");
    FFP_GET(U.st_combine_alpha, "combine_alpha");
    FFP_GET(U.st_src0_rgb, "src0_rgb");
    FFP_GET(U.st_src0_alpha, "src0_alpha");
    FFP_GET(U.st_src1_rgb, "src1_rgb");
    FFP_GET(U.st_src1_alpha, "src1_alpha");
    FFP_GET(U.st_src2_rgb, "src2_rgb");
    FFP_GET(U.st_src2_alpha, "src2_alpha");
    FFP_GET(U.st_op0_rgb, "op0_rgb");
    FFP_GET(U.st_op0_alpha, "op0_alpha");
    FFP_GET(U.st_op1_rgb, "op1_rgb");
    FFP_GET(U.st_op1_alpha, "op1_alpha");
    FFP_GET(U.st_op2_rgb, "op2_rgb");
    FFP_GET(U.st_op2_alpha, "op2_alpha");
    FFP_GET(U.st_alpha_func, "alpha_func");
    FFP_GET(U.st_fog_mode, "fog_mode");
    FFP_GET(U.st_point_sprite_enabled, "point_sprite_enabled");
    FFP_GET(U.st_point_sprite_coord_replace, "point_sprite_coord_replace");
    glUseProgram(0);
}

bool ensure_program() {
    if (g_attempted) return g_prog != 0;
    g_attempted = true;

    GLuint vsh = 0, fsh = 0;
    std::string vs = build_vertex_shader(), fs = build_fragment_shader();
    if (!compile_shader(GL_VERTEX_SHADER, vs, &vsh)) {
        snprintf(g_status, sizeof(g_status), "translation: vertex shader failed to compile");
        std::cout << "[FFP] " << g_status << " — vanilla rendering continues" << std::endl;
        return false;
    }
    if (!compile_shader(GL_FRAGMENT_SHADER, fs, &fsh)) {
        glDeleteShader(vsh);
        snprintf(g_status, sizeof(g_status), "translation: fragment shader failed to compile");
        std::cout << "[FFP] " << g_status << " — vanilla rendering continues" << std::endl;
        return false;
    }
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vsh);
    glAttachShader(g_prog, fsh);
    // ANGLE's shader names its inputs; ours are the bridge's packed attributes.
    glBindAttribLocation(g_prog, 0, "pos");
    glBindAttribLocation(g_prog, 1, "normal");
    glBindAttribLocation(g_prog, 2, "color");
    glBindAttribLocation(g_prog, 3, "pointsize");
    glBindAttribLocation(g_prog, 4, "texcoord0");
    glLinkProgram(g_prog);
    GLint linked = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
    glDeleteShader(vsh);
    glDeleteShader(fsh);
    if (!linked) {
        GLint len = 0;
        glGetProgramiv(g_prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(len > 1 ? len : 1, '\0');
        glGetProgramInfoLog(g_prog, len, nullptr, &log[0]);
        std::cerr << "[FFP] program link failed:\n" << log << std::endl;
        glDeleteProgram(g_prog);
        g_prog = 0;
        snprintf(g_status, sizeof(g_status), "translation: program link failed");
        std::cout << "[FFP] " << g_status << " — vanilla rendering continues" << std::endl;
        return false;
    }

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, FPV * 4, (void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, FPV * 4, (void*)(4 * 4));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, FPV * 4, (void*)(7 * 4));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, FPV * 4, (void*)(11 * 4));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, FPV * 4, (void*)(12 * 4));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // The vertex shader writes gl_PointSize (ANGLE's does), which only takes
    // effect with GL_PROGRAM_POINT_SIZE.
    glEnable(0x8642 /*GL_PROGRAM_POINT_SIZE*/);

    query_uniforms();
    snprintf(g_status, sizeof(g_status),
             "translation: ANGLE GLES1 emulation active (quality=%d)", g_quality);
    std::cout << "[FFP] " << g_status << std::endl;
    return true;
}

void check_matrix_shadow() {
    if (g_matrix_checked) return;
    g_matrix_checked = true;
    GLfloat drv_mv[16], drv_pr[16];
    glGetFloatv(0x0BA6, drv_mv);
    glGetFloatv(0x0BA7, drv_pr);
    auto close = [](const float* a, const float* b) {
        for (int i = 0; i < 16; i++) {
            float d = fabsf(a[i] - b[i]);
            float s = std::max(1.0f, fabsf(b[i]));
            if (d / s > 1e-3f) return false;
        }
        return true;
    };
    g_matrix_source = 0;   // the driver is authoritative either way
    if (!close(g_mat.modelview, drv_mv) || !close(g_mat.projection, drv_pr)) {
        std::cout << "[FFP] matrix shadow differs from the driver — using the driver's "
                     "matrices pending investigation" << std::endl;
        if (g_debug) {
            std::cout << "[FFP]   ours mv:";  for (int i = 0; i < 16; i++) std::cout << " " << g_mat.modelview[i];
            std::cout << "\n[FFP]   drv  mv:"; for (int i = 0; i < 16; i++) std::cout << " " << drv_mv[i];
            std::cout << std::endl;
        }
    }
}

void get_matrices(float mv[16], float pr[16], float tx[16]) {
    if (g_matrix_source == 0) {
        glGetFloatv(0x0BA6, mv);
        glGetFloatv(0x0BA7, pr);
        glGetFloatv(0x0BA8, tx);
        float s = 0.0f;
        for (int i = 0; i < 16; i++) s += fabsf(mv[i]) + fabsf(pr[i]);
        if (s > 1e-6f) return;
    }
    memcpy(mv, g_mat.modelview, sizeof(Mat4));
    memcpy(pr, g_mat.projection, sizeof(Mat4));
    memcpy(tx, g_mat.texture, sizeof(Mat4));
}

bool is_ortho_projection(const float* pr) {
    return fabsf(pr[15] - 1.0f) < 1e-5f && fabsf(pr[11]) < 1e-5f && fabsf(pr[3]) < 1e-5f;
}

void mat_normal_invtr(float out[16], const float* m) {
    // modelview_invtr = inverse-transpose of the 3x3, in a mat4.
    float a00 = m[0], a01 = m[1], a02 = m[2];
    float a10 = m[4], a11 = m[5], a12 = m[6];
    float a20 = m[8], a21 = m[9], a22 = m[10];
    float b01 =  a22 * a11 - a12 * a21;
    float b11 = -a22 * a10 + a12 * a20;
    float b21 =  a21 * a10 - a11 * a20;
    float det = a00 * b01 + a01 * b11 + a02 * b21;
    mat_identity(out);
    if (fabsf(det) < 1e-12f) return;
    float inv = 1.0f / det;
    float i00 = b01 * inv, i01 = (-a22 * a01 + a02 * a21) * inv, i02 = (a12 * a01 - a02 * a11) * inv;
    float i10 = b11 * inv, i11 = (a22 * a00 - a02 * a20) * inv, i12 = (-a12 * a00 + a02 * a10) * inv;
    float i20 = b21 * inv, i21 = (-a21 * a00 + a01 * a20) * inv, i22 = (a11 * a00 - a01 * a10) * inv;
    out[0] = i00; out[1] = i01; out[2] = i02;
    out[4] = i10; out[5] = i11; out[6] = i12;
    out[8] = i20; out[9] = i21; out[10] = i22;
}

bool supported_primitive(GLenum mode) {
    switch (mode) {
        case GL_POINTS: case GL_LINES: case GL_LINE_LOOP: case GL_LINE_STRIP:
        case GL_TRIANGLES: case GL_TRIANGLE_STRIP: case GL_TRIANGLE_FAN:
        case 0x0007:  // GL_QUADS / GL_QUAD_STRIP tolerance
        case 0x0009:  // GL_POLYGON
            return true;
        default: return false;
    }
}

// ── Uniform setters (all no-ops when the uniform was optimised out) ─────────
inline void u1i(GLint l, int v) { if (l >= 0) glUniform1i(l, v); }
inline void u1u(GLint l, unsigned v) { if (l >= 0) glUniform1ui(l, v); }
inline void u1f(GLint l, float v) { if (l >= 0) glUniform1f(l, v); }
inline void u4f(GLint l, const float* v) { if (l >= 0) glUniform4fv(l, 1, v); }
inline void u3f(GLint l, const float* v) { if (l >= 0) glUniform3fv(l, 1, v); }
inline void u1iv(GLint l, int n, const int* v) { if (l >= 0) glUniform1iv(l, n, v); }
inline void u1uiv(GLint l, int n, const unsigned* v) { if (l >= 0) glUniform1uiv(l, n, v); }
inline void u4fv(GLint l, int n, const float* v) { if (l >= 0) glUniform4fv(l, n, v); }
inline void u3fv(GLint l, int n, const float* v) { if (l >= 0) glUniform3fv(l, n, v); }
inline void u1fv(GLint l, int n, const float* v) { if (l >= 0) glUniform1fv(l, n, v); }
inline void um4(GLint l, const float* m) { if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, m); }

} // namespace

// ============================================================================
//  Public API
// ============================================================================
namespace ffp {

void init_from_env() {
    if (g_quality >= 0) return;

    g_quality = TRANSLATE;
    if (const char* q = getenv("SWORDIGO_FFP_LIGHTING")) {
        if (q[0] == '\0' || strcmp(q, "0") == 0 || strcmp(q, "off") == 0 || strcmp(q, "OFF") == 0)
            g_quality = OFF;
        else g_quality = atoi(q);
    }
    if (g_quality < OFF) g_quality = OFF;
    if (g_quality > TRANSLATE) g_quality = TRANSLATE;   // 2..4 reserved

    g_debug = (getenv("SWORDIGO_FFP_DEBUG") && strcmp(getenv("SWORDIGO_FFP_DEBUG"), "0") != 0);
    g_stats = (getenv("SWORDIGO_FFP_STATS") && strcmp(getenv("SWORDIGO_FFP_STATS"), "0") != 0);
    g_trace = (getenv("SWORDIGO_FFP_TRACE") && strcmp(getenv("SWORDIGO_FFP_TRACE"), "0") != 0);
    g_lit_scope = (getenv("SWORDIGO_FFP_SCOPE") && strcmp(getenv("SWORDIGO_FFP_SCOPE"), "lit") == 0);

    mat_identity(g_mat.modelview);
    mat_identity(g_mat.projection);
    mat_identity(g_mat.texture);
    for (int f = 0; f < 2; f++)
        for (int k = 0; k < 5; k++)
            for (int c = 0; c < 4; c++)
                g_st.mat[f][k][c] = (k == F_EMISSION) ? 0.0f : 1.0f;

    // ── Seed the shadow from the driver ───────────────────────────────────
    // GLES1 and desktop GL share the same initial values for everything here,
    // but reading the live context means anything the guest never touches is
    // identical to vanilla by construction instead of by assumption.
    if (glGetString(GL_VERSION)) {
        for (int f = 0; f < 2; f++) {
            GLenum face = (f == FACE_FRONT) ? GL_FRONT : GL_BACK;
            glGetMaterialfv(face, GL_AMBIENT,   g_st.mat[f][F_AMBIENT]);
            glGetMaterialfv(face, GL_DIFFUSE,   g_st.mat[f][F_DIFFUSE]);
            glGetMaterialfv(face, GL_SPECULAR,  g_st.mat[f][F_SPECULAR]);
            glGetMaterialfv(face, GL_EMISSION,  g_st.mat[f][F_EMISSION]);
            glGetMaterialfv(face, GL_SHININESS, g_st.mat[f][F_SHININESS]);
        }
        for (int i = 0; i < 8; i++) {
            GLenum L = LIGHT0 + i;
            glGetLightfv(L, GL_POSITION,             g_st.light[i].pos);
            glGetLightfv(L, GL_AMBIENT,              g_st.light[i].ambient);
            glGetLightfv(L, GL_DIFFUSE,              g_st.light[i].diffuse);
            glGetLightfv(L, GL_SPECULAR,             g_st.light[i].specular);
            glGetLightfv(L, GL_SPOT_DIRECTION,       g_st.light[i].direction);
            GLfloat v = 1.0f;
            glGetLightfv(L, GL_CONSTANT_ATTENUATION,  &v); g_st.light[i].atten[0] = v;
            v = 0.0f;
            glGetLightfv(L, GL_LINEAR_ATTENUATION,    &v); g_st.light[i].atten[1] = v;
            glGetLightfv(L, GL_QUADRATIC_ATTENUATION, &v); g_st.light[i].atten[2] = v;
            v = 180.0f;
            glGetLightfv(L, GL_SPOT_CUTOFF,           &v); g_st.light[i].spot_cutoff = v;
            v = 0.0f;
            glGetLightfv(L, GL_SPOT_EXPONENT,         &v); g_st.light[i].spot_exponent = v;
            g_st.light[i].enabled = glIsEnabled(L) != 0;
        }
        glGetFloatv(GL_LIGHT_MODEL_AMBIENT, g_st.light_model_ambient);
        GLfloat f1 = 0.0f;
        glGetFloatv(GL_LIGHT_MODEL_TWO_SIDE, &f1); g_st.two_sided = f1 != 0.0f;
        glGetFloatv(GL_CURRENT_COLOR, g_st.cur_color);
        GLfloat fv[4] = {0, 0, 0, 1};
        glGetFloatv(GL_FOG_COLOR, fv);   memcpy(g_st.fog_color, fv, sizeof(fv));
        glGetFloatv(GL_FOG_START,   &g_st.fog_start);
        glGetFloatv(GL_FOG_END,     &g_st.fog_end);
        glGetFloatv(GL_FOG_DENSITY, &g_st.fog_density);
        glGetFloatv(GL_POINT_SIZE,  &g_st.point_size);
        glGetFloatv(GL_ALPHA_TEST_REF, &g_st.alpha_ref);
        GLint iv = 0;
        glGetIntegerv(GL_FOG_MODE, &iv);         g_st.fog_mode = iv;
        glGetIntegerv(GL_SHADE_MODEL, &iv);      g_st.shade_model = iv;
        glGetIntegerv(GL_ALPHA_TEST_FUNC, &iv);  g_st.alpha_func = iv;
        glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &iv); g_st.texenv_mode = iv;
        glGetTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, fv);
        memcpy(g_st.texenv_color, fv, sizeof(fv));
        // Seed the whole combiner from the live context as well: the guest's
        // glTexEnvi calls are mirrored below, so whatever it never touches then
        // matches the driver exactly instead of matching a guess.
        {
            struct { unsigned pname; int* dst; } ints[] = {
                {0x8571, &g_st.combine_rgb}, {0x8572, &g_st.combine_alpha},
                {0x8580, &g_st.src0_rgb},    {0x8581, &g_st.src1_rgb},  {0x8582, &g_st.src2_rgb},
                {0x8588, &g_st.src0_alpha},  {0x8589, &g_st.src1_alpha},{0x858A, &g_st.src2_alpha},
                {0x8590, &g_st.op0_rgb},     {0x8591, &g_st.op1_rgb},   {0x8592, &g_st.op2_rgb},
                {0x8598, &g_st.op0_alpha},   {0x8599, &g_st.op1_alpha}, {0x859A, &g_st.op2_alpha},
            };
            for (auto& e : ints) {
                GLint v = 0;
                glGetTexEnviv(GL_TEXTURE_ENV, (GLenum)e.pname, &v);
                *e.dst = (int)v;
            }
            GLfloat s = 1.0f;
            glGetTexEnvfv(GL_TEXTURE_ENV, 0x8573, &s); g_st.rgb_scale = s;
            s = 1.0f;
            glGetTexEnvfv(GL_TEXTURE_ENV, 0x0D1C, &s); g_st.alpha_scale = s;
        }
        g_st.lit            = glIsEnabled(GL_LIGHTING) != 0;
        g_st.color_material = glIsEnabled(GL_COLOR_MATERIAL) != 0;
        g_st.texture2d      = glIsEnabled(GL_TEXTURE_2D) != 0;
        g_st.alpha_test     = glIsEnabled(GL_ALPHA_TEST) != 0;
        g_st.fog            = glIsEnabled(GL_FOG) != 0;
        g_st.normalize      = glIsEnabled(GL_NORMALIZE) != 0;
        g_st.rescale_normal = glIsEnabled(GL_RESCALE_NORMAL) != 0;
        glGetFloatv(0x0BA6, g_mat.modelview);
        glGetFloatv(0x0BA7, g_mat.projection);
        glGetFloatv(0x0BA8, g_mat.texture);
    }

    snprintf(g_status, sizeof(g_status),
             "translation: quality=%d (%s)", g_quality,
             g_quality == OFF ? "vanilla fixed-function"
                              : "ANGLE GLES1 emulation on GL 3.3");
    std::cout << "[FFP] " << g_status << std::endl;
    if (g_quality != OFF) {
        std::cout << "[FFP] every guest draw is transcoded (state shadow + ANGLE GLES1 "
                     "shader); SWORDIGO_FFP_LIGHTING=0 restores vanilla" << std::endl;
    }
}

void set_quality(int q) { g_quality = (q <= OFF) ? OFF : TRANSLATE; }
int  quality() { if (g_quality < 0) init_from_env(); return g_quality; }
bool available() { return quality() != OFF && ensure_program(); }
const char* status() { return g_status; }

void note_capability(GLenum cap, bool en) {
    switch (cap) {
        case CAP_LIGHTING:       g_st.lit = en; break;
        case CAP_COLOR_MATERIAL: g_st.color_material = en; break;
        case CAP_TEXTURE_2D:     g_st.texture2d = en; break;
        case CAP_ALPHA_TEST:     g_st.alpha_test = en; break;
        case CAP_FOG:            g_st.fog = en; break;
        case CAP_NORMALIZE:      g_st.normalize = en; break;
        case CAP_RESCALE_NORMAL: g_st.rescale_normal = en; break;
        default:
            if (cap >= LIGHT0 && cap <= LIGHT0 + 7) g_st.light[cap - LIGHT0].enabled = en;
            else if (cap >= CAP_CLIP_PLANE0 && cap <= CAP_CLIP_PLANE0 + 5) {
                g_st.clip_plane_enable[cap - CAP_CLIP_PLANE0] = en;
                g_st.clip_planes = true;
            }
            break;
    }
}

void note_materialfv(GLenum face, GLenum pname, const float* v) {
    if (!v) return;
    int f0 = (face == GL_BACK) ? FACE_BACK : FACE_FRONT;
    int f1 = (face == GL_FRONT_AND_BACK) ? FACE_BACK : f0;
    for (int f = f0; f <= f1; f++) {
        switch (pname) {
            case GL_AMBIENT:  memcpy(g_st.mat[f][F_AMBIENT], v, 16); break;
            case GL_DIFFUSE:  memcpy(g_st.mat[f][F_DIFFUSE], v, 16); break;
            case GL_SPECULAR: memcpy(g_st.mat[f][F_SPECULAR], v, 16); break;
            case GL_EMISSION: memcpy(g_st.mat[f][F_EMISSION], v, 16); break;
            case GL_AMBIENT_AND_DIFFUSE:
                memcpy(g_st.mat[f][F_AMBIENT], v, 16);
                memcpy(g_st.mat[f][F_DIFFUSE], v, 16);
                break;
            default: break;
        }
    }
}

void note_materialf(GLenum face, GLenum pname, float v) {
    if (pname != GL_SHININESS) return;
    int f0 = (face == GL_BACK) ? FACE_BACK : FACE_FRONT;
    int f1 = (face == GL_FRONT_AND_BACK) ? FACE_BACK : f0;
    for (int f = f0; f <= f1; f++) g_st.mat[f][F_SHININESS][0] = v;
}

void note_color_material_mode(GLenum face, GLenum mode) { (void)face; g_st.color_material_mode = (int)mode; }

void note_color4(float r, float g, float b, float a) {
    g_st.cur_color[0] = r; g_st.cur_color[1] = g; g_st.cur_color[2] = b; g_st.cur_color[3] = a;
}

void note_lightfv(int index, GLenum pname, const float* v) {
    if (index < 0 || index > 7 || !v) return;
    LightState& L = g_st.light[index];
    const float* m = g_mat.modelview;
    switch (pname) {
        case GL_POSITION: {
            float x = v[0], y = v[1], z = v[2], w = v[3];
            float ox = m[0] * x + m[4] * y + m[8]  * z;
            float oy = m[1] * x + m[5] * y + m[9]  * z;
            float oz = m[2] * x + m[6] * y + m[10] * z;
            if (w != 0.0f) { ox += m[12]; oy += m[13]; oz += m[14]; }
            L.pos[0] = ox; L.pos[1] = oy; L.pos[2] = oz; L.pos[3] = w;
            break;
        }
        case GL_AMBIENT:  memcpy(L.ambient, v, 16); break;
        case GL_DIFFUSE:  memcpy(L.diffuse, v, 16); break;
        case GL_SPECULAR: memcpy(L.specular, v, 16); break;
        case GL_SPOT_DIRECTION: {
            float x = v[0], y = v[1], z = v[2];
            L.direction[0] = m[0] * x + m[4] * y + m[8]  * z;
            L.direction[1] = m[1] * x + m[5] * y + m[9]  * z;
            L.direction[2] = m[2] * x + m[6] * y + m[10] * z;
            break;
        }
        default: break;
    }
}

void note_lightf(int index, GLenum pname, float v) {
    if (index < 0 || index > 7) return;
    LightState& L = g_st.light[index];
    switch (pname) {
        case GL_CONSTANT_ATTENUATION:  L.atten[0] = v; break;
        case GL_LINEAR_ATTENUATION:    L.atten[1] = v; break;
        case GL_QUADRATIC_ATTENUATION: L.atten[2] = v; break;
        case GL_SPOT_CUTOFF:           L.spot_cutoff = v; break;
        case GL_SPOT_EXPONENT:         L.spot_exponent = v; break;
        default: break;
    }
}

void note_light_enabled(int index, bool en) {
    if (index >= 0 && index < 8) g_st.light[index].enabled = en;
}

void note_light_model_fv(GLenum pname, const float* v) {
    if (!v) return;
    if (pname == GL_LIGHT_MODEL_AMBIENT) memcpy(g_st.light_model_ambient, v, 16);
    else if (pname == GL_LIGHT_MODEL_TWO_SIDE) {
        g_st.two_sided = v[0] != 0.0f;
        static bool warned = false;
        if (g_st.two_sided && !warned) {
            warned = true;
            std::cout << "[FFP] GL_LIGHT_MODEL_TWO_SIDE requested — the vendored ANGLE ES1 "
                         "shader does not implement two-sided materials (it never did); "
                         "front materials are used" << std::endl;
        }
    }
}
void note_light_model_f(GLenum pname, float v) {
    if (pname == GL_LIGHT_MODEL_TWO_SIDE) {
        g_st.two_sided = v != 0.0f;
        static bool warned = false;
        if (g_st.two_sided && !warned) {
            warned = true;
            std::cout << "[FFP] GL_LIGHT_MODEL_TWO_SIDE requested — not implemented by the "
                         "vendored ES1 shader" << std::endl;
        }
    }
}

void note_fogf(GLenum pname, float v) {
    switch (pname) {
        case GL_FOG_START:   g_st.fog_start = v; break;
        case GL_FOG_END:     g_st.fog_end = v; break;
        case GL_FOG_DENSITY: g_st.fog_density = v; break;
        default: break;
    }
}
void note_fogi(GLenum pname, int v) { if (pname == GL_FOG_MODE) g_st.fog_mode = v; }
void note_fogfv(GLenum pname, const float* v) {
    if (!v) return;
    if (pname == GL_FOG_COLOR) memcpy(g_st.fog_color, v, 16);
    else note_fogf(pname, v[0]);
}

// The full ES 1.1 texture environment for unit 0, including every pname that
// GL_COMBINE needs. The guest sets these through glTexEnvi/glTexEnvfv and the
// values are fed to the vendored ANGLE shader, which implements the combiner
// exactly (MODULATE / REPLACE / ADD / ADD_SIGNED / SUBTRACT / INTERPOLATE /
// DOT3 with the ES1 source and operand rules).
void note_texenv(GLenum pname, int iparam, const float* fparam) {
    switch (pname) {
        case 0x2200: g_st.texenv_mode = iparam; break;               // GL_TEXTURE_ENV_MODE
        case 0x2201:                                                  // GL_TEXTURE_ENV_COLOR
            if (fparam) memcpy(g_st.texenv_color, fparam, 16);
            break;
        case 0x8571: g_st.combine_rgb = iparam; break;               // GL_COMBINE_RGB
        case 0x8572: g_st.combine_alpha = iparam; break;             // GL_COMBINE_ALPHA
        case 0x8573:                                                  // GL_RGB_SCALE
            g_st.rgb_scale = fparam ? fparam[0] : (float)iparam;
            break;
        case 0x0D1C:                                                  // GL_ALPHA_SCALE
            g_st.alpha_scale = fparam ? fparam[0] : (float)iparam;
            break;
        case 0x8580: g_st.src0_rgb = iparam; break;
        case 0x8581: g_st.src1_rgb = iparam; break;
        case 0x8582: g_st.src2_rgb = iparam; break;
        case 0x8588: g_st.src0_alpha = iparam; break;
        case 0x8589: g_st.src1_alpha = iparam; break;
        case 0x858A: g_st.src2_alpha = iparam; break;
        case 0x8590: g_st.op0_rgb = iparam; break;
        case 0x8591: g_st.op1_rgb = iparam; break;
        case 0x8592: g_st.op2_rgb = iparam; break;
        case 0x8598: g_st.op0_alpha = iparam; break;
        case 0x8599: g_st.op1_alpha = iparam; break;
        case 0x859A: g_st.op2_alpha = iparam; break;
        default: break;
    }
}

void note_alpha_func(GLenum func, float ref) { g_st.alpha_func = (int)func; g_st.alpha_ref = ref; }
void note_shade_model(GLenum mode) { g_st.shade_model = (int)mode; }
void note_point_size(float size) { g_st.point_size = size; }

void note_matrix_mode(int mode) { g_mat.mode = mode; }
void note_load_identity() { mat_identity(*g_mat.current()); }
void note_load_matrix(int mode, const float* m) {
    if (!m) return;
    if (mode) g_mat.mode = mode;
    mat_copy(*g_mat.current(), m);
}
void note_mult_matrix(int mode, const float* m) {
    if (!m) return;
    if (mode) g_mat.mode = mode;
    Mat4 out;
    mat_mul_t(out, *g_mat.current(), m);
    mat_copy(*g_mat.current(), out);
}
void note_push_matrix() { g_mat.push(); }
void note_pop_matrix() { g_mat.pop(); }

namespace {
// Matrix helpers for the shadow stack (kept out of the public namespace).
void shadow_translate(float x, float y, float z) {
    Mat4 t; mat_identity(t); t[12] = x; t[13] = y; t[14] = z;
    Mat4 out; mat_mul_t(out, *g_mat.current(), t); mat_copy(*g_mat.current(), out);
}
void shadow_scale(float x, float y, float z) {
    Mat4 t; mat_identity(t); t[0] = x; t[5] = y; t[10] = z;
    Mat4 out; mat_mul_t(out, *g_mat.current(), t); mat_copy(*g_mat.current(), out);
}
void shadow_rotate(float deg, float ax, float ay, float az) {
    float len = sqrtf(ax * ax + ay * ay + az * az);
    if (len < 1e-8f) return;
    ax /= len; ay /= len; az /= len;
    float r = deg * 3.14159265358979f / 180.0f;
    float c = cosf(r), s = sinf(r), t = 1.0f - c;
    Mat4 q; mat_identity(q);
    q[0] = t * ax * ax + c;      q[1] = t * ax * ay + s * az; q[2] = t * ax * az - s * ay;
    q[4] = t * ax * ay - s * az; q[5] = t * ay * ay + c;      q[6] = t * ay * az + s * ax;
    q[8] = t * ax * az + s * ay; q[9] = t * ay * az - s * ax; q[10] = t * az * az + c;
    Mat4 out; mat_mul_t(out, *g_mat.current(), q); mat_copy(*g_mat.current(), out);
}
} // namespace

void note_translate(float x, float y, float z) { shadow_translate(x, y, z); }
void note_rotate(float a, float x, float y, float z) { shadow_rotate(a, x, y, z); }
void note_scale(float x, float y, float z) { shadow_scale(x, y, z); }
void note_ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 o; mat_identity(o);
    o[0] =  2.0f / (r - l);
    o[5] =  2.0f / (t - b);
    o[10] = -2.0f / (f - n);
    o[12] = -(r + l) / (r - l);
    o[13] = -(t + b) / (t - b);
    o[14] = -(f + n) / (f - n);
    Mat4 out; mat_mul_t(out, *g_mat.current(), o); mat_copy(*g_mat.current(), out);
}
void note_frustum(float l, float r, float b, float t, float n, float f) {
    Mat4 p; memset(p, 0, sizeof(p));
    p[0]  = (2.0f * n) / (r - l);
    p[5]  = (2.0f * n) / (t - b);
    p[8]  = (r + l) / (r - l);
    p[9]  = (t + b) / (t - b);
    p[10] = -(f + n) / (f - n);
    p[11] = -1.0f;
    p[14] = -(2.0f * f * n) / (f - n);
    Mat4 out; mat_mul_t(out, *g_mat.current(), p); mat_copy(*g_mat.current(), out);
}

void note_arrays(const GuestArrays& a) { g_arr = a; }
void dump_next_draw() { g_dump_next = 2; }

// ============================================================================
//  The draw
// ============================================================================
bool draw(GLenum mode, GLuint texture, int first, int count,
          const void* indices_host, GLenum indices_type) {
    if (g_quality <= OFF) { g_skip_off++; g_draws_vanilla++; return false; }
    if (count <= 0) { g_skip_empty++; g_draws_vanilla++; return false; }
    if (!ensure_program()) { g_skip_noprogram++; g_draws_vanilla++; return false; }

    glGetIntegerv(GL_CURRENT_PROGRAM, &g_saved_prog);
    if (g_saved_prog != 0 && g_saved_prog != (GLint)g_prog) {
        g_skip_foreign++; g_draws_vanilla++; return false;
    }
    if (!g_arr.v_enabled || !g_arr.vptr) { g_skip_no_varray++; g_draws_vanilla++; return false; }
    if (!supported_primitive(mode)) { g_skip_prim++; g_draws_vanilla++; return false; }
    if (count > 400000) { g_skip_big++; g_draws_vanilla++; return false; }
    // A bound texture with no texcoord array means the draw relies on the
    // current glTexCoord, which the engine never sets (no glTexCoord* handler
    // exists) — hand that one back rather than guess.
    if (g_st.texture2d && texture != 0 && !g_arr.t_enabled) {
        g_skip_no_texcoord++; g_draws_vanilla++; return false;
    }

    float mv[16], pr[16], tx[16];
    get_matrices(mv, pr, tx);
    if (!g_matrix_checked) check_matrix_shadow();
    const bool ortho = is_ortho_projection(pr);
    if (g_lit_scope && (!g_st.lit || ortho)) { g_skip_scope++; g_draws_vanilla++; return false; }

    const uint8_t* ib8 = nullptr;
    const uint16_t* ib16 = nullptr;
    const uint32_t* ib32 = nullptr;
    if (indices_host) {
        if (indices_type == 0x1401)      ib8  = (const uint8_t*)indices_host;
        else if (indices_type == 0x1403) ib16 = (const uint16_t*)indices_host;
        else if (indices_type == 0x1405) ib32 = (const uint32_t*)indices_host;
        else { g_skip_indices++; g_draws_vanilla++; return false; }
    }

    const int n_verts = count;
    g_scratch.resize((size_t)n_verts * FPV);
    for (int i = 0; i < n_verts; i++) {
        int src = first + i;
        if (ib8)       src = (int)ib8[i];
        else if (ib16) src = (int)ib16[i];
        else if (ib32) src = (int)ib32[i];
        if (!fetch_vertex(src, &g_scratch[(size_t)i * FPV])) {
            g_skip_fetch++; g_draws_vanilla++; return false;
        }
    }

    size_t bytes = g_scratch.size() * sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    if (bytes > g_vbo_bytes) {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, nullptr, GL_STREAM_DRAW);
        g_vbo_bytes = bytes;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, g_scratch.data());

    glGetIntegerv(0x85B5 /*GL_VERTEX_ARRAY_BINDING*/, &g_saved_vao);
    glGetIntegerv(0x8894 /*GL_ARRAY_BUFFER_BINDING*/, &g_saved_vbo);
    glUseProgram(g_prog);

    // ── Texture ───────────────────────────────────────────────────────────
    // The bound texture and the active unit are read from the driver rather
    // than from the bridge's best-effort cache: this is exactly the state the
    // fixed-function path would sample, and a stale value here is the
    // difference between a textured surface and a flat white one.
    GLint active_unit = GL_TEXTURE0;
    GLint bound_tex = 0;
    glGetIntegerv(0x84E0 /*GL_ACTIVE_TEXTURE*/, &active_unit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_tex);
    int unit_index = (int)(active_unit - GL_TEXTURE0);
    if (unit_index < 0 || unit_index > 7) unit_index = 0;

    // ── Matrices ──────────────────────────────────────────────────────────
    float invtr[16];
    mat_normal_invtr(invtr, mv);
    um4(U.projection, pr);
    um4(U.modelview, mv);
    um4(U.modelview_invtr, invtr);
    if (U.texture_matrix >= 0) glUniformMatrix4fv(U.texture_matrix, 1, GL_FALSE, tx);

    // ── Lighting / material state ─────────────────────────────────────────
    int light_enables[8];
    float lpos[8][4], lamb[8][4], ldif[8][4], lspec[8][4], ldir[8][3];
    float lse[8], lsc[8], lac[8], lal[8], laq[8];
    for (int i = 0; i < 8; i++) {
        const LightState& L = g_st.light[i];
        light_enables[i] = L.enabled ? 1 : 0;
        memcpy(lpos[i], L.pos, 16);
        memcpy(lamb[i], L.ambient, 16);
        memcpy(ldif[i], L.diffuse, 16);
        memcpy(lspec[i], L.specular, 16);
        memcpy(ldir[i], L.direction, 12);
        lse[i] = L.spot_exponent;
        lsc[i] = L.spot_cutoff;
        lac[i] = L.atten[0];
        lal[i] = L.atten[1];
        laq[i] = L.atten[2];
    }
    u1iv(U.st_light_enables, 8, light_enables);
    u4fv(U.light_positions, 8, &lpos[0][0]);
    u4fv(U.light_ambients, 8, &lamb[0][0]);
    u4fv(U.light_diffuses, 8, &ldif[0][0]);
    u4fv(U.light_speculars, 8, &lspec[0][0]);
    u3fv(U.light_directions, 8, &ldir[0][0]);
    u1fv(U.light_spotlight_exponents, 8, lse);
    u1fv(U.light_spotlight_cutoff_angles, 8, lsc);
    u1fv(U.light_attenuation_consts, 8, lac);
    u1fv(U.light_attenuation_linears, 8, lal);
    u1fv(U.light_attenuation_quadratics, 8, laq);

    const int face = FACE_FRONT;
    u4f(U.material_ambient,  g_st.mat[face][F_AMBIENT]);
    u4f(U.material_diffuse,  g_st.mat[face][F_DIFFUSE]);
    u4f(U.material_specular, g_st.mat[face][F_SPECULAR]);
    u4f(U.material_emissive, g_st.mat[face][F_EMISSION]);
    u1f(U.material_specular_exponent, g_st.mat[face][F_SHININESS][0]);
    u4f(U.light_model_scene_ambient, g_st.light_model_ambient);

    // ── Fixed-function enables ────────────────────────────────────────────
    u1i(U.st_enable_lighting, g_st.lit ? 1 : 0);
    u1i(U.st_enable_color_material, g_st.color_material ? 1 : 0);
    u1i(U.st_enable_normalize, g_st.normalize ? 1 : 0);
    u1i(U.st_enable_rescale_normal, g_st.rescale_normal ? 1 : 0);
    u1i(U.st_shade_model_flat, g_st.shade_model == SHADE_FLAT ? 1 : 0);
    u1i(U.st_enable_draw_texture, 0);
    u1i(U.st_point_rasterization, mode == GL_POINTS ? 1 : 0);
    u1i(U.st_point_sprite_enabled, 0);
    u1i(U.st_enable_clip_planes, g_st.clip_planes ? 1 : 0);
    u1i(U.st_enable_alpha_test, g_st.alpha_test ? 1 : 0);
    u1i(U.st_enable_fog, g_st.fog ? 1 : 0);
    u1u(U.st_alpha_func, (unsigned)g_st.alpha_func);
    u1u(U.st_fog_mode, (unsigned)g_st.fog_mode);
    u1f(U.alpha_test_ref, g_st.alpha_ref);
    u1f(U.fog_density, g_st.fog_density);
    u1f(U.fog_start, g_st.fog_start);
    u1f(U.fog_end, g_st.fog_end);
    u4f(U.fog_color, g_st.fog_color);
    u1i(U.st_enable_texture_2d, g_st.texture2d ? 1 : 0);
    u1i(U.st_enable_texture_cube_map, 0);
    u4f(U.texture_env_color, g_st.texenv_color);
    u1i(U.tex_sampler0, unit_index);
    u1i(U.tex_cube_sampler0, unit_index == 7 ? 0 : unit_index + 1);

    // The vendored shader is built with kTexUnits == 1, so the sampler is
    // pointed at whichever unit the fixed pipeline would have used.
    const bool has_tex = (bound_tex != 0) && g_st.texture2d && g_arr.t_enabled;
    if (!has_tex) {
        g_untextured_draws++;
        // Diagnostic: a translated draw that comes out flat-shaded. This is what
        // "the terrain turned white" looks like from the inside, and the state
        // printed here is the difference between guessing and knowing.
        if ((g_trace || g_debug) && g_untextured_reports < 6) {
            g_untextured_reports++;
            std::cout << "[FFP-NOTEX] mode=0x" << std::hex << mode << std::dec
                      << " bound=" << bound_tex << " unit=" << unit_index
                      << " tex2d=" << (g_st.texture2d ? 1 : 0)
                      << " tarr=" << (g_arr.t_enabled ? 1 : 0)
                      << " tptr=0x" << std::hex << g_arr.tptr << std::dec
                      << " tsize=" << g_arr.tsize << " ttype=0x" << std::hex << g_arr.ttype
                      << " tstride=" << std::dec << g_arr.tstride
                      << " vptr=0x" << std::hex << g_arr.vptr << std::dec
                      << " vsize=" << g_arr.vsize << " vsstride=" << g_arr.vstride
                      << " col=" << (g_arr.c_enabled ? 1 : 0)
                      << " lit=" << (g_st.lit ? 1 : 0)
                      << std::endl;
        }
    }
    {
        int enable_tex2d[4] = {has_tex ? 1 : 0, 0, 0, 0};
        unsigned env_mode[4] = {(unsigned)(has_tex ? g_st.texenv_mode : ENV_MODULATE), 0, 0, 0};
        unsigned fmt[4] = {0x1908u, 0x1908u, 0x1908u, 0x1908u};   // kRGBA
        unsigned combine_rgb[4]   = {(unsigned)g_st.combine_rgb, 0, 0, 0};
        unsigned combine_alpha[4] = {(unsigned)g_st.combine_alpha, 0, 0, 0};
        unsigned src0_rgb[4]   = {(unsigned)g_st.src0_rgb, 0, 0, 0};
        unsigned src1_rgb[4]   = {(unsigned)g_st.src1_rgb, 0, 0, 0};
        unsigned src2_rgb[4]   = {(unsigned)g_st.src2_rgb, 0, 0, 0};
        unsigned src0_alpha[4] = {(unsigned)g_st.src0_alpha, 0, 0, 0};
        unsigned src1_alpha[4] = {(unsigned)g_st.src1_alpha, 0, 0, 0};
        unsigned src2_alpha[4] = {(unsigned)g_st.src2_alpha, 0, 0, 0};
        unsigned op0_rgb[4]    = {(unsigned)g_st.op0_rgb, 0, 0, 0};
        unsigned op1_rgb[4]    = {(unsigned)g_st.op1_rgb, 0, 0, 0};
        unsigned op2_rgb[4]    = {(unsigned)g_st.op2_rgb, 0, 0, 0};
        unsigned op0_alpha[4]  = {(unsigned)g_st.op0_alpha, 0, 0, 0};
        unsigned op1_alpha[4]  = {(unsigned)g_st.op1_alpha, 0, 0, 0};
        unsigned op2_alpha[4]  = {(unsigned)g_st.op2_alpha, 0, 0, 0};
        u1iv(U.st_enable_texture_2d, 4, enable_tex2d);
        u1uiv(U.st_texture_env_mode, 4, env_mode);
        u1uiv(U.st_texture_format, 4, fmt);
        u1uiv(U.st_combine_rgb, 4, combine_rgb);
        u1uiv(U.st_combine_alpha, 4, combine_alpha);
        u1uiv(U.st_src0_rgb, 4, src0_rgb);   u1uiv(U.st_src0_alpha, 4, src0_alpha);
        u1uiv(U.st_src1_rgb, 4, src1_rgb);   u1uiv(U.st_src1_alpha, 4, src1_alpha);
        u1uiv(U.st_src2_rgb, 4, src2_rgb);   u1uiv(U.st_src2_alpha, 4, src2_alpha);
        u1uiv(U.st_op0_rgb, 4, op0_rgb);     u1uiv(U.st_op0_alpha, 4, op0_alpha);
        u1uiv(U.st_op1_rgb, 4, op1_rgb);     u1uiv(U.st_op1_alpha, 4, op1_alpha);
        u1uiv(U.st_op2_rgb, 4, op2_rgb);     u1uiv(U.st_op2_alpha, 4, op2_alpha);
        float rgb_scale[4]   = {g_st.rgb_scale, 1.0f, 1.0f, 1.0f};
        float alpha_scale[4] = {g_st.alpha_scale, 1.0f, 1.0f, 1.0f};
        float lod_bias[4]    = {0.0f, 0.0f, 0.0f, 0.0f};
        u1fv(U.texture_env_rgb_scale, 4, rgb_scale);
        u1fv(U.texture_env_alpha_scale, 4, alpha_scale);
        u1fv(U.texture_env_lod_bias, 4, lod_bias);
    }

    // ── Issue it ──────────────────────────────────────────────────────────
    glBindVertexArray(g_vao);
    glDrawArrays(mode, 0, n_verts);
    glBindVertexArray((GLuint)g_saved_vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)g_saved_vbo);
    glUseProgram((GLuint)g_saved_prog);

    g_draws_translated++;
    g_verts += n_verts;
    if (g_arr.vtype != 0x1406 || (g_arr.t_enabled && g_arr.ttype != 0x1406)) g_nonfloat_arrays++;

    // ── Trace: the three questions that decide whether a draw is right ────
    //   1. Is the texture state the shader sees the one the fixed pipeline
    //      would have sampled?  (tex2d / texcoord array / bound id / env mode)
    //   2. Do the texture coordinates land on the art, or somewhere else?
    //   3. Is the bound texture art, or a white placeholder?
    if (g_trace && g_big_reports < 8 && n_verts >= 96) {
        g_big_reports++;
        unsigned char t0[4] = {0, 0, 0, 0};
        float u0 = g_scratch[12], v0 = g_scratch[13];
        const TexProbe* tp = probe_texture(bound_tex);
        bool got = tp && tp->texel(u0, v0, t0);
        std::cout << "[FFP-BIG] id=" << g_big_reports << " verts=" << n_verts
                  << (indices_host ? " indexed" : " arrays")
                  << " bound=" << bound_tex << " has_tex=" << (has_tex ? 1 : 0)
                  << " tex2d=" << (g_st.texture2d ? 1 : 0)
                  << " tarr=" << (g_arr.t_enabled ? 1 : 0)
                  << " tptr=0x" << std::hex << g_arr.tptr << std::dec
                  << " ttype=0x" << std::hex << g_arr.ttype << std::dec
                  << " tsize=" << g_arr.tsize << " tstride=" << g_arr.tstride
                  << " vsize=" << g_arr.vsize << " vstride=" << g_arr.vstride
                  << " carr=" << (g_arr.c_enabled ? 1 : 0)
                  << " texenv=0x" << std::hex << g_st.texenv_mode
                  << " comb_rgb=0x" << g_st.combine_rgb << " comb_a=0x" << g_st.combine_alpha
                  << std::dec
                  << " lit=" << (g_st.lit ? 1 : 0)
                  << " cm=" << (g_st.color_material ? 1 : 0)
                  << " at=" << (g_st.alpha_test ? 1 : 0)
                  << " ortho=" << (ortho ? 1 : 0)
                  << " mat=" << g_st.mat[face][F_DIFFUSE][0] << "," << g_st.mat[face][F_DIFFUSE][1]
                  << "," << g_st.mat[face][F_DIFFUSE][2]
                  << " cc=" << g_scratch[7] << "," << g_scratch[8] << "," << g_scratch[9]
                  << "," << g_scratch[10]
                  << " uv0=" << u0 << "," << v0
                  << " uv1=" << g_scratch[FPV + 12] << "," << g_scratch[FPV + 13]
                  << " uv2=" << g_scratch[2 * FPV + 12] << "," << g_scratch[2 * FPV + 13];
        if (tp) std::cout << " tex=" << tp->w << "x" << tp->h
                          << " white=" << tp->white_frac;
        if (got) std::cout << " texel=" << (int)t0[0] << "," << (int)t0[1] << ","
                           << (int)t0[2] << "," << (int)t0[3];
        std::cout << std::endl;
    }
    if (g_trace && g_drv_reports < 4 && n_verts >= 24) {
        g_drv_reports++;
        // Driver-truth check: the shadow is only useful if it agrees with what
        // the fixed-function path would have used.
        GLfloat drv_mat[4] = {0, 0, 0, 0}, drv_col[4] = {0, 0, 0, 0};
        GLint drv_texenv = 0, drv_tex = 0;
        glGetMaterialfv(GL_FRONT, GL_DIFFUSE, drv_mat);
        glGetFloatv(GL_CURRENT_COLOR, drv_col);
        glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &drv_texenv);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &drv_tex);
        std::cout << "[FFP-DRV] shadow_diffuse=(" << g_st.mat[face][F_DIFFUSE][0] << ","
                  << g_st.mat[face][F_DIFFUSE][1] << "," << g_st.mat[face][F_DIFFUSE][2]
                  << ") driver_diffuse=(" << drv_mat[0] << "," << drv_mat[1] << "," << drv_mat[2]
                  << ") shadow_lit=" << (g_st.lit ? 1 : 0)
                  << " driver_lit=" << (glIsEnabled(GL_LIGHTING) ? 1 : 0)
                  << " shadow_texenv=0x" << std::hex << g_st.texenv_mode
                  << " driver_texenv=0x" << drv_texenv << std::dec
                  << " shadow_tex2d=" << (g_st.texture2d ? 1 : 0)
                  << " driver_tex2d=" << (glIsEnabled(GL_TEXTURE_2D) ? 1 : 0)
                  << " driver_at=" << (glIsEnabled(GL_ALPHA_TEST) ? 1 : 0)
                  << " driver_cm=" << (glIsEnabled(GL_COLOR_MATERIAL) ? 1 : 0)
                  << " cur_col=(" << drv_col[0] << "," << drv_col[1] << "," << drv_col[2] << "," << drv_col[3] << ")"
                  << " driver_bound=" << drv_tex << std::endl;
    }
    if (g_dump_next > 0) {
        g_dump_next--;
        std::cout << "[FFP] draw mode=0x" << std::hex << mode << std::dec
                  << " verts=" << n_verts << (indices_host ? " (indexed)" : "")
                  << " tex=" << texture << " lit=" << (g_st.lit ? 1 : 0)
                  << " cm=" << (g_st.color_material ? 1 : 0)
                  << " fog=" << (g_st.fog ? 1 : 0)
                  << " alpha_test=" << (g_st.alpha_test ? 1 : 0) << std::endl;
    }
    if (g_debug && (g_draws_translated % 500) == 0) {
        std::cout << "[FFP] translated=" << g_draws_translated
                  << " vanilla=" << g_draws_vanilla << " verts=" << g_verts << std::endl;
    }
    return true;
}

void reset_census() {
    g_draws_translated = g_draws_vanilla = g_verts = 0;
    g_skip_off = g_skip_empty = g_skip_noprogram = g_skip_foreign = 0;
    g_skip_no_varray = g_skip_scope = g_skip_no_texcoord = g_skip_prim = 0;
    g_skip_fetch = g_skip_indices = g_skip_big = 0;
    g_nonfloat_arrays = 0;
}

void print_stats() {
    if (!g_stats && !g_trace && g_draws_translated == 0 && g_draws_vanilla == 0) return;
    std::cout << "[FFP] " << g_status << std::endl
              << "[FFP]   draws translated=" << g_draws_translated
              << " handed to vanilla=" << g_draws_vanilla
              << " verts=" << g_verts
              << " matrices=" << (g_matrix_source == 0 ? "driver" : "shadow") << std::endl;
    std::cout << "[FFP]   handed back: off=" << g_skip_off
              << " empty=" << g_skip_empty
              << " no-program=" << g_skip_noprogram
              << " foreign-program=" << g_skip_foreign
              << " no-vertex-array=" << g_skip_no_varray
              << " scope=" << g_skip_scope
              << " no-texcoord=" << g_skip_no_texcoord
              << " primitive=" << g_skip_prim
              << " fetch=" << g_skip_fetch
              << " indices=" << g_skip_indices
              << " too-big=" << g_skip_big << std::endl;
    std::cout << "[FFP]   translated draws with no texture: " << g_untextured_draws
              << std::endl;
    if (g_nonfloat_arrays)
        std::cout << "[FFP]   non-float vertex/texcoord arrays seen: " << g_nonfloat_arrays << std::endl;
}

} // namespace ffp
