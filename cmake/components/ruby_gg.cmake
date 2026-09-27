# ============================================================================
# Component: ruby_gg — Swordigo Studio Qt6 Frontend
#
# Architecture:
#   ruby_gg  ->  Qt6 (Widgets, OpenGLWidgets, Core, Gui)
#                     |
#   Swordigo Headless Core Backends (src/tools/):
#     - pod_loader / pod_writer / pod_convert
#     - scene_loader / scene_workspace
#     - filerift (markup & protobuf)
#     - boulder (GroundMesh)
# ============================================================================

if (SWORDIGO_BUILD_RUBY_GG)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Network)

    set(RUBY_GG_SOURCES
        ${SRC_DIR}/ruby/main.cpp
        ${SRC_DIR}/ruby/editor/ruby_main_window.cpp
        ${SRC_DIR}/ruby/editor/ruby_title_bar.cpp
        ${SRC_DIR}/ruby/editor/studio_idle_widget.cpp
        ${SRC_DIR}/ruby/editor/script_ide_widget.cpp
        ${SRC_DIR}/ruby/editor/virtual_text_buffer.cpp
        ${SRC_DIR}/ruby/editor/doc_viewer_dialog.cpp
        ${SRC_DIR}/ruby/editor/new_file_dialog.cpp
        ${SRC_DIR}/ruby/editor/model_convert_dialog.cpp
        ${SRC_DIR}/ruby/editor/desktop_integration_dialog.cpp
        ${SRC_DIR}/ruby/editor/apk_session_panel.cpp
        ${SRC_DIR}/ruby/docs/docs.qrc
        ${SRC_DIR}/ruby/editor/filerift_schema.cpp
        ${SRC_DIR}/ruby/editor/filerift_analyzer.cpp
        ${SRC_DIR}/ruby/editor/filerift_highlighter.cpp
        # RubyGizmo & Raylib Math / Picking (Gem 0)
        ${SRC_DIR}/ruby/viewport/ruby_gizmo.cpp
        ${SRC_DIR}/ruby/viewport/ruby_picking.cpp
        ${SRC_DIR}/ruby/viewport/camera_bounds_gizmo.cpp
        # RubyGit offline Git integration & FileRift diff (Gem 1 & 2)
        ${SRC_DIR}/ruby/git/ruby_git.cpp
        ${SRC_DIR}/ruby/git/ruby_git_diff.cpp
        ${SRC_DIR}/ruby/database/swordigo_engine_db.cpp
        ${SRC_DIR}/ruby/database/filerift_ls.cpp
        ${SRC_DIR}/ruby/viewport/viewport_3d_widget.cpp
        ${SRC_DIR}/ruby/viewport/scene_loading_overlay.cpp
        ${SRC_DIR}/ruby/render/viewport_shader.cpp
        ${SRC_DIR}/ruby/render/water_renderer.cpp
        ${SRC_DIR}/ruby/render/portal_renderer.cpp
        ${SRC_DIR}/ruby/render/particle_engine.cpp
        ${SRC_DIR}/ruby/render/light_rig.cpp
        ${SRC_DIR}/ruby/render/fbo_chain.cpp
        ${SRC_DIR}/ruby/render/ssao_pass.cpp
        ${SRC_DIR}/ruby/render/post_pass.cpp
        ${SRC_DIR}/ruby/render/glb_model.cpp
        ${SRC_DIR}/ruby/panels/animation_control_bar.cpp
        ${SRC_DIR}/ruby/panels/texture_viewer_panel.cpp
        ${SRC_DIR}/ruby/panels/audio_viewer_panel.cpp
        ${SRC_DIR}/ruby/panels/scene_hierarchy_panel.cpp
        ${SRC_DIR}/ruby/panels/asset_browser_panel.cpp
        ${SRC_DIR}/ruby/panels/inspector_panel.cpp
        ${SRC_DIR}/ruby/panels/template_palette_panel.cpp
        ${SRC_DIR}/ruby/panels/template_inspector_panel.cpp
        ${SRC_DIR}/ruby/panels/console_panel.cpp
        ${SRC_DIR}/ruby/panels/local_history_panel.cpp
        ${SRC_DIR}/ruby/panels/lighting_panel.cpp
        ${SRC_DIR}/ruby/core/project_context.cpp
        ${SRC_DIR}/ruby/tools/ruby_tools_workspace.cpp
        ${SRC_DIR}/ruby/tools/ground_mesh_studio.cpp
        ${SRC_DIR}/ruby/graph/graphy.cpp
        ${SRC_DIR}/ruby/graph/graphy_canvas.cpp
        ${SRC_DIR}/ruby/graph/graphy_layout.cpp
        # Native document -> Graph builder: .scene/.scl bytes straight into the
        # canvas, no Python and no intermediate JSON.
        ${SRC_DIR}/ruby/graph/graphy_scene_builder.cpp

        # ---- Inline engine pod (1.4.13 mini emulator dock) ----
        ${SRC_DIR}/platform/pod_ipc.cpp
        ${SRC_DIR}/ruby/emulator/workspace_detect.cpp
        ${SRC_DIR}/ruby/emulator/engine_pod.cpp
        ${SRC_DIR}/ruby/emulator/preview_view.cpp
        ${SRC_DIR}/ruby/emulator/engine_preview_panel.cpp
        # caver::ILiveEngine over the pod above — one engine runner, no second.
        ${SRC_DIR}/ruby/emulator/live_engine_pod.cpp

        # ---- Headless Swordigo backends (no ImGui dependency) ----
        ${SRC_DIR}/tools/pod_loader.cpp
        ${SRC_DIR}/tools/pod_writer.cpp
        ${SRC_DIR}/tools/pod_convert.cpp
        ${SRC_DIR}/tools/scene_loader.cpp
        ${SRC_DIR}/tools/filerift.cpp
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
        ${SRC_DIR}/tools/boulder.cpp
        ${SRC_DIR}/tools/boulderx.cpp
        # Zypher (Gen 3). viewport_3d_widget.cpp / ruby_quick_viewport.cpp call
        # zypher::generate_terrain_object(), so the generator must be in the link;
        # it was omitted here, which left every target built from this list
        # (ruby_gg, scene_save_preserves_edit_test, scene_undo_test) with an
        # undefined reference to zypher:: and therefore unbuildable by ctest.
        ${SRC_DIR}/tools/roller.cpp
        ${SRC_DIR}/tools/rubymesh.cpp
        ${SRC_DIR}/tools/obj_loader.cpp
        ${SRC_DIR}/tools/ani_loader.cpp
        ${SRC_DIR}/tools/scn_loader.cpp
        ${SRC_DIR}/tools/map_loader.cpp
        ${SRC_DIR}/tools/gltf_export.cpp
        ${SRC_DIR}/tools/gltf_import.cpp
        ${SRC_DIR}/tools/gltf_bridge.cpp
        ${SRC_DIR}/tools/fbx_import.cpp
        ${SRC_DIR}/tools/intellij.cpp
        ${SRC_DIR}/tools/batch_converter.cpp
        ${SRC_DIR}/tools/apk_session.cpp
        ${SRC_DIR}/platform/zip_archive.cpp
        ${SRC_DIR}/platform/desktop_integration.cpp
        ${SRC_DIR}/tools/ufbx/ufbx.c
        ${SRC_DIR}/tools/tiny_gltf_v3.c
        ${SRC_DIR}/platform/win_dll_dir.cpp
        ${SRC_DIR}/platform/pvr_loader.cpp
        ${SRC_DIR}/platform/pvrtc_decoder.cpp
        ${SRC_DIR}/platform/astc_decoder.cpp
    )

    add_executable(ruby_gg ${RUBY_GG_SOURCES})

    # Enable Qt MOC automated processing for signals/slots
    set_target_properties(ruby_gg PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
    )

    target_compile_definitions(ruby_gg PRIVATE
        SWORDIGO_NO_IMGUI
        BATCH_CONVERTER_NO_UI
    )

    target_include_directories(ruby_gg PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/ruby
        ${SRC_DIR}/ruby/git/include
        ${SRC_DIR}/tools
        ${SRC_DIR}/tools/ufbx
        ${SRC_DIR}/stb
        ${SRC_DIR}/platform
        ${SRC_DIR}/sre/base/lua/src
        ${MPG123_INCLUDE_DIRS}
        ${VORBISFILE_INCLUDE_DIRS}
    )

    # --- Offline Embedded libgit2 (static, stripped, zero .so dependency) ---
    file(GLOB LIBGIT2_UTIL_SRCS
        "${SRC_DIR}/ruby/git/util/*.c"
        "${SRC_DIR}/ruby/git/util/allocators/*.c"
        "${SRC_DIR}/ruby/git/util/hash/*.c"
        "${SRC_DIR}/ruby/git/util/hash/sha1dc/*.c"
        "${SRC_DIR}/ruby/git/util/hash/rfc6234/*.c"
    )
    if (WIN32)
        file(GLOB LIBGIT2_PLATFORM_SRCS "${SRC_DIR}/ruby/git/util/win32/*.c")
    else()
        file(GLOB LIBGIT2_PLATFORM_SRCS "${SRC_DIR}/ruby/git/util/unix/*.c")
    endif()
    file(GLOB LIBGIT2_XDIFF_SRCS "${SRC_DIR}/ruby/git/deps/xdiff/*.c")
    file(GLOB LIBGIT2_CORE_SRCS "${SRC_DIR}/ruby/git/libgit2/*.c")

    add_library(git2_static STATIC
        ${LIBGIT2_UTIL_SRCS}
        ${LIBGIT2_PLATFORM_SRCS}
        ${LIBGIT2_XDIFF_SRCS}
        ${LIBGIT2_CORE_SRCS}
    )
    set_target_properties(git2_static PROPERTIES
        C_STANDARD 99
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    target_include_directories(git2_static
        PUBLIC
            "${SRC_DIR}/ruby/git/include"
        PRIVATE
            "${SRC_DIR}/ruby/git/include"
            "${SRC_DIR}/ruby/git/util"
            "${SRC_DIR}/ruby/git/libgit2"
            "${SRC_DIR}/ruby/git/deps/xdiff"
    )
    target_compile_definitions(git2_static PRIVATE
        _GNU_SOURCE
        _FILE_OFFSET_BITS=64
    )
    target_link_libraries(git2_static PRIVATE
        ZLIB::ZLIB
        Threads::Threads
    )
    if (WIN32)
        target_link_libraries(git2_static PRIVATE pcre2-8 ws2_32 crypt32)
    endif()
    if (UNIX AND NOT APPLE)
        target_link_libraries(git2_static PRIVATE rt)
    endif()

    target_link_libraries(ruby_gg PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGLWidgets
        Qt6::Network
        OpenGL::GL
        ZLIB::ZLIB
        swcore
        swfmt
        swpod
        filerift
        caver
        git2_static
        SDL3::SDL3
        ${SDL3_IMAGE_LINK}
        # Audio Viewer decoders — already REQUIRED project deps (swordfare
        # links them too), so this adds no new runtime dependencies.
        ${MPG123_LIBRARIES}
        ${VORBISFILE_LIBRARIES}
        ${SWORDIGO_LIBM}
        ${SWORDIGO_SOCKLIB}
    )

    if (NOT WIN32)
        target_link_libraries(ruby_gg PRIVATE dl pthread util)
    endif()

    # shm_open / mmap for the engine-pod frame ring (pod_ipc.cpp).
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ruby_gg PRIVATE rt)
    endif()

    # Standalone SCL Visual Node Graph Interactive Viewer
    add_executable(scl_graph_viewer
        # The standalone viewer stays Qt-only: the native document builder lives
        # in ruby_gg, which already links the scene loader and FileRift.
        ${SRC_DIR}/tools/scl_graph_viewer.cpp
        ${SRC_DIR}/ruby/graph/graphy.cpp
        ${SRC_DIR}/ruby/graph/graphy_canvas.cpp
        ${SRC_DIR}/ruby/graph/graphy_layout.cpp
    )
    set_target_properties(scl_graph_viewer PROPERTIES
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
    )
    target_include_directories(scl_graph_viewer PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/ruby
    )
    target_link_libraries(scl_graph_viewer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )

    # ========================================================================
    # Component: ruby_gg_mobile — Modular Mobile-First Ruby GG Port (Qt6)
    # ========================================================================
    set(RUBY_GG_MOBILE_SOURCES
        ${SRC_DIR}/ruby/android/main_mobile.cpp
        ${SRC_DIR}/ruby/android/mobile_main_window.cpp
        ${SRC_DIR}/ruby/android/mobile_viewport_widget.cpp
        ${SRC_DIR}/ruby/android/mobile_dpad_widget.cpp
        ${SRC_DIR}/ruby/android/mobile_hierarchy_drawer.cpp
        ${SRC_DIR}/ruby/android/mobile_inspector_drawer.cpp
        ${SRC_DIR}/ruby/android/mobile_code_editor.cpp
        ${SRC_DIR}/ruby/android/mobile_asset_browser.cpp
        ${SRC_DIR}/ruby/android/gles_shaders.cpp
        ${SRC_DIR}/ruby/viewport/ruby_gizmo.cpp
        ${SRC_DIR}/ruby/viewport/ruby_picking.cpp
        ${SRC_DIR}/ruby/viewport/camera_bounds_gizmo.cpp
        ${SRC_DIR}/ruby/core/project_context.cpp
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
        # Zypher (Gen 3). viewport_3d_widget.cpp / ruby_quick_viewport.cpp call
        # zypher::generate_terrain_object(), so the generator must be in the link;
        # it was omitted here, which left every target built from this list
        # (ruby_gg, scene_save_preserves_edit_test, scene_undo_test) with an
        # undefined reference to zypher:: and therefore unbuildable by ctest.
        ${SRC_DIR}/tools/roller.cpp
        ${SRC_DIR}/tools/rubymesh.cpp
        ${SRC_DIR}/tools/scene_asset_resolver.cpp
        ${SRC_DIR}/tools/scene_entity.cpp
        ${SRC_DIR}/tools/scene_physics.cpp
        ${SRC_DIR}/tools/scene_collision.cpp
        ${SRC_DIR}/tools/scene_terrain.cpp
        ${SRC_DIR}/tools/scene_lua.cpp
        ${SRC_DIR}/tools/scene_game.cpp
        ${SRC_DIR}/tools/scene_creator.cpp
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
        ${SRC_DIR}/platform/data_path.cpp
        ${SRC_DIR}/platform/font_fallback.cpp
        ${SRC_DIR}/platform/gdb_compat.cpp
        ${SRC_DIR}/platform/io_thread.cpp
        ${SRC_DIR}/platform/embedded_assets.cpp
        ${SRC_DIR}/platform/os_external.cpp
        ${SRC_DIR}/android/log.c
        ${HOST_LUA_SRCS}
    )

    if (ANDROID)
        add_library(ruby_gg_mobile SHARED ${RUBY_GG_MOBILE_SOURCES})
    else()
        add_executable(ruby_gg_mobile EXCLUDE_FROM_ALL ${RUBY_GG_MOBILE_SOURCES})
    endif()

    set_target_properties(ruby_gg_mobile PROPERTIES
        EXCLUDE_FROM_ALL TRUE
        AUTOMOC ON
        AUTORCC ON
        AUTOUIC ON
    )
    target_compile_definitions(ruby_gg_mobile PRIVATE
        SWORDIGO_NO_IMGUI
        SWORDIGO_MOBILE_PORT
        BATCH_CONVERTER_NO_UI
        "LUAI_FUNC=extern __attribute__((visibility(\"default\")))"
    )
    target_include_directories(ruby_gg_mobile PRIVATE
        ${SRC_DIR}
        ${SRC_DIR}/ruby
        ${SRC_DIR}/ruby/android
        ${SRC_DIR}/tools
        ${SRC_DIR}/tools/ufbx
        ${SRC_DIR}/stb
        ${SRC_DIR}/platform
        ${SRC_DIR}/sre/base/lua/src
    )
    target_link_libraries(ruby_gg_mobile PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGLWidgets
        Qt6::Network
        ZLIB::ZLIB
        Threads::Threads
        m
    )
    if (ANDROID)
        target_link_libraries(ruby_gg_mobile PRIVATE GLESv3 EGL log android)
    else()
        target_link_libraries(ruby_gg_mobile PRIVATE OpenGL::GL)
    endif()
endif()
