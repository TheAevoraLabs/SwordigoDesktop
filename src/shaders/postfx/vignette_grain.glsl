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
    vec3 col = texture(u_tex, v_uv).rgb;

    // Organic film grain
    float grain = (rand(v_uv + fract(u_time * 1.5)) * 2.0 - 1.0) * 0.05 * u_intensity;
    col += grain;

    // Optical vignette
    vec2 vig = v_uv * (1.0 - v_uv);
    float vignette = clamp(vig.x * vig.y * 15.0, 0.0, 1.0);
    col *= pow(vignette, 0.28 * u_intensity);

    FragColor = vec4(col, 1.0);
}
