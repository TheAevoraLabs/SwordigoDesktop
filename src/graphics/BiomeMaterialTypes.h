// =============================================================================
//  BiomeMaterialTypes.h — biome environment + per-material description
// =============================================================================
//  READ THIS BEFORE ADDING A BIOME OR A MATERIAL RULE.
//
//  Two things used to be wrong in this header, and both were visible in game:
//
//  1. The biome list was INVENTED. It contained Plains, SnowyPeaks, Floranum
//     (a typo), CarnivalVillage (which does not exist) and was missing Forest,
//     Grove and Fire. The real list lives in src/graphics/BiomeTable.h, which is
//     transcribed from src/tools/scene_v3_biomes.json — the database baked into
//     the runtime as kSceneV3BiomesJson and derived from real decoded vanilla
//     scenes. The enum below is now ALIGNED TO THAT TABLE BY ID, so
//     `(uint32_t)BiomeType::Grasslands == 0 == kBiomes[0].id`.
//
//  2. MaterialProperties had no way to say "I do not know what this is". Every
//     unregistered texture therefore came back as a generic stone material, and
//     texture registration is not currently reliable enough to justify
//     overriding the engine's own lighting with it. See `known` below.
// =============================================================================

#pragma once
#include <cstdint>
#include <string>

#include "graphics/BiomeTable.h"

namespace swordigo { namespace biome { /* BiomeTable.h */ } }

// Biome selector. Values are the authoritative ids from scene_v3_biomes.json.
// Use biome::FindByName() / biome::ResolveFromSceneFileName() to obtain one;
// never hand-roll a string match against the enum names.
enum class BiomeType : uint32_t {
    Grasslands = 0,   // v3_Grasslands   tex_top grass_grass        key 1.2 amb 0.3  water
    Forest     = 1,   // v3_Forest       tex_top forest_grass       key 3.0 amb 0.3
    Grove      = 2,   // v3_Grove        tex_top forest_ground      key 1.0 amb 0.2
    Wasteland  = 3,   // v3_Wasteland    tex_front grass_orange     key 3.0 amb 0.5
    IceCastle  = 4,   // v3_IceCastle    tex_top icecastle_ground   key 2.0 amb 0.2
    Cave       = 5,   // v3_Cave         tex_top brown128          key 2.0 amb 0.3  cave water
    Fire       = 6,   // v3_Fire         tex_top fire_grass        key 3.0 amb 0.4  water
    Florennum  = 7,   // v3_Florennum    tex_top florennum_ground  key 1.2 amb 0.3
    Count
};

// -----------------------------------------------------------------------------
//  MaterialProperties
// -----------------------------------------------------------------------------
//  The fields are consumed by ffp_forward.cpp as uniforms. The contract is:
//
//      known == false  ->  the caller MUST use the engine's own material values
//                          (g_roughness / g_metallic) and MUST NOT apply
//                          reflectivity, wetness or procedural detail injection.
//      known == true   ->  the values below are a deliberate, evidence-backed
//                          override and may replace the engine's.
//
//  `known` exists because a *wrong* material is far more damaging than a missing
//  one. The texture registration that feeds this struct pairs a GL texture name
//  with whatever asset path the asset manager last opened, which is frequently
//  an unrelated file. A stale name that happens to contain "sword" or "blade"
//  would set metallic = 0.90, which drives F0 to the albedo and removes the
//  diffuse term entirely - that is what "textures render as flat unlit boxes"
//  looks like from the inside. So: unknown means PASS THROUGH, never guess.
struct MaterialProperties {
    float    roughness            = 0.80f;
    float    metallic             = 0.00f;
    float    reflectivity         = 0.00f;
    float    specularPower        = 16.0f;
    float    wetnessFactor        = 0.00f;
    // 0 = keep authored UVs (characters, props, sprites).
    // 1 = world-space procedural detail is appropriate (terrain, rock walls).
    // The default is 0: injecting world-space noise into an unidentified draw is
    // what makes sprites and shadow quads look like noisy unlit rectangles.
    uint32_t proceduralDetailMode = 0;
    bool     known                = false;
};

// -----------------------------------------------------------------------------
//  BiomeEnvironment
// -----------------------------------------------------------------------------
//  Intensity comes from the game data; only the TINT is host-authored. That
//  split matters: the previous version multiplied an invented, extremely small
//  "skyAmbientColor" (~0.03-0.075) by an invented sunIntensity and used the
//  product to REPLACE glLightModelAmbient, which the guest sets to ~0.2. The
//  result was a ~4x ambient reduction and a scene that read as "very dark"
//  regardless of tier. Ambient is now additive and tinted (see ffp_forward.cpp).
struct BiomeEnvironment {
    BiomeType type = BiomeType::Grasslands;
    const char* name = "Grasslands";
    const swordigo::biome::BiomeEntry* entry = nullptr;

    // false => no biome could be resolved from the scene. Callers must then
    // apply NO biome shading at all rather than falling back to a default
    // biome: a wrong biome is invisible to the user and therefore never fixed.
    bool valid = false;

    // Recorded per-biome in scene_v3_biomes.json (BiomeTable lightKey/lightAmbient).
    float sunIntensity     = 1.2f;   // == BiomeEntry::lightKey
    float ambientIntensity = 0.3f;   // == BiomeEntry::lightAmbient
    bool  cave  = false;
    bool  water = false;

    // Host-authored TINT only (chromaticity, not level). Keep these near 1.0-ish
    // in aggregate; the level is ambientIntensity.
    float skyAmbientColor[3] = {0.55f, 0.60f, 0.70f};   // daylight sky-ish

    float ambientOcclusionBoost = 1.0f;
    float baseRoughnessOffset   = 0.0f;

    // Weather. NOTE: there is no authoritative source for either of these in the
    // game data - scene_v3_biomes.json records a `water` flag and per-biome torch
    // glow, but nothing about wind or rain. They remain host-authored and are
    // deliberately zero until a real weather channel exists.
    float rainIntensity      = 0.0f;
    float windSpeed          = 0.0f;
    float windDirection[2]   = {1.0f, 0.0f};
};
