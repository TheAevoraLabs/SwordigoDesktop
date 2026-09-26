#version 430 core
uniform sampler2D u_tex;
uniform sampler2D u_depth_tex;
uniform vec2 u_tex_size;
uniform float u_time;
uniform float u_vignette, u_grain, u_ca_offset;
uniform float u_saturation, u_contrast, u_brightness, u_warmth, u_sharpen;
// Outline uniforms
uniform float u_outline_enabled;
uniform float u_outline_thickness;
uniform float u_outline_intensity;
uniform float u_outline_depth_threshold;
uniform float u_outline_near;
uniform float u_outline_far;

in vec2 v_uv;
out vec4 FragColor;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

float lin_depth(float d) {
    float n = u_outline_near;
    float f = u_outline_far;
    return (2.0 * n) / (f + n - d * (f - n));
}

float detect_edge() {
    vec2 px = u_outline_thickness / u_tex_size;
    float d_c = lin_depth(texture(u_depth_tex, v_uv).r);
    float d_l = lin_depth(texture(u_depth_tex, v_uv + vec2(-px.x, 0)).r);
    float d_r = lin_depth(texture(u_depth_tex, v_uv + vec2( px.x, 0)).r);
    float d_u = lin_depth(texture(u_depth_tex, v_uv + vec2(0,  px.y)).r);
    float d_d = lin_depth(texture(u_depth_tex, v_uv + vec2(0, -px.y)).r);
    
    float edge_h = abs(d_l - d_r);
    float edge_v = abs(d_u - d_d);
    float depth_edge = max(edge_h, edge_v);
    
    vec3 c_c = texture(u_tex, v_uv).rgb;
    vec3 c_l = texture(u_tex, v_uv + vec2(-px.x, 0)).rgb;
    vec3 c_r = texture(u_tex, v_uv + vec2( px.x, 0)).rgb;
    vec3 c_u = texture(u_tex, v_uv + vec2(0,  px.y)).rgb;
    vec3 c_d = texture(u_tex, v_uv + vec2(0, -px.y)).rgb;
    
    float color_edge_h = length(c_l - c_r);
    float color_edge_v = length(c_u - c_d);
    float color_edge = max(color_edge_h, color_edge_v);
    
    float edge = smoothstep(u_outline_depth_threshold, u_outline_depth_threshold * 3.0, depth_edge);
    edge = max(edge, smoothstep(0.3, 0.6, color_edge) * 0.5);
    
    if (d_c > 0.95) edge = 0.0;
    return edge;
}

void main() {
    vec2 uv = v_uv;
    vec2 px = 1.0 / u_tex_size;
    vec4 col;
    if (u_ca_offset > 0.0) {
        vec2 dir = (uv - 0.5) * u_ca_offset;
        col.r = texture(u_tex, uv - dir).r;
        col.g = texture(u_tex, uv).g;
        col.b = texture(u_tex, uv + dir).b;
        col.a = 1.0;
    } else {
        col = texture(u_tex, uv);
    }
    if (u_sharpen > 0.0) {
        vec4 blur = (texture(u_tex, uv + vec2(-px.x,0)) + texture(u_tex, uv + vec2(px.x,0)) +
                     texture(u_tex, uv + vec2(0,-px.y)) + texture(u_tex, uv + vec2(0,px.y))) * 0.25;
        col.rgb += (col.rgb - blur.rgb) * u_sharpen;
    }
    col.rgb += u_brightness;
    col.rgb = (col.rgb - 0.5) * u_contrast + 0.5;
    float lum = dot(col.rgb, vec3(0.2126, 0.7152, 0.0722));
    col.rgb = mix(vec3(lum), col.rgb, u_saturation);
    if (u_warmth != 0.0) { col.r += u_warmth * 0.05; col.b -= u_warmth * 0.05; }
    if (u_grain > 0.0) { col.rgb += (rand(uv + fract(u_time)) * 2.0 - 1.0) * u_grain; }
    if (u_vignette > 0.0) {
        vec2 vig = uv * (1.0 - uv);
        col.rgb *= pow(clamp(vig.x * vig.y * 15.0, 0.0, 1.0), u_vignette);
    }
    
    if (u_outline_enabled > 0.5) {
        float edge = detect_edge();
        if (edge > 0.01) {
            vec2 opx = u_outline_thickness / u_tex_size;
            vec3 avg_color = (
                texture(u_tex, v_uv + vec2(-opx.x, 0)).rgb +
                texture(u_tex, v_uv + vec2( opx.x, 0)).rgb +
                texture(u_tex, v_uv + vec2(0, -opx.y)).rgb +
                texture(u_tex, v_uv + vec2(0,  opx.y)).rgb
            ) * 0.25;
            
            float avg_luma = dot(avg_color, vec3(0.2126, 0.7152, 0.0722));
            vec3 outline_color = mix(vec3(avg_luma), avg_color, 1.3);
            outline_color *= 0.3;
            col.rgb = mix(col.rgb, outline_color, edge * u_outline_intensity);
        }
    }
    FragColor = vec4(clamp(col.rgb, 0.0, 1.0), 1.0);
}
