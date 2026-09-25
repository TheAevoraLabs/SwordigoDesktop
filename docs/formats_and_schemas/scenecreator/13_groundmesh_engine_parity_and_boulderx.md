# 13 — Ground-mesh engine parity, boulder's limits, and BoulderX

> Verification pass on the ground-mesh subsystem: what `Caver::GroundMeshGenerator`
> actually does (from the arm32/arm64 decompiles *and* the meshes the shipped
> engine baked), where the Go-lineage `boulder` port falls short of it, and what
> the second-generation generator (`src/tools/boulderx.*`) implements instead.
>
> Companions: [09](09_ground_mesh_generator_engine.md) (call graph),
> [12](12_algorithms.md) (scene_generator's side of the same data),
> [05](05_component_catalog.md) (component ids).

---

## 0. Verdict up front

**boulder is a transcription of one vanilla mesh, not an implementation of the
generator.** `get_top_vertices()` in `src/tools/boulder.cpp` builds a fixed
20-vertex block with hardcoded `-10.0`, `-25.0`, `+5.0`, `-5.0` offsets and a
hardcoded `3.0` edge inset; `topIndicesLeft` / `topIndicesMiddle` /
`topIndicesRight` are three fixed triangle tables over that block. The result is
a *left cap + N straight middle quads + right cap*, which is exactly the shape of
the object those numbers were copied from.

Everything it needs beyond that is missing, and two of the gaps are documented in
boulder's own source:

| Gap | Evidence |
|---|---|
| `HorizNoise` never applied — the terrain is flat | `boulder.cpp` header: *"Missing Features … Noise displacement filter (\"HorizNoise\") to displace vertices procedurally (left as 0.0)"*; `get_top_vertices` / `generate_side_mesh` / `make_tri` use `gm.min_depth`/`gm.max_depth` unconditionally, and the emitted generator component writes `write_float_field(6, 0.0f)` |
| Slopes and arcs get **no surface at all** | `is_top_segment()` only accepts an edge whose angle is within `top_angle` (default 20°) of horizontal; the top mesh is generated per "top segment", and `generate_side_mesh` then *skips* those same segments. A curved hill has no such edge |
| Concave tops are unrepresentable | the top is the fixed 3-part template above. Ear clipping exists in `generate_face_mesh` (for the front *cap*) but is never used for the top |
| Depth is one scalar for the whole mesh | `struct GroundMesh { double min_depth, max_depth; double z; }` — the engine builds a per-vertex depth array |
| Dome hats are a guessed half-ellipse | `generate_hat_mesh` uses a fixed `N = 18` arc + two centre fans; the engine's `InsertRoundHatVertices` sweep is a 6-point cross-section |

The existing `boulderx.cpp` ("Zenith Mesh") was **not** trustworthy and is now
replaced. It invented its own model — `EdgeType`, per-node `z_front/z_back`,
`HatDome`, a `.rbc` PNG-with-metadata "MeshPen" container, and its own protobuf
`serialize_to_caver_protobuf` with invented component ids. None of that appears
in the engine or in shipped data. Its `triangulate_polygon` was the one real
piece, and the replacement keeps that idea while re-deriving it from Caver's own
predicates.

> The original Go source was not reachable from this machine (searched the
> checkout and `$HOME`; `~/go/pkg/mod` holds no boulder module, and a web search
> returns nothing). The verdict above therefore rests on the C++ port in this
> repo, which is the thing that actually ships in Ruby.

---

## 1. The engine's generator, re-verified

Read against the **arm32** decompiles where they are better (the polygon
algorithms decompile cleanly there) and arm64 for cross-checking.

| Function | arm32 | Job |
|---|---|---|
| `GroundMeshGeneratorComponent::GenerateMesh` | `0x2D516C` | orchestration + depth arrays |
| `GroundMeshGenerator::GenerateFrontMesh` | `0x2D4858` | the camera-facing cap |
| `GroundMeshGenerator::GenerateSurfaceMesh` | `0x2D3690` | the perimeter ribbon |
| `GroundMeshGenerator::InsertRoundHatVertices` | `0x2D3BDC` | the rounded rim cross-section |
| `GroundMeshGenerator::GeneratePlainSurfaceWithHatGaps` | `0x2D4128` | the inset ribbon |
| `GroundMeshGenerator::InitializeMeshBuilder` | `0x2D3980` | indexed vs non-indexed, vertex layout |
| `Caver::IsConvexVertex` / `PointInsideTriangle` / `IsAnEar` | `0x2D6C78` / `0x2D6E52` / `0x2D6EEC` | ear-clipping predicates |

### 1.1 What `GenerateMesh` actually does

`GenerateMesh` is where the shape of the whole thing is decided. It reads the
target mesh's depth `D`, then:

```
polygon.MinDepth = -D/2 - HatWidthOffset   (MeshType 1)  |  -D/2   (MeshType 0)
polygon.MaxDepth = +D/2 + HatWidthOffset   (MeshType 1)  |  +D/2   (MeshType 0)

for each polygon node i:
    front[i] = +D/2 + HorizNoise * (frand() - 0.5)
    back[i]  = -D/2 - HorizNoise * (frand() - 0.5)
```

**Two independent `frand()` draws per node, and the result is a per-node depth
array.** That single fact is why the terrain of a shipped level is 3D and why
boulder's flat output cannot look like it.

It then emits, in this order:

```
AddFrontMesh ( GenerateFrontMesh )        -- always
AddSurfaceMesh( GenerateSurfaceMeshWithRoundHat )   -- MeshType 1
AddSurfaceMesh( GeneratePlainSurfaceWithHatGaps )   -- MeshType 1
AddSurfaceMesh( GenerateSurfaceMesh )               -- MeshType 0
```

and finally rewrites `GroundMeshComponent.LocalAabb` from the polygon bounds
outset by `0.4 * HatHeight` (verified numerically in §3).

### 1.2 The three submeshes

**`FrontMesh` (field 9) — the camera-facing cap.** `InitializeMeshBuilder(args,
/*indices=*/false)`, so it is a **non-indexed** triangle soup (shipped scenes
have an empty `IndexData` on it). It ear-clips the outline
(`Caver::TriangulatePolygon`) and for each ear emits three vertices, each taking
its z from **its own node's `front[]` entry**, with the face normal
`normalize(cross(V1-V0, V2-V1))` and UV `(x/Scale + 0.5, y/Scale + 0.5)`.

**`SurfaceMesh` (field 8) — the perimeter ribbon.** Two vertices per node, back
plane then front plane, sharing the node's XY vertex normal (the normalized sum
of its two adjacent edge normals). The index pattern is the engine's own quad
split, visible verbatim in shipped data as `[0,2,3, 1,0,3, 2,4,5, 3,2,5, …]`.

**`SurfaceMesh` (field 8, MeshType 1) — the rounded rim.** `InsertRoundHatVertices`
sweeps a 6-point cross-section recorded in `(inward offset, z)` space. Verified
at `beyond_graveyard`'s corner `(186.595, 99.662)` with `D/2 = 100`,
`HatHeight = 25`, `HatWidthOffset = 5`:

```
rho = 0        z = -105 / +105     n = ∓outward XY   (the outer edge, on the polygon)
rho = 0.4·H    z = +110 / -110     n = +Z            (the bevel bulge)
rho = H        z = +105.1 / -104.9 n = ±outward XY   (the inner wall)
```

`+0.1` is the engine's anti-z-fighting nudge, reproduced including its sign
asymmetry.

### 1.3 Ear clipping, precisely

The engine's predicates are:

```c
// Caver::IsConvexVertex — polar-angle comparison of the two edge vectors
d = wrap(angle(next-cur) - angle(cur-prev));   convex iff d <= 0        (engine)
// Caver::PointInsideTriangle(a,b,c,t)
(a-c)×(t-c) >= 0 && (b-a)×(t-a) >= 0 && (c-b)×(t-b) >= 0
// Caver::IsAnEar(i) — uses a CACHED convexity flag, then the test above
```

Two consequences worth stating, because they are observable:

* `>=` means **inclusive**: a point exactly on an edge blocks the ear.
* A **collinear vertex is never convex**, so it is never clipped. The engine's
  loop can therefore exit with a ring bigger than 3, silently leaving holes.
  This is not theoretical — see fixture C in §3.

#### 1.3.1 What the engine's *selection rule* is not (measured)

The rules above describe one ear test. They do **not** identify which ear the
engine takes next, and on a serpentine cave wall that choice is the whole shape of
the cap. `thecave_part15.scene` `obj9#10` is the object this was pinned on — 59
nodes, a big non-convex outline, `MeshType 1`, `HorizNoise 250`:

* the bake's cap is a **complete, non-overlapping triangulation** of the outline:
  57 = n−2 triangles whose areas sum to the polygon's own shoelace area to the
  last float, and the outline has no self-intersections;
* it is **not** any reading of the ear-clip walk tested: 708 parameterisations
  (start vertex × ring direction × 3 rule for where `current` moves after a clip ×
  2 rules after a rejection) share at most **18 of the 57** triangles with the
  bake, and the narrower sweep over start × direction × inclusive/strict inside
  test peaks at 8; minimum-weight (Klincsek) reproduces 3, greedy
  shortest-diagonal 0, and Delaunay is wrong outright — **50 of the 57** bake
  triangles violate the empty-circumcircle property;
* what the bake *does* pin is concentration. Its busiest node carries **8 of 57**
  cap triangles, spread over several apexes (1, 2 and 43 hold 8 each).

The port used to reproduce the recovered `Polygon::Triangulate` walk, which
resumes at the *previous* vertex after every clip — so one apex absorbs every clip
it can, and on this object **node 10 carried 29 of the 57** cap triangles. That is
exactly the reported symptom: "the space between the first node and the last is
crumbled, every curvature and depth-different vertex crammed into one place",
visible as a starburst of cap triangles converging on a single XY point.

`boulderx::triangulate` therefore cuts at the **shortest valid diagonal**,
recursively: candidates are visited shortest-first (so the first valid one is the
declared one), a diagonal must not cross or touch a non-incident edge, and its
midpoint must lie inside the current sub-ring — the midpoint test is what rejects a
segment that crosses nothing but runs outside a concave throat. A sub-ring with no
valid diagonal falls back to an ear clip (Two Ears Theorem), and a fan only if even
that fails, with the area gate as the final arbiter.

On `obj9#10` the difference is measurable three ways (`.scratch/surface_delta.py`,
both meshes' cap surfaces interpolated over a 41×41 grid of the object's own
footprint):

| | fanning walk (before) | shortest-diagonal split | bake |
|---|---|---|---|
| busiest node's share of the cap | **29 / 57** | **10 / 57** | 8 / 57 |
| samples the cap fails to cover | 136 / 582 (23%) | **68 / 582 (12%)** | — |
| \|Δz\| against the bake: mean / median / p95 / max | 42.45 / 24.60 / 130.93 / 176.64 | **26.50 / 19.63 / 87.03 / 142.29** | — |

The coverage column is the crumple itself: a fan's long slivers fold over each
other, so a quarter of the footprint has no cap triangle over it at all. The
residual is a faithful *look*, not identical triangles — the engine's selection
rule is still unrecovered, and that is stated rather than papered over.

---

## 2. Corpus measurement

`.scratch/ground_gen_census.py` walks all 202 scenes in
`Scener/data/decoded_scenes` and decodes each ground object's polygon, generator
component, texture mappings and every submesh. **2,538 ground objects:**

| Measurement | Result |
|---|---|
| polygons `Closed : 1` / `Convex : 0` | **2,518 / 2,518** (no exceptions) |
| **concave** polygons | **1,770 (70%)** |
| `MeshType 1` (rounded rim) | 2,070 (81%) |
| `MeshType 0` (plain) | 468 (19%) |
| `HorizNoise` on the generator | 1,859 zeros; non-zero values 50 / 250 exist |

Submesh shape by `MeshType` (most common):

```
MT=1  FrontMesh 6x2 ; SurfaceMesh 8x6 ; SurfaceMesh 20x14      200 objects
MT=1  SurfaceMesh 21x20                                        212
MT=1  SurfaceMesh 30x20                                        180
MT=0  SurfaceMesh 22x20                                         99
MT=0  FrontMesh 6x2 ; SurfaceMesh 8x8                           89
MT=0  FrontMesh 42x14 ; SurfaceMesh 14x12 ; SurfaceMesh 22x20   42
```

Texture routing, measured across all of them: `SurfaceMesh` → the
**Surface**TextureMapping (1,366 explicit, 752 where both mappings name the same
texture); `FrontMesh` → the **Front**TextureMapping (545 explicit). The engine's
field numbers are 3 (`FrontTextureMappingId`) and 4
(`SurfaceTextureMappingId`); `beyond_graveyard` obj12 shows them as
`103 → graveyard_ground` and `104 → lowergrove_grass`.

---

## 3. Fixtures used by `tests/boulderx_engine_parity_test.cpp`

Each is a real object copied out of a decoded scene together with the values its
baked mesh actually has. These are the authority the test pins against.

**A — `beyond_graveyard.scene` obj12** (`MeshType 1`, 4 nodes, `HorizNoise 0`,
`D/2 = 100`, `H = 25`, `W = 5`, polygon `±105`):

| engine submesh | engine values | boulderx |
|---|---|---|
| `SurfaceMesh` #1 (rim) | nv 20, nf 14, z `[-110, 110]`, surface texture | z `[-110, 110]` ✓, 24 verts (**DEVIATION**) |
| `SurfaceMesh` #2 (inset) | nv 8, nf 6, z `[-100, 100]`, front texture | nv 8 ✓, z `±100` ✓ |
| `FrontMesh` | nv 6, nf 2, z `[100, 100]`, front texture | nv 6, nf 2, z `100` ✓ |
| `LocalAabb` | polygon outset by `0.4 * HatHeight` | same rule ✓ |
| stored polygon depth | `±105` (= `D/2 + W`, **not** the bevel tip) | same rule ✓ |

**B — `fire_part2.scene` obj3#32** (`MeshType 0`, 7 nodes, `HorizNoise 50`,
`D/2 = 10`): engine `SurfaceMesh` nv 14 / nf 14 with z `[-28.523, 28.695]`,
`FrontMesh` nv 15 / nf 5. boulderx reproduces both counts exactly. The z range is
the proof that `HorizNoise` is applied — it runs to ±28.7 on a mesh whose polygon
says ±10, and the cap's `+28.695` is only reachable from the **front** array.

**C — `fire_part5.scene` obj2** (24 nodes, collinear runs): the engine wrote a
**20-triangle** cap (`nv 60`) where 24 nodes should give 22, plus a *second*
`FrontMesh` (nv 6, nf 2) patching the region it missed. This is §1.3's
never-clipped-collinear-ear behaviour, visible in shipped data. boulderx emits 22
triangles and asserts its cap's total area equals the outline's.

**D — `fire_part1.scene` obj3#12** (20 nodes, concave): the 70% case, asserted
for `n-2` triangles and area closure.

---

## 4. What BoulderX implements

`src/tools/boulderx.{h,cpp}` — a faithful port, structured like the engine:

```
generate()
  ├─ depth arrays   <- GenerateMesh        (0x2D516C)
  ├─ build_cap()    <- GenerateFrontMesh   (0x2D4858)
  ├─ build_ribbon() <- GenerateSurfaceMesh (0x2D3690)
  └─ build_rim()    <- InsertRoundHatVertices (0x2D3BDC)
```

What changed versus boulder, in capability terms:

1. **Concave outlines work.** Ear clipping uses Caver's predicates verbatim
   (polar-angle convexity, inclusive `PointInsideTriangle`). 70% of shipped
   ground is concave, so this is the common case, not an edge case.
2. **Arcs and slopes work.** `append_arc` / `append_polyline` build true outlines;
   there is no `top_angle` heuristic anywhere, and no fixed vertex block. A
   semicircular hill generates a full, closed mesh.
3. **Per-node depth.** `Node{ x, y, front_depth, back_depth }` replaces the scalar
   pair. The cap, the ribbon and the rim all read the per-node arrays, so a tilted
   or noisy outline produces genuinely 3D geometry.
4. **`HorizNoise` is implemented** and deterministically seeded, and the emitted
   `GroundMeshGenerator` carries the real value where boulder hardcodes `0.0f`.
5. **No holes from collinear vertices.** Where the engine's loop stalls and
   leaves the ring unclipped, boulderx fans the remainder — more triangles, never
   fewer — then *verifies* that the emitted triangles tile the outline and
   refuses generation if they do not. A rejected outline produces no object at
   all rather than a solid-but-invisible one.
6. **Engine structure preserved**, in engine order: `SurfaceMesh` #1 = rim
   (surface texture), #2 = inset ribbon (front texture), `FrontMesh` = cap (front
   texture); `FrontMesh` non-indexed; `LocalAabb` outset by `0.4 * HatHeight`;
   polygon depth = `D/2 + W` for `MeshType 1`.

### Deliberate deviations

| Deviation | Why |
|---|---|
| Rim tessellates **every** node (6 profile points each) instead of eliding rings to convex corners | the engine's count is an artefact of vertex reuse; the cross-section geometry and z envelope are identical, and the test pins those |
| The ribbon **closes** its last segment; the engine leaves one open (`obj12` inset: 6 faces for 4 nodes) | leaving a seam open in a level's floor is a bug, not a contract |
| `HorizNoise` uses an LCG, not the engine's exact `frand()` | the recurrence is not recoverable from the decompile; the engine's guarantee is determinism, which is preserved, and 1,859 of 2,223 shipped generators have `HorizNoise = 0` anyway (same reasoning as [12](12_algorithms.md) §1 for scene_generator's splitmix64) |
| Collinear-heavy outlines get a **complete** cap (22 triangles for fixture C vs the engine's 20) | see §1.3 |

### Dome hats (implemented — and not engine geometry)

The plan this section used to carry was "port `InsertCapForRoundHat`". Measuring
first changed the plan, so the measurement is recorded here rather than the guess:

* **There is no dome in the engine.** `InsertRoundHatVertices` /
  `InsertCapForRoundHat` sweep and close the rounded **rim** of a MeshType 1 slab.
  Across all 2,538 ground objects in the 202 decoded scenes, the largest amount any
  baked vertex rises above its own polygon's top edge is **2.04**, and all 154 such
  vertices sit **exactly 3.000** from a **convex** node — the rim's corner shift. A
  `HatHeight` dome would rise 20-40. `.scratch/ground_dome_probe.py` is that
  measurement; doc 09 §4 carries the correction.
* **The dome is boulder's.** `Hat` + `generate_hat_mesh` (src/tools/boulder.cpp) is
  an authored feature with no engine counterpart, written into sheets as a `Hat[`
  block and into scenes as one extra `SurfaceMesh` per hat. So a generator that
  dropped it would delete domes from a level on a generator switch — which is what
  the old refusal was guarding against, one refusal too broadly.
* **BoulderX now emits it.** `boulderx::Hat` + `build_hat()` reproduce boulder's
  geometry (footprint circle, base 0.05 above the top edge, rising `height`,
  spanning the sheet's depth), structured the way the engine divides the same work:
  the swept profile ring, then the closing cap. A `Hat[` block in **either**
  dialect is read as geometry (`SwdmDocument::hats`), written back out, and passed
  through every entry point, so nothing is silently dropped anywhere:

  | Step | Result |
  |---|---|
  | boulderx opens a sheet with `Hat[` | domes read into `SwdmDocument::hats` |
  | boulderx saves a sheet | the block is written back (hatless sheets unchanged) |
  | boulderx generates an object | one `SurfaceMesh` per hat, after the rim and the ribbon, and the object's `LocalAabb` is extended to contain it (a dome rises *outside* the polygon, where an Aabb that stops at the polygon can get it culled) |
  | the generators are switched | both emit the same dome, so the level does not change |

The Ruby Touch studio carries a sheet's hats through load → edit → export
(`RubyToolsBridge` `domeHats`, shown as "N dome hat(s) preserved"); the 2D canvas
cannot draw a dome, so they are preserved rather than edited.

---

## 4b. The per-node depth array: recomputed, not recovered

An edit has to start from the object's own Z, not from one scalar — but it must
not start from the **bake** either. §1.1 already says why: `GenerateMesh` does not
read a per-node depth from anywhere, it *computes* one from the generator's own
parameters. So the editors call `boulderx::engine_node_depths()`, which
reproduces the law:

```
front[i] = SurfaceWidth/2 + HorizNoise * (frand() - 0.5)
back[i]  = -SurfaceWidth/2 - HorizNoise * (frand() - 0.5)
```

two draws per node off one seeded sequence. Inputs: `SurfaceWidth`, `HorizNoise`,
`RandomSeed`, node count/order — all of which a ground object carries. No XY
matching, so no node can fail to match and no node can collapse onto another's
depth.

### Why recovery could not do it (measured)

`recover_node_depths()` used to take **the highest matching z as the front depth
and the lowest as the back**. On a MeshType 1 object that is the *rim's* plane,
not the ribbon's, and `build_rim` then adds `HatWidthOffset1` to whatever it is
handed — so the rim landed a full `HatWidthOffset1` (5.0 on the caves) further
out than the engine puts it, and `+HatWidthOffset2` again at the bevel. Measured
on `thecave_crypt2 obj5` (SurfaceWidth 180, W1 5): an object whose own envelope is
`±100` regenerated at `[-105, 100]`.

It is not fixable by a smarter match, because at a **rim node the ribbon yields
that node to the rim** (`GeneratePlainSurfaceWithHatGaps`) and the bake carries
no plain-plane *back* vertex there at all. Measured on `fire_part2 obj3#15`
(SurfaceWidth 150, HorizNoise 50, seed 0, 36 nodes): the law's front plane is
present in the bake at **36/36** nodes, its back plane at **33/36**, and each of
the 3 misses is exactly **5.00** — `HatWidthOffset1` — too deep.
`.scratch/rim_noise_probe.py` is that run.

So `recover_node_depths()` is now the **fallback** (a bake whose generator is
gone), and it returns the **plain plane** — the deepest non-positive z and the
shallowest non-negative one on the node's column, which is the same rule
`tools/ground_mesh_truth.py`'s `ribbon_depths()` measures — because the plain
plane is what everything else is measured from: the ribbon and the cap sit on it,
and the rim's bevel and inner wall are offsets *from* it. At a rim node it is
still `HatWidthOffset1` too deep on the back and says so in its header.

### Consequences worth stating

* Both front-ends now generate with `Params::DepthSource::Outline` and an outline
  built from the law, so desktop and mobile produce the *same* mesh — which was
  not true while one recovered and the other re-derived.
* The outline's depths already contain the object's `HorizNoise`, so generation
  must not apply it a second time — while the object's generator component must
  still be written back verbatim. `Params::emitted_horiz_noise` decouples those
  two: `< 0` means "same as `horiz_noise`"; a set value is what goes into field 6.
* `clearGroundRelief()` in the editors means *"back to the object's own depths"*,
  not "flatten" — and "the object's own depths" are the law's, so a reset is a
  no-op on an unedited object rather than a slab.

### Corpus result

Twelve of the largest shipped cave ground objects, each regenerated and diffed
against its own bake (`tests/ground_mesh_compare_test.cpp`, driven from
`tools/ground_mesh_truth.py`): **per-node depth error 0.000 on all 12**, Z envelope
ratio **1.00x** on 10 and **0.99x** on 2, UV `v` and the cap's `u`/`v` exact.

---

## 5. How it is wired

`boulderx::ground_generator()` returns a two-value `GroundGenerator` enum
(`Boulder` | `BoulderX`), defaulting to **Boulder**. Boulder is the default
because it always returns a mesh; BoulderX is opt-in from either picker and is
the engine-faithful generator — its recovered per-node depth is what keeps a
vanilla object's relief — and it stays the automatic fallback when *it* declines
something it cannot reproduce. `RUBY_GROUND_GENERATOR=boulderx` (or `=boulder`)
overrides the *initial* value only — a saved choice from a picker calls
`set_ground_generator()` afterwards and wins, so the environment variable is a
debugging default rather than something that overrides the user every launch.
`ground_generator_id()` / `ground_generator_from_id()` are the persistence pair;
the latter is total and biased the same way, so only the literal `boulderx`
selects BoulderX — a stale or absent config lands on the default rather than
silently switching a scene to the non-default generator.

### 5.0 Objects with no polygon cannot be regenerated

A generator-driven ground object may ship **without a `GroundPolygonComponent`**
at all: it has baked `GroundMesh` submeshes but no outline. `thecave_part1411` is
19 such objects, one carrying 20 submeshes spanning `x[-2079, 2076]`.

Both editors used to synthesise an outline for these by scraping the **unique XY
of the baked meshes**, capped at 64, stopping at the first submesh. That is not
the polygon: it mixes the rim's inward-offset bevel and inner-wall points in, and
the first submesh is itself a rim patch — so the "outline" was a small inward
blob at one end of the level. Regenerating from it emitted geometry strung
between the first and last invented node and discarded the real ground.

That fallback is removed. When no outline is recoverable, `meshImport()` /
`mesh_import()` leave the point list short, the editor refuses to arm Mesh Edit,
and the object is left untouched — which is what the early return did before the
fallback was added. The baked mesh is the only faithful record of these objects
and the renderer already draws it verbatim (§6 note below), so leaving it alone
is the correct behaviour rather than a limitation to work around.
`.scratch/ground_roundtrip_probe.cpp` replays the import-and-regenerate path on a
real scene and prints the vanilla-vs-regenerated diff.

The picker lives in **one** place per front-end, and both write the same switch:

| Front-end | Picker |
|---|---|
| Ruby Touch (mobile) | Settings → *Ground Mesh Generator* (`SettingRow` segmented choice → `ruby_settings_bridge`) |
| Ruby GG (desktop) | Ground Mesh Studio → *Generator* combo (`ground_mesh_studio.cpp`) |
| ImGui `ruby` | none — boulder permanently, by design (deprecated front-end) |

Call sites that *generate*:

* `src/ruby/viewport/viewport_3d_widget.cpp` — Ruby GG `mesh_apply()`
* `src/ruby/android/ruby_quick_viewport.cpp` — Ruby Touch `regenerateGroundMesh()`
* `src/ruby/tools/ground_mesh_studio.cpp` — desktop studio `add_to_scene()`
* `src/ruby/android/ruby_tools_bridge.cpp` — mobile studio sheet export

Every one of them keeps boulder as the fallback, so a refusal (or Boulder being
selected) behaves exactly as before, and every one passes the per-node outline
plus the generator parameters recovered on import:

```cpp
std::string bin;
if (boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX) {
    // The object's own dome hats travel with the outline. boulderx.h never
    // includes boulder.h, so the identical hat structs are converted here
    // rather than aliased; the two generators then emit the same domes.
    std::vector<boulderx::Hat> hats;
    for (const auto& h : gm.hats) hats.push_back({h.x, h.y, h.radius, h.height});
    bin = boulderx::generate_ground_mesh_object_nodes(outline, params, name, hats, ids6);
}
if (bin.empty()) {                      // refusal, or Boulder selected
    bin = boulder::generate_ground_mesh_object(
        boulder::serialize_swdm(gm), name, z, &ids);
}
```

`last_decline_reason()` (boulderx) and `boulder::last_parse_error()` (boulder) are
consulted only for the generator that actually failed, and the mesh-edit HUD
reports which generator produced the mesh — a refusal that reaches the user must
be the refusal that just happened.

### 5.1 `.swdm` compatibility is one-way

`src/tools/swdm_format.h` defines both dialects and the marker, and both readers
and both writers go through it so they cannot drift:

| Sheet | Written by | Boulder | BoulderX |
|---|---|---|---|
| v1, `MinDepth`/`MaxDepth` + `Vertex[ x y ]` | boulder always; the studios when the sheet is a uniform slab and Boulder is selected | reads | reads (as a uniform slab) |
| v2, `FormatVersion : 2` + `Generator : 'boulderx'`, one `Node : x y front back` per vertex | boulderx always; the studios whenever there is per-node Z to keep | **refuses**, with `last_parse_error()` naming the generator to switch to | reads |

A `Hat[` block is orthogonal to the dialect: both writers emit it only when the
sheet actually has domes, both readers turn it into geometry, and the Ruby Touch
studio carries it through load → edit → export untouched (its 2D canvas cannot
draw a dome, so it preserves rather than edits them).

Refusing v2 in boulder mode is the point of the marker: boulder's format has one
depth for the whole sheet, so reading a v2 file would silently discard the relief
a modder authored — a valid file reported as corrupt is a much better failure than
a valid file quietly flattened.

---

## 6. Tests

| Test | What it pins |
|---|---|
| `boulderx_test` | L-shape / U-trough / comb / spiral triangulation with area closure; a real 20-node concave vanilla outline; arcs; per-node depth; `HorizNoise` relief **and** determinism (same seed → identical, different seed → different); `MeshType 0/1` structure and texture routing; `.swdm` round-trip; binary emission incl. refusing a collinear outline; **the dialect gate** (§5.1 — v1 loads in both, v2 is refused by boulder and says why, a real `Hat[` entry comes back as geometry and generates, boulder's empty `Hat[` block is not mistaken for a dome); **depth recovery** (§4b — per-node depths come back out of a baked mesh, an inset rim vertex does not match, a non-matching mesh recovers nothing, an unmatched node keeps the recovered range, and all four depths survive generation); **dome hats** (§4 — one `SurfaceMesh` per hat with the base on the top edge, the apex exactly `height` above it, the authored footprint width, the sheet's own depth span, a degenerate hat contributing nothing, the existing submeshes unrenumbered, the `LocalAabb` reaching the apex, and a hatless object keeping the polygon's own `LocalAabb`) |
| `boulderx_engine_parity_test` | fixtures A–D above, i.e. boulderx output against the meshes the 1.4.13 engine baked into shipped scenes |
| `ground_mesh_studio_test` | simple-polygon enforcement + the studio's *Add to Scene* object round trip (the studio now writes either dialect) |
| `ground_mesh_compare_test` | the corpus harness: reads a TSV from `tools/ground_mesh_truth.py` and diffs a regeneration of **a real shipped object** against its own bake — Z envelope against the polygon's `MinDepth`/`MaxDepth` (including the `SurfaceWidth/2 + HatWidthOffset1` write-back), per-node depth, strip order/routing, the UV laws, **cap concentration** (the busiest node's share of the cap triangles, held to the bake's +6, which is the guard against the fanning walk of §1.3.1 coming back), and both depth sources side by side. With no argument it runs the hand-checked fixtures *plus* every checked-in shipped object — `tests/data/ground_truth/fire2_obj3_15.tsv` (MeshType 1 with noise, which the fixtures cannot express) and `thecave_part15_obj9_10.tsv` (the serpentine 59-node wall, the only fixture that can expose a cap that fans). `tools/ground_mesh_view.py` renders the two meshes' OBJ exports (both sides write one: `truth.py obj`, the harness' `--obj`) into a four-view sheet. |

Both were run against the whole suite: `89% tests passed, 6 tests failed out of
53` (three Not Run, from the pre-existing `libswpod` → `filerift` link gap). The
failures are pre-existing and unrelated —
`pod_game_reader_contract_test` wants a user asset
(`~/.local/share/swordigo-desktop/assets/resources/hiro_run.POD`) that is absent,
and `scene_save_preserves_edit_test` / `scene_undo_test` segfault in
`ViewportShader::setProj` from `paintGL()`, the known offscreen-GL fault.

---

## 7. Open items

1. **The rim's convex-corner shift (3.0)** — *implemented, direction now
   measured.* `GenerateSurfaceMeshWithRoundHat` pushes the rim's ring at each
   *convex* node 3.0 along a neighbouring edge direction when
   `VertexAtIndexIsConvexCCW` holds, and shipped data confirms the magnitude
   exactly (the 154 vertices that rise above a polygon's top edge, in 43
   submeshes, are all `3.000` from a convex node). The port shifts the node
   **backwards along its incoming edge** (the direction the edge arrives from), and
   the corpus diff supports that: on `thecave_part15 obj9#10` the regenerated
   vertex set equals the bake's at **53 of 59 nodes exactly**, the other six to
   0.004 of float noise, and that comparison includes every rim vertex. The
   earlier "away from previous / away from next" split (73 vs 81 of 154) is what
   that reading resolves into when the normal's *orientation* is taken from the
   ring's traversal rather than from the edge alone. The remaining ~2% Z-span
   overshoot noted in §4b is this shift on the deepest node's bevel.
2. **The `InsertPlainSurfaceVertices` gap rule** — `GeneratePlainSurfaceWithHatGaps`
   emits the plain surface as *runs* separated by gaps, each boundary inserting the
   vertex pair again with the segment normal instead of the bisector, and it
   resamples the path by `SurfaceWidth` (fixture C's 24-node polygon produced an
   11-point ribbon). Porting the gaps therefore depends on open item 3 below: the
   duplication is real (285 submeshes carry a same-position/different-normal pair)
   but the normal it uses at a split is not recoverable from the decompile with
   confidence yet, and shipping a guessed split would break fixtures A and B, which
   pin the ribbon's `2n` counts. `build_ribbon` keeps its closed loop until then,
   as a documented deviation.
3. **Vertex elision parity** — reproduce the engine's ring re-use so the shipped
   `nv`/`nf` counts match exactly, if byte-level diffability matters.
4. **`HorizNoise` bit-exactness** — the PRNG is recovered and reproduces a
   shipped bake's depths exactly (`boulderx::Frand`; `Caver::fastrandom`'s two
   halves with the `(seed << 16) | 0x330E` / `seed >> 16` initialisation), so
   this item is closed for every object whose `SurfaceWidth`/`HorizNoise`/seed
   survive in its generator component. What is still open is the *edge* case:
   `seed == 0` is treated as `1`, which is not verified against the engine — and
   seed 0 is what every object in the caves carries.
5. **`SurfaceWidth`** is currently carried through untouched; the engine uses it
   to resample the surface path (it is why a 24-vertex polygon in fixture C
   produced an 11-point ribbon). Resampling is not implemented, and it is the
   blocker for open item 2.
6. **Desktop viewport relief buttons** — the mobile mesh-edit HUD and both
   studios have a *Randomise Z* control (`boulderx::randomise_node_depths`), but
   the Ruby GG viewport only carries law-derived and imported depths; its toolbar
   has no relief action yet.
7. **The ribbon's `u` (arc-length) parameterisation — newly open.** The baked
   mesh carries **two** `u` laws per node: the cap's is planar
   (`0.5 + x/scale`, confirmed exactly on every corpus object) and the ribbon's is
   arc length along the outline. `build_ribbon` uses `u = 0.5 + arc/scale` with
   `arc` accumulated from outline node 0, and against the bake that is *structurally*
   right — the spacing between consecutive nodes matches — but it is offset by a
   constant: on `thecave_crypt2 obj5` the bake's ribbon `u` implies an arc start of
   **5906 of a 28108-unit ring** (21%), where this generator starts at 0. Which
   node the engine starts counting from is not recovered, and the offset is not
   the same fraction across objects (21% here, 30% on `snowy_cave2 obj1#8`), so it
   is not a fixed node index. It is a **texture scroll along a wall, not a shape
   difference**, so it is reported rather than asserted: the harness prints the
   residual and `tools/ground_mesh_truth.py`'s `nodeu` section is the instrument
   (`.scratch/u_vs_arc.py`, `.scratch/u_start.py`). Note the old check — "does the
   generated `u` agree with its own arc" — cannot see this at all, because a
   constant offset is self-consistent; it was reporting 0.0000 on meshes whose `u`
   differs from the bake's by 43.
