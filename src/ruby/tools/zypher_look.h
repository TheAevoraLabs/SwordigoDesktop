#pragma once
// ============================================================================
// zypher_look.h — the shared "Zypher look" (Ruby GG desktop)
//
// THE SAME LOOK, TWO PLACES. Zypher's hat (side) and top (deck) styles plus its
// three numeric dials are generation-time inputs, not per-object data, so the
// Ground Mesh Studio and the in-scene mesh editor must agree on them: a modder
// who dials in a look in the studio and then opens the in-scene editor has to
// see the same silhouette, not a silent revert to vanilla. This header is the
// one place that (a) persists the look under the Ruby GG QSettings tree and
// (b) pours it into a zypher::TerrainParams, so neither call site can drift.
//
// Header-only on purpose: it is three inline functions over QSettings and
// roller.h, and the two consumers live in different CMake targets, so a .cpp
// would only exist to be listed twice.
//
// Env overrides still win at generation time (roller.cpp reads
// RUBY_ZYPHER_HAT_STYLE etc.), exactly as RUBY_GROUND_GENERATOR overrides the
// generator; the GUI is the normal path and the env vars are for A/B testing.
// ============================================================================

#include "tools/roller.h"

#include <QSettings>
#include <QString>
#include <algorithm>
#include <string>

namespace ruby::tools::zypher_look {

// The Rust-free plain value bundle the widgets read and write.
struct Look {
    zypher::HatStyle hat = zypher::HatStyle::Vanilla;  ///< side / shoulder
    zypher::TopStyle top = zypher::TopStyle::Plain;    ///< deck relief (Flat)
    float frequency = 6.0f;             ///< features per 100 world units
    float depth = 14.0f;                ///< feature height / pit depth
    float min_collision_radius = 8.0f;  ///< below this a feature casts no collision
};

// ── Persisted generator choice ────────────────────────────────────────────
// boulderx's own default is Boulder (a load-bearing library invariant the tests
// pin), so the APP has to layer its Zypher default on top — that is what this
// pair is for. An empty id means "nothing saved": the caller then decides.
inline QString saved_generator_id() {
    QSettings s;
    return s.value(QStringLiteral("ruby_gg/ground_generator")).toString();
}

inline void save_generator_id(const std::string& id) {
    QSettings s;
    s.setValue(QStringLiteral("ruby_gg/ground_generator"), QString::fromStdString(id));
    s.sync();
}

// The generator the app should start with: a saved choice wins, else Zypher.
// RUBY_GROUND_GENERATOR is deliberately NOT consulted here — boulderx already
// applies it when it initialises its global, and honouring it here too would
// mean a debug env var outranking a user's saved pick.
inline std::string initial_generator_id() {
    const QString saved = saved_generator_id();
    if (!saved.isEmpty()) return saved.toStdString();
    return "zypher";
}

// ── Look persistence ──────────────────────────────────────────────────────
inline Look load() {
    QSettings s;
    Look l;
    l.hat = zypher::hat_style_from_id(
        s.value(QStringLiteral("ruby_gg/zypher_hat"), QStringLiteral("vanilla"))
            .toString().toStdString());
    l.top = zypher::top_style_from_id(
        s.value(QStringLiteral("ruby_gg/zypher_top"), QStringLiteral("plain"))
            .toString().toStdString());
    l.frequency = std::max(
        0.5f, s.value(QStringLiteral("ruby_gg/zypher_freq"), 6.0).toFloat());
    l.depth = std::max(
        0.0f, s.value(QStringLiteral("ruby_gg/zypher_depth"), 14.0).toFloat());
    l.min_collision_radius = std::max(
        0.0f, s.value(QStringLiteral("ruby_gg/zypher_mincoll"), 8.0).toFloat());
    return l;
}

inline void save(const Look& l) {
    QSettings s;
    s.setValue(QStringLiteral("ruby_gg/zypher_hat"),
               QString::fromLatin1(zypher::hat_style_id(l.hat)));
    s.setValue(QStringLiteral("ruby_gg/zypher_top"),
               QString::fromLatin1(zypher::top_style_id(l.top)));
    s.setValue(QStringLiteral("ruby_gg/zypher_freq"), double(l.frequency));
    s.setValue(QStringLiteral("ruby_gg/zypher_depth"), double(l.depth));
    s.setValue(QStringLiteral("ruby_gg/zypher_mincoll"), double(l.min_collision_radius));
    s.sync();
}

// Pour the look into the generator's parameter block. Only the fields Zypher's
// generation actually reads are touched; the caller keeps ownership of the
// texture names, surface width, etc., so this cannot clobber them.
inline void apply(zypher::TerrainParams& p, const Look& l) {
    p.hat_style = l.hat;
    p.top_style = l.top;
    p.top_frequency = l.frequency;
    p.top_depth = l.depth;
    p.top_min_collision_radius = l.min_collision_radius;
}

} // namespace ruby::tools::zypher_look
