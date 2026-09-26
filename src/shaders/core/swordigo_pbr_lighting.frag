// =============================================================================
//  SwordigoDesktop — Cook-Torrance GGX forward lighting (point / directional)
//  src/shaders/core/swordigo_pbr_lighting.frag
//
//  TARGET: Intel Xe LP/HP EU, OpenGL 3.3 core, fragment stage.
//
//  This file is a COMPLETE, STANDALONE fragment shader: it carries its own
//  `#version`, uniform block and `main()`, which is what makes
//  `glslangValidator -S frag` a real check rather than a token one.
//
//  The reusable part is everything between the LIGHTING CORE markers. If the host
//  wants to concatenate this into an existing program instead — the way
//  ffp_forward.cpp :: build_fragment_shader() concatenates the PDI kernel — copy
//  that block and drop this file's uniform/varying/main section.
//
// =============================================================================
//  EVIDENCE: what the engine's lights actually are
// =============================================================================
//  Recovered from OpenSwordigo/arm64_13 (IDA/Hex-Rays of libswordigo.so 1.4.13).
//
//  Caver::LightComponent::Enable(RenderingContext*, int index) @ 0x331DD0
//  discriminates on `*(this + 104)`:
//
//    == 1  global ambient:  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, colour*intensity)
//    == 2  directional:     position = normalize(*(this+128)) with w set to 0
//    == 3  positional:      position = *(this+152) rotated by angle(*(this+144))
//                           plus the owner's transform, w = 0x3F800000 (1.0)
//
//  and every branch is gated on `if (!*ctx)` — the GLES1 rendering API — which is
//  the only API the shipped game ever constructs (Java_..._setupApplication
//  @ 0x507DF4 hardcodes RenderingAPI == 0).
//
//  Five details from that single function drive the maths below:
//
//    * GL_DIFFUSE (4609)      = colour(+112) * intensity(+108).
//    * GL_SPECULAR (4610)     = a hardcoded (1,1,1,1). `FMOV V0.4S, #1.0` then
//                               glLightfv. Every light's specular colour is flat
//                               white; only per-material glMaterialfv varies.
//    * GL_AMBIENT (4608)      is NEVER set for a light, so it keeps its GL
//                               default (0,0,0,1). There is no per-light ambient
//                               term in this game, which is why the loop below
//                               has no light_ambients[] term at all.
//    * attenuation            glLightf(4616, *(this+140)) and
//                               glLightf(4617, *(this+144)) set
//                               GL_LINEAR_ATTENUATION and
//                               GL_QUADRATIC_ATTENUATION.
//                               GL_CONSTANT_ATTENUATION (4615) is never set and
//                               stays 1.0 exactly.
//    * NO SPOTLIGHTS          glLightf(GL_SPOT_EXPONENT = 4613) and
//                               glLightf(GL_SPOT_CUTOFF = 4614) are never called
//                               anywhere in the light path. Every light is a full
//                               360-degree point or an infinite directional. See
//                               SWORDIGO_LIGHT_SPOTS — the cone maths is dropped
//                               from the hot loop because of this, which removes a
//                               `pow` per light.
//
//  So the engine's attenuation is exactly
//        att = 1 / (1 + kLinear * d + kQuadratic * d^2)
//  which the host bridge already mirrors. Keeping that curve is the default here
//  (SWORDIGO_LIGHT_ATTEN_MODE 0) so the modern pass does not silently relight the
//  game; a physical inverse-square alternative is one define away.
//
// =============================================================================
//  ON "MAX_LIGHTS (32)"
// =============================================================================
//  Read the evidence before treating 32 as a game fact. There is no 32 anywhere
//  in the engine's light path:
//
//    * The guest's point-light list is UNBOUNDED. Caver::Scene::Draw @ 0x4F905C
//      constructs a PointLightManager, walks the LightComponent collection calling
//      PointLightManager::AddLight for every role-3 light, and hands the result to
//      LightOverlay::SetPointLights(ptr, count) with count == the manager's live
//      count. AddLight grows a FastVector on demand
//      (FastVector<PointLight>::Resize, 60-byte elements).
//
//    * The only HARD bound in the design is GL's, and it is 8.
//      LightComponent::Enable does glEnable(GL_LIGHT0 + index) with
//      GL_LIGHT0 == 0x4000, and fixed-function GL provides GL_LIGHT0..7.
//      ffp_forward.cpp hardcodes the same limit twice: `#define kMaxLights 8u`
//      and its `for (int i = 0; i < 8; i++)` state mirror.
//
//    * The engine ALREADY culls per screen region:
//      Caver::PointLightManager::GetOptimizedLights(Caver::Rectangle const&,
//      Caver::PointLight *, int) takes a rectangle and fills a caller-supplied
//      buffer, and its caller sits on LightOverlay's per-frame path. That is
//      per-tile light culling and it is the design to imitate.
//
//  32 is therefore a host-side BUDGET (src/graphics/LightManager.h declares
//  `static constexpr size_t MAX_LIGHTS = 32`), not a property of the game. That is
//  a legitimate choice, but it means the shader must not simply spin 32 times:
//  `u_lightCount` is the real loop bound, and the host is expected to fill it with
//  the lights that survived a rect test. An 8-light draw and a 32-light draw must
//  not execute the same instruction stream. See Entry 003 in .agents/COLLAB_LOG.md.
//
// =============================================================================
//  UNIFORM BUDGET — why the per-light layout is packed this way
// =============================================================================
//  GL 3.3 guarantees only 1024 fragment uniform components. A naive layout of
//  (position, radius, colour, direction, linear, quadratic, spotCos, spotExp)
//  costs 7-8 vec4 slots per light, so 32 lights would eat 256 slots before the
//  existing material and 8-light arrays in ffp_forward.cpp are counted at all.
//
//  Packing to 4 vec4 per light brings 32 lights to 128 slots (512 components),
//  which leaves room:
//
//      u_lightPosRadius[i]  .xyz = position          .w = radius (0 => directional)
//      u_lightColor[i]      .rgb = linear radiance
//      u_lightAtten[i]      .x   = GL constant atten  .y = linear  .z = quadratic
//      u_lightAtten[i]      .w   = spot exponent (unused while SPOTS is 0)
//      u_lightDir[i]        .xyz = direction (directional lights only)
//
// =============================================================================

#version 330 core

#define MAX_LIGHTS 32

// ---- Lighting core uniforms -------------------------------------------------
uniform int   u_lightCount;                    // <= MAX_LIGHTS, data-driven
uniform int   u_lightEnables[MAX_LIGHTS];      // GL light slot enable, 0/1
uniform vec4  u_lightPosRadius[MAX_LIGHTS];    // xyz position, w radius
uniform vec3  u_lightColor[MAX_LIGHTS];        // linear radiance
uniform vec4  u_lightAtten[MAX_LIGHTS];        // x const, y linear, z quadratic
uniform vec4  u_lightDir[MAX_LIGHTS];          // xyz direction, w unused

uniform vec3  u_lightAmbient;                  // GL_LIGHT_MODEL_AMBIENT
uniform float u_roughness;
uniform float u_metallic;
uniform float u_Wetness;                       // 0 = dry, 1 = rain-soaked
uniform float u_lightIntensityScale;

// ---- Surface inputs ---------------------------------------------------------
in vec3 v_viewPos;      // view/eye space
in vec3 v_viewNormal;   // view/eye space, normalized
in vec3 v_albedo;
out vec4 fragColor;

// =============================================================================
//                          LIGHTING CORE — BEGIN
// =============================================================================

const float SWORDIGO_PI = 3.14159265359;

// 0 = the engine's own attenuation curve, 1 = physical inverse-square.
// Default is the one with evidence behind it.
#define SWORDIGO_LIGHT_ATTEN_MODE   0

// Window the attenuation to exactly zero at the cull radius. The GL curve is
// asymptotic and never reaches zero, so without a window there is nothing to cull
// against and a light would contribute at any distance.
#define SWORDIGO_LIGHT_WINDOW       1

// Spotlight cones. Disabled by default because the guest never sets
// GL_SPOT_EXPONENT or GL_SPOT_CUTOFF (see the evidence block): every light is a
// full 360-degree point or an infinite directional, so the cone maths, the
// smoothstep and a `pow` per light are all dead weight in the hot loop. Turn this
// on only for a host that synthesises cones of its own.
#define SWORDIGO_LIGHT_SPOTS        0

// HLSL-style Lambert uses 1/PI. The engine's fixed-function look does not.
// Leaving this at 1 makes the pass physically consistent and costs a single
// constant; the host compensates the overall brightness with
// u_lightIntensityScale. Set to 0 to match the legacy loop's brightness directly.
#define SWORDIGO_DIFFUSE_DIV_PI     1

// -----------------------------------------------------------------------------
//  Microfacet terms, written for Xe
// -----------------------------------------------------------------------------
//  Three algebraic changes against the textbook spelling. All three are pure
//  instruction-count reductions and none changes the result beyond fp rounding.
//
//  1. Fresnel's pow(x, 5.0) -> x2*x2*x.
//     GLSL pow is exp2(y * log2(x)): on Xe a LOG2, a multiply and an EX2, three
//     multi-cycle transcendentals. Three multiplies is two ALU ops after the first
//     square. F_Schlick runs per light per fragment, so at 32 lights the old form
//     was roughly 200 wasted instructions per fragment.
//
//  2. Every `a / max(b, eps)` -> `a * (1.0 / max(b, eps))`.
//     This is what the hardware does anyway, but writing it explicitly lets one
//     reciprocal serve several numerators — which is exactly what the two-lobe
//     clearcoat path needs.
//
//  3. The Smith visibility collapses. This is the interesting one, because the
//     usual textbook form carries a factor that annihilates.
//
//         spec = D * G * F / (4 * NdotV * NdotL)
//         G    = Gv * Gl,  Gv = NdotV / (NdotV*(1-k) + k),  Gl = NdotL / (NdotL*(1-k) + k)
//
//     Let gv, gl be the two denominators. Then
//
//         Gv * Gl = NdotV * NdotL / (gv * gl)
//         G / (4 NdotV NdotL) = 1 / (4 * gv * gl)
//
//     The NdotV * NdotL in the numerator cancels exactly against the microfacet
//     Jacobian. So the whole visibility factor is one reciprocal, not two
//     divisions, and it is NdotV*NdotL-free — which also means the loop never has
//     to guard that product against zero. NdotL = 0 needs no special case:
//     the light contribution is killed by the NdotL factor in `att` instead.

vec3 swordigo_fresnel_schlick(float cosTheta, vec3 F0) {
    float x  = clamp(1.0 - cosTheta, 0.0, 1.0);
    float x2 = x * x;
    return F0 + (vec3(1.0) - F0) * (x2 * x2 * x);
}

// GGX / Trowbridge-Reitz. `a2` is alpha^2 = roughness^4, loop-invariant.
float swordigo_D_ggx(float NdotH, float a2) {
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 * (1.0 / max(d * d * SWORDIGO_PI, 1e-7));
}

// Smith-Schlick visibility with the Jacobian folded in: returns
// G / (4 NdotV NdotL). Multiply by D and F to get the specular lobe directly.
// `k` is loop-invariant.
float swordigo_vis_kGGX(float NdotV, float NdotL, float k) {
    float gv = NdotV * (1.0 - k) + k;
    float gl = NdotL * (1.0 - k) + k;
    return 1.0 / max(4.0 * gv * gl, 1e-7);
}

// -----------------------------------------------------------------------------
//  Rain wetness
// -----------------------------------------------------------------------------
//  A wet surface is a thin water film over the base material, not a different
//  material. Three physical effects, applied branchlessly so u_Wetness can be
//  animated continuously — rain onset in Floranum drives it toward 0.45 — with no
//  perceptible state change:
//
//  (a) ROUGHNESS REDUCTION. The film fills micro-cavities, so the visible
//      interface becomes the much smoother water surface. The correct move is to
//      blend toward the film's own roughness, not to scale the base roughness by a
//      constant: a rough rock and a smooth tile converge to the same wet
//      appearance, which is why puddles look uniform across mixed terrain.
//      A floor of 0.03 keeps a trace of ripple.
//
//  (b) DIFFUSE DARKENING AND SATURATION. This is the effect that gets forgotten,
//      and leaving it out is why naive "wet = shinier" shaders read as plastic.
//      A water film over a dielectric reflects a few percent specularly and
//      transmits the rest into a higher-index medium; the returning subsurface
//      light is both dimmer and more chromatic. Net: albedo drops, saturation
//      rises. Without it, a wet surface reads as BRIGHTER, not wetter.
//
//  (c) CLEARCOAT SHEEN. A separate specular lobe for the film, using the
//      KHR_materials_clearcoat two-layer formulation: the base lobe is attenuated
//      by the film's Fresnel transmittance (1 - Fcc) and the film lobe is added on
//      top. This is what produces the bright grazing-angle streak that reads as
//      "wet" more than anything else, because Fcc -> 1 as the view grazes.
//
//  F0 for the water lobe is 0.02037, NOT the 0.04 of a lacquer clearcoat.
//  Schlick's F0 = ((ior-1)/(ior+1))^2:
//        water   (1.333): ((0.333)/(2.333))^2 = 0.02037
//        lacquer (1.500): ((0.500)/(2.500))^2 = 0.04000
//  Using 0.04 would make the rain film twice as reflective as water and bias every
//  wet surface toward looking lacquered.
#define SWORDIGO_WATER_F0            0.02037
#define SWORDIGO_WATER_ALPHA         0.03
#define SWORDIGO_WET_ROUGH_SCALE     0.35   // wet rock keeps some of its structure
#define SWORDIGO_WET_DIFFUSE_SCALE   0.55   // (b)
#define SWORDIGO_WET_SAT_BOOST       0.35   // (b)
#define SWORDIGO_WET_F0_PULL         0.35   // how far the base F0 is nudged wetward

struct SwordigoSurface {
    vec3  albedo;     // already darkened and saturated for the wetness
    vec3  F0;         // base-layer normal-incidence reflectance
    float a2Base;     // alpha^2 for D_ggx of the base layer
    float kBase;      // Smith k of the base layer
    float wetness;    // clamped u_Wetness
};

SwordigoSurface swordigo_build_surface(vec3 albedo) {
    SwordigoSurface s;
    s.wetness = clamp(u_Wetness, 0.0, 1.0);

    float rough = clamp(u_roughness, 0.03, 1.0);

    // (a) blend toward the water film's roughness
    float roughWet = mix(rough,
                         max(SWORDIGO_WATER_ALPHA, rough * SWORDIGO_WET_ROUGH_SCALE),
                         s.wetness);

    // (b) darken and saturate. luma is a dot product, not a texture fetch.
    float luma      = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
    vec3  saturated = mix(vec3(luma), albedo, 1.0 + SWORDIGO_WET_SAT_BOOST * s.wetness);
    s.albedo        = max(saturated * mix(1.0, SWORDIGO_WET_DIFFUSE_SCALE, s.wetness),
                          vec3(0.0));

    // F0: the dielectric base, nudged toward water but never reaching it — the
    // film itself is a separate lobe and must not be double counted here.
    vec3 baseF0 = mix(vec3(0.04), s.albedo, clamp(u_metallic, 0.0, 1.0));
    s.F0 = mix(baseF0, vec3(SWORDIGO_WATER_F0), s.wetness * SWORDIGO_WET_F0_PULL);

    float a = roughWet * roughWet;
    s.a2Base = a * a;                       // roughness^4, which is what D wants
    s.kBase  = (roughWet + 1.0) * (roughWet + 1.0) / 8.0;
    return s;
}

// -----------------------------------------------------------------------------
//  The light loop
// -----------------------------------------------------------------------------
//  Everything below is branchless. That is a real choice with a real cost, so
//  being explicit about what it buys matters.
//
//  WHAT IT DOES NOT BUY: a branchless cull does NOT skip the BRDF evaluation.
//  Computing step(distSq, radiusSq) and multiplying the result by zero still pays
//  for GGX, Smith and Fresnel. The phrase "branchless culling" conflates two
//  things, and only one of them removes work:
//
//    * SUPPRESSION — the light cannot affect the pixel. Branchless, free, and it
//                    is what stops a light popping as it crosses the radius.
//    * ELIMINATION — the light is not evaluated at all. Needs either a branch or,
//                    better, never having been handed the light.
//
//  WHAT IT DOES BUY: no divergence. The alternative is
//  `if (distSq < radiusSq)`. To be fair to it, that predicate is spatially
//  COHERENT — a light's radius projects to a contiguous screen region, so only the
//  boundary pixels of each footprint diverge — which makes the branch genuinely
//  defensible. It is not implemented here, but set the loop bound honestly and the
//  question mostly stops mattering.
//
//  WHERE THE REAL SAVING IS: not culling inside the shader, but passing a bound
//  that already reflects culling done where it is cheap. `u_lightCount` is that
//  lever.
vec3 swordigo_accumulate_lights(vec3 P, vec3 N, vec3 V, SwordigoSurface surf) {
    float NdotV = max(dot(N, V), 1e-4);
    float metallic = clamp(u_metallic, 0.0, 1.0);
    float count = float(u_lightCount);

    vec3 accum = surf.albedo * u_lightAmbient;

    // Clearcoat invariants: the film is smooth regardless of what is under it,
    // which is the entire point of (a).
    float a2_cc = SWORDIGO_WATER_ALPHA * SWORDIGO_WATER_ALPHA;
    float k_cc  = (SWORDIGO_WATER_ALPHA + 1.0) * (SWORDIGO_WATER_ALPHA + 1.0) / 8.0;
    // Fcc at the macro normal, constant across the loop: Schlick of NdotV.
    float xv  = 1.0 - NdotV;
    float xv2 = xv * xv;
    float Fcc = SWORDIGO_WATER_F0
              + (1.0 - SWORDIGO_WATER_F0) * (xv2 * xv2 * xv);

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        // Bound as a multiply rather than a branch. Uniform bounds on Xe are
        // already cheap, and keeping the body straight-line lets adjacent
        // iterations interleave their independent chains.
        float live = (float(i) < count) ? 1.0 : 0.0;
        live *= float(u_lightEnables[i]);

        vec4  pr    = u_lightPosRadius[i];
        float isPos = step(1e-6, pr.w);          // 1.0 point, 0.0 directional

        // ---- direction, branchless ----------------------------------------
        vec3  toL  = pr.xyz - P;
        float d2   = dot(toL, toL);
        float rcpD = inversesqrt(max(d2, 1e-8));
        vec3  L    = normalize(mix(u_lightDir[i].xyz, toL * rcpD, isPos));

        // ---- branchless distance-squared cull ------------------------------
        // t == 1.0 exactly on the radius, and the window below vanishes at and
        // beyond it, so the sphere edge is the cull boundary.
        float radiusSq = max(pr.w * pr.w, 1e-8);
        float t        = d2 * (1.0 / radiusSq);

#if SWORDIGO_LIGHT_WINDOW
        // C1 at the edge: with w = saturate(1 - t^2)^2, w(1) = 0 AND w'(1) = 0,
        // so a light entering or leaving the radius cannot pop.
        float u   = clamp(1.0 - t * t, 0.0, 1.0);
        float win = u * u;
#else
        float win = 1.0;
#endif

#if SWORDIGO_LIGHT_ATTEN_MODE == 0
        // The engine's own curve. GL_CONSTANT_ATTENUATION is never written by the
        // guest and stays 1.0, so the coefficient is a uniform rather than a
        // literal only to keep the host free to change that.
        float d    = d2 * rcpD;
        float attP = 1.0 / (u_lightAtten[i].x
                          + u_lightAtten[i].y * d
                          + u_lightAtten[i].z * d2 + 1e-9);
#else
        // Physical inverse-square, floored by the light's own radius so it stays
        // finite at the surface.
        float attP = radiusSq * (1.0 / max(d2, 1e-6));
#endif

        float att = mix(1.0, attP * win, isPos);

#if SWORDIGO_LIGHT_SPOTS
        vec3  spotAxis = normalize(u_lightDir[i].xyz);
        float spotDot  = clamp(dot(-L, spotAxis), 0.0, 1.0);
        float spotCos  = min(u_lightDir[i].w, 0.9999);
        // A soft cone edge instead of the hard GL cutoff: no aliasing on a moving
        // light, and no branch.
        float spot = smoothstep(spotCos, mix(spotCos, 1.0, 0.35), spotDot)
                   * pow(max(spotDot, 1e-4), max(u_lightAtten[i].w, 1.0));
        att *= mix(1.0, spot, isPos);
#endif

        // ---- shade ---------------------------------------------------------
        float NdotL = max(dot(N, L), 0.0);
        att *= NdotL * live;

        vec3  H    = normalize(V + L);
        float NdH  = max(dot(N, H), 0.0);
        float VdH  = max(dot(V, H), 0.0);

        float D   = swordigo_D_ggx(NdH, surf.a2Base);
        float Vis = swordigo_vis_kGGX(NdotV, NdotL, surf.kBase);
        vec3  F   = swordigo_fresnel_schlick(VdH, surf.F0);

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
#if SWORDIGO_DIFFUSE_DIV_PI
        kD *= (1.0 / SWORDIGO_PI);
#endif
        vec3 radiance = u_lightColor[i].rgb * att;
        vec3 base = (kD * surf.albedo + F * (D * Vis)) * radiance;

        // (c) two-layer clearcoat. Fcc is the film's Fresnel at the macro normal,
        // computed once outside the loop. Energy conservation — whatever the film
        // reflects is removed from the layer beneath — is what makes this read as
        // a film rather than as extra gloss.
        float Dcc  = swordigo_D_ggx(NdH, a2_cc);
        float VisC = swordigo_vis_kGGX(NdotV, NdotL, k_cc);
        vec3  film = radiance * (Dcc * VisC * Fcc);

        vec3 wet = base * (1.0 - surf.wetness * Fcc) + film;

        // One mix, with the weight applied exactly once. (An earlier draft mixed
        // in `wet` while `wet` already carried the weight, which squares it.)
        accum += mix(base, wet, surf.wetness);
    }

    return accum * u_lightIntensityScale;
}

// =============================================================================
//                          LIGHTING CORE — END
// =============================================================================

// -----------------------------------------------------------------------------
//  Standalone driver. Drop this when concatenating the core above into the host
//  program's own fragment shader.
// -----------------------------------------------------------------------------
void main() {
    vec3 N = normalize(v_viewNormal);
    vec3 V = normalize(-v_viewPos);

    SwordigoSurface surf = swordigo_build_surface(v_albedo);
    vec3 lit = swordigo_accumulate_lights(v_viewPos, N, V, surf);

    fragColor = vec4(lit, 1.0);
}
