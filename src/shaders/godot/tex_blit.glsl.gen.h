/**************************************************************************/
/*  tex_blit.glsl.gen.h                                                   */
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

class TexBlitShaderRD : public ShaderRD {
public:
	TexBlitShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450


layout(push_constant, std430) uniform TexBlitData {
	vec2 offset;
	vec2 size;
	vec4 modulate;
	vec2 pad;
	int convert_to_srgb;
	float time;
} data;

layout(location = 0) out vec2 uv;

void main() {
	vec2 base_arr[6] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0), vec2(1.0, 0.0), vec2(1.0, 1.0));
	uv = base_arr[gl_VertexIndex];
	

	gl_Position = vec4( (data.offset + (uv * data.size)) * 2.0 - 1.0, 1.0, 1.0);
}

)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 0) uniform sampler SAMPLER_NEAREST_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 1) uniform sampler SAMPLER_LINEAR_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 2) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 3) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 4) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 5) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 6) uniform sampler SAMPLER_NEAREST_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 7) uniform sampler SAMPLER_LINEAR_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 8) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 9) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 10) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 11) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_REPEAT;

#define OUTPUT0_SRGB uint(1)
#define OUTPUT1_SRGB uint(2)
#define OUTPUT2_SRGB uint(4)
#define OUTPUT3_SRGB uint(8)

layout(push_constant, std430) uniform TexBlitData {
	vec2 offset;
	vec2 size;
	vec4 modulate;
	vec2 pad;
	int convert_to_srgb;
	float time;
} data;

layout(set = 0, binding = 0) uniform texture2D source0;

layout(set = 0, binding = 1) uniform texture2D source1;

layout(set = 0, binding = 2) uniform texture2D source2;

layout(set = 0, binding = 3) uniform texture2D source3;

layout(location = 0) in vec2 uv;

layout (location = 0) out vec4 out_color0;

#ifdef USE_OUTPUT1
layout (location = 1) out vec4 out_color1;
#endif

#ifdef USE_OUTPUT2
layout (location = 2) out vec4 out_color2;
#endif

#ifdef USE_OUTPUT3
layout (location = 3) out vec4 out_color3;
#endif

#ifdef MATERIAL_UNIFORMS_USED
layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
#MATERIAL_UNIFORMS
} material;
#endif

#GLOBALS

vec3 linear_to_srgb(vec3 color) {
	
	color = clamp(color, vec3(0.0), vec3(1.0));
	const vec3 a = vec3(0.055f);
	return mix((vec3(1.0f) + a) * pow(color.rgb, vec3(1.0f / 2.4f)) - a, 12.92f * color.rgb, lessThan(color.rgb, vec3(0.0031308f)));
}

void main() {
	
	vec4 color0 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 color1 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 color2 = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 color3 = vec4(0.0, 0.0, 0.0, 1.0);

#CODE : BLIT

	
	out_color0 = color0;

#ifdef USE_OUTPUT1
	out_color1 = color1;
#endif
#ifdef USE_OUTPUT2
	out_color2 = color2;
#endif
#ifdef USE_OUTPUT3
	out_color3 = color3;
#endif

	if (bool(data.convert_to_srgb & OUTPUT0_SRGB)) {
		out_color0.rgb = linear_to_srgb(out_color0.rgb); 
	}
#ifdef USE_OUTPUT1
	if (bool(data.convert_to_srgb & OUTPUT1_SRGB)) {
		out_color1.rgb = linear_to_srgb(out_color1.rgb);
	}
#endif
#ifdef USE_OUTPUT2
	if (bool(data.convert_to_srgb & OUTPUT2_SRGB)) {
		out_color2.rgb = linear_to_srgb(out_color2.rgb);
	}
#endif
#ifdef USE_OUTPUT3
	if (bool(data.convert_to_srgb & OUTPUT3_SRGB)) {
		out_color3.rgb = linear_to_srgb(out_color3.rgb);
	}
#endif
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "TexBlitShaderRD");
	}
};
