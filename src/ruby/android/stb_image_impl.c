// ============================================================================
// stb_image_impl.c — the single stb_image implementation for the Android target
//
// stb_image needs exactly ONE translation unit to define
// STB_IMAGE_IMPLEMENTATION; every other file only includes the header. On the
// desktop that TU is the generated platform/embedded_assets.cpp.
//
// The Android build deliberately does NOT compile embedded_assets.cpp: it is a
// 657k-line generated table of 183 assets (~10 MB decoded) that are all
// desktop-only launcher art, biome backgrounds and UI atlases with no caller in
// this target. See the note in src/ruby/android/CMakeLists.txt. But the app still
// needs the image decoder itself — pod_convert.cpp calls stbi_load /
// stbi_load_from_memory / stbi_image_free and image_decode.cpp pulls the rest —
// so the implementation lives here instead: mobile-only, with no asset table
// attached to it.
//
// If platform/embedded_assets.cpp is ever added back to RUBY_MOBILE_SOURCES,
// remove this file from that list first: two STB_IMAGE_IMPLEMENTATION TUs is a
// duplicate-symbol link error.
// ============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
