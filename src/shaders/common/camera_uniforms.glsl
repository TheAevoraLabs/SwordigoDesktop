// =============================================================================
//  SwordigoDesktop — Camera & Projection Uniforms
// =============================================================================
#ifndef CAMERA_UNIFORMS_GLSL
#define CAMERA_UNIFORMS_GLSL

uniform mat4  u_modelview;
uniform mat4  u_projection;
uniform mat4  u_inv_projection;
uniform mat4  u_inv_view;
uniform vec3  u_cam_pos;
uniform float u_near_plane;
uniform float u_far_plane;
uniform vec2  u_viewport_size;

// Reconstruct view-space position from depth
vec3 reconstruct_view_pos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = u_inv_projection * clip;
    return view.xyz / view.w;
}

// Linearize depth
float linearize_depth(float d) {
    return (2.0 * u_near_plane) / (u_far_plane + u_near_plane - d * (u_far_plane - u_near_plane));
}

#endif // CAMERA_UNIFORMS_GLSL
