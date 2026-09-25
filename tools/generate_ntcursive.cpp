/* tools/generate_ntcursive.cpp — 3D Cursive "NeverTrolls" Generator
 *
 * Generates an intricate, flowing 3D cursive script sculpture of "NeverTrolls"
 * with an ornamental calligraphic underline swash, built using 3D Catmull-Rom
 * splines, rotation-minimizing parallel transport frames (Bishop frames),
 * smooth circular tube geometry, and rounded hemispherical stroke caps.
 *
 * Emitted as two-sided solid geometry into Caver's SurfaceMesh (field 8)
 * inside a complete .scene protobuf file.
 *
 * Output: ~/.local/share/swordigo-desktop/assets/resources/ntcursive.scene
 *
 * Build:
 *   g++ -std=c++17 -O2 -I src tools/generate_ntcursive.cpp -lm -o bin/generate_ntcursive
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstring>

#include "platform/protobuf_reader.h"

namespace fs = std::filesystem;
static const float PI = static_cast<float>(M_PI);

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const {
        float l = length();
        return l > 1e-9f ? (*this) * (1.0f / l) : Vec3{0, 1, 0};
    }
};

struct Vert {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct Mesh {
    std::vector<Vert>     verts;
    std::vector<uint16_t> indices;
    std::string           texture = "graveyard_ground";
};

// ─────────────────────────────────────────────────────────────────────────────
// Raw byte & Protobuf serialization helpers
// ─────────────────────────────────────────────────────────────────────────────

static void push_f32(std::vector<uint8_t>& out, float v) {
    uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

static void push_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static proto::Writer pw_vec2(float x, float y) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    return w;
}

static proto::Writer pw_rect(float x, float y, float ww, float hh) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, ww);
    w.write_float_field(4, hh);
    return w;
}

static proto::Writer pw_color(float r, float g, float b, float a) {
    proto::Writer w;
    w.write_float_field(1, r);
    w.write_float_field(2, g);
    w.write_float_field(3, b);
    w.write_float_field(4, a);
    return w;
}

static proto::Writer pw_mesh_attr(int vtype, int vper, int stride, int offset) {
    proto::Writer w;
    w.write_varint_field(1, static_cast<uint64_t>(vtype));
    w.write_varint_field(2, static_cast<uint64_t>(vper));
    w.write_varint_field(3, static_cast<uint64_t>(stride));
    w.write_varint_field(4, static_cast<uint64_t>(offset));
    return w;
}

static proto::Writer pw_aabb(float x, float y, float z, float ww, float hh, float d) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, z);
    w.write_float_field(4, ww);
    w.write_float_field(5, hh);
    w.write_float_field(6, d);
    return w;
}

static proto::Writer pw_material(const std::string& tex) {
    proto::Writer t;
    t.write_string_field(1, tex);
    t.write_varint_field(2, 1);
    t.write_varint_field(4, 1);
    proto::Writer m;
    m.write_nested_field(1, pw_color(1, 1, 1, 1));
    m.write_nested_field(2, pw_color(1, 1, 1, 1));
    m.write_nested_field(3, pw_color(1, 1, 1, 1));
    m.write_float_field(4, 0.0f);
    m.write_nested_field(5, t);
    return m;
}

static proto::Writer encode_mesh(const Mesh& m) {
    std::vector<uint8_t> vbuf, ibuf;
    float mn[3] = {1e9f, 1e9f, 1e9f};
    float mx[3] = {-1e9f, -1e9f, -1e9f};

    for (const auto& v : m.verts) {
        float p[8] = {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v};
        for (float f : p) push_f32(vbuf, f);
        for (int c = 0; c < 3; ++c) {
            if (p[c] < mn[c]) mn[c] = p[c];
            if (p[c] > mx[c]) mx[c] = p[c];
        }
    }
    if (m.verts.empty()) mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0;

    bool indexed = !m.indices.empty();
    if (indexed) {
        for (uint16_t i : m.indices) push_u16(ibuf, i);
    }

    size_t nv = m.verts.size();
    size_t nf = indexed ? m.indices.size() / 3 : nv / 3;

    proto::Writer w;
    w.write_varint_field(1, nv);
    w.write_varint_field(2, nf);
    if (indexed) w.write_nested_field(3, pw_mesh_attr(4, 1, 2, 0));
    w.write_nested_field(4, pw_mesh_attr(7, 3, 32, 0));
    w.write_nested_field(5, pw_mesh_attr(7, 3, 32, 12));
    w.write_nested_field(6, pw_mesh_attr(7, 2, 32, 24));
    w.write_nested_field(10, pw_material(m.texture));
    w.write_nested_field(11, pw_aabb(mn[0], mn[1], mn[2],
                                     mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]));
    w.write_bytes_field(50, std::string(vbuf.begin(), vbuf.end()));
    if (indexed) w.write_bytes_field(51, std::string(ibuf.begin(), ibuf.end()));
    return w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Two-sided geometry helpers
// ─────────────────────────────────────────────────────────────────────────────

static void push_quad(Mesh& m,
                      const Vert& a, const Vert& b,
                      const Vert& c, const Vert& d) {
    auto base = static_cast<uint16_t>(m.verts.size());
    m.verts.push_back(a); // 0
    m.verts.push_back(b); // 1
    m.verts.push_back(c); // 2
    m.verts.push_back(d); // 3
    // Side 1
    m.indices.push_back(base + 0); m.indices.push_back(base + 2); m.indices.push_back(base + 1);
    m.indices.push_back(base + 1); m.indices.push_back(base + 2); m.indices.push_back(base + 3);
    // Side 2 (reverse)
    m.indices.push_back(base + 0); m.indices.push_back(base + 1); m.indices.push_back(base + 2);
    m.indices.push_back(base + 1); m.indices.push_back(base + 3); m.indices.push_back(base + 2);
}

static void push_tri(Mesh& m, const Vert& a, const Vert& b, const Vert& c) {
    auto base = static_cast<uint16_t>(m.verts.size());
    m.verts.push_back(a);
    m.verts.push_back(b);
    m.verts.push_back(c);
    m.indices.push_back(base + 0); m.indices.push_back(base + 1); m.indices.push_back(base + 2);
    m.indices.push_back(base + 0); m.indices.push_back(base + 2); m.indices.push_back(base + 1);
}


// ─────────────────────────────────────────────────────────────────────────────
// Build complete SceneObject → Scene protobuf blob
// Verified wire field numbers (scene_schemas.cpp & boulderx.cpp):
//   GroundMeshComponent:
//     field 7  = LocalAabb
//     field 8  = SurfaceMesh (repeated)
//     field 9  = FrontMesh (repeated)
//     field 10 = Color
// ─────────────────────────────────────────────────────────────────────────────

static std::string build_scene_object(
    const std::string& name,
    const std::vector<Mesh>& surface,
    const std::vector<Mesh>& front,
    float bnd_x, float bnd_y,
    float pos_x = 0.0f, float pos_y = 0.0f,
    int base_cid = 8000)
{
    const std::string tex = surface.empty()
        ? (front.empty() ? "graveyard_ground" : front[0].texture)
        : surface[0].texture;

    // ── GroundPolygon ─────────────────────────────────────────────────────
    proto::Writer poly;
    float px[4] = {-bnd_x, bnd_x,  bnd_x, -bnd_x};
    float py[4] = {-bnd_y,-bnd_y,  bnd_y,  bnd_y};
    for (int i = 0; i < 4; ++i)
        poly.write_nested_field(1, pw_vec2(px[i], py[i]));
    poly.write_varint_field(2, 1);
    poly.write_varint_field(3, 1);

    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1);
    gpc.write_float_field(4, -bnd_y);
    gpc.write_float_field(5,  bnd_y);

    proto::Writer comp_poly;
    comp_poly.write_string_field(1, "GroundPolygon");
    comp_poly.write_varint_field(2, static_cast<uint64_t>(base_cid));
    comp_poly.write_nested_field(110, gpc);

    // ── GroundMeshComponent ───────────────────────────────────────────────
    proto::Writer gmc;
    // field 7 = LocalAabb
    gmc.write_nested_field(7, pw_rect(-bnd_x, -bnd_y, 2 * bnd_x, 2 * bnd_y));
    // field 8 = SurfaceMesh (repeated)
    for (const auto& sm : surface)
        gmc.write_nested_field(8, encode_mesh(sm));
    // field 9 = FrontMesh (repeated)
    for (const auto& fm : front)
        gmc.write_nested_field(9, encode_mesh(fm));
    // field 10 = Color
    gmc.write_nested_field(10, pw_color(1, 1, 1, 1));

    proto::Writer comp_mesh;
    comp_mesh.write_string_field(1, "GroundMesh");
    comp_mesh.write_varint_field(2, static_cast<uint64_t>(base_cid + 1));
    comp_mesh.write_nested_field(111, gmc);

    // ── GroundMeshGenerator ───────────────────────────────────────────────
    proto::Writer ggc;
    ggc.write_varint_field(1, static_cast<uint64_t>(base_cid));
    ggc.write_varint_field(2, static_cast<uint64_t>(base_cid + 1));
    ggc.write_varint_field(3, static_cast<uint64_t>(base_cid + 2));
    ggc.write_varint_field(4, static_cast<uint64_t>(base_cid + 3));
    ggc.write_varint_field(5, 1291618994ULL);
    ggc.write_float_field(6,  0.0f);
    ggc.write_varint_field(7,  1);
    ggc.write_float_field(8,  2 * bnd_x);
    ggc.write_float_field(9,  bnd_y);
    ggc.write_float_field(10, bnd_x);
    ggc.write_float_field(11, bnd_y * 0.5f);

    proto::Writer comp_gen;
    comp_gen.write_string_field(1, "GroundMeshGenerator");
    comp_gen.write_varint_field(2, static_cast<uint64_t>(base_cid + 4));
    comp_gen.write_nested_field(112, ggc);

    // ── CollisionShape ────────────────────────────────────────────────────
    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1);
    csc.write_float_field(6, -bnd_y);
    csc.write_float_field(7,  bnd_y);
    csc.write_varint_field(11, 1);

    proto::Writer comp_col;
    comp_col.write_string_field(1, "CollisionShape");
    comp_col.write_varint_field(2, static_cast<uint64_t>(base_cid + 5));
    comp_col.write_varint_field(4, static_cast<uint64_t>(base_cid));
    comp_col.write_nested_field(120, shape);
    comp_col.write_nested_field(121, csc);

    // ── TextureMapping ────────────────────────────────────────────────────
    auto make_tm = [&](int id) {
        proto::Writer tm;
        tm.write_string_field(1, tex);
        tm.write_float_field(2, 200.0f);
        tm.write_nested_field(3, pw_vec2(0, 0));
        proto::Writer c;
        c.write_string_field(1, "TextureMapping");
        c.write_varint_field(2, static_cast<uint64_t>(id));
        c.write_nested_field(113, tm);
        return c;
    };

    // ── SceneObject ───────────────────────────────────────────────────────
    proto::Writer obj;
    obj.write_string_field(1, "SceneObject");
    obj.write_string_field(2, name);
    obj.write_bytes_field(3, comp_poly.to_string());
    obj.write_bytes_field(3, comp_mesh.to_string());
    obj.write_bytes_field(3, comp_gen.to_string());
    obj.write_bytes_field(3, comp_col.to_string());
    obj.write_bytes_field(3, make_tm(base_cid + 3).to_string());
    obj.write_bytes_field(3, make_tm(base_cid + 2).to_string());
    obj.write_nested_field(4, pw_vec2(pos_x, pos_y));
    obj.write_float_field(5, 0.0f);
    obj.write_float_field(6, 0.0f);
    obj.write_float_field(7, 1.0f);
    obj.write_nested_field(8, pw_rect(-bnd_x, -bnd_y, 2 * bnd_x, 2 * bnd_y));
    obj.write_varint_field(9, 0);

    proto::Writer scene;
    scene.write_bytes_field(1, obj.to_string());
    return scene.to_string();
}


// ─────────────────────────────────────────────────────────────────────────────
// 3D Tube Sweep with Parallel Transport Frame (Bishop Frame)
// ─────────────────────────────────────────────────────────────────────────────

static void build_tube(Mesh& m, const std::vector<Vec3>& curve, float radius, int tube_sides = 12) {
    int M = static_cast<int>(curve.size());
    if (M < 2) return;

    // 1. Compute tangents along the curve
    std::vector<Vec3> T(M);
    for (int i = 0; i < M; ++i) {
        if (i == 0) T[i] = (curve[1] - curve[0]).normalized();
        else if (i == M - 1) T[i] = (curve[M - 1] - curve[M - 2]).normalized();
        else T[i] = (curve[i + 1] - curve[i - 1]).normalized();
    }

    // 2. Parallel Transport Frame (Bishop Frame)
    std::vector<Vec3> N(M), B(M);
    Vec3 ref = {0.0f, 0.0f, 1.0f};
    if (std::abs(T[0].dot(ref)) > 0.85f) ref = {0.0f, 1.0f, 0.0f};
    N[0] = (ref - T[0] * ref.dot(T[0])).normalized();
    B[0] = T[0].cross(N[0]).normalized();

    for (int i = 1; i < M; ++i) {
        Vec3 ni = N[i - 1] - T[i] * N[i - 1].dot(T[i]);
        float l = ni.length();
        if (l < 1e-5f) {
            Vec3 r = (std::abs(T[i].z) < 0.85f) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            ni = r - T[i] * r.dot(T[i]);
        }
        N[i] = ni.normalized();
        B[i] = T[i].cross(N[i]).normalized();
    }

    // 3. Cumulative distance along curve for texture mapping (v coordinate)
    std::vector<float> dist(M, 0.0f);
    for (int i = 1; i < M; ++i) {
        dist[i] = dist[i - 1] + (curve[i] - curve[i - 1]).length();
    }

    // Helper: generate vertex on ring i at angle theta
    auto ring_vert = [&](int i, int j, float r) -> Vert {
        float theta = 2.0f * PI * static_cast<float>(j) / tube_sides;
        float cos_th = std::cos(theta);
        float sin_th = std::sin(theta);
        Vec3 norm = (N[i] * cos_th + B[i] * sin_th).normalized();
        Vec3 pos = curve[i] + norm * r;
        float u = static_cast<float>(j) / tube_sides * 2.0f;
        float v = dist[i] * 0.03f;
        return {pos.x, pos.y, pos.z, norm.x, norm.y, norm.z, u, v};
    };

    // 4. Generate tube rings and connect adjacent rings
    for (int i = 0; i < M - 1; ++i) {
        // Subtle calligraphic taper at the very tips
        float r_curr = radius;
        float r_next = radius;
        if (i < 3) r_curr *= (0.4f + 0.6f * (static_cast<float>(i) / 3.0f));
        if (i + 1 < 3) r_next *= (0.4f + 0.6f * (static_cast<float>(i + 1) / 3.0f));
        if (i > M - 4) r_curr *= (0.4f + 0.6f * (static_cast<float>(M - 1 - i) / 3.0f));
        if (i + 1 > M - 4) r_next *= (0.4f + 0.6f * (static_cast<float>(M - 1 - (i + 1)) / 3.0f));

        for (int j = 0; j < tube_sides; ++j) {
            int j_next = (j + 1) % tube_sides;
            Vert a = ring_vert(i,     j,      r_curr);
            Vert b = ring_vert(i,     j_next, r_curr);
            Vert c = ring_vert(i + 1, j,      r_next);
            Vert d = ring_vert(i + 1, j_next, r_next);
            push_quad(m, a, b, c, d);
        }
    }

    // 5. Rounded start cap (hemispherical tip)
    {
        Vec3 cap_tip = curve[0] - T[0] * (radius * 0.8f);
        float r_start = radius * 0.4f;
        for (int j = 0; j < tube_sides; ++j) {
            int j_next = (j + 1) % tube_sides;
            Vert a = ring_vert(0, j, r_start);
            Vert b = ring_vert(0, j_next, r_start);
            Vert tip = {cap_tip.x, cap_tip.y, cap_tip.z, -T[0].x, -T[0].y, -T[0].z, 0.5f, 0.5f};
            push_tri(m, tip, a, b);
        }
    }

    // 6. Rounded end cap (hemispherical tip)
    {
        Vec3 cap_tip = curve[M - 1] + T[M - 1] * (radius * 0.8f);
        float r_end = radius * 0.4f;
        for (int j = 0; j < tube_sides; ++j) {
            int j_next = (j + 1) % tube_sides;
            Vert a = ring_vert(M - 1, j, r_end);
            Vert b = ring_vert(M - 1, j_next, r_end);
            Vert tip = {cap_tip.x, cap_tip.y, cap_tip.z, T[M - 1].x, T[M - 1].y, T[M - 1].z, 0.5f, 0.5f};
            push_tri(m, tip, b, a);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Catmull-Rom Spline Interpolator
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<Vec3> interpolate_spline(const std::vector<Vec3>& pts, int steps_per_segment = 10) {
    if (pts.size() < 2) return pts;
    int n = static_cast<int>(pts.size());

    std::vector<Vec3> padded;
    padded.push_back(pts[0] - (pts[1] - pts[0]));
    padded.insert(padded.end(), pts.begin(), pts.end());
    padded.push_back(pts[n - 1] + (pts[n - 1] - pts[n - 2]));

    std::vector<Vec3> res;
    for (size_t i = 0; i + 3 < padded.size(); ++i) {
        Vec3 p0 = padded[i];
        Vec3 p1 = padded[i + 1];
        Vec3 p2 = padded[i + 2];
        Vec3 p3 = padded[i + 3];

        for (int s = 0; s < steps_per_segment; ++s) {
            float t = static_cast<float>(s) / steps_per_segment;
            float t2 = t * t;
            float t3 = t2 * t;

            Vec3 pt = (p1 * 2.0f +
                      (p2 - p0) * t +
                      (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
                      (p0 * -1.0f + p1 * 3.0f - p2 * 3.0f + p3) * t3) * 0.5f;
            res.push_back(pt);
        }
    }
    res.push_back(pts.back());
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cursive Letter Stroke Definitions
// ─────────────────────────────────────────────────────────────────────────────

static void build_nevertrolls_cursive(std::vector<Mesh>& meshes, const std::string& texture) {
    // Helper: add spline stroke as 3D tube submesh
    auto add_stroke = [&](const std::vector<Vec3>& ctrl_pts, float radius, int sub = 8) {
        Mesh m;
        m.texture = texture;
        auto smooth_curve = interpolate_spline(ctrl_pts, sub);
        build_tube(m, smooth_curve, radius, 12);
        meshes.push_back(std::move(m));
    };

    const float R_TEXT = 8.5f;   // tube radius for cursive letters
    const float R_SWASH = 10.5f; // tube radius for underline swash

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 1: Capital 'N' (flowing entrance loop, dual crests, foot sweep)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> n_pts = {
        {-560, 110, 0},
        {-550, 160, 5},
        {-535, 195, 10},
        {-520, 180, 5},
        {-525, 100, 0},
        {-530,  15, -4},
        {-524,   0, 0},
        {-512,  30, 4},
        {-498, 120, 9},
        {-482, 185, 12},
        {-465, 192, 8},
        {-445, 120, 0},
        {-425,  40, -4},
        {-415,   0, 0},
        {-400,  80, 5},
        {-385, 160, 8},
        {-375, 180, 5},
        {-365, 140, 0},
        {-365,  50, -4},
        {-360,   0, 0},
        {-345,  35, 2} // leads into 'e1'
    };
    add_stroke(n_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 2: 'e' (first)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> e1_pts = {
        {-345, 35, 2},
        {-330, 65, 5},
        {-315, 90, 6},
        {-300, 95, 4},
        {-290, 75, 0},
        {-305, 55, -4},
        {-325, 45, -2},
        {-330, 20, 0},
        {-315,  0, 0},
        {-295, 10, 2},
        {-275, 35, 4} // leads into 'v'
    };
    add_stroke(e1_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 3: 'v'
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> v_pts = {
        {-275, 35, 4},
        {-260, 70, 6},
        {-245, 90, 5},
        {-235, 80, 2},
        {-240, 40, -2},
        {-235, 10, -3},
        {-220,  0, 0},
        {-205, 25, 3},
        {-190, 65, 6},
        {-180, 90, 8},
        {-170, 85, 5},
        {-165, 65, 0},
        {-155, 35, 2} // bridge into 'e2'
    };
    add_stroke(v_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 4: 'e' (second)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> e2_pts = {
        {-155, 35, 2},
        {-140, 65, 5},
        {-125, 90, 6},
        {-110, 95, 4},
        {-100, 75, 0},
        {-115, 55, -4},
        {-135, 45, -2},
        {-140, 20, 0},
        {-125,  0, 0},
        {-105, 10, 2},
        {-85,  35, 4} // leads into 'r1'
    };
    add_stroke(e2_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 5: 'r' (first)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> r1_pts = {
        {-85, 35, 4},
        {-70, 70, 6},
        {-58, 92, 8},
        {-52, 98, 6},
        {-40, 88, 3},
        {-25, 85, 2},
        {-20, 55, -2},
        {-18, 15, -3},
        {-10,  0, 0},
        {  8, 15, 3},
        { 28, 42, 6} // terminal sweep of "Never"
    };
    add_stroke(r1_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 6: Capital 'T' — Undulating Ornamental Hat
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> t_hat = {
        { 48, 185, 5},
        { 65, 205, 8},
        { 88, 220, 10},
        {118, 215, 7},
        {142, 198, 3},
        {168, 208, 6},
        {192, 220, 8}
    };
    add_stroke(t_hat, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 7: Capital 'T' — Elegant S-Stem & Swash Foot
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> t_stem = {
        {120, 198, 5},
        {116, 160, 6},
        {110, 120, 4},
        {115,  70, 0},
        {118,  25, -2},
        {110,   0, 0},
        { 94,   8, 3},
        { 85,  26, 5},
        {102,  36, 6},
        {128,  35, 4} // leads into 'r2'
    };
    add_stroke(t_stem, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 8: 'r' (second, in Trolls)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> r2_pts = {
        {128, 35, 4},
        {142, 70, 6},
        {154, 92, 8},
        {160, 98, 6},
        {172, 88, 3},
        {186, 85, 2},
        {192, 55, -2},
        {194, 15, -3},
        {202,  0, 0},
        {218, 20, 3},
        {232, 45, 5} // leads into 'o'
    };
    add_stroke(r2_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 9: 'o'
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> o_pts = {
        {232, 45, 5},
        {248, 75, 7},
        {262, 92, 6},
        {278, 85, 3},
        {282, 50, -2},
        {276, 10, -4},
        {260,  0, 0},
        {245, 15, 3},
        {238, 50, 6},
        {252, 85, 8},
        {270, 92, 6},
        {288, 82, 4},
        {300, 60, 2} // leads into 'l1'
    };
    add_stroke(o_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 10: 'l' (first tall ascender loop)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> l1_pts = {
        {300,  60, 2},
        {312, 100, 5},
        {328, 160, 8},
        {338, 205, 10},
        {345, 215, 8},
        {338, 195, 3},
        {322, 140, -3},
        {316,  80, -5},
        {322,  25, -2},
        {332,   0, 0},
        {352,  15, 3},
        {372,  45, 5} // leads into 'l2'
    };
    add_stroke(l1_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 11: 'l' (second tall ascender loop)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> l2_pts = {
        {372,  45, 5},
        {384, 100, 5},
        {398, 160, 8},
        {408, 205, 10},
        {415, 215, 8},
        {408, 195, 3},
        {392, 140, -3},
        {386,  80, -5},
        {392,  25, -2},
        {402,   0, 0},
        {420,  15, 3},
        {438,  40, 5} // leads into 's'
    };
    add_stroke(l2_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 12: 's' (cursive flourish finish)
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> s_pts = {
        {438, 40, 5},
        {452, 75, 7},
        {465, 95, 8},
        {472, 100, 6},
        {478, 85, 3},
        {482, 55, 0},
        {474, 25, -3},
        {458,  5, -4},
        {445,  0, 0},
        {452, 12, 4},
        {470, 18, 6},
        {500, 25, 8},
        {540, 36, 12},
        {585, 50, 15} // grand finishing sweep
    };
    add_stroke(s_pts, R_TEXT);

    // ─────────────────────────────────────────────────────────────────────────
    // Stroke 13: Underline Calligraphic Swash
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<Vec3> swash_pts = {
        {-540, -45, 0},
        {-450, -62, 5},
        {-300, -78, 8},
        {-100, -85, 10},
        { 100, -80, 10},
        { 300, -65, 8},
        { 480, -48, 6},
        { 590, -30, 10}
    };
    add_stroke(swash_pts, R_SWASH);
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::string out_path = "/home/quantumcreeper/.local/share/swordigo-desktop/assets/resources/ntcursive.scene";
    if (argc > 1) out_path = argv[1];

    std::cout << "========================================\n";
    std::cout << "  Zypher 3D Cursive 'NeverTrolls'\n";
    std::cout << "  Output: " << out_path << "\n";
    std::cout << "========================================\n\n";

    fs::path p(out_path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path());
    }

    const std::string TEX = "graveyard_ground";

    std::vector<Mesh> surface;
    std::cout << "Generating 3D cursive strokes for 'NeverTrolls' ...\n";
    build_nevertrolls_cursive(surface, TEX);

    size_t total_v = 0, total_f = 0;
    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;
    for (const auto& m : surface) {
        total_v += m.verts.size();
        total_f += m.indices.size() / 3;
        for (const auto& v : m.verts) {
            if (v.x < min_x) min_x = v.x;
            if (v.x > max_x) max_x = v.x;
            if (v.y < min_y) min_y = v.y;
            if (v.y > max_y) max_y = v.y;
        }
    }

    std::cout << "  Submeshes   : " << surface.size() << "\n";
    std::cout << "  Total Verts : " << total_v << "\n";
    std::cout << "  Total Faces : " << total_f << "\n";

    float bnd_x = std::max(std::abs(min_x), std::abs(max_x)) + 30.0f;
    float bnd_y = std::max(std::abs(min_y), std::abs(max_y)) + 30.0f;

    std::cout << "  Bounds      : X [-" << bnd_x << ", " << bnd_x << "], Y [-" << bnd_y << ", " << bnd_y << "]\n";

    std::vector<Mesh> front; // Empty — full 3D is in SurfaceMesh (field 8)

    std::string blob = build_scene_object("NeverTrolls", surface, front, bnd_x, bnd_y, 0.0f, 0.0f, 8000);

    std::ofstream f(out_path, std::ios::binary);
    if (!f) {
        std::cerr << "ERROR: cannot write to " << out_path << "\n";
        return 1;
    }
    f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
    f.close();

    std::cout << "Successfully written " << blob.size() << " bytes -> " << out_path << "\n";
    std::cout << "========================================\n";
    std::cout << "  Cursive 'NeverTrolls' Ready!\n";
    std::cout << "========================================\n";
    return 0;
}
