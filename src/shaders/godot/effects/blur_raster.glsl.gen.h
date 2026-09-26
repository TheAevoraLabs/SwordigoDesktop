/**************************************************************************/
/*  blur_raster.glsl.gen.h                                                */
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

class BlurRasterShaderRD : public ShaderRD {
public:
	BlurRasterShaderRD() {
		static const char _vertex_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#define FLAG_HORIZONTAL (1 << 0)
#define FLAG_USE_ORTHOGONAL_PROJECTION (1 << 1)
#define FLAG_GLOW_FIRST_PASS (1 << 2)

layout(push_constant, std430) uniform Blur {
	vec2 dest_pixel_size; 
	vec2 source_pixel_size; 

	vec2 pad; 
	uint flags; 
	float glow_level; 

	
	float glow_strength; 
	float glow_bloom; 
	float glow_hdr_threshold; 
	float glow_hdr_scale; 

	float glow_exposure; 
	float glow_white; 
	float glow_luminance_cap; 
	float luminance_multiplier; 
}
blur;

layout(location = 0) out vec2 uv_interp;
/* clang-format on */

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
	uv_interp = clamp(vertex_base, vec2(0.0, 0.0), vec2(1.0, 1.0)) * 2.0; 
}

/* clang-format off */
)<!>"
		};
		static const char _fragment_code[] = {
R"<!>(
#version 450

#VERSION_DEFINES

#define FLAG_HORIZONTAL (1 << 0)
#define FLAG_USE_ORTHOGONAL_PROJECTION (1 << 1)
#define FLAG_GLOW_FIRST_PASS (1 << 2)

layout(push_constant, std430) uniform Blur {
	vec2 dest_pixel_size; 
	vec2 source_pixel_size; 

	vec2 pad; 
	uint flags; 
	float glow_level; 

	
	float glow_strength; 
	float glow_bloom; 
	float glow_hdr_threshold; 
	float glow_hdr_scale; 

	float glow_exposure; 
	float glow_white; 
	float glow_luminance_cap; 
	float luminance_multiplier; 
}
blur;

layout(location = 0) in vec2 uv_interp;
/* clang-format on */

layout(set = 0, binding = 0) uniform sampler2D source_color;

#ifdef MODE_GLOW_UPSAMPLE

layout(set = 1, binding = 0) uniform sampler2D blend_color;
layout(constant_id = 0) const bool use_debanding = false;
layout(constant_id = 1) const bool use_blend_color = false;






vec3 screen_space_dither(vec2 frag_coord, float bit_alignment_diviser) {
	
	
	vec3 dither = vec3(dot(vec2(171.0, 231.0), frag_coord));
	dither.rgb = fract(dither.rgb / vec3(103.0, 71.0, 97.0));

	
	
	return (dither.rgb - 0.5) / bit_alignment_diviser;
}
#endif

layout(location = 0) out vec4 frag_color;

#ifdef MODE_GLOW_DOWNSAMPLE


vec4 BloomDownKernel4(sampler2D Tex, vec2 uv0) {
	vec2 RcpSrcTexRes = blur.source_pixel_size;

	vec2 tc = (uv0 * 2.0 + 1.0) * RcpSrcTexRes;

	float la = 1.0 / 4.0;

	vec2 o = (0.5 + la) * RcpSrcTexRes;

	vec4 c = vec4(0.0);
	c += textureLod(Tex, tc + vec2(-1.0, -1.0) * o, 0.0) * 0.25;
	c += textureLod(Tex, tc + vec2(1.0, -1.0) * o, 0.0) * 0.25;
	c += textureLod(Tex, tc + vec2(-1.0, 1.0) * o, 0.0) * 0.25;
	c += textureLod(Tex, tc + vec2(1.0, 1.0) * o, 0.0) * 0.25;

	return c;
}

#endif

#ifdef MODE_GLOW_UPSAMPLE


vec4 BloomUpKernel4(sampler2D Tex, vec2 uv0) {
	vec2 RcpSrcTexRes = blur.source_pixel_size;

	vec2 uv = uv0 * 0.5 + 0.5;

	vec2 uvI = floor(uv);
	vec2 uvF = uv - uvI;

	vec2 tc = uvI * RcpSrcTexRes.xy;

	
	float lw = 0.357386;
	float la = 25.0 / 32.0; 
	float lb = 3.0 / 64.0; 

	vec2 l = vec2(-1.5 + la, 0.5 + lb);

	vec2 lx = uvF.x == 0.0 ? l.xy : -l.yx;
	vec2 ly = uvF.y == 0.0 ? l.xy : -l.yx;

	lx *= RcpSrcTexRes.xx;
	ly *= RcpSrcTexRes.yy;

	vec4 c00 = textureLod(Tex, tc + vec2(lx.x, ly.x), 0.0);
	vec4 c10 = textureLod(Tex, tc + vec2(lx.y, ly.x), 0.0);
	vec4 c01 = textureLod(Tex, tc + vec2(lx.x, ly.y), 0.0);
	vec4 c11 = textureLod(Tex, tc + vec2(lx.y, ly.y), 0.0);

	vec2 w = abs(uvF * 2.0 - lw);

	vec4 cx0 = c00 * (1.0 - w.x) + (c10 * w.x);
	vec4 cx1 = c01 * (1.0 - w.x) + (c11 * w.x);

	vec4 cxy = cx0 * (1.0 - w.y) + (cx1 * w.y);

	return cxy;
}

#endif 

void main() {
	

#ifdef MODE_MIPMAP

	vec2 pix_size = blur.dest_pixel_size;
	vec4 color = texture(source_color, uv_interp + vec2(-0.5, -0.5) * pix_size);
	color += texture(source_color, uv_interp + vec2(0.5, -0.5) * pix_size);
	color += texture(source_color, uv_interp + vec2(0.5, 0.5) * pix_size);
	color += texture(source_color, uv_interp + vec2(-0.5, 0.5) * pix_size);
	frag_color = color / 4.0;

#endif

#ifdef MODE_GAUSSIAN_BLUR

	
	
	
	vec4 A = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(-1.0, -1.0));
	vec4 B = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(0.0, -1.0));
	vec4 C = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(1.0, -1.0));
	vec4 D = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(-0.5, -0.5));
	vec4 E = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(0.5, -0.5));
	vec4 F = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(-1.0, 0.0));
	vec4 G = texture(source_color, uv_interp);
	vec4 H = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(1.0, 0.0));
	vec4 I = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(-0.5, 0.5));
	vec4 J = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(0.5, 0.5));
	vec4 K = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(-1.0, 1.0));
	vec4 L = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(0.0, 1.0));
	vec4 M = texture(source_color, uv_interp + blur.dest_pixel_size * vec2(1.0, 1.0));

	float base_weight = 0.5 / 4.0;
	float lesser_weight = 0.125 / 4.0;

	frag_color = (D + E + I + J) * base_weight;
	frag_color += (A + B + G + F) * lesser_weight;
	frag_color += (B + C + H + G) * lesser_weight;
	frag_color += (F + G + L + K) * lesser_weight;
	frag_color += (G + H + M + L) * lesser_weight;
#endif

#ifdef MODE_GLOW_GATHER
	
	

	vec2 block_pos = floor(gl_FragCoord.xy) * 4.0;
	vec2 end = max(1.0 / blur.source_pixel_size - vec2(4.0), vec2(0.0));
	block_pos = clamp(block_pos, vec2(0.0), end);

	

	vec4 color = textureLod(source_color, (block_pos + vec2(1.0, 1.0)) * blur.source_pixel_size, 0.0);
	color += textureLod(source_color, (block_pos + vec2(1.0, 3.0)) * blur.source_pixel_size, 0.0);
	color += textureLod(source_color, (block_pos + vec2(3.0, 1.0)) * blur.source_pixel_size, 0.0);
	color += textureLod(source_color, (block_pos + vec2(3.0, 3.0)) * blur.source_pixel_size, 0.0);
	frag_color = color * 0.25;

	
	frag_color *= blur.glow_strength;
	frag_color *= blur.glow_strength;

	
	
	frag_color *= blur.luminance_multiplier;
	frag_color *= blur.glow_exposure;

	float luminance = max(frag_color.r, max(frag_color.g, frag_color.b));
	float feedback = max(smoothstep(blur.glow_hdr_threshold, blur.glow_hdr_threshold + blur.glow_hdr_scale, luminance), blur.glow_bloom);

	frag_color = min(frag_color * feedback, vec4(blur.glow_luminance_cap)) / blur.luminance_multiplier;
#endif 

#ifdef MODE_GLOW_DOWNSAMPLE
	
	frag_color = BloomDownKernel4(source_color, floor(gl_FragCoord.xy));
	frag_color *= blur.glow_strength;
#endif 

#ifdef MODE_GLOW_UPSAMPLE

	frag_color = BloomUpKernel4(source_color, floor(gl_FragCoord.xy)) * blur.glow_strength; 
	if (use_blend_color) {
		vec2 uv = floor(gl_FragCoord.xy) + 0.5;
		frag_color += textureLod(blend_color, uv * blur.dest_pixel_size, 0.0) * blur.glow_level;
	}

	if (use_debanding) {
		frag_color.rgb += screen_space_dither(gl_FragCoord.xy, 1023.0);
	}
#endif 

#ifdef MODE_COPY
	vec4 color = textureLod(source_color, uv_interp, 0.0);
	frag_color = color;
#endif
}
)<!>"
		};
		static const char *_compute_code = nullptr;
		setup(_vertex_code, _fragment_code, _compute_code, "BlurRasterShaderRD");
	}
};
