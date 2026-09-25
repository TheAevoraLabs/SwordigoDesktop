# 11. `scene_generator` — Ruby SDK API Reference

> Reference for `src/tools/scene_generator.h/.cpp` (namespace `sgen`).
> A procedural scene-mesh generator that emits **byte-exact vanilla-compatible
> `.scene` protobuf** — the authoring twin of the engine's
> `Caver::GroundMeshGenerator`.

---

## 1. Why it exists

Swordigo scenes store **seeds and profiles, not vertices** (see doc 10). The
engine generates the mesh at load. `scene_generator` gives Ruby the same
power in reverse: **describe a scene with a blueprint, get complete scene
bytes** that the vanilla loader and the game itself accept.

It reuses **boulder** (`src/tools/boulder.cpp` — the C++ port of
DanielSpaniel's Boulder engine, itself a port of `Caver::GroundMeshGenerator`)
for the geometry baking, and `scene_loader`'s own encoding for protobuf
compatibility.

---

## 2. Data structures

### `Vec2`
```cpp
struct Vec2 { float x = 0.0f, y = 0.0f; };
```

### `Hat` — round-hat dome on a platform surface
```cpp
struct Hat {
    float x = 0.0f, y = 0.0f;      // footprint center
    float radius = 60.0f;          // game default range: 40–80
    float height = 40.0f;          // game default range: 20–40
};
```
An authoring-side dome (see doc 12 §4): it is emitted as an extra `SurfaceMesh`
by boulder and by BoulderX. It does **not** map to `Caver::InsertRoundHatVertices`
/ `InsertCapForRoundHat` — those are the rounded rim of a MeshType 1 slab
(doc 09 §4).

### `Water` — fluid sheet (WaterMeshComponent)
```cpp
struct Water {
    float rect[4] = {0,0,0,0};                        // x, y, w, h (object-local)
    std::string texture = "water";
    float front_rgba[4]  = {0.00f, 0.314f, 0.233f, 0.744f}; // from florennum_cave1
    float surface_rgba[4]= {0.00f, 0.376f, 0.256f, 0.744f}; // from florennum_cave1
};
```
The default colors were copied from a **real decoded water object**
(`florennum_cave1.scene`) so generated water matches the game's look.

### `Platform` — one walkable ground platform
```cpp
struct Platform {
    std::vector<Vec2> polygon;       // CCW closed; if empty → rect used
    float rect[4] = {0,0,0,0};       // x, y, w, h (when polygon empty)
    std::vector<Hat> hats;           // round-hat domes on the surface
    float min_depth = -45.0f;        // depth band (game default)
    float max_depth =  45.0f;
    std::string top_texture   = "fire_grass";
    std::string front_texture = "graveyard_ground";
    uint32_t seed = 1291618994u;     // engine editor default (matches 344 scene instances)
    float horiz_noise = 0.0f;        // baked deterministic vertex jitter
    float surface_width = 80.0f;     // GMG metadata (50–250 observed)
    float hat_height = 25.0f;        // GMG metadata (20–40 observed)
    float z = 0.0f;                  // object Depth
};
```
> `seed = 1291618994` is the **engine's own editor default** — it appears in
> 344 real scene instances, which is why it's the default here.

### `Blueprint` — the full scene description
```cpp
struct Blueprint {
    std::vector<Platform> platforms;
    std::vector<Water> waters;
    Vec2 spawn = {0,0};               // {0,0} → auto (first platform top + 56)
    int spawn_facing = 1;             // 1 = right (game convention)
    std::string background = "grasslandsbackground_day";
    std::string light_name = "DirectionalLight";   // or DirectionalLight_day/_night
    float bounds_pad = 200.0f;        // padding around ground AABB for Bounds
    std::string scene_name = "new_scene";
};
```

### `Result`
```cpp
struct Result {
    std::string scene_bytes;   // complete .scene protobuf (objects + bounds)
    std::string error;
    float bounds[4];           // X, Y, W, H actually emitted
    int objects = 0;
    bool ok() const;           // error.empty() && !scene_bytes.empty()
};
```

---

## 3. Main entry

```cpp
Result generate_scene(const Blueprint& bp);
Result validate_scene(const Blueprint& bp, std::vector<std::string>* messages = nullptr);
```

Pipeline:
1. For each platform: `polygon` → (optional `apply_horiz_noise`) → `hats`
   → `build_ground_object(...)`.
2. Emit background, light, spawn, water objects.
3. Auto-compute root **Bounds** (field 3, four fixed32) from ground AABB +
   `bounds_pad`.
4. Assemble into scene bytes.

`validate_scene` re-checks the blueprint against every invariant that holds
across the 118 shipped scenes (bounds coverage, spawn elevation, water below
ground, background/light presence) and returns `ok()==true` only if all pass;
failures are collected in `messages` for UI display.

**Determinism:** same blueprint + same seed → byte-identical output
(verified by round-trip probe: 6 objects, stable across runs).

---

## 4. Deterministic RNG (splitmix64)

```cpp
uint64_t rng_next(uint64_t& state);
float    rng_float(uint64_t& state, float lo, float hi);
```
splitmix64 — fast, dependency-free, fully deterministic. Used for
`apply_horiz_noise` and any synthetic variation.

---

## 5. Terrain synthesis

```cpp
std::vector<Vec2> make_heightfield_polygon(const float* heights, int n,
                                           float step, float floor_y);
std::vector<Vec2> make_rect_polygon(float x, float y, float w, float h);
void apply_horiz_noise(std::vector<Vec2>& poly, float amplitude, uint32_t seed);
```

- `make_heightfield_polygon` — turns a height profile into a **closed CCW
  strip**: top edge follows `heights[i]` at `x = i*step`, bottom edge is
  `floor_y`. This is the "rolling hills" primitive.
- `make_rect_polygon` — centered rect: `(-w/2,-h/2)..(w/2,h/2)` around `(x,y)`.
- `apply_horiz_noise` — **the engine's `HorizNoise` semantics, baked**: each
  vertex is displaced along its edge normal by deterministic per-vertex noise
  in `[-amplitude, amplitude]`. Seeded → reproducible. Because the
  displacement is applied to the *source polygon*, the final mesh geometry is
  what the game will render — no mismatch between editor preview and runtime.

---

## 6. Object builders (raw scene-object bytes)

```cpp
std::string build_ground_object(const Platform& p, const std::string& name);
std::string build_background_object(const std::string& texture);
std::string build_light_object(const std::string& name, float x, float y);
std::string build_spawn_object(const std::string& name, float x, float y, int facing);
std::string build_water_object(const Water& w, const std::string& name);
std::string build_bounds_payload(float x, float y, float w, float h);  // root field 3
```

Each object builder returns the **root-field-1 encoded Object record**
(identifier + component list), identical to `scene_loader`'s serializer.

- `build_ground_object` → `GroundPolygon + GroundMesh + GroundMeshGenerator +
  CollisionShape + TextureMapping x2` bundle (reuses boulder).
- `build_bounds_payload` → 20 bytes: 4 × fixed32 floats `X, Y, W, H`.

---

## 7. Integration points

| Where | How |
|-------|-----|
| `Makefile` | `src/tools/scene_generator.cpp` added to `RUBY_SRCS` |
| Future UI | "New Scene → Procedural Terrain" dialog in Ruby reads/writes `Blueprint`, calls `validate_scene` before saving |
| MCP/CLI | `ruby_cli` can expose `sgen::generate_scene` as a tool |
| boulder | all geometry math reused; no duplicated meshing code |

---

## 8. Example (from the round-trip probe)

```cpp
sgen::Blueprint bp;
sgen::Platform main_plat;
main_plat.rect = {0, 0, 1400, 900};
main_plat.seed = 12345;
bp.platforms.push_back(main_plat);
// optional hills:
float h[200]; for (int i=0;i<200;i++) h[i] = 60.0f * sinf(i*0.02f);
bp.platforms[0].polygon = sgen::make_heightfield_polygon(h, 200, 14.0f, -350.0f);
// optional water:
sgen::Water w; w.rect = {-300, -280, 600, 60}; bp.waters.push_back(w);

sgen::Result r = sgen::generate_scene(bp);
// r.ok() → write r.scene_bytes to disk; decode-verified loadable.
```Decoded result: `Background`, `DirectionalLight`, `ground1`, `water`, `spawn_default` + root Bounds — structurally identical to a real shipped scene (validated with the same decoder used on the 118 stock scenes).
