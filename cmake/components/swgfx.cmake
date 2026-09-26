# ============================================================================
# Component: swgfx — rendering (FBO scaler, Vulkan backend, video bg, SRT overlay).
# ============================================================================

swordigo_library(swgfx
    ${SRC_DIR}/platform/fbo_scaler.cpp
    # Real forward-geometry pass: per-fragment lighting that replaces the guest's
    ${SRC_DIR}/platform/ffp_forward.cpp
    ${SRC_DIR}/platform/vulkan_backend.cpp
    ${SRC_DIR}/platform/video_background.cpp
    ${SRC_DIR}/platform/ffmpeg_dyn.cpp
    ${SRC_DIR}/platform/srt_overlay.cpp
    ${SRC_DIR}/graphics/TextureAwareMapper.cpp
    ${SRC_DIR}/graphics/LightManager.cpp
    ${SRC_DIR}/tools/scene_v3_db.cpp)
target_link_libraries(swgfx PRIVATE swcore swgui SDL3::SDL3 OpenGL::GL ${CMAKE_DL_LIBS} ${SWORDIGO_LIBM})
if (SWORDIGO_USE_FFMPEG)
    target_compile_definitions(swgfx PRIVATE "SWORDIGO_USE_FFMPEG=1")
    find_path(FFMPEG_INCLUDE_DIR NAMES libavformat/avformat.h PATH_SUFFIXES ffmpeg)
    if (FFMPEG_INCLUDE_DIR)
        target_include_directories(swgfx PRIVATE ${FFMPEG_INCLUDE_DIR})
    endif()
else()
    target_compile_definitions(swgfx PRIVATE "SWORDIGO_USE_FFMPEG=0")
endif()
