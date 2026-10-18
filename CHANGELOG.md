# Changelog

All notable changes to Swordigo Desktop.

## [1.4.5] — Multi-Sheet Volumetric Water Engine, Decomp-Faithful Water Box HUD, Index-FM File Manager, and Studio Dark UI Remaster

### Added
- **Multi-Sheet Volumetric Water Engine (`rubywater_meta.h`, `scene_loader.cpp`, `water_renderer.cpp`, `ruby_quick_viewport.cpp`)**:
  - Engine-faithful multi-sheet water volume metadata persistence: stores front-most Z, back-most Z, sheet count, sheet spacing, and group UUIDs in field 15 custom properties (`rubywater_meta.h`) without breaking binary scene compatibility.
  - Recursive front & back sheet generation: eliminates duplicate overlapping sheets and Z-fighting depth artifacts.
  - Synchronized Multi-Sheet Edit Mode: modifying, scaling, dragging, or resizing a water volume dynamically updates and transforms all companion sheets in the multi-sheet volume simultaneously in real-time across both 2D and 3D preview spaces.
- **Engine-Faithful Water Box HUD & 4-Corner Editor (`SceneViewportView.qml`, `ruby_quick_viewport.cpp`)**:
  - Replaced legacy freeform polygon wireframe editor with an authentic rectangular 4-corner box editor conforming to Swordigo's `BoundsShape` (`x, y, width, height`).
  - Interactive 4-corner dragging (`TL`, `TR`, `BR`, `BL`) and edge midpoint dragging with strict 90° rectangular geometry preservation.
  - In-viewport Water HUD with live `Water W×H` dimensions badge, corner cycler, width/height increment steppers (`[-] W [+]`, `[-] H [+]`), reset, discard, and apply controls.
  - 3D Volumetric Fluid Cage visual overlay with glowing cyan front contour, back plane, depth struts, animated wave crest, corner diamonds, and midpoint handles.
- **Index-FM Remastered Mobile File Manager (`FilesView.qml`, `OperationsBar.qml`, `FilePreviewModal.qml`, `PlacesDrawer.qml`, `ruby_file_model.cpp`)**:
  - Deep integration of native index-fm mobile file manager patterns into Ruby Touch.
  - Dual view modes: Grid view (card delegates) and List view (dense row delegates) with smooth animated switching.
  - Full batch multi-select operations with floating `OperationsBar` (Copy, Cut, Delete, Select All, Deselect).
  - Quick Places Drawer for navigation between Internal Storage, Downloads, Documents, Modding Workshop, and SD card roots.
  - Interactive File Preview Modal with metadata inspection, instant scene preview, texture inspection, and direct opening.
  - Dynamic search filter engine with real-time text query filtering.
  - Fixed Android root directory display (`/storage/emulated/0` resolving to clean `"Internal Storage"` instead of `"0"`).
- **Studio Dark UI Remaster & Theme Engine (`Theme.qml`, `SettingsView.qml`, `HomeView.qml`, `IdentityHeader.qml`, `GuideCard.qml`)**:
  - Purged all "web-SaaS bento fluff", artificial pill badges, and duplicate navigation controls.
  - 6 Studio Themes: Ruby Dark, Obsidian (pure AMOLED black), Cyber Synth, Emerald Grove, Royal Violet, and Nord Frost.
  - Complete Settings View overhaul: clean modular grouped sections (Studio Identity, Visual Appearance, Engine & Viewport Preferences, Storage & Workspace Paths, Diagnostics & Information).
  - Identity system upgrade: customizable profile avatar with 7 high-res vector avatars (`avatar_ruby_gem`, `avatar_cyber_ninja`, `avatar_dragon_crest`, `avatar_hero_sword`, `avatar_pixel_ghost`, `avatar_wizard_spark`, `avatar_zenith_core`), customizable display names with lightweight randomizer, and random avatar picker.
  - Home View 6-Point Master Polish:
    - Edge-to-edge layout with bottom breathing spacer, eliminating bottom scroll cutoff bugs above the bottom nav bar.
    - Dynamic hero card eyebrow and action button labels (`ACTIVE SCENE`, `ACTIVE TEXTURE`, `ACTIVE MODEL`, `ACTIVE SCRIPT`).
    - Deduplicated Recent Files list (starts from index 1 when hero shows index 0) with parent directory path format and inset list dividers (`64dp`).
    - Replaced duplicate top-right Settings gear with flat Material Search button.
    - Borderless tonal surface hierarchy across hero cards, modding guide cards, and identity header.

## [1.4.4] — Interactive Spectrum Color Picker, Deadly Fluid Hazard Control, and Decomp-Accurate Damage Pipeline

### Added
- **Interactive HSV Spectrum Color Picker in WaterMesh Studio**:
  - Full continuous HSV spectrum slider + saturation/brightness canvas and quick fluid color swatches for deep body and surface crest colors.
  - Interactive live preview and direct hex input with instant viewport shader sync.
- **Deadly Fluid Hazard & Damage Control (`CollisionShapeComponent` + `DamageComponent`)**:
  - Full integration of Swordigo's hazard collision pipeline: toggle deadly fluid (`InflictsDamage` / `SpecialType: 6`).
  - Adjustable damage rate / amount (`MinDamage` / `MaxDamage`), damage type (Physical/Fire vs Magical), and immunity bypass (`IgnoreTargetImmunity`).
  - Contact reaction triggers matching engine decompilation (`fireblast` woosh / `watersplash`).

## [1.4.3] — Mobile performance overhaul, 120Hz VSync pacing, and native WaterMesh Studio

### Added
- **WaterMesh Studio Revamp (`WaterMeshStudioPanel.qml`)**:
  - Native, responsive controls with dedicated `[ Enter Visual W-Mesh Editor ]` action bar.
  - Themed `IconButton` steppers (`-16` / `+16`) for tile size, body color picker, and dimensional steppers (Width, Depth, X, Y).
  - Clean foundation for arbitrary vertex polygon input and visual wireframe editing.

### Optimized & Fixed
- **Mobile Render & Engine Performance Overhaul**:
  - Removed artificial `Thread.sleep` from `GameRenderer.java` on the GLSurfaceView thread, restoring native VSync pacing and eliminating physics/frame stutter.
  - Dynamic 120Hz display refresh mode selection in `GameRenderer.java`.
  - Disabled 4x MSAA on mobile full-screen FBO (`ruby_quick_viewport.cpp`), eliminating tile resolve and memory bandwidth starvation on mobile GPUs.
  - Throttled continuous 30 FPS water animation timer when idle, running strictly during interactive W-Mesh edit sessions.
  - Batched non-visual scene markers into a single unlit shader pass, cutting hundreds of GPU state rebinding cycles per frame.
- **Orientation & Scene Loading Stability**:
  - Resolved screen orientation lock-out and removed `onWidthChanged` recursive loading overlay triggers in `SceneViewportView.qml`.

## [1.4.2] — Ruby Touch: selectable Zypher ground looks, and the UV / width fixes

> Zypher is a non-vanilla MeshType, so unlike the engine's single hardcoded
> ground look it can offer selectable ones. Everything below was read off the
> Caver source in `OpenSwordigo/arm64_13/functions/Caver/GroundMeshGenerator/`,
> which splits the job exactly the way the pickers do: the **shoulder** swept
> over the rim (`GenerateSurfaceMeshWithRoundHat` / `InsertRoundHatVertices`)
> and the walkable **deck** (`GeneratePlainSurfaceWithHatGaps`), over the
> visible **front** face (`GenerateFrontMesh`).

### Added
- **Selectable ground looks (`zypher::HatStyle` / `zypher::TopStyle`).**
  Sides: `vanilla` (the engine's two-inset-then-drop round hat), `curvy`,
  `plain`, `zigzag`, `none`. Tops: `plain` (the engine's flat surface),
  `spikey`, `porous`, `bubbly`. Every look is real geometry, driven by
  `top_frequency` (features per 100 world units) and `top_depth` (feature height
  / pit depth). Defaults are Vanilla/Plain, so an untouched editor emits exactly
  the geometry it did before.
- **Intelligent pattern collision.** Spikey covers half the spike height, so a
  cone is damageable but still standable; Bubbly gets a small pentagonal arch;
  Porous never collides; anything under `top_min_collision_radius` casts no
  collision at all. Bumps are spliced into the CCW outline in traversal order,
  so the polygon stays simple.
- **Ruby Touch mesh-edit side panel** (`ZypherLookPanel.qml`) — appears only for
  a Zypher ground object, with a line drawing of every side and top style so the
  choice is legible before regenerating. Backed by
  `RubyQuickViewport::setZypherLook()` / `getZypherLook()` / `zypherActive()`.
- `RUBY_ZYPHER_HAT_STYLE`, `RUBY_ZYPHER_TOP_STYLE`, `RUBY_ZYPHER_TOP_FREQ`,
  `RUBY_ZYPHER_TOP_DEPTH` for A/B-ing a look without a rebuild.

### Fixed
- **Weird top texturing.** Vanilla textures through
  `TextureMapping::TexCoordForPosition` = `(p - offset) / scale + 0.5`, fed
  `(arc-length, depth)` — never world XY. Zypher was hard-coding the deck's V to
  `0.0`/`0.5` and the shoulder's to `0.5 + t*0.5`, so the grass sampled a fixed
  half of the atlas and smeared across the whole plate regardless of its size;
  the side walls froze one V across the entire front-to-back span; and the rear
  cap mirrored U against the front. All four now use the engine's depth/scale
  law, and the rear cap matches the front.
- **The top mesh reading slimmer than vanilla.** Vanilla's round hat spans
  `HatWidthOffset1 + HatWidthOffset2` (`P31 + P32` in `InsertRoundHatVertices`);
  Zypher used `offset_1` alone, generating half the declared shoulder. Both the
  desktop and mobile callers now sum the two insets, and the emitter writes the
  whole width into `offset_1` with `offset_2 = 0` so a re-import recovers it
  instead of doubling it.

## [Unreleased] — Converter: the POD block grammar now matches the reference writer

> Report §8f. The two earlier fixes (§8d tags, §8e bone batches) put the right
> *values* in the file; this one puts the right *framing* around them.

### Fixed
- **Every block now carries its `<tag|0x80000000> <0>` close pair.** We were
  closing containers but not leaves. `PVRShamanGUI` ships the reference writer,
  and headless IDA extraction recovered it: `CPVRTModelPOD::SavePOD` @ 0x753370
  brackets every block with `sub_74BA40(f, tag, len)` (`<tag:u16><0:u16>` then
  `<len:u32>`) and `sub_74BAB0(f, tag)` (`<tag:u16><0x8000:u16>` then `<0:u32>`).
  `sub_74BAB0` runs after *every* `sub_74BA40`, with no exceptions, in every
  stock file — vanilla Swordigo assets, PVRShaman's examples and PVRGeoPOD
  output alike. libswordigo's reader tolerates the omission (an unknown tag just
  has its length skipped) which is exactly why this hid: our loader was
  length-driven too, so files round-tripped through `ruby_gg` perfectly. See
  `OpenSwordigo/PVRToolsDecomp/POD_WRITER_GRAMMAR.md`.
- **A stream with `n == 0` no longer carries a `9003 data` tag.** `sub_74BB90`
  returns immediately when its source pointer is NULL, so the block is only
  `9000 eType` / `9001 n = 0` / `9002 nStride = 0` — `rock1.POD`'s `6008 TANGENT`
  block is three tags long. We emitted a meaningless 4-byte placeholder.
- **Materials gained the full stock tag set (3000–3026).** The nine auxiliary
  texture slots `3009..3017` are the part that mattered: stock writes the `-1`
  sentinel for "no map of this kind", we left them at the `calloc`'d 0 — a
  perfectly valid texture index, so the runtime would bind the file's first
  texture as every map the material has a slot for. Flag values (`3018 = 3019 = 1`,
  `3022 = 3023 = 0x8006`) copied verbatim from `rock1.POD`.
- **Scene children reordered** to materials (`2015`), meshes (`2012`), nodes
  (`2013`), textures (`2014`), and the `1002` exporter-options / `1003` provenance
  blocks are back between the version block and the scene block — both purely
  informational to the engine but present in every stock file.

### Added
- **`OpenSwordigo/PVRToolsDecomp/`** — 684 POD-relevant functions from the
  PowerVR toolchain, extracted headlessly with IDA Pro (assembly + Hex-Rays
  pseudocode, indexed by string references and POD tag immediates).
  `PVRShamanGUI/` (284 fns) contains the reference POD reader *and* writer;
  `PVRGeoPODCLI/` (400 fns) contains the FBX/DAE importers and the exporter
  entry points. Distilled in `PVRToolsDecomp/POD_WRITER_GRAMMAR.md`, indexed in
  `PVRToolsDecomp/INDEX.md`.
- **`.scratch/pod_canon.py`** — a canonicalising POD dumper that prints one line
  per block with an explicit `MISSING close` marker, so a freshly converted file
  can be diffed tag-by-tag against `resources/rock1.POD`. Ours now diffs clean
  on mesh, node and material blocks: identical tags, order and nesting.

### Changed
- `tests/pod_game_reader_contract_test.cpp` grew a strict recursive walker
  (`grammar_scan`) requiring the close pair on every block, a byte-for-byte check
  that the file opens with the stock 27-byte `1000 / AB.POD.2.0` block, and
  checks that `1002`/`1003` exist, that no empty attribute stream carries a
  `9003`, and that every material declares all nine `-1` texture slots. 57 →
  **126 checks**, 0 failures.
- `SWORDIGO_POD_PIPELINE_REVISION` 5 → 6, so loading a stale bake now warns.
- All 21 converter-stamped PODs under the user's asset root, plus every `.POD`
  with a `.glb` sibling, were rebuilt. The vanilla originals in
  `assets/resources/soldier/hiro/` were left untouched.

## [Unreleased] — Converter: rigid-skin bake, OBJ→POD converter

> Two more open items from `docs/formats_and_schemas/TODO.md` (S1, E17).

### Added
- **S1 · Dominant-bone (rigid) skin bake — now the glTF default.** The game's
  `C_Matrix4Vector3ArraySkin` reads ONE bone index per vertex and ignores
  weights (`more_model_research.md` §6/§9), so smooth-skinned glTF rigs with
  2–4 influences deformed wrong in-game while looking fine in the ruby viewer.
  `build_pod_from_tg3` gained a `rigid_skin` mode that pre-bakes every vertex to
  its max-weight joint at weight 1.0 (ties → lowest joint slot) and emits
  1-component bone streams. The converter opts in by default
  (`PodConvertOptions::rigid_skin = true`); pass `--smooth-skin` to keep the
  full multi-bone weights (inspection / future soft-skin game mods). The raw
  import APIs default to full weights so the viewer preview stays smooth.
  Verified on `statue.glb`: default conversion dumps `NumComponents 1` on
  `Mesh.BoneIndexList`/`BoneWeightList`; `--smooth-skin` restores 4.
- **E17 · `--obj2pod` converter.** `bin/ruby --obj2pod <in.obj> [out.pod]
  [options]` turns Wavefront OBJ (+ optional `.mtl`) into game-loadable PODs,
  reusing the viewer's tolerant `obj_load` (v/vt/vn corners, quads/ngons
  fan-triangulated, `# ext.*` skin comments ignored for POD) and the shared
  conversion pipeline: DCC top-origin → game bottom-origin V flip (honours
  `--no-flip` and `uv_v_flipped`), `--scale`/`--unit` multipliers, texture
  re-encode to game `.pvr`/`.tex.png` via the same `find_source_image` +
  `encode_texture` path as FBX/GLB, `bake_and_center_model`, `pod_write`.
  Static geometry only (OBJ has no rigs). Verified on a synthetic textured
  cube: 36 verts / 12 tris, UNSIGNED_SHORT indices, texture re-encoded and
  referenced by the POD material.

### CLI
```
bin/ruby --obj2pod model.obj out.POD --scale 39.37   # OBJ → game POD (+ mtl textures)
bin/ruby --glb2pod model.glb out.POD                 # rigid-skin bake now DEFAULT
bin/ruby --glb2pod model.glb out.POD --smooth-skin   # opt out (keep 4-bone weights)
```

## [Unreleased] — Converter: KHR_texture_transform bake, engine-rate clips, u16 index guard

> Three open converter-parity items from `docs/formats_and_schemas/TODO.md`
> (E15, S2, E14 follow-up). All changes verified against the reference loaders
> documented in `pod_master/` and `more_model_research.md`.

### Added
- **E15 · `KHR_texture_transform` is now honoured on glTF import.**
  `gltf_import.cpp` reads the extension from the base-colour textureInfo (and
  from the `KHR_materials_pbrSpecularGlossiness.diffuseTexture` fallback path)
  and bakes `uv' = T · R(θ) · S · uv` into each mesh's UV stream at import.
  The transform is applied in glTF UV space, before the converter's `--flip-v`
  game-convention adapter, so the composition matches the ground truth
  (`game_uv = F(T(uv))`). POD has a single UV channel, so only the
  base-colour/diffuse transform is baked — the game has no notion of
  per-texture UV transforms. A one-line stderr note is printed per baked
  material so DCC-authored transforms are visible in conversion logs.
- **S2 · `--anim-fps <rate>` (default 24) — clips are resampled at the
  engine's rate.** The game hardcodes 24.0 FPS in
  `Caver::PODLoader::CreateAnimationFromFile` (`more_model_research.md` §15,
  float constant `0x41C00000`), so glTF clips sampled at their authored 30/60
  fps played ~25%/60% too fast in-game even though the ruby viewer (which
  honours the POD's fps tag) looked right. `gltf_import_all_clips` gained a
  `target_fps` parameter: when > 0 it bypasses the key-density snap and
  resamples every translation/rotation/scale channel at exactly that rate
  (linear/step/cubic-spline interpolation preserved). The converter passes
  `PodConvertOptions::anim_fps` (default 24); `--anim-fps 0` restores the old
  derive-from-key-density behaviour. Interpolated samples now land on the
  engine's frame grid, matching stock assets like `hiro_run.POD`
  (25 frames @ 24 fps).

### Fixed
- **E14 · u16 index truncation guard confirmed / completed.** The TODO flagged
  `write_index_block` as blindly truncating (`idx & 0xFFFF`). Inspection of the
  current `pod_writer.cpp` shows the width-selecting rewrite already shipped:
  meshes whose indices all fit below 65536 emit the stock `UNSIGNED_SHORT`
  (eType 3) layout, anything larger emits 4-byte 32-bit indices (eType 2,
  `PVRTModelPODDataTypeSize(2)==4`), which the game's reference 9003 reader
  dispatches by data-type size (`pod_master/03_mesh_and_vertex_data.md` §3).
  A regression-test item remains in TODO §1.1 to byte-compare a u16-index mesh
  against a stock POD.

### CLI
```
bin/ruby --glb2pod model.glb out.POD --anim-fps 24   # default; 0 = derive from source
```

## [Unreleased] — Node-transform baking: glTF/FBX PODs now render identically in-game and in ruby

> Root cause of "converted models are the wrong size in-game / don't match the ruby
> visualiser": the converter emitted the glTF scene's root-node transforms
> (Sketchfab unit-conversion scales, GLTF_SceneRootNode axis rotations, 500+-bone
> skeletons) as POD node matrices. The game's `PODLoader::CreateModel` BAKES every
> mesh node's world matrix into its vertices at load, so those hidden transforms
> silently rescaled / rotated / even zeroed the geometry in-game while the ruby
> visualiser (applying matrices at render time) showed something else.

### Fixed — `bake_and_center_model()` in `src/tools/pod_convert.cpp`
New converter step applied to both `fbx_to_pod` and `glb_to_pod` before `pod_write`:
1. **Bake** every mesh node's world matrix (`av::get_node_matrix`, full parent
   chain) into vertex positions + normals, then collapse the node to identity —
   the POD becomes self-contained; no hidden transforms left for the game to
   interpret.
2. **Strip the non-mesh hierarchy** (glTF root wrappers, skeleton bones) for
   static models — 528-node bone trees no longer poison the bbox/feet/AABB
   computations. Bone-animated models keep their skeleton.
3. **Re-center the bounding box at the origin** using AABBs recomputed from the
   BAKED positions (the loader's cached AABBs are pre-bake and stale).

### Verified results (previously broken → fixed)
| Model | Old POD | New POD (scale 1.0) | New POD (game units) |
|---|---|---|---|
| statue.glb | bbox centred at (13, -314, 536), 0.03u | centred at origin, 5.2u | `--scale 39.37` → 29×30×204u (proper statue) |
| minecraft_world.glb | **219 754u** across | centred, 2.8u | `--scale 39.37` → 111×39×77u (proper terrain) |

Reference: shipped `dragonkin_statue.POD` is 66×101×173u — the new outputs land in
exactly that class without guesswork.

### Workflow
```bash
bin/ruby --glb2pod model.glb out.POD --scale 39.37   # metres → game units (~1 inch)
bin/ruby --fbx2pod model.fbx out.POD --unit 100 --scale 1.0   # cm-authored FBX
```
The convert modal's presets (Hero ~70u / Prop ~100u / Decor ~250u) now land the
model at the right in-game size because the baked output is centred and in known units.

## [Unreleased] — Viewer glitch fixes: camera, skinning, textures, alpha (issues.md)

> Four live-viewer rendering glitches root-caused from the actual GLB assets
> (`minecraft_bee.glb`, `mc_demon.glb`) plus runtime shader/state analysis in
> `av_renderer.cpp` and viewport mechanics in `asset_viewer.cpp`. See
> `docs/formats_and_schemas/issues.md` for the full audit.

### Fixed
- **A1 · Camera zoom no longer collapses on big models.** The hardcoded
  `distance > 500.0f` clamp at the old `asset_viewer.cpp:3397` is gone — any model
  with radius > 500 (bee rigs, statues, world maps) used to snap the camera
  inside the geometry on the first wheel tick and could never zoom back out, and
  the default `far_plane = 1000` sliced them. New `apply_model_preview_zoom()` +
  `update_model_preview_clip()` (`asset_viewer.cpp:2975-2995`) clamp distance to
  `[max(r*0.01,0.01), max(2000,r*20)]` and drive `far_plane = max(2000,distance+r*10)`.
  Wired into the wheel handler and all three framing sites (model load, F-key
  reframe, R-key reset).
- **A2 · glTF inverse-bind-matrices now imported.** `gltf_import.cpp` reads each
  skin's `inverse_bind_matrices` accessor, inverts it into a bind-pose world
  matrix, and stores it in `PODNode.bind_matrix` (`has_bind_matrix=true`). The
  merge path in `pod_loader.cpp` now preserves an authored bind instead of
  overwriting it with frame-0 evaluation. Verified on `minecraft_bee.glb`: all
  17 joints carry authored binds (the bee's 921u static bbox is the asset's
  authored scale, not a bug — `bind_matrix` is a runtime-only field that improves
  in-session skinning of rigs posed away from bind at frame 0).
- **A3 · glTF sampler filtering honoured in the model viewport.** Pixel-art
  assets (Minecraft skins) declaring `GL_NEAREST` no longer get force-blurred by
  trilinear + 4× aniso. `gltf_import.cpp` marks `GLTFPBRInfo::Image::nearest`
  from the sampler (glTF values 9728/9984/9986); the viewer carries it through a
  new `gltf_pbr_img_nearest` array + `model_gltf_alias_nearest` map, and
  `load_texture_file` gained a `nearest` parameter that sets `GL_NEAREST` min/mag
  with no mipmaps/aniso. Both the PBR-spill path and the alias path honour it.
- **A4 · `alphaMode BLEND/MASK` depth + cutoff handling.** `PBRMaterial` gained
  `alpha_mode` + `alpha_cutoff`; `pbr_render_mesh` now: OPAQUE → blend off; MASK
  → blend off + alpha test at the material cutoff with depth write on (cutout
  geometry keeps occluding, e.g. foliage); BLEND → blend on with `depthMask(false)`
  (restored after the draw) so transparent surfaces can no longer depth-cull the
  geometry behind them. The viewer plumbs `pm.alpha_mode`/`pm.alpha_cutoff` from
  the glTF material.
## [Unreleased] — Interconversion pipeline hardening (POD ↔ FBX ↔ glTF ↔ .scene)
## [Unreleased] — Interconversion pipeline hardening (POD ↔ FBX ↔ glTF ↔ .scene)

> Root-caused from the decompiled Caver engine (OpenSwordigo `arm32`/`arm32_13`/`arm64_13`)
> plus the shipped `resources/` corpus. See
> `docs/formats_and_schemas/pod_fbx_gltf_interconversion_report.md` for the full audit.

### Fixed — converter correctness
- **glTF `--scale` no longer squares on root-scaled models.** `gltf_import.cpp` was
  scaling mesh positions AND the root node's matrix-3x3 / TRS scale channel, so any
  glTF whose root node carried an authored scale (unit-convert, assemblies) rendered at
  scale². Now only world-space positions + translation are scaled; node scale channels
  stay authored. Regression-tested: root scale 10 + `--scale 2` → 2.0u (was 40u).
- **FBX UV V no longer double-flips for left-handed sources.** ufbx already flips V
  when mirroring 3ds Max/Unity FBX scenes (`model.uv_v_flipped == true`);
  `fbx_to_pod` now skips its own flip in that case — matching the viewer's `dcc_uv`
  logic.
- **ETC1 alpha loss closed.** Textures carrying a non-opaque alpha channel now encode
  as uncompressed RGBA8888 (PVR flags `0x12`); opaque textures keep ETC1. Opaque path
  keeps premultiplied alpha; RGBA keeps straight alpha.
- **PVR header byte-matches stock.** Legacy 52-byte header now ships `mip_count=0`,
  `flags=0x10036`, `mask[]={FF,FF,FF,00}` — verified identical to shipped
  `resources/*.pvr` across all 13 uint32 fields.
- **FBX unit handling is explicit.** New `--unit`/`-u` multiplier (e.g. `100.0` for cm
  sources, `39.37` for m→inches) layered on top of the class-level `--scale`, since
  ufbx silently converts cm-authored FBX to metres at load.
- **GLB export flips V by default** (`gltf_export_glb` new `flip_v` param, default
  true) so exported GLBs display textures upright in Blender/DCC tools.

### Fixed — scale UX (the #1 "I can't guess the size" pain)
- **Convert-modal auto-fit presets now use measured stock heights.** Replaced the
  stale `Hero≈20u / Statue≈62u` with real values from the corpus: `Hero/NPC ~70u`
  (ash=74.2), `Prop/Door ~100u` (dragonkin_statue=100.6, door=103), `Decor/Tree ~250u`,
  `Large/Boss ~500u`. Resulting game-bounds readout + per-unit reference line updated.
- **Scene-object scale UI clamped to ≥ 0.01** to match the game's `DrawModels`
  render-cull threshold (objects below 0.01 are invisible in-game).
- **Stale `fbx_import.h` doc** corrected: the loader converts to metres, not a unit
  cube.

### Verified (no regressions)
- POD `pod_load → pod_write → pod_load` round-trip is bit-exact (knight.POD,
  statue.POD: pos/nrm/uv/idx maxdiff = 0.0).
- `bin/ruby --glb2pod MEGA/statue.glb --scale 1.0` reproduces the shipped statue POD
  bbox to 3 decimals.

## [Unreleased] — Scene Player: decompiled AI controllers + game camera (arm32/arm64 port)
## [Unreleased] — Scene Player: decompiled AI controllers + game camera (arm32/arm64 port)

### Fixed — AI entities never moved in Visualise mode (root cause)
- `sp::is_animated_entity()` used a strict 16-char suffix compare against `"MonsterController"`, which **failed** because protobuf-decoded component type names carry a trailing NUL/space (`'BatMonsterController'` is 20 chars, not 19). So `classify_ai()` found the archetype but `has_ai()` returned false → monsters were frozen. Switched to substring `find()` matching (robust to padding), with `MonsterDeathController` explicitly excluded.

### Added — AI controllers ported from IDA/Ghidra-decompiled `Caver::*MonsterControllerComponent::Update`
- **WalkingMonster** (0x2A1394): velocity patrol between roam bounds (home ± range), turns at the edges, faces travel direction, chases Hiro at 1.5× when in range.
- **BatMonster** (0x293550): Lissajous figure-8 orbit around home — `tx = 200·cos(phase·0.2·2π)`, `ty = 250·sin(phase·0.5·2π)` — chased at the engine's 600 u/s cap.
- **BouncingMonster** (0x294B58): gravity (1500) + jump-velocity (800) hop loop with ground snap, faces the hero when in range.
- **ChargingMonster** (0x2962EC): 100×250 detection box; on contact charges at 2.6×, turns around after a 0.4 s pause once it passes the hero.
- **Static / Archer / Leaping / Snapping / Projectile** classified and handled (stay + face hero, etc.).
- `comp_float()` decodes the real `WalkSpeed` (field 13) from the component protobuf payload when present, so monsters use their authored speed.

### Fixed — Hiro camera was zoomed in / hero off-frame
- Camera distance raised from `radius × 5` (~265) to `radius × 14` (~740), matching the real game's ~2835-unit view offset so the hero fills ~15% of the frame instead of half; pitch 28→24°.
- `player_apply_camera` now opens `far_plane` to `max(4000, distance·8)` so the scene around Hiro isn't clipped.

### Added — playback timeline
- Overlay timeline: Restart button, Loop toggle, scrubber + duration (0 = infinite) for deterministic seek; `player_rumble()` public helper for game-style camera shake.

## [Unreleased] — Scene Player subsystem (visualise / play-Hiro mode)

### Added — new `src/tools/scene_player.h/.cpp` playback engine
- **Scene Player** toolbar buttons + Mode menu: **Visualise Scene Playing** (runs the scene's animation clocks + simple AI simulation for monster/controller entities — patrol, bob, facing) and **Spawn Hiro and Play** (playable hero).
- **PlayHiro**: Hiro spawns at the scene's SpawnPoint (or origin), game-style smooth-lag camera (yaw/pitch/distance, dt-scaled exponential smoothing), A/D or arrow keys to run, Space to jump with gravity + land recovery, `hiro_stand` / `hiro_run` / `hiro_jump` / `hiro_jumpland` POD switching via the animation-only-POD merge path, Esc to exit.
- **Playback overlay window**: mode name + elapsed time, Pause/Resume, 0.1×–4× speed slider, live Hiro position/animation/grounded HUD, object + AI-moving counters, status line.
- Engine mutates the scene's transforms so the visualizer shows real motion; per-object frames feed the existing animation clocks.
- Tick is gated to the 3D visualizer tab and silenced while typing in text fields (hero keys never fire during editing).

### Added — Mode menu in the top menu bar
- New **Mode** menu (File · Edit · **Mode** · View · Help) with **Visualise Scene Playing**, **Spawn Hiro and Play** and **Stop Playback** — same engine as the toolbar Mode button, one clear entry point.

### Fixed — props must never auto-animate
- **Doors and chests stay closed.** Animation is now gated on `sp::is_animated_entity()` — only objects with a `*MonsterController` / Char / HeroEntity component loop their POD animation. Props that only carry `DoorController`/`BushController` + `AnimationController` + `KeyframeAnimation` hold their closed frame 0 instead of opening/closing forever.
- `MonsterEntity`/`EntityInfo` alone are no longer treated as animated (lava, shadowblobs and emitters carry them but must not drift/patrol) — real monsters always pair them with a `*MonsterController`.
- **Hiro camera fixed** — the camera used a distance of 8 world units while the hero model is ~106 units tall, placing the camera inside Hiro. `player_begin` now probes `hiro_stand.POD`'s radius (≈53) and sets `distance = radius × 5` (~265), `pitch 28°`, yaw 0 (+Z side), focusing on Hiro's chest so he sits in the lower third of the frame, game-style.

### Fixed — door rendering & T-pose animation
- **Doors/props no longer face the viewer**: the ModelComponent payload's baked Y-rotation (field 2, e.g. castle_lockdoor = 270°, chair = 90°) is now parsed (`model_y_rotation`, wire-type-guarded) and applied via the new `swk::object_render_matrix()` in both scene draw passes — matching main.js `addModel` rotation.y. Gizmo/picking stay on the unrotated world matrix.
- **Models no longer stuck in T-pose**: per-object POD animation clocks replace the single global frame; 2-frame models (doors, switches, toggles) hold their closed frame 0 instead of oscillating.
- Dev/QA: `RUBY_PLAY_MODE=1|2` env hook auto-starts the player for headless verification.

## [Unreleased] — Clean OBJECTS / PROPERTIES inspector (npm.md layout)

### Reworked — scene Tree tab inspector per .agents/npm.md (main.js parity)
- **OBJECTS panel (top)**: compact header (count + Add / Frame buttons), one search box, and a clean flat list of every scene object — eye toggle, category icon, blue selection highlight, template-name dim chip. Double-click still frames the object in the 3D view; Ctrl+click multi-select preserved.
- **PROPERTIES panel (bottom)**: header shows the selected object; inspector matches the npm.md mockup — **Template** combo (full-width dropdown over every scene-library template, content-hashed cache), **Name** field, collapsible **Position** (X / Y / Depth / Rotation / Scale labeled drags), collapsible **LocalAABB** (X / Y / Width / Height with new Rectangle parse + re-serialize helpers, auto-creates when missing, Clear button), and **Hidden** ON/OFF toggle.
- Kept the professional extras below the fold: Mesh Statistics, Components (badges + editable fields), Scene Metadata, and Frame / Duplicate / Materialize / Delete actions.

### Fixed
- **Undo snapshots**: every drag/input now snapshots once on its own activation (the old code snapshotted on the wrong item or not at all for position/AABB edits).
- Removed the obsolete category-filter combo / Expand-all toolbar that cluttered the old browser (category grouping still available via the Add-Object palette).
- Dev/QA env overrides: `RUBY_INIT_TAB` (open a specific scene tab) and `RUBY_SEL_OBJ` (auto-select an object index) for headless screenshot verification.

## [Unreleased] — Professional Objects panel (main.js OBJECTS parity)

### Added — two-pane Objects panel in the scene Tree tab (`draw_scene_inspector`)
- **Categorized browser**: 11 professional categories (Enemies, Entities & NPCs, Items, Geometry & Terrain, Effects, Lighting, Controllers, Audio, Portals & Doors, Utility, Other) derived from the arm32 component-interface taxonomy (`src/tools/scene_categories.h`) — group objects like Blender's outliner.
- **Search + category filter + expand/collapse-all** toolbar; per-row eye (hide/show) toggles, template-name chips and live vertex-count chips.
- **Inspector pane** (`draw_object_inspector`): drag-edit transform (pos/rot/scale), reset-identity, per-axis value drags, mesh stats (verts/tris), full component list, and Materialize Template / Duplicate / Delete actions.
- **Ctrl+click multi-select** preserved; double-click frames the object in the 3D view.

### Added — categorized Game Templates palette in the Add Object browser
- `scene_list_templates()` (scene_loader) enumerates every template from embedded ObjectLibrary + external .scl bytes → **206 real game templates** in grass_house.scene (woodendoor, swingingaxe, pressureplate, castle_lockdoor…).
- Templates grouped under the same 11-category taxonomy with component tooltips; `template_add_to_scene()` spawns in front of the camera (target→eye offset), materializes inherited components, frames + selects it.
- Template cache is content-hashed (library byte sizes) so re-saving a scene refreshes the palette.

### Fixed
- `ICON_FA_BOX` UTF-8 escape (was encoding U+E466 private-use; now correct U+F466) + added CUBES/BOX/PERSON/puzzle/link/icons used by the new panels.
- `scene_categories.h` now self-contained (includes IconsFontAwesome6.h).

## [Unreleased] — Lighting / PostFX overhaul

### Changed — Swordigo-faithful lighting (see .agents/npm.md)
- **Compact localized point lights**: falloff rewritten from a hard `1 − smoothstep(0, r, dist)` blob (×2 boost) to an inverse-square-ish core with a soft knee to zero just past the influence radius — torches now produce small, believable pools of light instead of giant uniform spheres.
- **Hemisphere ambient**: new `uAmbientGround` — platform tops catch the sky fill while undersides/ceilings/wall bases fall toward a dark ground fill, so corners and platform edges read instead of crushing to black.
- **Surface response preserved**: N·L (normal-dot-light) + distance attenuation + ambient; colored lights tint the material instead of overwriting it (per-channel clamp + no 2× boost); multiple lights still accumulate naturally.
- **Compact emitter markers**: glow billboards now track camera distance (6–30 world units) instead of scaling with the influence radius — no more giant green dots; bright cores still feed the bloom bright-pass.
- **Light debug overlay**: new “Show radius rings” toggle draws an editor-style emitter cross + influence-radius ring per point light, visually separated from the real illumination.
- **Overlay veil softened**: darkness veil keeps a readable floor (was ~20% ambient at full veil, now ~35%) — vanilla darkness, not crushed black.
- **Scene lights default ON** when a scene contains lights (was off) for the faithful-atmosphere preview.

### PostFX — Swordigo-faithful default profile + presets
- Defaults retuned: bloom 0.22/0.95, saturation 1.05, contrast 1.04, brightness 0.03, warmth 0.05, sharpen 0.30, vignette 0.22 — PostFX finishes the lighting, never compensates for it.
- New **Profile** presets in Settings → Post Processing: Vanilla Swordigo / Cinematic / Clean-Neutral (one-click starting points).

### Fixed
- Unlit line rendering (`render_lines`) now uploads the ground-ambient uniform too (was left stale from the scene pass).
- NaN guard on point-light `normalize()` when a vertex sits at the light position.

## [v8.0 Beta 3] — 2026-08-06

### Added — Ruby SDK Scene Editor (2-day drop)
- **Vanilla Lighting & Atmosphere**: Light components parsed from real scene data (type/intensity/color/offset/radius) plus SimpleGlow torch/fire as warm point lights; up to 16 point lights with smooth falloff; vanilla golden-sun + deep cave-ambient defaults; Scene Lights panel + render toggle.
- **Camera Ports**: Scene opens framed at `spawn_default`; View → Camera Ports submenu lists every SpawnPoint for one-click jumps (framing distance 160, pitch 30°).
- **Background Layer Picker**: Inspector section to pick any background layer object by name/texture (Auto = first visible default).
- **Emissive Bloom for Torches**: Additive camera-facing glow billboards at Light/SimpleGlow positions feeding the PostFX bloom bright-pass.
- **Depth Fog**: Distance-based atmospheric darkening in the model shader (vanilla cave depth), camera-distance-scaled, toggleable.
- **Fluid Rendering (Water / Lava)**: WaterMesh components parsed (shape rectangle + FrontColor/SurfaceColor + texture mapping) and rendered as animated semi-transparent fluid sheets with sine-wave surface + scrolling UV; `water_2x.pvr` auto-resolved; Settings toggle.
- **High-DPI Render Scale**: 1x/1.5x/2x/3x FBO + PostFX rendering (default 3x) with logical-size UI display.
- **HiDPI Texture Filtering**: Trilinear mipmaps + 4x anisotropy on uncompressed texture paths (compressed ETC1/PVRTC kept GL_LINEAR).
- **Ground Mesh Generator Rework**: Mesh tab hides the right inspector panel and donates full width to the editor; live 3D preview enlarged; preview textures now probe `_2x.pvr` variants (fixes white preview); GM depth editing.
- **Template/Proxy Cleanup**: Purely non-visual objects (Light/Portal/CollisionShape/SpawnPoint/controllers) render as tiny neutral markers instead of orange "missing model" dots; `scanscene` classifies them as non-visual.
- **GLTF import/export + POD writer wired into build** (were referenced but never linked into `bin/ruby`).
- **About page credits**: DanielSpaniel (official Python FileRift + Boulder engine, translated to C++), MrSinup, OpenSwordigo.

### Fixed
- **Scene-viewport input bug**: viewport hover captured immediately after `ImGui::Image` so orbit/zoom/pan/picking work reliably.
- **Render-scale mismatch**: picking/overlays use logical size while FBO/PostFX use scaled size — no more broken picking at non-1x scales.
- **Spawn camera too close**: reframed at in-game hero distance instead of hugging the spawn point.
- **Model viewport**: fog/light state no longer leaks into POD + GM previews.
- **`scanscene` stale-binary crash** resolved by forced rebuild; reports `non-visual` vs `missing-models` correctly.
- **`scene_loader` water parsing**: WaterMesh rect + colors + texture resolved from real scene data (verified against fire_part1 + florennum_jail_boss).

---

## [v8.0 Beta 2] — 2026-07-31

### Added
- **RakNet Network Framework Integration**: Integrated RakNet cross-platform UDP engine into core build tree with low-level packet serialization, GUID handshakes, and peer connection state management.
- **Modular Shared Object (.so) Build System**: Split monolithic code into 9 modular shared libraries (`libswcore.so`, `libswemu.so`, `libswgfx.so`, `libswgui.so`, `libfilerift.so`, `libswordfare.so`, `libswfmt.so`, `libsre.so`, `libswordigo.so`) under `bin/` with `swordfare` primary executable.
- **Swordfare Editor (IntelliJ) Overhaul**: Stateful multiline tokenization (`LexState`), custom `.styx` theme parser (`FileRift (Grove)`, `BatSyntax`), quote-aware comment stripping, string/comment-aware bracket error checking, line token caching, and Swordigo SDK autocomplete/symbol metadata (`Game`, `Character`, `Scene`, `PhysicsObject`, `Vector3`, `Shardshi`).
- **OptiX Architectural Framework**: 10 master technical research specifications for modern graphics, PBR lighting, reflection hooks, and Dynarmic ARM64 JIT hardware acceleration.
- **Dynarmic ARM64 JIT Tuning**: 512MB code cache allocation, unsafe FMA3 host instruction flags, and compiler optimization flags (`Unsafe_UnfuseFMA`, `Unsafe_ReducedErrorFP`, `Unsafe_InaccurateNaN`).

### Fixed
- **Regex Engine Stability**: Resolved `std::regex_error` lookbehind issues in `FileRift (Grove)` theme.
- **Quote-Aware Config Comment Stripping**: Prevented comment stripping from destroying strings containing `//` or `--`.
- **Modern Memory Access**: Standardized buffer access using C++17 `buffer->data()`.
- **RPM Build Spec**: Fixed `/usr/bin/ruby` unpackaged file error in RPM packaging.

---

## [v8.0 Beta 1] — 2026-07-11

### Added
- **Universal Mod Virtual File System (VFS)**: 5-layer prioritized asset loading hierarchy.
- **Native Uncompressed PVR & PVRTC Decoding**: Host-side PVR/PVRTC decompression.
- **SwKiwi Native UI Overlays**: Interactive ImGui window overlays with input gating.
- **True Lua Console & SRE Native Invocation**: `caver.call()` API and direct raw memory Read/Write APIs.

---

## [v7.1] — 2026-06-25

### Added
- **SRE Compatibility Check for Custom Instances** — added `custom-` path prefix check in `main.cpp`, ensuring `libsre.so` correctly loads for user-added modded instances.
- **SRE Dependency Registration** — added automatic dependency inclusion for `libsre.so` in `launcher_ui.cpp` when importing custom instances with SRE enabled.

### Changed
- Package version bumped to 7.1.0 with nickname **hot-fix**.
- **Config Persistence Priority** — config loaded from `instances.json` now merges and takes priority over filesystem-scanned metadata on duplicates, retaining game type and assets settings.

### Fixed
- **Custom Modded Instance Crash** — resolved startup abort crashes (empty `.POD` errors) on custom modded instances by ensuring the correct guest binary (e.g. RLSwordigo binary instead of vanilla) is copied and `libsre.so` hooks are properly initialized.
- **Bolt/Timer misbehavior and Text input crash** — removed old known limitations from documentation as SRE hooks now fully intercept and resolve them.

---

## [v7.0] — 2026-06-24

### Added
- **Dynarmic JIT compiler** — ARM64 code at near-native speed (60fps), replaces Unicorn as default
- **RLSwordigo support** — roguelike spinoff playable through custom instances
- **KiwiAPI / Combatch mod compatibility** — SWKiwi modloader hooks (Phase 1 & 2)
- **Bauble API** — Phase 3.3 trinket/bauble system hooks
- **Achievement System** — Phase 3.4 achievement hooks
- **io.open + fgets/fscanf bridges** — full file I/O for Combatch mod
- **_longjmp registration** — Lua error recovery support
- **Instance management overhaul** — custom assets folders per instance

### Changed
- Dynarmic is the default ARM64 backend (`make DYNARMIC=1`)
- Unicorn Engine retained as `--no-dynarmic` fallback
- F12 fullscreen toggle preserves native display aspect ratio (16:10, etc.)
- Launcher assets moved to `launcher/` subfolder for clean RPM/DEB installs
- Package version bumped to 7.0.0

### Fixed
- F12 fullscreen exit forced 16:9 aspect ratio — now queries actual window size
- Launcher icon/texture loading on packaged installs (RPM/DEB)

---

## [v6.5] — 2026-06-23

### Added
- **Complete modding documentation** — 22-file, 414KB `modapi/` suite
- **Vulkan renderer backend** — full GLES 1.x fixed-function pipeline emulator
- **Graphics API in F3 debug overlay** — shows OpenGL/Vulkan in HUD
- **ImGui launcher enhancements** — background texture, instance icons, Add Instance button
- **sre_mod.c** — mod config shared memory protocol at guest address 0x49000

### Changed
- Launcher version bumped to v6.5
- Vulkan radio button no longer labeled "(WIP)"

---

## [v6.0] — 2026-06-22

### Added
- **Full GUI DrawRect stack** — 8 native hooks for total GUI rendering control
- **Native GUI rendering** — GUIButton, GUILabel, GUIFrameView reimplemented in C
- **Button text override system** — rename any button at draw time ("Offers" → "Options")
- **Scoped button hiding** — remove IAP shop button cleanly
- **Save Editor** — built into launcher (coins, health, mana, XP, weapon, keys)
- **Asset Viewer** — standalone tool for browsing game textures, audio, scenes
- **PostFX pipeline** — 6 shader presets (Cinematic, Retro, Fantasy, Noir, Ethereal, Atmospheric)
- **SRE Lua Libraries** — custom Lua environment with Mini.*, LNI.*, Components.* tables
- **Virtual Filesystem** — `sre_vfs.c` for future mod asset layering
- 30+ active SRE hooks (up from 17 in v5.0)

### Fixed
- **Wastelands spinlock** — infamous ARM64 freeze in Wastelands region resolved
- **Death freeze** — ad SDK path hang fixed, instant checkpoint respawn
- **Death loop** — duplicate hook entry at 0x347efc removed
- **GameSceneView::Update** — entire function body recovered from TVPG snapshot
- **Player stats** — 13 volatile globals properly defined and populated

---

## [v5.0] — 2026-06-21

### Added
- **libsre.so** — Swordigo Runtime Engine: 17 active hooks replacing entire subsystems
- **Full music system** — MusicPlayer replaced with OpenAL command interface (6 hooks)
- **Instant death respawn** — `ShowAdMaybe` → `GameOverViewDidContinue` (no ads, no restart)
- **HUD reimplementation** — `GameSceneView::Update` fully rewritten in C
- **Smart coin bar** — shop-aware auto-hide with 3s fade timer
- **Damage flash** — red overlay on HP decrease
- **Player stats export** — HP/Mana/Coins/XP/Level/ATK visible in F3 overlay
- **GameState pointer** — direct host-side game memory access
- **Music loop watchdog** — detects and restarts stopped looping music
- **SRE version gate** — only loads for v1.4.12 ARM64, safe skip for other binaries
- **Lua error recovery** — ARM64 `setjmp`/`longjmp` for safe `lua_pcall`
- **Background rendering hooks** — 3 custom hooks for sky/parallax

### Changed
- Architecture renamed: **SRT** (Swordigo Runtime) as the overall framework
- Mod menu cleaned: only GAME section (Speed/Pause/Camera) visible
- Package builder updated: includes `libsre.so` in ARM64 engine dirs
- README rewritten for SRT architecture

### Fixed
- Music not repeating when track ends in same world
- Non-atomic string operations eliminate STXR spin loop hangs (4 hooks)

## [v4.5r] — 2026-06-19

### Added
- **Save Editor in Launcher** — browse and edit `.gplayer` save files directly from the launcher window
  - Lists all saves with name, area, level, progress percentage, and playtime
  - Editable fields: Coins, Health, Mana, XP, Equipped Weapon, Keys
  - Text input fields with keyboard navigation (TAB/ESC/ENTER)
  - Creates `.bak` backup before writing
  - Success/failure status feedback

### Changed
- Launcher version bumped to v4.5r
- ARM64 `pthread_create` now discards thread functions (matching ARM32 behavior)

### Fixed
- ARM64 entity processing: NOP'd setup call at `0x580708` with `MOV X0, XZR` to prevent spinlock on empty entity lists

### Removed
- In-game save editor (Mods menu) — replaced by the launcher-integrated version

### Known Issues
- **ARM64**: Game freezes (spinlock) when entering Wastelands
- **ARM32**: Timer-based repeating spikes and boss gate triggers don't work
- **Launcher**: Instance icons show placeholder in .deb package installs

---

## [v4.0r] — 2026-06-18

### Added
- **ARM64 (AArch64) emulation** via Unicorn Engine ARM64 backend
- ARM64 ELF loader with full RELA relocations
- ARM64 JNI bridge (200+ functions)
- Dual-arch launcher — ARM32 and ARM64 instances side-by-side
- **GPU draw call batcher** — streaming VBO reduces draw calls from 80-140 to 15-25 per frame
- **FSR 1.0 upscaling** option
- **PolyMC-inspired launcher** — instance card grid with icons, version badges, arch labels
- Custom instance import via file dialog
- GLSL 330 shader migration

### Changed
- Launcher redesigned with card grid + detail panel layout
- Threading bridges now functional (mutex, cond, create, once)

---

## [v3.0r] — 2026-06-17

### Added
- **HiDPI / native resolution rendering**
- **SDL2 → SDL3 migration**
- Standalone RPM and DEB packaging
- Binary Selector v2 with v1.4.12 as default

### Fixed
- Death screen hang resolved
- Improved FBO scaler with Sharp Bilinear mode

---

## [v2.0r] — 2026-06-17

### Added
- **SRE PostFX pipeline** — SSAO, God Rays, Volumetric Light Shafts
- 7 visual presets (Cinematic, Retro, Fantasy, Noir, Ethereal, Atmospheric)
- **Unified Launcher GUI** with binary selection + graphics API picker
- SHA-256 binary validation
- Depth buffer as texture for shader effects

---

## [v1.0r] — 2026-06-16

### Added
- Initial release
- Custom ARM ELF loader with Unicorn Engine
- JNI bridge layer (200+ functions)
- OpenGL rendering, OpenAL audio
- Keyboard + gamepad controls
- Save system persistence
