#include "../caver/RenderingContext.h"

G_DL_SYMBOL(
	RenderingContext_CurrentContext,
	"_ZN5Caver16RenderingContext14CurrentContextEv",
	RenderingContext*, (void)
);

G_DL_SYMBOL(
	RenderingContext_SetCurrentContext,
	"_ZN5Caver16RenderingContext17SetCurrentContextEPS0_",
	void, (RenderingContext *ctx)
);

G_DL_SYMBOL(
	RenderingContext_Clear,
	"_ZN5Caver16RenderingContext5ClearEbbb",
	void, (RenderingContext *ctx, bool color, bool depth, bool stencil)
);

G_DL_SYMBOL(
	RenderingContext_SetColor,
	"_ZN5Caver16RenderingContext8SetColorERKNS_5ColorE",
	void, (RenderingContext *ctx, void *color)
);

G_DL_SYMBOL(
	RenderingContext_SetAlpha,
	"_ZN5Caver16RenderingContext8SetAlphaEf",
	void, (RenderingContext *ctx, float a)
);

G_DL_SYMBOL(
	RenderingContext_SetMatrix,
	"_ZN5Caver16RenderingContext9SetMatrixERKNS_7Matrix4E",
	void, (RenderingContext *ctx, Matrix4 *mat)
);

G_DL_SYMBOL(
	RenderingContext_SetViewport,
	"_ZN5Caver16RenderingContext11SetViewportERKNS_9RectangleE",
	void, (RenderingContext *ctx, Rectangle *rect)
);

#include "../core/stdstring.h"
#include "texture_layout.h"

// libsre13 is linked with -nostdlib -fno-builtin, so do not reach for strcmp.
static int sre__streq(const char *a, const char *b) {
	while (*a && *a == *b) { ++a; ++b; }
	return *a == *b;
}

// =============================================================================
//  TEXTURE IDENTITY CHANNEL
// =============================================================================
//  The host is a renderer for a GLES1 *scene describer*: the guest never issues
//  its own draws, it only says what to draw. So the host needs to know, per
//  draw, WHICH texture is bound - otherwise every texture-aware feature
//  (material overrides, grass sway, water reflections, PDI asset class) is
//  guessing, and guessing is what produced the "every texture became a flat
//  unlit box" regression.
//
//  This file is the authoritative channel. It is the ONLY place that reads
//  Caver::Texture's layout, and every offset below is evidence-backed:
//
//    +32  std::string  #1 - the resolved resource path. Written by
//                         Texture::InitWithFile @0x54EDBC
//                           `std::string::operator=(a1 + 32, a2)`
//                         and by Texture::InitWithResource @0x54D278
//                           `std::string::operator=(a1 + 32, v27)`
//                         where v27 = PathForResourceOfType(name, "tex").
//                         e.g. ".../grass_grass.tex" - so it is a PATH, not a
//                         bare name; consumers must take the basename.
//    +56  _DWORD      ImageType (InitWithFile writes a3 here).
//    +64  std::string  #2 - the message/asset name. Confirmed as a second
//                         std::string by ~Texture @0x54CCF0, which frees BOTH
//                         inline-flag/heap-pointer pairs:
//                           if (*(_BYTE *)(this + 64) & 1) delete *(this + 80);
//                           if (*(_BYTE *)(this + 32) & 1) delete *(this + 48);
//                         A libc++ std::string at offset N has its "is long"
//                         flag at N and its heap pointer at N+16, and +48/+80
//                         are exactly +32+16 and +64+16.
//    +88  uint32       the GL texture name. Corroborated at three sites:
//                         Texture::InitWithData @0x54EE74 `glGenTextures(1, a1+88)`
//                         Texture::BindTexture  @0x515FB0 `*((_DWORD*)a2 + 22)`
//                         ~Texture              @0x54CCF0 `glDeleteTextures(1, this+88)`
//
//  std::string layout read by String_get() (core/stdstring.h) is the libc++
//  one: byte 0 low bit = is-long, inline data at +1 (23 bytes), and for long
//  strings cap/size/data at +0/+8/+16. sizeof(__long) - 1 == 23 is the short
//  capacity, which is why STRING_SHORT_CAP is 23 on LP64.
//
//  ---------------------------------------------------------------------------
//  TWO BUGS FIXED HERE, both of which made "texture awareness" silently wrong:
//
//  1. STALE IDENTITY. The previous revision only wrote the name/id when the
//     name was non-empty, so a texture with an empty name inherited whatever
//     was bound before it and the export kept that older texture's GL id. The
//     host pairs id+name and only accepts a match, so the visible symptom was
//     an intermittent gap - but a GL name is recycled by the driver after
//     glDeleteTextures, so a recycled name could inherit a previous texture's
//     identity and receive its material. The exports are now rewritten on
//     EVERY BindTexture call including the NULL/unbind path and the
//     empty-name path, so a name can never be attributed to a texture it did
//     not come from.
//
//  2. NO PROOF. There was no way to tell whether the channel worked at all.
//     There is now a ring buffer plus a distinct-name set, both readable by the
//     host from guest memory, so "which textures did this level actually bind"
//     is a measurement instead of a belief. See the F3 debug panel.
//  ---------------------------------------------------------------------------
//
//  Do NOT add a third guess. If a new field is needed, disassemble the method
//  that writes it first; the whole point of this comment block is that the
//  offsets are checked, not assumed.
// =============================================================================

#define SRE_TEX_NAME_CAP      64
#define SRE_TEX_TRACE_SLOTS  256
#define SRE_TEX_UNIQUE_SLOTS  96
#define SRE_TEX_UNIQUE_NAME   32

typedef struct {
	unsigned int id;
	unsigned int seq;
	char         name[SRE_TEX_NAME_CAP];
} SreTextureTraceEntry;

// Latest bind. Kept, with these exact names, because jni_bridge_arm64.cpp already
// resolves them by symbol at runtime.
__attribute__((visibility("default"))) char     g_sre_last_bound_texture_name[SRE_TEX_NAME_CAP] = {0};
__attribute__((visibility("default"))) unsigned int g_sre_last_bound_texture_id = 0;

// Ring buffer of every bind that carried an identity. `g_sre_texture_trace_head`
// is a MONOTONIC counter, not a wrapped index - the caller reads
//   for (i = head > SLOTS ? head - SLOTS : 0; i < head; ++i) trace[i % SLOTS]
// so a single counter is enough and there is no read/write race on the index.
__attribute__((visibility("default"))) SreTextureTraceEntry g_sre_texture_trace[SRE_TEX_TRACE_SLOTS] = {{0}};
__attribute__((visibility("default"))) unsigned int g_sre_texture_trace_head = 0;

// Distinct texture names bound since the set was last cleared. This is the
// per-level answer: the GUEST clears it on scene load (see Scene.c) and the host
// logs what accumulates. The host must not clear it itself - it cannot call into
// this library at all, because this is ARM64 code running under the emulator.
__attribute__((visibility("default"))) char         g_sre_texture_unique_names[SRE_TEX_UNIQUE_SLOTS][SRE_TEX_UNIQUE_NAME] = {{0}};
__attribute__((visibility("default"))) unsigned int g_sre_texture_unique_count = 0;

// Counters. `binds` is every call; `identified` is those that produced a
// non-empty name. If identified stays 0 while binds climbs, the offsets above
// are wrong for this build - which is exactly the signal that was missing.
__attribute__((visibility("default"))) unsigned int g_sre_texture_bind_calls = 0;
__attribute__((visibility("default"))) unsigned int g_sre_texture_identified_calls = 0;

// Start a new per-level texture-identity set. Called BY GUEST CODE from the
// Scene_LoadFromFile hook; exported with default visibility only so the host can
// resolve its address for diagnostics. The counters are cleared here too, so the
// three numbers the host reports (binds, identified, distinct) all describe the
// CURRENT level rather than a running total since process start.
__attribute__((visibility("default"))) void sre_texture_reset_unique_set(void) {
	g_sre_texture_unique_count = 0;
	g_sre_texture_bind_calls = 0;
	g_sre_texture_identified_calls = 0;
	for (int i = 0; i < SRE_TEX_UNIQUE_SLOTS; ++i) {
		g_sre_texture_unique_names[i][0] = '\0';
	}
}

// Caver::Texture field extraction lives in texture_layout.h so that it can be
// exercised on the host against a byte-exact libc++ model - see
// tests/sre_texture_layout_test.cpp. This file must not re-derive an offset.

HOOK_SYMBOL(
	RenderingContext_BindTexture_Hook,
	"_ZN5Caver16RenderingContext11BindTextureEPNS_7TextureE",
	void, (RenderingContext *ctx, void *tex)
) {
	// Guest RenderingContext::BindTexture @0x515FB0 dedupes: it only calls
	// glBindTexture when the GL name differs from the cached one at +236. So
	// this hook fires exactly once per real texture SWITCH, and the host's
	// glBindTexture hook fires immediately after with the same id still live in
	// the globals below. That ordering is what makes the pairing sound, and it
	// only holds because this hook writes the exports BEFORE calling orig.
	unsigned int glId = 0;
	int identified = 0;

	if (tex) {
		// +88 FIRST, and the name strings only if it looks like a live texture.
		//
		// The GL name is the one field the game itself reads here: the original
		// BindTexture dedupes on it and hands it to glBindTexture, so a texture
		// that is actually being bound always has a non-zero one. The two
		// std::strings at +32/+64 are NOT read by the original, so this hook is
		// the only code that touches them - and reading a heap pointer out of a
		// half-initialised object is exactly the wild dereference that
		// tests/sre_texture_layout_test.cpp caught on a 0xAB-filled field. Gate
		// the string read on the field the game has already validated, and the
		// object is at least a texture-shaped thing before we follow a pointer.
		glId = sre_tex_gl_name(tex);
		if (glId != 0u) {
			// sre_tex_extract_name() CLEARS dst before it tries, so a texture with
			// no usable name publishes an explicitly empty name rather than
			// inheriting the previous texture's - FIX 1. It also validates length
			// against the string's own capacity and rejects non-printable bytes,
			// so a wrong offset degrades to "no identity" instead of to garbage
			// identity.
			identified = sre_tex_extract_name(tex, g_sre_last_bound_texture_name, SRE_TEX_NAME_CAP);
		} else {
			// No GL name yet: nothing was uploaded, nothing can be drawn from it,
			// so there is no identity to publish. Clear rather than keep the
			// previous texture's name.
			g_sre_last_bound_texture_name[0] = '\0';
		}
	} else {
		g_sre_last_bound_texture_name[0] = '\0';
	}

	// FIX 1 continued: the id is written unconditionally, so it can never
	// disagree with the name beside it.
	g_sre_last_bound_texture_id = glId;

	++g_sre_texture_bind_calls;
	if (identified) {
		++g_sre_texture_identified_calls;

		SreTextureTraceEntry *e =
			&g_sre_texture_trace[g_sre_texture_trace_head % SRE_TEX_TRACE_SLOTS];
		e->id  = glId;
		e->seq = g_sre_texture_trace_head;
		for (int i = 0; i < SRE_TEX_NAME_CAP; ++i) {
			e->name[i] = g_sre_last_bound_texture_name[i];
			if (!e->name[i]) break;
		}
		e->name[SRE_TEX_NAME_CAP - 1] = '\0';
		++g_sre_texture_trace_head;

		// Distinct-name set, truncated to a comparable stem: the same texture
		// arrives as both a path and a bare name depending on which field was
		// populated, and "_2x" variants are the same surface at higher res.
		char stem[SRE_TEX_UNIQUE_NAME];
		int n = 0;
		for (; n < SRE_TEX_UNIQUE_NAME - 1 && e->name[n]; ++n) stem[n] = e->name[n];
		stem[n] = '\0';

		int seen = 0;
		for (unsigned int i = 0; i < g_sre_texture_unique_count; ++i) {
			if (sre__streq(g_sre_texture_unique_names[i], stem)) { seen = 1; break; }
		}
		if (!seen && g_sre_texture_unique_count < SRE_TEX_UNIQUE_SLOTS) {
			for (int i = 0; i < SRE_TEX_UNIQUE_NAME; ++i) {
				g_sre_texture_unique_names[g_sre_texture_unique_count][i] = stem[i];
				if (!stem[i]) break;
			}
			g_sre_texture_unique_names[g_sre_texture_unique_count][SRE_TEX_UNIQUE_NAME - 1] = '\0';
			++g_sre_texture_unique_count;
		}
	}

	if (orig_RenderingContext_BindTexture_Hook) {
		orig_RenderingContext_BindTexture_Hook(ctx, tex);
	}
}

G_DL_SYMBOL(
	RenderingContext_BindTexture,
	"_ZN5Caver16RenderingContext11BindTextureEPNS_7TextureE",
	void, (RenderingContext *ctx, void *tex)
);

G_DL_SYMBOL(
	RenderingContext_SetBlendingEnabled,
	"_ZN5Caver16RenderingContext18SetBlendingEnabledEb",
	void, (RenderingContext *ctx, bool enabled)
);

G_DL_SYMBOL(
	RenderingContext_PrepareForDrawing,
	"_ZN5Caver16RenderingContext17PrepareForDrawingEv",
	void, (RenderingContext *ctx)
);

G_DL_SYMBOL(
	RenderingContext_UseProgram,
	"_ZN5Caver16RenderingContext10UseProgramEj",
	void, (RenderingContext *ctx, unsigned int prog)
);

G_DL_SYMBOL(
	RenderingContext_DrawTexture,
	"_ZN5Caver16RenderingContext11DrawTextureERKN5boost13intrusive_ptrINS_7TextureEEERKNS_9RectangleES9_f",
	void, (RenderingContext *ctx, void *intrusive_tex, Rectangle *src, Rectangle *dst, float rot)
);
