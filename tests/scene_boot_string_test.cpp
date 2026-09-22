// scene_boot_string_test.cpp — the libc++ std::string contract behind direct scene boot.
//
// Ruby Touch boots the vanilla engine straight into a .scene by swapping the boot-scene
// std::string on CaverShell as CaverShell::InitView reads it (see
// docs/game/caver/scene/sceneloading/04_direct_scene_boot_feasibility_ruby_mobile.md).
// The runner writes that field through runner/stdstring.{h,c}, so that helper's layout and
// its inline/heap split ARE the interface to the engine:
//
//   libc++ __long  = { size_t __cap_; size_t __size_; pointer __data_; }
//                    long flag = __cap_ & 1   (low bit of the object's first byte)
//   libc++ __short = { unsigned char __size_; value_type __data_[__min_cap]; }
//                    short size = __size_ >> 1
//   __min_cap      = sizeof(__long) - 1  ->  22 usable chars on LP64 (arm64-v8a)
//                                            10 usable chars on ILP32 (armeabi-v7a)
//
// Two targets build this file:
//
//   scene_boot_string_lp64_test   STRING_SHORT_CAP=23                  (the host layout:
//                                 a 64-bit host's sizeof(__long) IS arm64's 24 bytes, so
//                                 this target exercises the real engine contract end to end)
//   scene_boot_string_ilp32_test  STRING_SHORT_CAP=11 TEST_ILP32_LAYOUT=1
//                                 (only the capacity contract can be modelled on a 64-bit
//                                 host — sizeof(__long) here is the host's 24, not arm32's
//                                 12 — so this target pins the *split* and the inline write
//                                 size, which is exactly what an LP64 threshold breaks)
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "stdstring.h"

#if defined(TEST_ILP32_LAYOUT)
#  define EXPECT_SHORT_CAP      11
#  define ABI_NAME              "armeabi-v7a (ILP32 capacity contract)"
#  define ENGINE_LONG_SIZE      12   /* what __long occupies on the real 32-bit target */
#else
#  define EXPECT_SHORT_CAP      23
#  define ABI_NAME              "arm64-v8a (LP64, full contract)"
#  define ENGINE_LONG_SIZE      ((int)sizeof(void*) == 8 ? 24 : 12)
#endif

// The engine field size we are allowed to assume on this host. On LP64 that is the real
// 24-byte std::string; on the ILP32 target we only model the 12-byte inline region.
static const size_t kEngineFieldSize = (size_t)ENGINE_LONG_SIZE;

static int g_failures = 0;

static void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("  FAIL  %s\n", what.c_str());
        ++g_failures;
    }
}

static void check_eq(long long got, long long want, const std::string& what) {
    if (got != want) {
        std::printf("  FAIL  %s (got %lld, want %lld)\n", what.c_str(), got, want);
        ++g_failures;
    }
}

// ── engine-side readers, written the way the engine writes them ──────────────
// Raw pointer arithmetic over the expected layout rather than the String_* helpers, so a
// reordering of String_Long's fields fails here instead of silently inside the game.
static bool engine_is_long(const unsigned char* raw) { return (raw[0] & 1u) != 0; }

static size_t engine_length(const unsigned char* raw) {
    if (engine_is_long(raw)) {
        size_t v;
        std::memcpy(&v, raw + sizeof(size_t), sizeof(v));   // __long.__size_
        return v;
    }
    return (size_t)(raw[0] >> 1);
}

static const char* engine_data(const unsigned char* raw) {
    if (engine_is_long(raw)) {
        const char* p;
        std::memcpy(&p, raw + 2 * sizeof(size_t), sizeof(p));   // __long.__data_
        return p;
    }
    return reinterpret_cast<const char*>(raw + 1);               // __short.__data_
}

// ── canary-guarded field ────────────────────────────────────────────────────
// A std::string embedded in the engine object: `field_size` bytes of string, with poison
// on both sides. Anything written outside `field_size` is real heap corruption in game.
static const unsigned char kPadByte = 0xA5;
static const size_t kPad = 32;

struct GuardedField {
    std::vector<unsigned char> bytes;
    size_t field_size;

    explicit GuardedField(size_t size) : bytes(kPad + size + kPad, kPadByte), field_size(size) {}
    unsigned char* field() { return bytes.data() + kPad; }
    bool pads_intact() const {
        for (size_t i = 0; i < kPad; ++i)
            if (bytes[i] != kPadByte) return false;
        for (size_t i = kPad + field_size; i < bytes.size(); ++i)
            if (bytes[i] != kPadByte) return false;
        return true;
    }
};

// The engine's own constructor zero-stores this field before anything writes it
// (CaverShell::CaverShell: `*(_OWORD*)(this + 104) = 0`), so a fresh field is a valid
// empty std::string, not uninitialised bytes. Anything else would make String_destroy()
// read a garbage __data_ pointer and free it — which is also why hook_InitView() inspects
// the field before touching it.
static void init_engine_field(GuardedField& f, const char* value) {
    String* s = reinterpret_cast<String*>(f.field());
    std::memset(f.field(), 0, f.field_size);
    String_create(s, value);
}

static std::string name_of_length(int len) {
    std::string name((size_t)len, 'a');
    for (int i = 0; i < len; ++i) name[(size_t)i] = (char)('a' + (i % 26));
    return name;
}

// ── LP64: the full contract, byte for byte ──────────────────────────────────
static void run_lp64_suite() {
    check_eq((long long)sizeof(String), (long long)kEngineFieldSize, "sizeof(std::string)");
    check_eq((long long)sizeof(String_Long), (long long)kEngineFieldSize, "sizeof(__long)");
    check_eq((long long)sizeof(String_Short), (long long)kEngineFieldSize, "sizeof(__short)");
    check_eq((long long)sizeof(String_Long) - 1, (long long)STRING_SHORT_CAP,
             "__min_cap == sizeof(__long) - 1");

    // The engine sets "menu" on every cold boot; the hook then re-seats it. Both paths.
    const char* names[] = {
        "menu",            // the value the engine itself writes
        "town_part1",
        "forest_part1",
        "Swordigo_Level_With_A_Very_Long_Name_0123",
        "",
    };
    for (const char* name : names) {
        const std::string label = std::string("'") + name + "'";
        GuardedField f(kEngineFieldSize);
        String* s = reinterpret_cast<String*>(f.field());
        init_engine_field(f, "menu");   // what the engine's setupApplication leaves behind
        String_destroy(s);
        String_create(s, name);

        unsigned char* raw = f.field();
        check_eq((long long)String_size(s), (long long)std::strlen(name), label + ": String_size");
        check(std::strcmp(String_get(s), name) == 0, label + ": String_get");
        check_eq((long long)engine_length(raw), (long long)std::strlen(name),
                 label + ": engine reads the same length");
        check(std::strcmp(engine_data(raw), name) == 0, label + ": engine reads the same bytes");
        check(f.pads_intact(), label + ": stayed inside sizeof(std::string)");

        const size_t len = std::strlen(name);
        if (len < (size_t)STRING_SHORT_CAP)
            check(!String_isLong(s), label + ": must be stored inline");
        else
            check(String_isLong(s), label + ": must take the heap path");
    }

    // Long then short: the heap buffer has to be released before the inline value is
    // seated, or the engine's own destructor later frees a pointer that has been
    // overwritten with characters.
    {
        GuardedField f(kEngineFieldSize);
        String* s = reinterpret_cast<String*>(f.field());
        init_engine_field(f, "Swordigo_Level_With_A_Very_Long_Name_0123");
        check(String_isLong(s), "long first: heap path");
        String_destroy(s);
        String_create(s, "menu");
        check(!String_isLong(s), "re-seated short: inline again");
        check(std::strcmp(String_get(s), "menu") == 0, "re-seated short: value");
        check(f.pads_intact(), "re-seated short: no out-of-range write");
    }

    // Every length across both sides of the split, including the exact boundary.
    for (int len = 0; len <= 40; ++len) {
        const std::string name = name_of_length(len);
        GuardedField f(kEngineFieldSize);
        String* s = reinterpret_cast<String*>(f.field());
        init_engine_field(f, name.c_str());
        const std::string label = "len " + std::to_string(len);
        check_eq((long long)String_size(s), len, label + ": size");
        check(std::strcmp(String_get(s), name.c_str()) == 0, label + ": bytes");
        check(f.pads_intact(), label + ": no out-of-range write");
        bool expect_long = len >= STRING_SHORT_CAP;
        check_eq((long long)(String_isLong(s) ? 1 : 0), (long long)(expect_long ? 1 : 0),
                 label + ": split matches the ABI threshold");
    }
}

// ── ILP32: the capacity contract, which is what a wrong threshold breaks ────
static void run_ilp32_suite() {
    // sizeof(__short) is host-independent: 1 size byte + __min_cap inline bytes.
    check_eq((long long)sizeof(String_Short), (long long)(1 + STRING_SHORT_CAP),
             "sizeof(__short) == 1 + __min_cap");
    check_eq((long long)(1 + STRING_SHORT_CAP), (long long)kEngineFieldSize,
             "inline region must equal arm32's 12-byte std::string");

    // A name that fits must be written entirely inside the engine's 12 bytes. This is the
    // case a 23-threshold silently corrupts: 11-22 character names (e.g. "forest_part1")
    // would be inline-copied past the end of the real field.
    for (int len = 0; len <= STRING_SHORT_CAP - 1; ++len) {
        const std::string name = name_of_length(len);
        GuardedField f(kEngineFieldSize);
        String* s = reinterpret_cast<String*>(f.field());
        init_engine_field(f, name.c_str());
        const std::string label = "inline len " + std::to_string(len);
        check(!String_isLong(s), label + ": must be inline on arm32");
        check_eq((long long)String_size(s), len, label + ": size");
        check(std::strcmp(String_get(s), name.c_str()) == 0, label + ": bytes");
        check(f.pads_intact(), label + ": stayed inside the 12-byte field");
    }

    // One character more must leave the inline buffer behind. Verified on a host-sized
    // buffer because __long itself is 24 bytes here, not arm32's 12 — the claim under test
    // is the *decision*, not arm32's heap layout.
    for (int len = STRING_SHORT_CAP; len <= 2 * STRING_SHORT_CAP; ++len) {
        const std::string name = name_of_length(len);
        GuardedField f(sizeof(String));
        String* s = reinterpret_cast<String*>(f.field());
        init_engine_field(f, name.c_str());
        const std::string label = "heap len " + std::to_string(len);
        check(String_isLong(s), label + ": must leave the inline buffer");
        check_eq((long long)String_size(s), len, label + ": size");
        check(std::strcmp(String_get(s), name.c_str()) == 0, label + ": bytes");
        check(f.pads_intact(), label + ": no out-of-range write");
        String_destroy(s);
    }

    std::printf("  note: __long byte layout is only observable on a 32-bit target; "
                "the %s target covers it\n", "arm64/ILP32");
}

int main() {
    std::printf("scene_boot_string_test — %s\n", ABI_NAME);
    check_eq((long long)STRING_SHORT_CAP, (long long)EXPECT_SHORT_CAP, "compile-time threshold");

#if defined(TEST_ILP32_LAYOUT)
    run_ilp32_suite();
#else
    run_lp64_suite();
#endif

    if (g_failures) {
        std::printf("FAIL: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("PASS: runner std::string contract holds for %s\n", ABI_NAME);
    return 0;
}
