# Modern Forward Renderer for Swordigo 1.4.13 — Verification & Implementation

Status: **implemented, builds clean (`swgfx`, `swemu`, `swordfare`), runtime-toggleable.**
This document records what the 1.4.13 binary *actually* does (evidence included),
why the guest cannot be "switched to GL2", and how the host bridge now renders the
game's geometry with a real GLSL 330 forward pass.

---

## 1. Verification — claim by claim

Addresses are from `OpenSwordigo/arm64_13/functions/**` (IDA/Hex-Rays 1.4.13);
runtime counts are from `swordigo_run.log` / `run.log` / `headless_run.log`.

| Claim | Verdict | Evidence |
|---|---|---|
| `RenderingContext` has `api == 0` (fixed-function GLES1) and `api == 1` (GLES2) | **True** | `RenderingContext(RenderingAPI)` @ `0x5151EC` stores the arg at `+0` and branches on it (`SetProjectionMatrix` @ `0x5153EC`: `if (*this) dirty=1; else { glMatrixMode; glLoadMatrixf; }`) |
| `BasicRenderingPrograms::AddToContext` registers exactly 4 programs | **True** | `arm64_13/.../0000000000517758__AddToContext.c` — `AddProgram(this, 0..3)` |
| Attribute sets are `[32884]`, `[32884,32888]`, `[32884,32886]`, `[32884,32886,32888]`; `32885` (normals) never declared | **True** | same file. `RenderingProgram::AddVertexAttribute` @ `0x516C64` pushes only the GL enum — **the name string is never used** |
| The GLES2 programs are flat/unlit | **True, but moot** | `RenderingProgramShader`'s ctor @ `0x518238` makes **no GL calls** (one callee: a `std::string` copy); `LinkWithShaders` @ `0x516DD8` is `return 1;` |
| `MeshInstance::Draw` uploads position **and normals** | **True** | `0x4E081C`: `AddArray(32884)`, then `AddArray(32885)` when normals exist, then `32888`, `32886`; draws `GL_TRIANGLES` |
| `GroundMeshComponent::DrawMesh` @ `0x3A5534` is where to bind PBR maps | **False — dead end** | exists with **0 callers**; the live path is `GroundMeshComponent::Draw` @ `0x3A5294` |
| GroundMesh shaders do directional lighting | **False** | no such shaders exist; `docs/groundmeshes/10_...md` is a *proposal*. The only GLSL literal in `libswordigo.so` is the flat `vertexPosition`/`mvpMatrix` vertex shader |
| `glMaterialfv` is used on terrain | **True** | `GroundMeshComponent::Draw` calls it with `GL_FRONT_AND_BACK` + `GL_AMBIENT_AND_DIFFUSE`, guarded by `if (!*ctx)` (api == 0) |
| Dynamic lights are real | **True** | `LightComponent::Enable` @ `0x331DD0`: `glEnable(GL_LIGHT0+i)`, `glLightfv(GL_POSITION/AMBIENT/DIFFUSE)`, `glLightf(GL_LINEAR_ATTENUATION)` — only when `api == 0` |
| `GL_LIGHTING` is on for scene geometry | **True** | `SetLightingEnabled` called from `Caver::Scene::Draw` @ `0x4F905C` and `Caver::SkeletonInstance::Draw` @ `0x4E6A9C` |
| Hooking engine shader registration gets you real shaders | **No — inverted premise** | see below |

### 1.1 The decisive facts

```c
// Java_com_touchfoo_swordigo_Native_setupApplication @ arm64_13 0x507DF4
v31 = operator new(0x128u);
Caver::RenderingContext::RenderingContext(v31, 0);   // ← RenderingAPI == 0
```

**The shipped game always renders through the legacy fixed-function GLES 1.1
path.** Two consequences:

1. The api==1 code path is **not a working backend**: it never compiles a shader
   and never binds a vertex attribute (every array-plumbing function
   `VertexArrayObject::AddArray` `0x5171FC`, `BindArrays` `0x5174A8`,
   `SetVertexAttribArrayEnabled` `0x5173F8`, `SetVertexAttribPointer` `0x517414`
   early-outs on `if (!*ctx)`). Flipping the constant to `1` in the guest would
   not "turn on GL2"; it would produce nothing, because the GLES2 half of the
   engine is authored-but-unbuilt.
2. Therefore the guest is a **scene describer**, not a renderer. Everything it
   says is visible to the host bridge, so the bridge can *be* the renderer.

Runtime evidence (same three sessions):

| GL call | swordigo_run.log | run.log | headless_run.log |
|---|---|---|---|
| `glEnableClientState` | 726 | 228 | 50 |
| `glVertexPointer` | 510 | 152 | 3 |
| `glLoadMatrixf` | 223 | 64 | 34 |
| `glMaterialfv` | 15 | 15 | 0 |
| `glLightfv` | 30 | 5 | 20 |
| **`glCreateShader` / `glCompileShader` / `glUseProgram` / `glVertexAttribPointer` / `glBindAttribLocation`** | **0** | **0** | **0** |

### 1.2 SRE13 scoping

`src/sre/sre13` (guest `libsre13.so`) has genuine trampoline support —
`HOOK_SYMBOL`/`HOOK_OFFSET` → `srehost_install_hook(target, replacement, &orig, 0)`
(`core/hook.h`) — and `hooks/RenderingContext.c` already resolves `UseProgram`.
In 1.4.13 those symbols are the wrong target for *shading* (never called), but
SRE13 stays valuable as the **scene-intent provider** (texture names, component
identity, light ownership). The host bridge is where the pixels are decided.

---

## 2. Implementation — `src/platform/ffp_forward.{h,cpp}`

A modern host-side forward renderer. The guest's fixed-function command stream is
transcoded into a GLSL 330 pass:

| Guest call | What the shader receives |
|---|---|
| `glMatrixMode` / `glLoadIdentity` / `glLoadMatrixf` / `glMultMatrixf` / `glPushMatrix` / `glPopMatrix` / `glTranslatef` / `glRotatef` / `glScalef` / `glOrthof` / `glFrustumf` | `u_mvp`, `u_mv`, `u_normal_mat`, `u_tex_mat` from a **shadowed matrix stack** — no compatibility built-ins |
| `glVertexPointer` + `glNormalPointer` + `glColorPointer` + `glTexCoordPointer` | a packed `GL_STREAM_DRAW` VBO (`pos3 nrm3 rgba uv2`), fetched from guest memory with arena bounds checks, honouring size/type/stride, `GL_UNSIGNED_BYTE`/`GL_SHORT`/`GL_FLOAT` |
| `glLightfv`/`glLightf`/`glLightModelfv` | 8-light uniform arrays, **eye-space** positions (`glGetLightfv` at the moment the guest sets them) + attenuation triple |
| `glMaterialfv`/`glMaterialf` | material uniforms, incl. `GL_AMBIENT_AND_DIFFUSE`; GGX roughness/metallic **derived** from the GLES1 shininess/specular |
| `glFogf`/`glFogfv`/`glFogi` | in-shader fog (a bound program disables `GL_FOG` outright) |
| `glTexEnvi(GL_TEXTURE_ENV_MODE)` | texture combine mode |
| `glEnable/Disable(GL_LIGHTING, GL_COLOR_MATERIAL, GL_FOG, GL_LIGHT0+i)` | gates and material-source selection |

### 2.1 Shader features

* **GLSL 330**, explicit `layout(location=…)` attributes, explicit uniform
  matrices, `out vec4 fragColor`. Nothing from the compatibility profile.
* **Real per-fragment lighting** with the engine's live light positions, colours
  and attenuation — replacing per-vertex hardware evaluation.
* **Derived normal mapping.** Swordigo ships no normal/height/ORM maps at all, so
  the height field is taken from the **diffuse map's luminance** and converted to
  a tangent-space normal per fragment (cotangent frame + surface gradient, with
  `fwidth()` as the step so it behaves like a mip-adapted bump). Zero new assets;
  it works on every painted wall, roof, stone and grass texture in the game.
* **Parallax occlusion mapping** raymarches that same derived height field with a
  bounded loop plus binary-search refinement.
* **Optional Cook-Torrance GGX** (D/V/F, Trowbridge-Reitz + height-correlated
  Smith + Schlick) with roughness/metallic derived from the only material data
  the game ever had (`GL_SHININESS`, `GL_SPECULAR`).
* **ACES filmic tonemapping + exposure** in the pass. This was a deliberate
  correctness decision: the existing desktop-effects chain
  (`fbo_scaler.cpp`) has **no tonemap stage** and its render targets are
  `GL_RGBA8`, so emitting raw HDR would clip to flat white. `SWORDIGO_FFP_HDR=1`
  switches to unclamped linear output for a future float chain.
* **GL-faithful details preserved:** `GL_COLOR_MATERIAL` selects vertex colour as
  `AMBIENT_AND_DIFFUSE`; lighting never modifies alpha (`lit.a = matDif.a`) so
  translucent scene geometry still blends correctly; a bound texture is only used
  when `GL_TEXTURE_2D` is enabled and a texcoord array is bound; `GL_REPLACE`,
  `GL_DECAL`, `GL_ADD`, `GL_MODULATE` combine modes; `GL_LIGHT_MODEL_TWO_SIDE`.

### 2.2 Safety

* **Gated** on `GL_LIGHTING` + a vertex array + triangle-ish primitives. Sprites,
  UI, particles and unlit props keep the vanilla path byte-for-byte.
* **Self-validating matrices.** The shadowed modelview/projection are compared
  against the driver's own `GL_MODELVIEW_MATRIX`/`GL_PROJECTION_MATRIX` on the
  first eligible draw. Any mismatch disables the modern path permanently and logs
  both matrices (`SWORDIGO_FFP_DEBUG=1`). A wrong MVP would corrupt the frame, so
  this is checked rather than assumed.
* **Self-disabling shader**: compile/link failure (e.g. a driver without GLSL 330)
  → vanilla fixed-function, logged.
* State is restored per draw: program, VAO, VBO binding. Vertex data is read out
  of guest memory only after arena bounds checks; unsupported type/stride
  combinations fall back to the fixed-function path for that draw.

### 2.3 Toggles

| Env var | Default | Meaning |
|---|---|---|
| `SWORDIGO_FFP_LIGHTING` | `2` | `0` vanilla fixed-function · `1` GLSL 330 per-fragment lights · `2` + derived normal map · `3` + parallax occlusion · `4` + GGX PBR |
| `SWORDIGO_FFP_EXPOSURE` | `1.0` | exposure before the filmic curve |
| `SWORDIGO_FFP_BUMP` | `1.0` | derived normal-map strength |
| `SWORDIGO_FFP_POM_SCALE` / `_POM_STEPS` | `0.02` / `16` | parallax depth and raymarch steps (capped at 64) |
| `SWORDIGO_FFP_HDR` | `0` | `1` = emit unclamped linear instead of tonemapping in-shader |
| `SWORDIGO_FFP_DEBUG` / `_STATS` | `0` | per-draw trace / handled-vs-skipped counters |

### 2.4 Build & verify

```bash
cmake --build build --target swgfx swemu swordfare -j"$(nproc)"
SWORDIGO_FFP_LIGHTING=4 SWORDIGO_FFP_DEBUG=1 ./bin/swordfare 2>&1 | grep '\[FFP\]'
```

Expect `[FFP] forward: GLSL 330 renderer ACTIVE (quality=4)`, then per-draw lines
`lights=N bump=1 pom=8 pbr=1 fog=1`. If the matrices ever disagree you get the
mismatch line instead and the game renders exactly as before.

---

## 3. Why not "just force `api = 1`"

Forcing the guest constant would swap a *working* fixed-function pipeline for a
half-written one: no compiled programs (the ctor never calls GL), no attribute
binding (guarded on `api == 0`), no `glUseProgram` anywhere in the engine. That
is strictly worse than rendering the same command stream ourselves, which is what
this module does — and it keeps the guest untouched, so nothing about game logic,
saves or mods changes.

---

## 4. Next steps

1. **Float render target + real HDR chain.** Make the game FBO `GL_RGBA16F` (and
   the bloom pyramid) and add an ACES/tonemap pass at the end of
   `fbo_scaler`'s composite, then set `SWORDIGO_FFP_HDR=1`. That is what turns
   light highlights into genuine bloom energy instead of in-shader tonemapping.
2. **Shadow maps.** Now that the forward pass owns the MVP and normals, a
   directional/point shadow map can be rendered from the same geometry and
   sampled in the same fragment shader (plus contact shadows). `LightComponent`
   already carries position, colour and range.
3. **Real PBR materials.** Feed `set_normal_map()`/ORM from sidecar assets:
   record the asset path per texture id at upload time in the bridge, or publish
   `Caver::Texture` names from `libsre13.so` (it already hooks
   `hooks/TextureLibrary.c`). The shader side (GGX + normal map + POM inputs)
   already exists.
4. **SCREEN-SPACE → geometry-space upgrades.** With depth written by this pass,
   SSAO/SSR in `fbo_scaler` can be normal-aware (the normal buffer can be
   exported as a second MRT attachment).
5. **Debug surface:** expose quality/status/light count in the debug overlay and
   the settings GUI instead of env-only control.
