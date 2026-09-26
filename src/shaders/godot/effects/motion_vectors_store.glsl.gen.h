/**************************************************************************/
/*  motion_vectors_store.glsl.gen.h                                       */
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

class MotionVectorsStoreShaderRD : public ShaderRD {
public:
	MotionVectorsStoreShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

vec2 derive_motion_vector(vec2 uv, float depth, mat4 reprojection_matrix) {
	vec4 previous_pos_ndc = reprojection_matrix * vec4(uv * 2.0f - 1.0f, depth * 2.0f - 1.0f, 1.0f);
	return 0.5f + (previous_pos_ndc.xy / previous_pos_ndc.w) * 0.5f - uv;
}

#define FFX_FSR2_OPTION_GODOT_DERIVE_INVALID_MOTION_VECTORS_FUNCTION(i, j, k) derive_motion_vector(i, j, k)

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D depth_buffer;
layout(rg16f, set = 0, binding = 1) uniform restrict writeonly image2D velocity_buffer;

layout(push_constant, std430) uniform Params {
	highp mat4 reprojection_matrix;
	vec2 resolution;
	uint pad[2];
}
params;

void main() {
	
	if (any(greaterThanEqual(vec2(gl_GlobalInvocationID.xy), params.resolution))) {
		return;
	}

	ivec2 pos = ivec2(gl_GlobalInvocationID.xy);

	float depth = texelFetch(depth_buffer, pos, 0).x;
	vec2 uv = (vec2(pos) + 0.5f) / params.resolution;
	vec2 velocity = derive_motion_vector(uv, depth, params.reprojection_matrix);
	imageStore(velocity_buffer, pos, vec4(velocity, 0.0f, 0.0f));
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "MotionVectorsStoreShaderRD");
	}
};
