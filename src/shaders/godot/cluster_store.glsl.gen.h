/**************************************************************************/
/*  cluster_store.glsl.gen.h                                              */
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

class ClusterStoreShaderRD : public ShaderRD {
public:
	ClusterStoreShaderRD() {
		static const char *_vertex_code = nullptr;
		static const char *_fragment_code = nullptr;
		static const char _compute_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(push_constant, std430) uniform Params {
	uint cluster_render_data_size; 
	uint max_render_element_count_div_32; 
	uvec2 cluster_screen_size;
	uint render_element_count_div_32; 

	uint max_cluster_element_count_div_32; 
	uint pad1;
	uint pad2;
}
params;

layout(set = 0, binding = 1, std430) buffer restrict readonly ClusterRender {
	uint data[];
}
cluster_render;

layout(set = 0, binding = 2, std430) buffer restrict ClusterStore {
	uint data[];
}
cluster_store;

struct RenderElement {
	uint type; 
	bool touches_near;
	bool touches_far;
	uint original_index;
	mat3x4 transform_inv;
	vec3 scale;
	uint pad;
};

layout(set = 0, binding = 3, std430) buffer restrict readonly RenderElements {
	RenderElement data[];
}
render_elements;

void main() {
	uvec2 pos = gl_GlobalInvocationID.xy;
	if (any(greaterThanEqual(pos, params.cluster_screen_size))) {
		return;
	}

	

	
	uint base_offset = (pos.x + params.cluster_screen_size.x * pos.y);
	uint src_offset = base_offset * params.cluster_render_data_size;

	uint render_element_offset = 0;

	
	while (render_element_offset < params.render_element_count_div_32) {
		uint bits = cluster_render.data[src_offset + render_element_offset];
		while (bits != 0) {
			
			uint index_bit = findLSB(bits);
			uint index = render_element_offset * 32 + index_bit;
			uint type = render_elements.data[index].type;

			uint z_range_offset = src_offset + params.max_render_element_count_div_32 + index;
			uint z_range = cluster_render.data[z_range_offset];

			
			if (z_range != 0) { 

				uint from_z = findLSB(z_range);
				uint to_z = findMSB(z_range) + 1;

				if (render_elements.data[index].touches_near) {
					from_z = 0;
				}

				if (render_elements.data[index].touches_far) {
					to_z = 32;
				}

				
				uint dst_offset = (base_offset + type * (params.cluster_screen_size.x * params.cluster_screen_size.y)) * (params.max_cluster_element_count_div_32 + 32);

				uint orig_index = render_elements.data[index].original_index;
				
				for (uint i = from_z; i < to_z; i++) {
					uint slice_ofs = dst_offset + params.max_cluster_element_count_div_32 + i;

					uint minmax = cluster_store.data[slice_ofs];

					if (minmax == 0) {
						minmax = 0xFFFF; 
					}

					uint elem_min = min(orig_index, minmax & 0xFFFF);
					uint elem_max = max(orig_index + 1, minmax >> 16); 

					minmax = elem_min | (elem_max << 16);
					cluster_store.data[slice_ofs] = minmax;
				}

				uint store_word = orig_index >> 5;
				uint store_bit = orig_index & 0x1F;

				
				cluster_store.data[dst_offset + store_word] |= 1 << store_bit;
			}

			bits &= ~(1 << index_bit); 
		}

		render_element_offset++;
	}
}
)<!>"
		};
		setup(_vertex_code, _fragment_code, _compute_code, "ClusterStoreShaderRD");
	}
};
