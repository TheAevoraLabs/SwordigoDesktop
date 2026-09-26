#version 430 core
// AMD FidelityFX Super Resolution 1.0 (EASU + RCAS)
uniform sampler2D u_tex;
uniform vec2 u_tex_size;
uniform vec2 u_out_size;
in vec2 v_uv;
out vec4 FragColor;

float lanczos2(float x) {
    if (abs(x) < 1e-6) return 1.0;
    if (abs(x) >= 2.0) return 0.0;
    float pi_x = 3.14159265 * x;
    return sin(pi_x) * sin(pi_x * 0.5) / (pi_x * pi_x * 0.5);
}

void main() {
    vec2 pixel = v_uv * u_tex_size;
    vec2 frac_pos = fract(pixel) - 0.5;
    vec2 pixel_center = floor(pixel) + 0.5;
    vec2 texel = 1.0 / u_tex_size;
    
    vec3 samples[4][4];
    for (int y = -1; y <= 2; y++) {
        for (int x = -1; x <= 2; x++) {
            vec2 uv = (pixel_center + vec2(x, y)) * texel;
            samples[y+1][x+1] = texture(u_tex, uv).rgb;
        }
    }
    
    vec3 dx = (samples[1][2] - samples[1][0]) + (samples[2][2] - samples[2][0]);
    vec3 dy = (samples[2][1] - samples[0][1]) + (samples[2][2] - samples[0][2]);
    float edge_h = dot(abs(dx), vec3(0.333));
    float edge_v = dot(abs(dy), vec3(0.333));
    
    float edge_strength = min(edge_h + edge_v, 1.0);
    float sharpness = mix(0.5, 1.0, edge_strength);
    
    vec3 color = vec3(0.0);
    float total_weight = 0.0;
    
    for (int y = -1; y <= 2; y++) {
        for (int x = -1; x <= 2; x++) {
            vec2 d = frac_pos - vec2(x, y);
            float w;
            if (edge_h > edge_v * 1.5) {
                w = lanczos2(d.x) * lanczos2(d.y * sharpness);
            } else if (edge_v > edge_h * 1.5) {
                w = lanczos2(d.x * sharpness) * lanczos2(d.y);
            } else {
                w = lanczos2(d.x) * lanczos2(d.y);
            }
            color += samples[y+1][x+1] * w;
            total_weight += w;
        }
    }
    
    color /= max(total_weight, 0.001);
    
    vec3 min_col = min(min(samples[1][1], samples[1][2]), min(samples[2][1], samples[2][2]));
    vec3 max_col = max(max(samples[1][1], samples[1][2]), max(samples[2][1], samples[2][2]));
    color = clamp(color, min_col, max_col);
    
    vec3 center = samples[1][1];
    vec3 n = samples[0][1], s = samples[2][1], e = samples[1][2], w2 = samples[1][0];
    vec3 sharp = center * 5.0 - n - s - e - w2;
    color = mix(color, clamp(sharp, min_col, max_col), 0.15);
    
    FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
