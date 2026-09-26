/**************************************************************************/
/*  octmap_downsampler.glsl.gen.h                                         */
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

class OctmapDownsamplerShaderRD : public ShaderRD {
public:
	OctmapDownsamplerShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#define BLOCK_SIZE 8


vec3 oct_to_vec3(vec2 e) {
	vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-v.z, 0.0);
	v.xy += t * -sign(v.xy);
	return normalize(v);
}


vec3 oct_to_vec3_with_border(vec2 uv, float border_size) {
	
	uv = (uv - 0.5) * (2.0 / border_size);
	
	
	vec2 mask = step(vec2(1.0), abs(uv));
	uv = 2.0 * clamp(uv, -1.0, 1.0) - uv;
	uv = mix(uv, -uv, mask.yx);
	return oct_to_vec3(uv);
}

vec2 oct_wrap(vec2 v) {
	vec2 signVal;
	signVal.x = v.x >= 0.0 ? 1.0 : -1.0;
	signVal.y = v.y >= 0.0 ? 1.0 : -1.0;
	return (1.0 - abs(v.yx)) * signVal;
}

vec2 vec3_to_oct(vec3 n) {
	
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = (n.z >= 0.0) ? n.xy : oct_wrap(n.xy);
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}



vec2 vec3_to_oct_with_border(vec3 n, vec2 border_size) {
	vec2 uv = vec3_to_oct(n);
	return uv * border_size.y + border_size.x;
}

float vec3_to_oct_lod(vec3 n_ddx, vec3 n_ddy, float pixel_size) {
	
	
	float pixel_size_sqr = 4.0 * pixel_size * pixel_size;
	float ddx = dot(n_ddx, n_ddx) / pixel_size_sqr;
	float ddy = dot(n_ddy, n_ddy) / pixel_size_sqr;
	float dd_sqr = max(ddx, ddy);
	return 0.25 * log2(dd_sqr + 1e-6f);
}

layout(local_size_x = BLOCK_SIZE, local_size_y = BLOCK_SIZE, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_octmap;

layout(OCTMAP_FORMAT, set = 1, binding = 0) uniform restrict writeonly image2D dest_octmap;

layout(push_constant, std430) uniform Params {
	float border_size;
	uint size;
	uint pad1;
	uint pad2;
}
params;


float calcWeight(float u, float v) {
	vec3 d = oct_to_vec3_with_border(vec2(u, v) * 0.5 + 0.5, params.border_size);
	return 1.0 / pow(abs(d.z) + 1.0, 3.0);
}

void main() {
	uvec2 id = gl_GlobalInvocationID.xy;
	if (id.x < params.size && id.y < params.size) {
		float inv_size = 1.0 / float(params.size);
		uvec2 sample_id = id;

		float u0 = (float(sample_id.x) * 2.0f + 1.0f - 0.75f) * inv_size - 1.0f;
		float u1 = (float(sample_id.x) * 2.0f + 1.0f + 0.75f) * inv_size - 1.0f;
		float v0 = (float(sample_id.y) * 2.0f + 1.0f - 0.75f) * inv_size - 1.0f;
		float v1 = (float(sample_id.y) * 2.0f + 1.0f + 0.75f) * inv_size - 1.0f;
		float weights[4];
		weights[0] = calcWeight(u0, v0);
		weights[1] = calcWeight(u1, v0);
		weights[2] = calcWeight(u0, v1);
		weights[3] = calcWeight(u1, v1);

		const float wsum = 0.5 / (weights[0] + weights[1] + weights[2] + weights[3]);
		for (int i = 0; i < 4; i++) {
			weights[i] = weights[i] * wsum + 0.125;
		}

		vec4 color = textureLod(source_octmap, vec2(u0, v0) * 0.5f + 0.5f, 0.0) * weights[0];
		color += textureLod(source_octmap, vec2(u1, v0) * 0.5f + 0.5f, 0.0) * weights[1];
		color += textureLod(source_octmap, vec2(u0, v1) * 0.5f + 0.5f, 0.0) * weights[2];
		color += textureLod(source_octmap, vec2(u1, v1) * 0.5f + 0.5f, 0.0) * weights[3];
		imageStore(dest_octmap, ivec2(id), color);
	}
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "OctmapDownsamplerShaderRD");
	}
};
