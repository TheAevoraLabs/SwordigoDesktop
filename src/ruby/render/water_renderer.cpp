// ============================================================================
// water_renderer.cpp — Animated fluid sheet renderer
//   Matches vanilla Caver::WaterMeshComponent (OpenSwordigo/arm32_13):
//     - Column count = roundf(rect.w / 20.0f)
//     - 4-vertex column slices:
//         V0: (x, rect.y, +40)             [bottom underwater boundary]
//         V1: (x, rect.y+rect.h+wave, +40) [front surface crest]
//         V2: (x, rect.y+rect.h+wave, +40) [surface front edge]
//         V3: (x, rect.y+rect.h+wave, -20) [surface back edge]
//     - Front face drawn with front_color * object_color
//     - Top surface sheet drawn with surface_color * object_color
//     - Surface wave equation: 6.0*sin(x/160 + 0.2t) + 6.0*sin(x/320 + 0.6t)
//     - Surface UV scroll: u += 0.2*sin(col/20 + 0.5t)
// ============================================================================

#include "water_renderer.h"
#include "tools/scene_workspace.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ruby::render {

static const char* k_water_vs = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uMVP;

out vec2 vUV;
out vec3 vLocalPos;

void main() {
    vUV = aUV;
    vLocalPos = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* k_water_fs = R"GLSL(
#version 330 core
in vec2 vUV;
in vec3 vLocalPos;

uniform sampler2D uTexture;
uniform int       uHasTex;
uniform vec4      uFrontColor;
uniform vec4      uSurfaceColor;
uniform float     uScroll;
uniform float     uTime;
uniform int       uIsBackFace;

out vec4 FragColor;

void main() {
    // Flowing UVs with dual-speed wave perturbation
    vec2 uv = vUV + vec2(uScroll, uScroll * 0.35);
    vec4 tex = (uHasTex != 0) ? texture(uTexture, uv) : vec4(1.0);

    vec4 baseColor = (uIsBackFace != 0) ? uSurfaceColor : uFrontColor;
    vec3 col = tex.rgb * baseColor.rgb;
    float a = baseColor.a * (uHasTex != 0 ? tex.a : 1.0);

    // Subtle surface highlight for the 3D top surface sheet
    if (uIsBackFace != 0) {
        float caustic = sin(vLocalPos.x * 0.08 + uTime * 2.2) * cos(vLocalPos.z * 0.08 + uTime * 1.6) * 0.05;
        col += vec3(0.06 + max(0.0, caustic));
    } else {
        // Front face: soft crest sheen near the undulating surface
        float crest = smoothstep(0.88, 1.0, 1.0 - vUV.y) * 0.15;
        col += vec3(crest);
    }

    // Extended Reinhard tone mapping matching ViewportShader pipeline
    col = (col * (1.0 + col / 6.25)) / (1.0 + col);

    FragColor = vec4(clamp(col, 0.0, 1.0), clamp(a, 0.0, 1.0));
}
)GLSL";

static void mult_mat4(float out[16], const float a[16], const float b[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = sum;
        }
    }
}

WaterRenderer::~WaterRenderer() {
    shutdown();
}

bool WaterRenderer::init() {
    // Bail out rather than call through a null table (offscreen / core-profile
    // contexts cannot supply the 3.3 compatibility entry points).
    if (!initializeOpenGLFunctions()) {
        fprintf(stderr, "[WaterRenderer] GL 3.3 compatibility table unavailable — water effects disabled.\n");
        return false;
    }

    auto compile_shader = [this](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
            fprintf(stderr, "[WaterRenderer] Shader error: %s\n", buf);
            glDeleteShader(s);
            return 0u;
        }
        return s;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER, k_water_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_water_fs);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(m_prog, sizeof(buf), nullptr, buf);
        fprintf(stderr, "[WaterRenderer] Program link error: %s\n", buf);
        glDeleteProgram(m_prog);
        m_prog = 0;
        return false;
    }

    m_loc_mvp           = glGetUniformLocation(m_prog, "uMVP");
    m_loc_texture       = glGetUniformLocation(m_prog, "uTexture");
    m_loc_has_tex       = glGetUniformLocation(m_prog, "uHasTex");
    m_loc_front_color   = glGetUniformLocation(m_prog, "uFrontColor");
    m_loc_surface_color = glGetUniformLocation(m_prog, "uSurfaceColor");
    m_loc_scroll        = glGetUniformLocation(m_prog, "uScroll");
    m_loc_time          = glGetUniformLocation(m_prog, "uTime");
    m_loc_is_back_face  = glGetUniformLocation(m_prog, "uIsBackFace");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

    // layout(location = 0) in vec3 aPos;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    // layout(location = 1) in vec2 aUV;
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return true;
}

void WaterRenderer::shutdown() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }
}

void WaterRenderer::render(const std::vector<av::SceneData::SceneWater>& waters,
                           const std::vector<av::SceneObject>& objects,
                           const std::unordered_map<std::string, GLuint>& tex_cache,
                           const float view_proj[16],
                           float time_sec) {
    if (!m_prog || waters.empty()) return;

    // Fluid pass state setup: translucent forward rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    glUseProgram(m_prog);
    glUniform1i(m_loc_texture, 0);
    glUniform1f(m_loc_time, time_sec);
    glUniform1f(m_loc_scroll, time_sec * 0.05f);

    glBindVertexArray(m_vao);

    constexpr float k_two_pi = 6.28318530718f;

    for (const auto& w : waters) {
        if (w.object_index < 0 || w.object_index >= (int)objects.size()) continue;
        const auto& obj = objects[w.object_index];
        if (obj.hidden) continue;

        const float rx = w.rect[0], ry = w.rect[1], rw = w.rect[2], rh = w.rect[3];
        if (rw <= 0.0f || rh <= 0.0f) continue;

        // Vanilla formula (Caver::WaterMeshComponent::CreateMesh):
        // cols = (int)roundf(rect.w / 20.0f);
        int cols = std::max(1, static_cast<int>(std::round(rw / 20.0f)));
        cols = std::min(cols, 128); // Generous guard against runaway vertex counts
        const float col_w = rw / static_cast<float>(cols);
        const int m = cols + 1;
        const int num_verts = m * 4;

        // Thickness: local Z front_z for front face, back_z for back face
        const float front_z = (w.front_z == 0.0f && w.back_z == 0.0f) ? 40.0f : w.front_z;
        const float back_z  = (w.front_z == 0.0f && w.back_z == 0.0f) ? -20.0f : w.back_z;

        std::vector<float> verts(static_cast<size_t>(num_verts) * 5, 0.0f);
        // cols * 6 indices for front face, cols * 6 indices for top surface face
        std::vector<uint16_t> idx(static_cast<size_t>(cols) * 12, 0);

        const float tile = w.tile_size > 0.0f ? w.tile_size : 64.0f;
        const float ox = w.tex_offset[0], oy = w.tex_offset[1];
        const float baseline_surface = ry + rh;

        for (int i = 0; i < m; ++i) {
            const float x = rx + i * col_w;

            // Dual sine wave displacement from Caver::WaterMeshComponent::UpdateMesh
            // v8 = (x / 160.0 + time * 0.2) * 2PI;
            // wave1 = sin(v8) * 6.0;
            // v10 = (x / 320.0 + time * 0.6) * 2PI;
            // wave = wave1 + sin(v10) * 6.0;
            const float phase1 = (x / 160.0f + time_sec * 0.2f) * k_two_pi;
            const float phase2 = (x / 320.0f + time_sec * 0.6f) * k_two_pi;
            const float wave = std::sin(phase1) * 6.0f + std::sin(phase2) * 6.0f;
            const float surface_y = baseline_surface + wave;

            // UV scroll formula from Caver::WaterMeshComponent::UpdateMesh
            // u = base_u + 0.2 * sin((i / 20.0 + time * 0.5) * 2PI)
            const float u_scroll = std::sin((i / 20.0f + time_sec * 0.5f) * k_two_pi) * 0.2f;

            const size_t base = static_cast<size_t>(i) * 4 * 5;

            // 4 vertices per slice (from CreateMesh.c & UpdateMesh.c):
            // V0: (x, ry, +40.0f)        [submerged bottom edge, camera front]
            // V1: (x, surface_y, +40.0f) [surface edge, camera front]
            // V2: (x, surface_y, +40.0f) [surface front edge, top sheet]
            // V3: (x, surface_y, -20.0f) [surface back edge, top sheet]
            const float vx[4] = { x, x, x, x };
            const float vy[4] = { ry, surface_y, surface_y, surface_y };
            const float vz[4] = { front_z, front_z, front_z, back_z };

            // UV coordinates: V0 bottom, V1/V2/V3 surface with wave scroll
            const float u_base = (x - ox) / tile + 0.5f;
            const float uv_u[4] = { u_base, u_base + u_scroll, u_base + u_scroll, u_base + u_scroll };
            const float uv_v[4] = {
                (ry - oy) / tile + 0.5f,
                (surface_y - oy) / tile + 0.5f,
                (surface_y - oy) / tile + 0.5f,
                (surface_y - oy) / tile + 0.5f
            };

            for (int v = 0; v < 4; ++v) {
                float* out = &verts[base + static_cast<size_t>(v) * 5];
                out[0] = vx[v];
                out[1] = vy[v];
                out[2] = vz[v];
                out[3] = uv_u[v];
                out[4] = uv_v[v];
            }

            // Triangle indexing matching CreateMesh.c:
            if (i < cols) {
                const uint16_t n = static_cast<uint16_t>(i * 4);
                // Front face quad: V0, V1 of col i with V0, V1 of col i+1 (indices n, n+1, n+4, n+5)
                idx[i * 6 + 0] = n;
                idx[i * 6 + 1] = n + 1;
                idx[i * 6 + 2] = n + 4;
                idx[i * 6 + 3] = n + 1;
                idx[i * 6 + 4] = n + 5;
                idx[i * 6 + 5] = n + 4;

                // Top surface sheet quad: V2, V3 of col i with V2, V3 of col i+1 (indices n+2, n+3, n+6, n+7)
                const size_t surf_offset = static_cast<size_t>(cols) * 6;
                idx[surf_offset + i * 6 + 0] = n + 3;
                idx[surf_offset + i * 6 + 1] = n + 2;
                idx[surf_offset + i * 6 + 2] = n + 6;
                idx[surf_offset + i * 6 + 3] = n + 3;
                idx[surf_offset + i * 6 + 4] = n + 6;
                idx[surf_offset + i * 6 + 5] = n + 7;
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_DYNAMIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint16_t)),
                     idx.data(), GL_DYNAMIC_DRAW);

        // Object world matrix and final MVP
        float wmat[16];
        swk::object_world_matrix(obj, wmat);
        float mvp[16];
        mult_mat4(mvp, view_proj, wmat);
        glUniformMatrix4fv(m_loc_mvp, 1, GL_FALSE, mvp);

        // Resolve texture
        GLuint tex_id = 0;
        if (!w.texture.empty()) {
            auto it = tex_cache.find(w.texture);
            if (it != tex_cache.end()) tex_id = it->second;
        }

        glUniform1i(m_loc_has_tex, tex_id != 0 ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id);

        glUniform4fv(m_loc_front_color, 1, w.front_color);
        glUniform4fv(m_loc_surface_color, 1, w.surface_color);

        // 1. Draw Front Face (cols * 6 indices)
        glUniform1i(m_loc_is_back_face, 0);
        glDrawElements(GL_TRIANGLES, cols * 6, GL_UNSIGNED_SHORT, nullptr);

        // 2. Draw Top Surface Sheet (cols * 6 indices)
        glUniform1i(m_loc_is_back_face, 1);
        glDrawElements(GL_TRIANGLES, cols * 6, GL_UNSIGNED_SHORT,
                       (void*)(size_t)(cols * 6 * sizeof(uint16_t)));
    }

    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace ruby::render
