/**************************************************************************/
/*  blit.glsl.gen.h                                                       */
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

class BlitShaderRD : public ShaderRD {
public:
	BlitShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(push_constant, std140) uniform Pos {
	vec4 src_rect;
	vec4 dst_rect;

	float rotation_sin;
	float rotation_cos;
	vec2 eye_center;

	float k1;
	float k2;
	float upscale;
	float aspect_ratio;

	uint layer;
	bool source_is_srgb;
	bool use_debanding;
	uint target_color_space;

	float reference_multiplier;
	float output_max_value;
	uint pad1;
	uint pad2;
}
data;

layout(location = 0) out vec2 uv;

void main() {
	mat4 swapchain_transform = mat4(1.0);
	swapchain_transform[0][0] = data.rotation_cos;
	swapchain_transform[0][1] = -data.rotation_sin;
	swapchain_transform[1][0] = data.rotation_sin;
	swapchain_transform[1][1] = data.rotation_cos;

	vec2 base_arr[4] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0));
	uv = data.src_rect.xy + base_arr[gl_VertexIndex] * data.src_rect.zw;
	vec2 vtx = data.dst_rect.xy + base_arr[gl_VertexIndex] * data.dst_rect.zw;
	gl_Position = swapchain_transform * vec4(vtx * 2.0 - 1.0, 0.0, 1.0);
}

)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(push_constant, std140) uniform Pos {
	vec4 src_rect;
	vec4 dst_rect;

	float rotation_sin;
	float rotation_cos;
	vec2 eye_center;

	float k1;
	float k2;
	float upscale;
	float aspect_ratio;

	uint layer;
	bool source_is_srgb;
	bool use_debanding;
	uint target_color_space;

	float reference_multiplier;
	float output_max_value;
	uint pad1;
	uint pad2;
}
data;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 color;

#ifdef USE_LAYER
layout(binding = 0) uniform sampler2DArray src_rt;
#else
layout(binding = 0) uniform sampler2D src_rt;
#endif


#define COLOR_SPACE_REC709_LINEAR 0
#define COLOR_SPACE_REC709_NONLINEAR_SRGB 1

vec3 srgb_to_linear(vec3 color) {
	return mix(pow((color.rgb + vec3(0.055)) * (1.0 / (1.0 + 0.055)), vec3(2.4)), color.rgb * (1.0 / 12.92), lessThan(color.rgb, vec3(0.04045)));
}

vec3 linear_to_srgb(vec3 color) {
	const vec3 a = vec3(0.055f);
	return mix((vec3(1.0f) + a) * pow(color.rgb, vec3(1.0f / 2.4f)) - a, 12.92f * color.rgb, lessThan(color.rgb, vec3(0.0031308f)));
}






vec3 screen_space_dither(vec2 frag_coord) {
	
	
	vec3 dither = vec3(dot(vec2(171.0, 231.0), frag_coord));
	dither.rgb = fract(dither.rgb / vec3(103.0, 71.0, 97.0));

	
	
	
	return (dither.rgb - 0.5) / 255.0;
}

void main() {
#ifdef APPLY_LENS_DISTORTION
	vec2 coords = uv * 2.0 - 1.0;
	vec2 offset = coords - data.eye_center;

	
	offset.y /= data.aspect_ratio;

	
	vec2 offset_sq = offset * offset;
	float radius_sq = offset_sq.x + offset_sq.y;
	float radius_s4 = radius_sq * radius_sq;
	float distortion_scale = 1.0 + (data.k1 * radius_sq) + (data.k2 * radius_s4);
	offset *= distortion_scale;

	
	offset.y *= data.aspect_ratio;

	
	coords = offset + data.eye_center;
	coords /= data.upscale;

	
	if (coords.x < -1.0 || coords.y < -1.0 || coords.x > 1.0 || coords.y > 1.0) {
		color = vec4(0.0, 0.0, 0.0, 1.0);
	} else {
		
		coords = (coords + vec2(1.0)) / vec2(2.0);
		color = texture(src_rt, vec3(coords, data.layer));
	}
#elif defined(USE_LAYER)
	color = texture(src_rt, vec3(uv, data.layer));
#else
	color = texture(src_rt, uv);
#endif

	
	if (data.target_color_space == COLOR_SPACE_REC709_LINEAR) {
		if (data.source_is_srgb == true) {
			
			color.rgb = srgb_to_linear(color.rgb);
		}

		
		
		
		
		
		
		
		
		color.rgb = clamp(color.rgb, vec3(0.0), vec3(data.output_max_value));

		
		color.rgb *= data.reference_multiplier;
	} else if (data.target_color_space == COLOR_SPACE_REC709_NONLINEAR_SRGB) {
		
		
		if (data.source_is_srgb == false) {
			
			color.rgb = linear_to_srgb(color.rgb);

			
			
			
			
			
			
			if (data.use_debanding) {
				color.rgb += screen_space_dither(gl_FragCoord.xy);
			}
		}
	}
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "BlitShaderRD");
	}
};
