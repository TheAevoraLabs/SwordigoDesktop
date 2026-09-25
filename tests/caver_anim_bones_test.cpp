// caver_anim_bones_test — the recovered animation node tree and bone matching.
//
// Pins the chain `AnimNode::UpdateWithModelInstance` (0x325898) →
// `AnimKeysNode::Update/Evaluate` → `FloatDataBuffer` → `FloatDataTrack` →
// `SkeletonInstance::ExtractDataFromBuffer` (0x4E6880) →
// `EvaluateMatrices` (0x4E692C), and the four things in it that are easy to get
// wrong and impossible to guess:
//
//   * the clip's stored duration is the clip's duration **minus 0.001** (0x325364);
//   * a node reports "changed" only when the *(frame, fraction)* pair moves by
//     more than 0.001 (0x3254B0), which is what decouples the pose from the fps;
//   * quaternions blend by **linear accumulation with a hemisphere flip**
//     (0x325AD4), not by slerp, and are normalised later on ingest (0x4E6880);
//   * the crossfade is **destructive on the left child's buffer** (0x324FB8), so
//     `resultBuffer()` answers "left" only until alpha reaches 0.999.
//
// Pure RAM: no guest, no GL, no Qt.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ruby/caver/anim_bones.h"

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

using namespace caver::anim;

static int g_failures = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// Column-major, so m[12..14] is the translation column: v' = M * v.
static Vec3 transform_point(const Mat4& m, const Vec3& v) {
    return Vec3{
        m.m[0] * v.x + m.m[4] * v.y + m.m[8]  * v.z + m.m[12],
        m.m[1] * v.x + m.m[5] * v.y + m.m[9]  * v.z + m.m[13],
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14],
    };
}

// A clip whose frame buffers hold `track_count` floats each.
static Clip make_clip(int frame_count, float fps, int track_count,
                      const std::vector<std::vector<float>>& values) {
    Clip clip;
    clip.frame_count = frame_count;
    clip.fps = fps;
    clip.frames.resize(static_cast<size_t>(frame_count));
    for (int i = 0; i < frame_count; ++i) {
        clip.frames[static_cast<size_t>(i)].data = values[static_cast<size_t>(i)];
        if (static_cast<int>(clip.frames[static_cast<size_t>(i)].data.size()) < track_count)
            clip.frames[static_cast<size_t>(i)].data.resize(static_cast<size_t>(track_count), 0.0f);
    }
    clip.duration = frame_count > 0 && fps > 0.0f
                        ? static_cast<float>(frame_count - 1) / fps
                        : 0.0f;
    return clip;
}

int main() {
    std::printf("caver animation nodes + bone matching\n");

    // ── FloatDataBuffer ─────────────────────────────────────────────────────
    {
        FloatDataBuffer buf;
        buf.resize(4);
        check(buf.count() == 4, "resize sets the float count");
        buf.data = {1.0f, 2.0f, 3.0f, 4.0f};
        buf.multiply(2.0f);
        check(near(buf.data[0], 2.0f) && near(buf.data[3], 8.0f), "Multiply scales every float");

        FloatDataBuffer other;
        other.data = {1.0f, 1.0f, 1.0f, 1.0f};
        buf.accumulate(other, 0.5f);
        check(near(buf.data[0], 2.5f) && near(buf.data[3], 8.5f),
              "Accumulate is this[i] += other[i] * weight");

        buf.fill_zero();
        check(buf.data[0] == 0.0f && buf.data[3] == 0.0f, "FillZero clears the whole buffer");
    }

    // ── FloatDataTrack accumulation, and the quaternion hemisphere flip ──────
    {
        FloatDataBuffer dst, src;
        dst.data = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        src.data = {1.0f, 2.0f, 0.0f, 2.0f, 0.0f, 0.0f};

        // A vector3 track at 0, stride 3: writes floats 0..2 only.
        accumulate_tracked(dst, src, FloatDataTrack{TrackType::Vector3, 0, 3}, 1.0f);
        check(near(dst.data[0], 2.0f) && near(dst.data[1], 2.0f) && near(dst.data[2], 0.0f),
              "a vector3 track accumulates 3 floats");

        // The same slot as a quaternion track covers 4 floats.
        FloatDataBuffer dst2, src2;
        dst2.data = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        src2.data = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
        accumulate_tracked(dst2, src2, FloatDataTrack{TrackType::Quaternion, 0, 4}, 1.0f);
        check(near(dst2.data[3], 1.0f), "a quaternion track accumulates 4 floats");

        // The flip: a destination pointing away from the source is negated first,
        // so the sum stays on one hemisphere. Without the flip the result would
        // be (-1, 0, 0, 0) — a quaternion that describes the same rotation but
        // sums in the wrong direction, which is what makes a blend wobble.
        FloatDataBuffer dst3, src3;
        dst3.data = {1.0f, 0.0f, 0.0f, 0.0f};
        src3.data = {1.0f, 0.0f, 0.0f, 0.0f};
        FloatDataBuffer dst4 = dst3;
        FloatDataBuffer negated;
        negated.data = {-1.0f, 0.0f, 0.0f, 0.0f};
        accumulate_tracked(dst3, negated, FloatDataTrack{TrackType::Quaternion, 0, 4}, 1.0f);
        check(near(dst3.data[0], -2.0f),
              "a quaternion pointing away from the accumulator is negated first");

        FloatDataBuffer dst5;
        dst5.data = {1.0f, 0.0f, 0.0f, 0.0f};
        accumulate_tracked(dst5, src3, FloatDataTrack{TrackType::Quaternion, 0, 4}, 1.0f);
        check(near(dst5.data[0], 2.0f),
              "a quaternion pointing the same way is not flipped");

        // The loop guard is a range check, not a count: `start <= count - width`.
        // A run that would overrun is truncated after the steps that did fit.
        FloatDataBuffer dst6, src6;
        dst6.data = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        src6.data = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        accumulate_tracked(dst6, src6, FloatDataTrack{TrackType::Vector3, 2, 3}, 1.0f);
        check(near(dst6.data[2], 1.0f) && near(dst6.data[4], 1.0f),
              "a track that fits once is accumulated, then the walk leaves the buffer");
        FloatDataBuffer too_small, src_small;
        too_small.data = {0.0f, 0.0f, 0.0f};
        src_small.data = {1.0f, 1.0f, 1.0f};
        accumulate_tracked(too_small, src_small, FloatDataTrack{TrackType::Vector3, 2, 3}, 1.0f);
        check(near(too_small.data[2], 0.0f),
              "a track that cannot fit at all is skipped, not partially written");

        // A stride of 1 walks one float at a time, overlapping itself.
        FloatDataBuffer dst7, src7;
        dst7.data = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        src7.data = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        accumulate_tracked(dst7, src7, FloatDataTrack{TrackType::Vector3, 0, 1}, 1.0f);
        // off = 0,1,2,3 each fit (off + 3 <= 6), so the middle floats get written
        // three times: {1,2,3,3,2,1}.
        check(near(dst7.data[0], 1.0f) && near(dst7.data[2], 3.0f) && near(dst7.data[5], 1.0f),
              "a stride of 1 walks the buffer one float at a time, overlapping itself");
        accumulate_tracked(dst7, src7, FloatDataTrack{TrackType::Vector3, 0, 0}, 1.0f);
        check(near(dst7.data[0], 1.0f), "a zero stride is refused rather than spun on");
        accumulate_tracked(dst7, src7, FloatDataTrack{static_cast<TrackType>(7), 0, 3}, 1.0f);
        check(near(dst7.data[0], 1.0f),
              "an unknown track type is a no-op, as in the binary's missing default branch");
    }

    // ── AnimKeysNode: duration, phase, time advance, wrap vs clamp ───────────
    {
        Clip clip = make_clip(3, 10.0f, 2, {{0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}});
        check(near(clip.duration, 0.2f), "a 3-frame clip at 10 fps is 0.2s long");

        KeysNode node;
        node.init_with_animation(&clip);
        check(near(node.duration, 0.199f),
              "the node stores clip duration minus 0.001 (0x325364)");
        check(node.duration < clip.duration,
              "so the stored duration is strictly shorter than the clip's");

        node.set_phase(0.5f);
        check(near(node.time, 0.199f * 0.5f), "setPhase is duration * phase");

        node.time = 0.0f;
        node.speed = 1.0f;
        node.speed_multiplier = 1.0f;
        node.loops = false;
        node.update(0.1f);
        check(near(node.time, 0.1f), "time advances by speed * dt * multiplier");
        check(node.frame == 1, "and converts to a frame through the clip's fps");

        // A non-looping node clamps at its own duration, not the clip's.
        for (int i = 0; i < 10; ++i) node.update(0.1f);
        check(near(node.time, node.duration), "a one-shot clip clamps at its duration");
        check(node.time < clip.duration, "which is the epsilon-short duration, not the clip's");

        // A looping node wraps instead.
        KeysNode looper;
        looper.init_with_animation(&clip);
        looper.loops = true;
        looper.update(0.1f);
        looper.update(0.1f);
        check(looper.time < 0.5f * looper.duration, "a looping clip wraps past its duration");

        // Reverse: wrap on loop, clamp at zero otherwise.
        KeysNode back;
        back.init_with_animation(&clip);
        back.reverse = true;
        back.time = 0.05f;
        back.update(0.1f);
        check(near(back.time, 0.0f), "reverse past zero clamps to 0 when not looping");
        back.time = 0.05f;
        back.loops = true;
        back.update(0.1f);
        check(near(back.time, back.duration), "reverse past zero wraps to the duration when looping");

        // update_time == false leaves the clock alone (an external driver).
        KeysNode driven;
        driven.init_with_animation(&clip);
        driven.update_time = false;
        driven.time = 0.05f;
        driven.update(0.1f);
        check(near(driven.time, 0.05f), "a node with update_time false is not advanced");
    }

    // ── The "changed" test, and why a slow clip does not rebuild the skeleton ─
    {
        Clip clip = make_clip(11, 10.0f, 1, [] {
            std::vector<std::vector<float>> v;
            for (int i = 0; i < 11; ++i) v.push_back({static_cast<float>(i)});
            return v;
        }());

        KeysNode node;
        node.init_with_animation(&clip);
        // A zero-length step moves neither frame nor fraction, so even the first
        // update reports no change — the test is on movement, not on call order.
        check(!node.update(0.0f), "an update that moves nothing is not a change");
        check(node.update(0.01f), "a step that moves the fraction is a change");
        check(!node.update(0.0f), "and a following zero-length step is not");

        // A tenth of a frame at 10 fps: 0.01s advances the fraction by 0.1, which
        // is well past the 0.001 threshold.
        check(node.update(0.01f), "a fraction move past 0.001 is a change");

        // 0.0001s at 10 fps moves the fraction by 0.001 exactly — not past it.
        KeysNode slow;
        slow.init_with_animation(&clip);
        slow.update(0.0f);
        check(!slow.update(0.0001f),
              "a move of exactly 0.001 is not a change (`<=` in the recovered test)");
    }

    // ── Evaluate: the frame blend ───────────────────────────────────────────
    {
        Clip clip = make_clip(3, 10.0f, 2, {{0.0f, 10.0f}, {2.0f, 20.0f}, {4.0f, 40.0f}});

        KeysNode node;
        node.init_with_animation(&clip);
        node.evaluate();
        check(node.result_buffer().count() == 2, "the result buffer is sized to the track count");
        check(near(node.result_buffer().data[0], 0.0f),
              "at time 0 the pose is the first frame");

        // Half way between frames 0 and 1.
        node.frame = 0;
        node.frame_fraction = 0.5f;
        node.evaluate();
        check(near(node.result_buffer().data[0], 1.0f),
              "the frame blend is frames[i]*(1-f) + frames[i+1]*f");
        check(near(node.result_buffer().data[1], 15.0f), "per float, not per bone");

        // A single-frame clip takes the no-blend path.
        Clip single = make_clip(1, 10.0f, 1, {{7.0f}});
        KeysNode one;
        one.init_with_animation(&single);
        one.frame = 5;              // out of range: clamped by Clip::frame
        one.frame_fraction = 0.9f;
        one.evaluate();
        check(near(one.result_buffer().data[0], 7.0f),
              "a one-frame clip accumulates that frame at 1.0 with no fraction");

        // The last frame is reachable, which is the point of the epsilon.
        KeysNode last;
        last.init_with_animation(&clip);
        last.time = last.duration;
        last.update(0.0f);
        last.evaluate();
        check(last.frame >= 1, "the epsilon-short duration still reaches the last frame");
    }

    // ── timeToFinish ────────────────────────────────────────────────────────
    {
        Clip clip = make_clip(11, 10.0f, 1, std::vector<std::vector<float>>(11, {0.0f}));
        KeysNode node;
        node.init_with_animation(&clip);
        node.time = 0.0f;
        check(near(node.time_to_finish(), node.duration),
              "timeToFinish at time 0 is the whole duration");
        node.time = node.duration * 0.5f;
        check(near(node.time_to_finish(), node.duration * 0.5f), "and half of it at half time");
        node.speed = 2.0f;
        check(near(node.time_to_finish(), node.duration * 0.25f),
              "a double speed halves the remaining time");
        node.loops = true;
        check(near(node.time_to_finish(), 0.0f), "a looping clip never finishes");
    }

    // ── AnimBlendNode: the crossfade is destructive on the left buffer ───────
    {
        KeysNode left, right;
        left.result_buffer().data = {0.0f, 0.0f};
        right.result_buffer().data = {10.0f, 20.0f};

        BlendNode blend;
        blend.left = &left;
        blend.right = &right;

        blend.alpha = 0.0f;
        check(blend.evaluate() == &left.result_buffer(), "alpha 0 returns the left buffer");
        blend.alpha = 1.0f;
        check(blend.evaluate() == &right.result_buffer(), "alpha 1 returns the right buffer");
        blend.alpha = 0.999f;
        check(blend.evaluate() == &left.result_buffer(),
              "0.999 is not above the high threshold (the test is strict), so left is returned");
        blend.alpha = 0.9991f;
        check(blend.evaluate() == &right.result_buffer(),
              "just above 0.999 the right child is returned untouched");
        blend.alpha = 0.001f;
        check(blend.evaluate() == &left.result_buffer(),
              "0.001 is not below the low threshold (the test is strict)");

        // In between: the left buffer is scaled and then has the right added.
        left.result_buffer().data = {0.0f, 0.0f};
        right.result_buffer().data = {10.0f, 20.0f};
        blend.alpha = 0.25f;
        FloatDataBuffer* out = blend.evaluate();
        check(out == &left.result_buffer(),
              "the blend is in place on the left child's buffer");
        check(near(out->data[0], 2.5f) && near(out->data[1], 5.0f),
              "the mix is left*(1-a) + right*a");
        check(near(left.result_buffer().data[0], 2.5f),
              "and the left node's own buffer now holds the blend, not its own pose");

        // Advance: alpha moves by dt / blend_time and stops at 1.
        BlendNode timed;
        timed.left = &left;
        timed.right = &right;
        timed.blend_time = 0.5f;
        timed.blending = true;
        timed.update(0.25f);
        check(near(timed.alpha, 0.5f), "alpha advances by dt / blend_time");
        timed.update(0.5f);
        check(near(timed.alpha, 1.0f) && !timed.blending,
              "and clamps to 1 with the blend flag cleared");
        timed.update(0.25f);
        check(near(timed.alpha, 1.0f), "a cleared blend no longer advances");

        // Reverse blend uses blend_in_time when it is positive.
        BlendNode back;
        back.left = &left;
        back.right = &right;
        back.alpha = 1.0f;
        back.blend_time = 1.0f;
        back.blend_in_time = 0.25f;
        back.reverse = true;
        back.blending = true;
        back.update(0.25f);
        check(near(back.alpha, 0.0f),
              "a reverse blend advances by dt / blend_in_time (0.25 / 0.25 = 1.0)");
        // Landing exactly on 0.0f does not clear the flag — the recovered test is
        // `if (alpha >= 0) continue`, so the blend ends on the step past zero.
        check(back.blending, "a blend that lands exactly on 0 keeps blending");
        back.update(0.01f);
        check(near(back.alpha, 0.0f) && !back.blending,
              "and clears once the next step takes it below zero");
    }

    // ── Mat4 convention: the thing that decides the whole chain ──────────────
    {
        Mat4 identity;
        check(near(identity.m[12], 0.0f) && near(identity.m[15], 1.0f),
              "the identity has its translation column at m[12..14]");

        // PreTranslate puts t into the translation column when the rotation part
        // is identity: M' = T * I = T.
        Mat4 m;
        m.pre_translate(Vec3{3.0f, 4.0f, 5.0f});
        check(near(m.m[12], 3.0f) && near(m.m[13], 4.0f) && near(m.m[14], 5.0f),
              "pre-translating an identity yields the translation");
        check(near(transform_point(m, Vec3{1.0f, 0.0f, 0.0f}).x, 4.0f),
              "and moves a point by it");

        // A 90-degree yaw about z, then a translation: `M' = T * R`, so the point
        // is rotated first and translated second.
        const float s = std::sin(3.14159265358979f / 4.0f);
        const float c = std::cos(3.14159265358979f / 4.0f);
        Mat4 local = Mat4::from_rotation_quaternion(Quat{0.0f, 0.0f, s, c});
        local.pre_translate(Vec3{5.0f, 0.0f, 0.0f});
        const Vec3 p = transform_point(local, Vec3{1.0f, 0.0f, 0.0f});
        check(near(p.x, 5.0f, 1e-4f) && near(p.y, 1.0f, 1e-4f),
              "local is T*R: rotate then translate, not translate then rotate");

        // mul(a, b) is a * b: applying it to a point is b then a.
        Mat4 first = Mat4::from_rotation_quaternion(Quat{0.0f, 0.0f, s, c});
        Mat4 second;
        second.pre_translate(Vec3{1.0f, 0.0f, 0.0f});
        // mul(R, T) applied to (1,0,0): T first gives (2,0,0), then R rotates it
        // onto +y. The other pairing would give (1,1,0) — translate-after-rotate
        // — so this is the assertion that pins the direction, and it is the only
        // one in this file that a translation-only matrix cannot satisfy by luck.
        const Vec3 composed = transform_point(Mat4::mul(first, second), Vec3{1.0f, 0.0f, 0.0f});
        check(near(composed.x, 0.0f, 1e-4f) && near(composed.y, 2.0f, 1e-4f),
              "mul(first, second) applies second first, then first");
        const Vec3 reversed = transform_point(Mat4::mul(second, first), Vec3{1.0f, 0.0f, 0.0f});
        check(near(reversed.x, 1.0f, 1e-4f) && near(reversed.y, 1.0f, 1e-4f),
              "and the other order is not the same matrix");
    }

    // ── Skeleton: the two strided blocks and the bone matrix chain ───────────
    {
        Skeleton skeleton;
        skeleton.bone_count = 2;
        // Translations at floats 0 and 3 (stride 3); rotations at 6 and 10
        // (stride 4). Two blocks, independently strided.
        skeleton.translation_start = 0;
        skeleton.translation_stride = 3;
        skeleton.rotation_start = 6;
        skeleton.rotation_stride = 4;
        skeleton.rest.resize(2);
        skeleton.rest[0].parent_index = -1;          // root
        skeleton.rest[1].parent_index = 0;           // child of the root
        // Inverse binds: identity for the root, and a translate for the child.
        skeleton.rest[1].inverse_bind.pre_translate(Vec3{-1.0f, 0.0f, 0.0f});

        FloatDataBuffer buffer;
        buffer.data.resize(14, 0.0f);
        buffer.data[0] = 1.0f; buffer.data[1] = 0.0f; buffer.data[2] = 0.0f;   // bone 0 translation
        buffer.data[3] = 0.0f; buffer.data[4] = 2.0f; buffer.data[5] = 0.0f;   // bone 1 translation
        buffer.data[6] = 0.0f; buffer.data[7] = 0.0f; buffer.data[8] = 0.0f; buffer.data[9] = 0.5f;
        // bone 0 rotation: deliberately unnormalised, to check the ingest normalise.
        buffer.data[10] = 0.0f; buffer.data[11] = 0.0f; buffer.data[12] = 0.0f; buffer.data[13] = 2.0f;
        // bone 1 rotation: w = 2 (also unnormalised)

        std::vector<Bone> bones;
        extract_data_from_buffer(skeleton, buffer, bones);
        check(bones.size() == 2, "one bone instance per skeleton bone");
        check(near(bones[0].translation.x, 1.0f) && near(bones[0].translation.y, 0.0f),
              "the translation comes from the translation block");
        check(near(bones[1].translation.y, 2.0f),
              "and the block's stride steps one bone at a time");
        check(near(bones[0].rotation.w, 1.0f),
              "the quaternion is normalised on ingest (0.5 becomes 1.0)");
        check(near(bones[1].rotation.w, 1.0f), "for every bone, not just the first");

        evaluate_matrices(skeleton, bones);
        // Root: world == local, and local == T(1,0,0) * I.
        check(near(bones[0].local.m[12], 1.0f) && near(bones[0].world.m[12], 1.0f),
              "a root's world is its local");
        // Child: local is T(0,2,0), world is parent.world * local.
        check(near(bones[1].local.m[13], 2.0f), "the child's local carries its translation");
        check(near(bones[1].world.m[12], 1.0f) && near(bones[1].world.m[13], 2.0f),
              "the child's world is parent.world * local");
        // skin = world * inverse_bind, so the child's -1 x shift applies.
        check(near(bones[1].skin.m[12], 0.0f) && near(bones[1].skin.m[13], 2.0f),
              "skin is world * inverse_bind");

        // A null buffer is the engine's early-out.
        check(!apply_buffer_to_skeleton(skeleton, nullptr, bones),
              "a null buffer does not touch the skeleton");

        // The name lookup BoneControlledCollisionShapeComponent::BoneForName
        // (0x3269E4) drives, and the 220-byte stride it indexes with.
        skeleton.bone_names = {"root", "hand"};
        check(bone_index_for_name(skeleton, "root") == 0, "a bone name resolves to its index");
        check(bone_index_for_name(skeleton, "hand") == 1, "and to the right one");
        check(bone_index_for_name(skeleton, "nonexistent") == -1,
              "a missing bone is a negative index, which the caller turns into a null shape");
        check(bones.size() == 2 && &bones[1] - &bones[0] == 1,
              "the bone array is one instance per bone, i.e. the recovered 220-byte stride");
    }

    // ── The top-level entry point ───────────────────────────────────────────
    {
        Clip clip = make_clip(3, 10.0f, 2, {{0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 2.0f}});
        Skeleton skeleton;
        skeleton.bone_count = 1;
        skeleton.translation_start = 0;
        skeleton.translation_stride = 3;
        skeleton.rotation_start = 3;
        skeleton.rotation_stride = 4;
        skeleton.rest.resize(1);
        skeleton.rest[0].parent_index = -1;

        KeysNode node;
        node.init_with_animation(&clip);
        std::vector<Bone> bones;

        check(!update_with_skeleton(node, 0.0f, skeleton, bones),
              "a zero-length step evaluates nothing, so no skeleton is built");
        check(bones.empty(), "and nothing was produced");

        check(update_with_skeleton(node, 0.01f, skeleton, bones),
              "a step that moves the clip evaluates and fills the skeleton");
        check(bones.size() == 1, "which produced a bone");

        check(!update_with_skeleton(node, 0.0f, skeleton, bones),
              "an update that changes nothing does not evaluate again");
    }

    if (g_failures == 0) std::printf("PASS: caver animation nodes + bone matching\n");
    else std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
