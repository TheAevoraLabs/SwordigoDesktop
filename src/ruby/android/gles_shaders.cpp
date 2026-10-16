// ============================================================================
// gles_shaders.cpp — Implementation of OpenGL ES 3.0 Mobile Shaders
// ============================================================================

#include "gles_shaders.h"

namespace ruby::android {

static const char* GLES_VS = R"GLSL(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec2 aUV;

uniform mat4 uProj;
uniform mat4 uModelView;

out highp vec3 vViewPos;
out highp vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 vp      = uModelView * vec4(aPos, 1.0);
    vViewPos     = vp.xyz;
    mat3 normMat = mat3(uModelView);
    vec3 n       = normMat * aNorm;
    float nlen   = length(n);
    vNormal      = (nlen > 1e-5) ? (n / nlen) : vec3(0.0, 1.0, 0.0);
    vUV          = aUV;
    gl_Position  = uProj * vp;
}
)GLSL";

static const char* GLES_FS = R"GLSL(#version 300 es
precision highp float;

in highp vec3 vViewPos;
in highp vec3 vNormal;
in vec2 vUV;

uniform sampler2D uTex;
uniform int       uHasTex;
uniform vec4      uDiffuseColor;

// Lighting uniforms
uniform vec3      uSunDir;       // view-space normalized direction towards sun
uniform vec3      uSunColor;
uniform vec3      uSkyColor;
uniform vec3      uGroundColor;

// Selection uniforms for uniform highlight shading
uniform int       uIsSelected;
uniform vec4      uSelectionColor;

out vec4 fragColor;

void main() {
    vec4 baseCol = (uHasTex != 0) ? texture(uTex, vUV) : uDiffuseColor;
    if (baseCol.a < 0.05) discard;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(uSunDir);

    // Half-Lambert diffuse (Valve / Swordigo style)
    float NdotL = dot(N, L);
    float halfLambert = NdotL * 0.5 + 0.5;
    float wrapLight = halfLambert * halfLambert;

    // Hemisphere ambient
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambient = mix(uGroundColor, uSkyColor, hemi);

    // Final color accumulation
    vec3 lit = baseCol.rgb * (ambient + uSunColor * wrapLight);

    // Uniform selection shading on selected entity
    if (uIsSelected != 0) {
        vec3 selCol = uSelectionColor.rgb;
        float selAlpha = clamp(uSelectionColor.a, 0.0, 0.65);
        // Clean uniform tint: blends the entity with selection color evenly while preserving surface texture & lighting details
        vec3 tinted = mix(lit, selCol * (0.80 + 0.20 * wrapLight), selAlpha);
        // Soft, strictly-bounded fresnel edge accent that never overflows or blows out into pure white
        float dist = length(vViewPos);
        vec3 V = (dist > 1e-4) ? normalize(-vViewPos) : vec3(0.0, 0.0, 1.0);
        float NdotV = clamp(dot(N, V), 0.0, 1.0);
        float rim = pow(1.0 - NdotV, 3.0);
        lit = mix(tinted, selCol, rim * 0.15 * selAlpha);
    }

    // Extended Reinhard tone-mapping (L_white = 2.5) matching desktop Ruby GG PostFX
    // Preserves Swordigo's warm saturated palette without crushing brightness
    const float L_white = 2.5;
    lit = lit * (vec3(1.0) + lit / (L_white * L_white)) / (vec3(1.0) + lit);

    // Swordigo color grade: +15% saturation + warm highlight push
    float lum = dot(lit, vec3(0.299, 0.587, 0.114));
    lit = mix(vec3(lum), lit, 1.15);
    lit += vec3(0.04, 0.02, 0.0) * clamp(lum - 0.45, 0.0, 0.55);

    // Linear-to-sRGB gamma output (1/1.6 curve matching desktop engine)
    lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 1.6));

    fragColor = vec4(lit, baseCol.a * uDiffuseColor.a);
}
)GLSL";

static const char* UNLIT_VS = R"GLSL(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uModelView;

void main() {
    gl_Position = uProj * (uModelView * vec4(aPos, 1.0));
}
)GLSL";

static const char* UNLIT_FS = R"GLSL(#version 300 es
precision mediump float;

uniform vec4 uColor;
out vec4 fragColor;

void main() {
    fragColor = uColor;
}
)GLSL";

static const char* OUTLINE_VS = R"GLSL(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;

uniform mat4 uProj;
uniform mat4 uModelView;
uniform float uOutlineOffset;

void main() {
    float nlen = length(aNorm);
    vec3 n = (nlen > 1e-5) ? (aNorm / nlen) : vec3(0.0, 1.0, 0.0);
    vec3 pos = aPos + n * uOutlineOffset;
    gl_Position = uProj * (uModelView * vec4(pos, 1.0));
}
)GLSL";

static const char* OUTLINE_FS = R"GLSL(#version 300 es
precision mediump float;

uniform vec4 uColor;
out vec4 fragColor;

void main() {
    fragColor = uColor;
}
)GLSL";

const char* GLESShaders::vertex_shader_source() {
    return GLES_VS;
}

const char* GLESShaders::fragment_shader_source() {
    return GLES_FS;
}

const char* GLESShaders::unlit_vertex_shader_source() {
    return UNLIT_VS;
}

const char* GLESShaders::unlit_fragment_shader_source() {
    return UNLIT_FS;
}

const char* GLESShaders::outline_vertex_shader_source() {
    return OUTLINE_VS;
}

const char* GLESShaders::outline_fragment_shader_source() {
    return OUTLINE_FS;
}

static const char* WATER_VS = R"GLSL(#version 300 es
precision highp float;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uMVP;

out vec2 vUV;
out vec3 vLocalPos;

void main() {
    vUV = aUV;
    vLocalPos = aPos;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* WATER_FS = R"GLSL(#version 300 es
precision highp float;

in vec2 vUV;
in vec3 vLocalPos;

uniform sampler2D uTexture;
uniform int       uHasTex;
uniform vec4      uFrontColor;
uniform vec4      uSurfaceColor;
uniform float     uScroll;
uniform float     uTime;
uniform int       uIsBackFace;
uniform int       uIsSelected;
uniform vec4      uSelectionColor;

out vec4 fragColor;

void main() {
    vec2 uv = vUV + vec2(uScroll, uScroll * 0.35);
    vec4 tex = (uHasTex != 0) ? texture(uTexture, uv) : vec4(1.0);

    vec4 baseColor = (uIsBackFace != 0) ? uSurfaceColor : uFrontColor;
    vec3 col = tex.rgb * baseColor.rgb;
    float a = baseColor.a * (uHasTex != 0 ? tex.a : 1.0);

    if (uIsBackFace != 0) {
        float caustic = sin(vLocalPos.x * 0.08 + uTime * 2.2) * cos(vLocalPos.z * 0.08 + uTime * 1.6) * 0.05;
        col += vec3(0.06 + max(0.0, caustic));
    } else {
        float crest = smoothstep(0.88, 1.0, 1.0 - vUV.y) * 0.15;
        col += vec3(crest);
    }

    // Uniform selection shading
    if (uIsSelected != 0) {
        vec3 selCol = uSelectionColor.rgb;
        float selAlpha = clamp(uSelectionColor.a, 0.0, 0.65);
        col = mix(col, selCol, selAlpha * 0.85);
        a = max(a, 0.55);
    }

    col = (col * (1.0 + col / 6.25)) / (1.0 + col);
    fragColor = vec4(clamp(col, 0.0, 1.0), clamp(a, 0.0, 1.0));
}
)GLSL";

const char* GLESShaders::water_vertex_shader_source() {
    return WATER_VS;
}

const char* GLESShaders::water_fragment_shader_source() {
    return WATER_FS;
}

} // namespace ruby::android
