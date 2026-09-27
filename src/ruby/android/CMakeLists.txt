# ============================================================================
# CMakeLists.txt — Ruby GG Mobile Port for Android (Qt6)
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(ruby_gg_mobile LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (DEFINED RUBY_BUILD_VERSION)
    add_compile_definitions(RUBY_BUILD_VERSION="${RUBY_BUILD_VERSION}")
endif()

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Svg)



# Host Lua sources for FileRift bytecode generator
set(LUA_SRCS lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib ldblib liolib lmathlib loslib lstrlib ltablib loadlib linit)
set(HOST_LUA_SRCS)
foreach(name IN LISTS LUA_SRCS)
    list(APPEND HOST_LUA_SRCS ${SRC_DIR}/sre/base/lua/src/${name}.c)
endforeach()

set(RUBY_MOBILE_SOURCES
    # Mobile-first modular UI & Android platform entry (Qt Quick + Material 3)
    ${CMAKE_CURRENT_SOURCE_DIR}/main_mobile.cpp
    # aqt Qt 6.6.3 Android QtCore lacks qt_resourceFeatureZstd (see the file)
    ${CMAKE_CURRENT_SOURCE_DIR}/qt_zstd_feature_fix.cpp
    # The single stb_image implementation for this target (see the file). It has
    # to live outside platform/embedded_assets.cpp, which this target no longer
    # compiles.
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image_impl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_file_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_audio_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_texture_provider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_highlighter_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_quick_viewport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_tools_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ruby_settings_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_orientation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gles_shaders.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/qml.qrc
    ${SRC_DIR}/ruby/docs/docs.qrc
    ${SRC_DIR}/ruby/editor/filerift_highlighter.cpp
    ${SRC_DIR}/ruby/editor/filerift_analyzer.cpp
    ${SRC_DIR}/ruby/editor/filerift_schema.cpp
    ${SRC_DIR}/ruby/database/swordigo_engine_db.cpp

    # Reused Ruby Core & Viewport Gizmo/Picking math
    ${SRC_DIR}/ruby/viewport/ruby_gizmo.cpp
    ${SRC_DIR}/ruby/viewport/ruby_picking.cpp
    ${SRC_DIR}/ruby/viewport/camera_bounds_gizmo.cpp
    ${SRC_DIR}/ruby/core/project_context.cpp

    # Headless Swordigo backends (no ImGui / no desktop dock dependencies)
    ${SRC_DIR}/tools/pod_loader.cpp
    ${SRC_DIR}/tools/pod_writer.cpp
    ${SRC_DIR}/tools/pod_convert.cpp
    ${SRC_DIR}/tools/pod_stamp.cpp
    ${SRC_DIR}/tools/image_decode.cpp
    ${SRC_DIR}/tools/scene_schemas.cpp
    ${SRC_DIR}/tools/scene_loader.cpp
    ${SRC_DIR}/tools/filerift.cpp
    ${SRC_DIR}/tools/boulder.cpp
    ${SRC_DIR}/tools/boulderx.cpp
    ${SRC_DIR}/tools/roller.cpp
    ${SRC_DIR}/tools/rubymesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/av_math_stubs.cpp
    ${SRC_DIR}/tools/scene_asset_resolver.cpp
    ${SRC_DIR}/tools/scene_workspace.cpp
    ${SRC_DIR}/tools/scene_entity.cpp
    ${SRC_DIR}/tools/scene_physics.cpp
    ${SRC_DIR}/tools/scene_collision.cpp
    ${SRC_DIR}/tools/scene_terrain.cpp
    ${SRC_DIR}/tools/scene_lua.cpp
    ${SRC_DIR}/tools/scene_game.cpp
    ${SRC_DIR}/tools/scene_creator.cpp
    ${SRC_DIR}/tools/scene_generator.cpp
    ${SRC_DIR}/tools/scene_generator_v2.cpp
    ${SRC_DIR}/tools/scene_generator_v2_3d.cpp
    ${SRC_DIR}/tools/scene_generator_v3.cpp
    ${SRC_DIR}/tools/scene_v3_db.cpp
    ${SRC_DIR}/tools/scene_program.cpp
    ${SRC_DIR}/tools/obj_loader.cpp
    ${SRC_DIR}/tools/ani_loader.cpp
    ${SRC_DIR}/tools/scn_loader.cpp
    ${SRC_DIR}/tools/map_loader.cpp
    ${SRC_DIR}/tools/gltf_export.cpp
    ${SRC_DIR}/tools/gltf_import.cpp
    ${SRC_DIR}/tools/gltf_bridge.cpp
    ${SRC_DIR}/tools/fbx_import.cpp
    ${SRC_DIR}/tools/ufbx/ufbx.c
    ${SRC_DIR}/tools/tiny_gltf_v3.c
    ${SRC_DIR}/stb/stb_image_write_impl.c
    ${SRC_DIR}/platform/pvr_loader.cpp
    ${SRC_DIR}/platform/pvrtc_decoder.cpp
    ${SRC_DIR}/platform/astc_decoder.cpp
    ${SRC_DIR}/platform/zip_archive.cpp
    ${SRC_DIR}/platform/scl_parser.cpp
    ${SRC_DIR}/platform/data_path.cpp
    ${SRC_DIR}/platform/font_fallback.cpp
    ${SRC_DIR}/platform/gdb_compat.cpp
    ${SRC_DIR}/platform/io_thread.cpp
    # NOTE: platform/embedded_assets.cpp is deliberately NOT compiled here.
    # It is a 657k-line generated table of 183 assets (10 MB decoded): desktop
    # launcher icons, biome background art, UI atlases, fonts. Every caller of
    # embedded_asset()/asset_decode_image() is a desktop-only TU (launcher.cpp,
    # launcher_ui.cpp, asset_viewer.cpp, map_editor.cpp, swordfare_gui.cpp,
    # display.cpp, loading_screen.cpp) and none of them is in this target, so
    # the whole table used to be linked into libruby.so as ~10 MB of .rodata the
    # Android app can never reach. Desktop builds (cmake/components/*.cmake) still
    # compile it, so nothing there changes. Re-add it here only if a mobile source
    # starts calling embedded_asset().
    ${SRC_DIR}/platform/os_external.cpp
    ${SRC_DIR}/android/log.c
    ${HOST_LUA_SRCS}
)

# ── FOSS-compliant flavor ───────────────────────────────────────────────────
# ON  → build without the proprietary GlossHook prebuilt: GlossHook API calls
#        become no-op stubs (runner/gloss_foss_stub.c) and the play-in-game
#        feature is compiled out of the UI. Zero non-free dependencies.
# OFF → full build (default): links the prebuilt libGlossHook.so and enables
#        every feature. Used for our own first-party distribution.
option(RUBY_FOSS_COMPLIANT "Build FOSS-compliant flavor without proprietary GlossHook" OFF)

if (ANDROID)
    # Qt6 on Android creates a shared library loaded by QtActivity
    add_library(ruby SHARED ${RUBY_MOBILE_SOURCES})
else()
    # On desktop/Linux developer testing
    add_executable(ruby ${RUBY_MOBILE_SOURCES})
endif()

set_target_properties(ruby PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
    OUTPUT_NAME "ruby"
)

target_compile_definitions(ruby PRIVATE
    SWORDIGO_NO_IMGUI
    SWORDIGO_MOBILE_PORT
    BATCH_CONVERTER_NO_UI
    "LUAI_FUNC=extern __attribute__((visibility(\"default\")))"
)

target_include_directories(ruby PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SRC_DIR}
    ${SRC_DIR}/ruby
    ${SRC_DIR}/tools
    ${SRC_DIR}/tools/ufbx
    ${SRC_DIR}/stb
    ${SRC_DIR}/platform
    ${SRC_DIR}/sre/base/lua/src
)

find_package(ZLIB REQUIRED)

target_link_libraries(ruby PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::Svg
    ZLIB::ZLIB
    Threads::Threads
    m
)



if (ANDROID)
    target_link_libraries(ruby PRIVATE
        GLESv3
        EGL
        log
        android
    )
    # Set Android package properties
    set_property(TARGET ruby PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    # ── Swordigo In-Engine Runner (GlossHook VFS) ───────────────────────────
    # Full builds link the proprietary prebuilt libGlossHook.so; FOSS builds
    # skip it entirely and compile the no-op API stubs instead.
    if (NOT RUBY_FOSS_COMPLIANT)
        add_library(GlossHook SHARED IMPORTED)
        set_target_properties(GlossHook PROPERTIES
            IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libGlossHook.so"
        )
    endif()

    set(RUBY_RUNNER_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/runner_vfs.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/runner_scene.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/runner_music.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/runner_crasher.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/runner_jni.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/stdstring.c
        ${CMAKE_CURRENT_SOURCE_DIR}/runner/gloss_foss_stub.c
    )
    add_library(ruby_runner SHARED ${RUBY_RUNNER_SOURCES})
    target_include_directories(ruby_runner PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/runner
    )
    if (RUBY_FOSS_COMPLIANT)
        target_compile_definitions(ruby_runner PRIVATE RUBY_FOSS_COMPLIANT)
        target_compile_definitions(ruby PRIVATE RUBY_FOSS_COMPLIANT)
        target_link_libraries(ruby_runner PRIVATE
            ZLIB::ZLIB
            android
            log
            dl
        )
    else()
        target_link_libraries(ruby_runner PRIVATE
            GlossHook
            ZLIB::ZLIB
            android
            log
            dl
        )
    endif()
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(ruby PRIVATE OpenGL::GL)
endif()
