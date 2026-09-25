# 09 — The Engine's Ground-Mesh Generator (Reverse-Engineered)

> This is the heart of Swordigo's "procedural" level construction. Every shipped
> level is built from **GroundMeshGeneratorComponent** bundles (2,223 across all
> 118 scenes). The engine can regenerate the visible mesh from the component at
> load time — the shipped scenes carry both the baked mesh **and** the generator
> parameters. Decompiled sources are archived in
> [`docs/scenecreator/source/`](source/).

---

## 1. The call graph (Observed — decompiled symbols)

Entry point: `Caver::GroundMeshGeneratorComponent::GenerateMesh()` (@0x2BC4E8)

```
GenerateMesh()
 ├─ InitializeMeshBuilder(bool)                      @0x202FF0 / @0x2B9E5C
 │    (clears Caver::MeshBuilder; rebuild vertex/normal/texcoord/index data)
 ├─ GenerateSurfaceMesh(int, Vector2 const*, float const*, float const*,
 │                      TextureMapping const*)       @0x1F7420 / @0x2B9A48
 │    — plain surface (MeshType 0) or:
 ├─ GenerateSurfaceMeshWithRoundHat(f,f,f,f)         @0x1F8340 / @0x2BB010
 │    — surface with a rounded rim (MeshType 1; see §4)
 │     ├─ InsertRoundHatVertices(Vector2, f, f, Vector3, Vector3, f)  @0x1FDEB0 / @0x2BA45C
 │     ├─ InsertCapForRoundHat(bool, Vector2, f, f, Vector3, Vector3, f) @0x1FF330 / @0x2BA724
 │     └─ Polygon::VertexAtIndexIsConvexCCW(int)     @0x1F9E50
 │         Vector2::Normalize / Vector3::Normalize / sqrtf
 ├─ GeneratePlainSurfaceWithHatGaps(float)           @0x1FE290 / @0x2BAC60
 │    — the same surface as separate runs, with gaps where it meets the rim's
 │      inner wall (InsertPlainSurfaceVertices @0x202930; see §4)
 └─ GenerateFrontMesh()                              @0x1FD800 / @0x2BB564
      ├─ TriangulatePolygon(Vector2 const*, int, ushort*)  @0x1F5150 / @0x2C1CC4
      │    (ear-clipping triangulation of the source polygon)
      └─ TextureMapping::TexCoordForPosition(Vector3 const&)  @0x1FF490
```

Everything is emitted through `Caver::MeshBuilder`:
`MeshBuilder()`, `AddVertex(Vertex const&)` (@0x1F6B80/@0x2BA388),
`AddFace(int,int,int)` (@0x1F8200/@0x2BAADC), `GenerateMesh()` (@0x1F44E0),
`InitializePosition/InitializeNormal/InitializeTexCoordSet/InitializeIndices`,
`TransformVertices`, `InitWithMeshBuilderFaceSubset`, `SetNumTexCoordSets`.

### What the pieces do (Inferred from signatures + call graph)

| Function | Job |
|----------|-----|
| `GenerateSurfaceMesh` | Builds the flat **top surface** mesh of the ground polygon (the walkable area) at constant depth, textured with the surface `TextureMapping`. |
| `GenerateSurfaceMeshWithRoundHat` | Same surface, but with a rounded **rim**: `InsertRoundHatVertices` sweeps the rim's cross-section at each outline node and `InsertCapForRoundHat` closes the sweep at its seam; `VertexAtIndexIsConvexCCW` decides where the rim's corner is pushed out. |
| `GeneratePlainSurfaceWithHatGaps` | The plain surface emitted as *separate runs* with gaps at the creases, so it does not wrap around the rim's inner wall. See §4. |
| `GenerateFrontMesh` | **Front face**: ear-clips the polygon (`TriangulatePolygon`) and extrudes each triangle to the front depth band, textured with the front `TextureMapping` — this is the vertical strip the player sees. |
| `InitializeMeshBuilder` | Prepares the builder; `AddVertex`/`AddFace` accumulate; `GenerateMesh()` materializes the `Caver::Mesh`. |
| `TextureMapping::TexCoordForPosition` | Computes the UV for a world-space vertex — **UVs are derived from world position**, which is why `TextureMapping.Scale` (250) controls texel density. |

---

## 2. GroundMeshGeneratorComponent (the scene-side parameter bundle)

Component payload field **112** on the object's `GroundMeshGenerator` component.
Layout (Observed schema + decoded scenes):

| Field | Name | Meaning | Game range |
|-------|------|---------|-----------|
| 1 | `GroundPolygonId` | local id of the polygon the mesh is generated from | 100 / 980 |
| 2 | `TargetMeshId` | local id of the `GroundMesh` the result is written to | 101 / 981 |
| 3 | `FrontTextureMappingId` | local id of the front-face `TextureMapping` | 107 / 985 |
| 4 | `SurfaceTextureMappingId` | local id of the surface `TextureMapping` | 106 / 984 |
| 5 | `RandomSeed` | seed for any vertex noise | 0, 1291618994, 1573073931 |
| 6 | `HorizNoise` | horizontal displacement amplitude | 0 (1859×), 50 (209×), 250 (62×) |
| 7 | `MeshType` | 0 = plain, 1 = round-hat surfaces | 1 (1759×), 0 (464×) |
| 8 | `SurfaceWidth` | surface detail spacing | 20–250 (50, 100, 150, 120, 80… most common) |
| 9 | `HatHeight` | default round-hat height | 25 (1816×), 20, 40, 33 |
| 10 | `HatWidthOffset1` | hat profile width (start) | 5 (2223×) |
| 11 | `HatWidthOffset2` | hat profile width (end) | 5 (2223×) |

> **Note on the editor default:** `RandomSeed 1291618994` appears 344 times across
> scenes — it is the engine editor's default seed (also what the Ruby GMG bakes).
> `RandomSeed 0` (1652×) means "use a fresh/deterministic-zero seed".

---

## 3. The baked `GroundMesh` layout (what the generator writes)

A generated ground object carries (local ids in parentheses — the "980 bundle"):

```
GroundPolygon (980)      — polygon (f2), Collides (f3), MinDepth/MaxDepth (f4/f5)
GroundMesh    (981)      — LocalAabb (f7),
                           SurfaceMesh (f8, repeated):
                              top surface (top texture)
                              side walls  (front/bottom texture)
                              one SurfaceMesh per round hat (top texture)
                           FrontMesh (f9, non-indexed triangles, front texture)
                           Color (f10, RGBA 1,1,1,1)
GroundMeshGenerator (982) — params above
CollisionShape (983, ParentComponentIdentifier=980)
                           — ShapeComponent.Polygon (f120-f3), CollisionShapeComponent:
                             IsGround (f2=1), MinDepth (f6), MaxDepth (f7), Enabled (f11=1)
TextureMapping (984)      — surface texture, Scale 250
TextureMapping (985)      — front texture, Scale 250
```

### Vertex layout (Observed from `SurfaceMesh.Vertices`)

`ValueType 7` (float32×N), `ValuesPerVertex 3`, `Stride 32`, `DataOffset 0`.
The 32-byte vertex = **position (12 B) + normal (12 B) + UV (8 B)** — confirmed by
`DataOffset 12` on Normals and `DataOffset 24` on TexCoordSet in every scene.
Indices: `ValueType 4` (uint16), `Stride 2`.

---

## 4. "Round hats" are the RIM, not domes — corrected

> **Correction (measured, not inferred).** An earlier revision of this document
> read "round hat" as a *dome standing on the surface* and cited `HatHeight 30-40`
> and a "60-radius / 25-height dome" as examples. Both readings are wrong. The
> shipped data settles it: across all **2,538 ground objects** in the 202 decoded
> scenes, the largest amount any baked vertex rises above its own polygon's top
> edge is **2.04 units**, and every one of those **154** vertices (in 43 submeshes)
> sits **exactly 3.000** from a **convex** node — which is the rim's corner shift
> (`VertexAtIndexIsConvexCCW` plus a hard-coded `3.0` in
> `GenerateSurfaceMeshWithRoundHat`). A `HatHeight` dome would rise 20-40. So
> `HatHeight` is a *rim* depth (the generator field's own name for it), and
> `MeshType 1` means "this slab has a rounded rim". Reproduce the measurement with
> `.scratch/ground_dome_probe.py`.
>
> (The rim does put vertices outside a polygon's *bounding box* in general — an
> inward profile offset at an obtuse corner moves along the axis the box measures.
> 7,466 of the 18,357 out-of-box vertices in the corpus are the 3.000 shift; the
> rest are those inward offsets, and none of them rises above the top edge.)

So `InsertRoundHatVertices` / `InsertCapForRoundHat` build the **brim** of a
MeshType 1 slab, and there is no dome anywhere in the engine's ground pipeline.

`InsertRoundHatVertices(Vector2 center, float a3, float a4, Vector3 u, Vector3 v, float a7)`
sweeps the rim's cross-section:
- One **profile ring** per outline node, standing ON the surface plane
  (`+0.05` to avoid z-fighting — the same trick boulder uses).
- The rim spans the slab's depth and bevels outward past the front/back planes,
  which is what makes the terrain read as a rounded plate rather than a cut block.
- `InsertCapForRoundHat` adds the two closing vertices and the six faces that
  close the sweep at its seam — it calls `InsertRoundHatVertices` first and then
  stitches the ring it just added.

A **dome** as an authoring feature is boulder's own (`src/tools/boulder.{h,cpp}`,
`Hat` + `generate_hat_mesh`), written into `.swdm` sheets as a `Hat[` block. It has
no engine counterpart, so nothing can be ported for it; BoulderX reproduces
boulder's geometry instead (see doc 13 §4).

### The three planes the rim is built from

Every z the rim emits is an **offset from that node's plain plane** (the array
`GenerateMesh` computes — §1.1 of doc 13):

```
plain[i]        = +-(SurfaceWidth/2 + HorizNoise * (frand() - 0.5))   the ribbon and the cap
rim outer       = plain[i] +- HatWidthOffset1                        on the node's own XY
rim bevel       = plain[i] +- (HatWidthOffset1 + HatWidthOffset2)    at 0.4*HatHeight inward
rim inner wall  = plain[i] +- (HatWidthOffset1 + 0.1)                at HatHeight inward
```

Read that as a warning rather than a formula: because the outer pair already
carries `HatWidthOffset1`, a per-node depth **recovered from the bake** at a rim
node is `HatWidthOffset1` too far out, and feeding it back makes the rim add the
offset twice. That is exactly what happened — a `±100` sheet regenerated at
`[-105, 100]`. The per-node array must come from the law, not the bake; doc 13 §4b
has the measurement and the fix.

---

## 5. Determinism

Because `RandomSeed` is stored per component and the mesh is regenerated from it,
**the same seed + same polygon + same params → the same mesh**. The Ruby generator
honors this (see [12](12_generation_algorithms.md)): the same blueprint + seed
produces byte-identical scenes.

---

## 6. Archived decompiled sources

`docs/scenecreator/source/` contains 69 IDA/Hex-Rays `.c` files, including:
- `GroundMeshGenerator`: `GenerateSurfaceMesh.c`, `GenerateSurfaceMeshWithRoundHat.c`,
  `GeneratePlainSurfaceWithHatGaps.c`, `GenerateFrontMesh.c`,
  `InsertRoundHatVertices.c`, `InsertCapForRoundHat.c`, `InsertPlainSurfaceVertices.c`
- `MeshBuilder`: `AddVertex.c`, `AddFace.c`, `GenerateMesh.c`, `Initialize*.c`
- `TriangulatePolygon.c`
- `Proto/GroundMeshGeneratorComponent/*.c`

These are **generated evidence** (IDEA decompiler output), not original source.
