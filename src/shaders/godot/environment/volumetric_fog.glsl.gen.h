/**************************************************************************/
/*  volumetric_fog.glsl.gen.h                                             */
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

class VolumetricFogShaderRD : public ShaderRD {
public:
	VolumetricFogShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#ifdef USE_VULKAN_MEMORY_MODEL
#pragma use_vulkan_memory_model
#endif

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

#define DENSITY_SCALE 1024.0

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

#define M_PI 3.14159265359

layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 0) uniform sampler SAMPLER_NEAREST_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 1) uniform sampler SAMPLER_LINEAR_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 2) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 3) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 4) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 5) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_CLAMP;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 6) uniform sampler SAMPLER_NEAREST_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 7) uniform sampler SAMPLER_LINEAR_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 8) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 9) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 10) uniform sampler SAMPLER_NEAREST_WITH_MIPMAPS_ANISOTROPIC_REPEAT;
layout(set = 0, binding = SAMPLERS_BINDING_FIRST_INDEX + 11) uniform sampler SAMPLER_LINEAR_WITH_MIPMAPS_ANISOTROPIC_REPEAT;

layout(set = 0, binding = 2, std430) restrict readonly buffer GlobalShaderUniformData {
	vec4 data[];
}
global_shader_uniforms;

layout(push_constant, std430) uniform Params {
	vec3 position;
	float pad;

	vec3 size;
	float pad2;

	ivec3 corner;
	uint shape;

	mat4 transform;
}
params;

#ifdef NO_IMAGE_ATOMICS
layout(set = 1, binding = 1) volatile buffer emissive_only_map_buffer {
	uint emissive_only_map[];
};
#else
layout(r32ui, set = 1, binding = 1) uniform volatile uimage3D emissive_only_map;
#endif

layout(set = 1, binding = 2, std140) uniform SceneParams {
	vec2 fog_frustum_size_begin;
	vec2 fog_frustum_size_end;

	float fog_frustum_end;
	float z_near; 
	float z_far; 
	float time;

	ivec3 fog_volume_size;
	uint directional_light_count; 

	bool use_temporal_reprojection;
	uint temporal_frame;
	float detail_spread;
	float temporal_blend;

	mat4 to_prev_view;
	mat4 transform;
}
scene_params;

#ifdef NO_IMAGE_ATOMICS
layout(set = 1, binding = 3) volatile buffer density_only_map_buffer {
	uint density_only_map[];
};
layout(set = 1, binding = 4) volatile buffer light_only_map_buffer {
	uint light_only_map[];
};
#else
layout(r32ui, set = 1, binding = 3) uniform volatile uimage3D density_only_map;
layout(r32ui, set = 1, binding = 4) uniform volatile uimage3D light_only_map;
#endif

#ifdef MATERIAL_UNIFORMS_USED
/* clang-format off */
layout(set = 2, binding = 0, std140) uniform MaterialUniforms {
#MATERIAL_UNIFORMS
} material;
/* clang-format on */
#endif

#GLOBALS

float get_depth_at_pos(float cell_depth_size, int z) {
	float d = float(z) * cell_depth_size + cell_depth_size * 0.5; 
	d = pow(d, scene_params.detail_spread);
	return scene_params.fog_frustum_end * d;
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

void main() {
	vec3 fog_cell_size = 1.0 / vec3(scene_params.fog_volume_size);

	ivec3 pos = ivec3(gl_GlobalInvocationID.xyz) + params.corner;
	if (any(greaterThanEqual(pos, scene_params.fog_volume_size))) {
		return; 
	}
#ifdef NO_IMAGE_ATOMICS
	uint lpos = pos.z * scene_params.fog_volume_size.x * scene_params.fog_volume_size.y + pos.y * scene_params.fog_volume_size.x + pos.x;
#endif

	vec3 posf = vec3(pos);

	vec3 fog_unit_pos = posf * fog_cell_size + fog_cell_size * 0.5; 
	fog_unit_pos.z = pow(fog_unit_pos.z, scene_params.detail_spread);

	vec3 view_pos;
	view_pos.xy = (fog_unit_pos.xy * 2.0 - 1.0) * mix(scene_params.fog_frustum_size_begin, scene_params.fog_frustum_size_end, vec2(fog_unit_pos.z));
	view_pos.z = -scene_params.fog_frustum_end * fog_unit_pos.z;
	view_pos.y = -view_pos.y;

	if (scene_params.use_temporal_reprojection) {
		vec3 prev_view = (scene_params.to_prev_view * vec4(view_pos, 1.0)).xyz;
		
		prev_view.y = -prev_view.y;
		
		prev_view.z /= -scene_params.fog_frustum_end;
		
		prev_view.xy /= mix(scene_params.fog_frustum_size_begin, scene_params.fog_frustum_size_end, vec2(prev_view.z));
		prev_view.xy = prev_view.xy * 0.5 + 0.5;
		
		prev_view.z = pow(prev_view.z, 1.0 / scene_params.detail_spread);

		if (all(greaterThan(prev_view, vec3(0.0))) && all(lessThan(prev_view, vec3(1.0)))) {
			
			
			

			fog_unit_pos = posf * fog_cell_size + fog_cell_size * halton_map[scene_params.temporal_frame]; 
			fog_unit_pos.z = pow(fog_unit_pos.z, scene_params.detail_spread);

			view_pos.xy = (fog_unit_pos.xy * 2.0 - 1.0) * mix(scene_params.fog_frustum_size_begin, scene_params.fog_frustum_size_end, vec2(fog_unit_pos.z));
			view_pos.z = -scene_params.fog_frustum_end * fog_unit_pos.z;
			view_pos.y = -view_pos.y;
		}
	}

	float density = 0.0;
	vec3 emission = vec3(0.0);
	vec3 albedo = vec3(0.0);

	float cell_depth_size = abs(view_pos.z - get_depth_at_pos(fog_cell_size.z, pos.z + 1));

	vec4 world = scene_params.transform * vec4(view_pos, 1.0);
	world.xyz /= world.w;

	vec3 uvw = fog_unit_pos;

	vec4 local_pos = params.transform * world;
	local_pos.xyz /= local_pos.w;

	vec3 half_size = params.size / 2.0;
	float sdf = -1.0;
	if (params.shape == 0) {
		
		
		float k0 = length(local_pos.xyz / half_size);
		float k1 = length(local_pos.xyz / (half_size * half_size));
		sdf = k0 * (k0 - 1.0) / k1;
	} else if (params.shape == 1) {
		
		

		
		float inv_height = 1.0 / max(0.001, half_size.y);
		float radius = 1.0 / max(0.001, (min(half_size.x, half_size.z) * 0.5));
		float hypotenuse = sqrt(radius * radius + inv_height * inv_height);
		float rsin = radius / hypotenuse;
		float rcos = inv_height / hypotenuse;
		vec2 c = vec2(rsin, rcos);

		float q = length(local_pos.xz);
		sdf = max(dot(c, vec2(q, local_pos.y - half_size.y)), -half_size.y - local_pos.y);
	} else if (params.shape == 2) {
		
		
		vec2 d = abs(vec2(length(local_pos.xz), local_pos.y)) - vec2(min(half_size.x, half_size.z), half_size.y);
		sdf = min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
	} else if (params.shape == 3) {
		
		
		vec3 q = abs(local_pos.xyz) - half_size;
		sdf = length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
	}

	float cull_mask = 1.0; 
	if (params.shape <= 3) {
#ifndef SDF_USED
		cull_mask = 1.0 - smoothstep(-0.1, 0.0, sdf);
#endif
		uvw = clamp((local_pos.xyz + half_size) / params.size, 0.0, 1.0);
	}

	if (cull_mask > 0.0) {
		{
#CODE : FOG
		}

#ifdef DENSITY_USED
		density *= cull_mask;
		if (abs(density) > 0.001) {
			int final_density = int(density * DENSITY_SCALE);
#ifdef NO_IMAGE_ATOMICS
			atomicAdd(density_only_map[lpos], uint(final_density));
#else
			imageAtomicAdd(density_only_map, pos, uint(final_density));
#endif

#ifdef EMISSION_USED
			{
				emission *= clamp(density, 0.0, 1.0);
				emission = clamp(emission, vec3(0.0), vec3(4.0));
				
				uvec3 emission_u = uvec3(emission.r * 511.0, emission.g * 511.0, emission.b * 255.0);
				
				uint final_emission = emission_u.r << 21 | emission_u.g << 10 | emission_u.b;
#ifdef NO_IMAGE_ATOMICS
				uint prev_emission = atomicAdd(emissive_only_map[lpos], final_emission);
#else
				uint prev_emission = imageAtomicAdd(emissive_only_map, pos, final_emission);
#endif

				
				uvec3 prev_emission_u = uvec3(prev_emission >> 21, (prev_emission << 11) >> 21, prev_emission % 1024);
				uint add_emission = final_emission + prev_emission;
				uvec3 add_emission_u = uvec3(add_emission >> 21, (add_emission << 11) >> 21, add_emission % 1024);

				bvec3 overflowing = lessThan(add_emission_u, prev_emission_u + emission_u);

				if (any(overflowing)) {
					uvec3 overflow_factor = mix(uvec3(0), uvec3(2047 << 21, 2047 << 10, 1023), overflowing);
					uint force_max = overflow_factor.r | overflow_factor.g | overflow_factor.b;
#ifdef NO_IMAGE_ATOMICS
					atomicOr(emissive_only_map[lpos], force_max);
#else
					imageAtomicOr(emissive_only_map, pos, force_max);
#endif
				}
			}
#endif
#ifdef ALBEDO_USED
			{
				vec3 scattering = albedo * clamp(density, 0.0, 1.0);
				scattering = clamp(scattering, vec3(0.0), vec3(1.0));
				uvec3 scattering_u = uvec3(scattering.r * 2047.0, scattering.g * 2047.0, scattering.b * 1023.0);
				
				uint final_scattering = scattering_u.r << 21 | scattering_u.g << 10 | scattering_u.b;
#ifdef NO_IMAGE_ATOMICS
				uint prev_scattering = atomicAdd(light_only_map[lpos], final_scattering);
#else
				uint prev_scattering = imageAtomicAdd(light_only_map, pos, final_scattering);
#endif

				
				uvec3 prev_scattering_u = uvec3(prev_scattering >> 21, (prev_scattering << 11) >> 21, prev_scattering % 1024);
				uint add_scattering = final_scattering + prev_scattering;
				uvec3 add_scattering_u = uvec3(add_scattering >> 21, (add_scattering << 11) >> 21, add_scattering % 1024);

				bvec3 overflowing = lessThan(add_scattering_u, prev_scattering_u + scattering_u);

				if (any(overflowing)) {
					uvec3 overflow_factor = mix(uvec3(0), uvec3(2047 << 21, 2047 << 10, 1023), overflowing);
					uint force_max = overflow_factor.r | overflow_factor.g | overflow_factor.b;
#ifdef NO_IMAGE_ATOMICS
					atomicOr(light_only_map[lpos], force_max);
#else
					imageAtomicOr(light_only_map, pos, force_max);
#endif
				}
			}
#endif 
		}
#endif 
	}
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "VolumetricFogShaderRD");
	}
};
