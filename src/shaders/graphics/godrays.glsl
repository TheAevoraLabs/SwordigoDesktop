#version 430 core
// ====================================================================
//  Swordigo Screen-Space Volumetric Rays (SSVR)
//  Algorithms adapted from Godot 4 & Unreal Engine
// ====================================================================

uniform sampler2D u_tex;        // scene color
uniform sampler2D u_depth;      // depth texture (GL_DEPTH24_STENCIL8)
uniform vec2 u_sun_pos;         // sun position in UV space (e.g. 0.5, 1.1)
uniform float u_intensity;      // overall intensity
uniform float u_decay;          // per-step transmittance decay
uniform float u_density;        // step density/stride
uniform float u_exposure;       // final exposure multiply
uniform vec2 u_tex_size;        // render target resolution
uniform float u_time;           // animation time
uniform float u_near;           // near plane (50.0)
uniform float u_far;            // far plane (20000.0)

in vec2 v_uv;
out vec4 FragColor;

// Jorge Jimenez Interleaved Gradient Noise (IGN)
float ign(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

float linearize_depth(float d) {
    float n = (u_near > 0.1) ? u_near : 50.0;
    float f = (u_far > 100.0) ? u_far : 20000.0;
    return (2.0 * n) / (f + n - d * (f - n));
}

// Henyey-Greenstein forward phase function (anisotropic atmospheric scattering)
float henyey_greenstein(float cos_theta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 / (4.0 * 3.14159265)) * ((1.0 - g2) / max(pow(denom, 1.5), 0.0001));
}

void main() {
    vec2 sun_uv = u_sun_pos;
    vec2 delta = sun_uv - v_uv;
    float dist = length(delta);
    
    // Surface depth at current pixel
    float raw_pixel_depth = texture(u_depth, v_uv).r;
    float lin_pixel_depth = linearize_depth(raw_pixel_depth);
    
    // Virtual sun depth (sky/background = 0.98 in linearized units)
    float sun_depth_lin = 0.98;
    
    // View and sun directions in view-space
    vec3 V = normalize(vec3(v_uv - 0.5, 1.0));
    vec3 L = normalize(vec3(delta, 0.4));
    float cos_theta = clamp(dot(V, L), -1.0, 1.0);
    float hg_phase = henyey_greenstein(cos_theta, 0.65) * 4.0 * 3.14159265;
    
    // Screen-space raymarching setup (48 steps for smooth gradients)
    const int NUM_STEPS = 48;
    vec2 step_dir = (delta / max(dist, 0.0001)) * min(dist, 0.85) / float(NUM_STEPS) * u_density;
    
    // Interleaved gradient noise dither to eliminate banding
    float dither = ign(gl_FragCoord.xy + fract(u_time * 0.05) * 16.0);
    
    float transmittance = 1.0;
    vec3 ray_accum = vec3(0.0);
    float step_decay = clamp(u_decay, 0.85, 0.995);
    
    // March from current pixel toward the sun
    for (int i = 0; i < NUM_STEPS; i++) {
        float t = (float(i) + dither) / float(NUM_STEPS);
        vec2 march_uv = v_uv + step_dir * (float(i) + dither);
        
        if (march_uv.x < 0.0 || march_uv.x > 1.0 || march_uv.y < 0.0 || march_uv.y > 1.0) {
            break;
        }
        
        float raw_sample_depth = texture(u_depth, march_uv).r;
        float geom_depth = linearize_depth(raw_sample_depth);
        
        // Interpolate ray depth from pixel surface to distant sun
        float ray_depth = mix(lin_pixel_depth, sun_depth_lin, t);
        
        // Depth-tested occlusion: if geometry is in front of the ray, light is blocked
        float is_occluded = step(geom_depth, ray_depth - 0.003);
        float visibility = 1.0 - is_occluded;
        
        // Light emission from background/bright surfaces
        vec3 sample_col = texture(u_tex, march_uv).rgb;
        float sky_mask = smoothstep(0.85, 1.0, raw_sample_depth);
        float luma = dot(sample_col, vec3(0.2126, 0.7152, 0.0722));
        float bright_mask = smoothstep(0.25, 0.85, luma);
        float emitter = mix(bright_mask * 0.35, 1.0, sky_mask);
        
        float dist_to_sun = length(march_uv - sun_uv);
        float radial_att = 1.0 / (1.0 + dist_to_sun * 1.5);
        
        ray_accum += sample_col * emitter * visibility * transmittance * radial_att;
        transmittance *= step_decay;
    }
    
    ray_accum = (ray_accum / float(NUM_STEPS)) * u_exposure * hg_phase * u_intensity;
    
    // Subtle horizon ambient haze
    float top_haze = smoothstep(0.65, 1.0, v_uv.y) * 0.08 * u_intensity;
    ray_accum += vec3(top_haze * 1.0, top_haze * 0.9, top_haze * 0.7);
    
    FragColor = vec4(clamp(ray_accum, 0.0, 1.0), 1.0);
}
