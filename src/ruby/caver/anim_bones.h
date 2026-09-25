#pragma once
// anim_bones.h — the character animation pipeline, recovered from libswordigo
// v1.4.13: the animation *node tree*, the float buffer the nodes write into, the
// tracks that say which floats belong to which bone, and the skeleton that turns
// those floats into skinning matrices.
//
// Sources (all arm64_13):
//
//   AnimNode::UpdateWithModelInstance       0x325898
//   AnimKeysNode::{InitWithAnimation, Update, UpdateFrameBlend, Evaluate,
//                  Evaluate(int,FloatDataTrack const*), setPhase, timeToFinish,
//                  resultBuffer, CreateBufferIfNeccessary}
//                                           0x325364 0x3254B0 0x325438 0x32559C
//                                           0x32564C 0x32585C 0x32586C 0x325854
//                                           0x325414
//   AnimBlendNode::{ctor, Update, Evaluate, Evaluate(int,…), timeToFinish,
//                   resultBuffer}           0x324DD0 0x324E9C 0x324FB8 0x3250A4
//   FloatDataBuffer::{ctor, Resize, FillZero, Multiply, Accumulate(×2),
//                     AccumulateVector3, AccumulateQuaternion}
//                                           0x325914 0x325958 0x32599C 0x3259B0
//                                           0x325A20 0x325AD4
//   SkeletonInstance::{ExtractDataFromBuffer, EvaluateMatrices}
//                                           0x4E6880 0x4E692C
//   ModelInstance::ExtractSkeletonDataFromBuffer   0x4E2240
//   Matrix4::{FromRotationQuaternion, PreTranslate} 0x5365BC 0x536634
//
// ── The pipeline, end to end ────────────────────────────────────────────────
//
//   AnimNode::UpdateWithModelInstance(dt, model)          (0x325898)
//     ├─ Update(dt)                → bool "something moved"
//     ├─ Evaluate(int, tracks)     → fills the node's FloatDataBuffer
//     ├─ resultBuffer()            → which buffer that was
//     └─ ModelInstance::ExtractSkeletonDataFromBuffer(buffer)
//          ├─ SkeletonInstance::ExtractDataFromBuffer(buffer)   (0x4E6880)
//          └─ SkeletonInstance::EvaluateMatrices()              (0x4E692C)
//
// `AnimNode` is a *tree*: `AnimKeysNode` samples one clip, `AnimBlendNode`
// crossfades two children. The leaf nodes own a `FloatDataBuffer`, which is a
// flat float array — not a struct per bone. Which floats belong to which bone is
// declared in two different places depending on the direction of travel:
//
//   * on the way *in* (node → buffer) by `FloatDataTrack`, a 12-byte record
//     `{type, start, stride}` that says "the quaternion at floats [start + i*stride]";
//   * on the way *out* (buffer → bone) by the `Skeleton`, which declares two
//     block descriptors — where the translations start and how far apart they
//     are, and the same for the rotations (0x4E6880).
//
// So the buffer is two *interleaved blocks*, not an array of bone records. That
// layout is the single most important thing here and it is not guessable.
//
// ── The two pieces of math that are easy to get wrong ───────────────────────
//
//   1. Blending quaternions is a **linear accumulation with a hemisphere flip**,
//      not a slerp: `Accumulate` (0x325AD4) computes `dot(dst, src)` and, when it
//      is negative, negates the destination in place before adding. The result is
//      normalised later, on ingest into the bone (0x4E6880).
//   2. The crossfade is **destructive on the left child's buffer**: `Evaluate`
//      (0x324FB8) calls `left->Multiply(1 - alpha)` and then
//      `leftBuffer->Accumulate(rightBuffer, alpha)` and returns the *left*
//      buffer. That is why `resultBuffer()` answers "left" until alpha reaches
//      0.999 — past that it has to answer "right", because the left buffer is no
//      longer the result.
//
// ── Matrix convention, and why it is decided rather than assumed ────────────
//
// `Matrix4::PreTranslate` (0x5365BC) reads `f[3]`, `f[7]`, `f[11]`, `f[15]` —
// the fourth element of each 4-float group — and adds `t.x/t.y/t.z` times it to
// that group's first three. Read as **column-major** (a group is a column, so
// f[12..14] is the translation column) that is exactly `M' = T * M`, which is
// what "PreTranslate" means. So this is a column-major, column-vector engine —
// GL-style — and the chain reads
//    m_local = R then PreTranslate(pos)   → rotate, then translate
//    m_world = parent.m_world * m_local
//    m_skin  = m_world * inverse_bind
// `Mat4` below is that convention: `m[12..14]` is the translation *column* and
// `mul(a, b)` is `a * b`, i.e. "apply b, then a".
//
// Worth recording that this was first read as row-vector from the same
// function's *name*, and the loop above is what refuted it: the addend is the
// 4th element of each group, and only the column-major reading makes
// `PreTranslate` a pre-translation.

#include <cstdint>
#include <string>
#include <vector>

namespace caver {
namespace anim {

// ── Math (column-major: translation in m[12..14], the last column) ──────────

struct Vec3 { float x = 0, y = 0, z = 0; };
// Stored x,y,z,w — the same order the POD loader and the buffer use.
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    // Matrix4::FromRotationQuaternion (0x5365BC's sibling, called by
    // EvaluateMatrices 0x4E692C before PreTranslate).
    static Mat4 from_rotation_quaternion(const Quat& q);
    // Matrix4::PreTranslate (0x5365BC): `m[i][j] += m[3][j] * t[i]` — i.e. a
    // *post*-rotation translation in row-vector order, so M = R * T.
    void pre_translate(const Vec3& t);
    // C_Matrix4Mul: row-vector composition, out = a * b.
    static Mat4 mul(const Mat4& a, const Mat4& b);
    Vec3 translation() const { return Vec3{m[12], m[13], m[14]}; }
};

Quat normalized(const Quat& q);

// ── FloatDataBuffer — the flat float array every node writes ────────────────
//
// Layout recovered from FloatDataBuffer's own functions: +0 is the count, +8 the
// heap array, and nothing else. `Resize` frees and reallocates even when the size
// is unchanged, so a caller that resizes every frame churns — the engine guards
// it with `if (size != wanted)` and so does this.
struct FloatDataBuffer {
    std::vector<float> data;   // == count floats

    int count() const { return static_cast<int>(data.size()); }
    void resize(int count) { data.assign(static_cast<size_t>(count < 0 ? 0 : count), 0.0f); }
    void fill_zero() { for (float& f : data) f = 0.0f; }
    // FloatDataBuffer::Multiply (0x3259B0): every element *= k.
    void multiply(float k);
    // FloatDataBuffer::Accumulate(const&,float) (0x325A20): this[i] += other[i]*w.
    void accumulate(const FloatDataBuffer& other, float weight);
};

// ── FloatDataTrack — where one bone's floats live in the buffer ─────────────
//
// 12 bytes, because `AnimKeysNode::Evaluate(int, track)` (0x32564C) advances the
// track pointer by 12 per iteration. The field meanings come from the walk in
// `FloatDataBuffer::Accumulate(const&, float, const&)` (0x325AD4):
//   type   at +0: 4 = quaternion (4 floats), 2 = vector3 (3 floats), else no-op
//   start  at +4: the first float index this track writes
//   stride at +8: how many floats to advance per step (the loop adds 4*stride
//                 bytes to *both* the destination and the source)
enum class TrackType : int32_t { Vector3 = 2, Quaternion = 4 };

struct FloatDataTrack {
    TrackType type = TrackType::Vector3;
    int start = 0;
    int stride = 0;
};

// FloatDataBuffer::Accumulate(const&, float, const&) (0x325AD4). Walks from
// `track.start` in `track.stride` steps for as long as the destination range
// stays inside the buffer — the recovered loop guard is a range check, not a
// count, so a track whose run would overrun is truncated rather than refused.
// Quaternion tracks apply the hemisphere flip; any other type is skipped.
void accumulate_tracked(FloatDataBuffer& dst, const FloatDataBuffer& src,
                        const FloatDataTrack& track, float weight);

// ── KeyframeAnimation — the clip ────────────────────────────────────────────
//
// Field offsets as used by AnimKeysNode (0x325364, 0x3254B0, 0x32559C):
//   +24 int frame_count          (`*(anim + 24) == 1` selects the no-blend path)
//   +28 float fps                (time * fps = frame position)
//   +32 FloatDataBuffer* frames  (16-byte stride: one buffer per frame)
//   +48 float duration           (the node stores `duration - 0.001`)
struct Clip {
    int frame_count = 0;
    float fps = 0.0f;
    float duration = 0.0f;
    std::vector<FloatDataBuffer> frames;

    // Frame count and fps are what the node reads; duration is the clip's own.
    FloatDataBuffer frame(int index) const;
    // Track count = the first frame's count (`**(int**)(anim + 32)`).
    int track_count() const { return frames.empty() ? 0 : frames.front().count(); }
};

// The constant AnimKeysNode::InitWithAnimation (0x325364) subtracts from the
// clip's duration when it stores its own: `anim[+48] - 0.001`. It is not a
// tolerance — it is the stored duration, which is why a clip played to the end
// stops one millisecond short and the last frame is reachable.
inline constexpr float kClipDurationEpsilon = 0.001f;

// ── AnimKeysNode — the clip sampler ────────────────────────────────────────
//
// Offsets from its own functions:
//   +12 float time            (+3 as float)   advance, clamped to duration
//   +16 int   frame           (+4 as int)     cached frame index
//   +20 float frame_fraction  (+5 as float)   cached sub-frame fraction
//   +24/+32   shared_ptr<const Clip>
//   +40 float duration        (+10 as float)   = clip.duration - 0.001
//   +44 bool  loops           wrap on overrun instead of clamping
//   +48 float speed           (+12 as float)   playback scale
//   +52 float speed_multiplier(+13 as float)   per-instance factor
//   +56 bool  update_time     when false the time is driven externally
//   +57 bool  reverse         when true time runs backwards
//   +64 FloatDataBuffer result (embedded, `resultBuffer()` returns &this->+64)
class KeysNode {
public:
    static constexpr float kChangedEpsilon = 0.001f;

    void init_with_animation(const Clip* clip);
    // AnimKeysNode::setPhase (0x32585C): `time = duration * phase`.
    void set_phase(float phase);
    // AnimKeysNode::Update (0x3254B0). Returns whether (frame, fraction) moved
    // by more than 0.001 — i.e. whether the pose is worth re-evaluating.
    bool update(float dt);
    // AnimKeysNode::Evaluate (0x32559C): zero the buffer, then accumulate the
    // current frame at (1 - fraction) and the next at fraction. With a single
    // frame, accumulate that frame at 1.0.
    void evaluate();
    FloatDataBuffer& result_buffer() { return result_; }
    const FloatDataBuffer& result_buffer() const { return result_; }
    // AnimKeysNode::timeToFinish (0x32586C): 0 while looping, else
    // `(duration - time) / (speed * speed_multiplier)`.
    float time_to_finish() const;

    const Clip* clip = nullptr;
    float time = 0.0f;
    int frame = 0;
    float frame_fraction = 0.0f;
    float duration = 0.0f;
    float speed = 1.0f;
    float speed_multiplier = 1.0f;
    bool loops = false;
    bool update_time = true;
    bool reverse = false;
    FloatDataBuffer result_;
};

// ── The blend flag pair, and why it is spelled as a word ────────────────────
//
// Every transition in the engine writes the two flag bytes *together*, as one
// 16-bit store, and both spellings appear in the binary:
//
//   0x0100   reverse = 0, blending = 1   alpha advances toward the right child
//   0x0101   reverse = 1, blending = 1   alpha retreats toward the left child
//   0x0000   reverse = 0, blending = 0   parked
//
// `reverse` is byte 44 and `blending` is byte 45 (AnimBlendNode::Update reads
// +45 as its gate and +44 as its direction), and the store is little-endian — so
// the **low** byte of the word carries `reverse`, not `blending`. Getting that
// backwards makes every forward transition read as a backward one.
//
// Reading 0x0101 as {reverse = 1, blending = 0} is the other mistake this
// spelling exists to prevent: 0x0101 sets *both* bytes, so the backward
// transitions — StopMoving (0x35799C) and StandOnGround (0x357D14) — are
// animated fades, not teleports. `Reset` (0x3574B4) is the one that parks, and
// it parks by clearing `blending` and alpha alone: it never touches `reverse`,
// which leaves the pair at 0x0001 rather than at 0x0000.
inline constexpr uint16_t kBlendReverseBit  = 0x0001;   // byte 44
inline constexpr uint16_t kBlendBlendingBit = 0x0100;   // byte 45
inline constexpr uint16_t kBlendTowardRight = 0x0100;
inline constexpr uint16_t kBlendTowardLeft  = 0x0101;
inline constexpr uint16_t kBlendParked      = 0x0000;

inline uint16_t blend_flags(bool reverse, bool blending) {
    return static_cast<uint16_t>((reverse ? kBlendReverseBit : 0u) |
                                 (blending ? kBlendBlendingBit : 0u));
}

// The alpha law, factored out of AnimBlendNode::Update (0x324E9C) because it has
// a second caller with node-reference children rather than KeysNode ones: the
// character controller's action slot (char_anim.h). One implementation, so the
// two cannot drift.
void advance_blend(bool& blending, bool& reverse, float& alpha,
                   float blend_time, float blend_in_time, float dt);

// The branch boundaries in Evaluate (0x324FB8) — hard thresholds, not
// tolerances: below 0.001 the left child is returned untouched, above 0.999 the
// right, and in between the destructive mix happens.
inline constexpr float kBlendLowThreshold = 0.001f;
inline constexpr float kBlendHighThreshold = 0.999f;

// AnimBlendNode::Evaluate (0x324FB8) over two already-evaluated buffers: scale
// the left by (1 - alpha) in place, add the right scaled by alpha, return the
// left. Destructive on `left`, which is the whole reason resultBuffer() has to
// answer "right" once alpha is past 0.999.
FloatDataBuffer* crossfade(FloatDataBuffer* left, FloatDataBuffer* right, float alpha);

// ── AnimBlendNode — the crossfade ──────────────────────────────────────────
//
//   +16 left AnimNode*, +24 right AnimNode*, +32 alpha,
//   +36 blend_time (default 1.0), +40 blend_in_time (default -1.0),
//   +44 reverse, +45 blending
//
// The 4-argument constructor (0x324E00) is `(left, right, blend_time,
// blend_in_time)`; the 0-argument one (0x324DD0) loads 0xBF8000003F800000 into
// +36, i.e. blend_time = 1.0 and blend_in_time = -1.0, and zeroes the flag word.
class BlendNode {
public:
    static constexpr float kLowThreshold = kBlendLowThreshold;
    static constexpr float kHighThreshold = kBlendHighThreshold;

    KeysNode* left = nullptr;
    KeysNode* right = nullptr;
    float alpha = 0.0f;
    float blend_time = 1.0f;
    float blend_in_time = -1.0f;
    bool reverse = false;
    bool blending = false;

    // The flag pair as the guest stores it, so a transition's exact word is
    // testable rather than inferred from two bools.
    uint16_t flags_word() const { return blend_flags(reverse, blending); }
    void set_flags_word(uint16_t word) {
        reverse = (word & kBlendReverseBit) != 0;
        blending = (word & kBlendBlendingBit) != 0;
    }

    // AnimBlendNode::Update (0x324E9C): alpha advances by dt / duration and the
    // blend flag clears when it clamps. Reverse uses blend_in_time when that is
    // positive, otherwise blend_time. Without both children the whole update is a
    // no-op that reports no change.
    void update(float dt);
    // AnimBlendNode::Evaluate (0x324FB8).
    FloatDataBuffer* evaluate();
};

// ── Skeleton and bones — the other end of the pipe ──────────────────────────

// Bone layout from SkeletonInstance::EvaluateMatrices (0x4E692C), whose loop
// advances the bone pointer by 55 ints (220 bytes):
//     +0   Vec3   translation   (3 floats, from the buffer)
//     +12  Quat   rotation      (4 floats, from the buffer, normalised on ingest)
//     +28  Mat4   local         (T * R)
//     +92  Mat4   world
//     +156 Mat4   skin          (world * inverse_bind)
struct Bone {
    Vec3 translation;
    Quat rotation;
    Mat4 local;
    Mat4 world;
    Mat4 skin;
};

// The *asset* side, 160 bytes per bone, of which two fields are used here: the
// parent index at +24 (`*(int*)(v6 - 68)` where v6 is the bone's inverse-bind
// matrix at +92 — 92 - 68 = 24) and the inverse bind matrix at +92. A parent
// index with the top bit set is a root: `(v8 & 0x80000000) != 0` copies the
// local matrix straight into world.
struct RestBone {
    int parent_index = -1;    // -1 (0x80000000 set) = root
    Mat4 inverse_bind;
};

// Skeleton field offsets, all read by ExtractDataFromBuffer (0x4E6880) as ints:
//   +0  int bone_count
//   +20 int translation_start   (float index into the buffer)
//   +24 int translation_stride  (float index step)
//   +32 int rotation_start
//   +36 int rotation_stride
// Two blocks, each strided independently — not one interleaved record per bone.
struct Skeleton {
    int bone_count = 0;
    int translation_start = 0;
    int translation_stride = 0;
    int rotation_start = 0;
    int rotation_stride = 0;
    std::vector<RestBone> rest;

    // Bone names, used by the name lookup below. The *storage* inside the binary's
    // Skeleton is not recovered yet (Skeleton::BoneIndexForName 0x603600 reads it
    // out of the Skeleton, but its container is not in these functions); what is
    // recovered is the lookup's contract: an index, or a negative value when the
    // name is absent, and the caller turning that into `bone_array + 220 * index`.
    std::vector<std::string> bone_names;
};

// Skeleton::BoneIndexForName (0x603600), as used by
// BoneControlledCollisionShapeComponent::BoneForName (0x3269E4): first match
// wins, or -1. The `>= 0` test at the call site is the whole reason a missing
// bone yields a null shape instead of a bone at a wrapped index.
int bone_index_for_name(const Skeleton& skeleton, const std::string& name);

// SkeletonInstance::ExtractDataFromBuffer (0x4E6880): for each bone, read the
// translation from the translation block and the quaternion from the rotation
// block, and normalise the quaternion on the way in.
void extract_data_from_buffer(const Skeleton& skeleton, const FloatDataBuffer& buffer,
                              std::vector<Bone>& bones);

// SkeletonInstance::EvaluateMatrices (0x4E692C): `local = from_quaternion(rot)`
// then `pre_translate(translation)`; `world = parent.world * local` (or `= local`
// for a root); `skin = world * inverse_bind`.
void evaluate_matrices(const Skeleton& skeleton, std::vector<Bone>& bones);

// ModelInstance::ExtractSkeletonDataFromBuffer (0x4E2240) is the whole tail:
// extract, then evaluate, then flag the instance dirty (`+16` and `+69`). Returns
// false when the buffer is null, which is the engine's own early-out.
bool apply_buffer_to_skeleton(const Skeleton& skeleton, const FloatDataBuffer* buffer,
                              std::vector<Bone>& bones);

// AnimNode::UpdateWithModelInstance (0x325898): `Update(dt)`, and only if that
// reports a change, evaluate and push into the skeleton.
bool update_with_skeleton(KeysNode& node, float dt, const Skeleton& skeleton,
                          std::vector<Bone>& bones);

} // namespace anim
} // namespace caver
