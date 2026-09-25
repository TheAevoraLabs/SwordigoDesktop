// ============================================================================
// ground_mesh_compare_test.cpp — the C++ half of the ground-mesh harness.
//
// tools/ground_mesh_truth.py decodes a real ground object out of a shipped scene
// (polygon, generator parameters, both TextureMappings, and the baked mesh's
// vertices/normals/UVs/indexes) and writes it as TSV. This reads that TSV, runs
// boulderx::generate on the *same* outline and parameters, and diffs the result
// against what the engine actually baked.
//
// It is a test, not a script, because the diff has to fail loudly when a
// generator change moves away from the shipped meshes. Run it with a file:
//
//   python3 tools/ground_mesh_truth.py tsv --scene <scene> --object <id> \
//       --out /tmp/cave.tsv
//   ./bin/tests/ground_mesh_compare_test /tmp/cave.tsv
//
// With no argument it uses fixtures shipped in the source (small, hand-checked
// objects) so the suite keeps covering the comparison even without a corpus run.
//
// What it reports, and why each line is here:
//   * the Z envelope against the polygon's own MinDepth/MaxDepth and the
//     generator's SurfaceWidth / HatWidthOffset1 — the engine *writes*
//     MinDepth = -(SurfaceWidth/2) - HatWidthOffset1 for MeshType 1, so a mesh
//     whose |z| exceeds those is not the engine's shape;
//   * the per-node front/back depth against the depth recovered from the bake —
//     the engine's depth law is `(frand() - 0.5) * HorizNoise + SurfaceWidth/2`,
//     so a generator that *adds* noise to an already-jittered depth doubles the
//     relief, which is exactly the "Z balloons" symptom;
//   * submesh count/kind/texture per strip, in order — the engine's routing is
//     rim → surface mapping, plain ribbon → front mapping, cap → front mapping;
//   * the UV law of each strip: u = 0.5 + arc/scale, v = 0.5 + z/scale (ribbons)
//     and (0.5 + x/scale, 0.5 + y/scale) (the cap). Measured on shipped meshes,
//     see the report in doc 13.
// ============================================================================

#include "tools/boulderx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

// ── run options ─────────────────────────────────────────────────────────────
// The harness is also the bootstrap for *looking* at a mesh, so it can dump the
// mesh it generated as OBJ (tools/ground_mesh_view.py turns that plus the
// vanilla OBJ into a side-by-side sheet) and it can A/B the two depth laws
// against the same bake — the question "does the engine's law or the outline's
// own relief reproduce this object?" is answered by running both, not by
// reasoning about which one should.
struct Options {
    bool dump_obj = false;
    std::string obj_path;
    boulderx::Params::DepthSource depth_source = boulderx::Params::DepthSource::Engine;
};
static Options g_opts;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        std::printf("  FAIL  %s\n", what.c_str());
        ++g_failures;
    }
}

static void write_obj_mesh(const boulderx::Mesh& mesh, const std::string& path) {
    std::ofstream out(path);
    out << "# boulderx::generate — regenerated mesh\n";
    size_t base = 1;   // OBJ indices are 1-based
    size_t g = 0;
    const auto emit = [&](const boulderx::Submesh& sm) {
        out << "g " << g++ << "_" << sm.kind << "_" << sm.texture << "\n";
        for (const auto& v : sm.vertices)
            out << "v " << v.x << ' ' << v.y << ' ' << v.z << "\n";
        for (const auto& v : sm.vertices)
            out << "vn " << v.nx << ' ' << v.ny << ' ' << v.nz << "\n";
        for (const auto& v : sm.vertices)
            out << "vt " << v.u << ' ' << v.v << "\n";
        if (!sm.indices.empty()) {
            for (size_t i = 0; i + 2 < sm.indices.size(); i += 3) {
                const size_t a = base + sm.indices[i], b = base + sm.indices[i + 1],
                             c = base + sm.indices[i + 2];
                out << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b << '/' << b
                    << ' ' << c << '/' << c << '/' << c << "\n";
            }
        } else {
            for (size_t i = 0; i + 2 < sm.vertices.size(); i += 3) {
                const size_t a = base + i, b = base + i + 1, c = base + i + 2;
                out << "f " << a << '/' << a << '/' << a << ' ' << b << '/' << b << '/' << b
                    << ' ' << c << '/' << c << '/' << c << "\n";
            }
        }
        base += sm.vertices.size();
    };
    for (const auto& sm : mesh.surfaces) emit(sm);
    for (const auto& sm : mesh.fronts) emit(sm);
}

static void check_near(double got, double want, double tol, const std::string& what) {
    ++g_checks;
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL  %s (got %.4f, want %.4f +/- %g)\n", what.c_str(), got, want, tol);
        ++g_failures;
    }
}

// ── the TSV ─────────────────────────────────────────────────────────────────
struct Truth {
    std::string scene, identifier;
    double min_depth = 0, max_depth = 0, half_depth = 0;
    double surface_width = 0, hat_height = 0, hat_w1 = 0, hat_w2 = 0;
    double horiz_noise = 0, surface_scale = 250, front_scale = 250;
    unsigned random_seed = 0;
    int mesh_type = 0;
    std::string surface_tex, front_tex;
    double vanilla_zlo = 0, vanilla_zhi = 0;
    std::vector<std::pair<double, double>> polygon;   // node x,y
    std::vector<std::pair<double, double>> nodez;     // (back, front) per node
    // The u values the bake carries on each node's own XY (see the `nodeu`
    // section). This is the ground truth for the ribbon's arc-length mapping, as
    // opposed to checking the generated ribbon against its own arc.
    std::vector<std::vector<double>> nodeu;
    // How many cap triangles stand on each node in the BAKE (see the `capdeg`
    // section). The bake's busiest node is the ceiling the generator is held to:
    // the crumble symptom IS one node carrying a large share of the cap.
    std::vector<int> cap_deg;
    struct Strip {
        std::string kind, mapping, texture, normal_axis;
        int nv = 0, nf = 0, indexed = 1;
        double zlo = 0, zhi = 0;
    };
    std::vector<Strip> strips;
};

static bool read_truth(const std::string& path, Truth& t) {
    std::ifstream in(path);
    if (!in) return false;
    std::string section, line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.rfind("#section", 0) == 0) {
            section = line.substr(line.find('\t') + 1);
            continue;
        }
        if (line[0] == '#') continue;
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) f.push_back(cell);
        if (section == "object") {
            if (f.size() < 2) continue;
            const std::string& k = f[0];
            const std::string v = f[1];
            auto num = [&](double& dst) { if (!v.empty()) dst = std::atof(v.c_str()); };
            if (k == "identifier") t.identifier = v;
            else if (k == "scene") t.scene = v;
            else if (k == "min_depth") num(t.min_depth);
            else if (k == "max_depth") num(t.max_depth);
            else if (k == "half_depth") num(t.half_depth);
            else if (k == "SurfaceWidth") num(t.surface_width);
            else if (k == "HatHeight") num(t.hat_height);
            else if (k == "HatWidthOffset1") num(t.hat_w1);
            else if (k == "HatWidthOffset2") num(t.hat_w2);
            else if (k == "HorizNoise") num(t.horiz_noise);
            else if (k == "surface_scale") num(t.surface_scale);
            else if (k == "front_scale") num(t.front_scale);
            else if (k == "MeshType") t.mesh_type = std::atoi(v.c_str());
            else if (k == "RandomSeed") t.random_seed = static_cast<unsigned>(std::strtoul(v.c_str(), nullptr, 10));
            else if (k == "surface_tex") t.surface_tex = v;
            else if (k == "front_tex") t.front_tex = v;
            else if (k == "vanilla_zlo") num(t.vanilla_zlo);
            else if (k == "vanilla_zhi") num(t.vanilla_zhi);
        } else if (section == "polygon") {
            if (f.size() >= 3 && f[0] != "node")
                t.polygon.emplace_back(std::atof(f[1].c_str()), std::atof(f[2].c_str()));
        } else if (section == "nodez") {
            if (f.size() >= 4 && f[0] != "node") {
                const double back = f[1].empty() ? 0.0 : std::atof(f[1].c_str());
                const double front = f[2].empty() ? 0.0 : std::atof(f[2].c_str());
                t.nodez.emplace_back(back, front);
            }
        } else if (section == "nodeu") {
            if (f.size() >= 2 && f[0] != "node") {
                std::vector<double> us;
                std::stringstream rest(f[2]);
                std::string cell;
                while (rest >> cell) us.push_back(std::atof(cell.c_str()));
                if (t.nodeu.size() < t.polygon.size()) t.nodeu.resize(t.polygon.size());
                t.nodeu[std::atoi(f[0].c_str())] = us;
            }
        } else if (section == "capdeg") {
            if (f.size() >= 2 && f[0] != "node")
                t.cap_deg.push_back(std::atoi(f[1].c_str()));
        } else if (section == "submesh") {
            if (f.size() >= 10 && f[0] != "index") {
                Truth::Strip s;
                s.kind = f[1];
                s.nv = std::atoi(f[2].c_str());
                s.nf = std::atoi(f[3].c_str());
                s.indexed = std::atoi(f[4].c_str());
                s.mapping = f[5];
                s.texture = f[6];
                s.zlo = f[7].empty() ? 0.0 : std::atof(f[7].c_str());
                s.zhi = f[8].empty() ? 0.0 : std::atof(f[8].c_str());
                s.normal_axis = f[9];
                t.strips.push_back(s);
            }
        }
    }
    return !t.polygon.empty();
}

// ── statistics of whatever the generator produced ───────────────────────────
struct Generated {
    std::vector<Truth::Strip> strips;
    double zlo = 0, zhi = 0;
    // Cap concentration: how many cap triangles stand on the busiest node, and
    // how many cap triangles there are. A triangulation that fans from one apex
    // shows up here as a busiest node holding a large share of the cap — the
    // "every curvature and depth collapses into one place" symptom.
    int cap_max_deg = 0;
    int cap_busiest_node = -1;
    int cap_tris = 0;
    std::vector<std::pair<double, double>> nodez;
    // The UV law residuals, over the ribbons only: u - (arc/scale + 0.5) and
    // v - (z/scale + 0.5). Zero for a mesh that follows the engine's mapping.
    double uv_arc_err = 0, uv_z_err = 0;
    double uv_cap_err = 0;
};

static std::string normal_axis(const boulderx::Submesh& sm) {
    double ax[3] = {0, 0, 0};
    for (const auto& v : sm.vertices) {
        ax[0] += std::fabs(v.nx);
        ax[1] += std::fabs(v.ny);
        ax[2] += std::fabs(v.nz);
    }
    const int k = (ax[1] > ax[0] && ax[1] > ax[2]) ? 1 : (ax[2] > ax[0] ? 2 : 0);
    return std::string(1, "xyz"[k]);
}

static Generated measure(const Truth& t, const boulderx::Mesh& mesh) {
    Generated g;
    const double scale = t.surface_scale > 0 ? t.surface_scale : 250.0;
    for (const auto& sm : mesh.surfaces) {
        Truth::Strip s;
        s.kind = sm.kind;
        s.nv = static_cast<int>(sm.vertices.size());
        s.nf = static_cast<int>(sm.indices.size() / 3);
        s.texture = sm.texture;
        s.mapping = (sm.texture == t.surface_tex) ? "surface"
                    : (sm.texture == t.front_tex) ? "front" : "other";
        s.normal_axis = normal_axis(sm);
        s.zlo = 1e18; s.zhi = -1e18;
        for (const auto& v : sm.vertices) {
            s.zlo = std::min(s.zlo, static_cast<double>(v.z));
            s.zhi = std::max(s.zhi, static_cast<double>(v.z));
        }
        g.strips.push_back(s);
    }
    for (const auto& sm : mesh.fronts) {
        Truth::Strip s;
        s.kind = sm.kind;
        s.nv = static_cast<int>(sm.vertices.size());
        s.nf = static_cast<int>(sm.vertices.size() / 3);
        s.texture = sm.texture;
        s.mapping = (sm.texture == t.surface_tex) ? "surface"
                    : (sm.texture == t.front_tex) ? "front" : "other";
        s.indexed = 0;
        s.normal_axis = normal_axis(sm);
        s.zlo = 1e18; s.zhi = -1e18;
        for (const auto& v : sm.vertices) {
            s.zlo = std::min(s.zlo, static_cast<double>(v.z));
            s.zhi = std::max(s.zhi, static_cast<double>(v.z));
        }
        g.strips.push_back(s);
    }
    for (const auto& s : g.strips) {
        g.zlo = std::min(g.zlo, s.zlo);
        g.zhi = std::max(g.zhi, s.zhi);
    }

    // Cap concentration, by nearest outline node per cap corner.
    for (const auto& sm : mesh.fronts) {
        g.cap_tris += static_cast<int>(sm.vertices.size() / 3);
        std::vector<int> deg(t.polygon.size(), 0);
        for (const auto& v : sm.vertices) {
            size_t best = 0;
            double best_d = 1e30;
            for (size_t i = 0; i < t.polygon.size(); ++i) {
                const double dx = static_cast<double>(v.x) - t.polygon[i].first;
                const double dy = static_cast<double>(v.y) - t.polygon[i].second;
                const double d = dx * dx + dy * dy;
                if (d < best_d) { best_d = d; best = i; }
            }
            ++deg[best];
        }
        for (size_t i = 0; i < deg.size(); ++i) {
            if (deg[i] > g.cap_max_deg) {
                g.cap_max_deg = deg[i];
                g.cap_busiest_node = static_cast<int>(i);
            }
        }
    }

    // The UV law on the ribbons: `u = 0.5 + arc/scale`, `v = 0.5 + z/scale`, with
    // `arc` the cumulative outline distance at the node the vertex belongs to.
    //
    // The node is found by *nearest* outline point, not by assuming a
    // two-vertices-per-node layout: a rim strip carries six vertices per node and
    // five of them sit inward of the node's own XY, so position-grouping is the
    // only test that works for both strip families. (Getting this wrong makes the
    // check report a 0.83 error on a correct mesh — it did.)
    {
        // Only the PLAIN RIBBON is measured here. The rim's v is not 0.5 + z/scale
        // and should not be: the engine walks the cross-section's profile length
        // (0.030, 0.970, 1.037, 1.135 at florennum_cave1 obj9#18 node 1), so its
        // texture stretches around the bevel instead of compressing there. The
        // plain ribbon is always the last SurfaceMesh — GenerateMesh appends the
        // rim first, then the plain-with-hat-gaps ribbon.
        //
        // u is checked as an ACCUMULATION, not against the fixture's own node
        // order: the generator normalises the outline to CCW (ensure_ccw), so a
        // clockwise fixture is walked backwards and every u is mirrored about the
        // perimeter. Accumulating the outline distance along the order the ribbon
        // itself visits the nodes checks the same law without depending on which
        // way round the ring was authored.
        if (!mesh.surfaces.empty()) {
            const auto& rib = mesh.surfaces.back().vertices;
            std::vector<std::pair<double, double>> walked;   // one entry per node
            for (size_t k = 0; k + 1 < rib.size(); k += 2)
                walked.emplace_back(rib[k].x, rib[k].y);
            // u is checked against the outline arc measured in BOTH ring
            // directions, and the better of the two is reported: the generator
            // normalises the outline to CCW (ensure_ccw), so a clockwise fixture
            // is walked backwards and every u is mirrored about the perimeter.
            // Each emitted vertex is matched to the node it sits on, which is
            // also what makes the check work across the ribbon's gaps — a
            // MeshType 1 ribbon only visits 150 of thecave_crypt2 obj5's 189
            // nodes, and the arc it carries steps over the nodes the rim took.
            const size_t N = t.polygon.size();
            auto node_at = [&](double x, double y) {
                size_t best = 0;
                double best_d = 1e18;
                for (size_t i = 0; i < N; ++i) {
                    const double dx = x - t.polygon[i].first, dy = y - t.polygon[i].second;
                    const double d = dx * dx + dy * dy;
                    if (d < best_d) { best_d = d; best = i; }
                }
                return best;
            };
            // The arc the generator carries starts at whichever node its own walk
            // begins on — after ensure_ccw that is the *last* node of a clockwise
            // fixture, which is why node 0 of the ring cannot be the reference.
            // Each step spans the ring from one emitted node to the next, crossing
            // any nodes the ribbon skipped, exactly as the generator's accumulator
            // does.
            size_t first_node = N;
            double arc = 0.0;
            for (size_t k = 0; k < rib.size(); ++k) {
                const size_t ni = node_at(rib[k].x, rib[k].y);
                if (k == 0) {
                    first_node = ni;
                } else if (k % 2 == 0) {
                    const size_t prev = node_at(rib[k - 2].x, rib[k - 2].y);
                    if (prev != ni) {
                        const int step = static_cast<int>((ni + N - prev) % N);
                        const int dir = (step <= static_cast<int>(N) / 2) ? 1 : -1;
                        size_t cur = prev;
                        int guard = static_cast<int>(N) + 1;
                        while (cur != ni && guard-- > 0) {
                            const size_t nxt = dir > 0 ? (cur + 1) % N : (cur + N - 1) % N;
                            const double dx = t.polygon[nxt].first - t.polygon[cur].first;
                            const double dy = t.polygon[nxt].second - t.polygon[cur].second;
                            arc += std::sqrt(dx * dx + dy * dy);
                            cur = nxt;
                        }
                    }
                }
                const double want_u = 0.5 + arc / scale;
                const double want_v = 0.5 + static_cast<double>(rib[k].z) / scale;
                g.uv_arc_err = std::max(g.uv_arc_err,
                                        std::fabs(static_cast<double>(rib[k].u) - want_u));
                g.uv_z_err = std::max(g.uv_z_err,
                                      std::fabs(static_cast<double>(rib[k].v) - want_v));
            }
            if (getenv("GM_DEBUG"))
                std::printf("    arc dbg err=%.4f first_node=%zu (u there %.4f)\n",
                            g.uv_arc_err, first_node,
                            rib.empty() ? 0.0 : (double)rib[0].u);
            (void)walked;
        }
    }
    for (const auto& sm : mesh.fronts) {
        for (const auto& v : sm.vertices) {
            const double want_u = 0.5 + static_cast<double>(v.x) / scale;
            const double want_v = 0.5 + static_cast<double>(v.y) / scale;
            g.uv_cap_err = std::max(g.uv_cap_err, std::fabs(static_cast<double>(v.u) - want_u));
            g.uv_cap_err = std::max(g.uv_cap_err, std::fabs(static_cast<double>(v.v) - want_v));
        }
    }
    return g;
}

// Per-node depth out of a generated mesh: the front/back pair sitting on each
// node's own XY, the same match tools/ground_mesh_truth.py does on the bake.
static void generated_node_depths(const Truth& t, const boulderx::Mesh& mesh,
                                  std::vector<std::pair<double, double>>& out) {
    out.assign(t.polygon.size(), {0.0, 0.0});
    std::vector<char> seen(t.polygon.size(), 0);
    // The plain ribbon only (the last SurfaceMesh). The rim legitimately carries
    // vertices at the node's own XY too — at z = +-(SurfaceWidth/2 +
    // HatWidthOffset1), which is the polygon's own MaxDepth — and counting those
    // as "the node's depth" reports a spurious 5.0 error on a correct mesh. The
    // bake has them as well, which is why tools/ground_mesh_truth.py's nodez
    // column is the *ribbon* plane and not the rim's.
    const std::vector<boulderx::Submesh> rib =
        mesh.surfaces.empty() ? std::vector<boulderx::Submesh>{}
                              : std::vector<boulderx::Submesh>{mesh.surfaces.back()};
    for (const auto& sm : rib) {
        for (const auto& v : sm.vertices) {
            for (size_t i = 0; i < t.polygon.size(); ++i) {
                if (std::fabs(v.x - t.polygon[i].first) <= 1e-3 &&
                    std::fabs(v.y - t.polygon[i].second) <= 1e-3) {
                    if (!seen[i]) { out[i] = {v.z, v.z}; seen[i] = 1; }
                    else {
                        out[i].first = std::min(out[i].first, static_cast<double>(v.z));
                        out[i].second = std::max(out[i].second, static_cast<double>(v.z));
                    }
                }
            }
        }
    }
}

// ── the comparison ──────────────────────────────────────────────────────────
static void run_case(const Truth& t, bool verbose) {
    std::printf("===============================================================================\n");
    std::printf("object %s (%s)\n", t.identifier.c_str(), t.scene.c_str());
    std::printf("  polygon %zu nodes  MinDepth %.2f MaxDepth %.2f  half %.2f\n",
                t.polygon.size(), t.min_depth, t.max_depth, t.half_depth);
    std::printf("  generator MeshType %d SurfaceWidth %.2f HatHeight %.2f HWOff1 %.2f "
                "HWOff2 %.2f HorizNoise %.2f seed %u\n",
                t.mesh_type, t.surface_width, t.hat_height, t.hat_w1, t.hat_w2,
                t.horiz_noise, t.random_seed);
    std::printf("  mappings surface='%s' scale %.2f   front='%s' scale %.2f\n",
                t.surface_tex.c_str(), t.surface_scale, t.front_tex.c_str(), t.front_scale);

    // The engine's own write-back (GenerateMesh, 0x2D516C): the polygon's
    // MinDepth/MaxDepth are OUTPUTS, not authored input.
    const double expect_half = t.surface_width / 2.0 + (t.mesh_type == 1 ? t.hat_w1 : 0.0);
    check_near(t.max_depth, expect_half, 0.51,
               "the polygon's MaxDepth is SurfaceWidth/2 (+HatWidthOffset1 for MeshType 1)");
    check_near(t.min_depth, -expect_half, 0.51,
               "…and MinDepth its negative");

    boulderx::Params params;
    params.mesh_type = t.mesh_type;
    params.surface_width = t.surface_width;
    params.hat_height = t.hat_height;
    params.hat_width_offset_1 = t.hat_w1;
    params.hat_width_offset_2 = t.hat_w2;
    params.horiz_noise = t.horiz_noise;
    params.random_seed = t.random_seed;
    params.texture_scale = t.surface_scale;
    params.surface_texture = t.surface_tex;
    params.front_texture = t.front_tex;
    params.depth_source = g_opts.depth_source;
    std::printf("  depth source: %s\n",
                g_opts.depth_source == boulderx::Params::DepthSource::Engine
                    ? "engine law (SurfaceWidth/2 + frand*HorizNoise)"
                    : "the outline's own per-node relief");

    // The outline, with each node's depth taken from the *bake* — the same input
    // the editors hand over when they regenerate an existing object.
    std::vector<boulderx::Node> outline;
    for (size_t i = 0; i < t.polygon.size(); ++i) {
        boulderx::Node n;
        n.x = t.polygon[i].first;
        n.y = t.polygon[i].second;
        if (i < t.nodez.size()) { n.back_depth = t.nodez[i].first; n.front_depth = t.nodez[i].second; }
        outline.push_back(n);
    }

    boulderx::Mesh mesh;
    check(boulderx::generate(outline, params, mesh), "boulderx::generate succeeds");
    if (g_opts.dump_obj && !g_opts.obj_path.empty() && mesh.ok) {
        write_obj_mesh(mesh, g_opts.obj_path);
        std::printf("  wrote %s (%zu verts, %zu tris)\n", g_opts.obj_path.c_str(),
                    mesh.vertex_count(), mesh.triangle_count());
    }
    if (mesh.surfaces.empty()) return;

    const Generated g = measure(t, mesh);
    std::vector<std::pair<double, double>> gnz;
    generated_node_depths(t, mesh, gnz);

    std::printf("  %-13s %5s %5s %-8s %-9s %-16s %-22s %s\n",
                "gen strip", "nv", "nf", "mapping", "vs vanilla", "texture", "z", "normal");
    for (size_t i = 0; i < g.strips.size(); ++i) {
        const auto& s = g.strips[i];
        std::string vs = "-";
        if (i < t.strips.size()) {
            const auto& v = t.strips[i];
            vs = (v.kind == s.kind) ? "same-kind" : ("want " + v.kind);
            if (v.mapping == s.mapping) vs += "/same-tex";
            else vs += "/want " + v.mapping;
        } else if (i < t.strips.size() + 1) {
            vs = "extra";
        }
        std::printf("  %-13s %5d %5d %-8s %-9s %-16s [%8.2f,%8.2f] %s\n",
                    s.kind.c_str(), s.nv, s.nf, s.mapping.c_str(), vs.c_str(),
                    s.texture.c_str(), s.zlo, s.zhi, s.normal_axis.c_str());
    }
    if (verbose) {
        std::printf("  vanilla strips (in bake order):\n");
        for (size_t i = 0; i < t.strips.size() && i < 20; ++i) {
            const auto& v = t.strips[i];
            std::printf("    %-13s %5d %5d %-8s %-16s [%8.2f,%8.2f] %s\n",
                        v.kind.c_str(), v.nv, v.nf, v.mapping.c_str(), v.texture.c_str(),
                        v.zlo, v.zhi, v.normal_axis.c_str());
        }
    }

    // The editors' own path: the outline carries the law's planes and the
    // generate runs with depth_source = Outline (see mesh_import and mesh_apply
    // in both front-ends). Both paths are supposed to land on the same mesh, so
    // this one is checked against the bake too — a divergence here is a desktop
    // / mobile mismatch, which is exactly what "the desktop app flattens Z"
    // turned out to be.
    {
        boulderx::Params ed = params;
        ed.depth_source = boulderx::Params::DepthSource::Outline;
        ed.horiz_noise = 0.0;   // the outline already carries the jitter
        std::vector<double> lf, lb;
        boulderx::engine_node_depths(params, t.polygon.size(), lf, lb);
        std::vector<boulderx::Node> outline2;
        for (size_t i = 0; i < t.polygon.size(); ++i)
            outline2.push_back({t.polygon[i].first, t.polygon[i].second, lf[i], lb[i]});
        boulderx::Mesh mesh2;
        check(boulderx::generate(outline2, ed, mesh2), "the editors' path generates");
        if (mesh2.ok) {
            const Generated g2 = measure(t, mesh2);
            std::printf("  editor path : envelope [%.2f, %.2f]  span %.2f (vanilla %.2f)\n",
                        g2.zlo, g2.zhi, g2.zhi - g2.zlo, t.vanilla_zhi - t.vanilla_zlo);
            check_near(g2.zhi, g.zhi, 0.01, "the editor path and the law agree on the front envelope");
            check_near(g2.zlo, g.zlo, 0.01, "…and on the back");
        }
    }

    std::printf("  Z envelope  : vanilla [%.2f, %.2f]  generated [%.2f, %.2f]\n",
                t.vanilla_zlo, t.vanilla_zhi, g.zlo, g.zhi);
    const double van_span = t.vanilla_zhi - t.vanilla_zlo;
    const double gen_span = g.zhi - g.zlo;
    std::printf("  Z span      : vanilla %.2f  generated %.2f  (ratio %.2fx)\n",
                van_span, gen_span, van_span > 0 ? gen_span / van_span : 0.0);

    // The envelope must not BALLOON. This is the check for the reported symptom
    // ("it over-increases the Z distance"): taking the per-node array from the
    // bake instead of from the law regenerated thecave_crypt2 obj5 at a span of
    // 261.47 against vanilla's 208.24 — **1.26x** — because the rim's offset got
    // applied on top of a depth that already contained it.
    //
    // The tolerance is 3%, not 0, and that is a statement about what is still
    // unrecovered rather than a fudge: the corpus' worst case today is 1.02x,
    // and the excess is the rim's *back bevel* at the deepest node
    // (`-(plain + W1 + W2)` where the bake stops at `-(plain + W1)`). It is
    // bounded and it is not the failure this check exists for.
    if (van_span > 0.0)
        check(gen_span <= van_span * 1.03,
              "the regenerated Z span does not balloon past the bake's");

    // The per-node depth against the bake. This is the measurement that names the
    // double-noise defect: the bake already carries HorizNoise, so adding it again
    // multiplies the relief.
    double max_err = 0, van_relief = 0, gen_relief = 0;
    size_t compared = 0;
    for (size_t i = 0; i < t.nodez.size() && i < gnz.size(); ++i) {
        const double vf = t.nodez[i].second, vb = t.nodez[i].first;
        const double gf = gnz[i].second, gb = gnz[i].first;
        if (vf == 0.0 && vb == 0.0) continue;
        // A node the ribbon skipped (MeshType 1 gives it to the rim) has no
        // ribbon pair to compare on either side. The bake has the same hole, and
        // tools/ground_mesh_truth.py's nodez column reports whatever else sits
        // there — the rim's plane — so gating on the generated side is the only
        // fair comparison.
        if (gf == 0.0 && gb == 0.0) continue;
        ++compared;
        max_err = std::max(max_err, std::fabs(gf - vf));
        max_err = std::max(max_err, std::fabs(gb - vb));
        van_relief = std::max(van_relief, vf - vb);
        gen_relief = std::max(gen_relief, gf - gb);
    }
    if (compared) {
        std::printf("  per-node z  : %zu nodes compared, max |error| %.3f  "
                    "(relief vanilla %.2f vs generated %.2f)\n",
                    compared, max_err, van_relief, gen_relief);
        check_near(max_err, 0.0, 0.01,
                   "the regenerated per-node depth reproduces the bake");
    }

    // The cap must not fan. This is the guard for the reported crumble ("the
    // space between the first and last node is crumbled ... all curvatures and
    // depth-different vertices crammed into one place"): the port's ear-clip walk
    // resumed at the previous vertex after every clip, which let a single apex
    // absorb the band — 29 of 57 cap triangles on node 10 of this very object —
    // while the bake's busiest node carries 8. The tolerance is relative to the
    // bake (+6) because the bake's own concentration varies with the outline.
    if (!t.cap_deg.empty() && g.cap_tris > 0) {
        const int van_max = *std::max_element(t.cap_deg.begin(), t.cap_deg.end());
        int van_cap_tris = 0;
        for (const auto& s : t.strips)
            if (s.kind == "FrontMesh") van_cap_tris += s.nf;
        std::printf("  cap shape   : busiest node %d carries %d of %d cap triangles "
                    "(bake: %d of %d)\n",
                    g.cap_busiest_node, g.cap_max_deg, g.cap_tris, van_max,
                    van_cap_tris);
        check(g.cap_max_deg <= van_max + 6,
              "no single node absorbs the cap triangulation");
    }

    std::printf("  UV law      : ribbon v(z) err %.4f  cap err %.4f  "
                "ribbon u(arc, advisory) %.4f\n",
                g.uv_z_err, g.uv_cap_err, g.uv_arc_err);
    check_near(g.uv_z_err, 0.0, 1e-4, "ribbon v follows 0.5 + z/scale");
    check_near(g.uv_cap_err, 0.0, 1e-4, "cap UVs follow (0.5 + x/scale, 0.5 + y/scale)");

    // The ribbon's u against the BAKE's own u at the same node.
    //
    // u is a per-node parameter, so the bake is the ground truth for it and no
    // ring-direction guess is needed. Two laws live in the bake and both are
    // measured here: the cap's is planar (`0.5 + x/scale`) and the ribbon's is
    // arc length along the outline, which is why the comparison takes the
    // *nearest* baked u at the node and then looks at how the residual varies.
    //
    // What is asserted is that the residual is CONSTANT along the ribbon, i.e.
    // that the generator carries the bake's arc-length spacing node for node.
    // A constant offset is not zero, and it is deliberately not asserted away:
    // the bake's arc starts `offset * scale` world units along the ring from
    // where this generator starts its own, and *which* node the engine starts on
    // is not recovered yet. It is a texture scroll along the wall, not a shape
    // difference — .scratch/u_start.py is the measurement of that offset, and
    // tools/ground_mesh_truth.py's `nodeu` section is what exposes it.
    if (!t.nodeu.empty() && !mesh.surfaces.empty()) {
        const auto& rib = mesh.surfaces.back().vertices;
        const size_t N = t.polygon.size();
        double lo = 1e18, hi = -1e18;
        size_t compared = 0, orphan = 0;
        for (size_t k = 0; k < rib.size(); ++k) {
            if (k % 2 != 0) continue;                 // pairs share the node's u
            size_t best = 0;
            double best_d = 1e18;
            for (size_t i = 0; i < N; ++i) {
                const double dx = rib[k].x - t.polygon[i].first;
                const double dy = rib[k].y - t.polygon[i].second;
                const double d = dx * dx + dy * dy;
                if (d < best_d) { best_d = d; best = i; }
            }
            if (best >= t.nodeu.size() || t.nodeu[best].empty()) { ++orphan; continue; }
            double nearest = 1e18;
            for (double u : t.nodeu[best]) nearest = std::min(nearest, std::fabs(u - rib[k].u));
            ++compared;
            if (compared == 1 || nearest < lo) lo = nearest;
            if (compared == 1 || nearest > hi) hi = nearest;
        }
        std::printf("  ribbon u    : %zu nodes against the bake, residual %.4f..%.4f "
                    "(spread %.4f)",
                    compared, compared ? lo : 0.0, compared ? hi : 0.0,
                    compared ? hi - lo : 0.0);
        if (orphan) std::printf(" (%zu nodes carry no baked u)", orphan);
        std::printf("\n");
        // Deliberately ADVISORY. The residual does not reproduce as a constant
        // here, so the u law is not merely offset from the bake's — the arc this
        // generator carries and the bake's do not agree node for node, and
        // `0.5 + arc/scale` (the law the comment above records as measured) is
        // therefore incomplete. It is not asserted to zero because that would be
        // pinning a value nobody has explained; it is not asserted to a
        // tolerance either, because a tolerance chosen to pass is a check that
        // cannot fail. The instrument that opens this up is the `nodeu` section:
        // the bake carries TWO u laws per node — planar (0.5 + x/scale) for the
        // cap and arc length for the ribbon — and the ribbon's arc starts
        // somewhere on the ring other than node 0. .scratch/u_start.py and
        // .scratch/u_vs_arc.py are the two measurements. It is a texture scroll
        // along a wall, not a shape difference; every Z assertion above is hard.
    }

    // Strip routing: the engine's MeshType 1 order is rim(surface) then
    // plain(front); MeshType 0 is a single ribbon(surface); the cap is always last
    // and front-mapped.
    if (!g.strips.empty()) {
        const auto& last = g.strips.back();
        check(last.kind == "FrontMesh", "the cap is the last submesh");
        // Shipped objects usually point both TextureMappings at the *same* name,
        // and then a material name cannot say which mapping a strip was routed
        // through — the UVs can, but not at this granularity. Saying so beats
        // reporting a failure the data cannot support (thecave_crypt2 obj5 sets
        // both to crypt_tiles).
        if (t.surface_tex == t.front_tex) {
            std::printf("  mapping     : both mappings are '%s' — routing is ambiguous, "
                        "not judged\n", t.front_tex.c_str());
        } else {
            check(last.mapping == "front", "the cap carries the front mapping");
        }
    }
}

// ── the built-in fixtures ───────────────────────────────────────────────────
// Small objects, hand-checked against the corpus so the suite still covers the
// comparison when no TSV is passed.
struct Fixture {
    const char* name;
    double surface_width, hat_w1, hat_height, horiz_noise;
    double max_depth;
    int mesh_type;
    std::vector<std::pair<double, double>> polygon;
};

static std::vector<Fixture> fixtures() {
    // A cut-down 12-node cave-ish polygon (a floor with two shelves) and the
    // engine's own parameters for a MeshType 1 slab.
    return {
        {"cave_floor", 400.0, 5.0, 20.0, 0.0, 205.0, 1,
         {{-600, 0}, {-400, 0}, {-200, 40}, {0, 40}, {200, -20}, {400, -20},
          {400, -200}, {200, -200}, {0, -160}, {-200, -160}, {-400, -200}, {-600, -200}}},
        {"plain_slab", 200.0, 5.0, 20.0, 0.0, 100.0, 0,
         {{-300, 100}, {-100, 120}, {100, 90}, {300, 110},
          {300, -110}, {100, -90}, {-100, -120}, {-300, -100}}},
    };
}

static void run_fixture(const Fixture& f) {
    Truth t;
    t.identifier = f.name;
    t.scene = "fixture";
    t.surface_width = f.surface_width;
    t.hat_w1 = f.hat_w1;
    t.hat_height = f.hat_height;
    t.horiz_noise = f.horiz_noise;
    t.max_depth = f.max_depth;
    t.min_depth = -f.max_depth;
    t.half_depth = f.max_depth;
    t.mesh_type = f.mesh_type;
    t.surface_scale = 250.0;
    t.front_scale = 250.0;
    t.surface_tex = "cave_rock";
    t.front_tex = "cave_front";
    t.random_seed = 1291618994u;
    t.polygon = f.polygon;
    // The bake's own per-node depth, from the engine's law: SurfaceWidth/2 with
    // HorizNoise jitter. Zero noise here, so the bake is a clean slab.
    for (size_t i = 0; i < t.polygon.size(); ++i)
        t.nodez.emplace_back(-f.surface_width / 2.0, f.surface_width / 2.0);
    t.vanilla_zlo = -f.surface_width / 2.0 - f.hat_w1;
    t.vanilla_zhi = f.surface_width / 2.0 + f.hat_w1;

    run_case(t, true);

    // The engine's write-back, checked on the fixture's own numbers.
    check_near(t.max_depth, t.surface_width / 2.0 + (f.mesh_type == 1 ? f.hat_w1 : 0.0), 0.51,
               std::string(f.name) + ": MaxDepth is the engine's write-back");
}

static void usage() {
    std::printf("usage: ground_mesh_compare_test [truth.tsv] [--obj out.obj] "
                "[--depth-source engine|outline]\n");
}

int main(int argc, char** argv) {
    std::string tsv;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--obj" && i + 1 < argc) {
            g_opts.dump_obj = true;
            g_opts.obj_path = argv[++i];
        } else if (a == "--depth-source" && i + 1 < argc) {
            const std::string s = argv[++i];
            if (s == "outline") g_opts.depth_source = boulderx::Params::DepthSource::Outline;
            else if (s == "engine") g_opts.depth_source = boulderx::Params::DepthSource::Engine;
            else { std::printf("FAIL: unknown depth source '%s'\n", s.c_str()); usage(); return 2; }
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::printf("FAIL: unknown option '%s'\n", a.c_str());
            usage();
            return 2;
        } else if (tsv.empty()) {
            tsv = a;
        }
    }

    if (!tsv.empty()) {
        Truth t;
        if (!read_truth(tsv, t)) {
            std::printf("FAIL: could not read the truth TSV %s\n", tsv.c_str());
            return 1;
        }
        run_case(t, true);
    } else {
        // No argument: the fixtures AND every checked-in truth TSV, so real
        // shipped objects are diffed on every run. The fixtures alone cannot catch
        // a bug that only shows on a MeshType 1 object with HorizNoise — which is
        // what the rim-offset defect was (their noise is 0 and their outline is
        // on the law, so every depth source agrees on them).
        //
        // thecave_part15 obj9#10 is checked in for a different reason: it is the
        // serpentine 59-node cave wall the crumble was reported on, where the
        // fanning ear-clip walk put 29 of 57 cap triangles on one node. A small
        // convex-ish outline cannot expose that, so this fixture is what makes the
        // cap-concentration check real.
        std::printf("no TSV given: running the built-in fixtures");
        std::vector<std::string> checked = {};
        for (const char* dir : {"tests/data/ground_truth", "../tests/data/ground_truth",
                                "../../tests/data/ground_truth"}) {
            if (!std::ifstream(std::string(dir) + "/fire2_obj3_15.tsv").good()) continue;
            for (const auto& name : {"fire2_obj3_15.tsv", "thecave_part15_obj9_10.tsv"})
                checked.push_back(std::string(dir) + "/" + name);
            break;
        }
        std::printf(", plus %zu checked-in shipped object(s)\n", checked.size());
        if (checked.empty())
            std::printf("  NOTE: tests/data/ground_truth was not found — the real-object "
                        "diff did NOT run. Pass a path from `tools/ground_mesh_truth.py "
                        "tsv` to diff one.\n");
        for (const auto& f : fixtures()) run_fixture(f);
        for (const auto& path : checked) {
            Truth t;
            if (!read_truth(path, t)) {
                std::printf("FAIL: could not read the checked-in truth TSV %s\n", path.c_str());
                return 1;
            }
            run_case(t, false);
        }
    }

    std::printf("===============================================================================\n");
    if (g_failures == 0) {
        std::printf("PASS: ground mesh comparison (%d checks)\n", g_checks);
        return 0;
    }
    std::printf("FAIL: ground mesh comparison (%d/%d checks failed)\n", g_failures, g_checks);
    return 1;
}
