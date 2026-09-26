// =============================================================================
//  TextureAwareMapper.h — per-texture material lookup (TRUST-GATED)
// =============================================================================
//  This class answers two questions for ffp_forward.cpp:
//      1. "What material is this draw?"          -> Lookup(textureId)
//      2. "What environment are we currently in?" -> GetCurrentBiome()
//
//  Q1 is currently answered conservatively, and that is deliberate.
//
//  The registration path is `texture_aware_mapper_on_texture_registered(
//  g_frame_stats.last_bound_texture, g_last_opened_asset)` in
//  jni_bridge_arm64.cpp. The first argument is a GL texture NAME; the second is
//  whatever asset file the asset manager last opened. Those two are not the same
//  thing and are frequently unrelated - a deferred upload, a hovered UI element
//  or an atlas read will leave `g_last_opened_asset` pointing at some other file.
//  The pairing is therefore UNSOUND, and it produced exactly the symptom users
//  reported: materials assigned effectively at random, "no matter what the
//  texture is", including to sprites and shadow quads, which then rendered as
//  flat unlit rectangles because a bogus metallic value removes the diffuse term.
//
//  Until a real texture-identity channel exists (the plan of record is to wrap
//  Caver::RenderingContext::BindTexture and read Caver::Texture's name field -
//  see .agents/deepseek_managed.md Entry 004), this class only trusts names that
//  it can corroborate against the authoritative biome texture table. Everything
//  else returns MaterialProperties{ .known = false }, and ffp_forward.cpp passes
//  the engine's own material through untouched.
//
//  Net effect: an unidentified draw looks exactly like it does in vanilla. The
//  feature can only ever make things better; it can no longer make them worse.
// =============================================================================

#pragma once
#include "BiomeMaterialTypes.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>

class TextureAwareMapper {
private:
    std::unordered_map<uint32_t, MaterialProperties> m_textureCache;
    std::unordered_map<uint32_t, std::string> m_textureNames;
    BiomeEnvironment m_activeBiome;

    // Asset basenames that appear in the authoritative biome texture table.
    std::unordered_set<std::string> m_trustedNames;

    // Diagnostics, so the debug panel can show whether the gate is doing
    // anything instead of leaving it to guesswork.
    uint32_t m_registeredCount = 0;
    uint32_t m_trustedCount    = 0;
    uint32_t m_rejectedCount   = 0;

    TextureAwareMapper();

public:
    static TextureAwareMapper& Instance();

    void InitializeDefaults();

    // Resolve and install a biome. Accepts either a decoded scene filename
    // ("v3_Grasslands.scene", "de_out/v3_Florennum.scene") or a bare level name.
    // Sets .valid = false when nothing matches, so callers can skip biome
    // shading entirely rather than applying a wrong default.
    void SetBiome(BiomeType type);
    void SetBiomeFromSceneName(const std::string& sceneName);

    // Register a texture. `assetName` is only used when it can be corroborated;
    // see the file header. Returns true when the registration was trusted.
    bool RegisterTextureName(uint32_t textureID, const std::string& assetName);

    // Returns MaterialProperties with .known == false when the texture is not a
    // trusted identity. Callers MUST honour that flag - see the file header.
    MaterialProperties Lookup(uint32_t textureID) const;

    const BiomeEnvironment& GetCurrentBiome() const { return m_activeBiome; }

    // True when `assetName`'s basename is a texture the game data itself names
    // (e.g. "grass_grass", "forest_ground", "cavewalls"). This is the gate.
    bool IsTrustedName(const std::string& assetName) const;

    // True when this texture id resolved to a trusted terrain material. This is
    // the TEXTURE-BASED replacement for ffp_forward.cpp's `n_verts >= 60`
    // ground-form heuristic, which counts vertices instead of identifying the
    // surface and therefore misclassifies small ground patches (and any large
    // sprite) in both directions.
    bool LooksLikeTerrain(uint32_t textureID) const;

    // True when this texture id resolved to a trusted grass surface.
    bool LooksLikeGrass(uint32_t textureID) const;

    struct Stats { uint32_t registered, trusted, rejected; };
    Stats GetStats() const { return { m_registeredCount, m_trustedCount, m_rejectedCount }; }

    // Pure name->material rules. Does NOT consult the trust gate; callers decide
    // whether the name is worth trusting. Prefer Lookup().
    static MaterialProperties DeduceFromAssetName(const std::string& assetName);
};

// C bridge for android asset_manager.c scene hook
#ifdef __cplusplus
extern "C" {
#endif
void texture_aware_mapper_on_scene_open(const char* sceneName);
void texture_aware_mapper_on_texture_registered(uint32_t textureId, const char* assetName);

// Gate counters, exposed as C so translation units that deliberately do not
// include this C++ header (jni_bridge_arm64.cpp declares the two entry points
// above as local externs) can still report what the gate did. Any pointer may
// be NULL. Kept in the C bridge rather than as a C++ getter because the debug
// panel and the texture-identity telemetry are its only consumers.
void texture_aware_mapper_get_stats(uint32_t* registered, uint32_t* trusted, uint32_t* rejected);
#ifdef __cplusplus
}
#endif
