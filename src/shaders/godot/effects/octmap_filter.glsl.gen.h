/**************************************************************************/
/*  octmap_filter.glsl.gen.h                                              */
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

class OctmapFilterShaderRD : public ShaderRD {
public:
	OctmapFilterShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#define GROUP_SIZE 64


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

layout(local_size_x = GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D source_octmap;
layout(OCTMAP_FORMAT, set = 2, binding = 0) uniform restrict writeonly image2D dest_octmap0;
layout(OCTMAP_FORMAT, set = 2, binding = 1) uniform restrict writeonly image2D dest_octmap1;
layout(OCTMAP_FORMAT, set = 2, binding = 2) uniform restrict writeonly image2D dest_octmap2;
layout(OCTMAP_FORMAT, set = 2, binding = 3) uniform restrict writeonly image2D dest_octmap3;
layout(OCTMAP_FORMAT, set = 2, binding = 4) uniform restrict writeonly image2D dest_octmap4;
layout(OCTMAP_FORMAT, set = 2, binding = 5) uniform restrict writeonly image2D dest_octmap5;

#ifdef USE_HIGH_QUALITY
#define NUM_TAPS 32
#else
#define NUM_TAPS 8
#endif

layout(push_constant, std430) uniform Params {
	vec2 border_size;
	vec2 pad;
}
params;

#define BASE_RESOLUTION 320

#ifdef USE_HIGH_QUALITY
layout(set = 1, binding = 0, std430) buffer restrict readonly Data {
	vec4[7][5][3][24] coeffs;
}
data;
#else
layout(set = 1, binding = 0, std430) buffer restrict readonly Data {
	vec4[7][5][6] coeffs;
}
data;
#endif

void main() {
	
	uvec2 id = gl_GlobalInvocationID.xy;
	uint mip_level = 0;
#ifndef USE_TEXTURE_ARRAY
	uint res = BASE_RESOLUTION;
	while ((id.x >= (res * res)) && (res > 1)) {
		id.x -= res * res;
		res = res >> 1;
		mip_level++;
	}
#else 
	uint res = BASE_RESOLUTION;
	mip_level = id.x / (BASE_RESOLUTION * BASE_RESOLUTION);
	id.x -= mip_level * BASE_RESOLUTION * BASE_RESOLUTION;
#endif
	
	id.y = id.x / res;
	id.x -= id.y * res;

	vec2 inv_res = 1.0 / vec2(res);
	vec3 dir = oct_to_vec3_with_border((vec2(id.xy) + vec2(0.5)) * inv_res, params.border_size.y);
	vec3 adir = abs(dir);
	vec3 frameZ = dir;

	
	vec4 color = vec4(0.0);
	for (int axis = 0; axis < 3; axis++) {
		const int otherAxis0 = 1 - (axis & 1) - (axis >> 1);
		const int otherAxis1 = 2 - (axis >> 1);
		const float lowerBound = 0.57735; 
		float frameweight = (max(adir[otherAxis0], adir[otherAxis1]) - lowerBound) / (1.0 - lowerBound);
		if (frameweight > 0.0) {
			
			vec3 UpVector;
			switch (axis) {
				case 0:
					UpVector = vec3(1, 0, 0);
					break;
				case 1:
					UpVector = vec3(0, 1, 0);
					break;
				default:
					UpVector = vec3(0, 0, 1);
					break;
			}

			vec3 frameX = normalize(cross(UpVector, frameZ));
			vec3 frameY = cross(frameZ, frameX);

			
			float Nx = dir[otherAxis0];
			float Ny = dir[otherAxis1];
			float Nz = adir[axis];

			float NmaxXY = max(abs(Ny), abs(Nx));
			Nx /= NmaxXY;
			Ny /= NmaxXY;

			float theta;
			if (Ny < Nx) {
				if (Ny <= -0.999) {
					theta = Nx;
				} else {
					theta = Ny;
				}
			} else {
				if (Ny >= 0.999) {
					theta = -Nx;
				} else {
					theta = -Ny;
				}
			}

			float phi;
			if (Nz <= -0.999) {
				phi = -NmaxXY;
			} else if (Nz >= 0.999) {
				phi = NmaxXY;
			} else {
				phi = Nz;
			}

			float theta2 = theta * theta;
			float phi2 = phi * phi;

			
			uint coeff_mip_level = min(mip_level, 5);
			for (int iSuperTap = 0; iSuperTap < NUM_TAPS / 4; iSuperTap++) {
				const int index = (NUM_TAPS / 4) * axis + iSuperTap;

#ifdef USE_HIGH_QUALITY
				vec4 coeffsDir0[3];
				vec4 coeffsDir1[3];
				vec4 coeffsDir2[3];
				vec4 coeffsLevel[3];
				vec4 coeffsWeight[3];

				for (int iCoeff = 0; iCoeff < 3; iCoeff++) {
					coeffsDir0[iCoeff] = data.coeffs[coeff_mip_level][0][iCoeff][index];
					coeffsDir1[iCoeff] = data.coeffs[coeff_mip_level][1][iCoeff][index];
					coeffsDir2[iCoeff] = data.coeffs[coeff_mip_level][2][iCoeff][index];
					coeffsLevel[iCoeff] = data.coeffs[coeff_mip_level][3][iCoeff][index];
					coeffsWeight[iCoeff] = data.coeffs[coeff_mip_level][4][iCoeff][index];
				}

				for (int iSubTap = 0; iSubTap < 4; iSubTap++) {
					
					vec3 sample_dir = frameX * (coeffsDir0[0][iSubTap] + coeffsDir0[1][iSubTap] * theta2 + coeffsDir0[2][iSubTap] * phi2) + frameY * (coeffsDir1[0][iSubTap] + coeffsDir1[1][iSubTap] * theta2 + coeffsDir1[2][iSubTap] * phi2) + frameZ * (coeffsDir2[0][iSubTap] + coeffsDir2[1][iSubTap] * theta2 + coeffsDir2[2][iSubTap] * phi2);

					float sample_level = coeffsLevel[0][iSubTap] + coeffsLevel[1][iSubTap] * theta2 + coeffsLevel[2][iSubTap] * phi2;

					float sample_weight = coeffsWeight[0][iSubTap] + coeffsWeight[1][iSubTap] * theta2 + coeffsWeight[2][iSubTap] * phi2;
#else
				vec4 coeffsDir0 = data.coeffs[coeff_mip_level][0][index];
				vec4 coeffsDir1 = data.coeffs[coeff_mip_level][1][index];
				vec4 coeffsDir2 = data.coeffs[coeff_mip_level][2][index];
				vec4 coeffsLevel = data.coeffs[coeff_mip_level][3][index];
				vec4 coeffsWeight = data.coeffs[coeff_mip_level][4][index];

				for (int iSubTap = 0; iSubTap < 4; iSubTap++) {
					
					vec3 sample_dir = frameX * coeffsDir0[iSubTap] + frameY * coeffsDir1[iSubTap] + frameZ * coeffsDir2[iSubTap];

					float sample_level = coeffsLevel[iSubTap];

					float sample_weight = coeffsWeight[iSubTap];
#endif

					sample_weight *= frameweight;

#ifdef USE_HIGH_QUALITY
					
					sample_dir /= max(abs(sample_dir[0]), max(abs(sample_dir[1]), abs(sample_dir[2])));
					sample_level += 0.75 * log2(dot(sample_dir, sample_dir));
#endif

#ifndef USE_TEXTURE_ARRAY
					sample_level += float(mip_level) / 5.0; 
#endif
					
					vec2 sample_uv = vec3_to_oct_with_border(normalize(sample_dir), params.border_size);
					color.rgb += textureLod(source_octmap, sample_uv, sample_level).rgb * sample_weight;
					color.a += sample_weight;
				}
			}
		}
	}

	
	color = vec4(max(vec3(0.0), color.rgb / color.a), 1.0);

#ifdef USE_TEXTURE_ARRAY
	id.xy *= uvec2(2, 2);
#endif

	if (mip_level > 5) {
		return;
	}

#ifdef USE_TEXTURE_ARRAY
#define IMAGE_STORE(x)                             \
	imageStore(x, ivec2(id), color);               \
	imageStore(x, ivec2(id) + ivec2(1, 0), color); \
	imageStore(x, ivec2(id) + ivec2(0, 1), color); \
	imageStore(x, ivec2(id) + ivec2(1, 1), color)
#else
#define IMAGE_STORE(x) imageStore(x, ivec2(id), color)
#endif

	switch (mip_level) {
		case 0:
			IMAGE_STORE(dest_octmap0);
			break;
		case 1:
			IMAGE_STORE(dest_octmap1);
			break;
		case 2:
			IMAGE_STORE(dest_octmap2);
			break;
		case 3:
			IMAGE_STORE(dest_octmap3);
			break;
		case 4:
			IMAGE_STORE(dest_octmap4);
			break;
		case 5:
		default:
			IMAGE_STORE(dest_octmap5);
			break;
	}
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "OctmapFilterShaderRD");
	}
};
