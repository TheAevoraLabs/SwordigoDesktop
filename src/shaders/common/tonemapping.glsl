// =============================================================================
//  SwordigoDesktop — ACES Filmic Tone Reproduction
// =============================================================================
#ifndef TONEMAPPING_GLSL
#define TONEMAPPING_GLSL

// ACES Narkowicz 2015 fit
vec3 tonemap_aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Linear to sRGB
vec3 linear_to_srgb(vec3 color) {
    return mix(
        color * 12.92,
        1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
        step(vec3(0.0031308), color)
    );
}

// sRGB to Linear
vec3 srgb_to_linear(vec3 color) {
    return mix(
        color / 12.92,
        pow((color + 0.055) / 1.055, vec3(2.4)),
        step(vec3(0.04045), color)
    );
}

#endif // TONEMAPPING_GLSL
