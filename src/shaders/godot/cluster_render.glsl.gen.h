/**************************************************************************/
/*  cluster_render.glsl.gen.h                                             */
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

class ClusterRenderShaderRD : public ShaderRD {
public:
	ClusterRenderShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(location = 0) in vec3 vertex_attrib;

layout(location = 0) out float depth_interp;
layout(location = 1) out flat uint element_index;

layout(push_constant, std430) uniform Params {
	uint base_index;
	uint pad0;
	uint pad1;
	uint pad2;
}
params;

layout(set = 0, binding = 1, std140) uniform State {
	mat4 projection;

	float inv_z_far;
	uint screen_to_clusters_shift; 
	uint cluster_screen_width; 
	uint cluster_data_size; 

	uint cluster_depth_offset;
	uint pad0;
	uint pad1;
	uint pad2;
}
state;

struct RenderElement {
	uint type; 
	bool touches_near;
	bool touches_far;
	uint original_index;
	mat3x4 transform_inv;
	vec3 scale;
	uint pad;
};

layout(set = 0, binding = 2, std430) buffer restrict readonly RenderElements {
	RenderElement data[];
}
render_elements;

void main() {
	element_index = params.base_index + gl_InstanceIndex;

	vec3 vertex = vertex_attrib;
	vertex *= render_elements.data[element_index].scale;

	vertex = vec4(vertex, 1.0) * render_elements.data[element_index].transform_inv;
	depth_interp = -vertex.z;

	gl_Position = state.projection * vec4(vertex, 1.0);
}

)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable
#extension GL_KHR_shader_subgroup_vote : enable



layout(constant_id = 0) const bool sc_use_helper_check = true;

layout(location = 0) in float depth_interp;
layout(location = 1) in flat uint element_index;

layout(set = 0, binding = 1, std140) uniform State {
	mat4 projection;
	float inv_z_far;
	uint screen_to_clusters_shift; 
	uint cluster_screen_width; 
	uint cluster_data_size; 
	uint cluster_depth_offset;
	uint pad0;
	uint pad1;
	uint pad2;
}
state;





layout(set = 0, binding = 3, std430) buffer restrict ClusterRender {
	uint data[];
}
cluster_render;

#ifdef USE_ATTACHMENT
layout(location = 0) out vec4 frag_color;
#endif

void main() {
	
	uvec2 cluster = uvec2(gl_FragCoord.xy) >> state.screen_to_clusters_shift;

	
	uint cluster_offset = cluster.x + state.cluster_screen_width * cluster.y;
	
	cluster_offset *= state.cluster_data_size;

	
	uint usage_write_offset = cluster_offset + (element_index >> 5);
	uint usage_write_bit = 1 << (element_index & 0x1F);

	uint aux = 0;

	uint cluster_thread_group_index;
	if (!sc_use_helper_check || !gl_HelperInvocation) {
		

		uvec4 mask;

		while (true) {
			
			
			uint first = subgroupBroadcastFirst(cluster_offset);
			
			mask = subgroupBallot(first == cluster_offset);
			if (first == cluster_offset) {
				
				
				break;
			}
		}

		cluster_thread_group_index = subgroupBallotExclusiveBitCount(mask);

		if (cluster_thread_group_index == 0) {
			aux = atomicOr(cluster_render.data[usage_write_offset], usage_write_bit);
		}
	}

	
	float unit_depth = depth_interp * state.inv_z_far;

	uint z_bit = clamp(uint(floor(unit_depth * 32.0)), 0, 31);

	uint z_write_offset = cluster_offset + state.cluster_depth_offset + element_index;
	uint z_write_bit = 1 << z_bit;

	if (!sc_use_helper_check || !gl_HelperInvocation) {
		z_write_bit = subgroupOr(z_write_bit); 
		if (cluster_thread_group_index == 0) {
			aux = atomicOr(cluster_render.data[z_write_offset], z_write_bit);
		}
	}

#ifdef USE_ATTACHMENT
	frag_color = vec4(float(aux));
#endif
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "ClusterRenderShaderRD");
	}
};
