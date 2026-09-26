/**************************************************************************/
/*  octmap_roughness.glsl.gen.h                                           */
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

class OctmapRoughnessShaderRD : public ShaderRD {
public:
	OctmapRoughnessShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#define GROUP_SIZE 8

layout(local_size_x = GROUP_SIZE, local_size_y = GROUP_SIZE, local_size_z = 1) in;

shared vec4 samples[GROUP_SIZE * GROUP_SIZE * 4]; 

layout(set = 0, binding = 0) uniform sampler2D source_oct;

layout(OCTMAP_FORMAT, set = 1, binding = 0) uniform restrict writeonly image2D dest_octmap;


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
#define M_PI 3.14159265359

layout(push_constant, std430) uniform Params {
	uint sample_count;
	float roughness;
	uint source_size;
	uint dest_size;

	vec2 border_size;
	bool use_direct_write;
	uint pad;
}
params;

vec3 ImportanceSampleGGX(vec2 xi, float roughness4) {
	
	float Phi = 2.0 * M_PI * xi.x;
	float CosTheta = sqrt((1.0 - xi.y) / (1.0 + (roughness4 - 1.0) * xi.y));
	float SinTheta = sqrt(1.0 - CosTheta * CosTheta);

	
	vec3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	return H;
}

float DistributionGGX(float NdotH, float roughness4) {
	float NdotH2 = NdotH * NdotH;
	float denom = (NdotH2 * (roughness4 - 1.0) + 1.0);
	denom = M_PI * denom * denom;

	return roughness4 / denom;
}

float radicalInverse_VdC(uint bits) {
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10; 
}

vec2 Hammersley(uint i, uint N) {
	return vec2(float(i) / float(N), radicalInverse_VdC(i));
}

void main() {
	uvec2 id = gl_GlobalInvocationID.xy;
	vec2 inv_source_size = 1.0 / vec2(params.source_size);
	vec2 inv_dest_size = 1.0 / vec2(params.dest_size);
	vec2 uv = (vec2(id.xy) + 0.5) * inv_dest_size;
	if (params.use_direct_write) {
		if (id.x < params.dest_size && id.y < params.dest_size) {
			imageStore(dest_octmap, ivec2(id), vec4(texture(source_oct, uv).rgb, 1.0));
		}
	} else {
		float solid_angle_texel = 4.0 * M_PI / float(params.dest_size * params.dest_size);
		float roughness2 = params.roughness * params.roughness;
		float roughness4 = roughness2 * roughness2;

		uint scaled_samples = max(uint(float(params.sample_count * 4) * params.roughness), 4);

		
		uint samples_per_thread = max(1, ((scaled_samples) / (GROUP_SIZE * GROUP_SIZE)));
		uint total_samples = samples_per_thread * (GROUP_SIZE * GROUP_SIZE);

		for (uint local_sample = 0; local_sample < samples_per_thread; local_sample++) {
			uint sample_idx = local_sample * (GROUP_SIZE * GROUP_SIZE) + gl_LocalInvocationIndex;
			vec2 xi = Hammersley(sample_idx, total_samples);
			vec3 H_local = ImportanceSampleGGX(xi, roughness4);
			float NdotH = H_local.z;
			vec3 L_local = 2.0 * NdotH * H_local - vec3(0.0, 0.0, 1.0);

			float ndotl = L_local.z;
			if (ndotl > 0.0) {
				float D = DistributionGGX(NdotH, roughness4);
				float pdf = D * NdotH / (4.0 * NdotH) + 0.0001;

				float solid_angle_sample = 1.0 / (float(total_samples) * pdf + 0.0001);

				float mipLevel = 0.5 * log2(solid_angle_sample / solid_angle_texel);
				samples[sample_idx] = vec4(L_local, mipLevel);
			} else {
				samples[sample_idx] = vec4(-1.0);
			}
		}

		memoryBarrierShared();
		barrier();

		if (id.x < params.dest_size && id.y < params.dest_size) {
			vec3 N = oct_to_vec3_with_border(uv, params.border_size.y);
			vec4 sum = vec4(0.0, 0.0, 0.0, 0.0);
			vec3 UpVector = abs(N.y) < 0.99999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
			mat3 T;
			T[0] = normalize(cross(UpVector, N));
			T[1] = cross(N, T[0]);
			T[2] = N;

			for (uint i = 0; i < total_samples; i++) {
				vec4 s = samples[i];
				float ndotl = s.z;
				if (ndotl > 0.0) {
					vec3 L_world = T * s.xyz;
					vec2 sample_uv = vec3_to_oct_with_border(L_world, params.border_size);
					sum.rgb += textureLod(source_oct, sample_uv, s.w).rgb * ndotl;
					sum.a += ndotl;
				}
			}

			imageStore(dest_octmap, ivec2(id), vec4(sum.rgb / sum.a, 1.0));
		}
	}
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "OctmapRoughnessShaderRD");
	}
};
