// anim_bones.cpp — the recovered animation pipeline (see anim_bones.h for the
// evidence and the offsets).

#include "ruby/caver/anim_bones.h"

#include <algorithm>
#include <cmath>

namespace caver {
namespace anim {

// ── Math ────────────────────────────────────────────────────────────────────

// The standard column-vector rotation matrix, which is what the verified
// column-major layout of PreTranslate implies. The signs follow from that layout
// rather than from a line-by-line read of FromRotationQuaternion's body, so a
// hand-check against the guest is still outstanding (see the status doc).
Mat4 Mat4::from_rotation_quaternion(const Quat& q) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 out;
    out.m[0]  = 1.0f - 2.0f * (yy + zz);
    out.m[1]  = 2.0f * (xy + wz);
    out.m[2]  = 2.0f * (xz - wy);
    out.m[3]  = 0.0f;
    out.m[4]  = 2.0f * (xy - wz);
    out.m[5]  = 1.0f - 2.0f * (xx + zz);
    out.m[6]  = 2.0f * (yz + wx);
    out.m[7]  = 0.0f;
    out.m[8]  = 2.0f * (xz + wy);
    out.m[9]  = 2.0f * (yz - wx);
    out.m[10] = 1.0f - 2.0f * (xx + yy);
    out.m[11] = 0.0f;
    out.m[12] = 0.0f; out.m[13] = 0.0f; out.m[14] = 0.0f; out.m[15] = 1.0f;
    return out;
}

// Matrix4::PreTranslate (0x5365BC), transcribed with the binary's own flat
// indices. It reads `f[3]`, `f[7]`, `f[11]`, `f[15]` — the fourth element of each
// 4-float group — and adds `t.x/t.y/t.z` times it to that group's first three:
//
//     f[4g+0] += f[4g+3] * t.x
//     f[4g+1] += f[4g+3] * t.y
//     f[4g+2] += f[4g+3] * t.z
//
// Read as column-major (a group is a column, so f[12..14] is the translation
// column) this is exactly `M' = T * M` — which is what "PreTranslate" means: the
// translation goes on the *left*. The earlier reading of this as a row-vector
// engine was wrong, and this loop is what settles it.
void Mat4::pre_translate(const Vec3& t) {
    for (int g = 0; g < 4; ++g) {
        const float w = m[g * 4 + 3];
        m[g * 4 + 0] += w * t.x;
        m[g * 4 + 1] += w * t.y;
        m[g * 4 + 2] += w * t.z;
    }
}

// C_Matrix4Mul: out = a * b in the engine's column-vector convention, so on a
// vector it means "apply b, then a" — the order the recovered chain relies on
// (world = parent.world * local, skin = world * inverse_bind).
//
// The pairing has to follow the storage: with column-major flat indices,
// element (row r, col c) lives at `4 * c + r`, so the product accumulates
// `a[4k + r] * b[4c + k]` into `4c + r`. Getting this wrong is invisible on
// translation-only matrices — they commute — and only shows up once a rotation
// is involved, which is what `caver_anim_bones_test`'s mul case exists for.
Mat4 Mat4::mul(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + r] * b.m[c * 4 + k];
            out.m[c * 4 + r] = sum;
        }
    }
    return out;
}

Quat normalized(const Quat& q) {
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len <= 0.0f) return Quat{};
    const float inv = 1.0f / len;
    return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

// ── FloatDataBuffer ─────────────────────────────────────────────────────────

void FloatDataBuffer::multiply(float k) {
    for (float& f : data) f *= k;
}

void FloatDataBuffer::accumulate(const FloatDataBuffer& other, float weight) {
    const size_t n = std::min(data.size(), other.data.size());
    for (size_t i = 0; i < n; ++i) data[i] += other.data[i] * weight;
}

// FloatDataBuffer::Accumulate(const&, float, const&) (0x325AD4). Two behaviours,
// chosen by the track's type; anything else is skipped (the binary falls through
// both branches and returns).
void accumulate_tracked(FloatDataBuffer& dst, const FloatDataBuffer& src,
                        const FloatDataTrack& track, float weight) {
    const int type = static_cast<int>(track.type);
    if (type != static_cast<int>(TrackType::Vector3) &&
        type != static_cast<int>(TrackType::Quaternion)) {
        return;   // the binary has no default branch, so an unknown type is a no-op
    }
    // A zero stride would spin forever in the binary (it is an addend, not a
    // count); refused here so a malformed track cannot hang a preview.
    if (track.stride <= 0) return;

    const int width = (type == static_cast<int>(TrackType::Quaternion)) ? 4 : 3;
    const int n = dst.count();
    const int srcn = src.count();
    const bool quat = type == static_cast<int>(TrackType::Quaternion);

    for (int off = track.start; off >= 0 && off + width <= n; off += track.stride) {
        if (off + width > srcn) break;
        if (quat) {
            // The hemisphere fix: if the running sum points away from the value
            // being added, negate the running sum first so the two are on the
            // same side of the sphere. Linear accumulation, not a slerp.
            const float dot = dst.data[off]     * src.data[off]
                            + dst.data[off + 1] * src.data[off + 1]
                            + dst.data[off + 2] * src.data[off + 2]
                            + dst.data[off + 3] * src.data[off + 3];
            if (dot < 0.0f) {
                for (int k = 0; k < 4; ++k) dst.data[off + k] = -dst.data[off + k];
            }
        }
        for (int k = 0; k < width; ++k) dst.data[off + k] += src.data[off + k] * weight;
    }
}

// ── Clip ────────────────────────────────────────────────────────────────────

FloatDataBuffer Clip::frame(int index) const {
    if (frames.empty()) return FloatDataBuffer{};
    const int i = std::clamp(index, 0, static_cast<int>(frames.size()) - 1);
    return frames[static_cast<size_t>(i)];
}

// ── AnimKeysNode ────────────────────────────────────────────────────────────

// AnimKeysNode::InitWithAnimation (0x325364): bind the clip and store
// `clip.duration - 0.001` as this node's duration, which is the whole reason the
// last frame is reachable.
void KeysNode::init_with_animation(const Clip* c) {
    clip = c;
    if (!clip) return;
    duration = clip->duration - kClipDurationEpsilon;
}

// AnimKeysNode::setPhase (0x32585C): `time = duration * phase`.
void KeysNode::set_phase(float phase) {
    time = duration * phase;
}

// AnimKeysNode::Update (0x3254B0).
bool KeysNode::update(float dt) {
    if (!clip) return false;

    if (update_time) {
        // The step is speed * dt * speed_multiplier (+48, dt, +52).
        const float step = (speed * dt) * speed_multiplier;
        if (reverse) {
            time -= step;
            if (time < 0.0f) time = loops ? duration : 0.0f;
        } else {
            time += step;
            if (time > duration) time = loops ? 0.0f : duration;
        }
    }

    // Clamp, then convert time to a frame position with the clip's fps (+28).
    if (time > duration) time = duration;
    const float position = time * clip->fps;
    const int new_frame = static_cast<int>(position);
    const float new_fraction = position - static_cast<float>(new_frame);

    // The recovered "did anything change" test: the frame index moved, or the
    // fraction moved by more than 0.001. This is what decouples pose evaluation
    // from the frame rate.
    if (frame == new_frame && std::fabs(new_fraction - frame_fraction) <= kChangedEpsilon) {
        return false;
    }
    frame = new_frame;
    frame_fraction = new_fraction;
    return true;
}

// AnimKeysNode::Evaluate (0x32559C).
void KeysNode::evaluate() {
    if (!clip) return;
    result_.resize(clip->track_count());
    result_.fill_zero();

    if (clip->frames.empty()) return;
    if (clip->frame_count == 1) {
        // Single-frame clip: no interpolation at all.
        result_.accumulate(clip->frame(0), 1.0f);
        return;
    }
    // `frames[frame] * (1 - fraction) + frames[frame + 1] * fraction`. The engine
    // indexes frame + 1 unguarded and is safe only because the stored duration is
    // a millisecond short of the clip's; Clip::frame clamps defensively.
    result_.accumulate(clip->frame(frame), 1.0f - frame_fraction);
    result_.accumulate(clip->frame(frame + 1), frame_fraction);
}

// AnimKeysNode::timeToFinish (0x32586C): zero while looping, else the remaining
// time at the current speed.
float KeysNode::time_to_finish() const {
    if (loops) return 0.0f;
    const float rate = speed * speed_multiplier;
    if (rate == 0.0f) return 0.0f;
    return (duration - time) / rate;
}

// ── AnimBlendNode ───────────────────────────────────────────────────────────

// AnimBlendNode::Update (0x324E9C). alpha advances by dt / duration — a
// reciprocal, so a longer blend is a slower alpha — and the blend flag clears
// exactly when it clamps.
// AnimBlendNode::Update (0x324E9C)'s state change. The guest reads the two
// children first and returns "no change" when either is missing, so the guard
// lives at the call site rather than in the law.
//
// The guest's reverse branch is `span = blend_in_time; if (span <= 0) span =
// blend_time; alpha -= dt / span` with no zero test. The `span != 0` here is the
// one deliberate divergence in this file: a node whose blend_in_time *and*
// blend_time are both non-positive would divide by zero in the guest and pin
// alpha at NaN forever, and there is no shipped configuration that does it.
void advance_blend(bool& blending, bool& reverse, float& alpha,
                   float blend_time, float blend_in_time, float dt) {
    if (!blending) return;
    if (reverse) {
        float span = blend_in_time;
        if (span <= 0.0f) span = blend_time;
        if (span != 0.0f) alpha -= dt / span;
        if (alpha < 0.0f) { alpha = 0.0f; blending = false; }
    } else {
        if (blend_time != 0.0f) alpha += dt / blend_time;
        if (alpha > 1.0f) { alpha = 1.0f; blending = false; }
    }
}

// AnimBlendNode::Evaluate (0x324FB8). The crossfade happens *in* the left
// child's buffer: scale it by (1 - alpha), add the right's by alpha, return it.
FloatDataBuffer* crossfade(FloatDataBuffer* left, FloatDataBuffer* right, float alpha) {
    if (!left) return right;
    if (!right) return left;
    if (alpha < kBlendLowThreshold) return left;
    if (alpha > kBlendHighThreshold) return right;
    left->multiply(1.0f - alpha);
    left->accumulate(*right, alpha);
    return left;
}

void BlendNode::update(float dt) {
    if (!left || !right) return;
    advance_blend(blending, reverse, alpha, blend_time, blend_in_time, dt);
}

FloatDataBuffer* BlendNode::evaluate() {
    if (!left || !right) {
        return left ? &left->result_buffer() : (right ? &right->result_buffer() : nullptr);
    }
    return crossfade(&left->result_buffer(), &right->result_buffer(), alpha);
}

// ── Skeleton ────────────────────────────────────────────────────────────────

// SkeletonInstance::ExtractDataFromBuffer (0x4E6880). Two strided blocks, read
// independently, and the quaternion is normalised here — not in the node, and
// not in EvaluateMatrices.
void extract_data_from_buffer(const Skeleton& skeleton, const FloatDataBuffer& buffer,
                              std::vector<Bone>& bones) {
    const int n = skeleton.bone_count;
    if (n < 1) return;
    bones.assign(static_cast<size_t>(n), Bone{});
    if (buffer.data.empty()) return;

    const int count = buffer.count();
    for (int i = 0; i < n; ++i) {
        Bone& bone = bones[static_cast<size_t>(i)];

        const int t = skeleton.translation_start + i * skeleton.translation_stride;
        if (t >= 0 && t + 3 <= count) {
            bone.translation = Vec3{buffer.data[static_cast<size_t>(t)],
                                    buffer.data[static_cast<size_t>(t) + 1],
                                    buffer.data[static_cast<size_t>(t) + 2]};
        }

        const int r = skeleton.rotation_start + i * skeleton.rotation_stride;
        if (r >= 0 && r + 4 <= count) {
            bone.rotation = normalized(Quat{buffer.data[static_cast<size_t>(r)],
                                            buffer.data[static_cast<size_t>(r) + 1],
                                            buffer.data[static_cast<size_t>(r) + 2],
                                            buffer.data[static_cast<size_t>(r) + 3]});
        }
    }
}

// SkeletonInstance::EvaluateMatrices (0x4E692C). One forward and one backward
// dependence: `world` needs the parent's *already computed* world, so the bone
// order has to be parents-first, which is the order the asset stores them in.
void evaluate_matrices(const Skeleton& skeleton, std::vector<Bone>& bones) {
    if (bones.empty()) return;
    const int n = std::min(skeleton.bone_count, static_cast<int>(bones.size()));
    for (int i = 0; i < n; ++i) {
        Bone& bone = bones[static_cast<size_t>(i)];

        // local = R, then PreTranslate -> rotate, then translate.
        bone.local = Mat4::from_rotation_quaternion(bone.rotation);
        bone.local.pre_translate(bone.translation);

        const int parent = (i < static_cast<int>(skeleton.rest.size()))
                               ? skeleton.rest[static_cast<size_t>(i)].parent_index
                               : -1;
        if (parent < 0 || parent >= n) {
            // Root: the recovered `(v8 & 0x80000000) != 0` branch copies the
            // local matrix into world.
            bone.world = bone.local;
        } else {
            bone.world = Mat4::mul(bones[static_cast<size_t>(parent)].world, bone.local);
        }

        const Mat4 inverse_bind = (i < static_cast<int>(skeleton.rest.size()))
                                      ? skeleton.rest[static_cast<size_t>(i)].inverse_bind
                                      : Mat4{};
        bone.skin = Mat4::mul(bone.world, inverse_bind);
    }
}

// Skeleton::BoneIndexForName (0x603600). BoneControlledCollisionShapeComponent
// ::BoneForName (0x3269E4) is the recovered caller: it resolves the component's
// model outlet, takes the `SkeletonInstance` out of `ModelInstance + 24`, asks
// this for an index, and returns `bone_array(+16) + 220 * index` — the same
// 220-byte stride `EvaluateMatrices` walks with `+ 55` ints and
// `ExtractDataFromBuffer` with `v5 += 55`, from three independent functions.
int bone_index_for_name(const Skeleton& skeleton, const std::string& name) {
    for (size_t i = 0; i < skeleton.bone_names.size(); ++i) {
        if (skeleton.bone_names[i] == name) return static_cast<int>(i);
    }
    return -1;
}

// ModelInstance::ExtractSkeletonDataFromBuffer (0x4E2240): the null check is the
// engine's own early-out, and the two instance dirty flags follow the work.
bool apply_buffer_to_skeleton(const Skeleton& skeleton, const FloatDataBuffer* buffer,
                              std::vector<Bone>& bones) {
    if (!buffer) return false;
    extract_data_from_buffer(skeleton, *buffer, bones);
    evaluate_matrices(skeleton, bones);
    return true;
}

// AnimNode::UpdateWithModelInstance (0x325898): nothing is evaluated unless
// Update() reports a change, so a clip playing slower than the frame rate does
// not rebuild the skeleton every frame.
bool update_with_skeleton(KeysNode& node, float dt, const Skeleton& skeleton,
                          std::vector<Bone>& bones) {
    if (!node.update(dt)) return false;
    node.evaluate();
    return apply_buffer_to_skeleton(skeleton, &node.result_buffer(), bones);
}

} // namespace anim
} // namespace caver
