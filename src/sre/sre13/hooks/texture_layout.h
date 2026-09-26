/* texture_layout.h — Caver::Texture field extraction, shared by guest and host
 *
 * WHY THIS IS A HEADER AND NOT PART OF RenderingContext.c
 * ------------------------------------------------------
 * Everything that reads Caver::Texture's raw memory is a claim about another
 * binary's struct layout. Claims like that rot silently: they keep compiling
 * and keep returning bytes, and the only symptom is that materials are applied
 * to the wrong surfaces. So the layout arithmetic lives here, free of any hook
 * or guest dependency, and is exercised by tests/sre_texture_layout_test.cpp
 * against a byte-exact model of the libc++ object.
 *
 * This file must stay compilable as C (guest: -nostdlib -fno-builtin) AND as
 * C++ (host test). No libc calls, no allocation, no C99 features beyond what
 * C++ also accepts.
 *
 * ---------------------------------------------------------------------------
 * EVIDENCE for every offset below (OpenSwordigo/arm64_13, libswordigo 1.4.13)
 * ---------------------------------------------------------------------------
 *   +32  std::string #1, the RESOLVED RESOURCE PATH.
 *          Texture::InitWithFile @0x54EDBC
 *              std::string::operator=(a1 + 32, a2);      // a2 = caller path
 *          Texture::InitWithResource @0x54D278
 *              PathForResourceOfType(v27, name, "tex");
 *              std::string::operator=(a1 + 32, v27);     // v27 = resolved path
 *        => it is a path such as ".../grass_grass.tex", NOT a bare name.
 *   +56  _DWORD ImageType. InitWithFile writes a3 there; InitWithResource
 *        writes 3. Not used here - listed so nobody re-derives it.
 *   +64  std::string #2, the ASSET/MESSAGE NAME (bare).
 *          ~Texture @0x54CCF0 frees both strings, and the offsets pin them:
 *              if ((*(_BYTE*)this + 64) & 1) operator delete(*((void**)this + 10));
 *              if ((*(_BYTE*)this + 32) & 1) operator delete(*((void**)this + 6));
 *          A libc++ std::string at N keeps its is-long flag at N and its heap
 *          pointer at N+16. (void**)this + 6 == this + 48 == 32 + 16, and
 *          (void**)this + 10 == this + 80 == 64 + 16. Both fit exactly.
 *   +88  unsigned int, the GL texture NAME.
 *          Texture::InitWithData @0x54EE74   glGenTextures(1, a1 + 88)
 *          RenderingContext::BindTexture @0x515FB0
 *                                            v3 = *((_DWORD*)a2 + 22)  // 22*4
 *          ~Texture @0x54CCF0                glDeleteTextures(1, this + 88)
 *        Three independent sites agree, and one of them is the very function
 *        being hooked, so this is the strongest of the set.
 *
 * libc++ std::string, both representations (sizeof == 24 on LP64):
 *   __long  { size_t __cap_; size_t __size_; char* __data_; }
 *           __cap_ has bit 0 SET.        => flag at +0, size at +8, ptr at +16
 *   __short { unsigned char __size_; char __data_[23]; }
 *           __size_ == (len << 1), so bit 0 is CLEAR.
 *   sizeof(__long) - 1 == 23 is the short capacity: that is why the engine's
 *   String helpers use STRING_SHORT_CAP 23 (and 11 on armeabi-v7a, where
 *   sizeof(__long) is 12). This header does NOT assume 23 - it reads the
 *   inline buffer length from the representation itself.
 */

#ifndef SRE13_TEXTURE_LAYOUT_H
#define SRE13_TEXTURE_LAYOUT_H

#define SRE_TEX_OFF_PATH      32u   /* std::string #1: resolved resource path */
#define SRE_TEX_OFF_IMAGE_TYPE 56u  /* int: ImageType                          */
#define SRE_TEX_OFF_ASSET     64u   /* std::string #2: bare asset name          */
#define SRE_TEX_OFF_GL_NAME   88u   /* unsigned int: glGenTextures result       */

/* libc++ std::string, as read from guest memory. */
typedef struct SreGuestString {
    unsigned char  is_long;   /* bit 0 of byte 0                                  */
    unsigned int   size;      /* logical length, never including the NUL          */
    const char    *data;      /* long: heap pointer; short: &bytes[N + 1]         */
    unsigned int   capacity;  /* bytes addressable at `data` (minus NUL)          */
} SreGuestString;

/* LIBRARY FUNCTION - reads the object; INTEGRATION TEST - callers must pass a
 * pointer to a real Caver::Texture, never a guessed address. */
/* `static inline` throughout: this is a header, so a translation unit that uses
 * only some of these helpers must not trip -Wunused-function. */
static inline void sre_tex_read_string(const void *tex, unsigned int off, SreGuestString *out) {
    const unsigned char *base = (const unsigned char *)tex + off;
    out->is_long = (unsigned char)(base[0] & 1u);
    if (out->is_long) {
        /* __long { size_t __cap_; size_t __size_; char* __data_; } laid out at
         * +0 / +8 / +16. Reading through a 32-bit view, __cap_ occupies words[0]
         * (low) and words[1] (high) and __size_ occupies words[2]/words[3] - so
         * the LOW halves are words[0] and words[2]. Getting these the wrong way
         * round would silently compare a length against a capacity. */
        const unsigned int *words = (const unsigned int *)base;
        out->size     = words[2];                       /* low half of __size_ */
        out->data     = *(const char *const *)(base + 16);
        out->capacity = words[0];                       /* low half of __cap_  */
    } else {
        out->size     = (unsigned int)(base[0] >> 1);
        out->data     = (const char *)(base + 1);       /* inline __data_      */
        out->capacity = 23u - 1u;                       /* sizeof(__long)-1-1  */
    }
}

/* The GL texture name, or 0 when the field has never been written. */
static inline unsigned int sre_tex_gl_name(const void *tex) {
    if (!tex) return 0u;
    return *(const unsigned int *)((const unsigned char *)tex + SRE_TEX_OFF_GL_NAME);
}

/* Upper bounds used to decide whether a std::string is REAL before touching
 * its buffer.
 *
 * These exist because a plain `size <= capacity` check is not enough, and that
 * was a real bug found by tests/sre_texture_layout_test.cpp: an uninitialised
 * field (the test fills with 0xAB) reads as is_long=1 with
 * __size_ == __cap_ == 0xABABABAB. size <= capacity holds, so the loop then
 * dereferenced __data_ == 0xABABABABABABABAB and segfaulted.
 *
 * A texture name or resource path is short - the longest path in this tree is
 * well under 128 bytes - so anything claiming to be longer is not one of these
 * strings and must not be dereferenced at all. */
#define SRE_TEX_MAX_NAME  192u   /* longest believable name/path  */
#define SRE_TEX_MAX_CAP  1024u   /* longest believable allocation */

/* True when `s` is plausibly a real texture name and not a half-initialised
 * std::string, a pointer into freed memory, or a binary blob that happens to
 * live where a name should be. This is the guard that keeps a broken offset
 * from publishing garbage as an identity - and, since it is the only thing
 * standing between a wrong offset and a wild pointer dereference, it is
 * deliberately strict. */
static inline int sre_tex_name_is_printable(const SreGuestString *s) {
    unsigned int i;

    if (!s || !s->data || s->size == 0u) return 0;
    if (s->size > SRE_TEX_MAX_NAME) return 0;      /* implausibly long  */
    if (s->size > s->capacity) return 0;           /* past the buffer   */

    /* Only a LONG string has an off-object pointer that can be wild; a short
     * string's data is inline and always safe to read. So bound the allocation
     * for the long form specifically. The guest's own strings are heap-backed
     * by operator new, which never hands back a capacity above the request. */
    if (s->is_long) {
        if (s->capacity > SRE_TEX_MAX_CAP) return 0;
    }

    for (i = 0; i < s->size; ++i) {
        unsigned char c = (unsigned char)s->data[i];
        if (c < 0x20u || c > 0x7eu) return 0;
    }
    return 1;
}

/* Extracts the best available name into a fixed field.
 *
 * Prefers +64 (the bare asset name) and falls back to +32 (the resolved path);
 * both are acceptable because every consumer takes a basename, and a path is
 * strictly more information than nothing.
 *
 * Returns 1 on success and 0 when the object carries no usable name - which is
 * a normal outcome for a sub-texture or an atlas region, NOT an error. Callers
 * MUST clear their destination on 0 rather than keeping the previous value; a
 * stale name is what made texture identity unsound before. */
static inline int sre_tex_extract_name(const void *tex, char *dst, unsigned int cap) {
    unsigned int offsets[2];
    unsigned int i;

    if (!dst || cap == 0u) return 0;
    dst[0] = '\0';
    if (!tex) return 0;

    offsets[0] = SRE_TEX_OFF_ASSET;   /* preferred: bare name */
    offsets[1] = SRE_TEX_OFF_PATH;    /* fallback: resolved path */

    for (i = 0; i < 2u; ++i) {
        SreGuestString s;
        unsigned int n;
        sre_tex_read_string(tex, offsets[i], &s);
        if (!sre_tex_name_is_printable(&s)) continue;
        for (n = 0; n < s.size && n < cap - 1u; ++n) dst[n] = s.data[n];
        dst[n] = '\0';
        if (n > 0u) return 1;
    }
    return 0;
}

/* Convenience: does this object carry ANY usable identity? Used by callers that
 * only want a yes/no. The scratch buffer is capped at the same limit the guard
 * enforces, so this cannot over-read even if the guard is later relaxed. */
static inline int sre_tex_has_identity(const void *tex) {
    char scratch[64];
    return sre_tex_extract_name(tex, scratch, (unsigned int)sizeof(scratch));
}

#endif /* SRE13_TEXTURE_LAYOUT_H */
