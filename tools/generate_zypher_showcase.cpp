/* tools/generate_zypher_showcase.cpp — Zypher Showcase Generator (v4)
 *
 * Fully closed solid 3D meshes with CONTOUR-ACCURATE COLLISION for all shapes:
 *   1. zypher_terrain.scene        — Full Catmull-Rom spline collision contour (walkable rolling hills)
 *   2. zypher_pillar.scene         — Exact pillar rectangular cross-section collision (walkable top cap + solid sides)
 *   3. zypher_dome.scene           — Semicircular dome + skirt collision contour (walkable curved dome)
 *   4. zypher_arch.scene           — Hollow gateway collision contour (open passable archway tunnel + walkable pillars/top)
 *   5. zypher_torus.scene          — Smooth 36-point circular outline collision
 *   6. zypher_bevel_platform.scene — Exact beveled platform collision contour (flat top + radial bevels)
 *
 * Wire fields verified against libswordigo / scene_schemas.cpp / boulderx.cpp:
 *   GroundMeshComponent:
 *     field 7  = LocalAabb (Rectangle)
 *     field 8  = SurfaceMesh (repeated Mesh)
 *     field 9  = FrontMesh (repeated Mesh)
 *     field 10 = Color (FloatColor)
 *
 * Build:
 *   g++ -std=c++17 -O2 -I src tools/generate_zypher_showcase.cpp -lm -o bin/generate_zypher_showcase
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <utility>

#include "platform/protobuf_reader.h"

namespace fs = std::filesystem;
static const float PI = static_cast<float>(M_PI);

// ─────────────────────────────────────────────────────────────────────────────
// Vertex layout: 8 floats = 32 bytes
//   [0..2]  position  (x, y, z)
//   [3..5]  normal    (nx, ny, nz)
//   [6..7]  texcoord  (u, v)
// ─────────────────────────────────────────────────────────────────────────────

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
// Raw byte helpers
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

// ─────────────────────────────────────────────────────────────────────────────
// Protobuf helpers
// ─────────────────────────────────────────────────────────────────────────────

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
// Build complete SceneObject → Scene protobuf blob
// With custom contour collision polygon!
// ─────────────────────────────────────────────────────────────────────────────

static std::string build_scene_object(
    const std::string& name,
    const std::vector<Mesh>& surface,
    const std::vector<Mesh>& front,
    const std::vector<std::pair<float, float>>& outline,
    float depth_z,
    float bnd_x, float bnd_y,
    float pos_x = 0.0f, float pos_y = 0.0f,
    int base_cid = 2000)
{
    const std::string tex = surface.empty()
        ? (front.empty() ? "graveyard_ground" : front[0].texture)
        : surface[0].texture;

    // Ensure polygon is strictly Counter-Clockwise (CCW, positive signed area).
    // Chipmunk 2D requires CCW order so all outward normals point outside the body.
    float signed_area = 0.0f;
    for (size_t i = 0; i < outline.size(); ++i) {
        size_t next = (i + 1) % outline.size();
        signed_area += (outline[i].first * outline[next].second - outline[next].first * outline[i].second);
    }
    std::vector<std::pair<float, float>> ccw_outline = outline;
    if (signed_area < 0.0f) {
        std::reverse(ccw_outline.begin(), ccw_outline.end());
    }

    // ── GroundPolygon (Accurate 2D Physics Contour) ───────────────────────
    proto::Writer poly;
    for (const auto& pt : ccw_outline) {
        poly.write_nested_field(1, pw_vec2(pt.first, pt.second));
    }
    poly.write_varint_field(2, 0); // Convex: 0 (enables concave/curved/vaulted polygon shapes)
    poly.write_varint_field(3, 1); // Closed: 1

    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1); // Collides: 1
    gpc.write_float_field(4, -depth_z); // MinDepth
    gpc.write_float_field(5,  depth_z); // MaxDepth

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

    // ── CollisionShape (Chipmunk Physics Body) ─────────────────────────────
    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1); // IsGround: 1
    csc.write_float_field(6, -depth_z); // MinDepth
    csc.write_float_field(7,  depth_z); // MaxDepth
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
// Robust Two-Sided Geometry Helpers
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
// Shape 1: CATMULL-ROM TERRAIN BLOCK (walkable wave collision)
// ─────────────────────────────────────────────────────────────────────────────
static void gen_terrain(std::vector<Mesh>& surf_out, std::vector<Mesh>& /*front_out*/,
                        std::vector<std::pair<float, float>>& outline_out,
                        const std::string& tex)
{
    const int   SEGS   = 120;
    const float HALF_W = 600.0f;
    const float DEPTH  = 90.0f;
    const float FLOOR  = -160.0f;

    const float ctrl[] = {-30, 40, 80, 20, -20, 60, 100, 50, -10, 40, -40, 70, 90, 30, -30};
    const int NC = static_cast<int>(sizeof(ctrl) / sizeof(ctrl[0]));

    auto catmull = [&](int seg) -> float {
        float t = static_cast<float>(seg) / SEGS;
        float ft = t * (NC - 1);
        int i1 = static_cast<int>(ft);
        if (i1 >= NC - 1) i1 = NC - 2;
        float lt = ft - i1;
        int i0 = std::max(0, i1 - 1);
        int i2 = std::min(NC - 1, i1 + 1);
        int i3 = std::min(NC - 1, i1 + 2);
        float p0 = ctrl[i0], p1 = ctrl[i1], p2 = ctrl[i2], p3 = ctrl[i3];
        return 0.5f * ((2 * p1) + (-p0 + p2) * lt + (2 * p0 - 5 * p1 + 4 * p2 - p3) * lt * lt + (-p0 + 3 * p1 - 3 * p2 + p3) * lt * lt * lt);
    };

    Mesh m;
    m.texture = tex;

    std::vector<float> ys(SEGS + 1);
    for (int s = 0; s <= SEGS; ++s) ys[s] = catmull(s);

    for (int s = 0; s < SEGS; ++s) {
        float x0 = -HALF_W + 2 * HALF_W * s / SEGS;
        float x1 = -HALF_W + 2 * HALF_W * (s + 1) / SEGS;
        float y0 = ys[s], y1 = ys[s + 1];

        float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy) + 1e-9f;
        float nx = -dy / len, ny = dx / len;

        float u0 = static_cast<float>(s) / SEGS * 6.0f;
        float u1 = static_cast<float>(s + 1) / SEGS * 6.0f;

        // Top rolling surface
        push_quad(m,
            {x0, y0, -DEPTH, nx, ny, 0, u0, 0.0f},
            {x1, y1, -DEPTH, nx, ny, 0, u1, 0.0f},
            {x0, y0,  DEPTH, nx, ny, 0, u0, 1.0f},
            {x1, y1,  DEPTH, nx, ny, 0, u1, 1.0f});

        // Front cliff face
        push_quad(m,
            {x0, FLOOR, DEPTH, 0, 0, 1, u0, 2.0f},
            {x1, FLOOR, DEPTH, 0, 0, 1, u1, 2.0f},
            {x0, y0,    DEPTH, 0, 0, 1, u0, 0.0f},
            {x1, y1,    DEPTH, 0, 0, 1, u1, 0.0f});

        // Back cliff face
        push_quad(m,
            {x0, FLOOR, -DEPTH, 0, 0, -1, u0, 2.0f},
            {x1, FLOOR, -DEPTH, 0, 0, -1, u1, 2.0f},
            {x0, y0,    -DEPTH, 0, 0, -1, u0, 0.0f},
            {x1, y1,    -DEPTH, 0, 0, -1, u1, 0.0f});

        // Bottom floor face
        push_quad(m,
            {x0, FLOOR, -DEPTH, 0, -1, 0, u0, 0.0f},
            {x1, FLOOR, -DEPTH, 0, -1, 0, u1, 0.0f},
            {x0, FLOOR,  DEPTH, 0, -1, 0, u0, 1.0f},
            {x1, FLOOR,  DEPTH, 0, -1, 0, u1, 1.0f});
    }

    // Left end cap
    push_quad(m,
        {-HALF_W, FLOOR, -DEPTH, -1, 0, 0, 0.0f, 1.0f},
        {-HALF_W, FLOOR,  DEPTH, -1, 0, 0, 1.0f, 1.0f},
        {-HALF_W, ys[0], -DEPTH, -1, 0, 0, 0.0f, 0.0f},
        {-HALF_W, ys[0],  DEPTH, -1, 0, 0, 1.0f, 0.0f});

    // Right end cap
    push_quad(m,
        {HALF_W, FLOOR, -DEPTH, 1, 0, 0, 0.0f, 1.0f},
        {HALF_W, FLOOR,  DEPTH, 1, 0, 0, 1.0f, 1.0f},
        {HALF_W, ys[SEGS], -DEPTH, 1, 0, 0, 0.0f, 0.0f},
        {HALF_W, ys[SEGS],  DEPTH, 1, 0, 0, 1.0f, 0.0f});

    surf_out.push_back(std::move(m));

    // ── Accurate Catmull-Rom Collision Contour (CCW winding, 40 segments for zero ghost snags) ──
    outline_out.clear();
    const int COLL_SEGS = 40;
    // 1. Bottom-left to bottom-right (rightward along floor)
    outline_out.push_back({-HALF_W, FLOOR});
    outline_out.push_back({HALF_W, FLOOR});
    // 2. Follow wave from right to left (leftward across top curve)
    for (int s = COLL_SEGS; s >= 0; --s) {
        float x = -HALF_W + 2 * HALF_W * s / COLL_SEGS;
        outline_out.push_back({x, catmull(s * SEGS / COLL_SEGS)});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape 2: CYLINDRICAL PILLAR (standable top cap + solid vertical collision)
// ─────────────────────────────────────────────────────────────────────────────
static void gen_pillar(std::vector<Mesh>& surf_out, std::vector<Mesh>& /*front_out*/,
                       std::vector<std::pair<float, float>>& outline_out,
                       const std::string& tex)
{
    const int   SEGS   = 48;
    const float RADIUS = 90.0f;
    const float HALF_H = 180.0f;

    Mesh m;
    m.texture = tex;

    for (int i = 0; i < SEGS; ++i) {
        float a0 = 2.0f * PI * i / SEGS;
        float a1 = 2.0f * PI * (i + 1) / SEGS;

        float x0 = RADIUS * std::cos(a0), z0 = RADIUS * std::sin(a0);
        float x1 = RADIUS * std::cos(a1), z1 = RADIUS * std::sin(a1);

        float cu0 = 0.5f + 0.5f * std::cos(a0), cv0 = 0.5f + 0.5f * std::sin(a0);
        float cu1 = 0.5f + 0.5f * std::cos(a1), cv1 = 0.5f + 0.5f * std::sin(a1);

        float u0 = static_cast<float>(i) / SEGS * 4.0f;
        float u1 = static_cast<float>(i + 1) / SEGS * 4.0f;

        float nx0 = std::cos(a0), nz0 = std::sin(a0);
        float nx1 = std::cos(a1), nz1 = std::sin(a1);

        // Top cap disc
        push_tri(m,
            {0.0f, HALF_H, 0.0f, 0, 1, 0, 0.5f, 0.5f},
            {x0,   HALF_H, z0,   0, 1, 0, cu0,  cv0},
            {x1,   HALF_H, z1,   0, 1, 0, cu1,  cv1});

        // Tube wall
        push_quad(m,
            {x0, -HALF_H, z0, nx0, 0, nz0, u0, 2.0f},
            {x1, -HALF_H, z1, nx1, 0, nz1, u1, 2.0f},
            {x0,  HALF_H, z0, nx0, 0, nz0, u0, 0.0f},
            {x1,  HALF_H, z1, nx1, 0, nz1, u1, 0.0f});

        // Bottom cap disc
        push_tri(m,
            {0.0f, -HALF_H, 0.0f, 0, -1, 0, 0.5f, 0.5f},
            {x0,   -HALF_H, z0,   0, -1, 0, cu0,  cv0},
            {x1,   -HALF_H, z1,   0, -1, 0, cu1,  cv1});
    }

    surf_out.push_back(std::move(m));

    // ── Exact Pillar Rectangle Collision ──
    outline_out = {
        {-RADIUS, -HALF_H},
        { RADIUS, -HALF_H},
        { RADIUS,  HALF_H},
        {-RADIUS,  HALF_H}
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape 3: DOME / HEMISPHERE (climbable curved dome surface collision)
// ─────────────────────────────────────────────────────────────────────────────
static void gen_dome(std::vector<Mesh>& surf_out, std::vector<Mesh>& /*front_out*/,
                     std::vector<std::pair<float, float>>& outline_out,
                     const std::string& tex)
{
    const int   RINGS   = 20;
    const int   SECTORS = 48;
    const float RADIUS  = 240.0f;
    const float SKIRT_H = 40.0f;

    Mesh m;
    m.texture = tex;

    auto sv = [&](int ri, int si) -> Vert {
        float phi = PI / 2.0f * ri / RINGS;
        float th  = 2.0f * PI * si / SECTORS;
        float y   = RADIUS * std::sin(phi);
        float r2  = RADIUS * std::cos(phi);
        float x   = r2 * std::cos(th);
        float z   = r2 * std::sin(th);
        return {x, y, z, x / RADIUS, y / RADIUS, z / RADIUS,
                static_cast<float>(si) / SECTORS * 4.0f,
                static_cast<float>(ri) / RINGS * 2.0f};
    };

    // Upper dome
    for (int i = 0; i < RINGS; ++i) {
        for (int j = 0; j < SECTORS; ++j) {
            push_quad(m, sv(i, j), sv(i, j + 1), sv(i + 1, j), sv(i + 1, j + 1));
        }
    }

    // Vertical skirt
    for (int j = 0; j < SECTORS; ++j) {
        float th0 = 2.0f * PI * j / SECTORS;
        float th1 = 2.0f * PI * (j + 1) / SECTORS;
        float x0 = RADIUS * std::cos(th0), z0 = RADIUS * std::sin(th0);
        float x1 = RADIUS * std::cos(th1), z1 = RADIUS * std::sin(th1);
        float u0 = static_cast<float>(j) / SECTORS * 4.0f;
        float u1 = static_cast<float>(j + 1) / SECTORS * 4.0f;

        push_quad(m,
            {x0, -SKIRT_H, z0, std::cos(th0), 0, std::sin(th0), u0, 1.0f},
            {x1, -SKIRT_H, z1, std::cos(th1), 0, std::sin(th1), u1, 1.0f},
            {x0,  0.0f,    z0, std::cos(th0), 0, std::sin(th0), u0, 0.0f},
            {x1,  0.0f,    z1, std::cos(th1), 0, std::sin(th1), u1, 0.0f});
    }

    // Bottom base disc
    for (int j = 0; j < SECTORS; ++j) {
        float th0 = 2.0f * PI * j / SECTORS;
        float th1 = 2.0f * PI * (j + 1) / SECTORS;
        float x0 = RADIUS * std::cos(th0), z0 = RADIUS * std::sin(th0);
        float x1 = RADIUS * std::cos(th1), z1 = RADIUS * std::sin(th1);
        float cu0 = 0.5f + 0.5f * std::cos(th0), cv0 = 0.5f + 0.5f * std::sin(th0);
        float cu1 = 0.5f + 0.5f * std::cos(th1), cv1 = 0.5f + 0.5f * std::sin(th1);

        push_tri(m,
            {0.0f, -SKIRT_H, 0.0f, 0, -1, 0, 0.5f, 0.5f},
            {x0,   -SKIRT_H, z0,   0, -1, 0, cu0,  cv0},
            {x1,   -SKIRT_H, z1,   0, -1, 0, cu1,  cv1});
    }

    surf_out.push_back(std::move(m));

    // ── Exact Semicircular Dome Collision Contour ──
    outline_out.clear();
    outline_out.push_back({-RADIUS, -SKIRT_H});
    outline_out.push_back({-RADIUS, 0.0f});
    const int N_DOME_POLY = 32;
    for (int k = 0; k <= N_DOME_POLY; ++k) {
        float a = PI * (1.0f - static_cast<float>(k) / N_DOME_POLY);
        outline_out.push_back({RADIUS * std::cos(a), RADIUS * std::sin(a)});
    }
    outline_out.push_back({RADIUS, -SKIRT_H});
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape 4: ARCH / GATEWAY (walk-through hollow arch opening + solid pillars)
// ─────────────────────────────────────────────────────────────────────────────
static void gen_arch(std::vector<Mesh>& surf_out, std::vector<Mesh>& /*front_out*/,
                     std::vector<std::pair<float, float>>& outline_out,
                     const std::string& tex)
{
    const float SPAN       = 240.0f;
    const float PIL_W      = 60.0f;
    const float PILLAR_H   = 200.0f;
    const float PILLAR_BOT = -100.0f;
    const float ARCH_R_IN  = SPAN - PIL_W;
    const float ARCH_R_OUT = SPAN + PIL_W;
    const float DEPTH      = 90.0f;
    const int   ARCH_N     = 36;

    Mesh m;
    m.texture = tex;

    auto add_solid_box = [&](float xmin, float xmax, float ymin, float ymax, float zmin, float zmax) {
        push_quad(m, {xmin, ymin, zmax, 0, 0, 1, 0, 1}, {xmax, ymin, zmax, 0, 0, 1, 1, 1},
                     {xmin, ymax, zmax, 0, 0, 1, 0, 0}, {xmax, ymax, zmax, 0, 0, 1, 1, 0});
        push_quad(m, {xmin, ymin, zmin, 0, 0, -1, 0, 1}, {xmax, ymin, zmin, 0, 0, -1, 1, 1},
                     {xmin, ymax, zmin, 0, 0, -1, 0, 0}, {xmax, ymax, zmin, 0, 0, -1, 1, 0});
        push_quad(m, {xmin, ymin, zmin, -1, 0, 0, 0, 1}, {xmin, ymin, zmax, -1, 0, 0, 1, 1},
                     {xmin, ymax, zmin, -1, 0, 0, 0, 0}, {xmin, ymax, zmax, -1, 0, 0, 1, 0});
        push_quad(m, {xmax, ymin, zmin, 1, 0, 0, 0, 1}, {xmax, ymin, zmax, 1, 0, 0, 1, 1},
                     {xmax, ymax, zmin, 1, 0, 0, 0, 0}, {xmax, ymax, zmax, 1, 0, 0, 1, 0});
        push_quad(m, {xmin, ymin, zmin, 0, -1, 0, 0, 0}, {xmax, ymin, zmin, 0, -1, 0, 1, 0},
                     {xmin, ymin, zmax, 0, -1, 0, 0, 1}, {xmax, ymin, zmax, 0, -1, 0, 1, 1});
        push_quad(m, {xmin, ymax, zmin, 0, 1, 0, 0, 0}, {xmax, ymax, zmin, 0, 1, 0, 1, 0},
                     {xmin, ymax, zmax, 0, 1, 0, 0, 1}, {xmax, ymax, zmax, 0, 1, 0, 1, 1});
    };

    // Left and Right solid pillars
    add_solid_box(-SPAN - PIL_W, -SPAN + PIL_W, PILLAR_BOT, PILLAR_H, -DEPTH, DEPTH);
    add_solid_box( SPAN - PIL_W,  SPAN + PIL_W, PILLAR_BOT, PILLAR_H, -DEPTH, DEPTH);

    // Arch structure
    for (int s = 0; s < ARCH_N; ++s) {
        float t0 = static_cast<float>(s) / ARCH_N;
        float t1 = static_cast<float>(s + 1) / ARCH_N;
        float a0 = PI * t0;
        float a1 = PI * t1;

        float xo0 = -ARCH_R_OUT * std::cos(a0), yo0 = ARCH_R_OUT * std::sin(a0) + PILLAR_H;
        float xo1 = -ARCH_R_OUT * std::cos(a1), yo1 = ARCH_R_OUT * std::sin(a1) + PILLAR_H;
        float xi0 = -ARCH_R_IN  * std::cos(a0), yi0 = ARCH_R_IN  * std::sin(a0) + PILLAR_H;
        float xi1 = -ARCH_R_IN  * std::cos(a1), yi1 = ARCH_R_IN  * std::sin(a1) + PILLAR_H;

        float mid_a = (a0 + a1) * 0.5f;
        float onx = -std::cos(mid_a), ony = std::sin(mid_a);

        // Outer Top Curve
        push_quad(m,
            {xo0, yo0, -DEPTH, onx, ony, 0, t0 * 3.0f, 0.0f},
            {xo1, yo1, -DEPTH, onx, ony, 0, t1 * 3.0f, 0.0f},
            {xo0, yo0,  DEPTH, onx, ony, 0, t0 * 3.0f, 1.0f},
            {xo1, yo1,  DEPTH, onx, ony, 0, t1 * 3.0f, 1.0f});

        // Inner Vault Ceiling
        push_quad(m,
            {xi0, yi0, -DEPTH, -onx, -ony, 0, t0 * 3.0f, 0.0f},
            {xi1, yi1, -DEPTH, -onx, -ony, 0, t1 * 3.0f, 0.0f},
            {xi0, yi0,  DEPTH, -onx, -ony, 0, t0 * 3.0f, 1.0f},
            {xi1, yi1,  DEPTH, -onx, -ony, 0, t1 * 3.0f, 1.0f});

        // Front Face
        push_quad(m,
            {xi0, yi0, DEPTH, 0, 0, 1, t0 * 2.0f, 1.0f},
            {xi1, yi1, DEPTH, 0, 0, 1, t1 * 2.0f, 1.0f},
            {xo0, yo0, DEPTH, 0, 0, 1, t0 * 2.0f, 0.0f},
            {xo1, yo1, DEPTH, 0, 0, 1, t1 * 2.0f, 0.0f});

        // Back Face
        push_quad(m,
            {xi0, yi0, -DEPTH, 0, 0, -1, t0 * 2.0f, 1.0f},
            {xi1, yi1, -DEPTH, 0, 0, -1, t1 * 2.0f, 1.0f},
            {xo0, yo0, -DEPTH, 0, 0, -1, t0 * 2.0f, 0.0f},
            {xo1, yo1, -DEPTH, 0, 0, -1, t1 * 2.0f, 0.0f});
    }

    surf_out.push_back(std::move(m));

    // ── Hollow Archway Collision Contour (Tunnel in center is 100% open!) ──
    outline_out.clear();
    // 1. Left pillar outer base
    outline_out.push_back({-SPAN - PIL_W, PILLAR_BOT});
    // 2. Up left outer wall
    outline_out.push_back({-SPAN - PIL_W, PILLAR_H});
    // 3. Outer arch curve (from PI down to 0)
    const int N_ARCH_POLY = 24;
    for (int k = 0; k <= N_ARCH_POLY; ++k) {
        float a = PI * (1.0f - static_cast<float>(k) / N_ARCH_POLY);
        outline_out.push_back({-ARCH_R_OUT * std::cos(a), ARCH_R_OUT * std::sin(a) + PILLAR_H});
    }
    // 4. Down right outer wall
    outline_out.push_back({SPAN + PIL_W, PILLAR_BOT});
    // 5. Across right pillar base
    outline_out.push_back({SPAN - PIL_W, PILLAR_BOT});
    // 6. Up right inner wall
    outline_out.push_back({SPAN - PIL_W, PILLAR_H});
    // 7. Inner arch vault ceiling curve (from 0 up to PI)
    for (int k = 0; k <= N_ARCH_POLY; ++k) {
        float a = PI * (static_cast<float>(k) / N_ARCH_POLY);
        outline_out.push_back({-ARCH_R_IN * std::cos(a), ARCH_R_IN * std::sin(a) + PILLAR_H});
    }
    // 8. Down left inner wall
    outline_out.push_back({-SPAN + PIL_W, PILLAR_BOT});
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape 5: TORUS (smooth 36-point circular collision profile)
// ─────────────────────────────────────────────────────────────────────────────
static void gen_torus(std::vector<Mesh>& surf_out, std::vector<Mesh>& /*front_out*/,
                      std::vector<std::pair<float, float>>& outline_out,
                      const std::string& tex)
{
    const float R     = 180.0f;
    const float r     = 60.0f;
    const int   MAJOR = 48;
    const int   MINOR = 24;

    Mesh m;
    m.texture = tex;

    auto tv = [&](int mi, int mj) -> Vert {
        float phi   = 2.0f * PI * mi / MAJOR;
        float theta = 2.0f * PI * mj / MINOR;

        float cx = R * std::cos(phi), cz = R * std::sin(phi);
        float x  = (R + r * std::cos(theta)) * std::cos(phi);
        float y  = r * std::sin(theta);
        float z  = (R + r * std::cos(theta)) * std::sin(phi);

        float nx = (x - cx) / r, ny = y / r, nz = (z - cz) / r;
        return {x, y, z, nx, ny, nz,
                static_cast<float>(mi) / MAJOR * 6.0f,
                static_cast<float>(mj) / MINOR * 2.0f};
    };

    for (int i = 0; i < MAJOR; ++i) {
        for (int j = 0; j < MINOR; ++j) {
            push_quad(m, tv(i, j), tv(i + 1, j), tv(i, j + 1), tv(i + 1, j + 1));
        }
    }

    surf_out.push_back(std::move(m));

    // ── Smooth 36-Point Outer Circular Profile ──
    outline_out.clear();
    const float R_OUT = R + r;
    const int N_TORUS_POLY = 36;
    for (int k = 0; k < N_TORUS_POLY; ++k) {
        float a = 2.0f * PI * k / N_TORUS_POLY;
        outline_out.push_back({R_OUT * std::cos(a), R_OUT * std::sin(a)});
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shape 6: BEVELED PLATFORM (flat top + radial bevel collision contour)
// ─────────────────────────────────────────────────────────────────────────────
static void gen_bevel_platform(std::vector<Mesh>& surf_out, std::vector<Mesh>& /*front_out*/,
                                std::vector<std::pair<float, float>>& outline_out,
                                const std::string& tex)
{
    const float HALF_W = 360.0f;
    const float HALF_H = 45.0f;
    const float BEV_R  = 30.0f;
    const float DEPTH  = 95.0f;
    const int   BEV_N  = 12;
    const float IW     = HALF_W - BEV_R;

    Mesh m;
    m.texture = tex;

    // Top face
    push_quad(m,
        {-IW, HALF_H, -DEPTH, 0, 1, 0, 0.1f, 0.0f},
        { IW, HALF_H, -DEPTH, 0, 1, 0, 0.9f, 0.0f},
        {-IW, HALF_H,  DEPTH, 0, 1, 0, 0.1f, 1.0f},
        { IW, HALF_H,  DEPTH, 0, 1, 0, 0.9f, 1.0f});

    // Left & Right top bevel arcs
    for (int side : {-1, 1}) {
        float cx = side * IW;
        float cy = HALF_H - BEV_R;

        for (int k = 0; k < BEV_N; ++k) {
            float t0 = static_cast<float>(k) / BEV_N;
            float t1 = static_cast<float>(k + 1) / BEV_N;

            float a0 = PI / 2.0f * (1.0f - t0);
            float a1 = PI / 2.0f * (1.0f - t1);
            if (side < 0) { a0 = PI - a0; a1 = PI - a1; }

            float x0 = cx + BEV_R * std::cos(a0), y0 = cy + BEV_R * std::sin(a0);
            float x1 = cx + BEV_R * std::cos(a1), y1 = cy + BEV_R * std::sin(a1);

            float nx0 = std::cos(a0), ny0 = std::sin(a0);
            float nx1 = std::cos(a1), ny1 = std::sin(a1);

            push_quad(m,
                {x0, y0, -DEPTH, nx0, ny0, 0, (x0 / (2 * HALF_W) + 0.5f), 0.0f},
                {x1, y1, -DEPTH, nx1, ny1, 0, (x1 / (2 * HALF_W) + 0.5f), 0.0f},
                {x0, y0,  DEPTH, nx0, ny0, 0, (x0 / (2 * HALF_W) + 0.5f), 1.0f},
                {x1, y1,  DEPTH, nx1, ny1, 0, (x1 / (2 * HALF_W) + 0.5f), 1.0f});
        }
    }

    float SIDE_Y = HALF_H - BEV_R;

    // Front Face
    push_quad(m,
        {-IW, -HALF_H, DEPTH, 0, 0, 1, 0.1f, 1.0f},
        { IW, -HALF_H, DEPTH, 0, 0, 1, 0.9f, 1.0f},
        {-IW,  SIDE_Y, DEPTH, 0, 0, 1, 0.1f, 0.2f},
        { IW,  SIDE_Y, DEPTH, 0, 0, 1, 0.9f, 0.2f});

    for (int side : {-1, 1}) {
        float cx = side * IW;
        float cy = HALF_H - BEV_R;
        for (int k = 0; k < BEV_N; ++k) {
            float t0 = static_cast<float>(k) / BEV_N;
            float t1 = static_cast<float>(k + 1) / BEV_N;
            float a0 = PI / 2.0f * t0, a1 = PI / 2.0f * t1;
            if (side < 0) { a0 = PI - a0; a1 = PI - a1; }
            float x0 = cx + BEV_R * std::cos(a0), y0 = cy + BEV_R * std::sin(a0);
            float x1 = cx + BEV_R * std::cos(a1), y1 = cy + BEV_R * std::sin(a1);
            push_quad(m,
                {x0, -HALF_H, DEPTH, 0, 0, 1, 0.0f, 1.0f},
                {x1, -HALF_H, DEPTH, 0, 0, 1, 1.0f, 1.0f},
                {x0,  y0,     DEPTH, 0, 0, 1, 0.0f, 0.0f},
                {x1,  y1,     DEPTH, 0, 0, 1, 1.0f, 0.0f});
        }
    }

    // Back Face
    push_quad(m,
        {-IW, -HALF_H, -DEPTH, 0, 0, -1, 0.1f, 1.0f},
        { IW, -HALF_H, -DEPTH, 0, 0, -1, 0.9f, 1.0f},
        {-IW,  SIDE_Y, -DEPTH, 0, 0, -1, 0.1f, 0.2f},
        { IW,  SIDE_Y, -DEPTH, 0, 0, -1, 0.9f, 0.2f});

    for (int side : {-1, 1}) {
        float cx = side * IW;
        float cy = HALF_H - BEV_R;
        for (int k = 0; k < BEV_N; ++k) {
            float t0 = static_cast<float>(k) / BEV_N;
            float t1 = static_cast<float>(k + 1) / BEV_N;
            float a0 = PI / 2.0f * t0, a1 = PI / 2.0f * t1;
            if (side < 0) { a0 = PI - a0; a1 = PI - a1; }
            float x0 = cx + BEV_R * std::cos(a0), y0 = cy + BEV_R * std::sin(a0);
            float x1 = cx + BEV_R * std::cos(a1), y1 = cy + BEV_R * std::sin(a1);
            push_quad(m,
                {x0, -HALF_H, -DEPTH, 0, 0, -1, 0.0f, 1.0f},
                {x1, -HALF_H, -DEPTH, 0, 0, -1, 1.0f, 1.0f},
                {x0,  y0,     -DEPTH, 0, 0, -1, 0.0f, 0.0f},
                {x1,  y1,     -DEPTH, 0, 0, -1, 1.0f, 0.0f});
        }
    }

    // Left and Right walls
    push_quad(m,
        {-HALF_W, -HALF_H, -DEPTH, -1, 0, 0, 0.0f, 1.0f},
        {-HALF_W, -HALF_H,  DEPTH, -1, 0, 0, 1.0f, 1.0f},
        {-HALF_W,  SIDE_Y, -DEPTH, -1, 0, 0, 0.0f, 0.0f},
        {-HALF_W,  SIDE_Y,  DEPTH, -1, 0, 0, 1.0f, 0.0f});

    push_quad(m,
        {HALF_W, -HALF_H, -DEPTH, 1, 0, 0, 0.0f, 1.0f},
        {HALF_W, -HALF_H,  DEPTH, 1, 0, 0, 1.0f, 1.0f},
        {HALF_W,  SIDE_Y, -DEPTH, 1, 0, 0, 0.0f, 0.0f},
        {HALF_W,  SIDE_Y,  DEPTH, 1, 0, 0, 1.0f, 0.0f});

    // Bottom floor
    push_quad(m,
        {-HALF_W, -HALF_H, -DEPTH, 0, -1, 0, 0.0f, 0.0f},
        { HALF_W, -HALF_H, -DEPTH, 0, -1, 0, 1.0f, 0.0f},
        {-HALF_W, -HALF_H,  DEPTH, 0, -1, 0, 0.0f, 1.0f},
        { HALF_W, -HALF_H,  DEPTH, 0, -1, 0, 1.0f, 1.0f});

    surf_out.push_back(std::move(m));

    // ── Exact Beveled Platform Collision Contour ──
    outline_out.clear();
    outline_out.push_back({-HALF_W, -HALF_H});
    outline_out.push_back({-HALF_W, SIDE_Y});
    const int N_BEV_POLY = 8;
    for (int k = 0; k <= N_BEV_POLY; ++k) {
        float a = PI - (PI / 2.0f * k / N_BEV_POLY);
        outline_out.push_back({-IW + BEV_R * std::cos(a), SIDE_Y + BEV_R * std::sin(a)});
    }
    outline_out.push_back({IW, HALF_H});
    for (int k = 0; k <= N_BEV_POLY; ++k) {
        float a = PI / 2.0f * (1.0f - static_cast<float>(k) / N_BEV_POLY);
        outline_out.push_back({IW + BEV_R * std::cos(a), SIDE_Y + BEV_R * std::sin(a)});
    }
    outline_out.push_back({HALF_W, -HALF_H});
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::string out_dir = "/home/quantumcreeper/.local/share/swordigo-desktop/assets/resources";
    if (argc > 1) out_dir = argv[1];

    std::cout << "====================================\n";
    std::cout << "  Zypher Showcase Generator v4\n";
    std::cout << "  (Contour-Accurate Solid Collision)\n";
    std::cout << "  Output: " << out_dir << "\n";
    std::cout << "====================================\n\n";

    fs::create_directories(out_dir);

    const std::string TEX = "graveyard_ground";

    struct ShapeDesc {
        const char* name;
        float bnd_x, bnd_y;
        float depth_z;
        int   base_cid;
        void (*gen)(std::vector<Mesh>&, std::vector<Mesh>&, std::vector<std::pair<float, float>>&, const std::string&);
    };

    ShapeDesc shapes[] = {
        {"zypher_terrain",        680, 260,  90, 2000, gen_terrain},
        {"zypher_pillar",         130, 240,  90, 3000, gen_pillar},
        {"zypher_dome",           280, 290, 240, 4000, gen_dome},
        {"zypher_arch",           380, 520,  90, 5000, gen_arch},
        {"zypher_torus",          270, 270,  60, 6000, gen_torus},
        {"zypher_bevel_platform", 400, 100,  95, 7000, gen_bevel_platform},
    };

    int ok = 0, fail = 0;
    for (const auto& s : shapes) {
        std::cout << "Generating " << s.name << " ...\n";

        std::vector<Mesh> surf, front;
        std::vector<std::pair<float, float>> outline;
        s.gen(surf, front, outline, TEX);

        size_t sv = 0, si = 0, fv = 0, fi = 0;
        for (auto& m : surf)  { sv += m.verts.size(); si += m.indices.size(); }
        for (auto& m : front) { fv += m.verts.size(); fi += m.indices.size(); }
        std::cout << "  surface: " << sv << " verts, " << si / 3 << " faces\n";
        std::cout << "  physics contour vertices: " << outline.size() << "\n";

        std::string blob = build_scene_object(s.name, surf, front, outline,
                                              s.depth_z, s.bnd_x, s.bnd_y,
                                              0.0f, 0.0f, s.base_cid);

        std::string path = out_dir + "/" + s.name + ".scene";
        std::ofstream f(path, std::ios::binary);
        if (!f) {
            std::cerr << "  ERROR: cannot write " << path << "\n";
            ++fail;
            continue;
        }
        f.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        f.close();
        std::cout << "  Written " << blob.size() << " bytes -> " << path << "\n\n";
        ++ok;
    }

    std::cout << "====================================\n";
    std::cout << "  Done: " << ok << " ok, " << fail << " failed\n";
    std::cout << "====================================\n";
    return fail > 0 ? 1 : 0;
}
