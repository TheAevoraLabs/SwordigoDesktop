# 05 — Zypher Implementation Roadmap & Phased Engineering Plan

## 1. Engineering Principles

1. **Absolute Non-Interference with Existing Workflows**:
   - `boulder.cpp` (Boulder) remains untouched.
   - `boulderx.cpp` (Zenith) remains untouched.
   - Zypher is self-contained in `src/tools/roller.h` and `src/tools/roller.cpp` *(or `boulderz.{h,cpp}`)* within `namespace zypher`.
2. **Standard Protobuf Wire-Format Parity**:
   - Zypher emits standard Swordigo protobuf components (`GroundPolygonComponent`, `GroundMeshComponent`, `CollisionShapeComponent`, `TextureMappingComponent`).
   - Any level authored with Zypher loads in vanilla `libswordigo 1.4.13`, `Ruby GG`, `Ruby Touch`, and `ruby_cli` with zero client-side engine modifications.
3. **Robust Fallbacks & Determinism**:
   - All procedural noise is deterministically seeded.
   - Triangulation is mathematically verified before emission; degenerate inputs are declined with human-readable error reasons.

---

## 2. Phased Implementation Roadmap

### Phase 1: Mathematical Engine & Geometric Primitives (`src/tools/zypher_math.h`)
- [ ] **Centripetal Catmull-Rom & Cubic B-Spline Evaluator**:
  - Arc-length parameterization.
  - Adaptive curvature subdivision based on angular deflection.
- [ ] **Parallel Transport Moving Frames (Bishop Frame)**:
  - Twist-free sweeping along 2D/3D polylines and splines.
- [ ] **Constrained Delaunay Triangulation (CDT)**:
  - Robust interior triangulation of complex polygons with holes.
  - Poisson-disk interior point injection.
- [ ] **Procedural Noise Engine**:
  - OpenSimplex noise 2D/3D implementation.
  - Multi-octave Fractal Brownian Motion (fBm).
  - Worley/Cellular noise for rock strata.
- [ ] **Concave Hull & Simplification**:
  - $\alpha$-shapes algorithm for 2D silhouette extraction.
  - Ramer-Douglas-Peucker polygon simplification.

---

### Phase 2: Core Generator Engine (`src/tools/roller.h`, `src/tools/roller.cpp`)
- [ ] **Core Data Structures**:
  - `zypher::Node`, `zypher::Profile`, `zypher::Params`, `zypher::Mesh`, `zypher::Submesh`.
- [ ] **`ZypherTerrain` ("RubyMesh") Generator**:
  - Spline-driven top surface with customizable edge profiles (*Fillet*, *Step*, *Gothic*, *Manga*).
  - 3D Bas-relief front cliff faces with procedural Simplex/Voronoi displacement.
  - Slope-aware submesh partitioning with dual/triple material binding.
- [ ] **Binary Protobuf Emission**:
  - Interleaved 32-byte vertex buffer writer (`Position`, `Normal`, `TexCoord`).
  - Wire-format packing into `GroundMeshComponent` (Tag 111), `GroundPolygonComponent` (Tag 110), `CollisionShapeComponent` (Tag 120/121), and `TextureMappingComponent` (Tag 113).

---

### Phase 3: 3D Parametric Primitives Subsystem (`zypher_parametric.h / .cpp`)
- [ ] **`zypher::generate_sphere(...)`**:
  - UV sphere and geodesic icosahedral sphere with radial normals and $(u, v)$ spherical projection.
- [ ] **`zypher::generate_torus(...)`**:
  - Curved rings, circular hoops, and portal frames.
- [ ] **`zypher::generate_arch(...)`**:
  - Vaulted stone bridge underbellies and archways.
- [ ] **`zypher::generate_helix(...)`**:
  - 3D spiral walkways and rampways.

---

### Phase 4: Mesh-To-Ground Model Importer (`zypher_model.h / .cpp`)
- [ ] **Wavefront OBJ Parser**:
  - Ingests 3D `.obj` files directly from Blender or asset libraries.
- [ ] **Mesh Normalizer & Chunking**:
  - Normalizes scale and center.
  - Automatically partitions meshes exceeding 65,535 vertices into multiple `SurfaceMesh` entries.
- [ ] **Automated Collision Boundary Generator**:
  - Evaluates 2D silhouette projection.
  - Generates optimized 2D collision polygon.
- [ ] **CLI Subcommand**:
  - `ruby_cli zypher import-obj model.obj out.scene --texture rock`

---

### Phase 5: Editor & GUI Integration
- [ ] **Generator Enum Update**:
  - Add `GroundGenerator::Zypher` to `src/tools/boulderx.h` / `roller.h`:
    ```cpp
    enum class GroundGenerator {
        Boulder,    // Legacy 1st Gen
        BoulderX,   // Zenith (2nd Gen, Engine Parity)
        Zypher,     // Zypher (3rd Gen, RubyMesh)
    };
    ```
- [ ] **Desktop Editor (`Ruby GG`)**:
  - Add "Zypher" to GroundMesh Studio generator dropdown.
  - Expose spline tension, bevel profile picker, and 3D relief sliders in the UI.
- [ ] **Mobile Editor (`Ruby Touch`)**:
  - Connect settings bridge to toggle Zypher generator.
- [ ] **Ruby CLI**:
  - Add `ruby_cli zypher` subcommands for headless terrain generation and batch model compilation.

---

### Phase 6: Automated Verification & Test Suite (`tests/zypher_test.cpp`)
- [ ] **Spline Smoothness Tests**: Verify $C^1$ continuity and absence of self-intersections.
- [ ] **CDT & Topology Tests**: Assert valid non-overlapping manifold triangulation across complex concave polygons.
- [ ] **65k Vertex Boundary Tests**: Verify seamless submesh chunking on high-poly models.
- [ ] **Engine Compatibility Tests**: Assert byte-exact deserialization and round-trip through `scene_loader` and `filerift`.
