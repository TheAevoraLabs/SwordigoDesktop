# ============================================================================
# Components: swfmt (PVR texture decode) and swpod (POD scene loading).
# ============================================================================

swordigo_library(swfmt
    ${SRC_DIR}/platform/pvr_loader.cpp
    # image_decode.cpp: the WebP path, which stb_image and QImage both lack.
    ${SRC_DIR}/tools/image_decode.cpp
    ${SRC_DIR}/platform/pvrtc_decoder.cpp
    ${SRC_DIR}/platform/astc_decoder.cpp)
target_link_libraries(swfmt PRIVATE swcore ZLIB::ZLIB OpenGL::GL)
if (SWORDIGO_HAVE_WEBP)
    target_compile_definitions(swfmt PRIVATE SWORDIGO_HAVE_WEBP=1)
    target_include_directories(swfmt PRIVATE ${SWORDIGO_WEBP_INCLUDE_DIRS})
    target_link_libraries(swfmt PRIVATE ${SWORDIGO_WEBP_LIBRARIES})
endif()

# scene_schemas.cpp lives only in swpod: defining av::g_schemas twice caused
# two destructor registrations for one interposed object -> double free at exit.
swordigo_library(swpod
    ${SRC_DIR}/tools/pod_loader.cpp
    ${SRC_DIR}/tools/render_model.cpp
    ${SRC_DIR}/tools/av_renderer.cpp
    ${SRC_DIR}/tools/scene_loader.cpp
    ${SRC_DIR}/tools/scene_schemas.cpp
    ${SRC_DIR}/tools/scene_collision.cpp
    ${SRC_DIR}/tools/scene_entity.cpp
    ${SRC_DIR}/tools/scene_physics.cpp
    ${SRC_DIR}/tools/scene_game.cpp
    ${SRC_DIR}/tools/scene_terrain.cpp
    ${SRC_DIR}/tools/scene_workspace.cpp
    ${SRC_DIR}/tools/template_sources.cpp
    # The merged .rbsrc document model: structure + behaviour + timeline tiers,
    # lowerable to Lua. Used by ruby_gg's visual scripter and by ruby_cli.
    ${SRC_DIR}/tools/rbsrc.cpp
    # pod_stamp.cpp lives here because pod_loader.cpp is its only consumer: the
    # sidecar is read at pod_load() time to warn about stale bakes.
    ${SRC_DIR}/tools/pod_stamp.cpp)
target_link_libraries(swpod PRIVATE swcore SDL3::SDL3 OpenGL::GL OpenAL::OpenAL)

# ----------------------------------------------------------------------------
# scene_material_report — authoritative per-level material + lighting dump.
#
# Prints, straight from the decoded .scene via av::scene_load, the texture set a
# level references plus every SceneLight / SceneShadow / SceneOverlay in it. It
# is the ground-truth side of two host subsystems that previously ran on guesses:
# TextureAwareMapper's texture-identity trust gate, and the lighting / bloom /
# blob-shadow model.
#
#     ./bin/scene_material_report src/assets/v3_Grasslands.scene
#     ./bin/scene_material_report src/assets --quiet-textures
#
# Lives here rather than in the root CMakeLists' BUILD_TESTING block, which is
# OFF in a normal configure — a diagnostic tool inside a test gate is dead code.
# It is a first-class tool: it links the same libs the app does (swpod + filerift)
# so it tracks scene_loader.cpp's real dependency set instead of a hand-kept
# source list. Read-only; it never writes a .scene.
# ----------------------------------------------------------------------------
add_executable(scene_material_report ${SRC_DIR}/tools/scene_material_report.cpp)
target_include_directories(scene_material_report PRIVATE ${SRC_DIR} ${SRC_DIR}/tools)
target_link_libraries(scene_material_report PRIVATE filerift swpod)
set_target_properties(scene_material_report PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
