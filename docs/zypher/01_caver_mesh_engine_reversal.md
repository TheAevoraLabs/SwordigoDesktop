# 01 — Caver GroundMesh Engine Reversal & Physical GPU Limits

## 1. Executive Summary

This document establishes the exact ground-truth reverse engineering of Swordigo's terrain rendering and ground-mesh subsystems inside the **Caver Engine** (version 1.4.13, cross-referenced between ARM32 and ARM64 decompilations from `libswordigo.so`).

Historically, modding tools (including early `boulder.cpp`) treated Swordigo's terrain as flat 2D extrusions because Touch Foo authored platformer slabs that way. However, disassembly of `Caver::GroundMeshComponent`, `Caver::MeshBuilder`, and `Caver::Scene::DrawGroundMeshes` proves that **the engine does not enforce 2.5D prism geometry**. Instead, `GroundMeshComponent` is a generic container for arbitrary, hardware-accelerated 3D vertex and index streams.

---

## 2. Decompiled Call Hierarchy & Pipeline

### 2.1 The Regeneration Path (`GroundMeshGeneratorComponent::GenerateMesh`)
- **ARM32 address**: `0x002D516C`
- **ARM64 address**: `0x003A6000`

```
Caver::GroundMeshGeneratorComponent::GenerateMesh()
 ├─ targetMesh->RemoveAllMeshes()
 ├─ Caver::seed_fastrandom(RandomSeed)
 ├─ Allocate front[N] and back[N] float depth arrays:
 │    front[i] =  0.5 * SurfaceWidth + (frand() - 0.5) * HorizNoise
 │    back[i]  = -0.5 * SurfaceWidth - (frand() - 0.5) * HorizNoise
 ├─ GenerateFrontMesh() [Non-indexed triangle soup]
 │    ├─ TriangulatePolygon(polygon.vertices) -> Ear clipping
 │    └─ AddFrontMesh(targetMesh)
 ├─ MeshType == 1 (Rounded Rim):
 │    ├─ GenerateSurfaceMeshWithRoundHat()
 │    │    ├─ InsertRoundHatVertices() -> sweeps 6-point cross-section profile
 │    │    ├─ InsertCapForRoundHat()   -> seams the sweep at loop boundary
 │    │    └─ AddSurfaceMesh(targetMesh) [SurfaceTexture]
 │    └─ GeneratePlainSurfaceWithHatGaps()
 │         └─ AddSurfaceMesh(targetMesh) [FrontTexture]
 ├─ MeshType == 0 (Plain Extrusion):
 │    └─ GenerateSurfaceMesh()
 │         └─ AddSurfaceMesh(targetMesh) [SurfaceTexture]
 └─ Calculate LocalAabb from Polygon::Bounds() outset by 0.4 * HatHeight
```

### 2.2 The GPU Drawing Path (`Caver::Scene::DrawGroundMeshes`)
- **ARM32 address**: `0x002D23EE`
- **ARM64 address**: `0x004F8CF4`

```
Caver::Scene::DrawGroundMeshes(RenderingContext* ctx, Matrix4 const& view_proj, bool transparent)
 ├─ ComponentCollectionForInterface(GroundMeshComponent::Interface)
 └─ For each GroundMeshComponent in scene:
      ├─ Frustum Culling against LocalAabb transformed by Object World Matrix
      └─ GroundMeshComponent::Draw(ctx, clip_rect, world_matrix)
           ├─ Bind Texture from Material[5] or TextureMappingId
           ├─ Upload/Bind VBO (GL_ARRAY_BUFFER) & EBO (GL_ELEMENT_ARRAY_BUFFER)
           ├─ Enable Vertex Attributes:
           │    a_position (3 floats @ offset 0)
           │    a_normal   (3 floats @ offset 12)
           │    a_texcoord (2 floats @ offset 24)
           │    [Optional] a_color (4 ubytes @ offset 32)
           ├─ Uniforms: u_mvp, u_model_matrix, u_material_color, u_light_direction
           └─ glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, 0)
```

---

## 3. Binary Memory & Vertex Layout (`CreateInterleavedData`)

From `Caver::Mesh::CreateInterleavedData` (`0x004DD328` in ARM64):

The engine formats vertex buffers into tightly packed 32-byte interleaved structures:

```
+-----------------------------------------------------------------------------------+
| Byte 0 - 11 (12B)  | Byte 12 - 23 (12B) | Byte 24 - 31 (8B)  | Stride: 32 Bytes   |
| Position (X, Y, Z) | Normal (Nx, Ny, Nz)| UV Coords (U, V)   | ValueType: FLOAT (7)|
+-----------------------------------------------------------------------------------+
```

### Attribute Specifications
| Attribute | Protobuf ValueType | OpenGL Type | Components | Stride | Offset |
|---|---|---|---|---|---|
| **Position** | 7 (Float32) | `GL_FLOAT` | 3 | 32 | 0 |
| **Normal** | 7 (Float32) | `GL_FLOAT` | 3 | 32 | 12 |
| **TexCoord** | 7 (Float32) | `GL_FLOAT` | 2 | 32 | 24 |
| **Indices** | 4 (UShort16) | `GL_UNSIGNED_SHORT` | 1 | 2 | 0 |

---

## 4. Physical GPU & Engine Constraints

Understanding the absolute hardware and software limits of Caver dictates how far the generator can be pushed:

1. **16-Bit Index Buffer Limit**:
   - `Indices` are serialized as `uint16_t`.
   - **Limit**: A single `SurfaceMesh` or `FrontMesh` submesh cannot reference more than **65,535 vertices**.
   - **Architectural Solution in Zypher**: For massive, high-detail models or expansive procedural landscapes exceeding 65k vertices, Zypher automatically partitions the geometry into multiple chained `SurfaceMesh` entries inside the same `GroundMeshComponent`.

2. **Index Buffer vs. Non-Indexed Soup**:
   - `SurfaceMesh` (Field 8) uses indexed rendering (`GL_TRIANGLES` via `glDrawElements`).
   - `FrontMesh` (Field 9) in vanilla scenes is authored as **non-indexed triangle soup** (`InitializeMeshBuilder(false)`), but the underlying `Caver::Mesh` structure fully supports indices on Field 9 as well.

3. **Collision Disconnect (Planar 2.5D Physics)**:
   - Swordigo's physics engine (`Caver::CollisionShapeComponent`) strictly tests against `GroundPolygonComponent` (a 2D closed polygon on XY) extruded between `MinDepth` and `MaxDepth`.
   - **Limit**: The physics engine cannot collide against true 3D concave hollows (e.g., walking through the hole of a 3D donut) using a single ground polygon.
   - **Architectural Solution in Zypher**: Automatic 2D silhouette hull generation for convex/closed bodies, and automatic decomposition into compound sub-colliders for hollow architecture.

4. **Lighting & Shading**:
   - The engine’s fragment shader (`caver_ground.frag`) performs Lambertian directional lighting:
     `diff = max(dot(normalize(v_normal), -u_light_direction), 0.3)`.
   - In vanilla, normals were trivially calculated from flat 2D edge normals: `(nx, ny, 0)`.
   - **Opportunity in Zypher**: By calculating genuine 3D analytical normals $\mathbf{n} = (n_x, n_y, n_z)$ across curved and organic surfaces, the engine renders rich specular highlights and smooth 3D shading identical to high-end `.POD` models.
