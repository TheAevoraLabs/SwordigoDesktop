#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform float u_time;
uniform float u_intensity; // default 1.0

in vec2 v_uv;
out vec4 FragColor;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_uv;

    // Horizontal tracking tape distortion
    float jitter = sin(uv.y * 30.0 + u_time * 5.0) * 0.002 * u_intensity;
    if (rand(vec2(u_time, floor(uv.y * 20.0))) > 0.96) {
        jitter += (rand(vec2(u_time * 2.0, uv.y)) - 0.5) * 0.02 * u_intensity;
    }
    uv.x += jitter;

    // Chromatic drift
    float shift = 0.004 * u_intensity;
    float r = texture(u_tex, uv + vec2(shift, 0.0)).r;
    float g = texture(u_tex, uv).g;
    float b = texture(u_tex, uv - vec2(shift, 0.0)).b;

    vec3 col = vec3(r, g, b);

    // Scanline & static grain
    float scan = sin(uv.y * u_tex_size.y * 0.8) * 0.1 * u_intensity;
    float noise = (rand(uv + vec2(u_time * 10.0, 0.0)) - 0.5) * 0.12 * u_intensity;
    col += scan + noise;

    FragColor = vec4(col, 1.0);
}
