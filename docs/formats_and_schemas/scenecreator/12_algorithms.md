# 12. Algorithm Deep-Dive: From Seed to Scene Bytes

> The exact math inside `scene_generator` and the engine code it mirrors.
> Each section: *engine behavior (from decompile)* → *Ruby implementation*.

---

## 1. Deterministic noise — splitmix64

**Engine:** `Caver` uses a seeded `Random()` (LCG). Exact constants **Unknown**
(see doc 10 §7) — the *shape* of the output matters more than bit-exactness.

**Ruby:** `sgen::rng_next(state)` implements **splitmix64**:

```
state += 0x9E3779B97F4A7C15;
z = state;
z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
return z ^ (z >> 31);
```

- Deterministic: same state → same sequence, forever.
- Fast, no allocations, trivially portable (needed for MCP/CLI reuse).
- `rng_float(state, lo, hi)` = `lo + (hi-lo) * (next >> 11) * (1.0/9007199254740992.0)`.

**Guarantee:** same `Blueprint` + same seeds → **byte-identical `.scene`**
(verified by the round-trip probe: 6 objects, stable across runs).

---

## 2. Heightfield → polygon strip

**Engine:** `GenerateSurfaceMesh` samples a profile path (X,Y pairs) and
emits a triangle strip; `GenerateFrontMesh` extrudes to `MaxDepth`.

**Ruby:** `make_heightfield_polygon(heights, n, step, floor_y)`:

```
top edge:   P[i]     = (i * step, heights[i])          for i in 0..n-1
bottom edge: P[n-1+j] = ((n-1-j) * step, floor_y)      for j in 0..n-1
```

Produces a **closed CCW polygon**: top follows the profile, bottom is a flat
line at `floor_y`. This is exactly the silhouette the engine's front-mesh
extrusion would produce — one polygon now, no separate front mesh needed.

---

## 3. `HorizNoise` — baked displacement

**Engine:** `HorizNoise` is a *generation-time* parameter of
`GroundMeshGeneratorComponent`. The engine's `Random()` jitters the surface
as it generates.

**Ruby:** `apply_horiz_noise(poly, amplitude, seed)`:

```
for each vertex v_i:
    d  = hash1D(i, seed)                       // deterministic ∈ [0,1)
    s  = (d - 0.5) * 2.0 * amplitude           // ∈ [-amp, amp]
    n̂  = unit edge-normal at v_i
    v_i += n̂ * s
```

Why **baked** rather than left to the engine: Ruby's editor preview and the
game's runtime render must agree. By applying the noise to the *source
polygon* before serialization — and setting the emitted component's
`HorizNoise` field to 0 — the geometry the game generates from the noisy
`GroundPolygon` is the geometry the editor shows. No editor/game mismatch —
this was a real bug class in earlier mesh work ("looks right in editor,
invisible/broken in game"). Jitter magnitude is clamped to ¼ of the shortest
adjacent edge so pathological amplitudes can't self-intersect or flip
winding.

---

## 4. Round hats — domes on platforms

> The names here come from the engine's rim functions, which are a *different*
> feature: `InsertRoundHatVertices` / `InsertCapForRoundHat` sweep and close the
> rounded RIM of a MeshType 1 slab, and no shipped ground object contains a dome
> (doc 09 §4). The `Hat` dome below is Ruby's own authoring construct — boulder
> emits it into `Hat[` blocks and scene objects, and BoulderX reproduces it — so
> read this section as "the generator's Hat feature", not as a port of those two
> symbols.

**Ruby:** `Platform.hats` → each `Hat{x, y, radius, height}` is appended to
the platform's polygon as a semi-circle dome:

**Ruby:** `Platform.hats` → each `Hat{x, y, radius, height}` is appended to
the platform's polygon as a semi-circle dome:

```
samples = 10 arc points:
    θ from π to 0  (upper half-circle)
    point = (x + radius·cos θ, y + height·sin θ)
```

Defaults (radius 60, height 40) match the ranges the parameter fields are
handed in shipped scenes (`HatWidthOffset`/`HatHeight` neighbours r: 40–80,
h: 20–40). The dome is part of the platform polygon → the same
ground-object builder emits it — collision, texture mapping and geometry all
stay consistent.

---

## 5. Ground object bundle (boulder integration)

`build_ground_object(p, name)` produces the canonical **GroundMesh object**
seen in all 118 shipped scenes:

```
Object{ Identifier: name
  GroundPolygonComponent        { polygon: <platform polygon> }
  GroundMeshComponent           { Depth: p.z }
  GroundMeshGeneratorComponent  { MeshType:0, RandomSeed: p.seed,
                                  SurfaceWidth: p.surface_width,
                                  HatHeight: p.hat_height,
                                  MinDepth: p.min_depth, MaxDepth: p.max_depth }
  CollisionShapeComponent       { ... }
  TextureMappingComponent x2    { top_texture / front_texture }
}
```

The **polygon baking** (actual mesh vertices + UVs + indices) is delegated to
**boulder** (`src/tools/boulder.cpp`), the C++ port of DanielSpaniel's
Boulder engine — itself a faithful port of `Caver::GroundMeshGenerator`. No
mesh code is duplicated: `scene_generator` is composition + assembly, boulder
is the mesher.

---

## 6. Water sheet

`build_water_object(w, name)` emits:

```
Object{ Identifier: name
  WaterMeshComponent { Rect: w.rect, TextureName: w.texture,
                       FrontColor: w.front_rgba, SurfaceColor: w.surface_rgba }
}
```

Default RGBA copied verbatim from a decoded `florennum_cave1.scene` water
object, so generated water is visually faithful without tuning.

---

## 7. Root Bounds (the invisible requirement)

**Engine:** the root `Bounds` (protobuf field 3) is a required scene header —
**118/118 shipped scenes carry it**, but FileRift's text decoder doesn't print
it (it's 20 bytes of fixed32 floats `X, Y, W, H`), which is why it was
"discovered" only during this research.

**Ruby:** `build_bounds_payload(x, y, w, h)` encodes 4 × fixed32 floats.
`generate_scene` auto-computes them from the union of all platform AABBs +
`bounds_pad` (default 200):

```
X = minX - pad     Y = minY - pad
W = (maxX - minX) + 2·pad     H = (maxY - minY) + 2·pad
```

`Result.bounds` reports what was actually emitted so callers can validate.

---

## 8. Spawn placement

If `Blueprint.spawn == {0,0}` (auto):

```
spawn.y = first platform's top edge Y + 56    (Hiro feet clearance)
spawn.x = first platform's left edge X + 80   (slightly inside)
```

`build_spawn_object` emits `SpawnPoint` + `SpawnPointComponent`
(`FacingDirection: p.spawn_facing`, 1 = right — the game convention from
`spawn_default` objects).

---

## 9. Validation checklist (`validate_scene`)

1. Blueprint has ≥ 1 platform.
2. Bounds cover the ground AABB (pad ≥ 0).
3. Spawn is **above** the highest platform top edge (+56 clearance).
4. Water Y is **below** the lowest platform bottom edge.
5. Background and light objects are present.
6. `Result.ok()` → scene bytes non-empty, no error string.

---

## 10. Provenance summary

| Step | Engine source (decompiled) | Ruby implementation |
|------|----------------------------|---------------------|
| Noise | `Caver::Random` (LCG) | splitmix64 (§1) |
| Surface | `GenerateSurfaceMesh` | `make_heightfield_polygon` (§2) |
| Jitter | `HorizNoise` param | `apply_horiz_noise` (§3) |
| Rim | `InsertRoundHatVertices`, `InsertCapForRoundHat` | `Hat` dome (§4) |
| Mesher | `Caver::GroundMeshGenerator` | **boulder** (reused) (§5) |
| Water | `WaterMeshComponent` layout | `build_water_object` (§6) |
| Bounds | root field 3 (fixed32 ×4) | `build_bounds_payload` (§7) |
| Spawn | `spawn_default` layout | `build_spawn_object` (§8) |

Full decompiled engine sources archived in `docs/scenecreator/source/`
(69 files: `GroundMeshGenerator/*.c`, `MeshBuilder/*.c`,
`TriangulatePolygon.c`, `GroundMeshGeneratorComponent/*.c`).
