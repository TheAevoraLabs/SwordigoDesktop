/* tools/model_to_mesh.cpp — 3D Model (.POD) to GroundMesh Converter (C++)
 *
 * Converts 3D models into native Swordigo GroundMesh objects with:
 *   - Multi-material submeshes with texture references
 *   - 32-byte vertex streams (position xyz, normal xyz, uv)
 *   - Two-sided indexed triangles
 *   - Guaranteed Counter-Clockwise (CCW) 2D convex hull collision contour
 *   - Full Chipmunk 2D physics integration
 *
 * Build:
 *   g++ -std=c++17 -O2 -I src -I src/tools tools/model_to_mesh.cpp src/tools/pod_loader.cpp -lm -o bin/model_to_mesh
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstring>
#include <set>

#include "tools/pod_loader.h"
#include "platform/protobuf_reader.h"

namespace fs = std::filesystem;

// ── Raw byte helpers ─────────────────────────────────────────────────────────

static void push_f32(std::vector<uint8_t>& out, float v) {
    uint8_t b[4];
    std::memcpy(b, &v, 4);
    out.insert(out.end(), b, b + 4);
}

static void push_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

// ── Protobuf helpers ─────────────────────────────────────────────────────────

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

static proto::Writer pw_box(float x, float y, float z, float ww, float hh, float d) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, z);
    w.write_float_field(4, ww);
    w.write_float_field(5, hh);
    w.write_float_field(6, d);
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

struct SubmeshData {
    std::vector<float> verts; // 8 floats per vert: pos(3), norm(3), uv(2)
    std::vector<uint16_t> indices;
    std::string texture;
    float min_x = 1e9f, min_y = 1e9f, min_z = 1e9f;
    float max_x = -1e9f, max_y = -1e9f, max_z = -1e9f;
};

static proto::Writer encode_submesh(const SubmeshData& sm, bool two_sided = true) {
    std::vector<uint8_t> vbuf, ibuf;
    for (float f : sm.verts) push_f32(vbuf, f);

    std::vector<uint16_t> all_indices = sm.indices;
    if (two_sided) {
        for (size_t i = 0; i + 2 < sm.indices.size(); i += 3) {
            all_indices.push_back(sm.indices[i]);
            all_indices.push_back(sm.indices[i+2]);
            all_indices.push_back(sm.indices[i+1]);
        }
    }
    for (uint16_t idx : all_indices) push_u16(ibuf, idx);

    size_t nv = sm.verts.size() / 8;
    size_t nf = all_indices.size() / 3;

    proto::Writer w;
    w.write_varint_field(1, nv);
    w.write_varint_field(2, nf);
    if (!all_indices.empty()) w.write_nested_field(3, pw_mesh_attr(4, 1, 2, 0));
    w.write_nested_field(4, pw_mesh_attr(7, 3, 32, 0));
    w.write_nested_field(5, pw_mesh_attr(7, 3, 32, 12));
    w.write_nested_field(6, pw_mesh_attr(7, 2, 32, 24));
    w.write_string_field(7, sm.texture);
    w.write_nested_field(10, pw_material(sm.texture));
    w.write_nested_field(11, pw_box(sm.min_x, sm.min_y, sm.min_z,
                                    sm.max_x - sm.min_x, sm.max_y - sm.min_y, sm.max_z - sm.min_z));
    w.write_bytes_field(50, std::string(vbuf.begin(), vbuf.end()));
    if (!all_indices.empty())
        w.write_bytes_field(51, std::string(ibuf.begin(), ibuf.end()));
    return w;
}

// ── 2D Convex Hull (Monotone Chain) ─────────────────────────────────────────

struct Point2D {
    float x, y;
    bool operator<(const Point2D& o) const {
        return x < o.x || (x == o.x && y < o.y);
    }
    bool operator==(const Point2D& o) const {
        return std::abs(x - o.x) < 1e-4f && std::abs(y - o.y) < 1e-4f;
    }
};

static float cross(const Point2D& o, const Point2D& a, const Point2D& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static std::vector<Point2D> compute_ccw_convex_hull(std::vector<Point2D> pts) {
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() <= 2) return pts;

    std::vector<Point2D> lower;
    for (const auto& p : pts) {
        while (lower.size() >= 2 && cross(lower[lower.size()-2], lower.back(), p) <= 0.0f)
            lower.pop_back();
        lower.push_back(p);
    }

    std::vector<Point2D> upper;
    for (int i = static_cast<int>(pts.size()) - 1; i >= 0; --i) {
        while (upper.size() >= 2 && cross(upper[upper.size()-2], upper.back(), pts[i]) <= 0.0f)
            upper.pop_back();
        upper.push_back(pts[i]);
    }

    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());

    // Verify CCW
    float area = 0.0f;
    for (size_t i = 0; i < lower.size(); ++i) {
        size_t next = (i + 1) % lower.size();
        area += (lower[i].x * lower[next].y - lower[next].x * lower[i].y);
    }
    if (area < 0.0f) std::reverse(lower.begin(), lower.end());
    return lower;
}

// ── Main Conversion Logic ───────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: model_to_mesh <model.POD> [--out <out.scene>] [--name <id>] [--pos <x> <y>] [--depth <z>]\n";
        return 1;
    }

    std::string pod_path = argv[1];
    std::string out_scene = "";
    std::string obj_name = fs::path(pod_path).stem().string();
    float pos_x = 0.0f, pos_y = 0.0f, depth_z = 0.0f, scale = 1.0f;
    int base_cid = 9000;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) out_scene = argv[++i];
        else if (arg == "--name" && i + 1 < argc) obj_name = argv[++i];
        else if (arg == "--pos" && i + 2 < argc) { pos_x = std::stof(argv[++i]); pos_y = std::stof(argv[++i]); }
        else if (arg == "--depth" && i + 1 < argc) depth_z = std::stof(argv[++i]);
        else if (arg == "--scale" && i + 1 < argc) scale = std::stof(argv[++i]);
        else if (arg == "--base-cid" && i + 1 < argc) base_cid = std::stoi(argv[++i]);
    }

    av::PODModel model = av::pod_load(pod_path);
    if (model.meshes.empty()) {
        std::cerr << "Error: failed to load or empty POD model: " << pod_path << "\n";
        return 1;
    }

    std::vector<SubmeshData> submeshes;
    std::vector<Point2D> all_pts;
    std::vector<std::string> unique_textures;
    float global_min_z = 1e9f, global_max_z = -1e9f;
    float global_min_x = 1e9f, global_max_x = -1e9f;
    float global_min_y = 1e9f, global_max_y = -1e9f;

    for (int ni = 0; ni < model.num_mesh_nodes; ++ni) {
        const auto& node = model.nodes[ni];
        if (node.object_index < 0 || node.object_index >= static_cast<int>(model.meshes.size())) continue;
        const auto& mesh = model.meshes[node.object_index];

        std::string tex = "graveyard_ground";
        if (node.material_index >= 0 && node.material_index < static_cast<int>(model.materials.size())) {
            int ti = model.materials[node.material_index].diffuse_texture_index;
            if (ti >= 0 && ti < static_cast<int>(model.texture_filenames.size())) {
                tex = fs::path(model.texture_filenames[ti]).stem().string();
            }
        }
        if (std::find(unique_textures.begin(), unique_textures.end(), tex) == unique_textures.end()) {
            unique_textures.push_back(tex);
        }

        SubmeshData sm;
        sm.texture = tex;
        sm.indices.assign(mesh.indices.begin(), mesh.indices.end());

        for (int i = 0; i < mesh.num_vertices; ++i) {
            float x = mesh.positions[i * 3 + 0] * scale;
            float y = mesh.positions[i * 3 + 1] * scale;
            float z = mesh.positions[i * 3 + 2] * scale;
            float nx = (mesh.normals.size() > i * 3 + 2) ? mesh.normals[i * 3 + 0] : 0.0f;
            float ny = (mesh.normals.size() > i * 3 + 2) ? mesh.normals[i * 3 + 1] : 1.0f;
            float nz = (mesh.normals.size() > i * 3 + 2) ? mesh.normals[i * 3 + 2] : 0.0f;
            float u = (mesh.uvs.size() > i * 2 + 1) ? mesh.uvs[i * 2 + 0] : 0.0f;
            float v = (mesh.uvs.size() > i * 2 + 1) ? mesh.uvs[i * 2 + 1] : 0.0f;

            sm.verts.insert(sm.verts.end(), {x, y, z, nx, ny, nz, u, v});
            sm.min_x = std::min(sm.min_x, x); sm.max_x = std::max(sm.max_x, x);
            sm.min_y = std::min(sm.min_y, y); sm.max_y = std::max(sm.max_y, y);
            sm.min_z = std::min(sm.min_z, z); sm.max_z = std::max(sm.max_z, z);

            all_pts.push_back({x, y});
            global_min_x = std::min(global_min_x, x); global_max_x = std::max(global_max_x, x);
            global_min_y = std::min(global_min_y, y); global_max_y = std::max(global_max_y, y);
            global_min_z = std::min(global_min_z, z); global_max_z = std::max(global_max_z, z);
        }
        submeshes.push_back(std::move(sm));
    }

    std::vector<Point2D> hull = compute_ccw_convex_hull(all_pts);
    float depth_extent = std::max({std::abs(global_min_z), std::abs(global_max_z), 30.0f}) + 15.0f;

    // GroundPolygon
    proto::Writer poly;
    for (const auto& p : hull) poly.write_nested_field(1, pw_vec2(p.x, p.y));
    poly.write_varint_field(2, 0); // Convex: 0
    poly.write_varint_field(3, 1); // Closed: 1

    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1);
    gpc.write_float_field(4, -depth_extent);
    gpc.write_float_field(5,  depth_extent);

    proto::Writer comp_poly;
    comp_poly.write_string_field(1, "GroundPolygon");
    comp_poly.write_varint_field(2, static_cast<uint64_t>(base_cid));
    comp_poly.write_nested_field(110, gpc);

    // GroundMeshComponent
    float bnd_w = (global_max_x - global_min_x) + 20.0f;
    float bnd_h = (global_max_y - global_min_y) + 20.0f;
    float center_x = (global_min_x + global_max_x) * 0.5f;
    float center_y = (global_min_y + global_max_y) * 0.5f;

    proto::Writer gmc;
    gmc.write_nested_field(7, pw_rect(center_x - bnd_w * 0.5f, center_y - bnd_h * 0.5f, bnd_w, bnd_h));
    for (const auto& sm : submeshes) gmc.write_nested_field(8, encode_submesh(sm));
    gmc.write_nested_field(10, pw_color(1, 1, 1, 1));

    proto::Writer comp_mesh;
    comp_mesh.write_string_field(1, "GroundMesh");
    comp_mesh.write_varint_field(2, static_cast<uint64_t>(base_cid + 1));
    comp_mesh.write_nested_field(111, gmc);

    // GroundMeshGenerator
    proto::Writer ggc;
    ggc.write_varint_field(1, static_cast<uint64_t>(base_cid));
    ggc.write_varint_field(2, static_cast<uint64_t>(base_cid + 1));
    ggc.write_varint_field(3, static_cast<uint64_t>(base_cid + 2));
    ggc.write_varint_field(4, static_cast<uint64_t>(base_cid + 3));
    ggc.write_varint_field(5, 1291618994ULL);
    ggc.write_float_field(6,  0.0f);
    ggc.write_varint_field(7,  1);
    ggc.write_float_field(8,  bnd_w);
    ggc.write_float_field(9,  bnd_h);
    ggc.write_float_field(10, bnd_w * 0.5f);
    ggc.write_float_field(11, bnd_h * 0.5f);

    proto::Writer comp_gen;
    comp_gen.write_string_field(1, "GroundMeshGenerator");
    comp_gen.write_varint_field(2, static_cast<uint64_t>(base_cid + 4));
    comp_gen.write_nested_field(112, ggc);

    // CollisionShape
    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1);
    csc.write_float_field(6, -depth_extent);
    csc.write_float_field(7,  depth_extent);
    csc.write_varint_field(11, 1);

    proto::Writer comp_col;
    comp_col.write_string_field(1, "CollisionShape");
    comp_col.write_varint_field(2, static_cast<uint64_t>(base_cid + 5));
    comp_col.write_varint_field(4, static_cast<uint64_t>(base_cid));
    comp_col.write_nested_field(120, shape);
    comp_col.write_nested_field(121, csc);

    // TextureMappings
    auto make_tm = [&](int cid, const std::string& tex) {
        proto::Writer tm;
        tm.write_string_field(1, tex);
        tm.write_float_field(2, 200.0f);
        tm.write_nested_field(3, pw_vec2(0.0f, 0.0f));
        proto::Writer c;
        c.write_string_field(1, "TextureMapping");
        c.write_varint_field(2, static_cast<uint64_t>(cid));
        c.write_nested_field(113, tm);
        return c;
    };

    // SceneObject
    proto::Writer obj;
    obj.write_string_field(1, "SceneObject");
    obj.write_string_field(2, obj_name);
    obj.write_bytes_field(3, comp_poly.to_string());
    obj.write_bytes_field(3, comp_mesh.to_string());
    obj.write_bytes_field(3, comp_gen.to_string());
    obj.write_bytes_field(3, comp_col.to_string());
    for (size_t i = 0; i < unique_textures.size(); ++i) {
        obj.write_bytes_field(3, make_tm(base_cid + 2 + static_cast<int>(i), unique_textures[i]).to_string());
    }
    obj.write_nested_field(4, pw_vec2(pos_x, pos_y));
    obj.write_float_field(5, depth_z);
    obj.write_float_field(6, 0.0f);
    obj.write_float_field(7, 1.0f);
    obj.write_nested_field(8, pw_rect(center_x - bnd_w * 0.5f, center_y - bnd_h * 0.5f, bnd_w, bnd_h));
    obj.write_varint_field(9, 0);

    if (!out_scene.empty()) {
        proto::Writer scene;
        scene.write_bytes_field(1, obj.to_string());
        std::ofstream f(out_scene, std::ios::binary);
        std::string s = scene.to_string();
        f.write(s.data(), s.size());
        std::cout << "[ModelToMesh] Wrote " << s.size() << " bytes -> " << out_scene << "\n";
    }

    std::cout << "[ModelToMesh] Success for " << obj_name << " (" << submeshes.size() << " submeshes)\n";
    return 0;
}
