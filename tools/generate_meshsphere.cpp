/* tools/generate_meshsphere.cpp
 *
 * Generates a spherical ground mesh object named 'meshsphere' using
 * BoulderX / Zenith infrastructure, injects it into a vanilla .scene file,
 * and saves both binary and FileRift markup scenes.
 *
 * Does NOT touch or modify boulderx.cpp!
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <cstring>

#include "tools/boulderx.h"
#include "tools/scene_loader.h"
#include "tools/filerift.h"
#include "platform/protobuf_reader.h"

namespace fs = std::filesystem;

std::string g_instance_assets_dir = "assets";

// Helper: build a true 3D UV sphere SurfaceMesh
static boulderx::Submesh build_uv_sphere_submesh(double radius, int rings, int sectors,
                                                  const std::string& texture, double texture_scale) {
    boulderx::Submesh sm;
    sm.kind = "SurfaceMesh";
    sm.texture = texture;
    sm.indexed = true;

    for (int i = 0; i <= rings; ++i) {
        const double phi = -M_PI / 2.0 + M_PI * static_cast<double>(i) / rings;
        const double y = radius * std::sin(phi);
        const double r = radius * std::cos(phi);
        const double v = static_cast<double>(i) / rings;

        for (int j = 0; j <= sectors; ++j) {
            const double theta = 2.0 * M_PI * static_cast<double>(j) / sectors;
            const double x = r * std::cos(theta);
            const double z = r * std::sin(theta);
            const double u = static_cast<double>(j) / sectors;

            boulderx::Vertex vert;
            vert.x = static_cast<float>(x);
            vert.y = static_cast<float>(y);
            vert.z = static_cast<float>(z);
            // Outward normal on unit sphere
            vert.nx = static_cast<float>(x / radius);
            vert.ny = static_cast<float>(y / radius);
            vert.nz = static_cast<float>(z / radius);
            vert.u = static_cast<float>(u * (2.0 * M_PI * radius / texture_scale));
            vert.v = static_cast<float>(v * (M_PI * radius / texture_scale));
            sm.vertices.push_back(vert);
        }
    }

    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sectors; ++j) {
            uint16_t a = static_cast<uint16_t>(i * (sectors + 1) + j);
            uint16_t b = static_cast<uint16_t>(i * (sectors + 1) + (j + 1));
            uint16_t c = static_cast<uint16_t>((i + 1) * (sectors + 1) + j);
            uint16_t d = static_cast<uint16_t>((i + 1) * (sectors + 1) + (j + 1));

            // CCW triangles facing outward
            sm.indices.push_back(a);
            sm.indices.push_back(c);
            sm.indices.push_back(b);

            sm.indices.push_back(b);
            sm.indices.push_back(c);
            sm.indices.push_back(d);
        }
    }

    return sm;
}

// Helpers for vertex buffer packing
static void append_float(std::vector<uint8_t>& out, double v) {
    float f = static_cast<float>(v);
    uint8_t b[4];
    std::memcpy(b, &f, 4);
    out.insert(out.end(), b, b + 4);
}

static void append_ushort(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

static void vertex_bits_and_bounds(const boulderx::Submesh& sm, std::vector<uint8_t>& bits,
                                   float* mn, float* mx) {
    mn[0] = mn[1] = mn[2] = 1e9f;
    mx[0] = mx[1] = mx[2] = -1e9f;
    for (const auto& v : sm.vertices) {
        const float p[8] = {v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v};
        for (float f : p) append_float(bits, f);
        for (int c = 0; c < 3; ++c) {
            if (p[c] < mn[c]) mn[c] = p[c];
            if (p[c] > mx[c]) mx[c] = p[c];
        }
    }
    if (sm.vertices.empty()) { mn[0] = mn[1] = mn[2] = 0.0f; mx[0] = mx[1] = mx[2] = 0.0f; }
}

static proto::Writer make_float_color(float r, float g, float b, float a) {
    proto::Writer w;
    w.write_float_field(1, r);
    w.write_float_field(2, g);
    w.write_float_field(3, b);
    w.write_float_field(4, a);
    return w;
}

static proto::Writer make_mesh_data(int value_type, int values_per_vertex, int stride, int data_offset) {
    proto::Writer w;
    w.write_varint_field(1, static_cast<uint64_t>(value_type));
    w.write_varint_field(2, static_cast<uint64_t>(values_per_vertex));
    w.write_varint_field(3, static_cast<uint64_t>(stride));
    w.write_varint_field(4, static_cast<uint64_t>(data_offset));
    return w;
}

static proto::Writer make_box(float x, float y, float z, float width, float height, float depth) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    w.write_float_field(3, z);
    w.write_float_field(4, width);
    w.write_float_field(5, height);
    w.write_float_field(6, depth);
    return w;
}

static proto::Writer make_mesh_material(const std::string& texture_name) {
    proto::Writer tex;
    tex.write_string_field(1, texture_name);
    tex.write_varint_field(2, 1);
    tex.write_varint_field(4, 1);
    proto::Writer mat;
    mat.write_nested_field(1, make_float_color(1, 1, 1, 1));
    mat.write_nested_field(2, make_float_color(1, 1, 1, 1));
    mat.write_nested_field(3, make_float_color(1, 1, 1, 1));
    mat.write_float_field(4, 0.0f);
    mat.write_nested_field(5, tex);
    return mat;
}

static proto::Writer make_mesh(const boulderx::Submesh& sm) {
    proto::Writer w;
    std::vector<uint8_t> vbits, ibits;
    float mn[3], mx[3];
    vertex_bits_and_bounds(sm, vbits, mn, mx);
    for (uint16_t idx : sm.indices) append_ushort(ibits, idx);

    const size_t num_vertices = sm.vertices.size();
    const size_t num_faces = sm.indexed ? sm.indices.size() / 3 : sm.vertices.size() / 3;
    w.write_varint_field(1, num_vertices);
    w.write_varint_field(2, num_faces);
    if (sm.indexed && !ibits.empty())
        w.write_nested_field(3, make_mesh_data(4, 1, 2, 0));
    w.write_nested_field(4, make_mesh_data(7, 3, 32, 0));
    w.write_nested_field(5, make_mesh_data(7, 3, 32, 12));
    w.write_nested_field(6, make_mesh_data(7, 2, 32, 24));
    w.write_nested_field(10, make_mesh_material(sm.texture));
    w.write_nested_field(11, make_box(mn[0], mn[1], mn[2],
                                      mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]));
    w.write_bytes_field(50, std::string(vbits.begin(), vbits.end()));
    if (sm.indexed && !ibits.empty())
        w.write_bytes_field(51, std::string(ibits.begin(), ibits.end()));
    return w;
}

static proto::Writer make_vector2(double x, double y) {
    proto::Writer w;
    w.write_float_field(1, static_cast<float>(x));
    w.write_float_field(2, static_cast<float>(y));
    return w;
}

static proto::Writer make_rectangle(double x, double y, double width, double height) {
    proto::Writer w;
    w.write_float_field(1, static_cast<float>(x));
    w.write_float_field(2, static_cast<float>(y));
    w.write_float_field(3, static_cast<float>(width));
    w.write_float_field(4, static_cast<float>(height));
    return w;
}

// Generate a true 3D UV Sphere scene object wrapped in Scene protobuf (Tag 1)
static std::string generate_uv_sphere_scene_object(
    double radius, int rings, int sectors,
    const std::string& texture, double texture_scale,
    const std::string& identifier, const boulderx::ComponentIds& cid) {

    // 1. Equatorial circular outline for collision
    const int N = std::max(48, sectors);
    proto::Writer poly;
    for (int i = 0; i < N; ++i) {
        double theta = 2.0 * M_PI * i / N;
        poly.write_nested_field(1, make_vector2(radius * std::cos(theta), radius * std::sin(theta)));
    }
    poly.write_varint_field(2, 0); // Convex: 0
    poly.write_varint_field(3, 1); // Closed: 1

    proto::Writer gpc;
    gpc.write_nested_field(2, poly);
    gpc.write_varint_field(3, 1); // Collides
    gpc.write_float_field(4, static_cast<float>(-radius));
    gpc.write_float_field(5, static_cast<float>(radius));

    proto::Writer comp_poly;
    comp_poly.write_string_field(1, "GroundPolygon");
    comp_poly.write_varint_field(2, static_cast<uint64_t>(cid.polygon_id));
    comp_poly.write_nested_field(110, gpc);

    // 2. 3D UV Sphere Mesh
    boulderx::Submesh sphere_sm = build_uv_sphere_submesh(radius, rings, sectors, texture, texture_scale);
    proto::Writer gmc;
    gmc.write_nested_field(7, make_rectangle(-radius, -radius, 2 * radius, 2 * radius));
    gmc.write_nested_field(8, make_mesh(sphere_sm)); // SurfaceMesh
    gmc.write_nested_field(10, make_float_color(1, 1, 1, 1));

    proto::Writer comp_mesh;
    comp_mesh.write_string_field(1, "GroundMesh");
    comp_mesh.write_varint_field(2, static_cast<uint64_t>(cid.mesh_id));
    comp_mesh.write_nested_field(111, gmc);

    // 3. GroundMeshGenerator
    proto::Writer ggc;
    ggc.write_varint_field(1, static_cast<uint64_t>(cid.polygon_id));
    ggc.write_varint_field(2, static_cast<uint64_t>(cid.mesh_id));
    ggc.write_varint_field(3, static_cast<uint64_t>(cid.tm_front_id));
    ggc.write_varint_field(4, static_cast<uint64_t>(cid.tm_surface_id));
    ggc.write_varint_field(5, 1291618994ULL);
    ggc.write_float_field(6, 0.0f);
    ggc.write_varint_field(7, 1); // MeshType 1
    ggc.write_float_field(8, static_cast<float>(2 * radius));
    ggc.write_float_field(9, static_cast<float>(radius));
    ggc.write_float_field(10, static_cast<float>(radius * 0.5));
    ggc.write_float_field(11, static_cast<float>(radius * 0.5));

    proto::Writer comp_gen;
    comp_gen.write_string_field(1, "GroundMeshGenerator");
    comp_gen.write_varint_field(2, static_cast<uint64_t>(cid.generator_id));
    comp_gen.write_nested_field(112, ggc);

    // 4. CollisionShape
    proto::Writer shape;
    shape.write_nested_field(3, poly);
    proto::Writer csc;
    csc.write_varint_field(2, 1); // IsGround
    csc.write_float_field(6, static_cast<float>(-radius));
    csc.write_float_field(7, static_cast<float>(radius));
    csc.write_varint_field(11, 1);

    proto::Writer comp_col;
    comp_col.write_string_field(1, "CollisionShape");
    comp_col.write_varint_field(2, static_cast<uint64_t>(cid.collision_id));
    comp_col.write_varint_field(4, static_cast<uint64_t>(cid.polygon_id));
    comp_col.write_nested_field(120, shape);
    comp_col.write_nested_field(121, csc);

    // 5. TextureMappings
    auto make_tm = [&](const std::string& tex, int id) {
        proto::Writer tm;
        tm.write_string_field(1, tex);
        tm.write_float_field(2, static_cast<float>(texture_scale));
        tm.write_nested_field(3, make_vector2(0, 0));
        proto::Writer comp;
        comp.write_string_field(1, "TextureMapping");
        comp.write_varint_field(2, static_cast<uint64_t>(id));
        comp.write_nested_field(113, tm);
        return comp;
    };
    proto::Writer comp_tm_surface = make_tm(texture, cid.tm_surface_id);
    proto::Writer comp_tm_front = make_tm(texture, cid.tm_front_id);

    // 6. SceneObject
    proto::Writer obj;
    obj.write_string_field(1, "SceneObject");
    obj.write_string_field(2, identifier);
    obj.write_bytes_field(3, comp_poly.to_string());
    obj.write_bytes_field(3, comp_mesh.to_string());
    obj.write_bytes_field(3, comp_gen.to_string());
    obj.write_bytes_field(3, comp_col.to_string());
    obj.write_bytes_field(3, comp_tm_surface.to_string());
    obj.write_bytes_field(3, comp_tm_front.to_string());
    obj.write_nested_field(4, make_vector2(0, 0));
    obj.write_float_field(5, 0.0f);
    obj.write_float_field(6, 0.0f);
    obj.write_float_field(7, 1.0f);
    obj.write_nested_field(8, make_rectangle(-radius, -radius, 2 * radius, 2 * radius));
    obj.write_varint_field(9, 0);

    proto::Writer scene;
    scene.write_bytes_field(1, obj.to_string());
    return scene.to_string();
}

int main(int argc, char** argv) {
    std::cout << "========================================\n";
    std::cout << "  Zenith / BoulderX Sphere Builder\n";
    std::cout << "========================================\n";

    std::string scene_path = "Scener/data/decoded_scenes/beyond_graveyard.scene";
    std::string out_bin_path = "beyond_graveyard_sphere.scene";
    std::string out_fr_path = "beyond_graveyard_sphere.fr";
    double radius = 40.0;
    double pos_x = -420.0; // Near player spawn_default (-495, -377)
    double pos_y = -350.0;
    double pos_z = 0.0;
    std::string mode = "zenith"; // "zenith" or "uvsphere" or "both"
    std::string texture = "graveyard_ground";
    int rings = 32;
    int sectors = 48;
    double texture_scale = 200.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--scene" && i + 1 < argc) scene_path = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_bin_path = argv[++i];
        else if (arg == "--out-fr" && i + 1 < argc) out_fr_path = argv[++i];
        else if (arg == "--radius" && i + 1 < argc) radius = std::stod(argv[++i]);
        else if (arg == "--pos-x" && i + 1 < argc) pos_x = std::stod(argv[++i]);
        else if (arg == "--pos-y" && i + 1 < argc) pos_y = std::stod(argv[++i]);
        else if (arg == "--pos-z" && i + 1 < argc) pos_z = std::stod(argv[++i]);
        else if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--texture" && i + 1 < argc) texture = argv[++i];
        else if (arg == "--rings" && i + 1 < argc) rings = std::stoi(argv[++i]);
        else if (arg == "--sectors" && i + 1 < argc) sectors = std::stoi(argv[++i]);
        else if (arg == "--texture-scale" && i + 1 < argc) texture_scale = std::stod(argv[++i]);
    }

    std::cout << "Loading base scene: " << scene_path << "\n";
    std::ifstream in_file(scene_path, std::ios::binary);
    if (!in_file) {
        std::cerr << "Error: cannot open scene: " << scene_path << "\n";
        return 1;
    }
    std::vector<uint8_t> scene_bytes((std::istreambuf_iterator<char>(in_file)),
                                     std::istreambuf_iterator<char>());
    in_file.close();

    av::SceneData scene = av::scene_load_bytes(scene_bytes, scene_path);
    std::cout << "Base scene loaded successfully: " << scene.objects.size() << " objects.\n";

    // Allocate safe component IDs (above highest ID in scene)
    int max_id = 1000;
    for (const auto& obj : scene.objects) {
        for (const auto& comp : obj.components) {
            max_id = std::max(max_id, comp.type_id);
        }
    }
    boulderx::ComponentIds cid;
    cid.polygon_id    = max_id + 10;
    cid.mesh_id       = max_id + 11;
    cid.generator_id  = max_id + 12;
    cid.collision_id  = max_id + 13;
    cid.tm_surface_id = max_id + 14;
    cid.tm_front_id   = max_id + 15;

    std::string sphere_bytes;

    if (mode == "uvsphere") {
        std::cout << "Generating true 3D UV-Sphere using Zenith protobuf infrastructure (radius="
                  << radius << ", rings=" << rings << ", sectors=" << sectors << ")...\n";
        sphere_bytes = generate_uv_sphere_scene_object(radius, rings, sectors, texture, texture_scale, "meshsphere", cid);
    } else {
        std::cout << "Generating sphere ground mesh using BoulderX / Zenith generator pipeline...\n";
        const int N = 48; // 48-node CCW circular outline
        std::vector<boulderx::Node> outline;
        outline.reserve(N);
        for (int i = 0; i < N; ++i) {
            double theta = 2.0 * M_PI * static_cast<double>(i) / N;
            boulderx::Node n;
            n.x = radius * std::cos(theta);
            n.y = radius * std::sin(theta);
            n.front_depth = 1.0;
            n.back_depth = -1.0;
            outline.push_back(n);
        }

        boulderx::Params params;
        params.mesh_type = 1; // Rounded rim profile (InsertRoundHatVertices)
        params.surface_width = 2.0;
        params.hat_height = radius * 0.98; // Inward sweep from perimeter to apex
        params.hat_width_offset_1 = radius * 0.85; // Outward spherical dome curvature (+Z)
        params.hat_width_offset_2 = radius * 0.85;
        params.horiz_noise = 0.0;
        params.depth_source = boulderx::Params::DepthSource::Outline;
        params.surface_texture = texture;
        params.front_texture = texture;
        params.texture_scale = 100.0;

        sphere_bytes = boulderx::generate_ground_mesh_object(outline, params, "meshsphere", &cid);
        if (sphere_bytes.empty()) {
            std::cerr << "BoulderX generation failed: " << boulderx::last_decline_reason() << "\n";
            return 1;
        }
    }

    // Parse the generated object and insert it into the scene
    std::vector<uint8_t> obj_buf(sphere_bytes.begin(), sphere_bytes.end());
    av::SceneData temp_scene = av::scene_load_bytes(obj_buf, "temp_obj");
    if (temp_scene.objects.empty()) {
        std::cerr << "Failed to parse generated sphere scene object!\n";
        return 1;
    }

    av::SceneObject sphere_obj = temp_scene.objects[0];
    sphere_obj.name = "meshsphere";
    sphere_obj.pos_x = static_cast<float>(pos_x);
    sphere_obj.pos_y = static_cast<float>(pos_y);
    sphere_obj.pos_z = static_cast<float>(pos_z);

    std::cout << "Sphere object created: name='" << sphere_obj.name << "'"
              << " at (" << sphere_obj.pos_x << ", " << sphere_obj.pos_y << ", " << sphere_obj.pos_z << ")\n";
    std::cout << "Components in meshsphere: " << sphere_obj.components.size() << "\n";

    // Remove any existing object with the same name if present
    scene.objects.erase(
        std::remove_if(scene.objects.begin(), scene.objects.end(),
                       [](const av::SceneObject& o) { return o.name == "meshsphere"; }),
        scene.objects.end());

    scene.objects.push_back(sphere_obj);
    std::cout << "Total objects in output scene: " << scene.objects.size() << "\n";

    // Save binary scene
    std::string err;
    if (!av::scene_save(out_bin_path, scene, &err)) {
        std::cerr << "Failed to save output scene: " << err << "\n";
        return 1;
    }
    std::cout << "Successfully saved binary scene to: " << out_bin_path << "\n";

    // Recode / transcode to FileRift markup
    try {
        std::ifstream out_bin_file(out_bin_path, std::ios::binary);
        std::string bin_data((std::istreambuf_iterator<char>(out_bin_file)),
                             std::istreambuf_iterator<char>());
        out_bin_file.close();

        std::string markup = filerift::decode_protobuf(bin_data, "scene");
        std::ofstream fr_file(out_fr_path);
        fr_file << markup;
        fr_file.close();
        std::cout << "Successfully saved FileRift markup to: " << out_fr_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Warning: FileRift decode failed: " << e.what() << "\n";
    }

    std::cout << "Done! meshsphere successfully placed.\n";
    return 0;
}
