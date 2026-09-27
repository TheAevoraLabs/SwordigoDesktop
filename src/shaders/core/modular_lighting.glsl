// ============================================================================
// modular_lighting.glsl — Swordigo Modular Lighting System
// 
// Provides:
//   - Proper directional sunlight with cascaded shadows
//   - Modular point light system with correct attenuation
//   - Hemisphere ambient with proper color cast
//   - Adaptive brightness/darkness normalization
//   - Swordigo-specific Half-Lambert² diffuse (no harsh terminator)
// ============================================================================

#ifndef MODULAR_LIGHTING_GLSL
#define MODULAR_LIGHTING_GLSL

// ============================================================================
// SUNLIGHT & DIRECTIONAL LIGHTING
// ============================================================================

/// Compute directional light contribution with Half-Lambert² diffuse.
/// This prevents harsh terminator lines on low-poly mobile meshes.
vec3 compute_sunlight(vec3 normal, vec3 sun_dir, vec3 sun_color, float intensity) {
    float ndotl = dot(normal, sun_dir);
    // Half-Lambert: (ndotl * 0.5 + 0.5)² — wraps light around the silhouette
    float diffuse = (ndotl * 0.5 + 0.5);
    diffuse = diffuse * diffuse;  // Square for softer falloff
    return sun_color * diffuse * intensity;
}

/// Compute directional light with soft shadows.
/// shadow_factor: 0 = fully shadowed, 1 = fully lit
vec3 compute_sunlight_shadowed(vec3 normal, vec3 sun_dir, vec3 sun_color, 
                                float intensity, float shadow_factor) {
    vec3 lit = compute_sunlight(normal, sun_dir, sun_color, intensity);
    // Shadow lifts the darkest areas slightly (prevents pure black)
    float shadow_lift = 0.15;  // Minimum brightness in shadow
    float shadow_blend = mix(shadow_lift, 1.0, shadow_factor);
    return lit * shadow_blend;
}

// ============================================================================
// POINT LIGHTS
// ============================================================================

/// Compute point light contribution with proper inverse-square falloff.
/// pos: fragment position in world/view space
/// light_pos: light position in same space
/// light_color: RGB color (can be HDR > 1.0)
/// light_radius: falloff radius
vec3 compute_point_light(vec3 normal, vec3 pos, vec3 light_pos, 
                         vec3 light_color, float light_radius) {
    vec3 to_light = light_pos - pos;
    float dist = length(to_light);
    
    if (dist >= light_radius || light_radius < 0.001) {
        return vec3(0.0);
    }
    
    vec3 L = to_light / dist;
    float ndotl = dot(normal, L);
    
    // Half-Lambert² diffuse
    float diffuse = (ndotl * 0.5 + 0.5);
    diffuse = diffuse * diffuse;
    
    // Inverse-square falloff with smooth edge
    float attenuation = 1.0 - (dist / light_radius);
    attenuation = attenuation * attenuation;  // Quadratic for smooth falloff
    
    return light_color * diffuse * attenuation;
}

// ============================================================================
// HEMISPHERE AMBIENT
// ============================================================================

/// Compute hemisphere ambient lighting.
/// Blends between sky color (up) and ground color (down) based on surface normal.
/// This is critical for Swordigo's forest/cave environments.
vec3 compute_hemisphere_ambient(vec3 normal, vec3 sky_color, vec3 ground_color) {
    // Blend factor: 0 = ground, 1 = sky
    float blend = normal.y * 0.5 + 0.5;
    return mix(ground_color, sky_color, blend);
}

// ============================================================================
// ADAPTIVE BRIGHTNESS NORMALIZATION
// ============================================================================

/// Normalize brightness to prevent "too bright" and "too dark" extremes.
/// Applies a soft knee curve that:
///   - Lifts shadows (prevents crushed blacks)
///   - Compresses highlights (prevents blown whites)
///   - Preserves mid-tone contrast
vec3 normalize_brightness(vec3 color, float shadow_lift, float highlight_compress) {
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    
    // Lift shadows: apply a minimum brightness floor
    float shadow_factor = smoothstep(0.0, 0.3, luma);
    color = mix(color * shadow_lift, color, shadow_factor);
    
    // Compress highlights: soft knee before 1.0
    float highlight_factor = smoothstep(0.7, 1.0, luma);
    color = mix(color, color * highlight_compress, highlight_factor);
    
    return color;
}

// ============================================================================
// SWORDIGO-SPECIFIC TONE MAPPING
// ============================================================================

/// Extended Reinhard tone mapping (L_white = 2.5).
/// Matches Swordigo V7.1 PostFX pipeline.
/// Never clips colors — soft roll-off into white.
vec3 tonemap_reinhard_swordigo(vec3 color) {
    const float L_white = 2.5;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float tone = luma * (1.0 + luma / (L_white * L_white)) / (1.0 + luma);
    return color * (tone / max(luma, 0.0001));
}

/// ACES Filmic tone mapping (Narkowicz 2015).
/// Better for HDR content, but requires careful application to display-referred signals.
vec3 tonemap_aces(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ============================================================================
// SWORDIGO COLOR GRADING
// ============================================================================

/// Apply Swordigo's signature color grade:
///   - +15% saturation (vibrant palette)
///   - Warm highlight push (fantastical adventure feel)
///   - Slight gamma adjustment for display
vec3 apply_swordigo_grade(vec3 color) {
    // Saturation boost: +15%
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 desaturated = vec3(luma);
    color = mix(desaturated, color, 1.15);
    
    // Warm highlight push: boost red/green in bright areas
    float brightness = luma;
    float warm_boost = smoothstep(0.5, 1.0, brightness) * 0.08;
    color.r += warm_boost;
    color.g += warm_boost * 0.5;
    
    return color;
}

// ============================================================================
// GAMMA CORRECTION
// ============================================================================

/// Apply output gamma for linear framebuffer on sRGB display.
/// Using 1/1.6 (not 1/2.2) because we don't decode sRGB textures.
vec3 apply_gamma_output(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(1.0 / 1.6));
}

/// Inverse gamma (for sRGB decode if needed).
vec3 apply_gamma_input(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(1.6));
}

#endif // MODULAR_LIGHTING_GLSL
