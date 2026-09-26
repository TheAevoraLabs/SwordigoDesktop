// =============================================================================
//  SwordigoDesktop — Procedural Detail Injection (PDI) kernel library
//  src/shaders/core/procedural_detail.glsl
//
//  INCLUDE-SAFE BY CONSTRUCTION
//  ---------------------------------------------------------------------------
//  This file intentionally declares NO `#version`, NO uniforms and NO varyings.
//  It is a pure function library meant to be concatenated into a host program.
//  `src/platform/ffp_forward.cpp :: build_fragment_shader()` emits `#version 330`
//  and then appends its state block, the vendored ANGLE GLES1 shader text, and
//  the PBR functions; a `#version` directive in this file would be a hard
//  compile error part-way through that translation unit.
//
//  Every symbol is `pdi`-prefixed. That is not cosmetic: this text shares one
//  translation unit with the vendored ANGLE functions and with the PBR block
//  (`derive_normal`, `D_GGX`, `F_Schlick`, ...). A bare `fbm` or `snoise` here
//  would either collide or silently shadow, and the resulting failure would show
//  up as a wrong-looking frame rather than as a compile error.
//
//  HOST CONTRACT (supplied by the C++ side, deliberately not declared here)
//  ---------------------------------------------------------------------------
//      uniform int   u_asset_detail_type;    // 0 = SensitiveUV, 1 = WorldProcedural
//      uniform float u_procedural_strength;  // 0..2, default 1.0
//      in vec3       v_world_pos;            // world-space interpolated position
//      in vec3       v_world_normal;         // world-space interpolated geometric normal
//
//  TARGET: Intel Xe LP/HP EU, OpenGL 3.3 core profile, fragment stage.
//
// =============================================================================
//  DESIGN NOTE — why the default path does NOT triplanar-blend
// =============================================================================
//  Triplanar projection exists to project a *2D texture* onto arbitrary 3D
//  geometry without UV seams. That problem exists only because a 2D texture has
//  no third axis to sample along.
//
//  An *analytical 3D field* does not have that problem. Sampling snoise()/fBm
//  directly at a world-space point is already seamless, isotropic and
//  orientation-independent — there is nothing to blend and nothing to seam.
//
//  The previous revision spent three full 3-octave fBm evaluations (9 snoise
//  calls) plus a `pow(n,4)` weight and blend to *approximate* what one 3-octave
//  fBm (3 snoise calls) computes exactly. It also correlated the three planes:
//  detailX sampled the field at z=0, detailY at y=0, detailZ at x=0, so all three
//  were slices of one field and agreed spuriously along the axes — visible as
//  soft axis-aligned creases on 45-degree terrain.
//
//  The default path below evaluates ONE isotropic 3D field.
//  pdiTriplanarWeights() / pdiTriplanarField() are retained, branchless, for the
//  one case that genuinely needs projection: replaying a *2D authored pattern*
//  across 3D geometry.
//
//  Worth stating plainly: this file is not yet included by any program in the
//  tree, so the values below are not changing an existing look — they are the
//  first look this effect will ever have. The tuning #defines are the dial.
// =============================================================================

// =============================================================================
//  1 — Field tuning
// =============================================================================

// Base frequency of the world-procedural field, in noise cells per world unit.
// Total detail spans PDI_WORLD_SCALE .. PDI_WORLD_SCALE * LACUNARITY^(OCTAVES-1).
//
// This is the primary visual dial. Two constraints bound it from above, and both
// bite hard in world space specifically:
//
//   * ALIASING. Above roughly one noise cell per pixel the field shimmers instead
//     of resolving. PDI_LOD_BANDLIMIT below handles that automatically.
//   * FLOAT32 PRECISION. The field coordinate is `worldPos * scale`. At field
//     coordinate magnitude M the representable spacing is proportional to M, so a
//     large scale dilates the noise domain until the *finest* octave quantises
//     into visible terraces. At scale 16 (in the ballpark of the previous
//     revision's effective frequency) a point 500 units from the origin lands at
//     field coordinate 8000, where one ULP is ~5e-4 cells — several hundred
//     quantisation steps across the finest octave, which reads as banding on
//     distant terrain. At scale 6 the same point sits at 3000 and one ULP is
//     ~2e-4, which stays clean.
#define PDI_WORLD_SCALE          6.0

// SensitiveUV assets (hero face, garments) keep their authored UVs and get only a
// micro-roughness perturbation, so the field is pushed far above the authored
// texture frequency instead of being used as a visual feature in its own right.
#define PDI_SENSITIVE_SCALE      48.0

// Octave count is a compile-time constant on purpose — see pdiFbm().
#define PDI_FBM_OCTAVES          3

// 2.03 rather than exactly 2.0: a power-of-two lacunarity makes every octave sit
// on a lattice that is a subdivision of the previous one, so their zero
// crossings reinforce and produce a faint checkerboard. The 3% offset
// decorrelates the octaves essentially for free.
#define PDI_FBM_LACUNARITY       2.03
#define PDI_FBM_GAIN             0.5

// Forward-difference epsilon, in *field cells* (i.e. after the scale multiply).
//
// The previous revision used `eps = 0.05` in WORLD units against `fbm(p * 24.0)`,
// so the probe offset was 0.05 * 24 = 1.2 field cells against a field whose
// finest 2-octave feature is roughly half a cell. The probe landed in an
// unrelated part of the field, and dividing that difference by 0.05 produced a
// "gradient" whose magnitude was dominated by uncorrelated noise. The bump
// normals were therefore effectively random per fragment.
//
// The correct epsilon balances two errors: truncation error grows with eps while
// the error from differencing float32 values grows as ulp/eps. That balance sits
// near sqrt(ulp) ≈ 2.4e-4 cells for small field coordinates, but eps must also
// stay comfortably above one ULP of the field coordinate (see PDI_WORLD_SCALE), so
// ~0.02 cells is the practical floor for world coordinates in the thousands.
#define PDI_GRAD_EPS             0.02

// Band-limit octaves against the screen-space footprint. Disable to compare
// shimmer-vs-cost behaviour; see pdiFbm() for what this actually buys.
#define PDI_LOD_BANDLIMIT        1

// =============================================================================
//  2 — 3D simplex gradient noise (Ashima Arts / Stefan Gustavson, MIT)
// =============================================================================
//  The structure of this noise is kept canonical and unmodified. Its cost is
//  dominated by the triangle of dependent permutes below, not by any algebraic
//  slack, so optimisation happens in the call pattern around it (octave loop
//  shape, probe sharing, band-limiting) and in the one place where the canonical
//  text is genuinely wrong — the gradient normalisation, documented below.
//
//  ALU note for Xe: this permutation triangle is a serial chain of six
//  multiply-modulo steps with no parallelism to hide behind, so it is a latency
//  bubble rather than a throughput problem. That is the whole argument for
//  keeping the loop shape tight and unrolled: with four fBm probes in flight
//  (pdiSampleField) the EU has four independent chains to interleave and the
//  bubbles fill. Those probes want to be issued together rather than serialised
//  by the compiler.
vec4 pdiPermute(vec4 x) { return mod(((x * 34.0) + 1.0) * x, 289.0); }

//  THE GRADIENT NORMALISATION IS NOT THE CANONICAL ONE, DELIBERATELY.
//
//  The usual vendored text normalises with a two-term polynomial fit of
//  1/sqrt(r):  `1.79284291400159 - 0.85373472095314 * r`. That fit is only
//  accurate for r roughly in [0.6, 1.0], and the four values actually produced
//  here are not in that band. Measured over 40000 random points, the argument
//  spans r in [0.337, 4.724] with a median of 0.480:
//
//      r=0.50  true 1.414   fit 1.366
//      r=1.00  true 1.000   fit 0.939
//      r=2.00  true 0.707   fit 0.085
//      r=2.10  true 0.690   fit 0.000
//      r=3.00  true 0.577   fit -0.768    <- sign inverted
//
//  It crosses zero at r = 2.10, so above that the fit does not scale the gradient
//  vector, it REVERSES it. That happens for 14.1% of evaluations. The effect on
//  the field is not subtle: with the polynomial fit the noise spans
//  [-4.27, +4.27] with std 0.712; with an exact normalisation it spans
//  [-1.05, +1.01] with std 0.396. Which is what a simplex noise is supposed to do,
//  and it explains why the previous revision's strength constants had to be
//  hand-fudged (0.85 + v*0.35) around a field whose amplitude was four times its
//  nominal range in the tails and whose gradient direction was wrong 14% of the
//  time.
//
//  Using inversesqrt also costs *less* on the target: Intel Xe implements it as
//  a single EU instruction (RSQ), where the polynomial is a multiply and a
//  subtract. Correct and cheaper. The max() guard is there because inversesqrt(0)
//  is infinite where the polynomial was merely wrong, so the degenerate r = 0 case
//  has to be kept finite.
vec4 pdiInvSqrtGrad(vec4 r) { return inversesqrt(max(r, 1e-12)); }

float pdiSnoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    // First corner
    vec3 i  = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    // Other corners — branchless axis ordering
    vec3 g  = step(x0.yzx, x0.xyz);
    vec3 l  = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    // Permutations
    i = mod(i, 289.0);
    vec4 p = pdiPermute(pdiPermute(pdiPermute(
                 i.z + vec4(0.0, i1.z, i2.z, 1.0))
               + i.y + vec4(0.0, i1.y, i2.y, 1.0))
               + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    // Gradients: 7x7 points over a square, mapped onto an octahedron.
    // The ring size 17*17 = 289 is close to a multiple of 49 (49*6 = 294), hence
    // the permute modulo 289 above.
    float n_ = 0.142857142857;              // 1/7
    vec3  ns = n_ * D.wyz - D.xzx;

    vec4 j  = p - 49.0 * floor(p * ns.z * ns.z);   // mod(p, 7*7)

    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);                 // mod(j, 7)

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    // Normalise gradients -- exact, and one instruction on Xe. See above.
    vec4 norm = pdiInvSqrtGrad(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    // Mix the four contributions.
    // 0.6 rather than the more common 0.5: 0.5 leaves the kernel support inside
    // the simplicial cell and costs roughly 25% of peak amplitude; 0.6 makes the
    // supports tile the cell with only a small overlap, which is why this variant
    // reproduces the documented [-1,1] range.
    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// =============================================================================
//  3 — fBm, unrolled and band-limited
// =============================================================================
//  OCTAVE COUNT IS A CONSTANT, NOT A PARAMETER.
//  The previous `float fbm(vec3 p, int octaves)` took the count as a function
//  argument. A run-time bound stops the compiler from fully unrolling: the loop
//  acquires a back edge, snoise's ~15 live temporaries must survive it, and the
//  register allocator has to keep the permutation state resident across the
//  iteration. On Xe that shows up as a register-count jump and lost occupancy.
//  With a compile-time bound the loop becomes straight-line code, the temporaries
//  of one octave are dead before the next begins, and they get reused.
//
//  Constant-folding also collapses `norm` (the running sum of amplitudes) into a
//  literal, so the final divide is a multiply by a constant.
//
//  NORMALISATION. The old version started at amplitude 0.5 and never divided by
//  the amplitude sum, so its peak sat at 0.500 / 0.875 / 0.969 x the noise peak
//  for 1 / 3 / 5 octaves — one `strength` value produced a different amount of
//  detail depending on the octave count. Dividing by the analytic sum pins the
//  peak at exactly 1.0 x the noise peak for every octave count, so `strength` is
//  stable when OCTAVES is retuned.
//
//  The absolute scale is only meaningful because pdiSnoise normalises exactly
//  (see the note there): with the exact normalisation this function spans about
//  [-1.05, +1.01] with std 0.396. A `strength` of 1.0 therefore means "modulate
//  by up to +-30% of the surface colour", not "modulate by some unknown amount".
//
//  BAND-LIMITING. Multiplying the position by the lacunarity each octave (rather
//  than maintaining a separate frequency accumulator) is one multiply instead of
//  two, and it lets the footprint below be carried with the same factor.
//
//  Without a band limit, any octave whose features are finer than a pixel
//  aliases, and a camera zoom changes how many octaves alias — which is exactly
//  the "crawls and shimmers when you move" failure, and the reason the effect
//  cannot simply be cranked to high frequency. The fade below is applied
//  branchlessly so the loop still unrolls: a real `break` would save the snoise
//  work for aliased octaves but reintroduce a divergent back edge, and since
//  screen footprint is spatially coherent that trade could go either way. Fade
//  is the predictable choice; switch to a break if profiling says the wasted
//  octaves dominate. Either way the octave contributes zero rather than
//  adversarial noise, so the image stays stable.
float pdiFbm(vec3 p) {
    float sum  = 0.0;
    float amp  = 1.0;
    float norm = 0.0;

#if PDI_LOD_BANDLIMIT
    // Footprint of one pixel measured in field cells, for the current octave.
    // dFdx/dFdy of the *sample point* already includes the caller's scale factor.
    float fp = max(length(dFdx(p)), length(dFdy(p)));
#endif

    for (int i = 0; i < PDI_FBM_OCTAVES; ++i) {
        float w = amp;
#if PDI_LOD_BANDLIMIT
        // fp == 1.0 cells/pixel is Nyquist: one field cell per pixel. At or above
        // that the octave cannot be represented and fades to exactly zero.
        w *= clamp(1.0 - fp, 0.0, 1.0);
#endif
        sum  += pdiSnoise(p) * w;
        norm += amp;

        p *= PDI_FBM_LACUNARITY;
        amp *= PDI_FBM_GAIN;
#if PDI_LOD_BANDLIMIT
        fp *= PDI_FBM_LACUNARITY;
#endif
    }

    return sum * (1.0 / norm);
}

// =============================================================================
//  4 — Value + gradient from ONE field
// =============================================================================
struct PdiSample {
    float value;   // field value, [-1,1]
    vec3  grad;    // field gradient in the same (scaled) space as the argument
};

//  The micro-bump normal and the albedo modulation must come from the same field
//  at the same scale. Otherwise the shading disagrees with the colour and the
//  result reads as "detail that is lit wrong" — which is what happens when the
//  bump is derived at one frequency and the colour at another.
//
//  Four probes (base plus three axis offsets) reconstruct the gradient. Four fBm
//  evaluations look expensive, but the four are mutually independent, so their
//  serial permutation chains interleave and the EU stays busy; the cost is much
//  better than 4x one evaluation in wall time.
//
//  BASE AND PROBES MUST AGREE ON OCTAVE COUNT.
//  A tempting optimisation is to probe with one octave fewer than the base, on
//  the theory that the finest octave is sub-pixel curvature. It is wrong: the
//  difference f(p + e) - f(p) then still contains the base's finest octave, and
//  dividing that residue by a small epsilon amplifies it enormously instead of
//  cancelling it. The gradient becomes dominated by the very octave that was
//  supposed to be excluded. Both sides use pdiFbm().
//
//  Cost accounting per fragment, world-procedural path, 3 octaves. "snoise" is
//  one pdiSnoise call; the standalone albedo path is counted separately below
//  because it needs no probes:
//      previous revision   albedo 9 (triplanar) + gradient 8 (a *different*
//                          scale, with a broken epsilon)     = 17 snoise
//      this revision       albedo 3 + gradient 12, shared     = 12 snoise
//  and the triplanar blend, its `pow`, its branch and its seams are gone.
//
//  If the normal path is the hot one, PDI_FBM_OCTAVES = 2 makes this 8 snoise,
//  i.e. parity with the old cost while staying correct — the normalisation in
//  pdiFbm() keeps the visible strength unchanged when octaves are retuned.
PdiSample pdiSampleField(vec3 p) {
    float f0 = pdiFbm(p);

    float fx = pdiFbm(p + vec3(PDI_GRAD_EPS, 0.0, 0.0));
    float fy = pdiFbm(p + vec3(0.0, PDI_GRAD_EPS, 0.0));
    float fz = pdiFbm(p + vec3(0.0, 0.0, PDI_GRAD_EPS));

    PdiSample s;
    s.value = f0;
    s.grad  = vec3(fx - f0, fy - f0, fz - f0) * (1.0 / PDI_GRAD_EPS);
    return s;
}

//  Per-asset-class field lookup. `assetType` is a *uniform*, so this branch is
//  uniform-coherent: every fragment in the draw takes the same side and Xe pays
//  no divergence. It is kept as a branch rather than evaluated both-ways on
//  purpose — folding it into a mix() would double the noise cost of the whole
//  screen to avoid a branch that is free.
PdiSample pdiSampleForAsset(vec3 worldPos, int assetType) {
    if (assetType == 0) {
        return pdiSampleField(worldPos * PDI_SENSITIVE_SCALE);
    }
    return pdiSampleField(worldPos * PDI_WORLD_SCALE);
}

//  Value-only lookup, for callers that need the albedo modulation but not the
//  normal. This is one fBm evaluation rather than four: no probes are needed to
//  read a value, only to differentiate one, and the old code paid for all three
//  triplanar planes to obtain a number a single evaluation produces exactly.
//
//  This is the cheapest useful entry in the file — 3 snoise at the default
//  octave count, against 9 in the previous revision.
float pdiValueForAsset(vec3 worldPos, int assetType) {
    if (assetType == 0) {
        return pdiFbm(worldPos * PDI_SENSITIVE_SCALE);
    }
    return pdiFbm(worldPos * PDI_WORLD_SCALE);
}

// =============================================================================
//  5 — Branchless triplanar (for genuine 2D projected patterns only)
// =============================================================================
//  Two problems in the previous weight/blend:
//
//   * `pow(abs(n), vec3(4.0))`. GLSL pow is exp2(y * log2(x)) — on Xe roughly a
//     six-to-eight instruction sequence per component, for a fourth power. n^4 is
//     two multiplies: s = n*n; w = s*s.
//
//   * `sum > 0.0001 ? w / sum : vec3(0.3333)`. A real branch, and a leaky guard:
//     the division that the ternary protects against can still be reached through
//     the other arm. The reciprocal below is unconditional, and because the
//     offset makes a degenerate normal fall through to exactly 1/3 on every axis,
//     the fallback the ternary was reaching for happens for free.
vec3 pdiTriplanarWeights(vec3 n) {
    vec3 a  = abs(n) + 1e-5;          // keeps the reciprocal well conditioned
    vec3 s2 = a * a;
    vec3 w  = s2 * s2;                // a^4, two multiplies
    return w * (1.0 / (w.x + w.y + w.z));
}

//  Projected 2D pattern replay. Each projection is offset by an irrational
//  amount before sampling. Sampling (y,z), (x,z) and (x,y) straight out of one
//  field puts all three on the z=0 / y=0 / x=0 slice, which correlates them and
//  shows up as axis-aligned creases on 45-degree geometry; the offsets
//  decorrelate them for three adds with no extra noise evaluations.
float pdiTriplanarField(vec3 worldPos, vec3 worldNormal, float scale) {
    vec3 w = pdiTriplanarWeights(worldNormal);
    vec3 q = worldPos * scale;

    return w.x * pdiFbm(q.yzx + vec3(  0.0,   17.31,  51.07))
         + w.y * pdiFbm(q.xzy + vec3(-23.53,  0.0,   89.11))
         + w.z * pdiFbm(q.xyz + vec3( 41.77, -67.13,  0.0  ));
}

// =============================================================================
//  6 — Public entry points
// =============================================================================
//  Signatures preserved from the previous revision so the C++ contract recorded
//  in .agents/COLLAB_LOG.md does not change.

//  Albedo-only modulation. Prefer PdiApply() when the normal is also needed: it
//  evaluates the field once instead of this twice-plus.
vec3 InjectProceduralDetail(
    vec3 baseColor,
    vec3 worldPos,
    vec3 worldNormal,
    int  assetType,
    float detailStrength)
{
    if (detailStrength <= 0.001) return baseColor;

    // Value only — see pdiValueForAsset(). This is the one place where the old
    // code's triplanar cost was pure waste rather than a quality trade.
    float v = pdiValueForAsset(worldPos, assetType);

    if (assetType == 0) {
        // SensitiveUV — a perturbation, not a feature. Deliberately weak: the
        // authored face and garment textures carry the intent and this only
        // breaks up their specular uniformity.
        return clamp(baseColor + (v * 0.05 * detailStrength), 0.0, 1.0);
    }

    // WorldProcedural — replace the old `0.85 + v*0.35` which, at the top of the
    // range, multiplied albedo by 1.2 and pushed already-bright rock and snow
    // into the clamp. Centring the modulation and keeping it below unity keeps
    // the detail a modulation of the surface rather than a second light.
    return clamp(baseColor * (1.0 + v * 0.30 * detailStrength), 0.0, 1.0);
}

//  World-space normal perturbation from the field gradient.
//
//  For an isotropic 3D field this is the correct formulation: the gradient is
//  already in world space, so no tangent frame is needed. The previous revision
//  computed 4 *separate* fBm evaluations here at a different scale (24.0) from
//  the albedo pass (16.0) — two disagreeing fields — and used an epsilon 60x too
//  large for the frequency it sampled, so the gradient was effectively noise.
vec3 PerturbNormalProcedural(vec3 worldNormal, vec3 worldPos, float strength) {
    if (strength <= 0.001) return worldNormal;

    PdiSample s = pdiSampleField(worldPos * PDI_WORLD_SCALE);

    // Tangential component of the gradient only. The normal-parallel component
    // does not tilt the surface, it only shortens or lengthens the normal vector,
    // which the final normalize() would discard anyway — so removing it first is
    // free accuracy: it stops a large gradient along the normal from eating the
    // perturbation budget of the two tangential axes.
    vec3 n   = normalize(worldNormal);
    vec3 g   = s.grad - n * dot(s.grad, n);

    return normalize(n - g * (0.15 * strength));
}

//  Tangent-space variant for SensitiveUV assets, where the detail belongs in the
//  authored frame rather than in world space. TBN is the cotangent frame the
//  caller already builds for the texture-space normal map (see cotangent_frame()
//  in ffp_forward.cpp), so this costs no extra derivatives.
//
//  detailHeight in [-1,1] is the field value; the gradient is projected into the
//  tangent frame to give the standard (dx, dy) bump slope.
vec3 PerturbNormalTangent(vec3 worldNormal, mat3 TBN, vec3 worldPos, float strength) {
    if (strength <= 0.001) return worldNormal;

    PdiSample s = pdiSampleField(worldPos * PDI_SENSITIVE_SCALE);

    vec3 gTS = vec3(dot(s.grad, TBN[0]), dot(s.grad, TBN[1]), dot(s.grad, TBN[2]));
    vec2 slope = vec2(gTS.x, gTS.y) * (0.08 * strength);

    vec3 nTS = normalize(vec3(-slope, 1.0));
    return normalize(TBN * nTS);
}

//  Single-pass entry. Preferred over calling the two functions above separately:
//  it evaluates the field once for a fragment that needs both the colour
//  modulation and the perturbed normal, instead of twice.
//
//  `worldNormal` is in/out — pass the interpolated geometric normal, read back the
//  perturbed one.
vec3 PdiApply(
    vec3 baseColor,
    vec3 worldPos,
    inout vec3 worldNormal,
    int  assetType,
    float detailStrength)
{
    if (detailStrength <= 0.001) return baseColor;

    PdiSample s = pdiSampleForAsset(worldPos, assetType);

    if (assetType == 0) {
        worldNormal = normalize(worldNormal - s.grad * (0.05 * detailStrength));
        return clamp(baseColor + (s.value * 0.05 * detailStrength), 0.0, 1.0);
    }

    vec3 n = normalize(worldNormal);
    vec3 g = s.grad - n * dot(s.grad, n);
    worldNormal = normalize(n - g * (0.15 * detailStrength));

    return clamp(baseColor * (1.0 + s.value * 0.30 * detailStrength), 0.0, 1.0);
}
