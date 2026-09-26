/**************************************************************************/
/*  sdfgi_direct_light.glsl.gen.h                                         */
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

class SdfgiDirectLightShaderRD : public ShaderRD {
public:
	SdfgiDirectLightShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
(R"<!>(
#version 450

#VERSION_DEFINES

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#define MAX_CASCADES 8

layout(set = 0, binding = 1) uniform texture3D sdf_cascades[MAX_CASCADES];
layout(set = 0, binding = 2) uniform sampler linear_sampler;
layout(set = 0, binding = 3) uniform sampler linear_sampler_with_mipmaps;

layout(set = 0, binding = 4, std430) restrict readonly buffer DispatchData {
	uint x;
	uint y;
	uint z;
	uint total_count;
}
dispatch_data;

struct ProcessVoxel {
	uint position; 
	uint albedo; 
	uint light; 
	uint light_aniso; 
	
};

#ifdef MODE_PROCESS_STATIC
layout(set = 0, binding = 5, std430) restrict buffer ProcessVoxels {
#else
layout(set = 0, binding = 5, std430) restrict buffer readonly ProcessVoxels {
#endif
	ProcessVoxel data[];
}
process_voxels;

layout(r32ui, set = 0, binding = 6) uniform restrict uimage3D dst_light;
layout(rgba8, set = 0, binding = 7) uniform restrict image3D dst_aniso0;
layout(rg8, set = 0, binding = 8) uniform restrict image3D dst_aniso1;

struct CascadeData {
	vec3 offset; 
	float to_cell; 
	ivec3 probe_world_offset;
	uint pad;
	vec4 pad2;
};

layout(set = 0, binding = 9, std140) uniform Cascades {
	CascadeData data[MAX_CASCADES];
}
cascades;

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_OMNI 1
#define LIGHT_TYPE_SPOT 2
#define LIGHT_TYPE_AREA 3



#define M_PI 3.14159265359
#define M_TAU 6.28318530718

float acos_approx(float p_x) {
	float x = abs(p_x);
	float res = -0.156583f * x + (M_PI / 2.0);
	res *= sqrt(1.0f - x);
	return (p_x >= 0) ? res : M_PI - res;
}

vec3 fetch_ltc_lod(vec2 uv, vec4 texture_rect, float lod, float max_mipmap, texture2D area_light_atlas, sampler texture_sampler) {
	float low = min(max(floor(lod), 0.0), max_mipmap - 1.0);
	float high = min(max(floor(lod + 1.0), 1.0), max_mipmap);
	vec2 sample_pos = texture_rect.xy + clamp(uv, 0.0, 1.0) * texture_rect.zw; 
	vec4 sample_col_low = textureLod(sampler2D(area_light_atlas, texture_sampler), sample_pos, low);
	vec4 sample_col_high = textureLod(sampler2D(area_light_atlas, texture_sampler), sample_pos, high);

	float blend = high - clamp(lod, high - 1.0, high);
	vec4 sample_col = mix(sample_col_high, sample_col_low, blend);
	return sample_col.rgb * sample_col.a; 
}

vec3 integrate_edge_hill(vec3 p0, vec3 p1) {
	
	
	float cosTheta = dot(p0, p1);

	float x = cosTheta;
	float y = abs(x);
	float a = 5.42031 + (3.12829 + 0.0902326 * y) * y;
	float b = 3.45068 + (4.18814 + y) * y;
	float theta_sintheta = a / b;

	if (x < 0.0) {
		theta_sintheta = M_PI * inversesqrt(1.0 - x * x) - theta_sintheta; 
	}
	return theta_sintheta * cross(p0, p1);
}

float integrate_edge(vec3 p_proj0, vec3 p_proj1, vec3 p0, vec3 p1) {
	float epsilon = 0.00001;
	bool opposite_sides = dot(p_proj0, p_proj1) < -1.0 + epsilon;
	if (opposite_sides) {
		
		vec3 half_point_t = p0 + normalize(p1 - p0) * dot(p0, normalize(p0 - p1));
		vec3 half_point = normalize(half_point_t);

		
		float theta_sintheta = 0.0;
		{
			float cosTheta = dot(p_proj0, half_point);

			float x = cosTheta;
			float y = abs(x);
			float a = 5.42031 + (3.12829 + 0.0902326 * y) * y;
			float b = 3.45068 + (4.18814 + y) * y;
			theta_sintheta = a / b;
			if (x < 0.0) {
				theta_sintheta = M_PI * inversesqrt(1.0 - x * x) - theta_sintheta; 
			}
		}
		float aa = (theta_sintheta * cross(p_proj0, half_point)).y;
		{
			float cosTheta = dot(half_point, p_proj1);

			float x = cosTheta;
			float y = abs(x);
			float a = 5.42031 + (3.12829 + 0.0902326 * y) * y;
			float b = 3.45068 + (4.18814 + y) * y;
			theta_sintheta = a / b;
			if (x < 0.0) {
				theta_sintheta = M_PI * inversesqrt(1.0 - x * x) - theta_sintheta; 
			}
		}
		float bb = (theta_sintheta * cross(half_point, p_proj1)).y;
		return aa + bb;
	} else {
		
		float cosTheta = dot(p_proj0, p_proj1);

		float x = cosTheta;
		float y = abs(x);
		float a = 5.42031 + (3.12829 + 0.0902326 * y) * y;
		float b = 3.45068 + (4.18814 + y) * y;
		float theta_sintheta = a / b;

		if (x < 0.0) {
			theta_sintheta = M_PI * inversesqrt(1.0 - x * x) - theta_sintheta; 
		}
		return (theta_sintheta * cross(p_proj0, p_proj1)).y;
	}
}

vec3 fetch_ltc_filtered_texture_with_form_factor(vec4 texture_rect, vec3 L[4], float max_mipmap, texture2D area_light_atlas, sampler texture_sampler) {
	vec3 L0 = normalize(L[0]);
	vec3 L1 = normalize(L[1]);
	vec3 L2 = normalize(L[2]);
	vec3 L3 = normalize(L[3]);

	vec3 F = vec3(0.0); 
	F += integrate_edge_hill(L0, L1);
	F += integrate_edge_hill(L1, L2);
	F += integrate_edge_hill(L2, L3);
	F += integrate_edge_hill(L3, L0);

	vec2 uv;
	float lod = 0.0;

	if (dot(F, F) < 1e-16) {
		uv = vec2(0.5);
		lod = max_mipmap;
	} else {
		vec3 lx = L[1] - L[0];
		vec3 ly = L[3] - L[0];
		vec3 ln = cross(lx, ly);

		float dist_x_area = dot(L[0], ln);
		float d = dist_x_area / dot(F, ln);
		vec3 isec = d * F;
		vec3 li = isec - L[0]; 

		float dot_lxy = dot(lx, ly);
		float inv_dot_lxlx = 1.0 / dot(lx, lx);
		vec3 ly_ = vec3(ly - lx * dot_lxy * inv_dot_lxlx); 
		uv.y = dot(vec3(li), ly_) / dot(ly_, ly_);
		uv.x = dot(vec3(li), lx) * inv_dot_lxlx - dot_lxy * inv_dot_lxlx * uv.y;

		lod = abs(dist_x_area) / pow(dot(ln, ln), 0.75);
		lod = log(2048.0 * lod) / log(3.0);
	}
	return fetch_ltc_lod(vec2(1.0) - uv, texture_rect, lod, max_mipmap, area_light_atlas, texture_sampler);
}


float quad_solid_angle(vec3 L[4]) {
	
	
	vec3 c1 = cross(L[0], L[1]);
	vec3 c2 = cross(L[1], L[2]);
	vec3 c3 = cross(L[2], L[3]);
	vec3 c4 = cross(L[3], L[0]);
	vec3 n0 = normalize(c1);
	vec3 n1 = normalize(c2);
	vec3 n2 = normalize(c3);
	vec3 n3 = normalize(c4);
	float g0 = acos(clamp(dot(-n0, n1), -1.0, 1.0));
	float g1 = acos(clamp(dot(-n1, n2), -1.0, 1.0));
	float g2 = acos(clamp(dot(-n2, n3), -1.0, 1.0));
	float g3 = acos(clamp(dot(-n3, n0), -1.0, 1.0));

	float angle_sum = g0 + g1 + g2 + g3;

	return clamp(angle_sum - M_TAU, 0.0, M_TAU);
}

void clip_quad_to_horizon(inout vec3 L[5], out int vertex_count) {
	
	int config = 0;
	if (L[0].y > 0.0) {
		config += 1;
	}
	if (L[1].y > 0.0) {
		config += 2;
	}
	if (L[2].y > 0.0) {
		config += 4;
	}
	if (L[3].y > 0.0) {
		config += 8;
	}

	
	vertex_count = 0;

	if (config == 0) {
		
	} else if (config == 1) { 
		vertex_count = 3;
		L[1] = -L[1].y * L[0] + L[0].y * L[1];
		L[2] = -L[3].y * L[0] + L[0].y * L[3];
	} else if (config == 2) { 
		vertex_count = 3;
		L[0] = -L[0].y * L[1] + L[1].y * L[0];
		L[2] = -L[2].y * L[1] + L[1].y * L[2];
	} else if (config == 3) { 
		vertex_count = 4;
		L[2] = -L[2].y * L[1] + L[1].y * L[2];
		L[3] = -L[3].y * L[0] + L[0].y * L[3];
	} else if (config == 4) { 
		vertex_count = 3;
		L[0] = -L[3].y * L[2] + L[2].y * L[3];
		L[1] = -L[1].y * L[2] + L[2].y * L[1];
	} else if (config == 5) { 
		vertex_count = 0;
	} else if (config == 6) { 
		vertex_count = 4;
		L[0] = -L[0].y * L[1] + L[1].y * L[0];
		L[3] = -L[3].y * L[2] + L[2].y * L[3];
	} else if (config == 7) { 
		vertex_count = 5;
		L[4] = -L[3].y * L[0] + L[0].y * L[3];
		L[3] = -L[3].y * L[2] + L[2].y * L[3];
	} else if (config == 8) { 
		vertex_count = 3;
		L[0] = -L[0].y * L[3] + L[3].y * L[0];
		L[1] = -L[2].y * L[3] + L[3].y * L[2];
		L[2] = L[3];
	} else if (config == 9) { 
		vertex_count = 4;
		L[1] = -L[1].y * L[0] + L[0].y * L[1];
		L[2] = -L[2].y * L[3] + L[3].y * L[2];
	} else if (config == 10) { 
		vertex_count = 0;
	} else if (config == 11) { 
		vertex_count = 5;
		L[4] = L[3];
		L[3] = -L[2].y * L[3] + L[3].y * L[2];
		L[2] = -L[2].y * L[1] + L[1].y * L[2];
	} else if (config == 12) { 
		vertex_count = 4;
		L[1] = -L[1].y * L[2] + L[2].y * L[1];
		L[0] = -L[0].y * L[3] + L[3].y * L[0];
	} else if (config == 13) { 
		vertex_count = 5;
		L[4] = L[3];
		L[3] = L[2];
		L[2] = -L[1].y * L[2] + L[2].y * L[1];
		L[1] = -L[1].y * L[0] + L[0].y * L[1];
	} else if (config == 14) { 
		vertex_count = 5;
		L[4] = -L[0].y * L[3] + L[3].y * L[0];
		L[0] = -L[0].y * L[1] + L[1].y * L[0];
	} else if (config == 15) { 
		vertex_count = 4;
	}

	if (vertex_count == 3) {
		L[3] = L[0];
	}
	if (vertex_count == 4) {
		L[4] = L[0];
	}
}

float ltc_integrate_clipped_quad(vec3 L[5], vec3 L_proj[5], int vertices_above_horizon) {
	float I;
	I = integrate_edge(L_proj[0], L_proj[1], L[0], L[1]);
	I += integrate_edge(L_proj[1], L_proj[2], L[1], L[2]);
	I += integrate_edge(L_proj[2], L_proj[3], L[2], L[3]);
	if (vertices_above_horizon >= 4) {
		I += integrate_edge(L_proj[3], L_proj[4], L[3], L[4]);
	}
	if (vertices_above_horizon == 5) {
		I += integrate_edge(L_proj[4], L_proj[0], L[4], L[0]);
	}
	return abs(I);
}

void ltc_evaluate(vec3 normal, vec3 eye_vec, mat3 M_inv, vec3 points[4], vec4 texture_rect, float max_mipmap, texture2D area_light_atlas, sampler texture_sampler, out float integral, out vec3 tex_color) {
	
	tex_color = vec3(1.0);
	
	vec3 x, z;
	z = -normalize(eye_vec - normal * dot(eye_vec, normal)); 
	x = cross(normal, z);

	
	M_inv = M_inv * transpose(mat3(x, normal, z));

	vec3 L[5];
	L[0] = M_inv * points[0];
	L[1] = M_inv * points[1];
	L[2] = M_inv * points[2];
	L[3] = M_inv * points[3];

	vec3 L_unclipped[4];
	L_unclipped[0] = L[0];
	L_unclipped[1] = L[1];
	L_unclipped[2] = L[2];
	L_unclipped[3] = L[3];

	int n;
	clip_quad_to_horizon(L, n);
	if (n == 0) {
		integral = 0.0;
		return;
	}

	
	vec3 L_proj[5];
	L_proj[0] = normalize(L[0]);
	L_proj[1] = normalize(L[1]);
	L_proj[2] = normalize(L[2]);
	L_proj[3] = normalize(L[3]);
	L_proj[4] = normalize(L[4]);

	if (texture_rect != vec4(0.0)) {
		tex_color = vec3(fetch_ltc_filtered_texture_with_form_factor(texture_rect, L_unclipped, max_mipmap, area_light_atlas, texture_sampler));
	}

	
	vec3 pnorm = normalize(cross(L_proj[0] - L_proj[1], L_proj[2] - L_proj[1]));
	if (abs(dot(pnorm, L_proj[0])) < 1e-10) {
		
		
		vec3 r10 = points[0] - points[1];
		vec3 r12 = points[2] - points[1];
		float alpha = -dot(points[1], r10) / dot(r10, r10);
		float beta = -dot(points[1], r12) / dot(r12, r12);
		if (0.0 < alpha && alpha < 1.0 && 0.0 < beta && beta < 1.0) { 
			integral = 1.0;
			return;
		} else {
			integral = 0.0;
			return;
		}
	}

	float I = ltc_integrate_clipped_quad(L, L_proj, n);
	integral = I / (2.0 * M_PI);
}

void ltc_evaluate_specular(vec3 normal, vec3 eye_vec, float roughness, vec3 points[4], vec4 texture_rect, float max_mipmap, texture2D area_light_atlas, sampler texture_sampler, sampler2D ltc_lut1, sampler2D ltc_lut2, out float ltc_specular, out vec2 fresnel, out vec3 ltc_specular_tex_color) {
	float theta = acos_approx(dot(normal, eye_vec));
	const float LTC_LUT_SIZE = float(64.0);
	vec2 lut_pos = vec2(max(roughness, float(0.02)), theta / float(0.5 * M_PI));
	vec2 lut_uv = vec2(lut_pos * (float(63.0) / LTC_LUT_SIZE) + vec2(float(0.5) / LTC_LUT_SIZE)); 
	vec4 M_brdf_abcd = texture(ltc_lut1, lut_uv);
	vec3 M_brdf_e_mag_fres = texture(ltc_lut2, lut_uv).xyz;
	float scale = 1.0 / (M_brdf_abcd.x * M_brdf_e_mag_fres.x - M_brdf_abcd.y * M_brdf_abcd.w);

	mat3 M_inv = mat3(
			vec3(0, 0, 1.0 / M_brdf_abcd.z),
			vec3(-M_brdf_abcd.w * scale, M_brdf_abcd.x * scale, 0),
			vec3(-M_brdf_e_mag_fres.x * scale, M_brdf_abcd.y * scale, 0));

	ltc_evaluate(normal, eye_vec, M_inv, points, texture_rect, max_mipmap, area_light_atlas, texture_sampler, ltc_specular, ltc_specular_tex_color);
	fresnel = vec2(M_brdf_e_mag_fres.yz);
}

void ltc_evaluate_diff(vec3 normal, vec3 points[4], vec4 texture_rect, float max_mipmap, texture2D area_light_atlas, sampler texture_sampler, out float integral, out vec3 tex_color) {
	
	tex_color = vec3(1.0);
	
	vec3 x, z;
	vec3 eye_vec = abs(normal.z) < 0.7 ? vec3(0.0, 0.0, -1.0) : vec3(1.0, 0.0, 0.0);
	z = -normalize(eye_vec - normal * dot(eye_vec, normal)); 
	x = cross(normal, z);

	
	mat3 M_inv = transpose(mat3(x, normal, z));

	vec3 L[5];
	L[0] = M_inv * points[0];
	L[1] = M_inv * points[1];
	L[2] = M_inv * points[2];
	L[3] = M_inv * points[3];

	vec3 L_unclipped[4];
	L_unclipped[0] = L[0];
	L_unclipped[1] = L[1];
	L_unclipped[2] = L[2];
	L_unclipped[3] = L[3];

	int n;
	clip_quad_to_horizon(L, n);
	if (n == 0) {
		integral = 0.0;
		return;
	}

	
	vec3 L_proj[5];
	L_proj[0] = normalize(L[0]);
	L_proj[1] = normalize(L[1]);
	L_proj[2] = normalize(L[2]);
	L_proj[3] = normalize(L[3]);
	L_proj[4] = normalize(L[4]);

	
	vec3 pnorm = normalize(cross(L_proj[0] - L_proj[1], L_proj[2] - L_proj[1]));
	if (abs(dot(pnorm, L_proj[0])) < 1e-10) {
		
		
		integral = 0.0;
		return;
	}

	if (texture_rect != vec4(0.0)) {
		tex_color = fetch_ltc_filtered_texture_with_form_factor(texture_rect, L_unclipped, max_mipmap, area_light_atlas, texture_sampler);
	}

	float I = ltc_integrate_clipped_quad(L, L_proj, n);
	integral = I; 
}

struct Light {
	vec3 color;
	float energy;

	vec3 direction;
	bool has_shadow;

	vec3 position;
	float attenuation;

	uint type;
	float cos_spot_angle;
	float inv_spot_attenuation;
	float radius;

	vec4 area_width;
	vec4 area_height;
	vec4 area_projector_rect;
};

layout(set = 0, binding = 10, std140) buffer restrict readonly Lights {
	Light data[];
}
lights;

layout(set = 0, binding = 11) uniform texture2DArray lightprobe_texture;
layout(set = 0, binding = 12) uniform texture3D occlusion_texture;

layout(set = 1, binding = 0) uniform texture2D area_light_atlas;

layout(push_constant, std430) uniform Params {
	vec3 grid_size;
	uint max_cascades;

	uint cascade;
	uint light_count;
	uint process_offset;
	uint process_increment;

	int probe_axis_size;
	float bounce_feedback;
	float y_mult;
	bool use_occlusion;
}
params;

vec2 octahedron_wrap(vec2 v) {
	vec2 signVal;
	signVal.x = v.x >= 0.0 ? 1.0 : -1.0;
	signVal.y = v.y >= 0.0 ? 1.0 : -1.0;
	return (1.0 - abs(v.yx)) * signVal;
}

vec2 octahedron_encode(vec3 n) {
	
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = n.z >= 0.0 ? n.xy : octahedron_wrap(n.xy);
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}

float get_omni_attenuation(float distance, float inv_range, float decay) {
	float nd = distance * inv_range;
	nd *= nd;
	nd *= nd; 
	nd = max(1.0 - nd, 0.0);
	nd *= nd; 
	return nd * pow(max(distance, 0.0001), -decay);
}

void compute_area_light(uint index, vec3 position, out float attenuation, out vec3 light_vec, out float light_distance, out vec3 texture_color) {
	vec3 area_width = lights.data[index].area_width.xyz;
	vec3 area_height = lights.data[index].area_height.xyz;
	vec3 area_direction = lights.data[index].direction;
	float a_len = length(area_width);
	float b_len = length(area_height);
	vec3 area_width_norm = normalize(area_width);
	vec3 area_height_norm = normalize(area_height);
	float a_half_len = a_len / 2.0;
	float b_half_len = b_len / 2.0;
	vec3 light_center = lights.data[index].position;
	vec3 light_to_vert = position - light_center;
	vec3 pos_local_to_light = vec3(dot(light_to_vert, area_width_norm), dot(light_to_vert, area_height_norm), dot(light_to_vert, -area_direction)); 
	vec3 closest_point_local_to_light = vec3(clamp(pos_local_to_light.x, -a_half_len, a_half_len), clamp(pos_local_to_light.y, -b_half_len, b_half_len), 0.0); 
	float inv_center_range = lights.data[index].inv_spot_attenuation;
	vec3 closest_point_on_light = light_center + closest_point_local_to_light.x * area_width_norm + closest_point_local_to_light.y * area_height_norm; 
	vec3 light_rel_vec = closest_point_on_light - position;
	light_distance = length(light_rel_vec);
	light_vec = light_rel_vec / light_distance;
	float EPSILON = 1e-4f;
	if (light_distance < EPSILON) {
		light_vec = area_direction;
	}
	float max_mipmap = lights.data[index].cos_spot_angle;

	if (light_distance * inv_center_range >= 1.0) { 
		attenuation = 0.0;
		return;
	}

	vec3 h_area_width = area_width / 2.0;
	vec3 h_area_height = area_height / 2.0;
	vec3 light_points[4];
	light_points[0] = lights.data[index].position - h_area_width - h_area_height - position;
	light_points[1] = lights.data[index].position + h_area_width - h_area_height - position;
	light_points[2] = lights.data[index].position + h_area_width + h_area_height - position;
	light_points[3] = lights.data[index].position - h_area_width + h_area_height - position;

	attenuation = get_omni_attenuation(light_distance, 1.0 / lights.data[index].radius, lights.data[index].attenuation - 2.0);
	float ltc_diffuse = 0.0;
	vec3 normal = light_vec;
	ltc_evaluate_diff(normal, light_points, lights.data[index].area_projector_rect, max_mipmap, area_light_atlas, linear_sampler_with_mipmaps, ltc_diffuse, texture_color);
	attenuation *= ltc_diffuse;
}

void main() {
	uint voxel_index = uint(gl_GlobalInvocationID.x);
)<!>" R"<!>(
	
	if (params.process_increment > 1) {
		voxel_index *= params.process_increment;
		voxel_index += params.process_offset;
	}

	if (voxel_index >= dispatch_data.total_count) {
		return;
	}

	uint voxel_position = process_voxels.data[voxel_index].position;

	
	ivec3 positioni = ivec3((uvec3(voxel_position, voxel_position, voxel_position) >> uvec3(0, 7, 14)) & uvec3(0x7F));

	vec3 position = vec3(positioni) + vec3(0.5);
	position /= cascades.data[params.cascade].to_cell;
	position += cascades.data[params.cascade].offset;

	uint voxel_albedo = process_voxels.data[voxel_index].albedo;

	vec3 albedo = vec3(uvec3(voxel_albedo >> 10, voxel_albedo >> 5, voxel_albedo) & uvec3(0x1F)) / float(0x1F);
	vec3 light_accum[6] = vec3[](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
	uint valid_aniso = (voxel_albedo >> 15) & 0x3F;

	const vec3 aniso_dir[6] = vec3[](
			vec3(1, 0, 0),
			vec3(0, 1, 0),
			vec3(0, 0, 1),
			vec3(-1, 0, 0),
			vec3(0, -1, 0),
			vec3(0, 0, -1));

	
#ifdef MODE_PROCESS_DYNAMIC
	if (params.bounce_feedback > 0.001) {
		vec3 feedback = (params.bounce_feedback < 1.0) ? (albedo * params.bounce_feedback) : mix(albedo, vec3(1.0), params.bounce_feedback - 1.0);
		vec3 pos = (vec3(positioni) + vec3(0.5)) * float(params.probe_axis_size - 1) / params.grid_size;
		ivec3 probe_base_pos = ivec3(pos);

		float weight_accum[6] = float[](0, 0, 0, 0, 0, 0);

		ivec3 tex_pos = ivec3(probe_base_pos.xy, int(params.cascade));
		tex_pos.x += probe_base_pos.z * int(params.probe_axis_size);

		tex_pos.xy = tex_pos.xy * (OCT_SIZE + 2) + ivec2(1);

		vec3 base_tex_posf = vec3(tex_pos);
		vec2 tex_pixel_size = 1.0 / vec2(ivec2((OCT_SIZE + 2) * params.probe_axis_size * params.probe_axis_size, (OCT_SIZE + 2) * params.probe_axis_size));
		vec3 probe_uv_offset = vec3(ivec3(OCT_SIZE + 2, OCT_SIZE + 2, (OCT_SIZE + 2) * params.probe_axis_size)) * tex_pixel_size.xyx;

		for (uint j = 0; j < 8; j++) {
			ivec3 offset = (ivec3(j) >> ivec3(0, 1, 2)) & ivec3(1, 1, 1);
			ivec3 probe_posi = probe_base_pos;
			probe_posi += offset;

			

			vec3 probe_pos = vec3(probe_posi);
			vec3 probe_to_pos = pos - probe_pos;
			vec3 probe_dir = normalize(-probe_to_pos);

			

			vec3 trilinear = vec3(1.0) - abs(probe_to_pos);

			for (uint k = 0; k < 6; k++) {
				if (bool(valid_aniso & (1 << k))) {
					vec3 n = aniso_dir[k];
					float weight = trilinear.x * trilinear.y * trilinear.z * max(0, dot(n, probe_dir));

					if (weight > 0.0 && params.use_occlusion) {
						ivec3 occ_indexv = abs((cascades.data[params.cascade].probe_world_offset + probe_posi) & ivec3(1, 1, 1)) * ivec3(1, 2, 4);
						vec4 occ_mask = mix(vec4(0.0), vec4(1.0), equal(ivec4(occ_indexv.x | occ_indexv.y), ivec4(0, 1, 2, 3)));

						vec3 occ_pos = (vec3(positioni) + aniso_dir[k] + vec3(0.5)) / params.grid_size;
						occ_pos.z += float(params.cascade);
						if (occ_indexv.z != 0) { 
							occ_pos.x += 1.0;
						}
						occ_pos *= vec3(0.5, 1.0, 1.0 / float(params.max_cascades)); 
						float occlusion = dot(textureLod(sampler3D(occlusion_texture, linear_sampler), occ_pos, 0.0), occ_mask);

						weight *= occlusion;
					}

					if (weight > 0.0) {
						vec3 tex_posf = base_tex_posf + vec3(octahedron_encode(n) * float(OCT_SIZE), 0.0);
						tex_posf.xy *= tex_pixel_size;

						vec3 pos_uvw = tex_posf;
						pos_uvw.xy += vec2(offset.xy) * probe_uv_offset.xy;
						pos_uvw.x += float(offset.z) * probe_uv_offset.z;
						vec3 indirect_light = textureLod(sampler2DArray(lightprobe_texture, linear_sampler), pos_uvw, 0.0).rgb;

						light_accum[k] += indirect_light * weight;
						weight_accum[k] += weight;
					}
				}
			}
		}

		for (uint k = 0; k < 6; k++) {
			if (weight_accum[k] > 0.0) {
				light_accum[k] /= weight_accum[k];
				light_accum[k] *= feedback;
			}
		}
	}

#endif

	{
		uint rgbe = process_voxels.data[voxel_index].light;

		
		float r = float((rgbe & 0xff) << 1);
		float g = float((rgbe >> 8) & 0x1ff);
		float b = float(((rgbe >> 17) & 0xff) << 1);
		float e = float((rgbe >> 25) & 0x1F);
		float m = pow(2.0, e - 15.0 - 9.0);

		vec3 l = vec3(r, g, b) * m;

		uint aniso = process_voxels.data[voxel_index].light_aniso;
		for (uint i = 0; i < 6; i++) {
			float strength = ((aniso >> (i * 5)) & 0x1F) / float(0x1F);
			light_accum[i] += l * strength;
		}
	}

	

	vec3 pos_to_uvw = 1.0 / params.grid_size;
	vec3 uvw_ofs = pos_to_uvw * 0.5;

	for (uint i = 0; i < params.light_count; i++) {
		float attenuation = 1.0;
		vec3 direction;
		float light_distance = 1e20;
		vec3 texture_color = vec3(1.0);

		switch (lights.data[i].type) {
			case LIGHT_TYPE_DIRECTIONAL: {
				direction = -lights.data[i].direction;
			} break;
			case LIGHT_TYPE_OMNI: {
				vec3 rel_vec = lights.data[i].position - position;
				direction = normalize(rel_vec);
				light_distance = length(rel_vec);
				rel_vec.y /= params.y_mult;
				attenuation = get_omni_attenuation(light_distance, 1.0 / lights.data[i].radius, lights.data[i].attenuation);

			} break;
			case LIGHT_TYPE_SPOT: {
				vec3 rel_vec = lights.data[i].position - position;
				direction = normalize(rel_vec);
				light_distance = length(rel_vec);
				rel_vec.y /= params.y_mult;
				attenuation = get_omni_attenuation(light_distance, 1.0 / lights.data[i].radius, lights.data[i].attenuation);

				float cos_spot_angle = lights.data[i].cos_spot_angle;
				float cos_angle = dot(-direction, lights.data[i].direction);

				if (cos_angle < cos_spot_angle) {
					continue;
				}

				float scos = max(cos_angle, cos_spot_angle);
				float spot_rim = max(0.0001, (1.0 - scos) / (1.0 - cos_spot_angle));
				attenuation *= 1.0 - pow(spot_rim, lights.data[i].inv_spot_attenuation);
			} break;
			case LIGHT_TYPE_AREA: {
				float EPSILON = 1e-7f;
				vec3 area_width = lights.data[i].area_width.xyz;
				vec3 area_height = lights.data[i].area_height.xyz;
				if (dot(area_width, area_width) < EPSILON || dot(area_height, area_height) < EPSILON) {
					continue; 
				}
				if (dot(lights.data[i].direction, position - lights.data[i].position) <= 0) {
					continue; 
				}
				compute_area_light(i, position, attenuation, direction, light_distance, texture_color);
			} break;
		}

		if (attenuation < 0.001) {
			continue;
		}

		bool hit = false;

		vec3 ray_pos = position;
		vec3 ray_dir = direction;
		vec3 inv_dir = 1.0 / ray_dir;

		
		float cell_size = 1.0 / cascades.data[params.cascade].to_cell;
		ray_pos += sign(direction) * cell_size * 0.48; 
		ray_pos += ray_dir * 0.4 * cell_size; 

		for (uint j = params.cascade; j < params.max_cascades; j++) {
			
			vec3 pos = ray_pos - cascades.data[j].offset;
			pos *= cascades.data[j].to_cell;
			float local_distance = light_distance * cascades.data[j].to_cell;

			if (any(lessThan(pos, vec3(0.0))) || any(greaterThanEqual(pos, params.grid_size))) {
				continue; 
			}

			
			vec3 t0 = -pos * inv_dir;
			vec3 t1 = (params.grid_size - pos) * inv_dir;
			vec3 tmax = max(t0, t1);
			float max_advance = min(tmax.x, min(tmax.y, tmax.z));

			max_advance = min(local_distance, max_advance);

			float advance = 0.0;
			float occlusion = 1.0;

			while (advance < max_advance) {
				
				vec3 uvw = (pos + ray_dir * advance) * pos_to_uvw;

				float distance = texture(sampler3D(sdf_cascades[j], linear_sampler), uvw).r * 255.0 - 1.0;
				if (distance < 0.001) {
					
					hit = true;
					break;
				}

				occlusion = min(occlusion, distance);

				advance += distance;
			}

			if (hit) {
				attenuation *= occlusion;
				break;
			}

			if (advance >= local_distance) {
				break; 
			}
			
			pos += ray_dir * max_advance;
			pos /= cascades.data[j].to_cell;
			pos += cascades.data[j].offset;
			light_distance -= max_advance / cascades.data[j].to_cell;
			ray_pos = pos;
		}

		if (!hit) {
			vec3 light = albedo * lights.data[i].color.rgb * texture_color * lights.data[i].energy * attenuation;

			for (int j = 0; j < 6; j++) {
				if (bool(valid_aniso & (1 << j))) {
					light_accum[j] += max(0.0, dot(aniso_dir[j], direction)) * light;
				}
			}
		}
	}

	

	float lumas[6];
	vec3 light_total = vec3(0);

	for (int i = 0; i < 6; i++) {
		light_total += light_accum[i];
		lumas[i] = max(light_accum[i].r, max(light_accum[i].g, light_accum[i].b));
	}

	float luma_total = max(light_total.r, max(light_total.g, light_total.b));

	uint light_total_rgbe;

	{
		

		const float pow2to9 = 512.0f;
		const float B = 15.0f;
		const float N = 9.0f;
		const float LN2 = 0.6931471805599453094172321215;

		float cRed = clamp(light_total.r, 0.0, 65408.0);
		float cGreen = clamp(light_total.g, 0.0, 65408.0);
		float cBlue = clamp(light_total.b, 0.0, 65408.0);

		float cMax = max(cRed, max(cGreen, cBlue));

		float expp = max(-B - 1.0f, floor(log(cMax) / LN2)) + 1.0f + B;

		float sMax = floor((cMax / pow(2.0f, expp - B - N)) + 0.5f);

		float exps = expp + 1.0f;

		if (0.0 <= sMax && sMax < pow2to9) {
			exps = expp;
		}

		float sRed = floor((cRed / pow(2.0f, exps - B - N)) + 0.5f);
		float sGreen = floor((cGreen / pow(2.0f, exps - B - N)) + 0.5f);
		float sBlue = floor((cBlue / pow(2.0f, exps - B - N)) + 0.5f);
#ifdef MODE_PROCESS_STATIC
		
		light_total_rgbe = ((uint(sRed) & 0x1FF) >> 1) | ((uint(sGreen) & 0x1FF) << 8) | (((uint(sBlue) & 0x1FF) >> 1) << 17) | ((uint(exps) & 0x1F) << 25);

#else
		light_total_rgbe = (uint(sRed) & 0x1FF) | ((uint(sGreen) & 0x1FF) << 9) | ((uint(sBlue) & 0x1FF) << 18) | ((uint(exps) & 0x1F) << 27);
#endif
	}

#ifdef MODE_PROCESS_DYNAMIC

	vec4 aniso0;
	aniso0.r = lumas[0] / luma_total;
	aniso0.g = lumas[1] / luma_total;
	aniso0.b = lumas[2] / luma_total;
	aniso0.a = lumas[3] / luma_total;

	vec2 aniso1;
	aniso1.r = lumas[4] / luma_total;
	aniso1.g = lumas[5] / luma_total;

	
	imageStore(dst_aniso0, positioni, aniso0);
	imageStore(dst_aniso1, positioni, vec4(aniso1, 0.0, 0.0));
	imageStore(dst_light, positioni, uvec4(light_total_rgbe));

	

	
	uint neighbors = (voxel_albedo >> 21) | ((voxel_position >> 21) << 11) | ((process_voxels.data[voxel_index].light >> 30) << 22) | ((process_voxels.data[voxel_index].light_aniso >> 30) << 24);

	const uint max_neighbours = 26;
	const ivec3 neighbour_positions[max_neighbours] = ivec3[](
			ivec3(-1, -1, -1),
			ivec3(-1, -1, 0),
			ivec3(-1, -1, 1),
			ivec3(-1, 0, -1),
			ivec3(-1, 0, 0),
			ivec3(-1, 0, 1),
			ivec3(-1, 1, -1),
			ivec3(-1, 1, 0),
			ivec3(-1, 1, 1),
			ivec3(0, -1, -1),
			ivec3(0, -1, 0),
			ivec3(0, -1, 1),
			ivec3(0, 0, -1),
			ivec3(0, 0, 1),
			ivec3(0, 1, -1),
			ivec3(0, 1, 0),
			ivec3(0, 1, 1),
			ivec3(1, -1, -1),
			ivec3(1, -1, 0),
			ivec3(1, -1, 1),
			ivec3(1, 0, -1),
			ivec3(1, 0, 0),
			ivec3(1, 0, 1),
			ivec3(1, 1, -1),
			ivec3(1, 1, 0),
			ivec3(1, 1, 1));

	for (uint i = 0; i < max_neighbours; i++) {
		if (bool(neighbors & (1 << i))) {
			ivec3 neighbour_pos = positioni + neighbour_positions[i];
			imageStore(dst_light, neighbour_pos, uvec4(light_total_rgbe));
			imageStore(dst_aniso0, neighbour_pos, aniso0);
			imageStore(dst_aniso1, neighbour_pos, vec4(aniso1, 0.0, 0.0));
		}
	}

#endif

#ifdef MODE_PROCESS_STATIC

	

	uint light = process_voxels.data[voxel_index].light & (3 << 30);
	light |= light_total_rgbe;
	process_voxels.data[voxel_index].light = light; 

	uint light_aniso = process_voxels.data[voxel_index].light_aniso & (3 << 30);
	for (int i = 0; i < 6; i++) {
		light_aniso |= min(31, uint((lumas[i] / luma_total) * 31.0)) << (i * 5);
	}

	process_voxels.data[voxel_index].light_aniso = light_aniso;

#endif
}
)<!>")
		};
		setup(_vertex_code, _fragment_code, _compute_code, "SdfgiDirectLightShaderRD");
	}
};
