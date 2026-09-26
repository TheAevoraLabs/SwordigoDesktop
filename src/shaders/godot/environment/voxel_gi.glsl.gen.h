/**************************************************************************/
/*  voxel_gi.glsl.gen.h                                                   */
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

class VoxelGiShaderRD : public ShaderRD {
public:
	VoxelGiShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
(R"<!>(
#version 450

#VERSION_DEFINES

#ifdef MODE_DYNAMIC
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#else
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#endif

#ifndef MODE_DYNAMIC

#define NO_CHILDREN 0xFFFFFFFF

struct CellChildren {
	uint children[8];
};

layout(set = 0, binding = 1, std430) buffer CellChildrenBuffer {
	CellChildren data[];
}
cell_children;

struct CellData {
	uint position; 
	uint albedo; 
	uint emission; 
	uint normal; 
};

layout(set = 0, binding = 2, std430) buffer CellDataBuffer {
	CellData data[];
}
cell_data;

#endif 

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

#if defined(MODE_COMPUTE_LIGHT) || defined(MODE_DYNAMIC_LIGHTING)

struct Light {
	uint type;
	float energy;
	float radius;
	float attenuation;

	vec3 color;
	float cos_spot_angle;

	vec3 position;
	float inv_spot_attenuation;

	vec3 direction;
	bool has_shadow;

	vec4 area_width;
	vec4 area_height;
	vec4 area_projector_rect;
};

layout(set = 0, binding = 3, std140) uniform Lights {
	Light data[MAX_LIGHTS];
}
lights;

layout(set = 1, binding = 0) uniform texture2D area_light_atlas;

#endif 

#ifdef MODE_SECOND_BOUNCE

layout(set = 0, binding = 5) uniform texture3D color_texture;

#endif 

#ifndef MODE_DYNAMIC

layout(push_constant, std430) uniform Params {
	ivec3 limits;
	uint stack_size;

	float emission_scale;
	float propagation;
	float dynamic_range;

	uint light_count;
	uint cell_offset;
	uint cell_count;
	float aniso_strength;
	float cell_size;
}
params;

layout(set = 0, binding = 4, std430) buffer Outputs {
	vec4 data[];
}
outputs;

#endif 

layout(set = 0, binding = 9) uniform texture3D texture_sdf;
layout(set = 0, binding = 10) uniform sampler texture_sampler;

#ifdef MODE_WRITE_TEXTURE

layout(rgba8, set = 0, binding = 5) uniform restrict writeonly image3D color_tex;

#endif

#ifdef MODE_DYNAMIC

layout(push_constant, std430) uniform Params {
	ivec3 limits;
	uint light_count; 
	ivec3 x_dir;
	float z_base;
	ivec3 y_dir;
	float z_sign;
	ivec3 z_dir;
	float pos_multiplier;
	ivec2 rect_pos;
	ivec2 rect_size;
	ivec2 prev_rect_ofs;
	ivec2 prev_rect_size;
	bool flip_x;
	bool flip_y;
	float dynamic_range;
	bool on_mipmap;
	float propagation;
	float cell_size;
	float pad[2];
}
params;

#ifdef MODE_DYNAMIC_LIGHTING

layout(rgba8, set = 0, binding = 5) uniform restrict readonly image2D source_albedo;
layout(rgba8, set = 0, binding = 6) uniform restrict readonly image2D source_normal;
layout(rgba8, set = 0, binding = 7) uniform restrict readonly image2D source_orm;

layout(rgba16f, set = 0, binding = 11) uniform restrict image2D emission;
layout(r32f, set = 0, binding = 12) uniform restrict image2D depth;

#endif

#ifdef MODE_DYNAMIC_SHRINK

layout(rgba16f, set = 0, binding = 5) uniform restrict readonly image2D source_light;
layout(r32f, set = 0, binding = 6) uniform restrict readonly image2D source_depth;

#ifdef MODE_DYNAMIC_SHRINK_WRITE

layout(rgba16f, set = 0, binding = 7) uniform restrict writeonly image2D light;
layout(r32f, set = 0, binding = 8) uniform restrict writeonly image2D depth;

#endif 

#ifdef MODE_DYNAMIC_SHRINK_PLOT

layout(rgba8, set = 0, binding = 11) uniform restrict image3D color_texture;

#endif 

#endif 



#endif 

#if defined(MODE_COMPUTE_LIGHT) || defined(MODE_DYNAMIC_LIGHTING)

float raymarch(float distance, float distance_adv, vec3 from, vec3 direction) {
	vec3 cell_size = 1.0 / vec3(params.limits);
	float occlusion = 1.0;
	while (distance > 0.5) { 
		float advance = texture(sampler3D(texture_sdf, texture_sampler), from * cell_size).r * 255.0 - 1.0;
		if (advance < 0.0) {
			occlusion = 0.0;
			break;
		}

		occlusion = min(advance, occlusion);

		advance = max(distance_adv, advance - mod(advance, distance_adv)); 

		from += direction * advance;
		distance -= advance;
	}

	return occlusion; 
}

float get_omni_attenuation(float distance, float inv_range, float decay) {
	float nd = distance * inv_range;
	nd *= nd;
	nd *= nd; 
	nd = max(1.0 - nd, 0.0);
	nd *= nd; 
	return nd * pow(max(distance, 0.0001), -decay);
}

bool compute_light_vector(uint light, vec3 pos, out float attenuation, out vec3 light_pos) {
	if (lights.data[light].type == LIGHT_TYPE_DIRECTIONAL) {
		light_pos = pos - lights.data[light].direction * length(vec3(params.limits));
		attenuation = 1.0;

	} else {
		light_pos = lights.data[light].position;
		float distance = length(pos - light_pos);
		if (distance >= lights.data[light].radius) {
			return false;
		}

		attenuation = get_omni_attenuation(
				distance * params.cell_size,
				1.0 / (lights.data[light].radius * params.cell_size),
				lights.data[light].attenuation);

		if (lights.data[light].type == LIGHT_TYPE_SPOT) {
			vec3 rel = normalize(pos - light_pos);
			float cos_spot_angle = lights.data[light].cos_spot_angle;
			float cos_angle = dot(rel, lights.data[light].direction);
			if (cos_angle < cos_spot_angle) {
				return false;
			}

			float scos = max(cos_angle, cos_spot_angle);
			float spot_rim = max(0.0001, (1.0 - scos) / (1.0 - cos_spot_angle));
			attenuation *= 1.0 - pow(spot_rim, lights.data[light].inv_spot_attenuation);
		}
	}

	return true;
}
)<!>" R"<!>(
float get_normal_advance(vec3 p_normal) {
	vec3 normal = p_normal;
	vec3 unorm = abs(normal);

	if ((unorm.x >= unorm.y) && (unorm.x >= unorm.z)) {
		
		unorm = normal.x > 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(-1.0, 0.0, 0.0);
	} else if ((unorm.y > unorm.x) && (unorm.y >= unorm.z)) {
		
		unorm = normal.y > 0.0 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, -1.0, 0.0);
	} else if ((unorm.z > unorm.x) && (unorm.z > unorm.y)) {
		
		unorm = normal.z > 0.0 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 0.0, -1.0);
	} else {
		
		
		unorm = vec3(1.0, 0.0, 0.0);
	}

	return 1.0 / dot(normal, unorm);
}

void clip_segment(vec4 plane, vec3 begin, inout vec3 end) {
	vec3 segment = begin - end;
	float den = dot(plane.xyz, segment);

	
	if (den < 0.0001) {
		return;
	}

	float dist = (dot(plane.xyz, begin) - plane.w) / den;

	if (dist < 0.0001 || dist > 1.0001) {
		return;
	}

	end = begin + segment * -dist;
}

bool compute_light_at_pos(uint index, vec3 pos, vec3 normal, inout vec3 light, inout vec3 light_dir) {
	float attenuation;
	vec3 light_pos;
	if (!compute_light_vector(index, pos, attenuation, light_pos)) {
		return false;
	}

	light_dir = normalize(pos - light_pos);

	if (attenuation < 0.01 || (length(normal) > 0.2 && dot(normal, light_dir) >= 0)) {
		return false; 
	}

	if (lights.data[index].has_shadow) {
		float distance_adv = get_normal_advance(light_dir);

		vec3 to = pos;
		if (length(normal) > 0.2) {
			to += normal * distance_adv * 0.51;
		} else {
			to -= sign(light_dir) * 0.45; 
		}

		
		clip_segment(mix(vec4(-1.0, 0.0, 0.0, 0.0), vec4(1.0, 0.0, 0.0, float(params.limits.x - 1)), bvec4(light_dir.x < 0.0)), to, light_pos);
		clip_segment(mix(vec4(0.0, -1.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, float(params.limits.y - 1)), bvec4(light_dir.y < 0.0)), to, light_pos);
		clip_segment(mix(vec4(0.0, 0.0, -1.0, 0.0), vec4(0.0, 0.0, 1.0, float(params.limits.z - 1)), bvec4(light_dir.z < 0.0)), to, light_pos);

		float distance = length(to - light_pos);
		if (distance < 0.1) {
			return false; 
		}

		distance += distance_adv - mod(distance, distance_adv); 
		light_pos = to - light_dir * distance;

		

		/*float dist = raymarch(distance,distance_adv,light_pos,light_dir);

		if (dist > distance_adv) {
			return false;
		}

		attenuation *= 1.0 - smoothstep(0.1*distance_adv,distance_adv,dist);
		*/

		float occlusion = raymarch(distance, distance_adv, light_pos, light_dir);

		if (occlusion == 0.0) {
			return false;
		}

		attenuation *= occlusion; 
	}

	light = lights.data[index].color * attenuation * lights.data[index].energy;
	return true;
}


bool compute_area_light(uint index, vec3 pos, vec3 normal, inout vec3 light) {
	float EPSILON = 1e-7f;
	vec3 area_width = lights.data[index].area_width.xyz;
	vec3 area_height = lights.data[index].area_height.xyz;
	vec3 area_direction = lights.data[index].direction;
	vec3 vertex = pos;

	if (dot(area_width, area_width) < EPSILON || dot(area_height, area_height) < EPSILON) { 
		return false;
	}
	if (dot(area_direction, vertex - lights.data[index].position) <= 0) {
		return false; 
	}

	float a_len = length(area_width);
	float b_len = length(area_height);
	vec3 area_width_norm = normalize(area_width);
	vec3 area_height_norm = normalize(area_height);
	float a_half_len = a_len / 2.0;
	float b_half_len = b_len / 2.0;
	vec3 light_center = lights.data[index].position;
	vec3 light_to_vert = vertex - light_center;
	vec3 pos_local_to_light = vec3(dot(light_to_vert, area_width_norm), dot(light_to_vert, area_height_norm), dot(light_to_vert, -area_direction)); 
	vec3 closest_point_local_to_light = vec3(clamp(pos_local_to_light.x, -a_half_len, a_half_len), clamp(pos_local_to_light.y, -b_half_len, b_half_len), 0); 
	vec3 closest_point_on_light = light_center + closest_point_local_to_light.x * area_width_norm + closest_point_local_to_light.y * area_height_norm; 
	float dist = length(closest_point_on_light - vertex);

	float light_length = max(1.0 / params.cell_size, dist);
	if (light_length >= lights.data[index].radius) {
		return false;
	}
	float attenuation = get_omni_attenuation(light_length * params.cell_size, 1.0 / (lights.data[index].radius * params.cell_size), lights.data[index].attenuation) * light_length * light_length * params.cell_size * params.cell_size; 

	if (attenuation < 0.01) {
		return false;
	}

	vec3 points[4];
	vec3 h_area_width = area_width / 2.0;
	vec3 h_area_height = area_height / 2.0;
	points[0] = (lights.data[index].position - h_area_width - h_area_height - vertex) * params.cell_size;
	points[1] = (lights.data[index].position + h_area_width - h_area_height - vertex) * params.cell_size;
	points[2] = (lights.data[index].position + h_area_width + h_area_height - vertex) * params.cell_size;
	points[3] = (lights.data[index].position - h_area_width + h_area_height - vertex) * params.cell_size;

	if (dot(normal, normal) < 0.04) { 
		
		
		normal = -area_direction;
	}
	vec3 light_tex_color = vec3(1.0);
	float ltc;
	ltc_evaluate_diff(normal, points, lights.data[index].area_projector_rect, lights.data[index].cos_spot_angle, area_light_atlas, texture_sampler, ltc, light_tex_color);
	vec3 ltc_diffuse = ltc * light_tex_color;

	light = lights.data[index].color * ltc_diffuse * attenuation * lights.data[index].energy;

	return true;
}

#endif 

void main() {
#ifndef MODE_DYNAMIC

	uint cell_index = gl_GlobalInvocationID.x;
	if (cell_index >= params.cell_count) {
		return;
	}
	cell_index += params.cell_offset;

	uvec3 posu = uvec3(cell_data.data[cell_index].position & 0x7FF, (cell_data.data[cell_index].position >> 11) & 0x3FF, cell_data.data[cell_index].position >> 21);
	vec4 albedo = unpackUnorm4x8(cell_data.data[cell_index].albedo);

#endif

	

#ifdef MODE_COMPUTE_LIGHT

	vec3 pos = vec3(posu) + vec3(0.5);

	vec3 emission = vec3(uvec3(cell_data.data[cell_index].emission & 0x1ff, (cell_data.data[cell_index].emission >> 9) & 0x1ff, (cell_data.data[cell_index].emission >> 18) & 0x1ff)) * pow(2.0, float(cell_data.data[cell_index].emission >> 27) - 15.0 - 9.0);
	vec3 normal = unpackSnorm4x8(cell_data.data[cell_index].normal).xyz;

	vec3 accum = vec3(0.0);

	for (uint i = 0; i < params.light_count; i++) {
		if (lights.data[i].type != LIGHT_TYPE_AREA) {
			vec3 light;
			vec3 light_dir;
			if (!compute_light_at_pos(i, pos, normal, light, light_dir)) {
				continue;
			}

			light *= albedo.rgb;

			if (length(normal) > 0.2) {
				accum += max(0.0, dot(normal, -light_dir)) * light;
			} else {
				
				accum += light;
			}
		} else {
			vec3 light;
			if (!compute_area_light(i, pos, normal, light)) {
				continue;
			}
			light *= albedo.rgb;
			accum += light;
			
		}
	}

	outputs.data[cell_index] = vec4(accum + emission, 0.0);

#endif 

	

#ifdef MODE_SECOND_BOUNCE
	vec3 pos = vec3(posu) + vec3(0.5);
	ivec3 ipos = ivec3(posu);
	vec4 normal = unpackSnorm4x8(cell_data.data[cell_index].normal);

	vec3 accum = outputs.data[cell_index].rgb;

	if (length(normal.xyz) > 0.2) {
		vec3 v0 = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
		vec3 tangent = normalize(cross(v0, normal.xyz));
		vec3 bitangent = normalize(cross(tangent, normal.xyz));
		mat3 normal_mat = mat3(tangent, bitangent, normal.xyz);

#define MAX_CONE_DIRS 6

		vec3 cone_dirs[MAX_CONE_DIRS] = vec3[](
				vec3(0.0, 0.0, 1.0),
				vec3(0.866025, 0.0, 0.5),
				vec3(0.267617, 0.823639, 0.5),
				vec3(-0.700629, 0.509037, 0.5),
				vec3(-0.700629, -0.509037, 0.5),
				vec3(0.267617, -0.823639, 0.5));

		float cone_weights[MAX_CONE_DIRS] = float[](0.25, 0.15, 0.15, 0.15, 0.15, 0.15);
		float tan_half_angle = 0.577;

		for (int i = 0; i < MAX_CONE_DIRS; i++) {
			vec3 direction = normal_mat * cone_dirs[i];
			vec4 color = vec4(0.0);
			{
				float dist = 1.5;
				float max_distance = length(vec3(params.limits));
				vec3 cell_size = 1.0 / vec3(params.limits);

				while (dist < max_distance && color.a < 0.95) {
					float diameter = max(1.0, 2.0 * tan_half_angle * dist);
					vec3 uvw_pos = (pos + dist * direction) * cell_size;
					float half_diameter = diameter * 0.5;
					
					
					
					

					float log2_diameter = log2(diameter);
					vec4 scolor = textureLod(sampler3D(color_texture, texture_sampler), uvw_pos, log2_diameter);
					float a = (1.0 - color.a);
					color += a * scolor;
					dist += half_diameter;
				}
			}
			color *= cone_weights[i] * vec4(albedo.rgb, 1.0) * params.dynamic_range; 
			accum += color.rgb;
		}
	}

	outputs.data[cell_index] = vec4(accum, 0.0);

#endif 

	

#ifdef MODE_UPDATE_MIPMAPS

	{
		vec3 light_accum = vec3(0.0);
		float count = 0.0;
		for (uint i = 0; i < 8; i++) {
			uint child_index = cell_children.data[cell_index].children[i];
			if (child_index == NO_CHILDREN) {
				continue;
			}
			light_accum += outputs.data[child_index].rgb;

			count += 1.0;
		}

		float divisor = mix(8.0, count, params.propagation);
		outputs.data[cell_index] = vec4(light_accum / divisor, 0.0);
	}
#endif

	

#ifdef MODE_WRITE_TEXTURE
	{
		imageStore(color_tex, ivec3(posu), vec4(outputs.data[cell_index].rgb / params.dynamic_range, albedo.a));
	}
#endif

	

#ifdef MODE_DYNAMIC

	ivec2 pos_xy = ivec2(gl_GlobalInvocationID.xy);
	if (any(greaterThanEqual(pos_xy, params.rect_size))) {
		return; 
	}

	ivec2 uv_xy = pos_xy;
	if (params.flip_x) {
		uv_xy.x = params.rect_size.x - pos_xy.x - 1;
	}
	if (params.flip_y) {
		uv_xy.y = params.rect_size.y - pos_xy.y - 1;
	}

#ifdef MODE_DYNAMIC_LIGHTING

	{
		float z = params.z_base + imageLoad(depth, uv_xy).x * params.z_sign;

		ivec3 pos = params.x_dir * (params.rect_pos.x + pos_xy.x) + params.y_dir * (params.rect_pos.y + pos_xy.y) + abs(params.z_dir) * int(z);

		vec3 normal = normalize(imageLoad(source_normal, uv_xy).xyz * 2.0 - 1.0);
		normal = vec3(params.x_dir) * normal.x * mix(1.0, -1.0, params.flip_x) + vec3(params.y_dir) * normal.y * mix(1.0, -1.0, params.flip_y) - vec3(params.z_dir) * normal.z;

		vec4 albedo = imageLoad(source_albedo, uv_xy);

		

		vec3 accum = vec3(0.0);
		for (uint i = 0; i < params.light_count; i++) {
			if (lights.data[i].type != LIGHT_TYPE_AREA) {
				vec3 light;
				vec3 light_dir;
				if (!compute_light_at_pos(i, vec3(pos) * params.pos_multiplier, normal, light, light_dir)) {
					continue;
				}

				light *= albedo.rgb;

				accum += max(0.0, dot(normal, -light_dir)) * light;
			} else {
				vec3 light;
				if (!compute_area_light(i, vec3(pos) * params.pos_multiplier, normal, light)) {
					continue;
				}
				light *= albedo.rgb;
				accum += light;

				
			}
		}

		accum += imageLoad(emission, uv_xy).xyz;

		imageStore(emission, uv_xy, vec4(accum, albedo.a));
		imageStore(depth, uv_xy, vec4(z));
	}

#endif 

#ifdef MODE_DYNAMIC_SHRINK

	{
		vec4 accum = vec4(0.0);
		float accum_z = 0.0;
		float count = 0.0;

		for (int i = 0; i < 4; i++) {
			ivec2 ofs = pos_xy * 2 + ivec2(i & 1, i >> 1) - params.prev_rect_ofs;
			if (any(lessThan(ofs, ivec2(0))) || any(greaterThanEqual(ofs, params.prev_rect_size))) {
				continue;
			}
			if (params.flip_x) {
				ofs.x = params.prev_rect_size.x - ofs.x - 1;
			}
			if (params.flip_y) {
				ofs.y = params.prev_rect_size.y - ofs.y - 1;
			}

			vec4 light = imageLoad(source_light, ofs);
			if (light.a == 0.0) { 
				continue;
			}
			accum += light;
			float z = imageLoad(source_depth, ofs).x;
			accum_z += z * 0.5; 
			count += 1.0;
		}

		if (params.on_mipmap) {
			accum.rgb /= mix(8.0, count, params.propagation);
			accum.a /= 8.0;
		} else {
			accum /= 4.0;
		}

		if (count == 0.0) {
			accum_z = 0.0; 
		} else {
			accum_z /= count;
		}

#ifdef MODE_DYNAMIC_SHRINK_WRITE

		imageStore(light, uv_xy, accum);
		imageStore(depth, uv_xy, vec4(accum_z));
#endif

#ifdef MODE_DYNAMIC_SHRINK_PLOT

		if (accum.a < 0.001) {
			return; 
		}

		ivec3 pos = params.x_dir * (params.rect_pos.x + pos_xy.x) + params.y_dir * (params.rect_pos.y + pos_xy.y) + abs(params.z_dir) * int(accum_z);

		float z_frac = fract(accum_z);

		for (int i = 0; i < 2; i++) {
			ivec3 pos3d = pos + abs(params.z_dir) * i;
			if (any(lessThan(pos3d, ivec3(0))) || any(greaterThanEqual(pos3d, params.limits))) {
				
				continue;
			}
			vec4 color_blit = accum * (i == 0 ? 1.0 - z_frac : z_frac);
			vec4 color = imageLoad(color_texture, pos3d);
			color.rgb *= params.dynamic_range;

#if 0
			color.rgb = mix(color.rgb,color_blit.rgb,color_blit.a);
			color.a+=color_blit.a;
#else

			float sa = 1.0 - color_blit.a;
			vec4 result;
			result.a = color.a * sa + color_blit.a;
			if (result.a == 0.0) {
				result = vec4(0.0);
			} else {
				result.rgb = (color.rgb * color.a * sa + color_blit.rgb * color_blit.a) / result.a;
				color = result;
			}

#endif
			color.rgb /= params.dynamic_range;
			imageStore(color_texture, pos3d, color);
			
		}
#endif 
	}
#endif

#endif 
}
)<!>")
		};
		setup(_vertex_code, _fragment_code, _compute_code, "VoxelGiShaderRD");
	}
};
