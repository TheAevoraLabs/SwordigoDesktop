#include <iostream>
#include <cassert>
#include <cmath>
#include "tools/rubymesh_meta.h"

int main() {
    std::cout << "[rubymesh_meta_test] Testing RBM1 Tag 15 Serialization...\n";

    // 1. Create an L-shaped polygon with Zypher Curvy hat and Bubbly top
    rubymeta::RubyMeshMeta meta;
    meta.generator = rubymeta::GenZypher;
    meta.editor_source = rubymeta::SourceRubyTouch;
    meta.hat_style = "curvy";
    meta.top_style = "bubbly";
    meta.top_frequency = 2.5f;
    meta.top_depth = 12.0f;
    meta.top_min_collision = 4.0f;
    meta.random_seed = 987654321u;
    meta.horiz_noise = 0.5f;
    meta.mesh_type = 1;
    meta.texture_scale = 300.0f;

    // L-shaped baseline outline: 6 points
    meta.baseline_points = {
        {0.0, 0.0},
        {150.0, 0.0},
        {150.0, -100.0},
        {100.0, -100.0},
        {100.0, -50.0},
        {0.0, -50.0}
    };

    // 2. Pack
    std::string packed = rubymeta::pack_rubymesh_meta(meta);
    std::cout << "Packed L-shaped mesh meta size: " << packed.size() << " bytes\n";
    assert(packed.size() > sizeof(rubymeta::RBM1Header));

    // 3. Unpack
    rubymeta::RubyMeshMeta unpacked;
    bool ok = rubymeta::unpack_rubymesh_meta(packed, unpacked);
    assert(ok);
    assert(unpacked.generator == rubymeta::GenZypher);
    assert(unpacked.editor_source == rubymeta::SourceRubyTouch);
    assert(unpacked.hat_style == "curvy");
    assert(unpacked.top_style == "bubbly");
    assert(std::fabs(unpacked.top_frequency - 2.5f) < 1e-4);
    assert(std::fabs(unpacked.top_depth - 12.0f) < 1e-4);
    assert(std::fabs(unpacked.top_min_collision - 4.0f) < 1e-4);
    assert(unpacked.random_seed == 987654321u);
    assert(std::fabs(unpacked.horiz_noise - 0.5f) < 1e-4);
    assert(unpacked.mesh_type == 1);
    assert(std::fabs(unpacked.texture_scale - 300.0f) < 1e-4);
    assert(unpacked.baseline_points.size() == 6);

    for (size_t i = 0; i < 6; ++i) {
        assert(std::fabs(unpacked.baseline_points[i].x - meta.baseline_points[i].x) < 1e-4);
        assert(std::fabs(unpacked.baseline_points[i].y - meta.baseline_points[i].y) < 1e-4);
    }
    std::cout << "L-shaped polygon coordinates verified verbatim!\n";

    // 4. Test Protobuf Component wrapper injection and extraction
    proto::Writer gpc;
    gpc.write_varint_field(3, 1);       // Collides = true
    gpc.write_float_field(4, -45.0f);   // MinDepth
    gpc.write_float_field(5, 45.0f);    // MaxDepth

    proto::Writer comp;
    comp.write_string_field(1, "GroundPolygon");
    comp.write_varint_field(2, 42);     // Id = 42
    comp.write_nested_field(110, gpc);

    std::string comp_raw = comp.to_string();

    // Inject Tag 15
    std::string injected = rubymeta::inject_rubymesh_meta(comp_raw, packed);
    assert(injected.size() > comp_raw.size());

    // Extract Tag 15 from component
    rubymeta::RubyMeshMeta extracted;
    bool ext_ok = rubymeta::extract_rubymesh_meta(injected, extracted);
    assert(ext_ok);
    assert(extracted.baseline_points.size() == 6);
    assert(extracted.hat_style == "curvy");
    assert(extracted.top_style == "bubbly");
    std::cout << "Component injection and extraction passed!\n";

    std::cout << "[rubymesh_meta_test] All tests PASSED successfully!\n";
    return 0;
}
