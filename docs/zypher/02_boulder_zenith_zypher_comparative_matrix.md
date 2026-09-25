# 02 — Boulder, Zenith, and Zypher: Three-Generation Architectural Audit

## 1. Evolution Overview

The ground mesh generation ecosystem in Ruby and Swordigo spans three distinct architectural generations:

```
  Generation 1: BOULDER (2020-2023)
  └── Origin: DanielSpaniel's initial C++ port of legacy Go scripts
  └── Role: The fast, flat, rigid platformer slab builder

  Generation 2: ZENITH / BoulderX (2024-2026)
  └── Origin: Strict reverse-engineering of libswordigo 1.4.13 decompilations
  └── Role: The Caver-faithful engine-parity engine for vanilla level preservation

  Generation 3: ZYPHER / RubyMesh (2026+)
  └── Origin: Modern computational geometry & generalized 3D procedural synthesis
  └── Role: The next-generation boundless world mesh engine
```

---

## 2. Feature & Capability Matrix

| Capability | Gen 1: Boulder | Gen 2: Zenith (BoulderX) | Gen 3: Zypher (RubyMesh) |
|---|---|---|---|
| **Underlying Math** | Fixed 20-vertex hardcoded quad strip | Decompiled Caver Ear-Clipping & 6-point sweep | Continuous Catmull-Rom/B-Splines, Constrained Delaunay, Parametric Manifolds |
| **Polygon Topology** | Convex-only, horizontal segments ($\le 20^\circ$) | Arbitrary closed 2D CCW polygons (concave & convex) | Complex 2D/3D polygons, splines, open curves, volumetric manifolds |
| **Curvature & Slopes** | Completely refused (0 vertices generated for slopes) | Segmented polyline approximation | Continuous adaptive curvature sampling ($C^1/C^2$ smoothness) |
| **Edge Bevel Profile** | Fixed hardcoded offsets (`-10, -25, +5, -5`) | Fixed 6-point decompiled rim (`prof[6]`) | Parameterized profile curves (Fillet, Chamfer, Stepped, Gothic, Inset) |
| **Front Face Mesh** | 2-triangle flat rectangular quad | Ear-clipped flat triangulation across boundary | 3D Bas-Relief with interior Steiner points & procedural displacement |
| **Depth Representation** | Single scalar `min_depth` / `max_depth` pair | Per-node `front_depth[i]` / `back_depth[i]` arrays | Full 3D coordinates $(x, y, z)$ per vertex + 3D vector fields |
| **Procedural Noise** | Hardcoded to `0.0` (flat) | Engine LCG `fastrandom` jitter on boundary nodes | Multi-octave Simplex fBm, Voronoi/Worley rock strata, domain warping |
| **Tessellation Limit** | Fixed 20 vertices | $N$ nodes $\times$ 6 profile points | Dynamic LoD (adaptive subdivision based on screen error/curvature) |
| **Material / Biome Routing** | 1 surface texture, 1 front texture | 1 surface texture (rim), 1 front texture (ribbon+cap) | Slope-aware biome partitioning (Grass on flats, Scree on slopes, Crag on cliffs) |
| **3D Primitives** | None | Dome hats (boulder v1 emulation only) | Full 3D primitives (UV Sphere, Geodesic Icosahedron, Torus, Arch, Helix) |
| **External Model Import** | Impossible | Impossible | Native OBJ/GLTF compilation directly into embedded `GroundMesh` |
| **Collision Generation** | Author's 2D input | Author's 2D input | Automatic 2D silhouette hull extraction for any 3D geometry |

---

## 3. Detailed Component Deep-Dive

### 3.1 Boulder (Gen 1) — Why it hit a wall
- Boulder was essentially a hardcoded cloner of a single vanilla platform object.
- Its function `get_top_vertices()` literally placed 20 hardcoded float offsets:
  ```cpp
  // From boulder.cpp
  vertices[0] = {left - 3.0, top, min_depth + 10.0, ...};
  vertices[1] = {left, top, min_depth + 25.0, ...};
  ```
- Because of `is_top_segment()` checking `fabs(angle) < 20.0 * M_PI / 180.0`, any hill steeper than $20^\circ$ generated **no walkable ground whatsoever**.
- Arcs, circles, vertical walls, and concave polygons were impossible.

### 3.2 Zenith / BoulderX (Gen 2) — The Parity Master
- Zenith solved the preservation problem by transcribing the exact ARM32/ARM64 decompilations:
  - Corrected ear-clipping triangulation with Caver predicates (`IsConvexVertex`, `PointInsideTriangle`, `IsAnEar`).
  - Implemented exact per-node depth arrays matching `seed_fastrandom` and `frand`.
  - Reconstructed the exact 6-point profile sweep `build_rim`.
- **Its boundary**: Zenith was intentionally bounded to what Touch Foo wrote in 2011. It preserves existing vanilla levels with byte-exact parity, but it inherits the 2.5D prism mindset of the original engine.

### 3.3 Zypher (Gen 3) — The Paradigm Shift
- Zypher decouples the ground mesh concept from the 2.5D extrusion paradigm.
- Recognizing that the engine's `SurfaceMesh` is an arbitrary indexed 32-byte GPU buffer, Zypher acts as a **complete modern procedural geometry synthesizer**:
  - It generates smoother, more natural platforming surfaces than vanilla ever could.
  - It enables true 3D level elements (curved arches, tunnels, spherical planets, bas-relief cliffs).
  - It enables compiling 3D models directly into ground meshes with automated collision envelopes.
