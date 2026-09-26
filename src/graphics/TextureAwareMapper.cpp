#include "TextureAwareMapper.h"
#include "tools/scene_v3_db.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>

// =============================================================================
//  TextureAwareMapper — see the header for why Lookup() is trust-gated.
// =============================================================================
//  Short version of the history, so nobody re-introduces it:
//
//  The previous revision returned a confident "generic stone" material for every
//  texture it had never seen, and its registration input
//  (g_last_opened_asset) is not actually the identity of the texture being
//  uploaded. Result: arbitrary draws - including sprites and the game's blob
//  shadow quads - got material overrides they had no business getting, and some
//  of those overrides (high metallic, high reflectivity, world-space procedural
//  detail) remove the diffuse term. That is what "every texture is now a flat
//  unlit box" looks like from the inside.
//
//  The gate below means an override can only ever be applied to a texture the
//  game data names as a terrain surface. Everything else is left exactly as the
//  engine shaded it.
// =============================================================================

static std::string to_lower(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return out;
}

// "assets/.../texture/Grass_Grass_2x.pvr" -> "grass_grass"
static std::string asset_basename(const std::string& raw) {
    std::string s = raw;
    size_t cut = s.find_last_of("/\\");
    if (cut != std::string::npos) s = s.substr(cut + 1);
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && dot > 0) s = s.substr(0, dot);
    s = to_lower(s);
    if (s.size() > 3 && s.substr(s.size() - 3) == "_2x") {
        s = s.substr(0, s.size() - 3);
    }
    return s;
}

TextureAwareMapper::TextureAwareMapper() {
    InitializeDefaults();
}

TextureAwareMapper& TextureAwareMapper::Instance() {
    static TextureAwareMapper s_instance;
    return s_instance;
}

void TextureAwareMapper::InitializeDefaults() {
    m_textureCache.clear();
    m_textureNames.clear();
    m_registeredCount = 0;
    m_trustedCount    = 0;
    m_rejectedCount   = 0;

    // Build the trust gate from the authoritative biome texture table.
    m_trustedNames.clear();
    for (int i = 0; i < swordigo::biome::kAuthoritativeTextureNameCount; ++i) {
        m_trustedNames.insert(swordigo::biome::kAuthoritativeTextureNames[i]);
    }

    // Start with NO biome. Not a default biome: until a scene name arrives, any
    // biome we name would be a guess, and a wrong biome changes ambient colour and
    // level without the user being able to see why. valid=false makes callers
    // apply no biome shading at all.
    m_activeBiome = BiomeEnvironment{};
    m_activeBiome.valid = false;

    std::cout << "[TextureAwareMapper] Trust gate armed with "
              << m_trustedNames.size() << " authoritative terrain texture names; "
              << "biome unset until a scene is opened" << std::endl;
}

// Host-authored TINT (chromaticity) per biome. Deliberately near-neutral: the
// LEVEL comes from BiomeEntry::lightAmbient, which is recorded in the game data.
// Keeping these from being dark is the point - the old values were 0.03-0.075 and
// were used as a replacement for glLightModelAmbient (~0.2), which is where the
// "SW+ Medium is very dark" report came from.
static void biome_default_tint(BiomeType t, float out[3]) {
    switch (t) {
        case BiomeType::Grasslands: out[0]=0.72f; out[1]=0.76f; out[2]=0.64f; break;
        case BiomeType::Forest:     out[0]=0.58f; out[1]=0.68f; out[2]=0.52f; break;
        case BiomeType::Grove:      out[0]=0.64f; out[1]=0.70f; out[2]=0.58f; break;
        case BiomeType::Wasteland:  out[0]=0.86f; out[1]=0.74f; out[2]=0.54f; break;
        case BiomeType::IceCastle:  out[0]=0.72f; out[1]=0.80f; out[2]=0.92f; break;
        case BiomeType::Cave:       out[0]=0.56f; out[1]=0.56f; out[2]=0.64f; break;
        case BiomeType::Fire:       out[0]=0.90f; out[1]=0.58f; out[2]=0.40f; break;
        case BiomeType::Florennum:  out[0]=0.62f; out[1]=0.68f; out[2]=0.80f; break;
        default:                    out[0]=0.70f; out[1]=0.70f; out[2]=0.70f; break;
    }
}

void TextureAwareMapper::SetBiome(BiomeType type) {
    const swordigo::biome::BiomeEntry* e =
        swordigo::biome::FindById((int)(uint32_t)type);
    if (!e) {
        m_activeBiome = BiomeEnvironment{};
        m_activeBiome.valid = false;
        std::cout << "[TextureAwareMapper] SetBiome: no authoritative entry for id "
                  << (int)(uint32_t)type << " - biome shading disabled" << std::endl;
        return;
    }

    m_activeBiome = BiomeEnvironment{};
    m_activeBiome.type             = type;
    m_activeBiome.name             = e->name;
    m_activeBiome.entry            = e;
    m_activeBiome.valid            = true;
    m_activeBiome.sunIntensity     = e->lightKey;       // recorded
    m_activeBiome.ambientIntensity = e->lightAmbient;   // recorded
    m_activeBiome.cave             = e->cave;
    m_activeBiome.water            = e->water;
    biome_default_tint(type, m_activeBiome.skyAmbientColor);

    // AO boost / roughness offset stay at their neutral defaults. The previous
    // values (1.0-1.65 AO, -0.45..+0.20 roughness) had no source in the game
    // data; the ice "glacial reflection" claim is now expressed through texture
    // identity (see DeduceFromAssetName) rather than a per-biome fudge.
    m_activeBiome.ambientOcclusionBoost = 1.0f;
    m_activeBiome.baseRoughnessOffset   = 0.0f;

    // Rain and wind have NO authoritative source - scene_v3_biomes.json contains
    // no weather field at all (grep for "rain" only matches
    // common.terrain_seed_default, and "wind" appears nowhere). They stay zero
    // rather than being invented, exactly like the windSpeed values that had to
    // be struck from BiomeMaterialTypes.h.
    m_activeBiome.rainIntensity   = 0.0f;
    m_activeBiome.windSpeed       = 0.0f;
    m_activeBiome.windDirection[0] = 1.0f;
    m_activeBiome.windDirection[1] = 0.0f;
}

void TextureAwareMapper::SetBiomeFromSceneName(const std::string& sceneName) {
    if (sceneName.empty()) return;

    // 1. Decoded scene filenames embed the biome directly: v3_Grasslands.scene.
    if (const swordigo::biome::BiomeEntry* e =
            swordigo::biome::ResolveFromSceneFileName(sceneName.c_str())) {
        SetBiome((BiomeType)(uint32_t)e->id);
        std::cout << "[TextureAwareMapper] '" << sceneName << "' -> "
                  << m_activeBiome.name << " (from filename)" << std::endl;
        return;
    }

    std::string base = sceneName;
    size_t cut = base.find_last_of("/\\");
    if (cut != std::string::npos) base = base.substr(cut + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    const std::string s = to_lower(base);

    // 2. Query runtime database portal_dests from scene_v3_biomes.json
    const auto& db = sgen::v3::v3_biome_db();
    for (size_t bi = 0; bi < db.size(); ++bi) {
        const auto& spec = db[bi];
        for (const auto& dest : spec.portal_dests) {
            std::string dest_lower = to_lower(dest);
            if (!dest_lower.empty() && s == dest_lower) {
                SetBiome((BiomeType)bi);
                std::cout << "[TextureAwareMapper] '" << sceneName << "' -> "
                          << m_activeBiome.name << " (matched portal_dest: " << dest << ")" << std::endl;
                return;
            }
        }
    }

    // 3. Match authentic level prefixes derived from decoded game scenes
    int resolvedId = -1;
    if (s.find("cave") != std::string::npos || s.find("crypt") != std::string::npos) {
        resolvedId = 5; // Cave
    } else if (s.rfind("grass_", 0) == 0 || s.rfind("town_", 0) == 0 || s.rfind("plains_", 0) == 0) {
        resolvedId = 0; // Grasslands (starting town and plains)
    } else if (s.rfind("forest_", 0) == 0) {
        resolvedId = 1; // Forest
    } else if (s.rfind("grove_", 0) == 0 || s.rfind("lowergrove_", 0) == 0) {
        resolvedId = 2; // Grove
    } else if (s.rfind("wasteland_", 0) == 0) {
        resolvedId = 3; // Wasteland
    } else if (s.rfind("icecastle_", 0) == 0 || s.rfind("snowy_", 0) == 0) {
        resolvedId = 4; // IceCastle
    } else if (s.rfind("fire", 0) == 0) {
        resolvedId = 6; // Fire
    } else if (s.rfind("florennum_", 0) == 0) {
        resolvedId = 7; // Florennum
    } else {
        // Look for an authoritative biome name anywhere in the level name
        for (int i = 0; i < swordigo::biome::kBiomeCount; ++i) {
            std::string n = to_lower(swordigo::biome::kBiomes[i].name);
            if (s.find(n) != std::string::npos) {
                resolvedId = swordigo::biome::kBiomes[i].id;
                break;
            }
        }
    }

    if (resolvedId >= 0 && resolvedId < swordigo::biome::kBiomeCount) {
        SetBiome((BiomeType)resolvedId);
        std::cout << "[TextureAwareMapper] '" << sceneName << "' -> "
                  << m_activeBiome.name << " (from scene pattern)" << std::endl;
        return;
    }

    // 4. Honest fallback when unmapped
    m_activeBiome = BiomeEnvironment{};
    m_activeBiome.valid = false;
    std::cout << "[TextureAwareMapper] '" << sceneName
              << "' -> no authoritative biome matched; biome shading disabled"
              << std::endl;
}

bool TextureAwareMapper::IsTrustedName(const std::string& assetName) const {
    if (assetName.empty()) return false;
    return m_trustedNames.count(asset_basename(assetName)) != 0;
}

bool TextureAwareMapper::RegisterTextureName(uint32_t textureID, const std::string& assetName) {
    if (textureID == 0 || assetName.empty()) return false;
    ++m_registeredCount;

    // The gate. `assetName` comes from g_last_opened_asset, which is NOT a
    // reliable identity for the texture being uploaded - so accept it only when
    // the basename is a name the game data itself uses for terrain.
    if (!IsTrustedName(assetName)) {
        ++m_rejectedCount;
        // Drop any previous entry: a GL texture name can be recycled by the
        // driver, and a stale material on a recycled name is exactly the failure
        // this class is being fixed for.
        m_textureNames.erase(textureID);
        m_textureCache.erase(textureID);
        return false;
    }

    ++m_trustedCount;
    m_textureNames[textureID] = assetName;
    m_textureCache[textureID] = DeduceFromAssetName(assetName);
    return true;
}

bool TextureAwareMapper::LooksLikeTerrain(uint32_t textureID) const {
    auto it = m_textureCache.find(textureID);
    return it != m_textureCache.end() && it->second.known;
}

bool TextureAwareMapper::LooksLikeGrass(uint32_t textureID) const {
    auto it = m_textureNames.find(textureID);
    if (it == m_textureNames.end()) return false;
    std::string base = asset_basename(it->second);
    return base.find("grass") != std::string::npos;
}

MaterialProperties TextureAwareMapper::Lookup(uint32_t textureID) const {
    auto it = m_textureCache.find(textureID);
    if (it == m_textureCache.end() || !it->second.known) {
        // Identity. .known == false tells ffp_forward.cpp to keep the engine's
        // own roughness/metallic and to skip reflectivity, wetness and procedural
        // detail injection. This is the safe answer, and it is the DEFAULT.
        return MaterialProperties{};
    }

    MaterialProperties props = it->second;

    if (m_activeBiome.valid) {
        props.roughness = std::clamp(props.roughness + m_activeBiome.baseRoughnessOffset,
                                     0.02f, 1.00f);
        // NOTE: biome water presence is NOT surface wetness. Grasslands and Cave
        // both have water=true and Florennum - the biome that visually rains -
        // has water=false, so `water` cannot drive u_Wetness. Wetness needs a
        // weather source that does not exist yet; it stays 0 here.
    }
    return props;
}

MaterialProperties TextureAwareMapper::DeduceFromAssetName(const std::string& rawName) {
    const std::string name = asset_basename(rawName);

    // Every rule below is only reachable for a name that passed the trust gate,
    // i.e. one of the terrain texture names in BiomeTable.h. Rules for
    // characters, weapons, UI and so on are deliberately NOT listed: they would be
    // unreachable today, and when texture identity becomes reliable they should be
    // written against real evidence rather than substring guesses.

    // Ice & glacier surfaces - the one class where a genuinely low roughness and
    // a dielectric specular is correct. metallic stays 0: ice is not a conductor,
    // and giving it metal values would remove its diffuse term.
    if (name.find("icicle") != std::string::npos ||
        name.find("icecastle") != std::string::npos) {
        MaterialProperties p;
        p.roughness = 0.16f; p.metallic = 0.0f; p.reflectivity = 0.40f;
        p.specularPower = 96.0f; p.wetnessFactor = 0.15f; p.proceduralDetailMode = 0;
        p.known = true;
        return p;
    }

    // Grass tops. detail mode 1 is correct here - these are GroundMesh top faces,
    // which is exactly the surface world-space procedural detail was designed for.
    if (name.find("grass") != std::string::npos) {
        MaterialProperties p;
        p.roughness = 0.78f; p.metallic = 0.0f; p.reflectivity = 0.05f;
        p.specularPower = 12.0f; p.proceduralDetailMode = 1;
        p.known = true;
        return p;
    }

    // Rock, cave, cliff, stone floors: rough dielectric.
    if (name.find("cavewall") != std::string::npos ||
        name == "brown128" || name.find("ground") != std::string::npos ||
        name.find("floor") != std::string::npos || name == "maybegood") {
        MaterialProperties p;
        p.roughness = 0.84f; p.metallic = 0.0f; p.reflectivity = 0.06f;
        p.specularPower = 16.0f; p.proceduralDetailMode = 1;
        p.known = true;
        return p;
    }

    // A trusted name with no specific rule still gets a safe terrain default
    // rather than a confident wrong one.
    MaterialProperties p;
    p.roughness = 0.82f; p.metallic = 0.0f; p.reflectivity = 0.05f;
    p.specularPower = 16.0f; p.proceduralDetailMode = 0;
    p.known = true;
    return p;
}

extern "C" {
void texture_aware_mapper_on_scene_open(const char* sceneName) {
    if (sceneName) {
        TextureAwareMapper::Instance().SetBiomeFromSceneName(sceneName);
    }
}

void texture_aware_mapper_on_texture_registered(uint32_t textureId, const char* assetName) {
    if (assetName) {
        TextureAwareMapper::Instance().RegisterTextureName(textureId, assetName);
    }
}

void texture_aware_mapper_get_stats(uint32_t* registered, uint32_t* trusted, uint32_t* rejected) {
    const TextureAwareMapper::Stats s = TextureAwareMapper::Instance().GetStats();
    if (registered) *registered = s.registered;
    if (trusted)    *trusted    = s.trusted;
    if (rejected)   *rejected   = s.rejected;
}
}
