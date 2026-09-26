/**************************************************************************/
/*  octmap_filter_raster.glsl.gen.h                                       */
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

class OctmapFilterRasterShaderRD : public ShaderRD {
public:
	OctmapFilterRasterShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(push_constant, std430) uniform Params {
	vec2 border_size;
	int mip_level;
	int pad;
}
params;

layout(location = 0) out vec2 uv_interp;
/* clang-format on */

void main() {
	vec2 base_arr[3] = vec2[](vec2(-1.0, -3.0), vec2(-1.0, 1.0), vec2(3.0, 1.0));
	uv_interp = base_arr[gl_VertexIndex];
	gl_Position = vec4(uv_interp, 0.0, 1.0);
}

/* clang-format off */
)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES


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

layout(push_constant, std430) uniform Params {
	vec2 border_size;
	int mip_level;
	int pad;
}
params;

layout(set = 0, binding = 0) uniform sampler2D source_octmap;

layout(location = 0) in vec2 uv_interp;
layout(location = 0) out vec4 frag_color;

/* clang-format on */

#ifdef USE_HIGH_QUALITY
#define NUM_TAPS 32
#else
#define NUM_TAPS 8
#endif

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
	
	int mip_level = 0;
	vec2 uv = uv_interp * 0.5 + 0.5;
	if (params.mip_level < 0) {
		
		frag_color.rgb = textureLod(source_octmap, uv, 0.0).rgb;
		frag_color.a = 1.0;
		return;
	} else if (params.mip_level > 5) {
		
		mip_level = 5;
	} else {
		
		mip_level = params.mip_level;
	}

	
	vec3 dir = oct_to_vec3_with_border(uv, params.border_size.y);
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

			
			for (int iSuperTap = 0; iSuperTap < NUM_TAPS / 4; iSuperTap++) {
				const int index = (NUM_TAPS / 4) * axis + iSuperTap;

#ifdef USE_HIGH_QUALITY
				vec4 coeffsDir0[3];
				vec4 coeffsDir1[3];
				vec4 coeffsDir2[3];
				vec4 coeffsLevel[3];
				vec4 coeffsWeight[3];

				for (int iCoeff = 0; iCoeff < 3; iCoeff++) {
					coeffsDir0[iCoeff] = data.coeffs[mip_level][0][iCoeff][index];
					coeffsDir1[iCoeff] = data.coeffs[mip_level][1][iCoeff][index];
					coeffsDir2[iCoeff] = data.coeffs[mip_level][2][iCoeff][index];
					coeffsLevel[iCoeff] = data.coeffs[mip_level][3][iCoeff][index];
					coeffsWeight[iCoeff] = data.coeffs[mip_level][4][iCoeff][index];
				}

				for (int iSubTap = 0; iSubTap < 4; iSubTap++) {
					
					vec3 sample_dir = frameX * (coeffsDir0[0][iSubTap] + coeffsDir0[1][iSubTap] * theta2 + coeffsDir0[2][iSubTap] * phi2) + frameY * (coeffsDir1[0][iSubTap] + coeffsDir1[1][iSubTap] * theta2 + coeffsDir1[2][iSubTap] * phi2) + frameZ * (coeffsDir2[0][iSubTap] + coeffsDir2[1][iSubTap] * theta2 + coeffsDir2[2][iSubTap] * phi2);

					float sample_level = coeffsLevel[0][iSubTap] + coeffsLevel[1][iSubTap] * theta2 + coeffsLevel[2][iSubTap] * phi2;

					float sample_weight = coeffsWeight[0][iSubTap] + coeffsWeight[1][iSubTap] * theta2 + coeffsWeight[2][iSubTap] * phi2;
#else
				vec4 coeffsDir0 = data.coeffs[mip_level][0][index];
				vec4 coeffsDir1 = data.coeffs[mip_level][1][index];
				vec4 coeffsDir2 = data.coeffs[mip_level][2][index];
				vec4 coeffsLevel = data.coeffs[mip_level][3][index];
				vec4 coeffsWeight = data.coeffs[mip_level][4][index];

				for (int iSubTap = 0; iSubTap < 4; iSubTap++) {
					
					vec3 sample_dir = frameX * coeffsDir0[iSubTap] + frameY * coeffsDir1[iSubTap] + frameZ * coeffsDir2[iSubTap];

					float sample_level = coeffsLevel[iSubTap];

					float sample_weight = coeffsWeight[iSubTap];
#endif

					sample_weight *= frameweight;

					
					vec2 sample_uv = vec3_to_oct_with_border(normalize(sample_dir), params.border_size);
					color.rgb += textureLod(source_octmap, sample_uv, sample_level).rgb * sample_weight;
					color.a += sample_weight;
				}
			}
		}
	}

	
	frag_color = vec4(max(vec3(0.0), color.rgb / color.a), 1.0);
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "OctmapFilterRasterShaderRD");
	}
};
