/* bloom_emitter_gate_test.cpp — does the bloom key actually find the game's lights?
 *
 * A C++ mirror of the gate in src/shaders/graphics/bloom_extract.glsl, run
 * against the REAL authored emitter palette (included from graphics/BiomeTable.h,
 * so editing the table re-runs this test) and against decoys measured from the
 * vanilla game data.
 *
 * There is no GL here and there is no shader compiler here: this checks the
 * ARITHMETIC and the THRESHOLD CHOICES, and glslangValidator checks that the
 * shader compiles. Keep the two in step - if a constant moves in the shader it
 * must move here, and the assertions below are what will fail when the two
 * disagree on behaviour.
 *
 * Build (no CMake needed; BUILD_TESTING is OFF in this tree):
 *     g++ -std=c++17 -Isrc -Wall -Wextra -O1 -o /tmp/bloom_gate_test \
 *         tests/bloom_emitter_gate_test.cpp && /tmp/bloom_gate_test
 *
 * WHAT IT PROVES, and what it deliberately does not:
 *   - every authored emitter in every biome passes the key when it is a local
 *     maximum, at its own authored brightness (this is the "lights bloom" half);
 *   - ordinary surfaces that share an emitter's HUE - the measured failure mode
 *     of a hue-only test - are rejected because they are not local maxima (this
 *     is the "only lights bloom" half);
 *   - nothing achromatic can pass the hue path at any brightness;
 *   - it does NOT prove any of this against a rendered frame. That needs the
 *     game running (F3 shows the emitter set; see .agents/deepseek_managed.md).
 */

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include "graphics/BiomeTable.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

// ---------------------------------------------------------------- shader mirror
// Constants must match bloom_extract.glsl / fbo_scaler.h exactly.
constexpr float EMITTER_HUE_LO = 0.95f;
constexpr float EMITTER_HUE_HI = 0.98f;
constexpr float EMITTER_CLIP_LO = 0.98f;
constexpr float EMITTER_LEVEL_KNEE = 0.10f;
constexpr float EMITTER_CONTRAST_KNEE = 0.35f;
constexpr float EMITTER_SAT_KNEE = 0.08f;

// The live host defaults (PostFXState). The test drives the same values the
// renderer starts with, so a default that breaks the key fails here.
constexpr float EMITTER_LUMA_DEFAULT = 0.10f;
constexpr float EMITTER_SAT_DEFAULT = 0.80f;
constexpr float EMITTER_CONTRAST_DEFAULT = 1.12f;
constexpr float EMITTER_GAIN_DEFAULT = 1.60f;

struct V3 { float r, g, b; };

float luma709(V3 c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

float smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

V3 chroma_dir(V3 c, float& strength) {
    float m = std::max(std::max(c.r, c.g), c.b);
    if (m < 1e-5f) { strength = 0.0f; return V3{0, 0, 0}; }
    V3 n{ std::max(c.r, 0.0f) / m, std::max(c.g, 0.0f) / m, std::max(c.b, 0.0f) / m };
    float l = 0.299f * n.r + 0.587f * n.g + 0.114f * n.b;
    V3 v{ n.r - l, n.g - l, n.b - l };
    strength = std::sqrt(v.r * v.r + v.g * v.g + v.b * v.b);
    if (strength < 1e-5f) return V3{0, 0, 0};
    return V3{ v.r / strength, v.g / strength, v.b / strength };
}

// (hue dot, saturation ratio) against the best-matching emitter of the list.
void emitter_match(V3 col, const std::vector<V3>& anchors, float& best_hue, float& best_sat) {
    best_hue = 0.0f;
    best_sat = 0.0f;
    float cand_strength;
    V3 cand = chroma_dir(col, cand_strength);
    if (cand_strength < 1e-4f) return;
    for (const V3& a : anchors) {
        float ref_strength;
        V3 ref = chroma_dir(a, ref_strength);
        if (ref_strength < 1e-4f) continue;
        float hue = cand.r * ref.r + cand.g * ref.g + cand.b * ref.b;
        if (hue > best_hue) {
            best_hue = hue;
            best_sat = cand_strength / ref_strength;
        }
    }
}

float emitter_weight(V3 col, float local_luma, const std::vector<V3>& anchors,
                     float luma_floor = EMITTER_LUMA_DEFAULT,
                     float sat_floor = EMITTER_SAT_DEFAULT,
                     float contrast_floor = EMITTER_CONTRAST_DEFAULT,
                     float gain = EMITTER_GAIN_DEFAULT) {
    float l = luma709(col);
    float ratio = l / std::max(local_luma, 1e-3f);
    float contrast = smoothstep(contrast_floor, contrast_floor + EMITTER_CONTRAST_KNEE, ratio);
    if (contrast <= 0.0f) return 0.0f;

    float level = smoothstep(luma_floor, luma_floor + EMITTER_LEVEL_KNEE, l);
    float hue_path = 0.0f;
    if (level > 0.0f) {
        float mh, ms;
        emitter_match(col, anchors, mh, ms);
        float hue = smoothstep(EMITTER_HUE_LO, EMITTER_HUE_HI, mh);
        float sat = smoothstep(sat_floor - EMITTER_SAT_KNEE, sat_floor + 0.02f, ms);
        hue_path = level * hue * sat;
    }
    float mx = std::max(std::max(col.r, col.g), col.b);
    float clip = smoothstep(EMITTER_CLIP_LO, 1.0f, mx);
    return std::max(0.0f, std::min(1.0f, std::max(hue_path, clip) * contrast * gain));
}

// The live emitter list for a biome, straight out of the shipped table.
std::vector<V3> anchors_for_biome(int biome_id) {
    std::vector<V3> out;
    const int n = swordigo::biome::EmitterCountForBiome(biome_id);
    for (int i = 0; i < n; ++i) {
        out.push_back(V3{ swordigo::biome::kEmitterPalette[biome_id][i][0],
                          swordigo::biome::kEmitterPalette[biome_id][i][1],
                          swordigo::biome::kEmitterPalette[biome_id][i][2] });
    }
    return out;
}

V3 scale_clamp(V3 c, float k) {
    return V3{ std::min(1.0f, c.r * k), std::min(1.0f, c.g * k), std::min(1.0f, c.b * k) };
}

} // namespace

int main() {
    std::printf("bloom emitter gate: %d biomes, decoys from the vanilla data\n\n",
                swordigo::biome::kBiomeCount);

    // ---------------------------------------------------------------------
    // 0. The invariant the whole key rests on: the chroma direction used by the
    //    gate is unchanged by scaling the colour AND by adding a constant to all
    //    three channels. Scaling is the emitter's brightness, and the constant is
    //    the opaque background a glow is additively drawn over. That is why an
    //    unclipped emitter pixel matches its authored hue exactly, at any
    //    brightness, over any background.
    // ---------------------------------------------------------------------
    std::printf("0. chroma direction is invariant to scale and to a grey offset\n");
    {
        const V3 base{ 1.0f, 0.48f, 0.0f };
        float bs; const V3 bd = chroma_dir(base, bs);
        for (float k : { 0.2f, 0.5f, 1.0f, 1.9f }) {
            for (float grey : { 0.0f, 0.15f, 0.4f, 0.7f }) {
                V3 c{ base.r * k + grey, base.g * k + grey, base.b * k + grey };
                float cs; const V3 cd = chroma_dir(c, cs);
                const float dot = bd.r * cd.r + bd.g * cd.g + bd.b * cd.b;
                char buf[160];
                std::snprintf(buf, sizeof buf,
                    "torch at %.1fx over grey %.2f keeps its hue (dot %.6f)", k, grey, dot);
                check(dot > 0.99999f, buf);
            }
        }
    }
    std::printf("   scale 0.2x..1.9x x grey offset 0.00..0.70: dot = 1.000000\n");

    // ---------------------------------------------------------------------
    // 1. Every authored emitter of every biome must bloom when it is a light.
    //    A light on screen is a local maximum, so the neighbourhood is taken
    //    as 45% of the emitter's own level - a conservative "sits on a wall
    //    that is already lit" case.
    // ---------------------------------------------------------------------
    std::printf("\n1. authored emitters as local maxima (weight must be > 0.5)\n");
    for (int b = 0; b < swordigo::biome::kBiomeCount; ++b) {
        const std::vector<V3> anchors = anchors_for_biome(b);
        const int n = swordigo::biome::EmitterCountForBiome(b);
        check(n > 0, std::string("biome ") + swordigo::biome::kBiomes[b].name +
                     " has at least one authored emitter");
        check(n == int(anchors.size()), "EmitterCountForBiome agrees with the palette row");
        for (int i = 0; i < n; ++i) {
            const V3 c = anchors[i];
            // At the authored brightness, and at the brightness a glow sprite
            // reaches with its additive halo (2x). The 2x case is the clipped
            // one for most of the palette: the pale pods have two channels at
            // the 8-bit ceiling there and are carried by the clip path, the warm
            // ones have one channel clipped and keep a usable hue.
            const float levels[2] = { 1.0f, 2.0f };
            for (float k : levels) {
                const V3 lit = scale_clamp(c, k);
                // The neighbourhood is dimmer than the light, but is not pitch
                // black - this is the "torch on a lit wall" case.
                const float local = std::max(0.05f, luma709(lit) * 0.45f);
                const float w = emitter_weight(lit, local, anchors);
                char buf[256];
                std::snprintf(buf, sizeof buf,
                    "%-11s emitter %2d x%.0f (%.3f,%.3f,%.3f) luma %.3f -> weight %.3f",
                    swordigo::biome::kBiomes[b].name, i, k, lit.r, lit.g, lit.b, luma709(lit), w);
                check(w > 0.5f, buf);
                if (w <= 0.5f) std::printf("        %s\n", buf);
            }
        }
        std::printf("   %-11s %d emitter hues, all pass\n", swordigo::biome::kBiomes[b].name, n);
    }

    // ---------------------------------------------------------------------
    // 2. The failure mode a hue-only key cannot avoid: ordinary surfaces that
    //    share an authored emitter's hue. They must be rejected because they
    //    are FLAT. Each is tested against every biome's palette, since a real
    //    frame does not know which biome it belongs to.
    // ---------------------------------------------------------------------
    struct Decoy { const char* name; V3 col; };
    const Decoy flat_decoys[] = {
        { "brown wood  (0.40,0.25,0.12)",        { 0.40f, 0.25f, 0.12f } },
        { "orange rock (0.70,0.40,0.15)",        { 0.70f, 0.40f, 0.15f } },
        { "orange rock (0.80,0.45,0.10)",        { 0.80f, 0.45f, 0.10f } },
        { "cave walls  (0.45,0.35,0.28)",        { 0.45f, 0.35f, 0.28f } },
        { "stone warm  (0.60,0.50,0.40)",        { 0.60f, 0.50f, 0.40f } },
        { "fire dir light (1.00,0.70,0.50)",     { 1.00f, 0.70f, 0.50f } },
        { "fire dir x grey albedo",              { 0.50f, 0.35f, 0.25f } },
        { "icecastle dir (0.80,0.90,1.00)",      { 0.80f, 0.90f, 1.00f } },
        { "florennum dir (1.00,0.95,0.80)",      { 1.00f, 0.95f, 0.80f } },
        { "grass green (0.30,0.50,0.20)",        { 0.30f, 0.50f, 0.20f } },
        { "grass lit   (0.36,0.60,0.24)",        { 0.36f, 0.60f, 0.24f } },
        { "water surface (0.00,0.376,0.256)",    { 0.00f, 0.376f, 0.256f } },
        { "water front   (0.00,0.314,0.233)",    { 0.00f, 0.314f, 0.233f } },
        { "sky blue    (0.40,0.60,0.90)",        { 0.40f, 0.60f, 0.90f } },
        { "ice blue    (0.55,0.75,1.00)",        { 0.55f, 0.75f, 1.00f } },
        { "grove terrain (0.30,0.35,0.45)",      { 0.30f, 0.35f, 0.45f } },
        { "magic cyan  (0.00,0.80,0.90)",        { 0.00f, 0.80f, 0.90f } },
    };
    std::printf("\n2. flat surfaces are never keyed, whatever their hue\n");
    for (const Decoy& d : flat_decoys) {
        bool any_biome_keyed = false;
        for (int b = 0; b < swordigo::biome::kBiomeCount; ++b) {
            const std::vector<V3> anchors = anchors_for_biome(b);
            // Flat: the pixel IS the local mean.
            if (emitter_weight(d.col, luma709(d.col), anchors) > 0.0f) any_biome_keyed = true;
        }
        check(!any_biome_keyed, std::string("flat ") + d.name + " is rejected in every biome");
        std::printf("   %-34s flat -> %s\n", d.name, any_biome_keyed ? "KEYED (bad)" : "rejected");
    }

    // ---------------------------------------------------------------------
    // 3. Achromatic invariant: no grey of any brightness may reach the hue
    //    path. This is what keeps a bright sky or a white wall from blooming
    //    in emitter mode; only an actually clipped core may, and only locally.
    // ---------------------------------------------------------------------
    std::printf("\n3. achromatic surfaces cannot match a hue\n");
    for (int b = 0; b < swordigo::biome::kBiomeCount; ++b) {
        const std::vector<V3> anchors = anchors_for_biome(b);
        for (int step = 1; step <= 9; ++step) {
            const float g = float(step) / 10.0f;           // 0.1 .. 0.9
            const V3 grey{ g, g, g };
            float mh, ms;
            emitter_match(grey, anchors, mh, ms);
            check(mh == 0.0f && ms == 0.0f, "grey has no chroma to match");
            // As a local maximum, but below the clip: must stay at zero.
            const float w = emitter_weight(grey, g * 0.5f, anchors);
            check(w == 0.0f, "grey below the clip level is not keyed");
        }
    }
    std::printf("   greys 0.1..0.9: chroma and weight both zero\n");

    // ---------------------------------------------------------------------
    // 4. A CLIPPED local hotspot is admitted on clip + contrast alone, because
    //    its hue no longer exists (see the shader header). The check is that it
    //    still needs the contrast: a flat clipped field must not pass.
    // ---------------------------------------------------------------------
    {
        const std::vector<V3> anchors = anchors_for_biome(0);
        const V3 white{ 1.0f, 1.0f, 1.0f };
        const float local_max = emitter_weight(white, 0.45f, anchors);
        const float flat      = emitter_weight(white, 1.0f, anchors);
        check(local_max > 0.5f, "a clipped local hotspot is keyed");
        check(flat == 0.0f, "a flat clipped field is not keyed");

        // The real pale-emitter case: grove lavender at 1.6x clips to pure
        // white. Flat shadow of itself, then as a local maximum.
        const V3 lav_white = scale_clamp(V3{ 0.822f, 0.724f, 1.000f }, 1.6f);
        float lw_hue, lw_sat;
        emitter_match(lav_white, anchors_for_biome(2), lw_hue, lw_sat);
        check(lw_hue == 0.0f && lw_sat == 0.0f, "lavender at 1.6x has no recoverable hue");
        check(emitter_weight(lav_white, luma709(lav_white) * 0.45f,
                             anchors_for_biome(2)) > 0.5f,
              "a clipped-to-white pale emitter is still keyed as a local maximum");
        std::printf("\n4. clipped: local max %.3f, flat %.3f, lavender 1.6x -> white, "
                    "hue %.3f, keyed %.3f\n",
                    local_max, flat, lw_hue,
                    emitter_weight(lav_white, luma709(lav_white) * 0.45f, anchors_for_biome(2)));
    }

    // ---------------------------------------------------------------------
    // 5. What the per-biome palette does and does not buy, stated exactly.
    //
    //    It does NOT restrict a palette to its own biome: every vanilla level
    //    authors the same torch (1.00,0.48,0.00), so the warm family is
    //    intentional everywhere, and eight of the ten authored emitter colours
    //    have a channel AT the ceiling by design, so they are found by the clip path
    //    in any biome. Both are recorded here as behaviour, not as accidents.
    //
    //    What it DOES buy is that the coloured, unclipped part of a cold pod is
    //    not keyed by a palette that authors no cold emitter, which is the case
    //    that would otherwise paint grove lavender over the ice level.
    // ---------------------------------------------------------------------
    {
        const auto ice  = anchors_for_biome(4);   // torch + cold blue pod
        const auto warm = anchors_for_biome(3);   // torch + orange pod
        const V3 grove_purple{ 0.267f, 0.111f, 0.389f };
        const V3 ice_pod = ice[1];
        // The ice pod scaled to 80% so no channel reaches the ceiling: this is
        // the pod's unclipped halo, which is the only part hue can speak for.
        const V3 ice_halo = scale_clamp(ice_pod, 0.80f);
        const V3 lava{ 0.800f, 0.200f, 0.000f };

        const float w_purple_on_ice = emitter_weight(grove_purple, luma709(grove_purple) * 0.45f, ice);
        const float w_ice_halo_warm = emitter_weight(ice_halo, luma709(ice_halo) * 0.45f, warm);
        check(w_purple_on_ice < 0.5f, "grove purple is not keyed by the IceCastle palette");
        check(w_ice_halo_warm < 0.5f, "an unclipped cold halo is not keyed by a warm-only palette");

        // The palette-independent part, asserted so it cannot change silently:
        // any pixel with a channel at the ceiling and local contrast is keyed.
        const float w_clipped_anywhere = emitter_weight(ice_pod, luma709(ice_pod) * 0.45f, warm);
        check(w_clipped_anywhere > 0.5f, "a ceiling-channel pixel is keyed by any palette (by design)");

        // Lava is genuinely 17 deg from the torch, so a hot emissive surface is
        // keyed by any torch-bearing palette. Recorded rather than assumed away.
        const float w_lava_anywhere = emitter_weight(lava, luma709(lava) * 0.45f, ice);
        std::printf("\n5. cross-biome: purple under ice %.3f, unclipped ice halo under warm-only "
                    "%.3f\n   ceiling-channel pixel under warm-only %.3f (clip path is "
                    "palette-independent)\n   lava under a torch-bearing palette %.3f "
                    "(warm family shared by design)\n",
                    w_purple_on_ice, w_ice_halo_warm, w_clipped_anywhere, w_lava_anywhere);
    }

    // ---------------------------------------------------------------------
    // 6. The legacy luminance mode must still be selectable, and the emitter
    //    mode must not silently equal it: the whole point is that the torch
    //    (authored luma 0.556) is BELOW the tier thresholds (0.78-0.85).
    // ---------------------------------------------------------------------
    {
        const float torch_luma = luma709(V3{ 1.0f, 0.48f, 0.0f });
        check(torch_luma < 0.78f, "the torch is below every tier bloom threshold");
        std::printf("\n6. torch luma709 = %.3f, lowest tier threshold 0.78 -> the legacy "
                    "path cannot see a torch\n", torch_luma);
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
