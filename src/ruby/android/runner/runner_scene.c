/*
 * runner_scene.c — direct scene boot for Ruby Touch.
 *
 * Boots the vanilla engine straight into a chosen .scene by swapping the boot-scene
 * std::string on CaverShell as CaverShell::InitView() reads it.
 *
 * Evidence (libswordigo.so 1.4.13, both ABIs; see
 * docs/game/caver/scene/sceneloading/04_direct_scene_boot_feasibility_ruby_mobile.md):
 *
 *   Java_com_touchfoo_swordigo_Native_setupApplication
 *       allocates CaverShell and writes "menu" into the boot-scene field
 *       (arm64 0x507DF4, arm32 0x37D378)
 *   CaverShell::InitView
 *       reads it, appends ".scene", and if the result's last path component is not
 *       "menu.scene" builds a synthetic profile and boots GameViewController
 *       (arm64 0x321DB4, arm32 0x28B500 — both exported in .dynsym)
 *   Java_..._setApplicationViewSize
 *       calls InitView exactly once, on its first invocation (arm64 0x508764)
 *
 * Only the field offset differs per ABI, and only the field is written.
 */

#include "runner_scene.h"
#include "stdstring.h"
#include "Gloss.h"

#include <stdio.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "RubyRunnerScene"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define HOOK_LIB_NAME "libswordigo.so"
#define SYM_INITVIEW "_ZN5Caver10CaverShell8InitViewEv"

/* CaverShell boot-scene std::string. Verified three ways per ABI: CaverShell's
 * constructor zero-stores, InitView's reads, ~CaverShell's destroy. */
#if defined(__aarch64__)
#  define CAVERSHELL_BOOT_SCENE_OFF 104u
#else
#  define CAVERSHELL_BOOT_SCENE_OFF 72u
#endif

/* Sanity bound for the read-back check: a scene name is never this long. */
#define SCENE_NAME_MAX 64

static char g_boot_scene[SCENE_NAME_MAX + 1];
static int g_boot_scene_consumed = 0;
static GHook g_hook_initview = NULL;
static void (*orig_InitView)(void *) = NULL;

void runner_set_boot_scene(const char *name) {
    g_boot_scene_consumed = 0;
    if (name && name[0]) {
        snprintf(g_boot_scene, sizeof(g_boot_scene), "%s", name);
        LOGI("Scene boot armed: '%s'", g_boot_scene);
    } else {
        g_boot_scene[0] = '\0';
        LOGI("Scene boot cleared");
    }
}

bool runner_boot_scene_pending(void) {
    return g_boot_scene[0] != '\0' && !g_boot_scene_consumed;
}

bool runner_boot_scene_supported(void) {
    return g_hook_initview != NULL;
}

/* Reject the field if it does not look like the engine's own short boot-scene string.
 * A wrong offset or a future layout change must leave the engine untouched rather than
 * scribble over an unrelated member. */
static int field_looks_like_scene_name(const String *f) {
    size_t len = String_size(f);
    if (len == 0 || len > SCENE_NAME_MAX) return 0;
    const char *p = String_get(f);
    if (!p) return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c > 0x7E) return 0;
    }
    return 1;
}

static void hook_InitView(void *self) {
    if (self && runner_boot_scene_pending()) {
        String *field = (String *)((char *)self + CAVERSHELL_BOOT_SCENE_OFF);
        if (field_looks_like_scene_name(field)) {
            char was[SCENE_NAME_MAX + 1];
            size_t n = String_size(field);
            if (n > SCENE_NAME_MAX) n = SCENE_NAME_MAX;
            memcpy(was, String_get(field), n);
            was[n] = '\0';

            /* libc++ std::string: release a heap buffer before re-seating, or the
             * engine's own destructor later frees at best a leak and at worst a stale
             * pointer. String_destroy is a no-op for short strings. */
            String_destroy(field);
            String_create(field, g_boot_scene);
            g_boot_scene_consumed = 1;

            LOGI("Direct scene boot: CaverShell+%u '%s' -> '%s' (engine will load '%s.scene')",
                 (unsigned)CAVERSHELL_BOOT_SCENE_OFF, was, g_boot_scene, g_boot_scene);
        } else {
            LOGE("Direct scene boot: CaverShell+%u does not hold a scene name; refusing to "
                 "write (wrong build, or InitView already ran). Vanilla boot continues.",
                 (unsigned)CAVERSHELL_BOOT_SCENE_OFF);
            g_boot_scene_consumed = 1;
        }
    }

    if (orig_InitView) orig_InitView(self);
}

void runner_install_scene_hook(void) {
    if (g_hook_initview) return;

    g_hook_initview = GlossHookByName(HOOK_LIB_NAME, SYM_INITVIEW,
                                      (void *)hook_InitView, (void **)&orig_InitView, NULL);
    if (g_hook_initview) {
        LOGI("CaverShell::InitView hook installed (boot scene field at +%u)",
             (unsigned)CAVERSHELL_BOOT_SCENE_OFF);
    } else {
        LOGE("CaverShell::InitView hook FAILED — direct scene boot unavailable");
    }
}

void runner_uninstall_scene_hook(void) {
    if (!g_hook_initview) return;
    GlossHookDelete(g_hook_initview);
    g_hook_initview = NULL;
    orig_InitView = NULL;
    LOGI("CaverShell::InitView hook removed");
}
