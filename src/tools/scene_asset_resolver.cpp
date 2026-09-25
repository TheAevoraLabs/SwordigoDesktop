#include "scene_asset_resolver.h"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <unordered_set>
#include <unordered_map>
#include <mutex>

namespace fs = std::filesystem;
namespace av::assets {

static std::unordered_map<std::string, fs::path> s_pod_cache;
static std::unordered_set<std::string> s_missing_pod_cache;
static std::mutex s_pod_cache_mutex;

static bool regular(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

static fs::path find_pod_recursive(const fs::path& root, const std::string& resource) {
    if (root.empty() || resource.empty()) return {};
    auto lowercase = [](std::string value) {
        for (char& ch : value) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        return value;
    };
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return {};
    const std::string wanted_name = lowercase(fs::path(resource).filename().string());
    const std::string wanted_stem = lowercase(fs::path(resource).stem().string());
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (it.depth() > 6) { it.disable_recursion_pending(); continue; }
        // Exclude build, cache, and vcs directories
        const std::string fn = it->path().filename().string();
        if (it->is_directory(ec)) {
            if (fn == "build" || fn == "bin" || fn == "CMakeFiles" || fn == ".git" || fn == ".gemini" || fn == "build-cmake") {
                it.disable_recursion_pending();
                continue;
            }
        }
        if (!it->is_regular_file(ec)) continue;
        const fs::path cand = it->path();
        if (lowercase(cand.extension().string()) != ".pod") continue;
        if (lowercase(cand.filename().string()) == wanted_name ||
            lowercase(cand.stem().string()) == wanted_stem) {
            return cand;
        }
    }
    return {};
}

std::filesystem::path resolve_pod(const fs::path& scene_path,
                                  const std::string& mesh,
                                  const std::vector<fs::path>& extra_roots) {
    if (mesh.empty()) return {};

    const std::string cache_key = scene_path.string() + "|" + mesh;
    {
        std::lock_guard<std::mutex> lock(s_pod_cache_mutex);
        if (s_missing_pod_cache.count(cache_key)) return {};
        auto it = s_pod_cache.find(cache_key);
        if (it != s_pod_cache.end()) return it->second;
    }

    const fs::path dir = scene_path.parent_path();
    const char* home = getenv("HOME");
    const fs::path home_path = home ? fs::path(home) : fs::path();

    std::vector<fs::path> dirs;
    if (!dir.empty()) {
        dirs.push_back(dir);
        dirs.push_back(dir / "resources");
        dirs.push_back(dir / "models");
        dirs.push_back(dir.parent_path());
        dirs.push_back(dir.parent_path() / "resources");
        dirs.push_back(dir.parent_path() / "models");
        dirs.push_back(dir.parent_path().parent_path() / "resources");
    }

    for (const auto& r : extra_roots) {
        if (r.empty()) continue;
        dirs.push_back(r);
        dirs.push_back(r / "resources");
        dirs.push_back(r / "models");
        dirs.push_back(r / "resources" / "models");
        dirs.push_back(r / "assets");
        dirs.push_back(r / "assets" / "resources");
        dirs.push_back(r / "assets" / "models");
    }

    if (!home_path.empty()) {
        const fs::path assets = home_path / ".local/share/swordigo-desktop/assets";
        dirs.push_back(assets);
        dirs.push_back(assets / "resources");
        dirs.push_back(assets / "models");
        dirs.push_back(assets / "resources" / "models");
        dirs.push_back(home_path / "resources");
        dirs.push_back(home_path / "SwordigoRefresh/assets/resources");
        dirs.push_back(home_path / "SwordigoDesktop/assets");
        dirs.push_back(home_path / "SwordigoDesktop/resources");
    }
    for (const char* apath : {
        "/storage/emulated/0/Download/Swordigo_Es_2.1.1/assets/resources",
        "/storage/emulated/0/SwordigoMini.ver.7.0.build.46/assets/resources",
        "/storage/emulated/0/Download/Swordigo_Es_2.1.1",
        "/storage/emulated/0/Swordigo/assets/resources",
        "/storage/emulated/0/Swordigo/resources",
        "/sdcard/Swordigo/assets/resources",
        "/sdcard/Download/Swordigo/resources",
        "/sdcard/Swordigo/resources"
    }) {
        dirs.push_back(fs::path(apath));
        dirs.push_back(fs::path(apath) / "resources");
    }
    dirs.push_back(fs::path("assets"));
    dirs.push_back(fs::path("assets/resources"));
    dirs.push_back(fs::path("assets/models"));
    dirs.push_back(fs::path("resources"));
    dirs.push_back(fs::path("models"));

    // 1. Direct candidate checks
    for (const auto& base : dirs) {
        for (const std::string& suffix : {std::string(), std::string(".pod"), std::string(".POD")}) {
            const fs::path candidate = base / (mesh + suffix);
            if (regular(candidate)) {
                std::lock_guard<std::mutex> lock(s_pod_cache_mutex);
                s_pod_cache[cache_key] = candidate;
                return candidate;
            }
        }
    }

    // 2. Recursive fallback matching ImGui Ruby
    std::unordered_set<std::string> searched_roots;
    for (const auto& base : dirs) {
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;
        std::string canon = fs::weakly_canonical(base, ec).string();
        if (searched_roots.insert(canon).second) {
            fs::path found = find_pod_recursive(base, mesh);
            if (!found.empty()) {
                std::lock_guard<std::mutex> lock(s_pod_cache_mutex);
                s_pod_cache[cache_key] = found;
                return found;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_pod_cache_mutex);
        s_missing_pod_cache.insert(cache_key);
    }
    return {};
}

std::filesystem::path resolve_pod(const fs::path& scene_path,
                                  const std::string& mesh,
                                  const fs::path& assets_root) {
    std::vector<fs::path> roots;
    if (!assets_root.empty()) roots.push_back(assets_root);
    return resolve_pod(scene_path, mesh, roots);
}

std::vector<fs::path> texture_candidates(const fs::path& model_path,
                                        const std::string& texture_name,
                                        const std::vector<fs::path>& extra_roots) {
    const fs::path dir = model_path.parent_path();
    const fs::path supplied(texture_name);
    const std::string stem = supplied.stem().string().empty() ? texture_name : supplied.stem().string();
    const char* home = getenv("HOME");
    const fs::path home_path = home ? fs::path(home) : fs::path();

    std::vector<fs::path> search_dirs;
    if (!dir.empty()) {
        search_dirs.push_back(dir);
        search_dirs.push_back(dir / "resources");
        search_dirs.push_back(dir.parent_path());
        search_dirs.push_back(dir.parent_path() / "resources");
    }
    for (const auto& r : extra_roots) {
        if (r.empty()) continue;
        search_dirs.push_back(r);
        search_dirs.push_back(r / "resources");
        search_dirs.push_back(r / "assets");
        search_dirs.push_back(r / "assets" / "resources");
    }
    if (!home_path.empty()) {
        search_dirs.push_back(home_path / ".local/share/swordigo-desktop/assets");
        search_dirs.push_back(home_path / ".local/share/swordigo-desktop/assets/resources");
        search_dirs.push_back(home_path / "resources");
        search_dirs.push_back(home_path / "SwordigoRefresh/assets/resources");
        search_dirs.push_back(home_path / "SwordigoDesktop/assets");
        search_dirs.push_back(home_path / "SwordigoDesktop/resources");
    }
    for (const char* apath : {
        "/storage/emulated/0/Download/Swordigo_Es_2.1.1/assets/resources",
        "/storage/emulated/0/SwordigoMini.ver.7.0.build.46/assets/resources",
        "/storage/emulated/0/Download/Swordigo_Es_2.1.1",
        "/storage/emulated/0/Swordigo/assets/resources",
        "/storage/emulated/0/Swordigo/resources",
        "/sdcard/Swordigo/assets/resources",
        "/sdcard/Download/Swordigo/resources",
        "/sdcard/Swordigo/resources"
    }) {
        search_dirs.push_back(fs::path(apath));
        search_dirs.push_back(fs::path(apath) / "resources");
    }
    search_dirs.push_back(fs::path("assets"));
    search_dirs.push_back(fs::path("assets/resources"));
    search_dirs.push_back(fs::path("resources"));

    std::vector<fs::path> out;
    for (const auto& d : search_dirs) {
        out.push_back(d / supplied);
        for (const auto& suffix : {"_2x.tex.png", ".tex.png", "_2x.pvr", ".pvr", "_2x.tex", ".tex", "_2x.png", ".png"}) {
            out.push_back(d / (stem + suffix));
        }
    }
    return out;
}

} // namespace av::assets
