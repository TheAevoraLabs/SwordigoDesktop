/**************************************************************************/
/*  canvas_occlusion.glsl.gen.h                                           */
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

class CanvasOcclusionShaderRD : public ShaderRD {
public:
	CanvasOcclusionShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(location = 0) in highp vec3 vertex;

#ifdef POSITIONAL_SHADOW
layout(push_constant, std430) uniform Constants {
	mat2x4 modelview;
	vec4 rotation;
	vec2 direction;
	float z_far;
	uint pad;
	float z_near;
	uint cull_mode;
	float pad3;
	float pad4;
}
constants;

layout(set = 0, binding = 0, std430) restrict readonly buffer OccluderTransforms {
	mat2x4 transforms[];
}
occluder_transforms;

#else

layout(push_constant, std430) uniform Constants {
	mat4 projection;
	mat2x4 modelview;
	vec2 direction;
	float z_far;
	uint cull_mode;
}
constants;

#endif

#ifdef MODE_SHADOW
layout(location = 0) out highp float depth;
#endif

void main() {
#ifdef POSITIONAL_SHADOW
	float c = -(constants.z_far + constants.z_near) / (constants.z_far - constants.z_near);
	float d = -2.0 * constants.z_far * constants.z_near / (constants.z_far - constants.z_near);

	mat4 projection = mat4(vec4(1.0, 0.0, 0.0, 0.0),
			vec4(0.0, 1.0, 0.0, 0.0),
			vec4(0.0, 0.0, c, -1.0),
			vec4(0.0, 0.0, d, 0.0));

	
	
	
	projection *= mat4(vec4(constants.rotation.x, 0.0, constants.rotation.y, 0.0), vec4(constants.rotation.z, 0.0, constants.rotation.w, 0.0), vec4(0.0, -1.0, 0.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	mat4 modelview = mat4(occluder_transforms.transforms[constants.pad]) * mat4(constants.modelview);
#else
	mat4 projection = constants.projection;
	mat4 modelview = mat4(constants.modelview[0], constants.modelview[1], vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
#endif

	highp vec4 vtx = vec4(vertex, 1.0) * modelview;

#ifdef MODE_SHADOW
	depth = dot(constants.direction, vtx.xy);
#endif

	gl_Position = projection * vtx;
}

)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#ifdef POSITIONAL_SHADOW
layout(push_constant, std430) uniform Constants {
	mat2x4 modelview;
	vec4 rotation;
	vec2 direction;
	float z_far;
	uint pad;
	float z_near;
	uint cull_mode;
	float pad3;
	float pad4;
}
constants;

#else

layout(push_constant, std430) uniform Constants {
	mat4 projection;
	mat2x4 modelview;
	vec2 direction;
	float z_far;
	uint cull_mode;
}
constants;

#endif

#ifdef MODE_SHADOW
layout(location = 0) in highp float depth;
layout(location = 0) out highp float distance_buf;
#else
layout(location = 0) out highp float sdf_buf;
#endif

#define POLYGON_CULL_DISABLED 0
#define POLYGON_CULL_FRONT 1
#define POLYGON_CULL_BACK 2

void main() {
#ifdef MODE_SHADOW
	bool front_facing = gl_FrontFacing;
	if (constants.cull_mode == POLYGON_CULL_BACK && !front_facing) {
		discard;
	} else if (constants.cull_mode == POLYGON_CULL_FRONT && front_facing) {
		discard;
	}
	distance_buf = depth / constants.z_far;
#else
	sdf_buf = 1.0;
#endif
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "CanvasOcclusionShaderRD");
	}
};
