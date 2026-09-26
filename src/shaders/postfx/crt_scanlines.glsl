#version 430 core
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform float u_time;
uniform float u_intensity; // default 1.0

in vec2 v_uv;
out vec4 FragColor;

vec2 curve_screen(vec2 uv) {
    uv = (uv - 0.5) * 2.0;
    uv *= 1.1;
    uv.x *= 1.0 + pow((abs(uv.y) / 5.0), 2.0);
    uv.y *= 1.0 + pow((abs(uv.x) / 4.0), 2.0);
    uv = (uv / 2.0) + 0.5;
    return uv;
}

void main() {
    vec2 curved_uv = mix(v_uv, curve_screen(v_uv), 0.7 * u_intensity);
    if (curved_uv.x < 0.0 || curved_uv.x > 1.0 || curved_uv.y < 0.0 || curved_uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 col;
    // Chromatic aberration at edges
    float dist = length(curved_uv - 0.5);
    float ca = 0.003 * dist * u_intensity;
    col.r = texture(u_tex, curved_uv - vec2(ca, 0.0)).r;
    col.g = texture(u_tex, curved_uv).g;
    col.b = texture(u_tex, curved_uv + vec2(ca, 0.0)).b;

    // Scanlines
    float scanline = sin(curved_uv.y * u_tex_size.y * 1.5) * 0.5 + 0.5;
    col *= 1.0 - (0.25 * u_intensity * scanline);

    // Vignette
    float vig = curved_uv.x * curved_uv.y * (1.0 - curved_uv.x) * (1.0 - curved_uv.y);
    col *= clamp(pow(16.0 * vig, 0.25 * u_intensity), 0.0, 1.0);

    FragColor = vec4(col, 1.0);
}
