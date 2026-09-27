# Ruby Touch Changelog

All notable changes to the Ruby Touch (Ruby Mobile) Android application and modding studio.
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html) and follows the [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

> **Repository move (2026-09-26):** development moved from GitHub (`TheAevoraLabs`, terminated by automated moderation) to GitLab — [AevoralabsIN/RubyTouch](https://gitlab.com/AevoralabsIN/RubyTouch) — with a mirror at [quantumcreeper/RubyTouch](https://codeberg.org/quantumcreeper/RubyTouch). `github.com` links in older entries below are historical.

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
