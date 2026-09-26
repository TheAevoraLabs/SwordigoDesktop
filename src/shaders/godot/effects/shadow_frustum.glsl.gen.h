/**************************************************************************/
/*  shadow_frustum.glsl.gen.h                                             */
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

class ShadowFrustumShaderRD : public ShaderRD {
public:
	ShadowFrustumShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

/* clang-format on */

layout(push_constant, std430) uniform Info {
	mat4 mvp;
	vec4 color;
}
info;

layout(location = 0) in vec3 vertex_attrib;

void main() {
	vec4 vertex = info.mvp * vec4(vertex_attrib, 1.0);
	vertex.xyz /= vertex.w;
	gl_Position = vec4(vertex.xy, 0.0, 1.0);
}

/* clang-format off */
)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

layout(push_constant, std430) uniform Info {
	mat4 mvp;
	vec4 color;
}
info;

layout(location = 0) out vec4 frag_color;

void main() {
	frag_color = info.color;
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "ShadowFrustumShaderRD");
	}
};
