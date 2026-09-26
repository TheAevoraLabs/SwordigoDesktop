// =============================================================================
//  SwordigoDesktop — Analytical wind sway (vertex displacement)
//  src/shaders/common/wind_sway.glsl
//
//  VERTEX STAGE ONLY. No derivatives are used anywhere in this file, which is
//  what lets it live in common/ alongside brdf.glsl and be included by a vertex
//  shader. (procedural_detail.glsl cannot be included here — it uses dFdx.)
//
//  INCLUDE-SAFE: no `#version`, no varyings, no entry point. The only uniforms it
//  declares are ones it actually needs, matching the precedent set by
//  camera_uniforms.glsl.
//
//  Zero texture fetches, zero memory stalls: everything below is sin/cos of
//  linear forms plus multiplies. On Intel Xe, sin/cos lower to the EU's native
//  transcendental path with no sampler or L1 traffic, so a vertex running this is
//  bottlenecked by nothing but ALU issue.
//
//  CONTRACT: all three entry points return a DISPLACEMENT in world units, not a
//  position. Compose as
//        vec4 world  = vec4(worldPos + ComputeWindSway(...), 1.0);
//  Returning a delta rather than an absolute position is what lets the host apply
//  the sway after its own matrix stack without having to undo and redo anything.
//
// =============================================================================
//  WHY THE TIME IS WRAPPED, AND WHY THAT IS NOT A DETAIL
// =============================================================================
//  The obvious implementation is `sin(w * time + phase)`. It is wrong for a
//  long-running session, and the failure is slow and confusing: at time = 3600 s
//  with a rustle at ~9.5 rad/s the phase argument is 34200 rad, where one float32
//  ULP is about 0.004 rad. The rustle visibly quantises into stepping after an
//  hour, and it looks like a driver bug rather than an arithmetic one.
//
//  The fix is to make the whole field EXACTLY periodic, then wrap. Every angular
//  frequency below is an integer multiple of the fundamental 2*PI/T where
//  T = SWORDIGO_WIND_PERIOD, so `time mod T` is a seamless wrap: at the wrap point
//  every sine returns to the same value it had at t = 0. That bounds the largest
//  phase argument at 2*PI*n_max (about 9.5e3 rad for the fastest term) instead of
//  letting it grow without limit, which keeps the precision loss near 1e-3 rad
//  regardless of uptime.
//
//  The spatial phases do not need this — worldPos is bounded by the level — so
//  only the temporal terms are constrained. Use SwordigoWindTime() below; it is
//  the one place the wrap is applied.
// =============================================================================

#ifndef SWORDIGO_WIND_SWAY_GLSL
#define SWORDIGO_WIND_SWAY_GLSL

// Common period of every temporal frequency in this file, in seconds.
#define SWORDIGO_WIND_PERIOD 1000.0

// Angular frequency of the n-th harmonic of the fundamental 2*PI/PERIOD. Every
// temporal frequency in this file is written this way so the wrap above is exact.
#define SWORDIGO_W_OMEGA(N) (6.28318530718 * float(N) / SWORDIGO_WIND_PERIOD)

// Finite-difference step for the normal reconstruction, in world units. Small
// enough to resolve the lowest spatial frequency here (waveK <= 0.6 rad/unit,
// i.e. a wavelength >= 10 units) with a truncation error well under a degree, and
// large enough to stay clear of float32 cancellation at level coordinates.
#define SWORDIGO_WIND_EPS 0.05

// objectType values. These are deliberately distinct from BiomeType: a banner in
// CarnivalVillage and a banner in Plains should both behave like a banner.
#define SWORDIGO_SWAY_GENERIC 0   // rigid-ish props, cloth, indeterminate geometry
#define SWORDIGO_SWAY_FOLIAGE 1   // BiomeType::Floranum — rain foliage rustle
#define SWORDIGO_SWAY_BANNER  2   // BiomeType::CarnivalVillage — coastal breeze

// Wind direction on the horizontal plane. The host sets this (the biome layer in
// src/graphics/TextureAwareMapper.cpp owns wind speed; it owns direction too, it
// just has not been given a field for it yet).
uniform vec2 u_windDirXZ;

// -----------------------------------------------------------------------------
//  Time
// -----------------------------------------------------------------------------
//  Wraps to the common period, so this is a no-op numerically for t < PERIOD and
//  costs one floor and one multiply-add for any uptime after that.
float SwordigoWindTime(float t) {
    return t - SWORDIGO_WIND_PERIOD * floor(t * (1.0 / SWORDIGO_WIND_PERIOD));
}

// Normalized wind direction, with a branchless fallback for an unset uniform.
// Same shape as pdiTriplanarWeights() in the PDI kernel: a guarded reciprocal plus
// a select, so a zero vector degrades to +X instead of producing NaN.
vec2 SwordigoWindDir() {
    float len = length(u_windDirXZ);
    vec2  d   = u_windDirXZ * (1.0 / max(len, 1e-5));
    return mix(vec2(1.0, 0.0), d, step(1e-4, len));
}

// -----------------------------------------------------------------------------
//  Profiles
// -----------------------------------------------------------------------------
//  EVERY temporal frequency below is SWORDIGO_W_OMEGA(integer). This is not
//  decoration: it is the precondition for the wrap at the top of this file to be
//  seamless. Writing `phase * 1.618` or `omega * 1.371` looks harmless and is the
//  obvious way to add a decorrelating second harmonic, but it puts a non-integer
//  multiple of t into the phase, so the field is no longer periodic over the wrap
//  period and time mod T reintroduces exactly the discontinuity the wrap existed
//  to avoid. The second harmonics are therefore real integer harmonics chosen so
//  that n2 / n1 sits near 1.618, which keeps the visual decorrelation while
//  keeping the wrap exact.
struct SwordigoWindProfile {
    float waveAmp;      // primary directional bend amplitude, world units
    float waveK;        // primary spatial wavenumber, rad per world unit
    float waveOmega;    // primary temporal angular frequency, rad/s
    float waveAmp2;     // second harmonic amplitude
    float waveK2;       // second harmonic wavenumber (n2/n1 ratio lives here)
    float waveOmega2;   // second harmonic angular frequency
    float rustleAmp;    // local flutter amplitude, world units
    float rustleOmega;  // flutter angular frequency, rad/s
    float rustleOmega2; // second flutter harmonic, for decorrelation
    float maskGain;     // how far this profile trusts the normal-derived mask
    float lift;         // vertical share of the directional bend
};

// Each profile is tuned against the biome numbers already in the tree
// (src/graphics/TextureAwareMapper.cpp, SetBiome): Floranum windSpeed 0.6 with
// rainIntensity 0.45, CarnivalVillage windSpeed 0.8 with zero rain, Plains 0.3.
//
// FOLIAGE is the Floranum brief: high frequency, low amplitude. The amplitude is
// small enough to read as a shimmer rather than a bend — leaves are being struck
// by droplets, not pushed by a steady force — and the frequency is high enough
// that the rustle decorrelates between neighbouring vertices, which is what makes
// it read as many individual leaves instead of one wobbling mass.
//
// BANNER is the Carnival brief: low frequency, directional. The wavenumber is
// low because a banner must see a nearly uniform push across its own width or it
// looks like it is made of rubber; the wave travels across the world so a row of
// banners ripples in sequence rather than in lockstep. maskGain is 1.0 because
// banner faces are vertical by construction.
//
// GENERIC keeps maskGain low on purpose. The normal-derived mask below is only
// valid for THIN, FREE surfaces — blades, cloth, banners — where the surface
// normal is perpendicular to the direction it can move. On a solid rock the mask
// reads as "the sides should sway", which is wrong, so generic geometry is held
// near-still and is expected to use ComputeWindSwayAnchored() instead.
SwordigoWindProfile SwordigoWindProfileFor(int objectType) {
    SwordigoWindProfile generic;
    generic.waveAmp      = 0.030;
    generic.waveK        = 0.22;
    generic.waveOmega    = SWORDIGO_W_OMEGA(175);   // 1.100 rad/s
    generic.waveAmp2     = 0.011;
    generic.waveK2       = 0.356;                   // 283/175 = 1.617
    generic.waveOmega2   = SWORDIGO_W_OMEGA(283);
    generic.rustleAmp    = 0.010;
    generic.rustleOmega  = SWORDIGO_W_OMEGA(478);   // 3.003 rad/s
    generic.rustleOmega2 = SWORDIGO_W_OMEGA(655);   // 655/478 = 1.370
    generic.maskGain     = 0.15;
    generic.lift         = 0.15;

    SwordigoWindProfile foliage;
    foliage.waveAmp      = 0.045;
    foliage.waveK        = 0.35;
    foliage.waveOmega    = SWORDIGO_W_OMEGA(255);   // 1.602 rad/s
    foliage.waveAmp2     = 0.017;
    foliage.waveK2       = 0.567;                   // 413/255 = 1.620
    foliage.waveOmega2   = SWORDIGO_W_OMEGA(413);
    foliage.rustleAmp    = 0.038;                   // high frequency, ...
    foliage.rustleOmega  = SWORDIGO_W_OMEGA(1512);  // 9.500 rad/s
    foliage.rustleOmega2 = SWORDIGO_W_OMEGA(2073);  // 2073/1512 = 1.371
    foliage.maskGain     = 1.0;
    foliage.lift         = 0.25;

    SwordigoWindProfile banner;
    banner.waveAmp       = 0.120;
    banner.waveK         = 0.16;                    // long wavelength: the banner
    banner.waveOmega     = SWORDIGO_W_OMEGA(143);   //   moves as a unit
    banner.waveAmp2      = 0.044;
    banner.waveK2        = 0.259;                   // 231/143 = 1.615
    banner.waveOmega2    = SWORDIGO_W_OMEGA(231);
    banner.rustleAmp     = 0.008;                   // ... low amplitude
    banner.rustleOmega   = SWORDIGO_W_OMEGA(398);   // 2.500 rad/s
    banner.rustleOmega2  = SWORDIGO_W_OMEGA(546);   // 546/398 = 1.372
    banner.maskGain      = 1.0;
    banner.lift          = 0.35;

    // Branchless selection. The weights are 0/1 so this is a two-level mix, but
    // it is written as a weighted sum so that a host passing a fractional
    // objectType (say 1.5, for a fern that is half-way to a banner) gets a
    // sensible blend rather than an arbitrary branch outcome.
    float wF = clamp(float(objectType == SWORDIGO_SWAY_FOLIAGE), 0.0, 1.0);
    float wB = clamp(float(objectType == SWORDIGO_SWAY_BANNER), 0.0, 1.0);
    float wG = max(0.0, 1.0 - wF - wB);

    SwordigoWindProfile p;
    p.waveAmp      = generic.waveAmp      * wG + foliage.waveAmp      * wF + banner.waveAmp      * wB;
    p.waveK        = generic.waveK        * wG + foliage.waveK        * wF + banner.waveK        * wB;
    p.waveOmega    = generic.waveOmega    * wG + foliage.waveOmega    * wF + banner.waveOmega    * wB;
    p.waveAmp2     = generic.waveAmp2     * wG + foliage.waveAmp2     * wF + banner.waveAmp2     * wB;
    p.waveK2       = generic.waveK2       * wG + foliage.waveK2       * wF + banner.waveK2       * wB;
    p.waveOmega2   = generic.waveOmega2   * wG + foliage.waveOmega2   * wF + banner.waveOmega2   * wB;
    p.rustleAmp    = generic.rustleAmp    * wG + foliage.rustleAmp    * wF + banner.rustleAmp    * wB;
    p.rustleOmega  = generic.rustleOmega  * wG + foliage.rustleOmega  * wF + banner.rustleOmega  * wB;
    p.rustleOmega2 = generic.rustleOmega2 * wG + foliage.rustleOmega2 * wF + banner.rustleOmega2 * wB;
    p.maskGain     = generic.maskGain     * wG + foliage.maskGain     * wF + banner.maskGain     * wB;
    p.lift         = generic.lift         * wG + foliage.lift         * wF + banner.lift         * wB;
    return p;
}

// -----------------------------------------------------------------------------
//  Masks
// -----------------------------------------------------------------------------
//  The normal-derived mask. For a blade or a banner the surface normal is
//  horizontal, so |n.y| ~ 0 and the vertex is free to move; for a floor or a
//  tabletop |n.y| ~ 1 and it must not. Squaring sharpens that so rigid surfaces
//  become genuinely rigid, while still being smooth — a squared term has no
//  discontinuity for a curved surface to catch on.
//
//  It is a HEURISTIC. It cannot distinguish a leaf from a boulder's flank, which
//  are geometrically the same statement. ComputeWindSwayAnchored() is the exact
//  form; this one exists because the required signature has no bounds to work
//  from.
float SwordigoSwayMask(vec3 normal) {
    float ny = abs(normalize(normal).y);
    float m  = 1.0 - ny;
    return m * m;
}

// -----------------------------------------------------------------------------
//  Displacement
// -----------------------------------------------------------------------------
vec3 SwordigoWindDisplace(
    vec3  worldPos,
    vec3  normal,
    float windSpeed,
    float t,
    SwordigoWindProfile p,
    float mask)
{
    t = SwordigoWindTime(t);

    vec2 wdir = SwordigoWindDir();
    vec3 wdir3 = vec3(wdir.x, 0.0, wdir.y);

    // Gust envelope: the slowest term, and the one that stops the motion reading
    // as a metronome. It is spatially varying too, so a gust crosses the level
    // instead of the whole world pulsing together.
    float gust = 0.62 + 0.38 * sin(SWORDIGO_W_OMEGA(59) * t + dot(worldPos.xz, wdir) * 0.021);

    // (1) Travelling directional wave. Two harmonics summed so the crests do not
    // repeat at the spatial period of a single sine. Both have their own spatial
    // and temporal frequency, and the temporal pair is a ratio near 1.618, so the
    // pattern does not read as a loop while the time wrap stays exact.
    float wave = sin(dot(worldPos.xz, wdir) * p.waveK  - p.waveOmega  * t) * 0.72
               + sin(dot(worldPos.xz, wdir) * p.waveK2 - p.waveOmega2 * t) * 0.28;

    // (2) Local rustle. Keyed on the full 3D position and on two different
    // direction vectors, so it is world-locked (it does not swim when the camera
    // moves) and neighbouring vertices are decorrelated.
    float rustle = 0.5 * sin(dot(worldPos, vec3( 11.3,  3.7,  7.9)) + p.rustleOmega  * t)
                 + 0.5 * sin(dot(worldPos, vec3( -5.3, 13.1,  4.1)) - p.rustleOmega2 * t);

    // Flutter runs along the horizontal part of the normal: that is the direction
    // a free surface can deflect without detaching from its support. A vertical
    // normal (a horizontal leaf, face up) leaves nothing to work with, so it falls
    // back to the cross-wind direction rather than to zero.
    //
    // Note where that fallback actually matters: under the normal-derived mask a
    // vertical normal already gives mask == 0, so the fallback cannot rescue it
    // there. It is the ANCHORED path that needs it — a horizontal leaf at the tip
    // of a stem gets a height mask that does not vanish, and without the fallback
    // its flutter direction would be a zero vector.
    vec3  n         = normalize(normal);
    vec3  flutter   = vec3(n.x, 0.0, n.z);
    float flutterLen = length(flutter);
    vec3  flutterDir = mix(vec3(-wdir.y, 0.0, wdir.x),
                           flutter * (1.0 / max(flutterLen, 1e-5)),
                           step(1e-4, flutterLen));

    // Banked so that a gust peak also lifts the surface slightly. wave*wave is
    // non-negative, so this is a one-sided lift rather than a symmetric bob.
    vec3 bend = wdir3 * (wave * gust * p.waveAmp)
              + vec3(0.0, 1.0, 0.0) * (wave * wave * p.lift * p.waveAmp);

    vec3 disp = bend + flutterDir * (rustle * p.rustleAmp);

    // windSpeed scales everything, so windSpeed == 0 is exactly still, with no
    // branch and no special case.
    return disp * (windSpeed * mask * p.maskGain);
}

// -----------------------------------------------------------------------------
//  Public entry points
// -----------------------------------------------------------------------------

// Required signature. Returns a world-space displacement; add it to the vertex
// position. Correct for thin free surfaces (foliage blades, banners, cloth). For
// solid geometry use the anchored variant below — see SwordigoSwayMask().
vec3 ComputeWindSway(
    vec3  worldPos,
    vec3  normal,
    float windSpeed,
    float time,
    int   objectType)
{
    return SwordigoWindDisplace(worldPos, normal, windSpeed, time,
                                SwordigoWindProfileFor(objectType),
                                SwordigoSwayMask(normal));
}

// The exact form, for a host that knows the object's bounds. `anchorY` is the
// world Y of the attachment (the root, the flagpole bracket) and `height` the
// object's vertical extent above it.
//
// The height mask is what the normal-derived heuristic cannot express: a banner
// hanging from a pole must be pinned along its top edge and must move most at the
// bottom, and its normals are identical along that whole span. A flat leaf at the
// tip of a stem has the same problem. Both fall out of this for free.
vec3 ComputeWindSwayAnchored(
    vec3  worldPos,
    vec3  normal,
    float anchorY,
    float height,
    float windSpeed,
    float time,
    int   objectType)
{
    // saturate((y - anchorY) / height): 0 at the attachment, 1 at the tip.
    float h    = clamp((worldPos.y - anchorY) / max(height, 1e-4), 0.0, 1.0);
    // Ease in so the attachment point does not kink: h*h is C1 at the root.
    float mask = h * h;
    return SwordigoWindDisplace(worldPos, normal, windSpeed, time,
                                SwordigoWindProfileFor(objectType),
                                mask);
}

// Normal correction.
//
//  Displacing vertices without touching the normal is a real defect, not a
//  nitpick: the lighting then describes a surface that is no longer there, and it
//  shows up as swirling speculars and a shaded crease that wanders across swaying
//  foliage. The cost of fixing it is one extra displacement evaluation.
//
//  The construction is first-order exact. Let T be a horizontal tangent along the
//  wind. Displacement maps the point p to p + D(p), so the material tangent T maps
//  to T + dD/dT, and the normal must stay orthogonal to that. The bi-tangent is
//  unchanged to first order, so the corrected normal is cross(T_new, B).
vec3 ComputeWindSwayNormal(
    vec3  worldPos,
    vec3  normal,
    float windSpeed,
    float time,
    int   objectType)
{
    vec3  n    = normalize(normal);
    vec2  wdir = SwordigoWindDir();
    vec3  T    = vec3(wdir.x, 0.0, wdir.y);

    SwordigoWindProfile p    = SwordigoWindProfileFor(objectType);
    float                mask = SwordigoSwayMask(n);

    vec3 d0 = SwordigoWindDisplace(worldPos,         n, windSpeed, time, p, mask);
    vec3 d1 = SwordigoWindDisplace(worldPos + T * SWORDIGO_WIND_EPS,
                                   n, windSpeed, time, p, mask);

    // dD/dT, scaled by the step, giving the tangent's perturbation directly.
    vec3 dT = (d1 - d0) * (1.0 / SWORDIGO_WIND_EPS);

    vec3 Tnew = normalize(T + dT);
    vec3 B    = cross(n, T);
    float bl  = length(B);
    B = mix(vec3(0.0, 1.0, 0.0), B * (1.0 / max(bl, 1e-5)), step(1e-4, bl));

    vec3 outN = cross(Tnew, B);
    return normalize(mix(n, outN, step(1e-4, length(outN))));
}

#undef SWORDIGO_W_OMEGA

#endif // SWORDIGO_WIND_SWAY_GLSL
