#version 430 core
// ====================================================================
//  Swordigo Cross-Bilateral Depth-Aware Blur Filter
//  Adapted from Godot 4 (Edge-Preserving SSAO & 9-Tap Bloom Blur)
// ====================================================================

uniform sampler2D u_tex;
uniform sampler2D u_depth;
uniform vec2  u_direction;     // step direction in UV space
uniform float u_use_depth;     // 1.0 = depth-aware cross bilateral, 0.0 = gaussian/tent
uniform float u_near_plane;
uniform float u_far_plane;

in vec2 v_uv;
out vec4 FragColor;

float linearize_depth(float d) {
    float n = (u_near_plane > 0.1) ? u_near_plane : 50.0;
    float f = (u_far_plane > 100.0) ? u_far_plane : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

const float WEIGHTS[5] = float[](0.2270, 0.1945, 0.1216, 0.0540, 0.0162);

void main() {
    if (u_use_depth > 0.5) {
        float center_d = linearize_depth(texture(u_depth, v_uv).r);
        vec4 accum_col = texture(u_tex, v_uv) * WEIGHTS[0];
        float total_weight = WEIGHTS[0];
        
        for (int i = 1; i <= 4; i++) {
            float spatial_w = WEIGHTS[i];
            vec2 off = u_direction * float(i);
            
            // Positive tap
            vec2 uv_pos = v_uv + off;
            float d_pos = linearize_depth(texture(u_depth, uv_pos).r);
            float range_w_pos = exp(-abs(d_pos - center_d) * 120.0);
            float w_pos = spatial_w * range_w_pos;
            accum_col += texture(u_tex, uv_pos) * w_pos;
            total_weight += w_pos;
            
            // Negative tap
            vec2 uv_neg = v_uv - off;
            float d_neg = linearize_depth(texture(u_depth, uv_neg).r);
            float range_w_neg = exp(-abs(d_neg - center_d) * 120.0);
            float w_neg = spatial_w * range_w_neg;
            accum_col += texture(u_tex, uv_neg) * w_neg;
            total_weight += w_neg;
        }
        FragColor = accum_col / max(total_weight, 0.0001);
    } else {
        vec4 col = texture(u_tex, v_uv) * WEIGHTS[0];
        for (int i = 1; i <= 4; i++) {
            vec2 off = u_direction * float(i);
            col += texture(u_tex, v_uv + off) * WEIGHTS[i];
            col += texture(u_tex, v_uv - off) * WEIGHTS[i];
        }
        FragColor = col;
    }
}
