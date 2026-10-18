# SwordigoDesktop & Swordigo Studio (Ruby GG)

**Native Linux Compatibility Layer, ARM64 JIT Execution Runtime, SRE Modding Framework, and Ruby GG Studio IDE for Touch Foo's *Swordigo*.**

---

## 1. Overview

**SwordigoDesktop** is an advanced native Linux execution environment, modding toolchain, and content-creation studio for *Swordigo*.

Instead of relying on heavy OS virtualization, Android containers, or slow emulators, SwordigoDesktop implements a high-performance native binary-translation and dynamic hooking architecture:

* **Host ELF Loader (`src/loader/`)**: Loads and maps original 64-bit ARM ELF binaries (`libswordigo.so`) into a contiguous 3.5 GB page-aligned virtual memory space with complete symbol relocation support.
* **Dynarmic ARM64 JIT Core (`src/platform/`)**: Executes ARM64 machine instructions using **Dynarmic**, configured with a 512 MB code cache, instruction cache invalidation traps (`IC IVAU`), and native x86_64 FMA hardware acceleration.
* **Native Platform Shims (`src/jni/`, `src/android/`)**: Intercepts Android Bionic libc calls, OpenAL/OpenSL audio, and JNI methods, routing them directly to host SDL3, OpenGL, and ALSA/PulseAudio drivers.
* **Swordigo Runtime Environment (SRE)**: A dedicated guest-side runtime library (`libsre12.so` for 1.4.12 and `libsre13.so` for 1.4.13) injected directly at guest address `0x2000000` via ARM64 function trampolines. SRE replaces legacy atomic spinlocks, optimizes render loops, provides Lua scripting hooks, and provides complete engine introspection.
* **Ruby GG Studio IDE (`src/ruby/`)**: A modern Qt6 level editor and game asset studio featuring the `Graphy` visual node editor, real-time 3D PBR viewport shaders, terrain generators, embedded headless engine preview, and offline Git integration.
* **Swordfare Launcher & Overlay (`src/launcher/`, `src/platform/`)**: A Qt6 desktop launcher and in-game HUD overlay with profile management, save editing, mod loading, and a zero-dependency runtime `dlopen()` video background engine.

---

## 2. System Architecture

```
+-------------------------------------------------------------------------------+
|                             Host Process (x86_64 Linux)                       |
|                                                                               |
|  Ruby GG Studio IDE          Swordfare Launcher        SDL3 / OpenGL Window   |
|  |-- Graphy Node Graph       |-- Profile Manager       |-- FBO Scaler         |
|  |-- 3D Viewport Shaders     |-- Mod & Save Editor     |-- PostFX (SSAO/Bloom)|
|  |-- Embedded Engine Pod     |-- Dynamic dlopen Video  |-- Upscaler (FSR)     |
|  `-- Offline Git Integration `-- In-Game HUD Overlay   `-- Low-Latency Input  |
|                                                                               |
+-------------------------------------------------------------------------------+
|                      Dynarmic A64 JIT CPU Core (512MB Cache)                  |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |                         Guest Address Space (3.5 GB)                    |  |
|  |                                                                         |  |
|  |  libswordigo.so (0x1000000)      libsre12/13.so (0x2000000)     JNI ABI |  |
|  |  [Original ARM64 Code]           [SRE Hooks & Lua Extensions]           |  |
|  |                                                                         |  |
|  |  <-------- Instructions Translated via Dynarmic JIT Core -------->      |  |
|  +-------------------------------------------------------------------------+  |
|                                                                               |
+-------------------------------------------------------------------------------+
|                           Bridge & Trampoline Layer                           |
|  ~400 JNI & Native Function Bridges mapped directly to Host C++ implementations|
+-------------------------------------------------------------------------------+
```

---

## 3. Subsystem Breakdown

### 3.1 Ruby GG Studio IDE (`src/ruby/`)
* **Graphy Visual Node Engine (`src/ruby/graph/`)**: Unreal Engine & Godot-inspired node graph canvas with type-safe color-coded pin palettes, Alt+Drag cutting laser wire slicing, double-click reroute knots (`UK2Node_Knot`), resizable comment bounding frames, and interactive HUD minimap.
* **Modern 3D Viewport & Shaders (`src/ruby/render/`)**: Custom GLSL 330 pipeline featuring energy-conserving wrapped half-Lambert diffuse lighting, hemisphere sky/ground ambient, Blinn-Phong specular sheen, camera-opposed rim lighting, EXP2 depth fog, SSAO pass, and ACES filmic tone mapping.
* **Intelligent 3D Gizmo (`src/ruby/viewport/`)**: Local vs. World coordinate space toggle (`Q`), camera-adaptive plane quadrants that auto-flip toward the user, negative ghost axis tails ($-X, -Y, -Z$), singularity edge-on culling, 3D ruler tick notches, and rotation protractor sectors.
* **Inline Engine Pod (`src/ruby/emulator/`)**: Runs a headless instance of the engine communicating across shared memory POSIX rings (`pod_ipc.cpp`), enabling real-time live gameplay previews directly within an editor dock.
* **Embedded Offline Git (`src/ruby/git/`)**: Embedded static `libgit2` implementation with zero external shared library dependencies, powering local history, visual diffing, and revision rollbacks.

### 3.2 Ruby Touch (Ruby Mobile) (`src/ruby/android/`)
* **Standalone Mobile Distribution**: Ruby Touch (also known as **Ruby Mobile**, formerly Ruby GG Mobile) brings the 3D scene studio, terrain modding, and volumetric fluid editing capabilities directly to Android smartphones and tablets.
* **Touch-Optimized Controls**: Dual floating thumbsticks for 3D camera translation, orbit, and elevation with hardware refresh rates up to 120Hz.
* **Ground Mesh Studio**: Direct 3D vertex manipulation via ray-plane touch projection, diamond vertex selection handles, and non-destructive in-place topology updates.
* **Volumetric Water Mesh Studio**: Multi-sheet water volume generation and synchronized multi-sheet editing, decomp-faithful 4-corner Water Box HUD (`x, y, width, height`), 3D fluid cage overlay with animated wave crest, and interactive HSV spectrum color picker.
* **Index-FM Remastered File Manager**: High-performance dual-mode (Grid & List) mobile file browser with batch operations (Copy, Cut, Delete), Places Drawer, file preview modal, and real-time search filter engine.
* **Studio Dark UI Remaster**: Modern edge-to-edge Material 3-inspired mobile interface featuring 6 studio themes (Ruby Dark, Obsidian AMOLED, Cyber Synth, Emerald Grove, Royal Violet, Nord Frost), customizable studio identities, and 7 vector avatars.
* **In-Place Biome Texturing**: Instant surface cap and vertical cliff texture swapping with 10 authentic game biome presets and local modder preset storage.
* **Standalone Distribution**: Packaged and mirrored as the lightweight standalone repository [RubyTouch](https://github.com/TheAevoraLabs/RubyTouch).

### 3.3 Swordigo Runtime Environment (SRE) (`src/sre/`)
* **Modular Architecture**:
  * `src/sre/base/`: Shared infrastructure including vendored Lua 5.1 runtime, fake-libc POSIX headers, `sre_setjmp.S`, `toml-c`, `luasocket`, `lfs`, and `raknet`.
  * `src/sre/sre12/`: Swordigo 1.4.12 guest runtime, non-atomic string optimizations, and core gameplay hooks.
  * `src/sre/sre13/`: Swordigo 1.4.13 guest runtime featuring complete Caver architecture hooks (`Camera`, `Component`, `Scene`, `ModelLibrary`, `PlayerProfile`), `rbmath` Lua math library, console, and recovery system.
  * `src/sre/extras/`: Extended SRE modules including dynamic FFI (`raijin_ffi`), memory allocators, and filesystem hooks.
* **Thread & Spinlock Optimization**: Replaces GNU libstdc++ atomic copy-on-write `std::string` reference counting (`LDAXR`/`STLXR`) with single-threaded operations, eliminating exclusive monitor contention.
* **Modern VBO Interception**: Intercepts legacy immediate vertex pointer calls, batching and binding them into GPU Vertex Buffer Objects.

### 3.3 Swordfare Launcher & Game Overlay (`src/launcher/`, `src/platform/`)
* **Qt6 Desktop Launcher (`src/launcher/`)**: Modern responsive launcher interface providing Play, Library, Mod Manager, Store, Save Editor, Profile Management, Settings, and Developer Tools.
* **Zero-Dependency Video Player (`src/platform/ffmpeg_dyn.cpp`)**: Video background player using runtime dynamic loading (`dlopen()`). Probes system libraries (`libavformat.so.61`, `.so.60`, `.so.59`, `.so.58`) dynamically—if FFmpeg is absent, video playback degrades gracefully without build or launch failures.
* **FBO Viewport Scaler**: Decouples 960x544 game rendering from desktop resolution with AMD FidelityFX Super Resolution (FSR 1.0), Sharp Bilinear, and CRT scanline filters.

### 3.4 Tooling & Converters (`tools/`, `src/tools/`)
* **`scl_to_graph.py` & `scene_to_graph.py`**: Compiles parsed `.scl` archetype templates and `.scene` level files into interactive visual node graph JSON representations.
* **`scl_graph_viewer`**: Standalone lightweight Qt6 interactive graph viewer executable.
* **`rubyforge`**: Dedicated Blender addon for importing, rigging, animating, and exporting native Swordigo POD models.
* **`boulder` & `rubymesh`**: Standalone terrain mesh generator and intermediate 3D model processing utilities.

---

## 4. Building and Toolchain Setup

### 4.1 System Prerequisites

#### Debian / Ubuntu (22.04 / 24.04 / newer):
```bash
sudo apt update
sudo apt install build-essential gcc-aarch64-linux-gnu \
    qt6-base-dev libqt6opengl6-dev \
    libsdl3-dev libsdl3-image-dev libgl-dev \
    libopenal-dev libvorbis-dev libmpg123-dev zlib1g-dev pkg-config
```

#### Fedora / RHEL / Arch Linux:
```bash
# Fedora
sudo dnf install gcc gcc-c++ gcc-aarch64-linux-gnu \
    qt6-qtbase-devel qt6-qtbase-gui \
    SDL3-devel SDL3_image-devel mesa-libGL-devel \
    openal-soft-devel libvorbis-devel mpg123-devel zlib-devel pkg-config

# Arch Linux
sudo pacman -S base-devel aarch64-linux-gnu-gcc \
    qt6-base sdl3 sdl3_image openal libvorbis mpg123 zlib
```

> **Note on FFmpeg**: FFmpeg development headers and static libraries are **NOT required**. The video background system dynamically detects system libraries at runtime via `dlopen()`.

### 4.2 Building from Source

```bash
# Clone repository
git clone https://github.com/TheAevoraLabs/SwordigoDesktop.git
cd SwordigoDesktop

# Configure CMake build tree
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Compile all targets (Swordfare, Ruby GG IDE, tools, and SRE libraries)
cmake --build build -j$(nproc)

# Run automated tests
ctest --test-dir build --output-on-failure
```

### 4.3 Running

```bash
# Launch the Swordfare desktop launcher & game client
./bin/swordfare

# Launch the Ruby GG Studio IDE
./bin/ruby_gg

# Launch the standalone visual node graph viewer
./bin/scl_graph_viewer quest_vase_graph.json
```

---

## 5. Controls and Keybindings

### Game Input & Overlay
| Key | Action | Subsystem |
| :--- | :--- | :--- |
| **WASD** / **Arrow Keys** | Character Movement | Game Input |
| **Space** / **W** | Jump / Double Jump | Game Input |
| **J** / **Z** | Sword Attack | Game Input |
| **K** / **X** | Magic Spell / Cast | Game Input |
| **I** | Inventory / Equipment | Game Input |
| **Escape** | Pause Menu / Options | Game Input |
| **F1** | Toggle SRT Performance Overlay | In-Game HUD |
| **F4** | Cycle FBO Upscaler Mode (FSR/Sharp/CRT) | Display Scaler |
| **F7** | Toggle Video Background Playback | Platform Video |
| **F8** | Pause / Resume Dynarmic JIT CPU Emulation | Emulator Core |
| **F11** | Open Scene & Display Toolbox | Display Config |
| **`** (Backtick) | Open Raijin Lua Debugger Console | Developer Console |

### Ruby GG 3D Viewport
| Shortcut | Action |
| :--- | :--- |
| **Right-Click + WASD** | Fly-cam Navigation |
| **Alt + Left-Click + Drag** | Orbit Camera Around Target |
| **Middle-Click + Drag** | Pan Viewport Camera |
| **Mouse Scroll** | Zoom In / Out |
| **F** | Frame Selected Object / Bounds |
| **Q** | Toggle Gizmo Space (World $\leftrightarrow$ Local) |
| **Alt + Left-Click + Drag (Graph)** | Wire Slicing Laser (Sever Splines) |
| **Double-Click Spline (Graph)** | Insert Reroute Knot |

---

## 6. Project Multi-License Framework

This project is governed by a modular multi-license structure:

| Component Domain | Directory Path | License Terms | Ownership / Rights Holders |
| :--- | :--- | :--- | :--- |
| **Swordigo Runtime Environment (SRE)** | `src/sre/` (`sre12`, `sre13`, `extras`, `base`) | **GNU GPLv3** | Jointly held by **Lawncher Team** (`Raijin`, `Kiziyon`) & **AevoraLabs (prev OpenSwordigo)** (`QuantumCreeper`, `Msinup`, `ManoK`) |
| **Ruby & Ruby GG Studio IDE** | `src/ruby/` | **GNU GPLv3** | Exclusively **AevoraLabs** |
| **Swordfare Launcher & Game Overlay** | `src/launcher/`, `src/platform/` | **GNU GPLv3** | Exclusively **AevoraLabs** |
| **Tooling & Converters** | `src/tools/`, `tools/` | **GNU GPLv3** | Exclusively **AevoraLabs** |
| **Host JNI & Android Shims** | `src/jni/`, `src/android/` | **MIT License** | Exclusively **AevoraLabs** |
| **Binary ELF Loader & SRE Host** | `src/loader/`, `src/srehost/` | **MIT License** | Exclusively **AevoraLabs** |
| **Third-Party Vendored Code** | `src/tools/ufbx/`, `src/sre/base/lua/`, etc. | Upstream (MIT / zlib / BSD) | Respective authors |

See [`LICENSE.md`](LICENSE.md), [`.github/CLA.md`](.github/CLA.md), [`src/sre/LICENSE.md`](src/sre/LICENSE.md), [`src/ruby/LICENSE.md`](src/ruby/LICENSE.md), and [`src/platform/LICENSE.md`](src/platform/LICENSE.md) for full legal terms.

---

## 7. Community, Governance & Policies

**AevoraLabs (prev OpenSwordigo)** is developed collaboratively under clear legal, architectural, and community frameworks:

* **[Contributor License Agreement (CLA)](.github/CLA.md)** — Defines the 50/50 dual-ownership copyright retention model and multi-license contribution rules.
* **[Contributing Guidelines](.github/CONTRIBUTING.md)** — Toolchain requirements, C++17 / C99 / Python standards, clean-room rules, and PR checklist.
* **[Project Governance Model](.github/GOVERNANCE.md)** — Administrative structure and sole decision-making authority of AevoraLabs & Lawncher Team.
* **[Code of Conduct](.github/CODE_OF_CONDUCT.md)** — Contributor Covenant v2.1 community pledge and enforcement procedures.
* **[Terms of Use & Online Services](.github/TERMS_OF_USE.md)** — Acceptable use policy and conditions for the Lawncher Mod Store network and online infrastructure.
* **[Privacy Policy](PRIVACY_POLICY.md)** — Zero-telemetry policy and local data protection disclosure.

---

## 8. Disclaimer

*Swordigo* is a registered trademark of Touch Foo / Ville Mäkynen. This project is an independent community software development kit and compatibility layer. Original game assets, levels, and proprietary binaries are not distributed with this repository and must be legally obtained from the official game package.
