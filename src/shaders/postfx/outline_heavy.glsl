#version 430 core
uniform sampler2D u_tex;
uniform sampler2D u_depth_tex;
uniform vec2 u_tex_size;
uniform float u_intensity; // default 1.0

in vec2 v_uv;
out vec4 FragColor;

float lin_depth(float d) {
    float n = 50.0;
    float f = 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

void main() {
    vec2 px = (2.2 * u_intensity) / u_tex_size;
    float d_c = lin_depth(texture(u_depth_tex, v_uv).r);
    float d_l = lin_depth(texture(u_depth_tex, v_uv + vec2(-px.x, 0.0)).r);
    float d_r = lin_depth(texture(u_depth_tex, v_uv + vec2( px.x, 0.0)).r);
    float d_u = lin_depth(texture(u_depth_tex, v_uv + vec2(0.0,  px.y)).r);
    float d_d = lin_depth(texture(u_depth_tex, v_uv + vec2(0.0, -px.y)).r);

    vec3 c_c = texture(u_tex, v_uv).rgb;
    vec3 c_l = texture(u_tex, v_uv + vec2(-px.x, 0.0)).rgb;
    vec3 c_r = texture(u_tex, v_uv + vec2( px.x, 0.0)).rgb;
    vec3 c_u = texture(u_tex, v_uv + vec2(0.0,  px.y)).rgb;
    vec3 c_d = texture(u_tex, v_uv + vec2(0.0, -px.y)).rgb;

    float depth_edge = max(abs(d_l - d_r), abs(d_u - d_d));
    float color_edge = max(length(c_l - c_r), length(c_u - c_d));

    float edge = smoothstep(0.0015, 0.0045, depth_edge);
    edge = max(edge, smoothstep(0.2, 0.5, color_edge));

    if (d_c > 0.95) edge = 0.0;

    // Bold anime ink line
    vec3 col = mix(c_c, vec3(0.02, 0.02, 0.04), edge * 0.92 * u_intensity);

    FragColor = vec4(col, 1.0);
}
