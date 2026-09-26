// =============================================================================
//  BiomeTable.h — the AUTHORITATIVE biome table, and the real texture names
// =============================================================================
//  There are two biome models in this tree. This one is the real one.
//
//  The values below are transcribed from src/tools/scene_v3_biomes.json
//  (schema "swordigo.scene_gen.v3"), which is itself baked into the runtime as
//  kSceneV3BiomesJson in src/tools/scene_v3_db.cpp. That JSON's own header says
//  it is "derived from real vanilla decoded scenes + .scl template component
//  analysis" — i.e. it came out of the shipped game, not out of anyone's head.
//
//  src/graphics/BiomeMaterialTypes.h holds the other model, and its biome names
//  are invented. Concretely:
//
//      invented (BiomeMaterialTypes.h)   real (scene_v3_biomes.json)
//      --------------------------------  ------------------------------------
//      Plains                            Grasslands        (id 0)
//      SnowyPeaks                        IceCastle         (id 4)
//      Floranum                          Florennum         (id 7)  <- typo
//      CarnivalVillage                   -- does not exist --
//      Wasteland                         Wasteland         (id 3)  <- matches
//      CavesDungeon                      Cave              (id 5)
//      (missing)                         Forest  (id 1), Grove (id 2), Fire (id 6)
//
//  Checked against the guest binary itself: "Florennum", "Carnival", "Wasteland"
//  and "SnowyPeaks" have ZERO whole-word hits in
//  OpenSwordigo/arm64_13/strings.tsv. The real names appear in the decoded scene
//  set (de_out/v3_Florennum.scene, v3_Grasslands.scene, v3_IceCastle.scene, ...).
//
//  This matters beyond tidiness: it means the environment values in that other
//  header (sunIntensity, rainIntensity, windSpeed, torch colours) are guesses,
//  while light key/ambient intensity, water presence and torch glow are all
//  recorded per biome here.
//
// =============================================================================
//  HOW THE HOST CAN ACTUALLY KNOW THINGS (the part that was missing)
// =============================================================================
//  1. WHICH BIOME.  Not guessable from an enum. The inputs that exist are:
//       * the current level name. SRE13 declares
//         `char g_sre_current_scene_name[128]` in src/sre/sre13/
//         sre13_scene_shifter.h — but READ THE CAVEAT BELOW, it is not populated.
//       * the level -> biome association. v3_biomes entries carry `portal_dests`
//         (Grasslands -> "plains_part2", "plains_part3", "forest_cave0"), and the
//         decoded scene filenames carry the biome directly
//         (de_out/v3_<Biome>.scene). Both are real starting points; neither is
//         yet a complete resolver.
//     So: a level -> biome resolver is REQUIRED WORK, not something to stub with
//     an invented enum. See V3BiomeResolveByName() below for the partial form.
//
//  2. WHICH TEXTURE IS ON WHICH FACE.  This is the question the biome DB already
//     answers, by NAME: tex_top / tex_front / tex_bottom above. "grass_grass" is
//     the Grasslands top face; "forest_grass" is the Forest top face. There is no
//     need to invent texture IDs — and the hex IDs in the other header
//     (0x1C3A, 0x2A1B, 0x3F4C) have no evidence behind them at all.
//
//     Turning a name into "what is bound for this draw" is a three-step chain,
//     and all three steps already exist:
//
//        name  --TextureLibrary_TextureForName(lib, name, bool)-->  Texture*
//        Texture*  --RenderingContext::BindTexture(Texture*)-->  bound texture
//        Texture  --GL name field at +0x68-->  the id glBindTexture received
//
//     * `Caver::TextureLibrary::TextureForName` is declared in
//       src/sre/sre13/caver/TextureLibrary.h and resolved by mangled symbol in
//       src/sre/sre13/hooks/TextureLibrary.c, alongside GetAllTextures() which
//       enumerates every loaded texture.
//     * `Caver::RenderingContext::BindTexture(Caver::Texture *)`
//       (_ZN5Caver16RenderingContext11BindTextureEPNS_7TextureE) is ALREADY
//       listed in src/sre/sre13/hooks/RenderingContext.c next to UseProgram. It
//       is resolved as a symbol but is not wrapped with HOOK_SYMBOL, so nothing
//       observes it yet. That hook is the whole texture-awareness mechanism.
//     * The GL name offset: `Caver::Texture::Load` @ 0x54E124 ends with
//       `*((_DWORD *)v1 + 26) = v37;` — index 26 dwords is byte offset 0x68.
//       Confirm with one breakpoint before trusting it; `Texture::InitWithData`
//       @ 0x54EE74 (8 GL calls) is the better place to corroborate it, since it
//       is where the object gets uploaded.
//
//     With that, mapping a draw to a material is a table lookup on a NAME. That
//     is what "texture aware" means here, and it needs no guest modding because
//     the guest is a scene describer and this host is the renderer.
//
//  3. GRASS IS A TEXTURE NAME, NOT A HEURISTIC.
//     The three grass top faces are "grass_grass" (Grasslands), "forest_grass"
//     (Forest) and "fire_grass" (Fire); Wasteland's front is "grass_orange".
//     BiomeHasGrass() below reads that off the table. The current
//     `u_is_groundmesh` in ffp_forward.cpp cannot do this at all — it is
//     `!ortho && normals_enabled && colors_enabled && n_verts >= 60`, a
//     vertex-count guess that also drives u_asset_detail_type.
// =============================================================================

#pragma once
#include <cstdint>
#include <cstring>

namespace swordigo {
namespace biome {

struct BiomeEntry {
    int         id;
    const char* name;        // the real name, as it appears in the game data
    const char* texTop;      // GroundMesh top face
    const char* texFront;    // cliff / side face
    const char* texBottom;
    const char* texWall;     // wall/interior face; nullptr when the biome has none
    float       lightKey;    // recorded per-biome key light intensity
    float       lightAmbient;
    bool        cave;        // no sky contribution
    bool        water;       // water is present in this biome

    // Recorded water colours (RGB), straight from the biome's `water` block.
    // These are the authoritative input for the water/reflection pass - see the
    // note under ADVANCED_WATER below. Alpha in the source data (0.744) is the
    // SURFACE TEXTURE's alpha, not a reflection strength, so it is not carried.
    float       waterFront[3];
    float       waterSurface[3];
};

// Transcribed from src/tools/scene_v3_biomes.json. Keep in step with that file;
// it is the source of truth and this is a projection of it.
//
// ADVANCED_WATER: Grasslands and Cave carry IDENTICAL water colours
// (front 0.0,0.314,0.233 / surface 0.0,0.376,0.256). That is almost certainly
// .scl template reuse rather than two independently authored palettes, so do not
// read it as "cave water looks like grassland water" in-game - the cave's own
// lighting is what differs. Fire is lava (0.6,0.1,0.0 / 0.8,0.2,0.0) and is the
// one biome where a hot, emissive surface is correct.
inline const BiomeEntry kBiomes[] = {
    { 0, "Grasslands", "grass_grass",      "maybegood",        "maybegood",       nullptr,     1.2f, 0.3f, false, true,
      { 0.000f, 0.314f, 0.233f }, { 0.000f, 0.376f, 0.256f } },
    { 1, "Forest",     "forest_grass",     "forest_ground",    "forest_ground",   nullptr,     3.0f, 0.3f, false, false,
      { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    { 2, "Grove",      "forest_ground",    "forest_grass",     "grove_ground",    nullptr,     1.0f, 0.2f, false, false,
      { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    { 3, "Wasteland",  "wasteland_ground", "grass_orange",     "grass_orange",    nullptr,     3.0f, 0.5f, false, false,
      { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    { 4, "IceCastle",  "icecastle_ground", "icicle",           "icecastle_floor", nullptr,     2.0f, 0.2f, false, false,
      { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    { 5, "Cave",       "brown128",         "cavewalls",        "cavewalls",       "cavewalls", 2.0f, 0.3f, true,  true,
      { 0.000f, 0.314f, 0.233f }, { 0.000f, 0.376f, 0.256f } },
    { 6, "Fire",       "fire_grass",       "graveyard_ground", "graveyard_ground", nullptr,    3.0f, 0.4f, false, true,
      { 0.600f, 0.100f, 0.000f }, { 0.800f, 0.200f, 0.000f } },
    { 7, "Florennum",  "florennum_ground", "florennum_ground", "florennum_ground", nullptr,    1.2f, 0.3f, false, false,
      { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
};

// Every texture name the game data itself declares for terrain surfaces. This is
// the authoritative allow-list used to corroborate a texture registration before
// any material override is allowed to replace the engine's own material.
// Includes the two names (cavewalls2) and (tex_wall) variants that only appear in
// some biome blocks.
inline const char* const kAuthoritativeTextureNames[] = {
    "brown128", "cavewalls", "cavewalls2", "fire_grass", "florennum_grass",
    "florennum_ground", "forest_grass", "forest_ground", "grass_grass",
    "grass_orange", "grass_road", "grass_subtle", "grass_yellow",
    "graveyard_grass", "graveyard_ground", "grove_grass", "grove_ground",
    "icecastle_floor", "icecastle_ground", "icicle", "lowergrove_grass",
    "maybegood", "wasteland_ground",

    // "water" is a real SURFACE, not a skybox: the fluid sheet's TextureMapping
    // names it and 3 of the 8 vanilla v3 levels reference it (Grasslands, Cave,
    // Fire). It was missing from this gate, and because the gate is a positive
    // allow-list, that omission meant the single surface most in need of the
    // reflection pass could never be identified. Everything else the levels
    // reference that is NOT here - grasslandsbackground_day, forest_background,
    // grove_bg, wasteland_bg, cavesbackground2, florennum_night_bg - is a
    // Background-component skybox plane, which must NOT receive a terrain
    // material. "atlon" is an atlas/sprite token, also deliberately excluded.
    //
    // The set is enumerated from the decoded scenes, not from the biome palette:
    // run tools/scene_material_report.cpp and diff its GLOBAL SUMMARY list
    // against this array whenever levels are added.
    "water",
};
inline constexpr int kAuthoritativeTextureNameCount =
    int(sizeof(kAuthoritativeTextureNames) / sizeof(kAuthoritativeTextureNames[0]));

// -----------------------------------------------------------------------------
//  AUTHORED EMITTER PALETTE  (bloom key)
// -----------------------------------------------------------------------------
//  Every glowing thing vanilla Swordigo draws is a POINT light with `glow` set,
//  and each one carries an authored RGB tint that the glow sprite is modulated
//  by. Those tints are the ONLY reliable description of "this draw is a light
//  source" that exists in the data, so they are what the bloom extract keys on
//  instead of a luminance threshold (see src/shaders/graphics/bloom_extract.glsl).
//
//  The values below are MEASURED, not designed: they are every distinct Point
//  colour across all eight vanilla v3_*.scene files, re-derived with
//
//      ./bin/scene_material_report src/assets --quiet-textures
//
//  and they total 143 glow lights. The full census, with instance counts:
//
//      (1.000,0.480,0.000)  73   torch / campfire  -- EVERY level, r250, flicker
//      (1.000,0.600,0.300)  22   cave + forest lantern pods
//      (1.000,0.500,0.200)   8   wasteland lantern pods
//      (0.822,0.724,1.000)   8   grove lavender, r250, flicker
//      (0.600,0.800,1.000)   8   icecastle lantern pods
//      (0.267,0.111,0.389)   8   grove purple, r75, dim (intensity 0.5)
//      (1.000,0.850,0.600)   6   florennum lantern pods
//      (1.000,0.850,0.500)   4   grasslands lantern pods
//      (0.550,0.700,1.000)   4   grove blue pods (this one was missing from the
//                                first pass of the audit -- four instances)
//      (1.000,0.400,0.100)   2   fire biome pods
//
//  Every pod is drawn as a pair - a small bright core (intensity 0.35, r120) and
//  a large halo (intensity 1.10, r~200-258) - which is why one authored colour
//  covers two instances.
//
//  Plus one non-point emitter: Fire's water block is LAVA (surface
//  RGB 0.800,0.200,0.000), an emissive surface rather than a light. It is listed
//  only for the Fire biome, which is the only biome that authors it.
//
//  WHY PER BIOME AND NOT ONE GLOBAL LIST: a shared list lets Grove's lavender
//  pods key bloom in IceCastle purely because the hues are a few degrees apart,
//  and lava key bloom in Grasslands. The biome is already resolved by
//  TextureAwareMapper (the same record that drives water), so the subset costs
//  nothing at runtime.
//
//  Row order MUST match kBiomes (id == row index). The static_assert enforces it.
inline const float kEmitterPalette[][4][3] = {
    // 0 Grasslands : torch + warm lantern pods
    { {1.000f, 0.480f, 0.000f}, {1.000f, 0.850f, 0.500f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
    // 1 Forest     : torch + cave/forest pods
    { {1.000f, 0.480f, 0.000f}, {1.000f, 0.600f, 0.300f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
    // 2 Grove      : torch + lavender + purple + blue
    { {1.000f, 0.480f, 0.000f}, {0.822f, 0.724f, 1.000f}, {0.267f, 0.111f, 0.389f}, {0.550f, 0.700f, 1.000f} },
    // 3 Wasteland  : torch + orange pods
    { {1.000f, 0.480f, 0.000f}, {1.000f, 0.500f, 0.200f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
    // 4 IceCastle  : torch + cold blue pods
    { {1.000f, 0.480f, 0.000f}, {0.600f, 0.800f, 1.000f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
    // 5 Cave       : torch + cave/forest pods
    { {1.000f, 0.480f, 0.000f}, {1.000f, 0.600f, 0.300f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
    // 6 Fire       : torch + fire pods + the lava sheet
    { {1.000f, 0.480f, 0.000f}, {1.000f, 0.400f, 0.100f}, {0.800f, 0.200f, 0.000f}, {0.0f, 0.0f, 0.0f} },
    // 7 Florennum  : torch + pale gold pods
    { {1.000f, 0.480f, 0.000f}, {1.000f, 0.850f, 0.600f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} },
};
inline constexpr int kEmitterPaletteStride = 4;
inline constexpr int kEmitterMaxPerBiome  = 4;

// Entries after the first N are zero-filled pad, never a real emitter, so the
// count is the row length minus the trailing zeros. Deriving it instead of
// storing it means a new emitter can never be added without its count following.
inline int EmitterCountForBiome(int biomeId) {
    constexpr int kRows = int(sizeof(kEmitterPalette) / sizeof(kEmitterPalette[0]));
    if (biomeId < 0 || biomeId >= kRows) return 0;
    const float (*row)[3] = kEmitterPalette[biomeId];
    for (int i = kEmitterMaxPerBiome - 1; i >= 0; --i) {
        const float* c = row[i];
        if (c[0] > 0.0f || c[1] > 0.0f || c[2] > 0.0f) return i + 1;
    }
    return 0;
}

inline constexpr int kBiomeCount = int(sizeof(kBiomes) / sizeof(kBiomes[0]));

// The palette is indexed by biome id, so a row per biome and no more.
static_assert(int(sizeof(kEmitterPalette) / sizeof(kEmitterPalette[0])) ==
                  int(sizeof(kBiomes) / sizeof(kBiomes[0])),
              "kEmitterPalette must have one row per biome (id == row index)");

// Exact-name lookup. Returns nullptr for an unknown name rather than defaulting,
// because a silent default here is how a wrong biome reaches the shader.
inline const BiomeEntry* FindByName(const char* name) {
    if (!name || !*name) return nullptr;
    for (int i = 0; i < kBiomeCount; ++i)
        if (std::strcmp(kBiomes[i].name, name) == 0) return kBiomes[i].id == -1 ? nullptr : &kBiomes[i];
    return nullptr;
}

inline const BiomeEntry* FindById(int id) {
    for (int i = 0; i < kBiomeCount; ++i)
        if (kBiomes[i].id == id) return &kBiomes[i];
    return nullptr;
}

// -----------------------------------------------------------------------------
//  PARTIAL level -> biome resolver. Deliberately not a full solution.
// -----------------------------------------------------------------------------
//  Two real signals exist today, and neither covers every level:
//
//    * decoded scene filenames embed the biome: "v3_Florennum.scene",
//      "v3_Grasslands.scene", "v3_IceCastle.scene". This catches the
//      authoring/test scenes.
//    * the JSON's `portal_dests` associate real level names with a biome, e.g.
//      Grasslands -> "plains_part2", "plains_part3", "forest_cave0". This catches
//      some shipping levels and is NOT in this header yet, because transcribing
//      it by hand is exactly the failure mode this file exists to stop — it
//      should be read from the JSON, not retyped.
//
//  A caller that gets nullptr must fall back to "no biome-specific shading",
//  not to a default biome. That is the honest behaviour and it is what the
//  caller currently cannot do, because the invented enum always has a value.
inline const BiomeEntry* ResolveFromSceneFileName(const char* sceneFileName) {
    if (!sceneFileName) return nullptr;
    const char* slash = std::strrchr(sceneFileName, '/');
    const char* base  = slash ? slash + 1 : sceneFileName;
    if (std::strncmp(base, "v3_", 3) != 0) return nullptr;

    const char* p = base + 3;
    for (int i = 0; i < kBiomeCount; ++i) {
        const size_t n = std::strlen(kBiomes[i].name);
        if (std::strncmp(p, kBiomes[i].name, n) == 0) {
            const char c = p[n];
            if (c == '.' || c == '\0') return &kBiomes[i];
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
//  Derived material signals
// -----------------------------------------------------------------------------

// Grass is identified by the texture NAME on the top face. This is the real
// texture-aware test, and it replaces `n_verts >= 60`.
inline bool HasGrass(const BiomeEntry& b) {
    return b.texTop && std::strstr(b.texTop, "grass") != nullptr;
}

// Sway profile for src/shaders/common/wind_sway.glsl's objectType. Grass tops
// sway as foliage; everything else is treated as generic geometry and is held
// near-still by that library's low maskGain.
inline int SwayObjectType(const BiomeEntry& b) {
    return HasGrass(b) ? 1 /* SWORDIGO_SWAY_FOLIAGE */ : 0 /* SWORDIGO_SWAY_GENERIC */;
}

// Wetness for swordigo_pbr_lighting.frag's u_Wetness.
//
// `water` here means "this biome contains water geometry", which is recorded and
// is NOT the same as "it is raining on it". Florennum — the rain biome — has
// water=false in the table. So this can only gate a *baseline*; the animated
// rain term needs a weather source that does not exist yet. Returning the
// presence flag as if it were wetness would be the same class of guess the
// invented model made.
inline float BaselineWetness(const BiomeEntry& b, bool raining) {
    return raining ? (b.water ? 0.45f : 0.30f) : 0.0f;
}

} // namespace biome
} // namespace swordigo
