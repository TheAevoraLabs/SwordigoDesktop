/**************************************************************************/
/*  half_inc.glsl.gen.h                                                   */
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

static const char half_inc_shader_glsl[] = {
R"<!>(// Use of FP16 in Godot is done explicitly through the types half and hvec.
// The extensions must be supported by the system to use this shader.
//
// If EXPLICIT_FP16 is not defined, all operations will use full precision
// floats instead and all casting operations will not be performed.

#ifndef HALF_INC_H
#define HALF_INC_H

#ifdef EXPLICIT_FP16

#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require

#define HALF_FLT_MIN float16_t(6.10352e-5)
#define HALF_FLT_MAX float16_t(65504.0)

#define half float16_t
#define hvec2 f16vec2
#define hvec3 f16vec3
#define hvec4 f16vec4
#define hmat2 f16mat2
#define hmat3 f16mat3
#define hmat4 f16mat4
#define saturateHalf(x) min(float16_t(x), HALF_FLT_MAX)

#else

#define HALF_FLT_MIN float(1.175494351e-38F)
#define HALF_FLT_MAX float(3.402823466e+38F)

#define half float
#define hvec2 vec2
#define hvec3 vec3
#define hvec4 vec4
#define hmat2 mat2
#define hmat3 mat3
#define hmat4 mat4
#define saturateHalf(x) (x)

#endif

#endif // HALF_INC_H
)<!>"
};
