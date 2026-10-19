# Ruby Touch Changelog

All notable changes to the Ruby Touch (Ruby Mobile) Android application and modding studio.
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html) and follows the [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

> **Repository move (2026-09-26):** development moved from GitHub (`TheAevoraLabs`, terminated by automated moderation) to GitLab — [AevoralabsIN/RubyTouch](https://gitlab.com/AevoralabsIN/RubyTouch) — with a mirror at [quantumcreeper/RubyTouch](https://codeberg.org/quantumcreeper/RubyTouch). `github.com` links in older entries below are historical.

## [1.4.5] - 2026-10-19

### Added
- **Full 3D Euler Rotation Persistence Engine (`scene_loader.cpp`, `scene_loader.h`, `ruby_quick_viewport.cpp`)**:
  - Full round-trip preservation of all 3 Euler rotation axes: Pitch (X), Yaw (Y), and Roll (Z) into Swordigo binary `.scene` files.
  - Automatically synthesizes and synchronizes `ModelComponent` overrides (Tag 2 `YRotation` for Yaw, Tag 4 `XRotation` for Pitch) alongside Tag 6 (`Rotation` for Roll) for any template instance (trees, rocks, props, decorations, items, monsters) and placed 3D models.
  - Explicit 0.0-degree rotation preservation: fixed regression where rotating an object to 0° on Pitch or Yaw dropped the tag or fell back to template defaults, ensuring 0° reset and fine tuning persist verbatim across save and reload cycles.
  - In-memory `scene_sync_model_rotation`: rotation mutations immediately update the object's `ModelComponent` in memory, preventing `scene_refresh` from reverting rotations during active editing sessions.
  - Guarded against double model rotation in `swk::object_render_matrix` when both world Euler angles and model component angles exist.
- **Precision Case-Based Uniform Rotation Module (`SceneViewportView.qml`, `ruby_quick_viewport.cpp`)**:
  - Context-adaptive rotation controls in the right-edge viewport deck:
    - **3D Model / Prop mode**: 3-way segmented axis selector (`X` Pitch red, `Y` Yaw green, `Z` Roll blue) with color-coded axis pips.
    - **2D Planar Mesh mode**: locked Planar Roll (`Z`) mode with cyan badge for GroundMesh and WaterMesh.
    - **No Selection mode**: Camera Orbit mode with camera yaw readout.
  - Hold-to-repeat steppers (`[-15°]` / `[+15°]`) with 70ms tick timer and subtle haptic feedback for fast, comfortable orientation.
  - Interactive live readout jog-scrubber chip with real-time drag scrubbing (0.75° sensitivity) and tap-to-reset.
  - One-tap quick action buttons: `180°` flip, `45°` snap, and `0°` reset.
  - Emits `objectTransformChanged` immediately on object selection change so the angle chip updates in real-time.
- **Audio Sanitizer Pipeline for Android Modding (`ruby_file_model.cpp`, `ruby_audio_bridge.cpp`)**:
  - Long-press context action on `.ogg` and `.wav` audio files to sanitize and clean metadata tags.
  - Strips incompatible comment headers and invalid tags, sets album identifier to `resources`, and re-encodes pure Ogg Vorbis streams to prevent audio engine crashes on Android.
- **Clean Icon-Only Center Gizmo Dock (`SceneViewportView.qml`)**:
  - Restored compact 36dp floating glassmorphic pill bar in the bottom center.
  - Icon-only delegates (`eye`, `move`, `rotate`, `scale`) with active accent glow.
  - 100% Zero-Emoji UI compliance across all toolbars, badges, and dialogues.

## [1.4.4] - 2026-10-18

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
- **Interactive HSV Spectrum Color Picker in WaterMesh Studio**:
  - Full continuous HSV spectrum slider + saturation/brightness canvas and quick fluid color swatches for deep body and surface crest colors.
  - Interactive live preview and direct hex input with instant viewport shader sync.
- **Deadly Fluid Hazard & Damage Control (`CollisionShapeComponent` + `DamageComponent`)**:
  - Full integration of Swordigo's hazard collision pipeline: toggle deadly fluid (`InflictsDamage` / `SpecialType: 6`).
  - Adjustable damage rate / amount (`MinDamage` / `MaxDamage`), damage type (Physical/Fire vs Magical), and immunity bypass (`IgnoreTargetImmunity`).
  - Contact reaction triggers matching engine decompilation (`fireblast` woosh / `watersplash`).
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

---

## [1.4.3] - 2026-10-17

### Added
- **WaterMesh Studio Revamp (`WaterMeshStudioPanel.qml`)**:
  - Native, responsive controls with dedicated `[ Enter Visual W-Mesh Editor ]` action bar.
  - Themed `IconButton` steppers (`-16` / `+16`) for tile size, body color picker, and dimensional steppers (Width, Depth, X, Y).
  - Foundation for arbitrary vertex polygon input and visual wireframe editing.

### Optimized & Fixed
- **Mobile Render & Engine Performance Overhaul**:
  - Removed artificial `Thread.sleep` from `GameRenderer.java` on the GLSurfaceView thread, restoring native VSync pacing and eliminating physics/frame stutter.
  - Dynamic 120Hz display refresh mode selection in `GameRenderer.java`.
  - Disabled 4x MSAA on mobile full-screen FBO (`ruby_quick_viewport.cpp`), eliminating tile resolve and memory bandwidth starvation on mobile GPUs.
  - Throttled continuous 30 FPS water animation timer when idle, running strictly during interactive W-Mesh edit sessions.
  - Batched non-visual scene markers into a single unlit shader pass, cutting hundreds of GPU state rebinding cycles per frame.
- **Orientation & Scene Loading Stability**:
  - Resolved screen orientation lock-out and removed `onWidthChanged` recursive loading overlay triggers in `SceneViewportView.qml`.

---

## [1.4.1] - 2026-09-26

### Added
- **FOSS build flavor** (`./build_android.sh --foss`): compiles out the proprietary GlossHook dependency entirely — zero proprietary libraries in the APK. The play-in-game and start-scene-in-game features are disabled (their buttons hidden via `playInGameAvailable`); all other features work identically.
- **Persistent release signing**: `build_android.sh` now accepts `RUBY_KEYSTORE_PATH` / `RUBY_KEYSTORE_PASS` / `RUBY_KEY_ALIAS` / `RUBY_KEY_PASS` for a stable release key, falling back to the previous ephemeral debug key when unset.
- **Native Crash Logger**: Ported `runner_crasher` signal handler with `_Unwind_Backtrace` and `dladdr` symbol resolution logging faulting PC and backtraces to logcat and `crash.log`.
- **In-Viewport Scene Play**: Added direct "Play Scene in Game" action inside the Scene Editor's Viewport Menu.

### Fixed
- **Play-in-Game Crash After Ground Mesh Resize (P0)**:
  - Fixed unsaved launch state: `launchGame()` and `startSceneInGame()` now synchronously commit active mesh edits (`applyMeshEdit()`) and flush dirty scene data to disk (`saveScene()`) before launching `GameActivity`.
  - Fixed ground mesh metadata desync in `RubyQuickViewport::regenerateGroundMesh`: updated `target.ground_mesh_raw` and `target.ground_mesh_fields` from re-parsed geometry, preventing `scene_save()` from encoding using stale wire buffers.
  - Added defensive mesh re-encoding validation in `scene_loader.cpp`: strict vertex count / stride checks (`num_vertices * 32 <= vertex_data.size()`), vertex index bounds clamping, and automatic `num_faces` (wire field 2) synchronization.
  - Fixed short `fread` handling in runner VFS hooks and guarded `BinaryFile::Open` against write-mode hijacking of read-only mod assets.
- **Scene Mode Sound & Music Playback**:
  - Eliminated `AssetManager` race in `GameActivity` by setting up the native engine environment prior to GLSurfaceView renderer initialization.
  - Ported `Port.java` MediaPlayer audio bridge and hooked all 6 `MusicPlayerJNI` symbols (`LoadFile`, `Play`, `Pause`, `Stop`, `SetLooping`, `SetVolume`), restoring background music from vanilla APK `res/raw/` or mod `music/` directories.
  - Added `Caver::AndroidIsGoogleGameServicesAvailable` stub returning false to prevent GMS hangs in runner mode.
  - Added comprehensive hook resolution and audio query diagnostics in logcat.
- Build-environment fixes: define `qt_resourceFeatureZstd` (missing from aqt's Qt 6.6.3 Android binaries) and pass `-encoding UTF-8` to `javac`.

---

## [1.4] - 2026-09-25

### Added
- **Scene Object Import & Placement Studio**:
  - Direct 3D model import supporting all 395 vanilla `.POD` models with automatic bounding box measurement and native `ModelComponent` instantiation.
  - Multi-material and skinned model safety: eliminated crashes from character models entering 2-unit terrain shaders.
  - Prefab terrain import supporting 275 templates from `groundmeshes.scl` across 25 biomes (`plains`, `forest`, `caves`, `forgotten_keep`, `snowy`, `fiery_depths`, etc.).
  - Preserved 2D editable polygon contours and surface textures on imported prefabs.
  - Archetype library discovery for external `.scl` containers (`rocks`, `platforms`, `caves_stuff`, `traps`, etc.).
  - Quick primitives placement (`Spawn Point`, `Portal Gate`, `Empty SceneObject`).
  - Mobile touch-first landscape `PlaceObjectDialog` modal with real-time search and biome category chips.
  - Dedicated **Clone** button on the left HUD quick action column.
- **Experimental Procedural Mesh Generators**:
  - Added two experimental terrain mesh generation backends: **Zypher** (smooth Catmull-Rom spline curves with Bishop twist-free frame extrusion and profile bevels) and **Zenith** (BoulderX concave hull triangulation). Each serves different scopes, selectable in Settings (default remains vanilla-safe **Boulder**).
  - Wired up Zypher / Zenith settings to GUI.

### Fixed
- **Complex Mesh Viewport Throttling**:
  - Fixed throttling and frame lag in complex 3D meshes with varied Z-depths and high triangle counts.

### Known Limitations
> [!WARNING]
> - **In-App Text Editor**: Please do not use it for production scripts; use `filerift.py` + MT Manager or external editor.
> - **Experimental Generators (Zypher / Zenith)**: Can corrupt or alter scene geometry if unconstrained. Always create backups before remastering meshes.
> - **Audio in Ruby Launcher**: Sound is not available in the internal Ruby test runner. The Ruby launcher is designed exclusively for fast, instant geometry and scene layout testing. It does NOT have the Kiwi API; please use Kiwi Launcher / Lawncher for full audio and gameplay testing.

### Future Plans
- Lawncher-format mod package import and export.
- Fixing sound issues in Ruby Launcher.
- Note: Kiwi API will never be integrated into Ruby; use Lawncher for full testing.
- Importing full animation models without bug/glitching.
- Further stability improvements and tools.

### Downloads & Mirrors
- Discord CDN: [RubyTouch-1.4.apk](https://cdn.discordapp.com/attachments/1536609175134806026/1552761046060503270/RubyTouch-1.4.apk?ex=6ab6c8ea&is=6ab5776a&hm=4210a0a6c3193548de76d3823aa993d942eb6d4cf5669062fbf9d473ddec84c1&) (14.65 MB)
- Local Artifact: `bin/RubyTouch-arm64-v8a.apk`

---

## [1.3] - 2026-09-24

### Added
- **Gameplay Integration**: Added game play launch option (thanks to Lawncher).
- **Direct Scene Launch**: Added option to boot and load the game directly into a specific `.scene`.

### Changed & Fixed
- **Mesh Editing**: Updated 2D/3D mesh edit workflow (managed in v1.4).
- **UI Performance**: Fixed lag in the main window and viewport initialization.

### Downloads & Mirrors
- MediaFire: [RubyTouch-arm64-v8a.apk](https://www.mediafire.com/file/kus9lpyxf1spj6q/RubyTouch-arm64-v8a.apk/file)

---

## [1.2] - 2026-09-24

### Architecture Overhaul
- **Multi-Architecture ABI Support**: Added cross-compilation support for 64-bit ARM (`arm64-v8a`), 32-bit ARM (`armeabi-v7a`), 32-bit x86 (`x86`), and 64-bit x86 (`x86_64`).
- **Standalone Distribution Repository**: Split Ruby Touch into a dedicated lightweight repository (`TheAevoraLabs/RubyTouch`) with automated syncing and release pipelines.
- **GitHub Releases Integration**: Standardized release builds and distribution channels.

### Downloads & Mirrors
- GitHub Release: [v1.2 Release Tag](https://github.com/TheAevoraLabs/RubyTouch/releases/tag/v1.2)

---

## [1.1] - 2026-09-18 (Beta)

### Added
- Complete ground mesh texture inspection and authentic biome presets.
- 3D vertex editing overhaul with touch-first drag handles.
- 120Hz high refresh rate display support for smooth camera navigation.
- Dedicated mobile APK build pipeline and APK packaging scripts.

### Downloads & Mirrors
- MediaFire: [ruby_gg_1.1.apk](https://www.mediafire.com/file/t76b0hiilr0dj8k/ruby_gg_1.1.apk/file)

---

## [1.0] - 2026-09-15 (Alpha)

### Initial Release
- Initial proof-of-concept mobile Android port of the Ruby GG modding studio (`ruby_gg_mobile.apk`).
- Basic scene loading, 3D camera navigation, and touch controls.

### Downloads & Mirrors
- MediaFire: [ruby_gg_mobile.apk](https://www.mediafire.com/file/4gv56639ovryo62/ruby_gg_mobile.apk/file)
