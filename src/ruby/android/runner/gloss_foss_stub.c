/* gloss_foss_stub.c — FOSS-compliant no-op stubs for the proprietary GlossHook API.
 *
 * When RUBY_FOSS_COMPLIANT is defined (FOSS flavor builds), the prebuilt
 * libGlossHook.so is neither linked nor shipped. These stubs satisfy the linker
 * and make every hook call a safe no-op: GlossInit does nothing, every
 * GlossHookByName returns NULL (hook unavailable — callers already handle NULL),
 * and GlossHookDelete is a no-op. The play-in-game feature is compiled out at
 * the UI layer (see RubyFileModel::isPlayInGameAvailable), so these stubs are
 * effectively unreachable in a FOSS build; they exist so the runner sources
 * compile and link unchanged.
 *
 * Full (non-FOSS) builds leave this translation unit empty and use the real
 * implementations from the prebuilt libGlossHook.so instead.
 */
#ifdef RUBY_FOSS_COMPLIANT

#include "Gloss.h"
#include <android/log.h>

#define LOG_TAG "GlossFossStub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void GlossInit(bool is_init_linker)
{
    (void)is_init_linker;
    LOGI("FOSS build: GlossHook engine unavailable — VFS hooks disabled");
}

GHook GlossHookByName(const char *lib_name, const char *sym_name,
                      void *new_func, void **old_func,
                      GlossHookCallback call_back_func)
{
    (void)lib_name;
    (void)sym_name;
    (void)new_func;
    (void)old_func;
    (void)call_back_func;
    return NULL;
}

void GlossHookDelete(GHook hook)
{
    (void)hook;
}

#endif /* RUBY_FOSS_COMPLIANT */
