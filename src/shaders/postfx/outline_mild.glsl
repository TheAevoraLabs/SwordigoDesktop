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
    vec2 px = (1.2 * u_intensity) / u_tex_size;
    float d_c = lin_depth(texture(u_depth_tex, v_uv).r);
    float d_l = lin_depth(texture(u_depth_tex, v_uv + vec2(-px.x, 0.0)).r);
    float d_r = lin_depth(texture(u_depth_tex, v_uv + vec2( px.x, 0.0)).r);
    float d_u = lin_depth(texture(u_depth_tex, v_uv + vec2(0.0,  px.y)).r);
    float d_d = lin_depth(texture(u_depth_tex, v_uv + vec2(0.0, -px.y)).r);

    float depth_edge = max(abs(d_l - d_r), abs(d_u - d_d));
    float edge = smoothstep(0.002, 0.006, depth_edge);

    if (d_c > 0.95) edge = 0.0;

    vec3 col = texture(u_tex, v_uv).rgb;
    // Mild darkening outline
    col = mix(col, col * 0.15, edge * 0.65 * u_intensity);

    FragColor = vec4(col, 1.0);
}
