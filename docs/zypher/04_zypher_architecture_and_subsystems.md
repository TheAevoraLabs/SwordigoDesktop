# 04 — Zypher System Architecture & Generator Subsystems

## 1. Naming & Identity

### 1.1 Public Identity
- **Name**: **Zypher**
- **Tagline**: *"Zypher — Gentle curves, limitless geometry: redefining terrain"*
  *(Secondary: "Soft, gentle, boundless: redefining the mesh")*

### 1.2 Internal Lineage & Filenames
To preserve consistency with the project’s generator lineage:
- Generation 1: `src/tools/boulder.h` / `boulder.cpp`
- Generation 2: `src/tools/boulderx.h` / `boulderx.cpp` (Zenith)
- Generation 3: `src/tools/roller.h` / `roller.cpp` *(or `boulderz.h` / `boulderz.cpp`)*
  - Internal Namespace: `namespace zypher { ... }`
  - Generator ID: `"zypher"`
  - Display Name in GUI: `"Zypher"`

---

## 2. System Architecture & The Four Core Subsystems

Zypher is engineered as a unified generator engine with four specialized generator modalities:

```
                            +-----------------------------------+
                            |           ZYPHER CORE             |
                            |   (roller.h / roller.cpp)         |
                            +-----------------+-----------------+
                                              |
        +--------------------+----------------+--------------------+--------------------+
        |                    |                                     |                    |
+-------v------------+ +-----v--------------+              +-------v------------+ +-----v--------------+
|   ZypherTerrain    | |  ZypherParametric  |              |    ZypherModel     | |  ZypherOcclusion   |
|   ("RubyMesh")     | |  (3D Primitives)   |              |  (OBJ/GLTF Importer| |  (Atmospheric Mesh)|
|                    | |                    |              |  -> GroundMesh)    | |                    |
| - Spline Terrain   | | - UV Sphere        |              | - Wavefront OBJ    | | - Volumetric Fog   |
| - Dynamic Bevels   | | - Geodesic Dome    |              | - 3D Props to Mesh | | - Depth Curtains   |
| - 3D Bas-Relief    | | - Torus & Rings    |              | - Auto Silhouette  | | - Silhouette Hills |
| - Biome Partition  | | - Arches & Helixes |              |   2D Collision     | | - Parallax Planes  |
+--------------------+ +--------------------+              +--------------------+ +--------------------+
```

---

## 3. Subsystem Breakdown

### 3.1 Subsystem 1: `ZypherTerrain` ("RubyMesh")
Designed specifically for high-end 2.5D platformer level authoring:
- **Spline-Based Surface**: Converts user control points into silky-smooth Catmull-Rom or B-Splines.
- **Customizable Bevel Profiles**: Authors can select between *Smooth Fillet*, *Sedimentary Step*, *Gothic Moulding*, and *Manga Chamfer*.
- **3D Bas-Relief Front Faces**: Rather than a flat vertical polygon, the camera-facing wall is populated with Poisson-disk interior nodes and displaced with multi-octave Simplex or Voronoi noise, producing realistic weathered stone cliffs with true physical depth.
- **Slope-Aware Submesh Partitioning**:
  - The generator inspects vertex surface normals:
    - $\text{Angle} < 35^\circ \to$ Top grass cap (`SurfaceMesh` #1)
    - $35^\circ \le \text{Angle} \le 75^\circ \to$ Dirt / scree slope (`SurfaceMesh` #2)
    - $\text{Angle} > 75^\circ \to$ Cliff rock (`FrontMesh`)
  - Automatically emits multiple submeshes with independent `TextureMapping` components.

### 3.2 Subsystem 2: `ZypherParametric` (True 3D Primitives)
Direct mathematical synthesis of complete 3D volumes into `GroundMeshComponent`:
- **`Sphere` / `Ellipsoid`**: UV or Icosphere parameterization with configurable latitude/longitude tessellation and radial normals.
- **`Torus` / `Ring`**: Swept circular cross-section along a circular ring (floating rings, portals, hoops).
- **`Arch` / `Tunnel`**: Curved vaulted ceilings and bridge underbellies with correct internal lighting and ceiling normals.
- **`Spiral Helix`**: 3D spiral ramps that climb up towers or trees.

### 3.3 Subsystem 3: `ZypherModel` (Mesh-To-Ground Compiler)
Enables importing static 3D models from Blender into native Swordigo `.scene` files:
- **Input**: Wavefront `.obj` (vertices, normals, UVs, triangular faces).
- **Geometry Processing**:
  - Validates and packs vertex streams into standard 32-byte Caver layout.
  - Automatically partitions meshes exceeding 65,535 vertices into multiple `SurfaceMesh` chunks.
- **Automated Collision Generation**:
  - Projects 3D mesh vertices onto the XY plane.
  - Computes the 2D Concave Hull ($\alpha$-shapes).
  - Simplifies the hull using Douglas-Peucker algorithm.
  - Generates matching `GroundPolygonComponent` and `CollisionShapeComponent`.
- **Result**: You can author a stone gargoyle, a ruined temple, or a massive giant tree in Blender, and drop it into a level as a functional ground object with exact collision.

### 3.4 Subsystem 4: `ZypherOcclusion` (Atmospheric & Silhouette Meshes)
Exploits `GroundMesh` rendering for atmospheric level depth:
- **Depth-Plane Silhouettes**: Large, low-poly layered mountain ranges placed in negative parallax layers.
- **Volumetric Depth Curtains**: Non-colliding semi-transparent gradient meshes simulating volumetric fog, dust motes, and light shafts.
