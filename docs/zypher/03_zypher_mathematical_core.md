# 03 — Zypher Mathematical Foundations & Computational Geometry

## 1. Spline Foundations: Smooth Terrain Curves

### 1.1 Centripetal Catmull-Rom Formulation
To avoid cusps, self-intersections, and overshoot loops commonly produced by standard cubic splines on sharp elevation changes, Zypher uses **Centripetal Catmull-Rom Splines** ($\alpha = 0.5$):

Given control points $\mathbf{P}_0, \mathbf{P}_1, \mathbf{P}_2, \mathbf{P}_3$, knot intervals are computed as:
$$t_{i+1} = t_i + \|\mathbf{P}_{i+1} - \mathbf{P}_i\|^\alpha, \quad \text{where } \alpha = 0.5$$

The curve between $\mathbf{P}_1$ and $\mathbf{P}_2$ is evaluated via recursive Barry-Goldman interpolation:
$$\begin{aligned}
\mathbf{A}_1 &= \frac{t_1 - t}{t_1 - t_0}\mathbf{P}_0 + \frac{t - t_0}{t_1 - t_0}\mathbf{P}_1, &
\mathbf{A}_2 &= \frac{t_2 - t}{t_2 - t_1}\mathbf{P}_1 + \frac{t - t_1}{t_2 - t_1}\mathbf{P}_2, &
\mathbf{A}_3 &= \frac{t_3 - t}{t_3 - t_2}\mathbf{P}_2 + \frac{t - t_2}{t_3 - t_2}\mathbf{P}_3 \\
\mathbf{B}_1 &= \frac{t_2 - t}{t_2 - t_0}\mathbf{A}_1 + \frac{t - t_0}{t_2 - t_0}\mathbf{A}_2, &
\mathbf{B}_2 &= \frac{t_3 - t}{t_3 - t_1}\mathbf{A}_2 + \frac{t - t_1}{t_3 - t_1}\mathbf{A}_3 \\
\mathbf{C}(t) &= \frac{t_2 - t}{t_2 - t_1}\mathbf{B}_1 + \frac{t - t_1}{t_2 - t_1}\mathbf{B}_2
\end{aligned}$$

### 1.2 Adaptive Curvature-Based Subdivision
Instead of uniform step sizes that waste vertices on straight lines and pinch curves, Zypher subdivides segments based on local angular deflection:
$$\theta = \arccos\left(\frac{\mathbf{T}_{k} \cdot \mathbf{T}_{k+1}}{\|\mathbf{T}_{k}\| \|\mathbf{T}_{k+1}\|}\right)$$
Segments where $\theta > \theta_{\text{threshold}}$ are recursively bisected until chordal tolerance $\epsilon \le 0.5$ units.

---

## 2. Generalized Edge Profile Sweeping

### 2.1 Moving Reference Frame
Along any 2D or 3D spine curve $\mathbf{C}(s)$ with unit tangent $\mathbf{T}(s) = \mathbf{C}'(s)$, Zypher establishes an orthonormal frame $(\mathbf{T}, \mathbf{N}, \mathbf{B})$ using parallel transport (Bishop frame) to prevent twisting on loops:
$$\mathbf{N}_{k+1} = \mathbf{N}_k - \frac{\mathbf{T}_k + \mathbf{T}_{k+1}}{1 + \mathbf{T}_k \cdot \mathbf{T}_{k+1}} (\mathbf{T}_{k+1} \cdot \mathbf{N}_k)$$
$$\mathbf{B}_{k+1} = \mathbf{T}_{k+1} \times \mathbf{N}_{k+1}$$

### 2.2 Parameterized Cross-Section Profiles
A profile is defined as a 2D parametric function $\mathbf{p}(u) = (x(u), z(u))$ for $u \in [0, 1]$.
The 3D surface vertex $\mathbf{V}(s, u)$ is computed by:
$$\mathbf{V}(s, u) = \mathbf{C}(s) + x(u) \mathbf{N}(s) + z(u) \mathbf{B}(s)$$

#### Pre-Engineered Zypher Profiles:
1. **Curved Fillet (Marble Smooth)**:
   $$x(u) = -R(1 - \cos(\frac{\pi}{2} u)), \quad z(u) = Z_{\text{front}} + R \sin(\frac{\pi}{2} u)$$
2. **Stepped Terraces (Sedimentary Strata)**:
   Piecewise step function generating $K$ distinct horizontal rock terraces.
3. **Manga Bevel (Hard 45° Chamfer)**:
   Linear bevel with sharp normal discontinuity.

---

## 3. Interior 3D Bas-Relief & Constrained Delaunay Triangulation

### 3.1 Interior Point Generation (Poisson-Disk Sampling)
To allow the camera-facing cliff face (`FrontMesh`) to have genuine 3D relief instead of stretching flat across boundary nodes:
1. Generate sample points $\mathbf{x}_i \in \text{Interior}(\text{Polygon})$ using Bridson's Poisson-Disk sampling with minimum separation radius $r_{\text{min}}$.
2. Perform **Constrained Delaunay Triangulation (CDT)** preserving all polygon boundary edges.

### 3.2 Procedural Relief Function ($Z$-Displacement)
For each interior vertex $(x, y)$, calculate $z$ via multi-octave Fractal Brownian Motion:
$$z(x, y) = Z_{\text{base}} + \sum_{o=1}^{M} A_o \cdot \text{SimplexNoise}(f_o x, f_o y)$$
Where:
- Persistence $p = 0.5$ ($A_{o+1} = p \cdot A_o$)
- Lacunarity $l = 2.0$ ($f_{o+1} = l \cdot f_o$)

For basalt / cracked granite, evaluate **Worley / Cellular Noise**:
$$F_1(x, y) = \min_{p \in \text{Seeds}} \|(x, y) - p\|$$
Creating hexagonal, crystal-like columnar blocks protruding from the wall.

---

## 4. Analytical 3D Normal Synthesis

To guarantee pristine lighting under `caver_ground.frag`:
- For parametric surfaces (spheres, toruses, swept profiles), evaluate the exact partial derivatives:
  $$\mathbf{T}_u = \frac{\partial \mathbf{V}}{\partial u}, \quad \mathbf{T}_s = \frac{\partial \mathbf{V}}{\partial s}$$
  $$\mathbf{n}(s, u) = \frac{\mathbf{T}_u \times \mathbf{T}_s}{\|\mathbf{T}_u \times \mathbf{T}_s\|}$$
- For triangulated irregular terrain:
  Accumulate area-weighted face normals into adjacent vertices:
  $$\mathbf{n}_v = \sum_{f \in \text{Faces}(v)} \text{Area}(f) \cdot \mathbf{n}_f$$
  and re-normalize.

---

## 5. Automated 2D Silhouette Extraction (For Physics Collision)

When compiling an arbitrary 3D model (e.g., an imported OBJ or a mathematical sphere) into a `GroundMeshComponent`:
1. **Orthographic XY Projection**: Project all 3D surface vertices onto the XY plane: $\mathbf{v}_i = (x_i, y_i)$.
2. **Concave Hull Extraction ($\alpha$-Shapes)**:
   - For an edge between $p_1$ and $p_2$, check if a circle of radius $\alpha$ can touch both points without containing any other points.
   - Generates the exact 2D boundary polygon tracing the visual silhouette.
3. **Douglas-Peucker Simplification**:
   - Reduces the silhouette polygon to $32$–$64$ nodes while maintaining positional error $\le 0.25$ units.
4. **Wire Format Emission**:
   - Serialized into `GroundPolygonComponent` (Tag 110) and `CollisionShapeComponent` (Tag 120/121), guaranteeing that the player interacts cleanly with the 3D visual object.
