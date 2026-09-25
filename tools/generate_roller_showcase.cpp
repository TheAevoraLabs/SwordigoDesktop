
// tools/generate_roller_showcase.cpp
// Builds Zypher Gen 3 showcase scenes using roller.h
// Output scenes placed alongside zypher_*.scene files
#include "tools/roller.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

static void write_scene(const std::string& path, const std::string& blob) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.write(blob.data(), blob.size());
    if (!f) throw std::runtime_error("write failed " + path);
    printf("  wrote %s (%zu bytes)\n", path.c_str(), blob.size());
}

int main() {
    const std::string OUT = "/home/quantumcreeper/.local/share/swordigo-desktop/assets/resources/";

    printf("Zypher Gen 3 scene export\n");

    // ── 1. Terrain ───────────────────────────────────────────────────────────
    printf("[1] ZypherTerrain — smooth mountain ridge\n");
    {
        std::vector<zypher::TerrainNode> nodes = {
            {-600, -200, 150, 0.2, 1.0},
            {-200, -100, 200, 0.1, 1.2},
            {  50,  -50, 250, 0.0, 0.8},
            { 350,  -80, 180, 0.1, 1.0},
            { 600, -200, 120, 0.2, 1.3},
            { 400, -350, 100, 0.0, 0.9},
            {   0, -400, 130, 0.1, 1.1},
            {-400, -350, 110, 0.0, 1.0},
        };
        zypher::TerrainParams p;
        p.bevel      = zypher::ProfileKind::SmoothFillet;
        p.bevel_steps = 14;
        p.spline_segs = 96;
        p.surface_width = 160.0;
        p.fbm_amplitude = 22.f;
        p.fbm_freq      = 0.009f;
        p.fbm_octaves   = 5;
        p.worley_blend  = 0.3f;
        p.surface_tex = "fire_grass";
        p.cliff_tex   = "graveyard_ground";
        zypher::ComponentIds ids{10000,10001,10002,10003,10004,10005};
        auto blob = zypher::generate_terrain_object(nodes, p, "roller_terrain_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_terrain.scene", blob);
    }

    // ── 2. Sedimentary cliff ─────────────────────────────────────────────────
    printf("[2] ZypherTerrain — sedimentary cliff ledges\n");
    {
        std::vector<zypher::TerrainNode> nodes = {
            {-500, -100, 200, 0.0, 1.0},
            { 500, -100, 200, 0.0, 1.0},
            { 500,  100, 200, 0.0, 1.0},
            {-500,  100, 200, 0.0, 1.0},
        };
        zypher::TerrainParams p;
        p.bevel      = zypher::ProfileKind::SedimentaryStep;
        p.bevel_steps = 18;
        p.spline_segs = 64;
        p.surface_width = 200.0;
        p.fbm_amplitude = 12.f;
        p.fbm_freq      = 0.015f;
        p.fbm_octaves   = 3;
        p.worley_blend  = 0.4f;
        p.surface_tex = "desert_rock";
        p.cliff_tex   = "desert_rock";
        zypher::ComponentIds ids{11000,11001,11002,11003,11004,11005};
        auto blob = zypher::generate_terrain_object(nodes, p, "roller_sediment_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_sediment.scene", blob);
    }

    // ── 3. Gothic arch ───────────────────────────────────────────────────────
    printf("[3] ZypherArch — gothic vaulted arch\n");
    {
        zypher::ArchParams p;
        p.span        = 500;
        p.height      = 420;
        p.thickness   = 80;
        p.depth       = 100;
        p.arch_segs   = 24;
        p.texture_scale = 220.f;
        p.tex_top  = "graveyard_ground";
        p.tex_face = "graveyard_ground";
        zypher::ComponentIds ids{12000,12001,12002,12003,12004,12005};
        auto blob = zypher::generate_arch_object(p, "roller_arch_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_arch.scene", blob);
    }

    // ── 4. Sphere ────────────────────────────────────────────────────────────
    printf("[4] ZypherSphere — UV sphere\n");
    {
        zypher::SphereParams p;
        p.radius = 250;
        p.rings  = 20;
        p.sectors = 32;
        p.texture = "cave_rock";
        zypher::ComponentIds ids{13000,13001,13002,13003,13004,13005};
        auto blob = zypher::generate_sphere_object(p, "roller_sphere_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_sphere.scene", blob);
    }

    // ── 5. Torus ─────────────────────────────────────────────────────────────
    printf("[5] ZypherTorus — torus ring\n");
    {
        zypher::TorusParams p;
        p.major_radius = 250;
        p.minor_radius = 70;
        p.major_segs   = 28;
        p.minor_segs   = 14;
        p.texture = "graveyard_ground";
        zypher::ComponentIds ids{14000,14001,14002,14003,14004,14005};
        auto blob = zypher::generate_torus_object(p, "roller_torus_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_torus.scene", blob);
    }

    // ── 6. Pillar ────────────────────────────────────────────────────────────
    printf("[6] ZypherPillar — beveled pillar\n");
    {
        zypher::PillarParams p;
        p.width   = 120;
        p.height  = 400;
        p.depth   = 120;
        p.bevel_n = 8;
        p.bevel_r = 15;
        p.texture = "cave_rock";
        zypher::ComponentIds ids{15000,15001,15002,15003,15004,15005};
        auto blob = zypher::generate_pillar_object(p, "roller_pillar_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_pillar.scene", blob);
    }

    // ── 7. Dome ──────────────────────────────────────────────────────────────
    printf("[7] ZypherDome — curved dome hemisphere\n");
    {
        zypher::DomeParams p;
        p.radius = 240;
        p.skirt_height = 40;
        p.rings = 20;
        p.sectors = 48;
        p.texture = "graveyard_ground";
        zypher::ComponentIds ids{16000,16001,16002,16003,16004,16005};
        auto blob = zypher::generate_dome_object(p, "roller_dome_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_dome.scene", blob);
    }

    // ── 8. Bevel platform ────────────────────────────────────────────────────
    printf("[8] ZypherBevelPlatform — beveled floating platform\n");
    {
        zypher::BevelPlatformParams p;
        p.half_width = 360;
        p.half_height = 45;
        p.bevel_radius = 30;
        p.depth = 95;
        p.bevel_segs = 12;
        p.texture = "graveyard_ground";
        zypher::ComponentIds ids{17000,17001,17002,17003,17004,17005};
        auto blob = zypher::generate_bevel_platform_object(p, "roller_bevel_platform_01", &ids);
        if (blob.empty()) { printf("  ERROR: %s\n", zypher::last_decline_reason().c_str()); }
        else write_scene(OUT + "roller_bevel_platform.scene", blob);
    }

    printf("\nAll done.\n");
    return 0;
}
