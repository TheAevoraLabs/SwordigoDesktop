/**************************************************************************/
/*  luminance_reduce.glsl.gen.h                                           */
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

class LuminanceReduceShaderRD : public ShaderRD {
public:
	LuminanceReduceShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#define BLOCK_SIZE 8

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

shared float tmp_data[BLOCK_SIZE * BLOCK_SIZE];

#ifdef READ_TEXTURE


layout(set = 0, binding = 0) uniform sampler2D source_texture;

#else


layout(r32f, set = 0, binding = 0) uniform restrict readonly image2D source_luminance;

#endif

layout(r32f, set = 1, binding = 0) uniform restrict writeonly image2D dest_luminance;

#ifdef WRITE_LUMINANCE
layout(set = 2, binding = 0) uniform sampler2D prev_luminance;
#endif

layout(push_constant, std430) uniform Params {
	ivec2 source_size;
	float max_luminance;
	float min_luminance;
	float exposure_adjust;
	float pad[3];
}
params;

void main() {
	uint t = gl_LocalInvocationID.y * BLOCK_SIZE + gl_LocalInvocationID.x;
	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

	if (any(lessThan(pos, params.source_size))) {
#ifdef READ_TEXTURE
		vec3 v = texelFetch(source_texture, pos, 0).rgb;
		tmp_data[t] = max(v.r, max(v.g, v.b));
#else
		tmp_data[t] = imageLoad(source_luminance, pos).r;
#endif
	} else {
		tmp_data[t] = 0.0;
	}

	groupMemoryBarrier();
	barrier();

	uint size = (BLOCK_SIZE * BLOCK_SIZE) >> 1;

	do {
		if (t < size) {
			tmp_data[t] += tmp_data[t + size];
		}
		groupMemoryBarrier();
		barrier();

		size >>= 1;
	} while (size >= 1);

	if (t == 0) {
		
		ivec2 rect_size = min(params.source_size - pos, ivec2(BLOCK_SIZE));
		float avg = tmp_data[0] / float(rect_size.x * rect_size.y);
		
		pos /= ivec2(BLOCK_SIZE);
#ifdef WRITE_LUMINANCE
		float prev_lum = texelFetch(prev_luminance, ivec2(0, 0), 0).r; 
		avg = clamp(prev_lum + (avg - prev_lum) * params.exposure_adjust, params.min_luminance, params.max_luminance);
#endif
		imageStore(dest_luminance, pos, vec4(avg));
	}
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "LuminanceReduceShaderRD");
	}
};
