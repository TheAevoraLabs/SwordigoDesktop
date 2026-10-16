#pragma once
// ============================================================================
// gles_shaders.h — OpenGL ES 3.0 Mobile Shader Pipeline for Ruby GG
//   Supplies GLES 3.0 shaders (#version 300 es) with precision qualifiers
//   and Half-Lambert / Reinhard tone-mapping tailored for mobile GPUs.
// ============================================================================

#include <string>

namespace ruby::android {

class GLESShaders {
public:
    // Returns vertex shader source compatible with OpenGL ES 3.0
    static const char* vertex_shader_source();

    // Returns fragment shader source compatible with OpenGL ES 3.0
    static const char* fragment_shader_source();

    // Line / Gizmo unlit shader for touch manipulation handles
    static const char* unlit_vertex_shader_source();
    static const char* unlit_fragment_shader_source();

    // Inverted-hull silhouette outline shader
    static const char* outline_vertex_shader_source();
    static const char* outline_fragment_shader_source();

    // Animated fluid water sheet shaders
    static const char* water_vertex_shader_source();
    static const char* water_fragment_shader_source();
};

} // namespace ruby::android
