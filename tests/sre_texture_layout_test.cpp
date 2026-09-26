/* sre_texture_layout_test.cpp — proves the guest texture-identity extraction
 *
 * THE CLAIM UNDER TEST
 * --------------------
 * src/sre/sre13/hooks/texture_layout.h reads Caver::Texture's raw memory to
 * answer "which texture is bound". Every offset in it is a claim about another
 * binary's struct layout, and a wrong offset does not crash - it silently
 * reports the wrong texture, which then receives the wrong material. That is
 * how the "every texture became a flat unlit box" regression happened.
 *
 * This test builds BYTE-EXACT libc++ std::string objects at the documented
 * offsets and asserts what the extractor returns. It deliberately constructs
 * the bytes by hand instead of using the host's std::string: the host is
 * libstdc++, whose string layout is completely different from the guest's
 * libc++ (libstdc++ is pointer-first with a 15-byte SSO buffer; libc++ is
 * flag-first with 23). Using a host std::string here would test nothing about
 * the guest.
 *
 * Offsets asserted (evidence in texture_layout.h):
 *     +32 std::string  resolved resource path
 *     +64 std::string  bare asset name
 *     +88 unsigned int GL texture name
 *
 * Build:  cmake target sre_texture_layout_test  (or see the ad-hoc line at the
 *         bottom of the file header in the task log)
 */

#include "sre/sre13/hooks/texture_layout.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks   = 0;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        ++g_checks;                                             \
        if (!(cond)) {                                          \
            ++g_failures;                                       \
            std::printf("  FAIL %s:%d  ", __FILE__, __LINE__);  \
            std::printf(__VA_ARGS__);                           \
            std::printf("\n");                                  \
        }                                                       \
    } while (0)

// ---------------------------------------------------------------- the model --
//
// A byte-exact stand-in for Caver::Texture, as far as the extractor can tell.
// Only the fields the extractor may touch are given real storage; the rest is
// padding that must never be read.
class GuestTexture {
public:
    static const size_t kBytes = 192;

    GuestTexture() : m_buf(kBytes, 0xAB) {   // 0xAB fill: poison for stray reads
        write_u32(SRE_TEX_OFF_GL_NAME, 0);
    }

    const void* raw() const { return m_buf.data(); }

    // ---- GL name (+88) -----------------------------------------------------
    void set_gl_name(unsigned int id) { write_u32(SRE_TEX_OFF_GL_NAME, id); }

    // ---- libc++ std::string writers ----------------------------------------
    //
    // Long representation: __cap_ (bit0 set) at +0, __size_ at +8, __data_ at +16.
    // libc++ sets __cap_ to the allocation size with the low bit forced on.
    void set_long(unsigned int off, const std::string& s) {
        // Heap backing keyed by field offset (offsets here are 32 and 64, both
        // < kHeapSlots). Held by the object so __data_ points at live memory for
        // as long as the model does - same lifetime contract as the guest's own
        // string, which the extractor must not assume beyond.
        std::vector<char>& heap = m_heap[off];
        heap.assign(s.begin(), s.end());
        heap.push_back('\0');

        write_u32(off + 0,  (unsigned int)(heap.size() | 1u));  // __cap_ low half
        write_u32(off + 4,  0u);                                 // __cap_ high half
        write_u32(off + 8,  (unsigned int)s.size());             // __size_ low
        write_u32(off + 12, 0u);                                 // __size_ high
        write_ptr(off + 16, (const void*)heap.data());           // __data_
    }

    // Short representation: __size_ == len << 1 (bit0 clear), data inline at +1.
    void set_short(unsigned int off, const std::string& s) {
        write_byte(off + 0, (unsigned char)(s.size() << 1));
        for (size_t i = 0; i < s.size(); ++i) write_byte(off + 1 + i, (unsigned char)s[i]);
        write_byte(off + 1 + s.size(), 0);
    }

    // Force a long-form string that LIES about its length, to prove the
    // buffer-bounds guard is real and not decorative.
    void set_long_with_fake_size(unsigned int off, const std::string& s, unsigned int fake) {
        set_long(off, s);
        write_u32(off + 8, fake);
    }

private:
    void write_byte(unsigned int off, unsigned char v) { m_buf[off] = (char)v; }
    void write_u32(unsigned int off, unsigned int v) {
        std::memcpy(&m_buf[off], &v, 4);
    }
    void write_ptr(unsigned int off, const void* p) {
        std::memcpy(&m_buf[off], &p, sizeof(p));
    }

    static const unsigned int kHeapSlots = 128;

    std::vector<char> m_buf;
    std::vector<char> m_heap[kHeapSlots];
};

// ---------------------------------------------------------------- the tests --

static void test_gl_name_offsets() {
    std::printf("test_gl_name_offsets\n");

    GuestTexture t;
    t.set_gl_name(0u);
    CHECK(sre_tex_gl_name(t.raw()) == 0u,
          "fresh texture must report GL name 0 (never written)");

    t.set_gl_name(0x1234u);
    CHECK(sre_tex_gl_name(t.raw()) == 0x1234u,
          "GL name must be read from +88, got %u", sre_tex_gl_name(t.raw()));

    // The one thing that must NOT happen: reading +88 as something else.
    // 0x1234 at +88 is exactly Texture::InitWithData's glGenTextures slot.
    CHECK(sre_tex_gl_name(nullptr) == 0u, "null texture must be safe");
}

static void test_short_strings() {
    std::printf("test_short_strings\n");

    GuestTexture t;
    t.set_short(SRE_TEX_OFF_ASSET, "grass_grass");

    char out[SRE_TEX_OFF_GL_NAME] = {0};
    const int ok = sre_tex_extract_name(t.raw(), out, sizeof(out));
    CHECK(ok == 1, "short asset name should extract");
    CHECK(std::strcmp(out, "grass_grass") == 0,
          "short asset name mismatch: '%s'", out);
}

static void test_long_strings() {
    std::printf("test_long_strings\n");

    // A path long enough to force libc++'s long representation (>22 bytes).
    const std::string path = "/data/data/com.touchfoo.swordigo/res/grass_grass.tex";

    GuestTexture t;
    t.set_long(SRE_TEX_OFF_PATH, path);

    char out[SRE_TEX_OFF_GL_NAME] = {0};
    const int ok = sre_tex_extract_name(t.raw(), out, sizeof(out));
    CHECK(ok == 1, "long path should extract");
    CHECK(std::string(out) == path, "long path mismatch: '%s'", out);
}

static void test_prefers_asset_name_over_path() {
    std::printf("test_prefers_asset_name_over_path\n");

    GuestTexture t;
    t.set_long(SRE_TEX_OFF_PATH,  "/x/y/grass_grass.tex");
    t.set_long(SRE_TEX_OFF_ASSET, "grass_grass");

    char out[SRE_TEX_OFF_GL_NAME] = {0};
    CHECK(sre_tex_extract_name(t.raw(), out, sizeof(out)) == 1, "should extract");
    CHECK(std::strcmp(out, "grass_grass") == 0,
          "+64 (bare asset name) must win over +32 (path), got '%s'", out);
}

static void test_falls_back_to_path_when_asset_empty() {
    std::printf("test_falls_back_to_path_when_asset_empty\n");

    GuestTexture t;
    t.set_long(SRE_TEX_OFF_PATH,  "/x/y/cavewalls.tex");
    t.set_short(SRE_TEX_OFF_ASSET, "");      // empty short string

    char out[SRE_TEX_OFF_GL_NAME] = {0};
    CHECK(sre_tex_extract_name(t.raw(), out, sizeof(out)) == 1,
          "must fall back to +32 when +64 is empty");
    CHECK(std::strcmp(out, "/x/y/cavewalls.tex") == 0,
          "fallback value mismatch: '%s'", out);
}

static void test_no_identity_clears_the_field() {
    std::printf("test_no_identity_clears_the_field\n");

    // This is FIX 1, the stale-identity bug. A texture with no name must leave
    // the destination EMPTY. The previous revision kept the previous texture's
    // name, and because a GL name is recycled after glDeleteTextures, a recycled
    // name could inherit a deleted texture's identity and its material.
    GuestTexture t;
    t.set_gl_name(77u);

    char out[SRE_TEX_OFF_GL_NAME];
    std::memset(out, 'X', sizeof(out));
    out[sizeof(out) - 1] = '\0';

    const int ok = sre_tex_extract_name(t.raw(), out, sizeof(out));
    CHECK(ok == 0, "unnamed texture must report no identity");
    CHECK(out[0] == '\0',
          "unnamed texture must CLEAR the destination, found '%s'", out);
}

static void test_rejects_binary_garbage() {
    std::printf("test_rejects_binary_garbage\n");

    // A half-initialised or wrong-offset std::string shows up as non-printable
    // bytes. Publishing those as a texture name is worse than publishing
    // nothing, because the name is what the whole trust gate keys on.
    GuestTexture t;
    t.set_short(SRE_TEX_OFF_ASSET, "ok\x01\x02""bad");

    char out[SRE_TEX_OFF_GL_NAME] = {0};
    CHECK(sre_tex_extract_name(t.raw(), out, sizeof(out)) == 0,
          "control bytes must be rejected as a name");
}

static void test_rejects_lying_length() {
    std::printf("test_rejects_lying_length\n");

    // __size_ larger than __cap_ means the object is not the layout we think it
    // is. Walking it would read past the allocation; refusing it degrades to
    // "no identity" instead.
    GuestTexture t;
    t.set_long_with_fake_size(SRE_TEX_OFF_ASSET, "grass", 4096u);

    char out[SRE_TEX_OFF_GL_NAME] = {0};
    CHECK(sre_tex_extract_name(t.raw(), out, sizeof(out)) == 0,
          "size > capacity must be refused (would read out of bounds)");
    CHECK(out[0] == '\0', "refused string must leave the destination empty");
}

static void test_null_and_truncation() {
    std::printf("test_null_and_truncation\n");

    char out[8];
    CHECK(sre_tex_extract_name(nullptr, out, sizeof(out)) == 0, "null tex");
    CHECK(out[0] == '\0', "null tex must clear");

    GuestTexture t;
    t.set_long(SRE_TEX_OFF_ASSET, "a_very_long_texture_name_indeed");

    std::memset(out, 0, sizeof(out));
    CHECK(sre_tex_extract_name(t.raw(), out, sizeof(out)) == 1, "should extract");
    CHECK(std::strlen(out) == sizeof(out) - 1,
          "must truncate to cap-1, got %zu", std::strlen(out));
    CHECK(out[sizeof(out) - 1] == '\0', "truncation must stay NUL-terminated");
}

static void test_poison_is_not_mistaken_for_a_name() {
    std::printf("test_poison_is_not_mistaken_for_a_name\n");

    // Fresh GuestTexture fills unused bytes with 0xAB, which as a libc++ short
    // string reads as is-long=1 with a garbage pointer. This is exactly the
    // real-world case of an offset pointing at a field nobody initialised.
    GuestTexture t;
    char out[SRE_TEX_OFF_GL_NAME] = {0};
    CHECK(sre_tex_extract_name(t.raw(), out, sizeof(out)) == 0,
          "0xAB poison must not yield an identity");
}

static void test_offsets_are_what_the_evidence_says() {
    std::printf("test_offsets_are_what_the_evidence_says\n");

    // Pin the constants themselves. A silent edit to one of these is the exact
    // failure mode this whole file exists to catch, so make it a test failure.
    CHECK(SRE_TEX_OFF_PATH      == 32u, "path offset drifted from InitWithFile");
    CHECK(SRE_TEX_OFF_ASSET     == 64u, "asset offset drifted from ~Texture");
    CHECK(SRE_TEX_OFF_GL_NAME   == 88u, "GL name offset drifted from glGenTextures");
    CHECK(SRE_TEX_OFF_IMAGE_TYPE == 56u, "ImageType offset drifted from InitWithFile");
}

int main() {
    std::printf("sre_texture_layout_test — Caver::Texture extraction\n");
    std::printf("===================================================\n");

    test_offsets_are_what_the_evidence_says();
    test_gl_name_offsets();
    test_short_strings();
    test_long_strings();
    test_prefers_asset_name_over_path();
    test_falls_back_to_path_when_asset_empty();
    test_no_identity_clears_the_field();
    test_rejects_binary_garbage();
    test_rejects_lying_length();
    test_null_and_truncation();
    test_poison_is_not_mistaken_for_a_name();

    std::printf("---------------------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
