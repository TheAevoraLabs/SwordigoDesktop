#version 430 core
// ====================================================================
//  Swordigo Screen-Space Ambient Occlusion (ASSAO)
//  Algorithms adapted from Godot 4 & Intel ASSAO
// ====================================================================

uniform sampler2D u_depth;    // depth texture
uniform vec2 u_tex_size;      // resolution
uniform float u_radius;       // sample radius
uniform float u_intensity;    // AO strength
uniform float u_time;         // for noise
uniform float u_near_plane;   // camera near plane (50.0)
uniform float u_far_plane;    // camera far plane  (20000.0)

in vec2 v_uv;
out vec4 FragColor;

float ign(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

float linearize_depth(float d) {
    float n = (u_near_plane > 0.1) ? u_near_plane : 50.0;
    float f = (u_far_plane > 100.0) ? u_far_plane : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

vec3 get_view_pos(vec2 uv) {
    float d = linearize_depth(texture(u_depth, uv).r);
    float aspect = u_tex_size.x / u_tex_size.y;
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * d * 0.17632698 * aspect, ndc.y * d * 0.17632698, d);
}

// Intel ASSAO 16-sample spiral disk pattern
const vec2 SAMPLE_DISK[16] = vec2[](
    vec2( 0.78488,  0.56661), vec2( 0.26022, -0.29575),
    vec2( 0.10459,  0.08372), vec2(-0.68286,  0.04963),
    vec2(-0.13570, -0.64190), vec2(-0.26193, -0.08205),
    vec2(-0.61177,  0.66664), vec2( 0.43675,  0.25119),
    vec2( 0.07884,  0.86618), vec2(-0.12790, -0.29869),
    vec2(-0.04031,  0.02413), vec2( 0.16201, -0.52851),
    vec2(-0.70991,  0.47301), vec2( 0.03277, -0.22349),
    vec2( 0.68921,  0.36800), vec2( 0.29251,  0.37775)
);

void main() {
    float depth = texture(u_depth, v_uv).r;
    if (depth > 0.999) {
        FragColor = vec4(1.0);
        return;
    }
    
    vec2 px = 1.0 / u_tex_size;
    vec3 pos_c = get_view_pos(v_uv);
    
    // Cross-depth reconstruction for bilateral normal estimation (Intel ASSAO)
    vec3 pos_r = get_view_pos(v_uv + vec2(px.x, 0.0));
    vec3 pos_l = get_view_pos(v_uv - vec2(px.x, 0.0));
    vec3 pos_u = get_view_pos(v_uv + vec2(0.0, px.y));
    vec3 pos_d = get_view_pos(v_uv - vec2(0.0, px.y));
    
    vec3 dx = (abs(pos_r.z - pos_c.z) < abs(pos_l.z - pos_c.z)) ? (pos_r - pos_c) : (pos_c - pos_l);
    vec3 dy = (abs(pos_u.z - pos_c.z) < abs(pos_d.z - pos_c.z)) ? (pos_u - pos_c) : (pos_c - pos_d);
    vec3 normal = normalize(cross(dx, dy));
    if (normal.z > 0.0) normal = -normal;
    
    float noise = ign(gl_FragCoord.xy) * 6.2831853;
    mat2 rot = mat2(cos(noise), -sin(noise), sin(noise), cos(noise));
    
    float world_radius = max(u_radius, 0.001);
    float falloff_sq = -1.0 / (world_radius * world_radius);
    float horizon_threshold = 0.05;
    
    float obscurance_sum = 0.0;
    float weight_sum = 0.0;
    
    for (int i = 0; i < 16; i++) {
        vec2 sample_offset = (rot * SAMPLE_DISK[i]) * world_radius;
        sample_offset.x *= u_tex_size.y / u_tex_size.x;
        
        vec2 sample_uv = v_uv + sample_offset;
        if (sample_uv.x < 0.0 || sample_uv.x > 1.0 || sample_uv.y < 0.0 || sample_uv.y > 1.0)
            continue;
        
        vec3 hit_pos = get_view_pos(sample_uv);
        vec3 hit_delta = hit_pos - pos_c;
        float len_sq = dot(hit_delta, hit_delta);
        float dist = sqrt(max(len_sq, 0.00001));
        
        float NdotD = dot(-normal, hit_delta) / dist;
        float falloff_mult = max(0.0, len_sq * falloff_sq + 1.0);
        float obscurance = max(0.0, NdotD - horizon_threshold) * falloff_mult;
        
        float reduce = max(0.0, hit_delta.z);
        float halo_weight = clamp(reduce * (-1.0 / world_radius) + 2.0, 0.0, 1.0);
        float weight = 0.6 * halo_weight + 0.4;
        
        obscurance_sum += obscurance * weight;
        weight_sum += weight;
    }
    
    float ao = 1.0;
    if (weight_sum > 0.001) {
        float avg_obscurance = obscurance_sum / weight_sum;
        ao = clamp(1.0 - avg_obscurance * u_intensity * 2.2, 0.0, 1.0);
    }
    ao = pow(ao, 0.9);
    FragColor = vec4(ao, ao, ao, 1.0);
}
