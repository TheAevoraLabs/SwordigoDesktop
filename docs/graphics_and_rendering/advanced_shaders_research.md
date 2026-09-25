# Advanced Shaders on SwordigoDesktop — Research

Status: **research complete, no code changed.**
Sources: IDA MCP on `libswordigo.so.i64` (session `ebd270c9`), `OpenSwordigo/arm64_13/`
decompilation, and the current tree.

> ⚠️ `src/platform/ffp_forward.cpp` was **being rewritten while this research ran**:
> 1295 lines at session start → 1498 lines at the end (mtime `01:13`, `.o` rebuilt
> `01:14:54`). `GLES1Shaders.inc` mtime `00:30`. Everything in §3 below describes the
> **final** state observed. Nothing in this research modified those files.

---

## 1. The guest binary has no shaders — verified on two independent builds

Two *different* `libswordigo.so` builds are in play, and their addresses do not match:

| | IDA IDB (`libswordigo.so.i64`) | `OpenSwordigo/arm64_13` |
|---|---|---|
| `BasicRenderingPrograms::AddToContext` | `0x48db54` (4920 B) | `0x517758` |
| `RenderingProgram::LinkWithShaders` | `0x48d0ac` (8 B) | `0x516DD8` |
| GLSL string `vertexPosition/mvpMatrix` | `0x596a7c` | `0x25CCC5` |

`ffp_forward.h`'s docblock cites the *arm64_13* addresses. The IDB is a **different
build** — and it reaches the same conclusions, so the claim is not build-specific.

### 1.1 Evidence (IDA, live)

```
__int64 Caver::RenderingProgram::LinkWithShaders()
{
  return 1; /*0x48d0b0*/          // 8 bytes total. No GL call. Ever.
}
```

`RenderingProgramShader`'s ctor (`0x518238`, arm64_13) has exactly **one callee** — a
`std::string` copy. It stores name, type and the `char const*` source, then returns.
Nothing is compiled.

### 1.2 Only two GLSL strings exist in the whole binary

```
0x596a7c  attribute highp vec4 vertexPosition;
          uniform mediump mat4 mvpMatrix;
          void main(void){ gl_Position = mvpMatrix * vertexPosition; }

0x596b0d  void main (void){ gl_FragColor = vec4(1.0, 1.0, 1.0 ,1.0); }
```

A *position-only* vertex shader and a *solid white* fragment shader.

### 1.3 Four programs registered, all sharing those two stubs

`BasicRenderingPrograms::AddToContext` builds and registers:

| id | name | vertex attributes (GL enums) |
|---|---|---|
| 0 | `PROGRAM_UNIFORM_COLOR` | 32884 `POSITION` |
| 1 | `PROGRAM_UNIFORM_COLOR_TEXTURING` | 32884, 32888 `TEX_COORD` |
| 2 | `PROGRAM_VARYING_COLOR` | 32884, 32886 `COLOR` |
| 3 | `PROGRAM_VARYING_COLOR_TEXTURING` | 32884, 32886, 32888 |

**32885 (`NORMAL`) is never declared by any program.** `AddVertexAttribute` pushes
only the enum — the name string argument is never read.

### 1.4 Zero shader APIs in the binary

237 `.dynsym` imports, **56 of them `gl*`**, all fixed-function GLES 1.1 + VBO:

```
glActiveTexture glBindBuffer glBindTexture glBlendFunc glBufferData glBufferSubData
glClear glClearColor glClearDepthf glClearStencil glColor4f glColor4ub glColorMask
glColorPointer glCompressedTexImage2D glCullFace glDeleteBuffers glDeleteTextures
glDepthFunc glDepthMask glDisable glDisableClientState glDrawArrays glDrawElements
glEnable glEnableClientState glFlush glGenBuffers glGenTextures glGetError
glGetIntegerv glGetString glLightModelfv glLightf glLightfv glLoadIdentity
glLoadMatrixf glMaterialfv glMatrixMode glNormalPointer glPixelStorei glPopMatrix
glPushMatrix glScalef glScissor glStencilFunc glStencilOp glTexCoordPointer
glTexEnvfv glTexEnvi glTexImage2D glTexParameterf glTexParameteri glTranslatef
glVertexPointer glViewport
```

A binary-wide string search for `glCreateShader`, `glShaderSource`, `glCompileShader`,
`glLinkProgram`, `glUseProgram`, `glUniform*`, `glBindAttribLocation`,
`glGetAttribLocation`, `glCreateProgram`, `glAttachShader`, `glDeleteShader` returns
**zero hits** — not imports, not strings, nothing.

Also absent from `.dynsym` (but reachable — see §2): `glAlphaFunc`, `glFogf/fv/i`,
`glShadeModel`, `glColorMaterial`, `glOrtho`, `glFrustum`, `glRotatef`,
`glMultMatrixf`, `glGetFloatv`, `glIsEnabled`, `glHint`, `glLineWidth`, `glPointSize`.

### 1.5 Conclusion

The guest is a fixed-function **scene describer**, not a renderer. There is no ES2
renderer to "switch on" — flipping `RenderingContext`'s api to `1` would produce
nothing, because that half of the engine is authored-but-unbuilt (its array-plumbing
functions early-out on `api == 0`). Everything the guest says is already visible to
the host bridge, so **the bridge is the place to add shaders**, with no guest patching.

---

## 2. The bridge already supports a full programmable pipeline

`src/jni/jni_bridge_arm64.cpp` resolves guest GL calls through a **name table**, not
through `.dynsym` — `register_handler("glFogf", bridge_glFogf)` and friends.

* **582** `register_handler(...)` entries total
* **114** unique `gl*` names
* **21** of them are shader-API (`glCreateShader`, `glShaderSource`, `glCompileShader`,
  `glLinkProgram`, `glUseProgram`, `glUniform1f/1i/2f/3f/4f/4fv/Matrix4fv`,
  `glBindAttribLocation`, `glGetAttribLocation`, `glGetUniformLocation`,
  `glVertexAttribPointer`, `glEnableVertexAttribArray`, `glCreateProgram`,
  `glDeleteProgram`, `glDeleteShader`, `glAttachShader`, `glDetachShader`, …)
* plus the FBO/renderbuffer set (`glBindFramebuffer`, `glGenFramebuffers`,
  `glFramebufferTexture2D`, `glCheckFramebufferStatus`, `glRenderbufferStorage`,
  `glReadPixels`)

**This is the key architectural fact for advanced shaders:** the plumbing for a real
GLES2-style pipeline exists host-side today, even though the guest never calls it.
Shader injection does not require patching the binary.

---

## 3. Our FFP forward renderer — current state

### 3.1 Architecture

* Shader source is **not hand-written**: it is ANGLE's GLES1 emulation shader,
  vendored BSD-3 at `src/platform/angle_gles1/GLES1Shaders.inc` (1292 lines).
* `normalize_angle_glsl()` (`ffp_forward.cpp:73-108`) strips `#version`, `precision …;`
  and `mediump/highp/lowp` keywords; the maths stays byte-for-byte ANGLE's.
* Assembly (`ffp_forward.cpp:165-180`):
  * VS = `#version 330` + `#define kTexUnits 1u` + VS header + `VS_STATE` + VS body
  * FS = `#version 330` + `#define kTexUnits 1u` + FS header + FS uniform defs +
    `FS_STATE` + functions + logic-op wrapper + multitexture + output + main
* **One program, ever.** `ensure_program()` is latched by `g_attempted`
  (`ffp_forward.cpp:582-583`). No program cache, no state variants, no recompilation.
* Full fixed-function state shadow → ~74 uniforms (`FFP_GET` × 74) uploaded **every
  draw**; no dirty tracking.
* Vertices packed into a grow-only `GL_STREAM_DRAW` VBO at 16 floats/vertex
  (`pos4 nrm3 col4 pointsize1 uv4`), read from guest memory behind arena bounds
  checks, de-indexed, always issued as `glDrawArrays`.

### 3.2 Quality levels 2–4 are declared but clamped away

```cpp
// ffp_forward.cpp:809
if (g_quality > TRANSLATE) g_quality = TRANSLATE;   // 2..4 reserved

// ffp_forward.cpp:917
void set_quality(int q) { g_quality = (q <= OFF) ? OFF : TRANSLATE; }

// ffp_forward.cpp:1172  — the ONLY behavioural test of g_quality
if (g_quality <= OFF) { g_skip_off++; g_draws_vanilla++; return false; }
```

`ffp_forward.h:83-89` declares `TRANSLATE / PERFRAG / POM / PBR`, but:

* no code path branches on `g_quality > TRANSLATE`
* the env vars the header promises (`SWORDIGO_FFP_TONEMAP`, `_EXPOSURE`, `_BUMP`,
  `_POM_SCALE`, `_POM_STEPS`, `_HDR`) are **never read** — the implementation only
  reads `LIGHTING`, `DEBUG`, `STATS`, `TRACE`, `SCOPE`
* `set_quality()` has no callers

**Levels 2/3/4 do not exist.**

### 3.3 The advanced shader code was lost in the migration

`docs/graphics_and_rendering/forward_lighting_ffp_verification.md` (Sep 24) documents
the **previous generation** of this module and says "Status: implemented":

* hand-written GLSL 330, `u_mvp` / `u_mv` / `u_normal_mat`, explicit `layout(location=…)`
* derived normal maps from diffuse luminance (cotangent frame + surface gradient)
* parallax occlusion raymarch with binary-search refinement
* Cook-Torrance GGX on roughness/metallic derived from `GL_SHININESS`/`GL_SPECULAR`
* ACES filmic tonemap in-pass, `SWORDIGO_FFP_LIGHTING` default **2**, log line
  `[FFP] forward: GLSL 330 renderer ACTIVE (quality=4)`

**None of that exists in the current source.** Verified: `set_normal_map`, `u_mvp`,
`GLSL 330 renderer`, `parallax`, `GGX`, `roughness` have zero hits in `src/platform/**`.
There is exactly one `ffp_forward.cpp.o` and it is the ANGLE build; the repo has **zero
git commits**, so there is no history to recover it from.

The design survives only in the doc. The code does not.

---

## 4. Feasibility of re-adding levels 2–4 on the ANGLE base — high

### 4.1 The structural facts that make this cheap

From `GLES1Shaders.inc`:

| Fact | Location |
|---|---|
| VS exports eye-space position `out vec4 pos_varying` | `:133`, assigned `:385` |
| VS exports eye-space normal `out vec3 normal_varying` | `:134`, assigned `:387` via `calcNormal()` (honours normalize/rescale) |
| FS **already receives both as inputs** | `:525-526` |
| FS also has `texcoord0_varying` and `color_varying` | `:529-530` |
| Lighting is computed **in the vertex shader**: `doLighting()` | `:155-260`, called `:418` |
| FS main = texture combine → fog → alpha test → logic op | `:1233-1292` |
| FS declares **no** `material_*` / `light_*` uniforms | `:499-567` |

**But the host already queries and uploads them** (`ffp_forward.cpp:565-578`, uploads
`:1271-1272`, `:1285`): `material_ambient/diffuse/specular/emissive/specular_exponent`,
`light_ambients/diffuses/speculars/positions/directions`, spot + attenuation arrays.

GL uniform locations are **program-global across stages**, so declaring these uniforms
in the FS binds them to the same locations the host is already writing. **No C++
changes are required for any of levels 2–4 — they are shader-only.**

### 4.2 Per level

| Level | Work | Cost |
|---|---|---|
| **2 — per-fragment lighting** | Move `doLighting()` out of VS `main` (`:415-420`) into FS `main` before the texture loop; add `material_*`/`light_*` declarations to `FS_STATE` (`ffp_forward.cpp:127-159`); VS must stop lighting so `color_varying` carries the *unlit* primary colour (correct GLES1 semantics). | **Small and local.** No new varyings, no vertex format change, no host changes. |
| **2b — derived normal maps** | Cotangent frame from `dFdx/dFdy` of `pos_varying` and `texcoord0_varying`; height field = luminance of the diffuse sample (`getTextureColor()` already fetches it). Insert before lighting. | Moderate, shader-only. |
| **3 — POM** | Raymarch the derived height, perturb `texcoord0_varying.xy` **before** the texture-combine loop (`:1252`). | Moderate, shader-only; needs an early extra texture fetch. |
| **4 — GGX PBR** | Replace `doLighting`'s spec term with Cook-Torrance; derive roughness/metallic from `material_specular_exponent` / `material_specular`. Uniforms already uploaded. | Moderate, shader-only. |

### 4.3 Two caveats

1. **`kTexUnits 1u`** (`ffp_forward.cpp:163`) — `getTextureColor()` is compiled for a
   single unit, so a second fetch for the height field must be a direct
   `texture(tex_sampler0, …)`, not a unit-loop iteration.
2. **Keep the vendored `.inc` pristine.** The `VS_STATE` / `FS_STATE` splice +
   `normalize_angle_glsl()` is the designed extension point. Put new code in those
   overlays (or new overlay fragments) so a future ANGLE refresh stays possible.

### 4.4 HDR is blocked downstream

`fbo_scaler.cpp` allocates the scene targets as **`GL_RGBA8`** (`:1308`, `:1333`) with
`GL_DEPTH24_STENCIL8` (`:1343`). There is **no `GL_RGBA16F` anywhere** and **no
ACES/tonemap stage** in `fbo_scaler`. Emitting unclamped HDR would clip to white.

So until the FBO chain is float, tonemapping must happen **in-pass** — which is
exactly the correctness argument the old doc made, and it still holds.

---

## 5. Existing advanced-shader assets in the repo (reuse candidates)

| Location | Technique | Wired up? |
|---|---|---|
| `src/tools/av_renderer.cpp` | GLSL 330 **full PBR** — GGX/Smith/Schlick, IBL split-sum + BRDF LUT, normal maps, **ACES**, shadow-map depth pass, DoF, SSAO, bloom | ACTIVE in `ruby` / `swpod` tools — **best GGX/ACES port source** |
| `src/platform/fbo_scaler.cpp` | bloom (soft-knee), SSAO, god rays, FSR1-EASU, CRT, portal warp, Sobel outlines, color grade, procedural LabPBR/GGX composite | ACTIVE, default swordfare path |
| `src/ruby/render/*` | Half-Lambert² + hemisphere ambient + Reinhard, SSAO + AO composite, water, portal vortex, particles, 3-FBO post chain | ACTIVE in `ruby_gg` studio |
| `src/ruby/caver/render/gl_pipeline.cpp` | 5 programs: lit scene, sprite billboards, blit, light-veil, background | ACTIVE in `opensw` |
| `src/platform/vulkan_shaders.glsl.h` | Vulkan uber-shader + full PostFX pass (CA, grade, vignette, grain, CRT), `#version 450` | Orphaned source-of-truth; `compile_shaders.sh` → `vulkan_shaders_spirv.h` → `vulkan_backend.cpp` |
| `src/ruby/zauonlok/{pbr,blinn,skybox}_shader.*` | **CPU** reference PBR (GGX, IBL, ACES, skinning, shadow compare) | NOT compiled (missing `../core/api.h`) — oracle only |
| `src/game/sky_renderer.h` | Procedural sky (gradient, sun/moon, hashed stars, 5-phase day/night) — fixed-function `glBegin/glEnd` | In the frame loop but **`enabled = false`** (`sky_renderer.h:38`) |

---

## 6. Stale documentation to correct

| File | Problem |
|---|---|
| `docs/graphics_and_rendering/forward_lighting_ffp_verification.md` §2, §2.3 | Describes the replaced hand-written renderer: default `2`, log line `GLSL 330 renderer ACTIVE (quality=4)`, `set_normal_map()`, env vars `_EXPOSURE/_BUMP/_POM_SCALE/_POM_STEPS/_HDR`, "Status: implemented". None true for the current tree. |
| `src/platform/ffp_forward.h` toggles block | Promises levels 2–4 and six env vars that are clamped / never read. |
| `src/ruby/render/viewport_shader.h:10-13` | Claims "Cook-Torrance GGX … ACES"; implementation is Half-Lambert² + Reinhard. |
| `src/ruby/render/post_pass.h:27` | Default AO strength 0.80; call site passes 0.45 (`viewport_3d_widget.cpp:1232`). |

---

## 7. Recommended next steps

1. **Re-add level 2 (per-fragment lighting)** — smallest real win, shader-only,
   `doLighting` relocation + `FS_STATE` declarations. Gate it behind the existing
   `SWORDIGO_FFP_LIGHTING` clamp so `1` stays byte-comparable and `2` is opt-in.
2. **Decide the fate of 3/4.** GGX is cheap now that all material/light uniforms
   already reach the host; POM needs the extra fetch and is the riskiest for parity.
3. **Make `fbo_scaler` targets `GL_RGBA16F`** and move tonemap into the composite,
   then `SWORDIGO_FFP_HDR=1` becomes meaningful (this is old-doc next-step 1, still valid).
4. **Port GGX/ACES from `src/tools/av_renderer.cpp`** rather than rewriting — it is
   already compiling GLSL 330 and is the closest match to what was lost.
5. **Fix the four stale docs** in §6, or the next reader will believe levels 2–4 work.
6. **Expose quality in the debug overlay / settings GUI** — `set_quality()` is
   currently uncalled and control is env-only.
