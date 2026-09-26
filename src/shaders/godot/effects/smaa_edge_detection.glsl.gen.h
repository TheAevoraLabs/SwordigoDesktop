/**************************************************************************/
/*  smaa_edge_detection.glsl.gen.h                                        */
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

class SmaaEdgeDetectionShaderRD : public ShaderRD {
public:
	SmaaEdgeDetectionShaderRD() {
		static const char _vertex_code[] = {
R"<!>(#version 450

layout(location = 0) out vec2 tex_coord;
layout(location = 1) out vec4 offset[3];

layout(push_constant, std430) uniform Params {
	vec2 inv_size;
	float threshold;
	float reserved;
}
params;

void main() {
	vec2 vertex_base;
	if (gl_VertexIndex == 0) {
		vertex_base = vec2(-1.0, -1.0);
	} else if (gl_VertexIndex == 1) {
		vertex_base = vec2(-1.0, 3.0);
	} else {
		vertex_base = vec2(3.0, -1.0);
	}
	gl_Position = vec4(vertex_base, 0.0, 1.0);
	tex_coord = clamp(vertex_base, vec2(0.0, 0.0), vec2(1.0, 1.0)) * 2.0; 

	offset[0] = fma(params.inv_size.xyxy, vec4(-1.0, 0.0, 0.0, -1.0), tex_coord.xyxy);
	offset[1] = fma(params.inv_size.xyxy, vec4(1.0, 0.0, 0.0, 1.0), tex_coord.xyxy);
	offset[2] = fma(params.inv_size.xyxy, vec4(-2.0, 0.0, 0.0, -2.0), tex_coord.xyxy);
}

)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(#version 450

layout(location = 0) in vec2 tex_coord;
layout(location = 1) in vec4 offset[3];

layout(set = 0, binding = 0) uniform sampler2D color_tex;

layout(location = 0) out vec2 edges;

layout(push_constant, std430) uniform Params {
	vec2 inv_size;
	float threshold;
	float reserved;
}
params;

#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0

void main() {
	vec2 threshold = vec2(params.threshold);

	vec4 delta;
	vec3 C = texture(color_tex, tex_coord).rgb;

	vec3 Cleft = texture(color_tex, offset[0].xy).rgb;
	vec3 t = abs(C - Cleft);
	delta.x = max(max(t.r, t.g), t.b);

	vec3 Ctop = texture(color_tex, offset[0].zw).rgb;
	t = abs(C - Ctop);
	delta.y = max(max(t.r, t.g), t.b);

	edges = step(threshold, delta.xy);

	if (dot(edges, vec2(1.0, 1.0)) == 0.0) {
		discard;
	}

	vec3 Cright = texture(color_tex, offset[1].xy).rgb;
	t = abs(C - Cright);
	delta.z = max(max(t.r, t.g), t.b);

	vec3 Cbottom = texture(color_tex, offset[1].zw).rgb;
	t = abs(C - Cbottom);
	delta.w = max(max(t.r, t.g), t.b);

	vec2 max_delta = max(delta.xy, delta.zw);

	vec3 Cleftleft = texture(color_tex, offset[2].xy).rgb;
	t = abs(Cleft - Cleftleft);
	delta.z = max(max(t.r, t.g), t.b);

	vec3 Ctoptop = texture(color_tex, offset[2].zw).rgb;
	t = abs(Ctop - Ctoptop);
	delta.w = max(max(t.r, t.g), t.b);

	max_delta = max(max_delta.xy, delta.zw);
	float final_delta = max(max_delta.x, max_delta.y);

	edges.xy *= step(final_delta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "SmaaEdgeDetectionShaderRD");
	}
};
