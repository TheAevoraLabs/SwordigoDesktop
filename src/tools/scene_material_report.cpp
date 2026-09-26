/* scene_material_report.cpp — authoritative per-level material + lighting dump
 *
 * WHY THIS EXISTS
 * ---------------
 * Two host-side subsystems need ground truth about what a Swordigo level
 * actually contains, and both were previously driven by guesses:
 *
 *   1. TextureAwareMapper / the texture-identity trust gate. The gate accepts a
 *      texture name only if it appears in `scene_v3_biomes.json`. That table is
 *      derived from the biome *palette* - it is NOT the set of textures a given
 *      LEVEL references. This tool prints the real per-level set straight from
 *      the decoded scene, so the gate can be checked against data instead of
 *      against an assumption.
 *
 *   2. The lighting remaster. Vanilla Swordigo's lighting is fully described by
 *      the scene graph and `scene_loader.cpp` already parses all of it:
 *        - SceneLight   f130 { f1 Type(1=Ambient,2=Dir,3=Point,4=Overlay),
 *                              f2 Intensity, f3 Color, f6 Offset, f7 Radius }
 *        - SceneOverlay type-4 lights: a darkness veil whose brightness is
 *                       restored around point-light falloff radii
 *        - SceneShadow  f131 { f1 WidthRadius, f2 DepthRadius, f3 Offset } -
 *                       the game's REAL shadow model. Vanilla does not use
 *                       shadow maps; it stamps soft ellipses on the ground
 *                       plane. Any "true shadows" work must start here.
 *
 * This is a read-only reporting tool. It never writes a .scene.
 *
 * Build:  see tools/build_scene_material_report.sh
 * Usage:  ./scene_material_report <file.scene | directory> [--quiet-textures]
 */

#include "tools/scene_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// libswcore references this global. Every standalone tool in this tree defines
// it exactly once in its own main TU (see ruby_cli.cpp, asset_viewer.cpp) - it is
// the same contract, kept here so the report links against the shipped libs
// rather than a private object list.
std::string g_instance_assets_dir = "assets";

// ---------------------------------------------------------------- helpers ---

static const char* light_type_name(int t) {
    switch (t) {
        case 1: return "Ambient";
        case 2: return "Directional";
        case 3: return "Point";
        case 4: return "Overlay";
        default: return "?";
    }
}

// A texture reference is "not a texture" when it is really a model name, an
// empty placeholder or a known non-image token. The scene component payloads
// occasionally carry a POD/mesh name in the same field a texture would occupy,
// so this filter is what keeps the reported set honest.
static bool looks_like_texture_token(const std::string& s) {
    if (s.empty()) return false;
    if (s.size() > 96) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return false;
    }
    static const char* kSuffixes[] = {".png", ".pvr", ".jpg", ".tex", ".jpeg", ".ktx"};
    for (const char* suf : kSuffixes) {
        const size_t n = std::strlen(suf);
        if (s.size() > n) {
            std::string tail = s.substr(s.size() - n);
            for (auto& ch : tail) ch = (char)std::tolower((unsigned char)ch);
            if (tail == suf) return true;
        }
    }
    // Bare sprite/terrain stems used by the game data.
    return s.find(' ') == std::string::npos;
}

struct LevelReport {
    std::string file;
    int objects = 0;
    std::map<std::string, int> textures;   // name -> reference count
    int lights_ambient = 0, lights_dir = 0, lights_point = 0, lights_overlay = 0;
    float dir_intensity_min = 1e9f, dir_intensity_max = -1e9f;
    float point_radius_min = 1e9f, point_radius_max = -1e9f;
    float ambient_intensity_min = 1e9f, ambient_intensity_max = -1e9f;
    int glow_count = 0, flicker_count = 0;
    int shadows = 0;
    float shadow_w_max = 0.0f, shadow_d_max = 0.0f;
    int waters = 0;
    std::vector<std::string> light_detail;
    std::vector<std::string> shadow_material;
    std::vector<std::string> water_textures;
};

static LevelReport analyse(const std::string& path) {
    LevelReport r;
    r.file = fs::path(path).filename().string();

    av::SceneData sc = av::scene_load(path);
    r.objects = (int)sc.objects.size();

    for (const auto& o : sc.objects) {
        auto add = [&](const std::string& t) {
            if (looks_like_texture_token(t)) r.textures[t]++;
        };
        add(o.texture_name);
        add(o.background_name);
        for (const auto& t : o.ground_mesh_textures) add(t);
    }
    for (const auto& w : sc.waters) {
        if (!w.texture.empty()) {
            r.textures[w.texture]++;
            r.water_textures.push_back(w.texture);
        }
        ++r.waters;
    }

    for (const auto& L : sc.lights) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "    %-11s intensity=%.3f color=(%.3f,%.3f,%.3f) radius=%.1f%s%s",
                      light_type_name(L.type), L.intensity,
                      L.color[0], L.color[1], L.color[2], L.radius,
                      L.glow ? " glow" : "", L.flicker ? " flicker" : "");
        r.light_detail.push_back(buf);

        switch (L.type) {
            case 1:
                ++r.lights_ambient;
                r.ambient_intensity_min = std::min(r.ambient_intensity_min, L.intensity);
                r.ambient_intensity_max = std::max(r.ambient_intensity_max, L.intensity);
                break;
            case 2:
                ++r.lights_dir;
                r.dir_intensity_min = std::min(r.dir_intensity_min, L.intensity);
                r.dir_intensity_max = std::max(r.dir_intensity_max, L.intensity);
                break;
            case 3:
                ++r.lights_point;
                r.point_radius_min = std::min(r.point_radius_min, L.radius);
                r.point_radius_max = std::max(r.point_radius_max, L.radius);
                break;
            case 4:
                ++r.lights_overlay;
                break;
            default: break;
        }
        if (L.glow)    ++r.glow_count;
        if (L.flicker) ++r.flicker_count;
    }

    for (const auto& S : sc.shadows) {
        ++r.shadows;
        r.shadow_w_max = std::max(r.shadow_w_max, S.width_radius);
        r.shadow_d_max = std::max(r.shadow_d_max, S.depth_radius);
    }

    return r;
}

static void print(const LevelReport& r, bool verbose_textures) {
    std::printf("\n=== %s ===\n", r.file.c_str());
    std::printf("  objects=%d  textures_referenced=%zu  waters=%d\n",
                r.objects, r.textures.size(), r.waters);

    std::printf("  lights: ambient=%d dir=%d point=%d overlay=%d  (glow=%d flicker=%d)\n",
                r.lights_ambient, r.lights_dir, r.lights_point, r.lights_overlay,
                r.glow_count, r.flicker_count);

    if (r.lights_dir > 0)
        std::printf("    dir intensity range   : %.3f .. %.3f\n", r.dir_intensity_min, r.dir_intensity_max);
    if (r.lights_ambient > 0)
        std::printf("    ambient intensity rng : %.3f .. %.3f\n", r.ambient_intensity_min, r.ambient_intensity_max);
    if (r.lights_point > 0)
        std::printf("    point radius range    : %.1f .. %.1f\n", r.point_radius_min, r.point_radius_max);

    std::printf("  blob shadows: %d  (max w=%.1f d=%.1f)\n",
                r.shadows, r.shadow_w_max, r.shadow_d_max);

    if (!r.water_textures.empty()) {
        std::printf("  water textures:");
        for (const auto& t : r.water_textures) std::printf(" %s", t.c_str());
        std::printf("\n");
    }

    if (verbose_textures) {
        std::printf("  --- texture set (name x refs) ---\n");
        for (const auto& kv : r.textures)
            std::printf("    %-36s x%d\n", kv.first.c_str(), kv.second);
    }

    if (!r.light_detail.empty()) {
        std::printf("  --- light instances ---\n");
        for (const auto& s : r.light_detail) std::printf("%s\n", s.c_str());
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> inputs;
    bool verbose_textures = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--quiet-textures") { verbose_textures = false; continue; }
        if (a == "--counts-only")    { verbose_textures = false; continue; }
        inputs.push_back(a);
    }
    if (inputs.empty()) {
        std::fprintf(stderr,
            "usage: scene_material_report <file.scene | directory> [--quiet-textures]\n");
        return 2;
    }

    std::vector<std::string> files;
    for (const auto& in : inputs) {
        std::error_code ec;
        if (fs::is_directory(in, ec)) {
            for (const auto& e : fs::directory_iterator(in, ec)) {
                if (e.path().extension() == ".scene") files.push_back(e.path().string());
            }
        } else {
            files.push_back(in);
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::fprintf(stderr, "no .scene files found\n"); return 1; }

    // Global union across every level, so the texture-identity gate can be
    // checked against ALL names the game data actually uses.
    std::map<std::string, int> global_textures;
    int total_lights = 0;

    for (const auto& f : files) {
        LevelReport r = analyse(f);
        print(r, verbose_textures);
        for (const auto& kv : r.textures) global_textures[kv.first] += kv.second;
        total_lights += r.lights_ambient + r.lights_dir + r.lights_point + r.lights_overlay;
    }

    std::printf("\n================ GLOBAL SUMMARY ================\n");
    std::printf("levels=%zu  distinct_textures=%zu  total_lights=%d\n",
                files.size(), global_textures.size(), total_lights);
    std::printf("--- every texture name the game data references ---\n");
    for (const auto& kv : global_textures)
        std::printf("  %-40s total=%d\n", kv.first.c_str(), kv.second);

    return 0;
}
