/**************************************************************************/
/*  tonemap.glsl.gen.h                                                    */
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

class TonemapShaderRD : public ShaderRD {
public:
	TonemapShaderRD() {
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

layout(set = 0, binding = 0) uniform SAMPLER_FORMAT source_color;
layout(set = 1, binding = 0) uniform sampler2D source_auto_exposure;
layout(set = 2, binding = 0) uniform SAMPLER_FORMAT source_glow;
layout(set = 2, binding = 1) uniform sampler2D glow_map; 

#ifdef USE_1D_LUT
layout(set = 3, binding = 0) uniform sampler2D source_color_correction;
#else
layout(set = 3, binding = 0) uniform sampler3D source_color_correction;
#endif

#define FLAG_USE_BCS (1 << 0)
#define FLAG_USE_GLOW (1 << 1)
#define FLAG_USE_AUTO_EXPOSURE (1 << 2)
#define FLAG_USE_COLOR_CORRECTION (1 << 3)
#define FLAG_USE_FXAA (1 << 4)
#define FLAG_USE_8_BIT_DEBANDING (1 << 5)
#define FLAG_CONVERT_TO_SRGB (1 << 6)

layout(push_constant, std430) uniform Params {
	vec3 bcs;
	uint flags;

	vec2 pixel_size;
	uint tonemapper;
	float output_max_value;

	uvec2 glow_texture_size;
	float glow_intensity;
	float glow_map_strength;

	uint glow_mode;
	float glow_levels[7];

	float exposure;
	float white;
	float auto_exposure_scale;
	float luminance_multiplier;

	vec4 tonemapper_params;
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

#define TONEMAPPER_LINEAR 0
#define TONEMAPPER_REINHARD 1
#define TONEMAPPER_FILMIC 2
#define TONEMAPPER_ACES 3
#define TONEMAPPER_AGX 4

vec3 apply_tonemapping(vec3 color) { 
	if (params.tonemapper == TONEMAPPER_LINEAR) {
		return color;
	}

	
	
	color = max(vec3(0.0), color);

	if (params.tonemapper == TONEMAPPER_REINHARD) {
		return tonemap_reinhard(color);
	} else if (params.tonemapper == TONEMAPPER_FILMIC) {
		return tonemap_filmic(color);
	} else if (params.tonemapper == TONEMAPPER_ACES) {
		return tonemap_aces(color);
	} else { 
		return tonemap_agx(color);
	}
}

#ifdef USE_GLOW_FILTER_BICUBIC

float w0(float a) {
	return (1.0f / 6.0f) * (a * (a * (-a + 3.0f) - 3.0f) + 1.0f);
}

float w1(float a) {
	return (1.0f / 6.0f) * (a * a * (3.0f * a - 6.0f) + 4.0f);
}

float w2(float a) {
	return (1.0f / 6.0f) * (a * (a * (-3.0f * a + 3.0f) + 3.0f) + 1.0f);
}

float w3(float a) {
	return (1.0f / 6.0f) * (a * a * a);
}


float g0(float a) {
	return w0(a) + w1(a);
}

float g1(float a) {
	return w2(a) + w3(a);
}


float h0(float a) {
	return -1.0f + w1(a) / (w0(a) + w1(a));
}

float h1(float a) {
	return 1.0f + w3(a) / (w2(a) + w3(a));
}

#ifdef USE_MULTIVIEW
vec4 texture2D_bicubic(sampler2DArray tex, vec2 uv, int p_lod) {
	float lod = float(p_lod);
	vec2 tex_size = vec2(params.glow_texture_size >> p_lod);
	vec2 pixel_size = vec2(1.0f) / tex_size;

	uv = uv * tex_size + vec2(0.5f);

	vec2 iuv = floor(uv);
	vec2 fuv = fract(uv);

	float g0x = g0(fuv.x);
	float g1x = g1(fuv.x);
	float h0x = h0(fuv.x);
	float h1x = h1(fuv.x);
	float h0y = h0(fuv.y);
	float h1y = h1(fuv.y);

	vec3 p0 = vec3((vec2(iuv.x + h0x, iuv.y + h0y) - vec2(0.5f)) * pixel_size, ViewIndex);
	vec3 p1 = vec3((vec2(iuv.x + h1x, iuv.y + h0y) - vec2(0.5f)) * pixel_size, ViewIndex);
	vec3 p2 = vec3((vec2(iuv.x + h0x, iuv.y + h1y) - vec2(0.5f)) * pixel_size, ViewIndex);
	vec3 p3 = vec3((vec2(iuv.x + h1x, iuv.y + h1y) - vec2(0.5f)) * pixel_size, ViewIndex);

	return (g0(fuv.y) * (g0x * textureLod(tex, p0, lod) + g1x * textureLod(tex, p1, lod))) +
			(g1(fuv.y) * (g0x * textureLod(tex, p2, lod) + g1x * textureLod(tex, p3, lod)));
}

#define GLOW_TEXTURE_SAMPLE(m_tex, m_uv, m_lod) texture2D_bicubic(m_tex, m_uv, m_lod)
#else 

vec4 texture2D_bicubic(sampler2D tex, vec2 uv, int p_lod) {
	float lod = float(p_lod);
	vec2 tex_size = vec2(params.glow_texture_size >> p_lod);
	vec2 pixel_size = vec2(1.0f) / tex_size;

	uv = uv * tex_size + vec2(0.5f);

	vec2 iuv = floor(uv);
	vec2 fuv = fract(uv);

	float g0x = g0(fuv.x);
	float g1x = g1(fuv.x);
	float h0x = h0(fuv.x);
	float h1x = h1(fuv.x);
	float h0y = h0(fuv.y);
	float h1y = h1(fuv.y);

	vec2 p0 = (vec2(iuv.x + h0x, iuv.y + h0y) - vec2(0.5f)) * pixel_size;
	vec2 p1 = (vec2(iuv.x + h1x, iuv.y + h0y) - vec2(0.5f)) * pixel_size;
	vec2 p2 = (vec2(iuv.x + h0x, iuv.y + h1y) - vec2(0.5f)) * pixel_size;
	vec2 p3 = (vec2(iuv.x + h1x, iuv.y + h1y) - vec2(0.5f)) * pixel_size;

	return (g0(fuv.y) * (g0x * textureLod(tex, p0, lod) + g1x * textureLod(tex, p1, lod))) +
			(g1(fuv.y) * (g0x * textureLod(tex, p2, lod) + g1x * textureLod(tex, p3, lod)));
}

#define GLOW_TEXTURE_SAMPLE(m_tex, m_uv, m_lod) texture2D_bicubic(m_tex, m_uv, m_lod)
#endif 

#else 

#ifdef USE_MULTIVIEW
#define GLOW_TEXTURE_SAMPLE(m_tex, m_uv, m_lod) textureLod(m_tex, vec3(m_uv, ViewIndex), float(m_lod))
#else 
#define GLOW_TEXTURE_SAMPLE(m_tex, m_uv, m_lod) textureLod(m_tex, m_uv, float(m_lod))
#endif 

#endif 

vec3 gather_glow(SAMPLER_FORMAT tex, vec2 uv) { 

	vec3 glow = vec3(0.0f);

	if (params.glow_levels[0] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 0).rgb * params.glow_levels[0];
	}

	if (params.glow_levels[1] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 1).rgb * params.glow_levels[1];
	}

	if (params.glow_levels[2] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 2).rgb * params.glow_levels[2];
	}

	if (params.glow_levels[3] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 3).rgb * params.glow_levels[3];
	}

	if (params.glow_levels[4] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 4).rgb * params.glow_levels[4];
	}

	if (params.glow_levels[5] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 5).rgb * params.glow_levels[5];
	}

	if (params.glow_levels[6] > 0.0001) {
		glow += GLOW_TEXTURE_SAMPLE(tex, uv, 6).rgb * params.glow_levels[6];
	}

	glow = glow * params.luminance_multiplier;

	return glow;
}

#define GLOW_MODE_ADD 0
#define GLOW_MODE_SCREEN 1
#define GLOW_MODE_SOFTLIGHT 2
#define GLOW_MODE_REPLACE 3
#define GLOW_MODE_MIX 4


vec3 apply_glow(vec3 color, vec3 glow, float white) {
	if (params.glow_mode == GLOW_MODE_ADD) {
		return color + glow;
	} else if (params.glow_mode == GLOW_MODE_SCREEN) {
		
		
		
		
		
		glow.rgb = clamp(glow.rgb, 0.0, white);

		
		
		
		
		
		

		
		color.rgb = color.rgb + glow.rgb - (color.rgb * glow.rgb / white);

		return color;
	} else if (params.glow_mode == GLOW_MODE_SOFTLIGHT) {
		
		
		
		
		
		
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

	float stepLength = isHorizontal ? params.pixel_size.y : params.pixel_size.x;

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

	vec2 offset = isHorizontal ? vec2(params.pixel_size.x, 0.0) : vec2(0.0, params.pixel_size.y);
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
)<!>" R"<!>(
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

	float stepLength = isHorizontal ? params.pixel_size.y : params.pixel_size.x;

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

	vec2 offset = isHorizontal ? vec2(params.pixel_size.x, 0.0) : vec2(0.0, params.pixel_size.y);
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






vec3 screen_space_dither(vec2 frag_coord, float bit_alignment_diviser) {
	
	
	vec3 dither = vec3(dot(vec2(171.0, 231.0), frag_coord));
	dither.rgb = fract(dither.rgb / vec3(103.0, 71.0, 97.0));

	
	
	return (dither.rgb - 0.5) / bit_alignment_diviser;
}

void main() {
#ifdef USE_MULTIVIEW
	vec4 color = textureLod(source_color, vec3(uv_interp, ViewIndex), 0.0f);
#else
	vec4 color = textureLod(source_color, uv_interp, 0.0f);
#endif
	color.rgb *= params.luminance_multiplier;

	

	float exposure = params.exposure;

	if (bool(params.flags & FLAG_USE_AUTO_EXPOSURE)) {
		exposure *= 1.0 / (texelFetch(source_auto_exposure, ivec2(0, 0), 0).r * params.luminance_multiplier / params.auto_exposure_scale);
	}

	color.rgb *= exposure;

	
	if (bool(params.flags & FLAG_USE_FXAA)) {
		
		color.rgb = do_fxaa(color.rgb, exposure, uv_interp);
	}

	if (bool(params.flags & FLAG_USE_GLOW) && params.glow_mode != GLOW_MODE_SOFTLIGHT) {
		vec3 glow = gather_glow(source_glow, uv_interp) * params.glow_intensity;
		if (params.glow_map_strength > 0.001) {
			glow = mix(glow, texture(glow_map, uv_interp).rgb * glow, params.glow_map_strength);
		}

		if (params.glow_mode == GLOW_MODE_MIX) {
			color.rgb = color.rgb * (1.0 - params.glow_intensity) + glow;
		} else {
			color.rgb = apply_glow(color.rgb, glow, params.white);
		}
	}

	

	color.rgb = apply_tonemapping(color.rgb);

	

	if (bool(params.flags & FLAG_USE_GLOW) && params.glow_mode == GLOW_MODE_SOFTLIGHT) {
		
		
		
		vec3 glow = gather_glow(source_glow, uv_interp) * params.glow_intensity;
		if (params.glow_map_strength > 0.001) {
			glow = mix(glow, texture(glow_map, uv_interp).rgb * glow, params.glow_map_strength);
		}
		glow = apply_tonemapping(glow);
		color.rgb = apply_glow(color.rgb, glow, params.white);
	}

	

	if (bool(params.flags & FLAG_USE_BCS)) {
		
		
		
		
		color.rgb = color.rgb * params.bcs.x;

		color.rgb = linear_to_srgb(color.rgb);

		
		
		
		
		
		color.rgb = mix(vec3(0.5), color.rgb, params.bcs.y);

		
		
		
		
		color.rgb = mix(vec3(dot(vec3(1.0), color.rgb) * (1.0 / 3.0)), color.rgb, params.bcs.z);

		if (bool(params.flags & FLAG_USE_COLOR_CORRECTION)) {
			color.rgb = clamp(color.rgb, vec3(0.0), vec3(1.0));
			color.rgb = apply_color_correction(color.rgb);
			
			
			
		} else if (!bool(params.flags & FLAG_CONVERT_TO_SRGB)) {
			color.rgb = srgb_to_linear(color.rgb);
		}
	} else if (bool(params.flags & FLAG_CONVERT_TO_SRGB)) {
		color.rgb = linear_to_srgb(color.rgb);
	}

	
	

	if (bool(params.flags & FLAG_USE_8_BIT_DEBANDING)) {
		
		color.rgb += screen_space_dither(gl_FragCoord.xy, 255.0);
	}

	frag_color = color;
}
)<!>")
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "TonemapShaderRD");
	}
};
