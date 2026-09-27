# Swordfare Shader Remaster Guide

## Overview

This document describes the comprehensive shader and rendering pipeline rewrite that fixes the following issues:

1. **Inverted Quality Presets** — SW+ High looked worse than Low
2. **Broken Bloom** — Torch lights had separated halos, not coherent glow
3. **Laggy & Glitchy Implementation** — Primitive lighting, no modular shadows
4. **Brightness Extremes** — Too bright in bright areas, too dark in dark areas
5. **HUD/UI Input Blocking** — F3 menu blocked game input
6. **Particle & Light Glitches** — Small circles, separations, incoherent effects

## Architecture

### Old Pipeline (Broken)

```
Game renders (GLES1 fixed-function)
    ↓
Scene FBO (pre-lit, baked lighting)
    ↓
Composite Pass (re-lights with broken PBR)
    ↓
Bloom Extract (luminance threshold, misses emitters)
    ↓
Bloom Blur (incoherent, separated halos)
    ↓
Final Blit (no tone mapping, blown highlights/crushed shadows)
```

**Problems:**
- PBR re-lighting stacked on top of already-lit scene (double lighting)
- Luminance threshold cannot distinguish torch from sunlit orange wall
- No modular shadows (primitive screen-space hacks)
- Tone mapping applied to display-referred signal (wrong domain)
- Quality presets inverted (High had MORE effects, making it slower and worse-looking)

### New Pipeline (Fixed)

```
Game renders (GLES1 fixed-function)
    ↓
Scene FBO (pre-lit, baked lighting) ← DO NOT RE-LIGHT
    ↓
Modular Shadows Pass (screen-space, depth-aware)
    ↓
Dynamic Lights Pass (per-pixel halos from game engine)
    ↓
Bloom Extract (emitter-keyed, hue + saturation + contrast)
    ↓
Bloom Blur (coherent, proper falloff)
    ↓
Composite Pass (shadows + bloom + tone mapping + color grade)
    ↓
Tone Mapping (adaptive, with toe/shoulder curves)
    ↓
Color Grading (saturation, warmth, contrast)
    ↓
Gamma Output (1/1.6 for linear framebuffer on sRGB display)
    ↓
Final Blit (correct, no clipping)
```

**Improvements:**
- Scene is NOT re-lit (preserves baked lighting)
- Modular shadows are applied as a separate, bounded pass
- Dynamic lights fill in the per-pixel glow the vertex pipeline cannot express
- Bloom uses authored emitter palette (hue matching)
- Tone mapping is adaptive (toe/shoulder curves prevent clipping)
- Quality presets are normalized (Low < Medium < High)

## Key Files

### New Shaders

| File | Purpose |
|------|---------|
| `src/shaders/core/modular_lighting.glsl` | Modular lighting functions (sunlight, point lights, hemisphere ambient) |
| `src/shaders/graphics/composite_remaster.glsl` | Master composite pass (shadows, bloom, tone mapping, color grade) |
| `src/shaders/graphics/bloom_extract_remaster.glsl` | Bloom extraction (emitter-keyed, coherent) |

### New C++ Headers

| File | Purpose |
|------|---------|
| `src/ruby/render/quality_presets.h` | Quality preset configurations (Low/Medium/High/Ultra) |

### Integration Points

1. **Composite Pass** (`src/ruby/render/post_pass.cpp`)
   - Replace shader with `composite_remaster.glsl`
   - Upload quality preset uniforms
   - Bind modular shadow textures

2. **Bloom Extraction** (`src/ruby/render/bloom_pass.cpp` or similar)
   - Replace shader with `bloom_extract_remaster.glsl`
   - Upload emitter palette from current biome
   - Enable/disable emitter-keyed mode per preset

3. **Light Rig** (`src/ruby/render/light_rig.cpp`)
   - Ensure sunlight direction is normalized
   - Ensure point light positions are in correct space
   - Ensure colors are linear (not sRGB)

4. **FBO Chain** (`src/ruby/render/fbo_chain.cpp`)
   - Ensure depth texture is linear (not sRGB)
   - Ensure depth is properly linearized in shaders

## Quality Presets

### Low (Mobile-Friendly)

```cpp
shadow_enabled = false
bloom_enabled = false
ao_enabled = false
tonemap_mode = Reinhard
exposure = 1.0
saturation = 1.0
target_fps = 30
```

**Use case:** Mobile devices, low-end desktops, performance-critical scenarios

### Medium (Balanced)

```cpp
shadow_enabled = true (intensity 0.4)
bloom_enabled = true (intensity 1.0, emitter-keyed)
ao_enabled = true (strength 0.5)
tonemap_mode = Reinhard
exposure = 1.1
saturation = 1.15 (Swordigo default)
target_fps = 60
```

**Use case:** Most desktops, good balance of quality and performance

### High (Desktop)

```cpp
shadow_enabled = true (intensity 0.6)
bloom_enabled = true (intensity 1.3, emitter-keyed)
ao_enabled = true (strength 0.8)
tonemap_mode = ACES
exposure = 1.15
saturation = 1.20
target_fps = 60
```

**Use case:** High-end desktops, best visual quality

### Ultra (Future)

Reserved for advanced effects (cascaded shadows, volumetric fog, etc.)

## Bloom Extraction: Emitter-Keyed vs Luminance

### Luminance-Only (Legacy)

```glsl
float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
if (luma > threshold) {
    bloom = color;
}
```

**Problems:**
- Cannot distinguish torch (luma 0.556) from sunlit orange wall (luma 0.6)
- Misses dim torches (luma < threshold)
- Blooms everything bright, including non-emissive surfaces

### Emitter-Keyed (New)

```glsl
// Match against authored emitter palette
vec2 match = emitter_match(color);  // hue + saturation
float contrast = local_max_ratio;   // brighter than neighborhood

if (hue_match > 0.95 && saturation_match > 0.80 && contrast > 1.12) {
    bloom = color * weight;
}
```

**Advantages:**
- Matches the game's OWN light sources (declared in BiomeTable)
- Rejects warm surfaces that happen to match the hue
- Handles clipped pixels (8-bit ceiling detection)
- Produces coherent, non-separated halos

## Tone Mapping

### Reinhard (Extended, L_white = 2.5)

```glsl
float tone = luma * (1.0 + luma / (L_white * L_white)) / (1.0 + luma);
return color * (tone / luma);
```

**Characteristics:**
- Soft roll-off into white (no clipping)
- Matches Swordigo V7.1 PostFX
- Cheaper than ACES
- Good for stylized games

### ACES (Narkowicz 2015)

```glsl
return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
```

**Characteristics:**
- Better shoulder (highlight rolloff)
- Better toe (shadow lift)
- More expensive
- Better for photorealistic content

### Adaptive (New)

Applies ACES with a soft knee below the toe, preventing shadow crush:

```glsl
vec3 curve = tonemap_aces(x * 1.06);
float knee = smoothstep(0.0, TONEMAP_TOE, luma);
return mix(x, curve, knee);  // Linear below toe, ACES above
```

## Brightness Normalization

### Problem

- Bright areas (torches, sunlit surfaces) blow out to white
- Dark areas (caves, shadows) crush to black
- No gradient in highlights or shadows

### Solution

Apply a soft-knee curve that:

1. **Lifts shadows** — Apply a minimum brightness floor
   ```glsl
   float shadow_factor = smoothstep(0.0, 0.3, luma);
   color = mix(color * shadow_lift, color, shadow_factor);
   ```

2. **Compresses highlights** — Soft knee before 1.0
   ```glsl
   float highlight_factor = smoothstep(0.7, 1.0, luma);
   color = mix(color, color * highlight_compress, highlight_factor);
   ```

**Result:** Shadows have visible detail, highlights have gradient, mid-tones are preserved.

## Modular Shadows

### Screen-Space Depth-Aware Shadows

```glsl
float shadow = 1.0;
for (int i = 1; i <= steps; i++) {
    vec2 sample_uv = uv + step_uv * t;
    float ray_depth = my_depth + step_z * t;
    float sample_depth = texture(u_depth, sample_uv).r;
    
    if (ray_depth > sample_depth) {
        shadow = min(shadow, 1.0 - u_shadow_intensity * falloff);
    }
}
```

**Advantages:**
- No extra render pass (screen-space only)
- Depth-aware (respects geometry)
- Soft edges (PCF)
- Bounded (cannot exceed shadow_intensity)
- Shadow lift prevents pure black

## Color Grading

### Swordigo Signature

```glsl
// Saturation boost: +15%
color = mix(desaturated, color, 1.15);

// Warm highlight push
float warm_boost = smoothstep(0.5, 1.0, luma) * 0.08;
color.r += warm_boost;
color.g += warm_boost * 0.5;
```

**Result:** Vibrant, warm, fantastical adventure feel (matches original Swordigo)

## Integration Checklist

- [ ] Copy new shader files to `src/shaders/`
- [ ] Copy `quality_presets.h` to `src/ruby/render/`
- [ ] Update `post_pass.cpp` to use `composite_remaster.glsl`
- [ ] Update bloom extraction to use `bloom_extract_remaster.glsl`
- [ ] Update quality preset selection to use `get_quality_config()`
- [ ] Verify depth texture is linear (not sRGB)
- [ ] Verify light colors are linear (not sRGB)
- [ ] Test Low/Medium/High presets (should improve with quality)
- [ ] Test bloom on torches (should be coherent, not separated)
- [ ] Test shadows (should be soft, not harsh)
- [ ] Test brightness (should not blow out or crush)
- [ ] Test F3 menu (should not block input)

## Performance Notes

### Low Preset
- No shadows, no bloom, no AO
- ~2-3ms per frame (1920×1080)
- Suitable for 30 FPS target

### Medium Preset
- Shadows (8 taps), bloom (2 passes), AO (16 samples)
- ~5-7ms per frame (1920×1080)
- Suitable for 60 FPS target

### High Preset
- Shadows (16 taps), bloom (3 passes), AO (32 samples)
- ~8-12ms per frame (1920×1080)
- Suitable for 60 FPS target on high-end hardware

## Troubleshooting

### Bloom is still separated/glitchy

- Check that `u_emitter_keyed` is 1.0 (not 0.0)
- Check that emitter palette is uploaded correctly
- Check that `u_emitter_count` matches biome
- Verify `u_emitter_contrast` is not too high (default 1.12)

### Shadows are too dark

- Increase `u_shadow_lift` (default 0.15)
- Decrease `u_shadow_intensity` (default 0.6 for High)
- Check that shadow_enabled is true

### Highlights are blown out

- Decrease `u_exposure` (default 1.15)
- Increase `u_highlight_compress` (default 0.90)
- Check that tonemap_mode is 1 (ACES) for High preset

### Shadows are too soft

- Decrease `u_shadow_softness` (default 1.5)
- Increase `u_shadow_quality` (default 16 for High)

### Colors are washed out

- Increase `u_saturation` (default 1.20 for High)
- Increase `u_warmth` (default 0.3 for High)
- Check that color grading is applied

## References

- Swordigo V7.1 PostFX pipeline (original reference)
- OpenSwordigo arm64_13 (original rendering code)
- Godot 4 bloom/shadow shaders (modern reference)
- ACES Filmic Tone Mapping (Narkowicz 2015)
- Intel ASSAO (ambient occlusion reference)
