// ============================================================================
// boulderx_engine_parity_test.cpp — boulderx vs the SHIPPED engine meshes.
//
// Each fixture below is a real ground object copied out of a decoded scene in
// Scener/data/decoded_scenes, together with the values the engine's own baked
// mesh actually has. That data was produced by the 1.4.13 engine, so it is the
// only authority worth testing against; nothing here is derived from my reading
// of the decompile alone.
//
// Corpus context (202 decoded scenes, 2,538 ground objects, measured by
// .scratch/ground_gen_census.py):
//   * 2,518 ground polygons are Closed=1 / Convex=0
//   * 1,770 of them (70%) are CONCAVE
//   * 2,070 (81%) are MeshType 1 (rounded rim), 468 are MeshType 0
//
// Two measurements from the same corpus that decide what is NOT tested here:
//   * the largest amount any baked vertex rises above its own polygon's top edge
//     is 2.04 units, and all 154 such vertices (43 submeshes) lie exactly 3.000
//     from a convex node — the rim's corner shift, which boulderx does not
//     implement yet. See doc 13 §7 item 1; the shift's direction is not
//     recoverable from the shipped data alone, so no fixture asserts it.
//     .scratch/ground_dome_probe.py is the measurement.
//   * no ground object anywhere carries a dome. "Round hat" names the RIM, not a
//     dome (doc 09 §4), so there is no shipped mesh to pin boulder's dome against;
//     boulderx_test [14] pins boulder's own geometry instead. Keeping the
//     measured absence here matters, because the obvious-looking alternative is to
//     hunt for a fixture that does not exist.
//
// Where boulderx deliberately differs from the engine it is marked DEVIATION
// and explained — never silently.
// ============================================================================

#include "tools/boulderx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL  %s\n", what.c_str());
        ++g_failures;
    }
}

static void check_near(double got, double want, double tol, const std::string& what) {
    if (std::fabs(got - want) > tol) {
        std::printf("  FAIL  %s (got %.6f, want %.6f +/- %g)\n", what.c_str(), got, want, tol);
        ++g_failures;
    }
}

static void z_range(const boulderx::Submesh& s, double& lo, double& hi) {
    lo = 1e18;
    hi = -1e18;
    for (const auto& v : s.vertices) {
        lo = std::min(lo, (double)v.z);
        hi = std::max(hi, (double)v.z);
    }
}

static double tri_area(const boulderx::Vertex& a, const boulderx::Vertex& b,
                       const boulderx::Vertex& c) {
    return 0.5 * std::fabs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
}

static double mesh_area(const boulderx::Submesh& s) {
    double a = 0.0;
    if (s.indexed) {
        for (size_t t = 0; t + 2 < s.indices.size(); t += 3)
            a += tri_area(s.vertices[s.indices[t]], s.vertices[s.indices[t + 1]],
                          s.vertices[s.indices[t + 2]]);
    } else {
        for (size_t t = 0; t + 2 < s.vertices.size(); t += 3)
            a += tri_area(s.vertices[t], s.vertices[t + 1], s.vertices[t + 2]);
    }
    return a;
}

// The generator emits every family in the engine's <=20-front chunks, so a
// fixture with more than 20 faces in one family arrives as SEVERAL submeshes.
// These aggregate over a family the way the engine's own per-family totals do.
static size_t total_verts(const std::vector<boulderx::Submesh>& subs) {
    size_t n = 0;
    for (const auto& s : subs) n += s.vertices.size();
    return n;
}

static size_t total_fronts(const std::vector<boulderx::Submesh>& subs) {
    size_t n = 0;
    for (const auto& s : subs)
        n += s.indexed ? s.indices.size() / 3 : s.vertices.size() / 3;
    return n;
}

static double total_area(const std::vector<boulderx::Submesh>& subs) {
    double a = 0.0;
    for (const auto& s : subs) a += mesh_area(s);
    return a;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture A — beyond_graveyard.scene, obj12
//
//   polygon (4 nodes, CCW): (-149.1927,-101.8163) (85.9740,-100.4861)
//                           (186.5951,99.6615)   (-186.5951,101.8163)
//   GroundPolygon: Collides 1, MinDepth -105, MaxDepth 105
//   GroundMeshGenerator: MeshType 1, SurfaceWidth 200, HatHeight 25,
//                        HatWidthOffset1 5, HatWidthOffset2 5,
//                        HorizNoise 0, RandomSeed 0
//   SurfaceTextureMapping   -> lowergrove_grass
//   FrontTextureMapping     -> graveyard_ground
//
//   Baked by the engine:
//     SurfaceMesh #1  nv 20  nf 14  z [-110, 110]  lowergrove_grass
//     SurfaceMesh #2  nv  8  nf  6  z [-100, 100]  graveyard_ground
//     FrontMesh       nv  6  nf  2  z [ 100, 100]  graveyard_ground
// ─────────────────────────────────────────────────────────────────────────────
static void fixture_beyond_graveyard_obj12() {
    std::printf("[A] beyond_graveyard obj12 — MeshType 1, 4 nodes, no noise\n");
    boulderx::SwdmDocument doc;
    doc.identifier = "obj12";
    const double xy[4][2] = {{-149.1927, -101.8163}, {85.9740, -100.4861},
                             {186.5951, 99.6615}, {-186.5951, 101.8163}};
    for (const auto& p : xy) doc.outline.push_back({p[0], p[1], 100.0, -100.0});
    doc.params.mesh_type = 1;
    doc.params.surface_width = 200.0;
    doc.params.hat_height = 25.0;
    doc.params.hat_width_offset_1 = 5.0;
    doc.params.hat_width_offset_2 = 5.0;
    doc.params.horiz_noise = 0.0;
    doc.params.random_seed = 0;
    doc.params.surface_texture = "lowergrove_grass";
    doc.params.front_texture = "graveyard_ground";

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "generates");
    check(mesh.surfaces.size() == 2, "two SurfaceMesh entries (engine: rim, then inset ribbon)");
    check(mesh.fronts.size() == 1, "one FrontMesh");

    // ── exact matches ──
    double lo = 0, hi = 0;
    z_range(mesh.surfaces[1], lo, hi);
    check_near(lo, -100.0, 1e-3, "inset ribbon back plane = -100 (engine -100)");
    check_near(hi, 100.0, 1e-3, "inset ribbon front plane = +100 (engine +100)");
    check(mesh.surfaces[1].vertices.size() == 8,
          "inset ribbon has 2*n = 8 vertices (engine nv 8)");
    check(mesh.surfaces[1].texture == "graveyard_ground",
          "inset ribbon carries the FRONT texture (engine: graveyard_ground)");
    check(mesh.surfaces[0].texture == "lowergrove_grass",
          "rim carries the SURFACE texture (engine: lowergrove_grass)");

    z_range(mesh.fronts[0], lo, hi);
    check_near(lo, 100.0, 1e-3, "cap sits on the front plane (engine z [100,100])");
    check_near(hi, 100.0, 1e-3, "cap is flat at this depth (engine z [100,100])");
    check(mesh.fronts[0].vertices.size() == 6, "cap has 3*(n-2) = 6 vertices (engine nv 6)");
    check(mesh.fronts[0].vertices.size() / 3 == 2, "cap has n-2 = 2 triangles (engine nf 2)");
    check(mesh.fronts[0].texture == "graveyard_ground",
          "cap carries the front texture (engine: graveyard_ground)");

    // ── the rim's envelope, which is what the player sees ──
    z_range(mesh.surfaces[0], lo, hi);
    check_near(lo, -110.0, 1e-3, "rim bevel reaches -110 (engine z [-110,110])");
    check_near(hi, 110.0, 1e-3, "rim bevel reaches +110 (engine z [-110,110])");

    // The rim lands where the engine puts it: on every node with an edge normal
    // within 45 degrees of +Y, and a run spans consecutive such nodes. For this
    // quad that is node 2 (out_normal (0.01, 1.00)) and node 3, whose INCOMING
    // edge (node 2 -> node 3) also faces +Y. This object's own bake confirms the
    // pair — it carries rings at node 2 AND node 3 (see the deviation note below
    // for the vertex-count difference) — so the rim is two 6-vertex
    // cross-sections with a band swept between them.
    check(mesh.surfaces[0].vertices.size() == 12,
          "rim: two 6-vertex cross-sections, one per +Y-facing node");
    check(!mesh.surfaces[0].indices.empty(),
          "and a band, because the two rim nodes are adjacent in the ring");

    // DEVIATION, and it is a real one: the shipped bake for this object carries
    // 20 rim vertices in 14 faces, because the engine's walk does not stop at the
    // last +Y-facing node — it emits one more ring past the run's end. Measured
    // there: rings at node 2 AND node 3, 10 vertices each (6 profile points plus
    // the band's landing points, whose u differs). The rule above reproduces the
    // node-level decision on ~96% of 378 nodes over 12 cave objects and exactly
    // on most whole objects, but the run's last ring is still unpinned; the
    // difference is one cross-section at the run's tail, not the cross-section's
    // geometry or the wrapping the rim used to do.

    const std::string bytes =
        boulderx::generate_ground_mesh_object(doc.outline, doc.params, "obj12");
    check(!bytes.empty(), "binary object emits");
    check_near(mesh.max_abs_z(), 110.0, 1e-3, "mesh outermost |z| is 110 (engine rim z)");
    // The engine stores +/-105 on GroundPolygon for this 110-deep mesh, i.e.
    // front plane + W rather than the bevel tip. Confirmed by re-reading the
    // emitted object: the MinDepth/MaxDepth floats must be 105, not 110.
    const float want105 = 105.0f;
    std::string needle(reinterpret_cast<const char*>(&want105), 4);
    size_t hits = 0;
    for (size_t p = bytes.find(needle); p != std::string::npos;
         p = bytes.find(needle, p + 1))
        ++hits;
    check(hits >= 2, "GroundPolygon/CollisionShape store +/-105 (front plane + W)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture B — fire_part2.scene, obj3#32
//
//   7 nodes, HorizNoise 50, MeshType 0, SurfaceWidth 20.
//   GroundPolygon MinDepth -10, MaxDepth 10  (front/back planes +/-10)
//   Baked: SurfaceMesh nv 14 (=2n) nf 14 (=2n) z [-28.523, 28.695] fire_grass
//          FrontMesh   nv 15 (=3(n-2)) nf 5   z [-10.181, 28.695] graveyard_ground
//
//   The z range is the important part: it runs to +/-28.7 on a mesh whose
//   polygon says +/-10, because the engine applies HorizNoise to the per-node
//   depth arrays. boulder leaves HorizNoise at 0 ("Missing Features" in its own
//   source), so it can never produce this.
// ─────────────────────────────────────────────────────────────────────────────
static void fixture_fire_part2_obj3_32() {
    std::printf("[B] fire_part2 obj3#32 — MeshType 0, 7 nodes, HorizNoise 50\n");
    boulderx::SwdmDocument doc;
    doc.identifier = "obj3#32";
    const double xy[7][2] = {{-1.1112, -47.0076}, {46.2810, -25.0029}, {41.6509, 24.4804},
                             {-1.4422, 47.0076},  {-36.5423, 28.6364}, {-46.2810, -2.5087},
                             {-36.4276, -31.6746}};
    for (const auto& p : xy) doc.outline.push_back({p[0], p[1], 10.0, -10.0});
    doc.params.mesh_type = 0;
    doc.params.surface_width = 20.0;
    doc.params.hat_height = 25.0;
    doc.params.horiz_noise = 50.0;
    doc.params.random_seed = 0;
    doc.params.surface_texture = "fire_grass";
    doc.params.front_texture = "graveyard_ground";

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "generates");
    check(mesh.surfaces.size() == 1 && mesh.fronts.size() == 1, "MT0: one ribbon + one cap");
    check(mesh.surfaces[0].vertices.size() == 14,
          "ribbon has 2n = 14 vertices (engine nv 14)");
    check(mesh.surfaces[0].indices.size() / 3 == 14,
          "ribbon has 2n = 14 triangles (engine nf 14)");
    check(mesh.fronts[0].vertices.size() == 15,
          "cap has 3*(n-2) = 15 vertices (engine nv 15)");
    check(mesh.fronts[0].vertices.size() / 3 == 5, "cap has n-2 = 5 triangles (engine nf 5)");

    double lo = 0, hi = 0;
    z_range(mesh.surfaces[0], lo, hi);
    // Envelope from the engine's own formula: plane +/- noise/2 around +/-10.
    check(lo >= -35.0 - 1e-6, "ribbon back depth stays within -10 - noise/2");
    check(hi <= 35.0 + 1e-6, "ribbon front depth stays within +10 + noise/2");
    check(hi > 10.5, "HorizNoise displaces the ribbon past the authored plane (boulder: 0)");
    check(lo < -10.5, "HorizNoise displaces the back plane too");

    // The cap must follow the FRONT depth array: the engine's cap reaches
    // +28.695, which is only reachable from the front plane (back tops out at
    // +15). If the cap used back depths this assertion fails.
    z_range(mesh.fronts[0], lo, hi);
    check(hi > 15.0, "cap corners use the FRONT depth array, not the back one");
    check(hi <= 35.0 + 1e-6, "cap stays within the front plane envelope");

    // Determinism, the engine's documented guarantee (doc 09 §5).
    boulderx::Mesh again;
    check(boulderx::generate(doc.outline, doc.params, again), "regenerates");
    bool same = true;
    for (size_t i = 0; i < mesh.surfaces[0].vertices.size(); ++i)
        if (mesh.surfaces[0].vertices[i].z != again.surfaces[0].vertices[i].z) same = false;
    check(same, "same seed reproduces the same relief exactly");
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture C — fire_part5.scene, obj2
//
//   24 nodes, MeshType 0, HorizNoise 0, planes +/-15, SurfaceWidth 30.
//   Baked: SurfaceMesh nv 22 nf 20; FrontMesh nv 60 nf 20 AND a second
//          FrontMesh nv 6 nf 2.
//
//   The cap is the finding: 24 nodes should ear-clip to 22 triangles, but the
//   engine wrote 20 (60 vertices). Its clipper rejects ears with a non-positive
//   turn, so the collinear runs in this outline are never clipped and the loop
//   exits with a ring bigger than 3 — leaving a hole in the level's floor. The
//   second FrontMesh is the engine's separate patch for the region it missed.
//
//   boulderx fans the leftover ring, so the cap always tiles its outline.
// ─────────────────────────────────────────────────────────────────────────────
static void fixture_fire_part5_obj2() {
    std::printf("[C] fire_part5 obj2 — 24 nodes with collinear runs\n");
    const double xy[24][2] = {
        {-11.2688, -147.3307}, {1.0480, -147.3307},  {13.3648, -147.3307},
        {13.3648, -75.6318},   {13.3648, -50.6318},  {13.3648, -25.6318},
        {13.3648, -0.6318},    {13.3648, 3.9815},    {18.0955, 12.2677},
        {30.6903, 45.7757},    {13.3648, 32.0638},   {13.3648, 74.3682},
        {13.3648, 147.3307},   {1.0480, 147.3307},   {-11.2688, 147.3307},
        {-11.2688, 74.3682},   {-11.2688, 49.3682},  {-11.2688, 31.2691},
        {-30.6903, 43.9814},   {-17.8449, 11.4025},  {-11.2646, 3.7242},
        {-11.2688, -25.6318},  {-11.2688, -50.6318}, {-11.2688, -75.6318}};

    boulderx::SwdmDocument doc;
    doc.identifier = "obj2";
    for (const auto& p : xy) doc.outline.push_back({p[0], p[1], 15.0, -15.0});
    doc.params.mesh_type = 0;
    doc.params.surface_width = 30.0;
    doc.params.horiz_noise = 0.0;
    doc.params.random_seed = 0;
    doc.params.surface_texture = "crypt_tiles";
    doc.params.front_texture = "crypt_tiles";

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "generates");
    check(!mesh.surfaces.empty() && !mesh.fronts.empty(),
          "MT0: a ribbon and a cap (engine also wrote a 2nd FrontMesh patch for its own hole)");
    // 24 nodes: the ribbon's 48 faces and the cap's 22 arrive in <=20-face
    // chunks, i.e. several submeshes per family.
    check(mesh.surfaces.size() > 1 && mesh.fronts.size() > 1,
          "both families are split into the engine's <=20-face chunks");

    double lo = 0, hi = 0;
    z_range(mesh.surfaces[0], lo, hi);
    check_near(lo, -15.0, 1e-3, "noiseless ribbon back plane = -15 (engine z [-15,15])");
    check_near(hi, 15.0, 1e-3, "noiseless ribbon front plane = +15 (engine z [-15,15])");
    z_range(mesh.fronts[0], lo, hi);
    check_near(lo, 15.0, 1e-3, "cap flat on the front plane (engine z [15,15])");

    check(total_fronts(mesh.fronts) == 22,
          "cap emits n-2 = 22 triangles (engine emitted only 20 — two ears left unclipped)");
    // 2n FACES, not 2n vertices: chunking re-emits the vertices on a chunk seam
    // so the two sides can be drawn independently (the bake does the same), so
    // the vertex total is >= 2n and the face total is exactly 2n.
    check(total_fronts(mesh.surfaces) == 2 * (int)doc.outline.size(),
          "ribbon emits 2n faces across its chunks");
    // Area closure is the property the engine's 20-triangle cap does not have.
    double outline_a = 0.0;
    for (size_t i = 0; i < doc.outline.size(); ++i) {
        const auto& p = doc.outline[i];
        const auto& q = doc.outline[(i + 1) % doc.outline.size()];
        outline_a += p.x * q.y - q.x * p.y;
    }
    outline_a = std::fabs(0.5 * outline_a);
    check_near(total_area(mesh.fronts), outline_a, outline_a * 1e-3 + 1e-6,
               "cap tiles the whole outline (no hole left by collinear runs)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture D — a concave vanilla outline (fire_part1.scene obj3#12, 20 nodes).
// 70% of shipped ground is concave, so this is the common case, not an edge case.
// ─────────────────────────────────────────────────────────────────────────────
static void fixture_concave_vanilla() {
    std::printf("[D] concave vanilla outline — the 70%% case\n");
    const double xy[20][2] = {
        {-52.598, 21.748},  {-61.923, -13.003}, {-143.823, -14.321}, {-148.877, -60.396},
        {-134.058, -101.517}, {-73.765, -172.373}, {76.276, -137.133}, {118.227, -74.527},
        {124.546, -12.288}, {148.877, 30.324},  {148.289, 76.231},   {123.966, 153.966},
        {32.495, 172.373},  {23.606, 98.096},   {35.744, 63.808},    {42.112, 58.114},
        {69.459, -7.827},   {40.433, -46.831},  {-34.626, 9.858},    {-51.4, 27.537}};

    boulderx::SwdmDocument doc;
    doc.identifier = "obj3#12";
    for (const auto& p : xy) doc.outline.push_back({p[0], p[1], 85.0, -85.0});
    doc.params.mesh_type = 0;
    doc.params.horiz_noise = 0.0;
    doc.params.surface_texture = "graveyard_ground";
    doc.params.front_texture = "graveyard_ground";

    // Concavity, measured the way the census measures it: the turn at some
    // vertex points the other way.
    bool pos = false, neg = false;
    for (size_t i = 0; i < doc.outline.size(); ++i) {
        const auto& a = doc.outline[(i + doc.outline.size() - 1) % doc.outline.size()];
        const auto& b = doc.outline[i];
        const auto& c = doc.outline[(i + 1) % doc.outline.size()];
        const double cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (cr > 0) pos = true;
        if (cr < 0) neg = true;
    }
    check(pos && neg, "fixture really is concave");

    boulderx::Mesh mesh;
    check(boulderx::generate(doc.outline, doc.params, mesh), "concave outline generates");
    check(total_fronts(mesh.fronts) == 18, "cap has n-2 = 18 triangles");
    double outline_a = 0.0;
    for (size_t i = 0; i < doc.outline.size(); ++i) {
        const auto& p = doc.outline[i];
        const auto& q = doc.outline[(i + 1) % doc.outline.size()];
        outline_a += p.x * q.y - q.x * p.y;
    }
    outline_a = std::fabs(0.5 * outline_a);
    check_near(total_area(mesh.fronts), outline_a, outline_a * 1e-3 + 1e-6,
               "cap tiles the concave outline");
    // 40 ribbon faces exceed one chunk, so aggregate over the family.
    check(total_fronts(mesh.surfaces) == 40, "ribbon has 2n = 40 faces");
    check(total_verts(mesh.surfaces) >= 40,
          "ribbon has at least 2n vertices (a chunk seam duplicates its boundary)");
}

int main() {
    std::printf("========================================\n");
    std::printf("  boulderx vs shipped engine meshes\n");
    std::printf("========================================\n");
    fixture_beyond_graveyard_obj12();
    fixture_fire_part2_obj3_32();
    fixture_fire_part5_obj2();
    fixture_concave_vanilla();
    if (g_failures) {
        std::printf("FAIL: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("PASS: boulderx matches the shipped ground meshes\n");
    return 0;
}
