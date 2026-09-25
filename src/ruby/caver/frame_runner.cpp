// frame_runner.cpp — the recovered frame order of Caver::Scene::Update
// (0x4F81AC). See frame_runner.h for the offsets and the evidence.

#include "ruby/caver/frame_runner.h"

#include <algorithm>

namespace caver {
namespace frame {

const std::vector<Pass>& frame_order() {
    // Exactly the sequence of calls in Scene::Update, top to bottom.
    static const std::vector<Pass> kOrder = {
        Pass::FramePrologue,
        Pass::ProgramUpdate,
        Pass::CullVolume,
        Pass::GridVisibleAreas,
        Pass::ActivateNearCamera,
        Pass::ObjectUpdate,
        Pass::CollisionBroadphase,
        Pass::PurgeInactive,
        Pass::CollisionNarrowphase,
        Pass::PairPurge,
        Pass::ComponentPurge,
    };
    return kOrder;
}

const char* pass_name(Pass pass) {
    switch (pass) {
        case Pass::FramePrologue:        return "frame-prologue";
        case Pass::ProgramUpdate:        return "program-update";
        case Pass::CullVolume:           return "cull-volume";
        case Pass::GridVisibleAreas:     return "grid-visible-areas";
        case Pass::ActivateNearCamera:   return "activate-near-camera";
        case Pass::ObjectUpdate:         return "object-update";
        case Pass::CollisionBroadphase:  return "collision-broadphase";
        case Pass::PurgeInactive:        return "purge-inactive";
        case Pass::CollisionNarrowphase: return "collision-narrowphase";
        case Pass::PairPurge:            return "pair-purge";
        case Pass::ComponentPurge:       return "component-purge";
    }
    return "unknown";
}

int pass_index(Pass pass) {
    const std::vector<Pass>& order = frame_order();
    const auto it = std::find(order.begin(), order.end(), pass);
    return it == order.end() ? -1 : static_cast<int>(it - order.begin());
}

bool runs_before(Pass a, Pass b) {
    const int ia = pass_index(a);
    const int ib = pass_index(b);
    return ia >= 0 && ib >= 0 && ia < ib;
}

bool scripts_before_objects() {
    return runs_before(Pass::ProgramUpdate, Pass::ObjectUpdate);
}

bool should_purge_pairs(float dt, int pause_depth) {
    return dt > kPairPurgeMinDt && pause_depth <= 0;
}

bool is_paused(int pause_depth) { return pause_depth > 0; }

ZRange cull_z_range() { return ZRange{-kCullZRange, kCullZRange}; }

} // namespace frame
} // namespace caver
