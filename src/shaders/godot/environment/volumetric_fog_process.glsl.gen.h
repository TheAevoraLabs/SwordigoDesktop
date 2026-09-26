/**************************************************************************/
/*  volumetric_fog_process.glsl.gen.h                                     */
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

class VolumetricFogProcessShaderRD : public ShaderRD {
public:
	VolumetricFogProcessShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
(R"<!>(
#version 450

#VERSION_DEFINES

#ifdef USE_VULKAN_MEMORY_MODEL
#pragma use_vulkan_memory_model
#endif

#ifdef MODE_DENSITY
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
#else
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#endif

#define CLUSTER_COUNTER_SHIFT 20
#define CLUSTER_POINTER_MASK ((1 << CLUSTER_COUNTER_SHIFT) - 1)
#define CLUSTER_COUNTER_MASK 0xfff
#define LIGHT_BAKE_DISABLED 0
#define LIGHT_BAKE_STATIC 1
#define LIGHT_BAKE_DYNAMIC 2

struct LightData { 
	vec3 position;
	float inv_radius;

	vec3 direction;
	float size;

	vec3 color;
	float attenuation;

	mediump vec3 area_width;
	float cone_attenuation; 

	mediump vec3 area_height;
	float cone_angle; 

	float specular_amount;
	float shadow_opacity;
	float pad[2];

	vec4 atlas_rect; 
	mat4 shadow_matrix;
	float shadow_bias;
	float shadow_normal_bias;
	float transmittance_bias;
	float soft_shadow_size; 
	float soft_shadow_scale; 
	uint mask;
	float volumetric_fog_energy;
	uint bake_mode;
	vec4 projector_rect; 
};

#define REFLECTION_AMBIENT_DISABLED 0
#define REFLECTION_AMBIENT_ENVIRONMENT 1
#define REFLECTION_AMBIENT_COLOR 2

struct ReflectionData {
	vec3 box_extents;
	float index;
	vec3 box_offset;
	uint mask;
	vec3 ambient; 
	float intensity;
	float blend_distance;
	bool exterior;
	bool box_project;
	uint ambient_mode;
	float exposure_normalization;
	float pad0;
	float pad1;
	float pad2;
	
	mat4 local_matrix; 
	
};

struct DirectionalLightData {
	vec3 direction;
	float energy; 
	vec3 color;
	float size;
	float specular;
	uint mask;
	float softshadow_angle;
	float soft_shadow_scale;
	bool blend_splits;
	float shadow_opacity;
	float fade_from;
	float fade_to;
	uvec2 pad;
	uint bake_mode;
	float volumetric_fog_energy;
	vec4 shadow_bias;
	vec4 shadow_normal_bias;
	vec4 shadow_transmittance_bias;
	vec4 shadow_z_range;
	vec4 shadow_range_begin;
	vec4 shadow_split_offsets;
	mat4 shadow_matrix1;
	mat4 shadow_matrix2;
	mat4 shadow_matrix3;
	mat4 shadow_matrix4;
	vec2 uv_scale1;
	vec2 uv_scale2;
	vec2 uv_scale3;
	vec2 uv_scale4;
};

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

#define M_TAU 6.28318530718
#define M_PI 3.14159265359

#define DENSITY_SCALE 1024.0

layout(set = 0, binding = 1) uniform texture2D shadow_atlas;
layout(set = 0, binding = 2) uniform texture2D directional_shadow_atlas;

layout(set = 0, binding = 3, std430) restrict readonly buffer OmniLights {
	LightData data[];
}
omni_lights;

layout(set = 0, binding = 4, std430) restrict readonly buffer SpotLights {
	LightData data[];
}
spot_lights;

layout(set = 0, binding = 5, std430) restrict readonly buffer AreaLights {
	LightData data[];
}
area_lights;

layout(set = 0, binding = 6, std140) uniform DirectionalLights {
	DirectionalLightData data[MAX_DIRECTIONAL_LIGHT_DATA_STRUCTS];
}
directional_lights;

layout(set = 0, binding = 7, std430) buffer restrict readonly ClusterBuffer {
	uint data[];
}
cluster_buffer;

layout(set = 0, binding = 8) uniform sampler linear_sampler;

#ifdef MODE_DENSITY
layout(rgba16f, set = 0, binding = 9) uniform restrict writeonly image3D density_map;
#endif

#ifdef MODE_FOG
layout(rgba16f, set = 0, binding = 9) uniform restrict readonly image3D density_map;
layout(rgba16f, set = 0, binding = 10) uniform restrict writeonly image3D fog_map;
#endif

#ifdef MODE_COPY
layout(rgba16f, set = 0, binding = 9) uniform restrict readonly image3D source_map;
layout(rgba16f, set = 0, binding = 10) uniform restrict writeonly image3D dest_map;
#endif

#ifdef MODE_FILTER
layout(rgba16f, set = 0, binding = 9) uniform restrict readonly image3D source_map;
layout(rgba16f, set = 0, binding = 10) uniform restrict writeonly image3D dest_map;
#endif

layout(set = 0, binding = 11) uniform sampler shadow_sampler;

#define MAX_VOXEL_GI_INSTANCES 8

struct VoxelGIData {
	mat4 xform; 

	vec3 bounds; 
	float dynamic_range; 

	float bias; 
	float normal_bias; 
	bool blend_ambient; 
	uint mipmaps; 

	vec3 pad; 
	float exposure_normalization; 
};

layout(set = 0, binding = 12, std140) uniform VoxelGIs {
	VoxelGIData data[MAX_VOXEL_GI_INSTANCES];
}
voxel_gi_instances;
)<!>" R"<!>(
layout(set = 0, binding = 13) uniform texture3D voxel_gi_textures[MAX_VOXEL_GI_INSTANCES];

layout(set = 0, binding = 14) uniform sampler linear_sampler_with_mipmaps;

#ifdef ENABLE_SDFGI


#define SDFGI_MAX_CASCADES 8

struct SDFVoxelGICascadeData {
	vec3 position;
	float to_probe;
	ivec3 probe_world_offset;
	float to_cell; 
	vec3 pad;
	float exposure_normalization;
};

layout(set = 1, binding = 0, std140) uniform SDFGI {
	vec3 grid_size;
	uint max_cascades;

	bool use_occlusion;
	int probe_axis_size;
	float probe_to_uvw;
	float normal_bias;

	vec3 lightprobe_tex_pixel_size;
	float energy;

	vec3 lightprobe_uv_offset;
	float y_mult;

	vec3 occlusion_clamp;
	uint pad3;

	vec3 occlusion_renormalize;
	uint pad4;

	vec3 cascade_probe_size;
	uint pad5;

	SDFVoxelGICascadeData cascades[SDFGI_MAX_CASCADES];
}
sdfgi;

layout(set = 1, binding = 1) uniform texture2DArray sdfgi_ambient_texture;

layout(set = 1, binding = 2) uniform texture3D sdfgi_occlusion_texture;

#endif 

layout(set = 0, binding = 15, std140) uniform Params {
	vec2 fog_frustum_size_begin;
	vec2 fog_frustum_size_end;

	float fog_frustum_end;
	float ambient_inject;
	float z_far;
	int filter_axis;

	vec3 ambient_color;
	float sky_contribution;

	ivec3 fog_volume_size;
	uint directional_light_count;

	vec3 base_emission;
	float base_density;

	vec3 base_scattering;
	float phase_g;

	float detail_spread;
	float gi_inject;
	uint max_voxel_gi_instances;
	uint cluster_type_size;

	vec2 screen_size;
	uint cluster_shift;
	uint cluster_width;

	uint max_cluster_element_count_div_32;
	bool use_temporal_reprojection;
	uint temporal_frame;
	float temporal_blend;

	vec2 sky_border_size;
	vec2 pad;

	mat3x4 cam_rotation;
	mat4 to_prev_view;

	mat3 radiance_inverse_xform;
}
params;
#ifndef MODE_COPY
layout(set = 0, binding = 16) uniform texture3D prev_density_texture;

#ifdef NO_IMAGE_ATOMICS
layout(set = 0, binding = 17) buffer density_only_map_buffer {
	uint density_only_map[];
};
layout(set = 0, binding = 18) buffer light_only_map_buffer {
	uint light_only_map[];
};
layout(set = 0, binding = 19) buffer emissive_only_map_buffer {
	uint emissive_only_map[];
};
#else
layout(r32ui, set = 0, binding = 17) uniform uimage3D density_only_map;
layout(r32ui, set = 0, binding = 18) uniform uimage3D light_only_map;
layout(r32ui, set = 0, binding = 19) uniform uimage3D emissive_only_map;
#endif

#ifdef USE_RADIANCE_OCTMAP_ARRAY
layout(set = 0, binding = 20) uniform texture2DArray sky_texture;
#else
layout(set = 0, binding = 20) uniform texture2D sky_texture;
#endif
#endif 

layout(set = 0, binding = 21) uniform texture2D area_light_atlas;

float get_depth_at_pos(float cell_depth_size, int z) {
	float d = float(z) * cell_depth_size + cell_depth_size * 0.5; 
	d = pow(d, params.detail_spread);
	return params.fog_frustum_end * d;
}

vec3 hash3f(uvec3 x) {
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = ((x >> 16) ^ x) * 0x45d9f3b;
	x = (x >> 16) ^ x;
	return vec3(x & 0xFFFFF) / vec3(float(0xFFFFF));
}

float get_omni_attenuation(float dist, float inv_range, float decay) {
	float nd = dist * inv_range;
	nd *= nd;
	nd *= nd; 
	nd = max(1.0 - nd, 0.0);
	nd *= nd; 
	return nd * pow(max(dist, 0.0001), -decay);
}

void cluster_get_item_range(uint p_offset, out uint item_min, out uint item_max, out uint item_from, out uint item_to) {
	uint item_min_max = cluster_buffer.data[p_offset];
	item_min = item_min_max & 0xFFFF;
	item_max = item_min_max >> 16;

	item_from = item_min >> 5;
	item_to = (item_max == 0) ? 0 : ((item_max - 1) >> 5) + 1; 
}

uint cluster_get_range_clip_mask(uint i, uint z_min, uint z_max) {
	int local_min = clamp(int(z_min) - int(i) * 32, 0, 31);
	int mask_width = min(int(z_max) - int(z_min), 32 - local_min);
	return bitfieldInsert(uint(0), uint(0xFFFFFFFF), local_min, mask_width);
}

float henyey_greenstein(float cos_theta, float g) {
	const float k = 0.0795774715459; 
	return k * (1.0 - g * g) / (pow(1.0 + g * g - 2.0 * g * cos_theta, 1.5));
}

vec3 safe_normalize(vec3 v) {
	float length_squared = dot(v, v);
	return (length_squared > 1e-12) ? v * inversesqrt(length_squared) : vec3(0.0);
}

#define TEMPORAL_FRAMES 16

const vec3 halton_map[TEMPORAL_FRAMES] = vec3[](
		vec3(0.5, 0.33333333, 0.2),
		vec3(0.25, 0.66666667, 0.4),
		vec3(0.75, 0.11111111, 0.6),
		vec3(0.125, 0.44444444, 0.8),
		vec3(0.625, 0.77777778, 0.04),
		vec3(0.375, 0.22222222, 0.24),
		vec3(0.875, 0.55555556, 0.44),
		vec3(0.0625, 0.88888889, 0.64),
		vec3(0.5625, 0.03703704, 0.84),
		vec3(0.3125, 0.37037037, 0.08),
		vec3(0.8125, 0.7037037, 0.28),
		vec3(0.1875, 0.14814815, 0.48),
		vec3(0.6875, 0.48148148, 0.68),
		vec3(0.4375, 0.81481481, 0.88),
		vec3(0.9375, 0.25925926, 0.12),
		vec3(0.03125, 0.59259259, 0.32));


const float INV_FOG_FADE = 10.0;

void main() {
	vec3 fog_cell_size = 1.0 / vec3(params.fog_volume_size);

#ifdef MODE_DENSITY

	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
	if (any(greaterThanEqual(pos, params.fog_volume_size))) {
		return; 
	}
#ifdef NO_IMAGE_ATOMICS
	uint lpos = pos.z * params.fog_volume_size.x * params.fog_volume_size.y + pos.y * params.fog_volume_size.x + pos.x;
#endif

	vec3 posf = vec3(pos);

	

	vec3 fog_unit_pos = posf * fog_cell_size + fog_cell_size * 0.5; 

	uvec2 screen_pos = uvec2(fog_unit_pos.xy * params.screen_size);
	uvec2 cluster_pos = screen_pos >> params.cluster_shift;
	uint cluster_offset = (params.cluster_width * cluster_pos.y + cluster_pos.x) * (params.max_cluster_element_count_div_32 + 32);
	

	fog_unit_pos.z = pow(fog_unit_pos.z, params.detail_spread);

	vec3 view_pos;
	view_pos.xy = (fog_unit_pos.xy * 2.0 - 1.0) * mix(params.fog_frustum_size_begin, params.fog_frustum_size_end, vec2(fog_unit_pos.z));
	view_pos.z = -params.fog_frustum_end * fog_unit_pos.z;
	view_pos.y = -view_pos.y;

	vec4 reprojected_density = vec4(0.0);
	float reproject_amount = 0.0;

	if (params.use_temporal_reprojection) {
		vec3 prev_view = (params.to_prev_view * vec4(view_pos, 1.0)).xyz;
		
		prev_view.y = -prev_view.y;
		
		prev_view.z /= -params.fog_frustum_end;
		
		prev_view.xy /= mix(params.fog_frustum_size_begin, params.fog_frustum_size_end, vec2(prev_view.z));
		prev_view.xy = prev_view.xy * 0.5 + 0.5;
		
		prev_view.z = pow(prev_view.z, 1.0 / params.detail_spread);

		if (all(greaterThan(prev_view, vec3(0.0))) && all(lessThan(prev_view, vec3(1.0)))) {
			

			reprojected_density = textureLod(sampler3D(prev_density_texture, linear_sampler), prev_view, 0.0);
			reproject_amount = params.temporal_blend;

			
			

			fog_unit_pos = posf * fog_cell_size + fog_cell_size * halton_map[params.temporal_frame]; 

			screen_pos = uvec2(fog_unit_pos.xy * params.screen_size);
			cluster_pos = screen_pos >> params.cluster_shift;
			cluster_offset = (params.cluster_width * cluster_pos.y + cluster_pos.x) * (params.max_cluster_element_count_div_32 + 32);
			

			fog_unit_pos.z = pow(fog_unit_pos.z, params.detail_spread);

			view_pos.xy = (fog_unit_pos.xy * 2.0 - 1.0) * mix(params.fog_frustum_size_begin, params.fog_frustum_size_end, vec2(fog_unit_pos.z));
			view_pos.z = -params.fog_frustum_end * fog_unit_pos.z;
			view_pos.y = -view_pos.y;
		}
	}

	uint cluster_z = uint(clamp((abs(view_pos.z) / params.z_far) * 32.0, 0.0, 31.0));

	vec3 total_light = vec3(0.0);

	float total_density = params.base_density;
#ifdef NO_IMAGE_ATOMICS
	uint local_density = density_only_map[lpos];
#else
	uint local_density = imageLoad(density_only_map, pos).x;
#endif

	total_density += float(int(local_density)) / DENSITY_SCALE;
	total_density = max(0.0, total_density);

#ifdef NO_IMAGE_ATOMICS
	uint scattering_u = light_only_map[lpos];
#else
	uint scattering_u = imageLoad(light_only_map, pos).x;
#endif
	vec3 scattering = vec3(scattering_u >> 21, (scattering_u << 11) >> 21, scattering_u % 1024) / vec3(2047.0, 2047.0, 1023.0);
	scattering += params.base_scattering * params.base_density;

#ifdef NO_IMAGE_ATOMICS
	uint emission_u = emissive_only_map[lpos];
#else
	uint emission_u = imageLoad(emissive_only_map, pos).x;
#endif
	vec3 emission = vec3(emission_u >> 21, (emission_u << 11) >> 21, emission_u % 1024) / vec3(511.0, 511.0, 255.0);
	emission += params.base_emission * params.base_density;

	float cell_depth_size = abs(view_pos.z - get_depth_at_pos(fog_cell_size.z, pos.z + 1));
	

	if (total_density > 0.00005) {
		for (uint i = 0; i < params.directional_light_count; i++) {
			if (directional_lights.data[i].volumetric_fog_energy > 0.001) {
				vec3 shadow_attenuation = vec3(1.0);

				if (directional_lights.data[i].shadow_opacity > 0.001) {
					float depth_z = -view_pos.z;

					vec4 pssm_coord;
					vec3 light_dir = directional_lights.data[i].direction;
					vec4 v = vec4(view_pos, 1.0);
					float z_range;

					if (depth_z < directional_lights.data[i].shadow_split_offsets.x) {
						pssm_coord = (directional_lights.data[i].shadow_matrix1 * v);
						pssm_coord /= pssm_coord.w;
						z_range = directional_lights.data[i].shadow_z_range.x;

					} else if (depth_z < directional_lights.data[i].shadow_split_offsets.y) {
						pssm_coord = (directional_lights.data[i].shadow_matrix2 * v);
						pssm_coord /= pssm_coord.w;
						z_range = directional_lights.data[i].shadow_z_range.y;

					} else if (depth_z < directional_lights.data[i].shadow_split_offsets.z) {
						pssm_coord = (directional_lights.data[i].shadow_matrix3 * v);
						pssm_coord /= pssm_coord.w;
						z_range = directional_lights.data[i].shadow_z_range.z;

					} else {
						pssm_coord = (directional_lights.data[i].shadow_matrix4 * v);
						pssm_coord /= pssm_coord.w;
						z_range = directional_lights.data[i].shadow_z_range.w;
					}

					float depth = texture(sampler2D(directional_shadow_atlas, linear_sampler), pssm_coord.xy).r;
					float shadow = exp(min(0.0, (pssm_coord.z - depth)) * z_range * INV_FOG_FADE);

					shadow = mix(shadow, 1.0, smoothstep(directional_lights.data[i].fade_from, directional_lights.data[i].fade_to, view_pos.z)); 

					shadow_attenuation = mix(vec3(1.0 - directional_lights.data[i].shadow_opacity), vec3(1.0), shadow);
				}

				total_light += shadow_attenuation * directional_lights.data[i].color * directional_lights.data[i].energy * henyey_greenstein(dot(safe_normalize(view_pos), safe_normalize(directional_lights.data[i].direction)), params.phase_g) * directional_lights.data[i].volumetric_fog_energy;
			}
		}

		
		if (params.ambient_inject > 0.0) {
			vec3 isotropic = vec3(0.0);
			vec3 anisotropic = vec3(0.0);
			if (params.sky_contribution > 0.0) {
				float mip_bias = 2.0 + total_density * (MAX_SKY_LOD - 2.0); 
				vec3 scatter_direction = (params.radiance_inverse_xform * safe_normalize(view_pos)) * sign(params.phase_g);
#ifdef USE_RADIANCE_OCTMAP_ARRAY
				isotropic = texture(sampler2DArray(sky_texture, linear_sampler_with_mipmaps), vec3(vec3_to_oct_with_border(vec3(0.0, 1.0, 0.0), params.sky_border_size), mip_bias)).rgb;
				anisotropic = texture(sampler2DArray(sky_texture, linear_sampler_with_mipmaps), vec3(vec3_to_oct_with_border(scatter_direction, params.sky_border_size), mip_bias)).rgb;
#else
				isotropic = textureLod(sampler2D(sky_texture, linear_sampler_with_mipmaps), vec3_to_oct_with_border(vec3(0.0, 1.0, 0.0), params.sky_border_size), mip_bias).rgb;
				anisotropic = textureLod(sampler2D(sky_texture, linear_sampler_with_mipmaps), vec3_to_oct_with_border(scatter_direction, params.sky_border_size), mip_bias).rgb;
#endif 
			}

			total_light += mix(params.ambient_color, mix(isotropic, anisotropic, abs(params.phase_g)), params.sky_contribution) * params.ambient_inject;
		}

		

		{ 

			uint cluster_omni_offset = cluster_offset;

			uint item_min;
			uint item_max;
			uint item_from;
			uint item_to;

			cluster_get_item_range(cluster_omni_offset + params.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

			for (uint i = item_from; i < item_to; i++) {
				uint mask = cluster_buffer.data[cluster_omni_offset + i];
				mask &= cluster_get_range_clip_mask(i, item_min, item_max);
				uint merged_mask = mask;

				while (merged_mask != 0) {
					uint bit = findMSB(merged_mask);
					merged_mask &= ~(1 << bit);

					uint light_index = 32 * i + bit;

					
					
					

					vec3 light_pos = omni_lights.data[light_index].position;
					float d = distance(omni_lights.data[light_index].position, view_pos);
					float shadow_attenuation = 1.0;

					if (omni_lights.data[light_index].volumetric_fog_energy > 0.001 && d * omni_lights.data[light_index].inv_radius < 1.0) {
						float attenuation = get_omni_attenuation(d, omni_lights.data[light_index].inv_radius, omni_lights.data[light_index].attenuation);

						vec3 light = omni_lights.data[light_index].color;

						if (omni_lights.data[light_index].shadow_opacity > 0.001) {
							
							vec4 uv_rect = omni_lights.data[light_index].atlas_rect;
							vec2 flip_offset = omni_lights.data[light_index].direction.xy;

							vec3 local_vert = (omni_lights.data[light_index].shadow_matrix * vec4(view_pos, 1.0)).xyz;

							float shadow_len = length(local_vert); 
							vec3 shadow_sample = safe_normalize(local_vert);

							if (shadow_sample.z >= 0.0) {
								uv_rect.xy += flip_offset;
							}

							shadow_sample.z = 1.0 + abs(shadow_sample.z);
							vec3 pos = vec3(shadow_sample.xy / shadow_sample.z, shadow_len - omni_lights.data[light_index].shadow_bias);
							pos.z *= omni_lights.data[light_index].inv_radius;
							pos.z = 1.0 - pos.z;

							pos.xy = pos.xy * 0.5 + 0.5;
							pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;

							float depth = texture(sampler2D(shadow_atlas, linear_sampler), pos.xy).r;

							shadow_attenuation = mix(1.0 - omni_lights.data[light_index].shadow_opacity, 1.0, exp(min(0.0, (pos.z - depth)) / omni_lights.data[light_index].inv_radius * INV_FOG_FADE));
						}
						total_light += light * attenuation * shadow_attenuation * henyey_greenstein(dot(safe_normalize(light_pos - view_pos), safe_normalize(view_pos)), params.phase_g) * omni_lights.data[light_index].volumetric_fog_energy;
					}
				}
			}
		}

		{ 

			uint cluster_spot_offset = cluster_offset + params.cluster_type_size;

			uint item_min;
			uint item_max;
			uint item_from;
			uint item_to;

			cluster_get_item_range(cluster_spot_offset + params.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

			for (uint i = item_from; i < item_to; i++) {
				uint mask = cluster_buffer.data[cluster_spot_offset + i];
				mask &= cluster_get_range_clip_mask(i, item_min, item_max);
				uint merged_mask = mask;

				while (merged_mask != 0) {
					uint bit = findMSB(merged_mask);
					merged_mask &= ~(1 << bit);

					
					
					

					uint light_index = 32 * i + bit;

					vec3 light_pos = spot_lights.data[light_index].position;
					vec3 light_rel_vec = spot_lights.data[light_index].position - view_pos;
					float d = length(light_rel_vec);
					float shadow_attenuation = 1.0;

					if (spot_lights.data[light_index].volumetric_fog_energy > 0.001 && d * spot_lights.data[light_index].inv_radius < 1.0) {
						float attenuation = get_omni_attenuation(d, spot_lights.data[light_index].inv_radius, spot_lights.data[light_index].attenuation);

						vec3 spot_dir = spot_lights.data[light_index].direction;
						float cone_angle = spot_lights.data[light_index].cone_angle;
						float scos = max(dot(-safe_normalize(light_rel_vec), spot_dir), cone_angle);
						float spot_rim = max(0.0001, (1.0 - scos) / (1.0 - cone_angle));
						attenuation *= 1.0 - pow(spot_rim, spot_lights.data[light_index].cone_attenuation);

						vec3 light = spot_lights.data[light_index].color;

						if (spot_lights.data[light_index].shadow_opacity > 0.001) {
							
							vec4 uv_rect = spot_lights.data[light_index].atlas_rect;

							vec4 v = vec4(view_pos, 1.0);

							vec4 splane = (spot_lights.data[light_index].shadow_matrix * v);
							splane.z -= spot_lights.data[light_index].shadow_bias;
							float safe_w = abs(splane.w) > 1e-6 ? splane.w : (splane.w >= 0.0 ? 1e-6 : -1e-6);
							splane /= safe_w;

							vec3 pos = vec3(splane.xy * spot_lights.data[light_index].atlas_rect.zw + spot_lights.data[light_index].atlas_rect.xy, splane.z);

							float depth = texture(sampler2D(shadow_atlas, linear_sampler), pos.xy).r;
)<!>" R"<!>(
							shadow_attenuation = mix(1.0 - spot_lights.data[light_index].shadow_opacity, 1.0, exp(min(0.0, (pos.z - depth)) / spot_lights.data[light_index].inv_radius * INV_FOG_FADE));
						}
						total_light += light * attenuation * shadow_attenuation * henyey_greenstein(dot(safe_normalize(light_rel_vec), safe_normalize(view_pos)), params.phase_g) * spot_lights.data[light_index].volumetric_fog_energy;
					}
				}
			}
		}

		{ 

			uint cluster_area_offset = cluster_offset + params.cluster_type_size * 2;

			uint item_min;
			uint item_max;
			uint item_from;
			uint item_to;

			cluster_get_item_range(cluster_area_offset + params.max_cluster_element_count_div_32 + cluster_z, item_min, item_max, item_from, item_to);

			for (uint i = item_from; i < item_to; i++) {
				uint mask = cluster_buffer.data[cluster_area_offset + i];
				mask &= cluster_get_range_clip_mask(i, item_min, item_max);
				uint merged_mask = mask;

				while (merged_mask != 0) {
					uint bit = findMSB(merged_mask);
					merged_mask &= ~(1 << bit);

					uint light_index = 32 * i + bit;
					float shadow_attenuation = 1.0;

					vec3 area_width = area_lights.data[light_index].area_width;
					vec3 area_height = area_lights.data[light_index].area_height;
					vec3 area_direction = area_lights.data[light_index].direction;
					float EPSILON = 1e-4f;

					if (area_lights.data[light_index].volumetric_fog_energy > 0.001 && dot(area_width, area_width) > EPSILON && dot(area_height, area_height) > EPSILON) {
						float a_len = length(area_width);
						float b_len = length(area_height);
						vec3 area_width_norm = normalize(area_width);
						vec3 area_height_norm = normalize(area_height);
						float a_half_len = a_len / 2.0;
						float b_half_len = b_len / 2.0;
						vec3 light_center = area_lights.data[light_index].position;
						vec3 light_to_vert = view_pos - light_center;
						vec3 pos_local_to_light = vec3(dot(light_to_vert, area_width_norm), dot(light_to_vert, area_height_norm), dot(light_to_vert, -area_direction)); 
						vec3 closest_point_local_to_light = vec3(clamp(pos_local_to_light.x, -a_half_len, a_half_len), clamp(pos_local_to_light.y, -b_half_len, b_half_len), 0.0); 
						float inv_center_range = area_lights.data[light_index].cone_attenuation;
						vec3 closest_point_on_light = light_center + closest_point_local_to_light.x * area_width_norm + closest_point_local_to_light.y * area_height_norm; 

						vec3 light_rel_vec = closest_point_on_light - view_pos;
						float d = length(light_rel_vec);

						
						
						
						vec3 light_vec = light_rel_vec / d;
						if (d < EPSILON) {
							light_vec = area_direction;
						}
						d = max(d, 1.0);

						if (d * inv_center_range < 1.0) { 
							
							float attenuation = get_omni_attenuation(d, area_lights.data[light_index].inv_radius, area_lights.data[light_index].attenuation - 1.0);
							vec3 h_area_width = area_width / 2.0;
							vec3 h_area_height = area_height / 2.0;
							vec3 light_points[4];
							light_points[0] = area_lights.data[light_index].position - h_area_width - h_area_height - view_pos;
							light_points[1] = area_lights.data[light_index].position + h_area_width - h_area_height - view_pos;
							light_points[2] = area_lights.data[light_index].position + h_area_width + h_area_height - view_pos;
							light_points[3] = area_lights.data[light_index].position - h_area_width + h_area_height - view_pos;

							float cutoff = smoothstep(0.0, 0.1, dot(area_direction, view_pos - area_lights.data[light_index].position)); 
							float ltc_diffuse = 0.0;
							vec3 normal = light_vec;
							float max_mipmap = area_lights.data[light_index].cone_angle;
							vec3 texture_color = vec3(1.0);
							ltc_evaluate_diff(normal, light_points, area_lights.data[light_index].projector_rect, max_mipmap, area_light_atlas, linear_sampler_with_mipmaps, ltc_diffuse, texture_color);
							attenuation *= ltc_diffuse * cutoff;
							vec3 light_color = area_lights.data[light_index].color * texture_color;

							if (area_lights.data[light_index].shadow_opacity > 0.001) {
								
								vec4 uv_rect = area_lights.data[light_index].atlas_rect;

								vec3 local_vert = (area_lights.data[light_index].shadow_matrix * vec4(view_pos, 1.0)).xyz;

								float shadow_len = length(local_vert); 
								vec3 shadow_sample = normalize(local_vert);

								shadow_sample.z = 1.0 + abs(shadow_sample.z);
								vec3 pos = vec3(shadow_sample.xy / shadow_sample.z, shadow_len - area_lights.data[light_index].shadow_bias);
								pos.z *= inv_center_range;
								pos.z = 1.0 - pos.z;

								pos.xy = pos.xy * 0.5 + 0.5;
								pos.xy = uv_rect.xy + pos.xy * uv_rect.zw;

								float depth = texture(sampler2D(shadow_atlas, linear_sampler), pos.xy).r;

								shadow_attenuation = mix(1.0 - area_lights.data[light_index].shadow_opacity, 1.0, exp(min(0.0, (pos.z - depth)) / inv_center_range * INV_FOG_FADE));
							}
							float cos_theta = 0.0;
							if (dot(light_rel_vec, light_rel_vec) > EPSILON) {
								cos_theta = dot(normalize(light_rel_vec), normalize(view_pos));
							}
							total_light += light_color * attenuation * shadow_attenuation * henyey_greenstein(cos_theta, params.phase_g) * area_lights.data[light_index].volumetric_fog_energy;
						}
					}
				}
			}
		}

		vec3 world_pos = mat3(params.cam_rotation) * view_pos;

		for (uint i = 0; i < params.max_voxel_gi_instances; i++) {
			vec3 position = (voxel_gi_instances.data[i].xform * vec4(world_pos, 1.0)).xyz;

			
			if (all(bvec2(all(greaterThanEqual(position, vec3(0.0))), all(lessThan(position, voxel_gi_instances.data[i].bounds))))) {
				position /= voxel_gi_instances.data[i].bounds;

				vec4 light = vec4(0.0);
				for (uint j = 0; j < voxel_gi_instances.data[i].mipmaps; j++) {
					vec4 slight = textureLod(sampler3D(voxel_gi_textures[i], linear_sampler_with_mipmaps), position, float(j));
					float a = (1.0 - light.a);
					light += a * slight;
				}

				light.rgb *= voxel_gi_instances.data[i].dynamic_range * params.gi_inject * voxel_gi_instances.data[i].exposure_normalization;

				total_light += light.rgb;
			}
		}

		
#ifdef ENABLE_SDFGI

		{
			float blend = -1.0;
			vec3 ambient_total = vec3(0.0);

			for (uint i = 0; i < sdfgi.max_cascades; i++) {
				vec3 cascade_pos = (world_pos - sdfgi.cascades[i].position) * sdfgi.cascades[i].to_probe;

				if (any(lessThan(cascade_pos, vec3(0.0))) || any(greaterThanEqual(cascade_pos, sdfgi.cascade_probe_size))) {
					continue; 
				}

				vec3 base_pos = floor(cascade_pos);
				ivec3 probe_base_pos = ivec3(base_pos);

				vec4 ambient_accum = vec4(0.0);

				ivec3 tex_pos = ivec3(probe_base_pos.xy, int(i));
				tex_pos.x += probe_base_pos.z * sdfgi.probe_axis_size;

				for (uint j = 0; j < 8; j++) {
					ivec3 offset = (ivec3(j) >> ivec3(0, 1, 2)) & ivec3(1, 1, 1);
					ivec3 probe_posi = probe_base_pos;
					probe_posi += offset;

					

					vec3 probe_pos = vec3(probe_posi);
					vec3 probe_to_pos = cascade_pos - probe_pos;

					vec3 trilinear = vec3(1.0) - abs(probe_to_pos);
					float weight = trilinear.x * trilinear.y * trilinear.z;

					

					if (sdfgi.use_occlusion) {
						ivec3 occ_indexv = abs((sdfgi.cascades[i].probe_world_offset + probe_posi) & ivec3(1, 1, 1)) * ivec3(1, 2, 4);
						vec4 occ_mask = mix(vec4(0.0), vec4(1.0), equal(ivec4(occ_indexv.x | occ_indexv.y), ivec4(0, 1, 2, 3)));

						vec3 occ_pos = clamp(cascade_pos, probe_pos - sdfgi.occlusion_clamp, probe_pos + sdfgi.occlusion_clamp) * sdfgi.probe_to_uvw;
						occ_pos.z += float(i);
						if (occ_indexv.z != 0) { 
							occ_pos.x += 1.0;
						}

						occ_pos *= sdfgi.occlusion_renormalize;
						float occlusion = dot(textureLod(sampler3D(sdfgi_occlusion_texture, linear_sampler), occ_pos, 0.0), occ_mask);

						weight *= max(occlusion, 0.01);
					}

					

					ivec3 uvw = tex_pos;
					uvw.xy += offset.xy;
					uvw.x += offset.z * sdfgi.probe_axis_size;

					vec3 ambient = texelFetch(sampler2DArray(sdfgi_ambient_texture, linear_sampler), uvw, 0).rgb;

					ambient_accum.rgb += ambient * weight * sdfgi.cascades[i].exposure_normalization;
					ambient_accum.a += weight;
				}

				if (ambient_accum.a > 0) {
					ambient_accum.rgb /= ambient_accum.a;
				}
				ambient_total = ambient_accum.rgb;
				break;
			}

			total_light += ambient_total * params.gi_inject;
		}

#endif
	}

	vec4 final_density = vec4(total_light * scattering + emission, total_density);
	bool is_reprojected_density_invalid = any(isnan(reprojected_density)) || any(isinf(reprojected_density));
	bool is_final_density_invalid = any(isnan(final_density)) || any(isinf(final_density));

	if (is_final_density_invalid) {
		final_density = is_reprojected_density_invalid ? vec4(0.0) : reprojected_density;
	} else if (!is_reprojected_density_invalid) {
		final_density = mix(final_density, reprojected_density, reproject_amount);
	}
	final_density = clamp(final_density, vec4(0.0), vec4(65504.0));

	imageStore(density_map, pos, final_density);
#ifdef NO_IMAGE_ATOMICS
	density_only_map[lpos] = 0;
	light_only_map[lpos] = 0;
	emissive_only_map[lpos] = 0;
#else
	imageStore(density_only_map, pos, uvec4(0));
	imageStore(light_only_map, pos, uvec4(0));
	imageStore(emissive_only_map, pos, uvec4(0));
#endif
#endif

#ifdef MODE_FOG

	ivec3 pos = ivec3(gl_GlobalInvocationID.xy, 0);

	if (any(greaterThanEqual(pos, params.fog_volume_size))) {
		return; 
	}

	vec4 fog_accum = vec4(0.0, 0.0, 0.0, 1.0);
	float prev_z = 0.0;

	for (int i = 0; i < params.fog_volume_size.z; i++) {
		
		ivec3 fog_pos = pos + ivec3(0, 0, i);
		
		vec4 fog = imageLoad(density_map, fog_pos);

		
		float z = get_depth_at_pos(fog_cell_size.z, i);
		
		float d = abs(prev_z - z);
		
		float transmittance = exp(-d * fog.a);

		fog_accum.rgb += ((fog.rgb - fog.rgb * transmittance) / max(fog.a, 0.00001)) * fog_accum.a;
		fog_accum.a *= transmittance;

		prev_z = z;

		vec4 final_fog = vec4(fog_accum.rgb, fog_accum.a);
		bool is_final_fog_invalid = any(isnan(final_fog)) || any(isinf(final_fog));
		final_fog = is_final_fog_invalid ? vec4(0.0) : final_fog;
		final_fog = clamp(final_fog, vec4(0.0), vec4(65504.0));
		imageStore(fog_map, fog_pos, final_fog);
	}

#endif

#ifdef MODE_FILTER

	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);

	const float gauss[7] = float[](0.071303, 0.131514, 0.189879, 0.214607, 0.189879, 0.131514, 0.071303);

	const ivec3 filter_dir[3] = ivec3[](ivec3(1, 0, 0), ivec3(0, 1, 0), ivec3(0, 0, 1));
	ivec3 offset = filter_dir[params.filter_axis];

	vec4 accum = vec4(0.0);
	for (int i = -3; i <= 3; i++) {
		accum += imageLoad(source_map, clamp(pos + offset * i, ivec3(0), params.fog_volume_size - ivec3(1))) * gauss[i + 3];
	}

	imageStore(dest_map, pos, accum);

#endif
#ifdef MODE_COPY
	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz);
	if (any(greaterThanEqual(pos, params.fog_volume_size))) {
		return; 
	}

	imageStore(dest_map, pos, imageLoad(source_map, pos));

#endif
}
)<!>")
		};
		setup(_vertex_code, _fragment_code, _compute_code, "VolumetricFogProcessShaderRD");
	}
};
