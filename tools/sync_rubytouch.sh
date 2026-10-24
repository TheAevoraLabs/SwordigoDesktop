#!/usr/bin/env bash
# ==============================================================================
# sync_rubytouch.sh — Synchronize Ruby Touch mobile repository from SwordigoDesktop
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEST_DIR="${1:-/home/quantumcreeper/RubyTouch}"

echo "=== Syncing Ruby Touch (Lightweight Mobile Distribution) ==="
echo "Source:      ${ROOT_DIR}"
echo "Destination: ${DEST_DIR}"

mkdir -p "${DEST_DIR}/src"

# 1. Sync source trees preserving exact relative paths, excluding desktop-only bloat
echo "--> Syncing src/ruby/..."
mkdir -p "${DEST_DIR}/src/ruby"
rsync -a --delete \
    --exclude "build/" \
    --exclude "build-*/" \
    --exclude "*.apk" \
    --exclude "*.so" \
    --exclude "git/" \
    --exclude "caver/" \
    --exclude "emulator/" \
    --exclude "panels/" \
    --exclude "theme/" \
    --exclude "zauonlok/" \
    "${ROOT_DIR}/src/ruby/" "${DEST_DIR}/src/ruby/"

# Ensure prebuilt runtime libraries (GlossHook) are synced
if [ -d "${ROOT_DIR}/src/ruby/android/libs" ]; then
    mkdir -p "${DEST_DIR}/src/ruby/android/libs"
    cp -r "${ROOT_DIR}/src/ruby/android/libs/"* "${DEST_DIR}/src/ruby/android/libs/"
fi

# Ensure APK assets (runtime plugins) are synced
if [ -d "${ROOT_DIR}/src/ruby/android/assets" ]; then
    mkdir -p "${DEST_DIR}/src/ruby/android/assets"
    cp -r "${ROOT_DIR}/src/ruby/android/assets/"* "${DEST_DIR}/src/ruby/android/assets/"
fi

echo "--> Syncing src/tools/..."
mkdir -p "${DEST_DIR}/src/tools"
rsync -a --delete \
    --exclude "build/" \
    --exclude "build-*/" \
    --exclude "King_Crown*" \
    --exclude "rubyforge/" \
    --exclude "blender_ext/" \
    --exclude "build_studio/" \
    --exclude "asset_viewer.*" \
    --exclude "*.glb" \
    --exclude "*.zip" \
    --exclude "*.bak" \
    "${ROOT_DIR}/src/tools/" "${DEST_DIR}/src/tools/"

echo "--> Syncing src/platform/..."
mkdir -p "${DEST_DIR}/src/platform"
rsync -a --delete \
    --exclude "build/" \
    --exclude "build-*/" \
    --exclude "embedded_assets.cpp" \
    --exclude "xpera/" \
    --exclude "swordfare*" \
    --exclude "launcher*" \
    --exclude "vulkan*" \
    --exclude "fbo_scaler*" \
    --exclude "emulator*" \
    --exclude "gui.*" \
    --exclude "mod_*" \
    --exclude "srt_overlay.*" \
    --exclude "binary_selector.*" \
    --exclude "*.bak" \
    "${ROOT_DIR}/src/platform/" "${DEST_DIR}/src/platform/"

# Write lightweight mobile embedded_assets.cpp stub with stb_image
cat << 'STUB_EOF' > "${DEST_DIR}/src/platform/embedded_assets.cpp"
// =============================================================================
// embedded_assets.cpp — Mobile implementation & stb_image provider
// Ruby Touch loads all assets from Android APK assets/ or filesystem.
// =============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "platform/embedded_assets.h"

extern "C" {

bool embedded_asset(const char* name, const unsigned char** data, size_t* size) {
    (void)name;
    if (data) *data = nullptr;
    if (size) *size = 0;
    return false;
}

bool embedded_asset_has_prefix(const char* prefix) {
    (void)prefix;
    return false;
}

bool asset_decode_image(const unsigned char* data, size_t size,
                        unsigned char** out, int* w, int* h) {
    if (!data || size == 0 || !out || !w || !h) return false;
    int channels = 0;
    *out = stbi_load_from_memory(data, (int)size, w, h, &channels, 4);
    return (*out != nullptr);
}

void asset_image_free(unsigned char* px) {
    if (px) stbi_image_free(px);
}

}
STUB_EOF

echo "--> Syncing src/stb/..."
mkdir -p "${DEST_DIR}/src/stb"
rsync -a --delete "${ROOT_DIR}/src/stb/" "${DEST_DIR}/src/stb/"

echo "--> Syncing src/android/..."
mkdir -p "${DEST_DIR}/src/android"
rsync -a --delete "${ROOT_DIR}/src/android/" "${DEST_DIR}/src/android/"

echo "--> Syncing src/sre/base/lua/..."
mkdir -p "${DEST_DIR}/src/sre/base/lua"
rsync -a --delete \
    --exclude "doc/" \
    "${ROOT_DIR}/src/sre/base/lua/" "${DEST_DIR}/src/sre/base/lua/"

# 2. Sync license, changelog & translations
echo "--> Syncing LICENSE.md, CHANGELOG.md and translations..."
cp "${ROOT_DIR}/LICENSE.md" "${DEST_DIR}/LICENSE.md"
if [ -f "${ROOT_DIR}/docs/RUBY_TOUCH_CHANGELOG.md" ]; then
    cp "${ROOT_DIR}/docs/RUBY_TOUCH_CHANGELOG.md" "${DEST_DIR}/CHANGELOG.md"
fi
mkdir -p "${DEST_DIR}/.github/.localisation"
rsync -a --delete "${ROOT_DIR}/.github/.localisation/" "${DEST_DIR}/.github/.localisation/"

# 3. Create root CMakeLists.txt wrapper
echo "--> Creating root CMakeLists.txt..."
cat << 'CMAKE_EOF' > "${DEST_DIR}/CMakeLists.txt"
cmake_minimum_required(VERSION 3.16)
project(RubyTouch VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src/ruby/android)
CMAKE_EOF

# 4. Create root build_android.sh runner
echo "--> Creating root build_android.sh..."
cat << 'RUNNER_EOF' > "${DEST_DIR}/build_android.sh"
#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${ROOT_DIR}/src/ruby/android/build_android.sh" "$@"
RUNNER_EOF
chmod +x "${DEST_DIR}/build_android.sh"

# 5. Create .gitignore
echo "--> Creating .gitignore..."
cat << 'IGNORE_EOF' > "${DEST_DIR}/.gitignore"
# Build directories and binaries
build/
build-*/
bin/
binw/
*.apk
*.aab
*.so
!src/ruby/android/assets/**/*.so
*.dex
*.zip
*.tar.gz
*.7z

# Object files & archives
*.o
*.a
*.obj
*.lib

# 3D large binary assets & temporary tests
*.glb
*.fbx
*.blend
*.blend1

# Android & Gradle artifacts
.gradle/
local.properties
.idea/
*.iml
.vscode/
.cache/

# Backups & temporary files
*.bak
*.tmp
*.swp
*~
*.log
.DS_Store
__pycache__/
*.pyc

# Agent instructions & internal AI rules
.agents/
.agent/
IGNORE_EOF

# 6. Create Fastlane metadata for F-Droid and store distribution
echo "--> Creating fastlane/metadata/android/..."
FASTLANE_DIR="${DEST_DIR}/fastlane/metadata/android/en-US"
mkdir -p "${FASTLANE_DIR}/changelogs" "${FASTLANE_DIR}/images"

cat << 'TITLE_EOF' > "${FASTLANE_DIR}/title.txt"
Ruby Touch
TITLE_EOF

cat << 'SHORT_EOF' > "${FASTLANE_DIR}/short_description.txt"
3D terrain and scene editor for Swordigo with real-time OpenGL ES rendering
SHORT_EOF

cat << 'CHANGE_EOF' > "${FASTLANE_DIR}/changelogs/2.txt"
Release 1.1: Complete ground mesh texture inspection, authentic biome presets, 3D vertex editing overhaul, 120Hz display refresh support, and dedicated mobile packaging.
CHANGE_EOF

cat << 'FULL_EOF' > "${FASTLANE_DIR}/full_description.txt"
Ruby Touch is a standalone mobile 3D scene studio and terrain editor designed for Swordigo. Engineered natively in C++20 and Qt 6 with OpenGL ES 3.0 hardware acceleration, Ruby Touch delivers desktop-grade level editing and biome inspection directly on Android tablets and smartphones.

Features:

Touch Viewport Navigation
Navigate 3D scenes fluidly using dual on-screen floating thumbsticks designed for handheld touch interaction. The camera controller supports translation, elevation, rotation, and distance panning with hardware-locked refresh rates up to 120Hz.

Ground Mesh Studio
Select, inspect, and edit terrain meshes directly inside the 3D viewport. Ruby Touch renders diamond vertex selection handles and wireframe outlines using hardware depth-tested shaders. Manipulate vertices in real time via ray-plane touch projection, add new control points, remove vertices, and apply structural mesh updates without geometric degradation.

Volumetric WaterMesh Studio
Multi-sheet water volume architecture generating recursive front and back sheets with zero depth-fighting. Synchronized multi-sheet edit mode transforms all companion sheets simultaneously. Authentic 4-corner rectangular box editor strictly conforming to Swordigo's BoundsShape engine parameters with 3D fluid cage overlay, animated wave crest, and HSV spectrum color picker.

Index-FM Remastered File Manager
High-performance mobile file manager with dual Grid and List view modes, batch operations (Copy, Cut, Delete, Select All), Places Drawer for rapid folder navigation, instant file preview modal with metadata inspection, and real-time search filter engine.

Studio Dark UI & Theme Engine
Clean edge-to-edge Material 3 mobile interface completely free of web clutter. 6 studio themes (Ruby Dark, Obsidian AMOLED, Cyber Synth, Emerald Grove, Royal Violet, Nord Frost), customizable studio identities with 7 vector avatars, and polished home navigation.

Biome Texture Inspector
Inspect and modify terrain surface (cap/grass) and cliff (front/wall) textures in place. Switch between authentic presets derived from game scenes, including Plains, Forest, Grove, Florennum, Wasteland, Snowy Peaks, Ice Castle, Volcano, Caves, and Crypt. Create and persist custom modder biome presets via device storage.

Scene and Entity Inspection
Inspect spatial components, node hierarchies, dimension bounds, and object properties across binary scene structures. Place, translate, and orient entities directly on terrain surfaces.

FileRift Bytecode and Scripting
Integrated FileRift schema analyzer and syntax highlighter backed by an embedded ANSI C Lua engine for inspection and modification of level logic and event scripts.

Technical Architecture
Ruby Touch runs fully offline with zero network trackers, zero telemetry, and zero advertisements. Built on modern C++20, Qt 6.6 Quick/QML, and Android NDK r28.

Free and Open Source
Ruby Touch is free software released under the GNU General Public License v3.0 (GPLv3).
FULL_EOF

if [ -f "${DEST_DIR}/src/ruby/android/res/ruby_icon.svg" ]; then
    cp "${DEST_DIR}/src/ruby/android/res/ruby_icon.svg" "${FASTLANE_DIR}/images/icon.svg"
fi

# 7. Create professional README.md (No emojis, Play Store / F-Droid caliber)
echo "--> Creating README.md..."
cat << 'README_EOF' > "${DEST_DIR}/README.md"
# Ruby Touch

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE.md)
[![Platform](https://img.shields.io/badge/Platform-Android%2024%2B-green.svg)](https://developer.android.com)
[![Architecture](https://img.shields.io/badge/Architecture-ARM64--v8a%20%7C%20armeabi--v7a%20%7C%20x86__64-orange.svg)](#architecture)
[![UI Framework](https://img.shields.io/badge/UI-Qt%206.6%20Quick%20%2F%20QML-41cd52.svg)](https://www.qt.io/)
[![Upstream](https://img.shields.io/badge/Upstream-SwordigoDesktop-informational.svg)](https://github.com/TheAevoraLabs/SwordigoDesktop)

Ruby Touch is a high-performance, standalone mobile 3D scene studio, terrain editor, and volumetric water toolchain tailored for Swordigo modders and level designers. Built entirely in modern C++20 and Qt 6 with OpenGL ES 3.0, Ruby Touch brings precision 3D viewport navigation, interactive ground mesh vertex editing, volumetric fluid simulation, and in-place biome texturing to Android devices.

> **Upstream Project Notice**: Ruby Touch is the dedicated mobile distribution and mirror repository for the mobile 3D editor component of [SwordigoDesktop](https://github.com/TheAevoraLabs/SwordigoDesktop). All core engine improvements, tools, and bug fixes are maintained upstream and mirrored here for standalone mobile packaging and F-Droid distribution.

---

## Key Features

### Touch-Optimized 3D Viewport
- **Dual Thumbstick Navigation**: Ergonomic floating thumbsticks for camera movement, flight elevation, and orbit control.
- **Hardware Refresh Rate**: Fully supports 90Hz and 120Hz high-refresh displays for smooth 3D viewport rendering.
- **OpenGL ES 3.0 Shaders**: Real-time rendering of terrain, alpha-blended geometry, sky domes, and bounding gizmos.

### Ground Mesh Studio
- **Direct 3D Vertex Editing**: Tap and drag terrain vertices in 3D space using ray-plane mathematical projection.
- **Native Overlay Rendering**: Diamond vertex selection handles and wireframe outlines rendered directly in the OpenGL pipeline.
- **Topology Tools**: Add new vertices, remove existing points, and cycle through control points via a floating mobile toolbar.
- **Non-Destructive In-Place Apply**: Retains existing component identifiers and dimension objects without geometric trashing.

### Volumetric WaterMesh Studio
- **Multi-Sheet Fluid Volumes**: Recursive front and back water sheet generation with zero depth-fighting artifacts.
- **Synchronized Multi-Sheet Edit Mode**: Moving, scaling, or resizing a water volume transforms all companion sheets in the volume simultaneously in real-time.
- **Decomp-Faithful Water Box HUD**: 4-corner box editor strictly conforming to Swordigo's `BoundsShape` (`x, y, width, height`) with corner and edge dragging.
- **3D Fluid Cage Overlay**: Glowing cyan front outline, back plane, depth struts, animated wave crest, corner diamonds, and midpoint handles.
- **Interactive HSV Spectrum Picker**: Continuous HSV slider, saturation/brightness canvas, fluid presets, and hex color input.

### Index-FM Remastered File Manager
- **Dual View Modes**: Smoothly toggle between visual Grid view (card delegates) and dense List view (row delegates).
- **Batch File Operations**: Integrated floating operations bar supporting Copy, Cut, Delete, Select All, and Deselect.
- **Places Drawer**: Quick jump drawer for Internal Storage, Downloads, Documents, Modding Workshop, and SD card locations.
- **File Preview Modal**: Inspect file metadata, scene hierarchy, and textures with instant preview and opening.
- **Real-Time Search Engine**: Instant search filtering across folders with parent path resolution.

### Studio Dark UI & Theme Engine
- **Edge-to-Edge Material 3 Layout**: Native mobile ergonomics with zero web-SaaS clutter, clean inset dividers, and bottom breathing padding.
- **6 Studio Themes**: Ruby Dark, Obsidian (pure AMOLED black), Cyber Synth, Emerald Grove, Royal Violet, and Nord Frost.
- **Customizable Studio Identity**: Configurable modder name, lightweight name randomizer, and 7 high-res vector avatars.

### Biome Inspector & Terrain Texturing
- **In-Place Texture Swapping**: Change surface cap textures and vertical cliff textures instantly without mesh regeneration.
- **Authentic Biome Presets**: One-touch access to 10 verified biome profiles:
  - Plains (grass_subtle + maybegood)
  - Forest (forest_grass + forest_ground)
  - Grove (grove_grass + grove_ground)
  - Florennum (florennum_ground + florennum_ground)
  - Wasteland (grass_orange + wasteland_ground)
  - Snowy Peaks (snowy_snow + atlon_ground)
  - Ice Castle (icicle + icecastle_ground)
  - Volcano (fire_grass + graveyard_ground)
  - Caves (wasteland_ground + wasteland_ground2)
  - Crypt (crypt_tiles + crypt_tiles)
- **Custom Modder Presets**: Save, name, and manage custom texture combinations persisted via local device storage.

### Scene & Entity Inspection
- **Spatial Object Transform**: Position, rotate, and scale scene objects with touch feedback.
- **Component Inspector**: Read and update entity attributes, visual models, collision shapes, and entity tags.
- **File System Integration**: Integrated storage access for opening and saving .scene, .scl, and .swdm files.

### FileRift & Embedded Lua
- **Bytecode Inspection**: Schema analysis and syntax highlighting for game data structures.
- **Embedded Lua Runtime**: Headless ANSI C Lua engine for validating level scripts and event triggers.


---

## Architecture

```text
RubyTouch/
├── CMakeLists.txt                 # Root project wrapper
├── build_android.sh               # Standalone compilation and APK packaging script
├── fastlane/                      # Store and F-Droid metadata
│   └── metadata/android/en-US/
└── src/
    ├── ruby/                      # Mobile QML UI, C++ bridges, viewport math, and Android entry
    ├── tools/                     # Headless terrain, pod, scene, ufbx, and glTF loaders
    ├── platform/                  # PVR/ASTC texture decoders, zip archives, and data paths
    ├── stb/                       # Image encoding utilities
    ├── android/                   # Android native logging bridge
    └── sre/base/lua/              # Embedded Lua C runtime
```

---

## Building from Source

### Prerequisites
- Android SDK (API 34 or later) with Build-Tools 35.0.0+
- Android NDK (r25b or later; tested with r28)
- Qt 6.6.3 for Android (ARM64-v8a target)
- CMake 3.22+ and Ninja
- JDK 17

### Build Commands

```bash
# Clone the repository
git clone https://github.com/TheAevoraLabs/RubyTouch.git
cd RubyTouch

# Build native shared libraries and package the APK
./build_android.sh

# Or build for all architectures (arm64-v8a, armeabi-v7a, x86_64)
./build_android.sh --all-abis
```

The compiled and signed APK will be output to bin/ruby_gg_mobile.apk. If an Android device is connected via ADB with USB debugging enabled, the script will automatically install and launch the application.

---

## Translations & Localisation

Legal and license documentation is translated into 13 languages located in [.github/.localisation/](.github/.localisation/):

- Indian Languages: Hindi, Bengali, Telugu, Tamil, Marathi, Gujarati
- International Languages: Spanish, French, Chinese, German, Japanese, Russian, Portuguese

---

## License

Ruby Touch is free software licensed under the GNU General Public License v3.0 (GPLv3). See [LICENSE.md](LICENSE.md) for full terms and conditions.

Copyright (C) 2026 The Lawncher Team & The Aevora Labs.
README_EOF

echo "=== Sync Complete ==="
