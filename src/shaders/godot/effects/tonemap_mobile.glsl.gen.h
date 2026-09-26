/**************************************************************************/
/*  tonemap_mobile.glsl.gen.h                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

/* THIS FILE IS GENERATED. EDITS WILL BE LOST. */

#pragma once

#include "servers/rendering/renderer_rd/shader_rd.h"

class TonemapMobileShaderRD : public ShaderRD {
public:
	TonemapMobileShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(location = 0) out vec2 uv_interp;

void main() {
	
	
	
	
	

	vec2 vertex_base;
	if (gl_VertexIndex == 0) {
		vertex_base = vec2(-1.0, -1.0);
	} else if (gl_VertexIndex == 1) {
		vertex_base = vec2(-1.0, 3.0);
	} else {
		vertex_base = vec2(3.0, -1.0);
	}
	gl_Position = vec4(vertex_base, 0.0, 1.0);
	uv_interp = clamp(vertex_base, vec2(0.0, 0.0), vec2(1.0, 1.0)) * 2.0; 
}

)<!>"
		};
		static const char _fragment_code[] = {
(R"<!>(
#version 450

#VERSION_DEFINES

#ifdef USE_MULTIVIEW
#extension GL_EXT_multiview : enable
#define ViewIndex gl_ViewIndex
#endif 

layout(location = 0) in vec2 uv_interp;

#ifdef USE_MULTIVIEW
#define SAMPLER_FORMAT sampler2DArray
#else
#define SAMPLER_FORMAT sampler2D
#endif


#ifdef SUBPASS
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput input_color;
#else
layout(set = 0, binding = 0) uniform SAMPLER_FORMAT source_color;
#endif

layout(set = 0, binding = 1) uniform SAMPLER_FORMAT source_glow;
layout(set = 0, binding = 2) uniform sampler2D glow_map;

#ifdef USE_1D_LUT
layout(set = 0, binding = 3) uniform sampler2D source_color_correction;
#else
layout(set = 0, binding = 3) uniform sampler3D source_color_correction;
#endif

layout(constant_id = 0) const bool use_bcs = false;
layout(constant_id = 1) const bool use_glow = false;
layout(constant_id = 2) const bool use_glow_map = false;
layout(constant_id = 3) const bool use_color_correction = false;
layout(constant_id = 4) const bool use_fxaa = false;
layout(constant_id = 5) const bool deband_8_bit = false;
layout(constant_id = 6) const bool deband_10_bit = false;
layout(constant_id = 7) const bool convert_to_srgb = false;
layout(constant_id = 8) const bool tonemapper_linear = false;
layout(constant_id = 9) const bool tonemapper_reinhard = false;
layout(constant_id = 10) const bool tonemapper_filmic = false;
layout(constant_id = 11) const bool tonemapper_aces = false;
layout(constant_id = 12) const bool tonemapper_agx = false;
layout(constant_id = 13) const bool glow_mode_add = false;
layout(constant_id = 14) const bool glow_mode_screen = false;
layout(constant_id = 15) const bool glow_mode_softlight = false;
layout(constant_id = 16) const bool glow_mode_replace = false;
layout(constant_id = 17) const bool glow_mode_mix = false;

layout(push_constant, std430) uniform Params {
	vec3 bcs;
	float luminance_multiplier;

	vec2 src_pixel_size;
	vec2 dest_pixel_size;

	float glow_intensity;
	float glow_map_strength;
	float exposure;
	float white;

	vec4 tonemapper_params;

	float output_max_value;
	float pad[3];
}
params;

layout(location = 0) out vec4 frag_color;


vec3 tonemap_reinhard(vec3 color) {
	float white_squared = params.tonemapper_params.x;
	
	return color * (1.0f + color / white_squared) / (1.0f + color / params.output_max_value);
}

vec3 tonemap_filmic(vec3 color) {
	
	
	
	
	const float exposure_bias = 2.0f;
	const float A = 0.22f * exposure_bias * exposure_bias; 
	const float B = 0.30f * exposure_bias;
	const float C = 0.10f;
	const float D = 0.20f;
	const float E = 0.01f;
	const float F = 0.30f;

	vec3 color_tonemapped = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;

	return color_tonemapped / params.tonemapper_params.x;
}



vec3 tonemap_aces(vec3 color) {
	
	const float exposure_bias = 1.8f;
	const float A = 0.0245786f;
	const float B = 0.000090537f;
	const float C = 0.983729f;
	const float D = 0.432951f;
	const float E = 0.238081f;

	
	const mat3 rgb_to_rrt = mat3(
			vec3(0.59719f * exposure_bias, 0.35458f * exposure_bias, 0.04823f * exposure_bias),
			vec3(0.07600f * exposure_bias, 0.90834f * exposure_bias, 0.01566f * exposure_bias),
			vec3(0.02840f * exposure_bias, 0.13383f * exposure_bias, 0.83777f * exposure_bias));

	const mat3 odt_to_rgb = mat3(
			vec3(1.60475f, -0.53108f, -0.07367f),
			vec3(-0.10208f, 1.10813f, -0.00605f),
			vec3(-0.00327f, -0.07276f, 1.07602f));

	color *= rgb_to_rrt;
	vec3 color_tonemapped = (color * (color + A) - B) / (color * (C * color + D) + E);
	color_tonemapped *= odt_to_rgb;

	return color_tonemapped / params.tonemapper_params.x;
}




vec3 allenwp_curve(vec3 x) {
	
	
	const float awp_crossover_point = 0.18;
	
	
	const float awp_shoulder_max = params.output_max_value - awp_crossover_point;

	float awp_contrast = params.tonemapper_params.x;
	float awp_toe_a = params.tonemapper_params.y;
	float awp_slope = params.tonemapper_params.z;
	float awp_w = params.tonemapper_params.w;

	
	vec3 s = x - awp_crossover_point;
	vec3 slope_s = awp_slope * s;
	s = slope_s * (1.0 + s / awp_w) / (1.0 + (slope_s / awp_shoulder_max));
	s += awp_crossover_point;

	
	vec3 t = pow(x, vec3(awp_contrast));
	t = t / (t + awp_toe_a);

	return mix(s, t, lessThan(x, vec3(awp_crossover_point)));
}





vec3 tonemap_agx(vec3 color) {
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	

	
	const mat3 rec709_to_rec2020_agx_inset_matrix = mat3(
			0.544814746488245, 0.140416948464053, 0.0888104196149096,
			0.373787398372697, 0.754137554567394, 0.178871756420858,
			0.0813978551390581, 0.105445496968552, 0.732317823964232);

	
	const mat3 agx_outset_rec2020_to_rec709_matrix = mat3(
			1.96488741169489, -0.299313364904742, -0.164352742528393,
			-0.855988495690215, 1.32639796461980, -0.238183969428088,
			-0.108898916004672, -0.0270845997150571, 1.40253671195648);

	
	color = rec709_to_rec2020_agx_inset_matrix * color;

	
	
	color = allenwp_curve(color);

	
	
	color = min(vec3(params.output_max_value), color);

	
	color = agx_outset_rec2020_to_rec709_matrix * color;

	
	
	
	return color;
}

vec3 linear_to_srgb(vec3 color) {
	const vec3 a = vec3(0.055f);
	return mix((vec3(1.0f) + a) * pow(color.rgb, vec3(1.0f / 2.4f)) - a, 12.92f * color.rgb, lessThan(color.rgb, vec3(0.0031308f)));
}

vec3 srgb_to_linear(vec3 color) {
	const vec3 a = vec3(0.055f);
	return mix(pow((color.rgb + a) * (1.0f / (vec3(1.0f) + a)), vec3(2.4f)), color.rgb * (1.0f / 12.92f), lessThan(color.rgb, vec3(0.04045f)));
}

vec3 apply_tonemapping(vec3 color) { 
	if (tonemapper_linear) {
		return color;
	}

	
	
	color = max(vec3(0.0), color);

	if (tonemapper_reinhard) {
		return tonemap_reinhard(color);
	} else if (tonemapper_filmic) {
		return tonemap_filmic(color);
	} else if (tonemapper_aces) {
		return tonemap_aces(color);
	} else { 
		return tonemap_agx(color);
	}
}

#ifdef USE_MULTIVIEW
vec3 gather_glow() {
	vec2 uv = gl_FragCoord.xy * params.dest_pixel_size;
	return textureLod(source_glow, vec3(uv, ViewIndex), 0.0).rgb * params.luminance_multiplier;
}
#else
vec3 gather_glow() {
	vec2 uv = gl_FragCoord.xy * params.dest_pixel_size;
	return textureLod(source_glow, uv, 0.0).rgb * params.luminance_multiplier;
}
#endif 


vec3 apply_glow(vec3 color, vec3 glow, float white) {
	if (glow_mode_add) {
		return color + glow;
	} else if (glow_mode_screen) {
		
		
		
		
		
		glow.rgb = clamp(glow.rgb, 0.0, white);

		
		
		
		
		
		

		
		color.rgb = color.rgb + glow.rgb - (color.rgb * glow.rgb / white);

		return color;
	} else if (glow_mode_softlight) {
		
		
		
		
		
		
		glow.rgb = clamp(glow.rgb, 0.0, 1.0);

		color.r = color.r > 1.0 ? color.r : color.r + glow.r * ((color.r <= 0.25f ? ((16.0f * color.r - 12.0f) * color.r + 4.0f) * color.r : sqrt(color.r)) - color.r);
		color.g = color.g > 1.0 ? color.g : color.g + glow.g * ((color.g <= 0.25f ? ((16.0f * color.g - 12.0f) * color.g + 4.0f) * color.g : sqrt(color.g)) - color.g);
		color.b = color.b > 1.0 ? color.b : color.b + glow.b * ((color.b <= 0.25f ? ((16.0f * color.b - 12.0f) * color.b + 4.0f) * color.b : sqrt(color.b)) - color.b);

		return color;
	} else { 
		return glow;
	}
}

#ifdef USE_1D_LUT
vec3 apply_color_correction(vec3 color) {
	color.r = texture(source_color_correction, vec2(color.r, 0.0f)).r;
	color.g = texture(source_color_correction, vec2(color.g, 0.0f)).g;
	color.b = texture(source_color_correction, vec2(color.b, 0.0f)).b;
	return color;
}
#else
vec3 apply_color_correction(vec3 color) {
	return textureLod(source_color_correction, color, 0.0).rgb;
}
#endif

#ifndef SUBPASS

































































float QUALITY(float q) {
	return (q < 5 ? 1.0 : (q > 5 ? (q < 10 ? 2.0 : (q < 11 ? 4.0 : 8.0)) : 1.5));
}

float rgb2luma(vec3 rgb) {
	return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

vec3 do_fxaa(vec3 color, float exposure, vec2 uv_interp) {
	const float EDGE_THRESHOLD_MIN = 0.0312;
	const float EDGE_THRESHOLD_MAX = 0.125;
	const int ITERATIONS = 12;
	const float SUBPIXEL_QUALITY = 0.75;

#ifdef USE_MULTIVIEW
	float lumaUp = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(0, 1)).xyz * exposure * params.luminance_multiplier);
	float lumaDown = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(0, -1)).xyz * exposure * params.luminance_multiplier);
	float lumaLeft = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(-1, 0)).xyz * exposure * params.luminance_multiplier);
	float lumaRight = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(1, 0)).xyz * exposure * params.luminance_multiplier);

	float lumaCenter = rgb2luma(color);

	float lumaMin = min(lumaCenter, min(min(lumaUp, lumaDown), min(lumaLeft, lumaRight)));
	float lumaMax = max(lumaCenter, max(max(lumaUp, lumaDown), max(lumaLeft, lumaRight)));

	float lumaRange = lumaMax - lumaMin;

	if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
		return color;
	}

	float lumaDownLeft = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(-1, -1)).xyz * exposure * params.luminance_multiplier);
	float lumaUpRight = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(1, 1)).xyz * exposure * params.luminance_multiplier);
	float lumaUpLeft = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(-1, 1)).xyz * exposure * params.luminance_multiplier);
	float lumaDownRight = rgb2luma(textureLodOffset(source_color, vec3(uv_interp, ViewIndex), 0.0, ivec2(1, -1)).xyz * exposure * params.luminance_multiplier);

	float lumaDownUp = lumaDown + lumaUp;
	float lumaLeftRight = lumaLeft + lumaRight;

	float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
	float lumaDownCorners = lumaDownLeft + lumaDownRight;
	float lumaRightCorners = lumaDownRight + lumaUpRight;
	float lumaUpCorners = lumaUpRight + lumaUpLeft;

	float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 + abs(-2.0 * lumaRight + lumaRightCorners);
	float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 + abs(-2.0 * lumaDown + lumaDownCorners);

	bool isHorizontal = (edgeHorizontal >= edgeVertical);

	float stepLength = isHorizontal ? params.src_pixel_size.y : params.src_pixel_size.x;

	float luma1 = isHorizontal ? lumaDown : lumaLeft;
	float luma2 = isHorizontal ? lumaUp : lumaRight;
	float gradient1 = luma1 - lumaCenter;
	float gradient2 = luma2 - lumaCenter;

	bool is1Steepest = abs(gradient1) >= abs(gradient2);

	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	float lumaLocalAverage = 0.0;
	if (is1Steepest) {
		stepLength = -stepLength;
		lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
	} else {
		lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
	}

	vec2 currentUv = uv_interp;
	if (isHorizontal) {
		currentUv.y += stepLength * 0.5;
	} else {
		currentUv.x += stepLength * 0.5;
	}

	vec2 offset = isHorizontal ? vec2(params.src_pixel_size.x, 0.0) : vec2(0.0, params.src_pixel_size.y);
	vec3 uv1 = vec3(currentUv - offset * QUALITY(0), ViewIndex);
	vec3 uv2 = vec3(currentUv + offset * QUALITY(0), ViewIndex);

	float lumaEnd1 = rgb2luma(textureLod(source_color, uv1, 0.0).xyz * exposure * params.luminance_multiplier);
	float lumaEnd2 = rgb2luma(textureLod(source_color, uv2, 0.0).xyz * exposure * params.luminance_multiplier);
	lumaEnd1 -= lumaLocalAverage;
	lumaEnd2 -= lumaLocalAverage;

	bool reached1 = abs(lumaEnd1) >= gradientScaled;
	bool reached2 = abs(lumaEnd2) >= gradientScaled;
	bool reachedBoth = reached1 && reached2;

	if (!reached1) {
		uv1 -= vec3(offset * QUALITY(1), 0.0);
	}
	if (!reached2) {
		uv2 += vec3(offset * QUALITY(1), 0.0);
	}

	if (!reachedBoth) {
		for (int i = 2; i < ITERATIONS; i++) {
			if (!reached1) {
				lumaEnd1 = rgb2luma(textureLod(source_color, uv1, 0.0).xyz * exposure * params.luminance_multiplier);
				lumaEnd1 = lumaEnd1 - lumaLocalAverage;
			}
			if (!reached2) {
				lumaEnd2 = rgb2luma(textureLod(source_color, uv2, 0.0).xyz * exposure * params.luminance_multiplier);
				lumaEnd2 = lumaEnd2 - lumaLocalAverage;
			}
			reached1 = abs(lumaEnd1) >= gradientScaled;
			reached2 = abs(lumaEnd2) >= gradientScaled;
			reachedBoth = reached1 && reached2;
			if (!reached1) {
				uv1 -= vec3(offset * QUALITY(i), 0.0);
			}
			if (!reached2) {
				uv2 += vec3(offset * QUALITY(i), 0.0);
			}
			if (reachedBoth) {
				break;
			}
		}
	}

	float distance1 = isHorizontal ? (uv_interp.x - uv1.x) : (uv_interp.y - uv1.y);
	float distance2 = isHorizontal ? (uv2.x - uv_interp.x) : (uv2.y - uv_interp.y);

	bool isDirection1 = distance1 < distance2;
	float distanceFinal = min(distance1, distance2);

	float edgeThickness = (distance1 + distance2);

	bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;

	bool correctVariation1 = (lumaEnd1 < 0.0) != isLumaCenterSmaller;
	bool correctVariation2 = (lumaEnd2 < 0.0) != isLumaCenterSmaller;

	bool correctVariation = isDirection1 ? correctVariation1 : correctVariation2;

	float pixelOffset = -distanceFinal / edgeThickness + 0.5;

	float finalOffset = correctVariation ? pixelOffset : 0.0;

	float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);

	float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
	float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;

	float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;

	finalOffset = max(finalOffset, subPixelOffsetFinal);

	vec3 finalUv = vec3(uv_interp, ViewIndex);
	if (isHorizontal) {
		finalUv.y += finalOffset * stepLength;
	} else {
		finalUv.x += finalOffset * stepLength;
	}

	vec3 finalColor = textureLod(source_color, finalUv, 0.0).xyz * exposure * params.luminance_multiplier;
	return finalColor;

#else
	float lumaUp = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(0, 1)).xyz * exposure * params.luminance_multiplier);
	float lumaDown = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(0, -1)).xyz * exposure * params.luminance_multiplier);
	float lumaLeft = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(-1, 0)).xyz * exposure * params.luminance_multiplier);
	float lumaRight = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(1, 0)).xyz * exposure * params.luminance_multiplier);

	float lumaCenter = rgb2luma(color);

	float lumaMin = min(lumaCenter, min(min(lumaUp, lumaDown), min(lumaLeft, lumaRight)));
	float lumaMax = max(lumaCenter, max(max(lumaUp, lumaDown), max(lumaLeft, lumaRight)));

	float lumaRange = lumaMax - lumaMin;

	if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
		return color;
	}

	float lumaDownLeft = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(-1, -1)).xyz * exposure * params.luminance_multiplier);
	float lumaUpRight = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(1, 1)).xyz * exposure * params.luminance_multiplier);
	float lumaUpLeft = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(-1, 1)).xyz * exposure * params.luminance_multiplier);
	float lumaDownRight = rgb2luma(textureLodOffset(source_color, uv_interp, 0.0, ivec2(1, -1)).xyz * exposure * params.luminance_multiplier);

	float lumaDownUp = lumaDown + lumaUp;
	float lumaLeftRight = lumaLeft + lumaRight;

	float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
	float lumaDownCorners = lumaDownLeft + lumaDownRight;
	float lumaRightCorners = lumaDownRight + lumaUpRight;
	float lumaUpCorners = lumaUpRight + lumaUpLeft;

	float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 + abs(-2.0 * lumaRight + lumaRightCorners);
	float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 + abs(-2.0 * lumaDown + lumaDownCorners);

	bool isHorizontal = (edgeHorizontal >= edgeVertical);

	float stepLength = isHorizontal ? params.src_pixel_size.y : params.src_pixel_size.x;

	float luma1 = isHorizontal ? lumaDown : lumaLeft;
	float luma2 = isHorizontal ? lumaUp : lumaRight;
	float gradient1 = luma1 - lumaCenter;
	float gradient2 = luma2 - lumaCenter;

	bool is1Steepest = abs(gradient1) >= abs(gradient2);
)<!>" R"<!>(
	float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

	float lumaLocalAverage = 0.0;
	if (is1Steepest) {
		stepLength = -stepLength;
		lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
	} else {
		lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
	}

	vec2 currentUv = uv_interp;
	if (isHorizontal) {
		currentUv.y += stepLength * 0.5;
	} else {
		currentUv.x += stepLength * 0.5;
	}

	vec2 offset = isHorizontal ? vec2(params.src_pixel_size.x, 0.0) : vec2(0.0, params.src_pixel_size.y);
	vec2 uv1 = currentUv - offset * QUALITY(0);
	vec2 uv2 = currentUv + offset * QUALITY(0);

	float lumaEnd1 = rgb2luma(textureLod(source_color, uv1, 0.0).xyz * exposure * params.luminance_multiplier);
	float lumaEnd2 = rgb2luma(textureLod(source_color, uv2, 0.0).xyz * exposure * params.luminance_multiplier);
	lumaEnd1 -= lumaLocalAverage;
	lumaEnd2 -= lumaLocalAverage;

	bool reached1 = abs(lumaEnd1) >= gradientScaled;
	bool reached2 = abs(lumaEnd2) >= gradientScaled;
	bool reachedBoth = reached1 && reached2;

	if (!reached1) {
		uv1 -= offset * QUALITY(1);
	}
	if (!reached2) {
		uv2 += offset * QUALITY(1);
	}

	if (!reachedBoth) {
		for (int i = 2; i < ITERATIONS; i++) {
			if (!reached1) {
				lumaEnd1 = rgb2luma(textureLod(source_color, uv1, 0.0).xyz * exposure * params.luminance_multiplier);
				lumaEnd1 = lumaEnd1 - lumaLocalAverage;
			}
			if (!reached2) {
				lumaEnd2 = rgb2luma(textureLod(source_color, uv2, 0.0).xyz * exposure * params.luminance_multiplier);
				lumaEnd2 = lumaEnd2 - lumaLocalAverage;
			}
			reached1 = abs(lumaEnd1) >= gradientScaled;
			reached2 = abs(lumaEnd2) >= gradientScaled;
			reachedBoth = reached1 && reached2;
			if (!reached1) {
				uv1 -= offset * QUALITY(i);
			}
			if (!reached2) {
				uv2 += offset * QUALITY(i);
			}
			if (reachedBoth) {
				break;
			}
		}
	}

	float distance1 = isHorizontal ? (uv_interp.x - uv1.x) : (uv_interp.y - uv1.y);
	float distance2 = isHorizontal ? (uv2.x - uv_interp.x) : (uv2.y - uv_interp.y);

	bool isDirection1 = distance1 < distance2;
	float distanceFinal = min(distance1, distance2);

	float edgeThickness = (distance1 + distance2);

	bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;

	bool correctVariation1 = (lumaEnd1 < 0.0) != isLumaCenterSmaller;
	bool correctVariation2 = (lumaEnd2 < 0.0) != isLumaCenterSmaller;

	bool correctVariation = isDirection1 ? correctVariation1 : correctVariation2;

	float pixelOffset = -distanceFinal / edgeThickness + 0.5;

	float finalOffset = correctVariation ? pixelOffset : 0.0;

	float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);

	float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
	float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;

	float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * SUBPIXEL_QUALITY;

	finalOffset = max(finalOffset, subPixelOffsetFinal);

	vec2 finalUv = uv_interp;
	if (isHorizontal) {
		finalUv.y += finalOffset * stepLength;
	} else {
		finalUv.x += finalOffset * stepLength;
	}

	vec3 finalColor = textureLod(source_color, finalUv, 0.0).xyz * exposure * params.luminance_multiplier;
	return finalColor;

#endif
}
#endif 






vec3 screen_space_dither(vec2 frag_coord, float bit_alignment_diviser) {
	
	
	vec3 dither = vec3(dot(vec2(171.0, 231.0), frag_coord));
	dither.rgb = fract(dither.rgb / vec3(103.0, 71.0, 97.0));

	
	
	return (dither.rgb - 0.5) / bit_alignment_diviser;
}

void main() {
#ifdef SUBPASS
	
#ifdef USE_MULTIVIEW
	
	
	uint vi = ViewIndex;
#endif
	vec4 color = subpassLoad(input_color);
#elif defined(USE_MULTIVIEW)
	vec4 color = textureLod(source_color, vec3(uv_interp, ViewIndex), 0.0f);
#else
	vec4 color = textureLod(source_color, uv_interp, 0.0f);
#endif
	color.rgb *= params.luminance_multiplier;

	

	color.rgb *= params.exposure;

#ifndef SUBPASS
	
	if (use_fxaa) {
		
		color.rgb = do_fxaa(color.rgb, params.exposure, uv_interp);
	}

	if (use_glow && !glow_mode_softlight) {
		vec3 glow = gather_glow() * params.glow_intensity;
		if (use_glow_map) {
			glow = mix(glow, texture(glow_map, uv_interp).rgb * glow, params.glow_map_strength);
		}

		if (glow_mode_mix) {
			color.rgb = color.rgb * (1.0 - params.glow_intensity) + glow;
		} else {
			color.rgb = apply_glow(color.rgb, glow, params.white);
		}
	}
#endif

	

	color.rgb = apply_tonemapping(color.rgb);

#ifndef SUBPASS
	

	if (use_glow && glow_mode_softlight) {
		
		
		
		vec3 glow = gather_glow() * params.glow_intensity;
		if (use_glow_map) {
			glow = mix(glow, texture(glow_map, uv_interp).rgb * glow, params.glow_map_strength);
		}
		glow = apply_tonemapping(glow);
		color.rgb = apply_glow(color.rgb, glow, params.white);
	}
#endif

	

	if (use_bcs) {
		
		
		
		
		color.rgb = color.rgb * params.bcs.x;

		color.rgb = linear_to_srgb(color.rgb);

		
		
		
		
		
		color.rgb = mix(vec3(0.5), color.rgb, params.bcs.y);

		
		
		
		
		color.rgb = mix(vec3(dot(vec3(1.0), color.rgb) * (1.0 / 3.0)), color.rgb, params.bcs.z);

		if (use_color_correction) {
			color.rgb = clamp(color.rgb, vec3(0.0), vec3(1.0));
			color.rgb = apply_color_correction(color.rgb);
			
			
			
		} else if (!convert_to_srgb) {
			color.rgb = srgb_to_linear(color.rgb);
		}
	} else if (convert_to_srgb) {
		color.rgb = linear_to_srgb(color.rgb); 
	}

	
	
	if (deband_8_bit) {
		
		color.rgb += screen_space_dither(gl_FragCoord.xy, 255.0);
	} else if (deband_10_bit) {
		
		color.rgb += screen_space_dither(gl_FragCoord.xy, 1023.0);
	}

	frag_color = color;
}
)<!>")
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "TonemapMobileShaderRD");
	}
};
