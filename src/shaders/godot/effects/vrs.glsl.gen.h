/**************************************************************************/
/*  vrs.glsl.gen.h                                                        */
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

class VrsShaderRD : public ShaderRD {
public:
	VrsShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#ifdef USE_MULTIVIEW
#extension GL_EXT_multiview : enable
#define ViewIndex gl_ViewIndex
#endif 

#ifdef USE_MULTIVIEW
layout(location = 0) out vec3 uv_interp;
#else
layout(location = 0) out vec2 uv_interp;
#endif

layout(push_constant, std430) uniform Params {
	float max_texel_factor;
	float res1;
	float res2;
	float res3;
}
params;

void main() {
	vec2 base_arr[3] = vec2[](vec2(-1.0, -1.0), vec2(-1.0, 3.0), vec2(3.0, -1.0));
	gl_Position = vec4(base_arr[gl_VertexIndex], 0.0, 1.0);
	uv_interp.xy = clamp(gl_Position.xy, vec2(0.0, 0.0), vec2(1.0, 1.0)) * 2.0; 
#ifdef USE_MULTIVIEW
	uv_interp.z = ViewIndex;
#endif
}

)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#ifdef USE_MULTIVIEW
layout(location = 0) in vec3 uv_interp;
layout(set = 0, binding = 0) uniform sampler2DArray source_color;
#else /* USE_MULTIVIEW */
layout(location = 0) in vec2 uv_interp;
layout(set = 0, binding = 0) uniform sampler2D source_color;
#endif /* USE_MULTIVIEW */

#ifdef SPLIT_RG
layout(location = 0) out vec2 frag_color;
#else
layout(location = 0) out uint frag_color;
#endif

layout(push_constant, std430) uniform Params {
	float max_texel_factor;
	float res1;
	float res2;
	float res3;
}
params;

void main() {
#ifdef USE_MULTIVIEW
	vec3 uv = uv_interp;
#else
	vec2 uv = uv_interp;
#endif

	
	vec4 color = textureLod(source_color, uv, 0.0);

#ifdef SPLIT_RG
	
	frag_color = max(vec2(1.0f) - color.rg, vec2(1.0f / 255.0f));
#else
	
	color.r = clamp(floor(color.r * params.max_texel_factor + 0.1), 0.0, params.max_texel_factor);
	color.g = clamp(floor(color.g * params.max_texel_factor + 0.1), 0.0, params.max_texel_factor);

	
	if (color.r < (color.g - 1.0)) {
		color.r = color.g - 1.0;
	}
	if (color.g < (color.r - 1.0)) {
		color.g = color.r - 1.0;
	}

	
	frag_color = int(color.r + 0.1) << 2;
	frag_color += int(color.g + 0.1);
#endif
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "VrsShaderRD");
	}
};
