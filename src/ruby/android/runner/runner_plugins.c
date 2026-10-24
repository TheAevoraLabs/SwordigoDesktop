#include "runner_plugins.h"
#include <dlfcn.h>
#include <jni.h>
#include <android/log.h>
#include <string.h>

#define LOG_TAG "RubyPlugins"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM *g_plugin_vm = NULL;
static void *g_active_plugin_handle = NULL;
static char g_active_plugin_path[1024] = {0};

/* Symbols exported for plugins requiring the Launcher runtime JNI environment */
__attribute__((visibility("default")))
jclass g_btn_cls = NULL;

__attribute__((visibility("default")))
JavaVM *java_get_vm(void) {
    return g_plugin_vm;
}

__attribute__((visibility("default")))
JNIEnv *java_get_env(int *out_attached) {
    if (out_attached) *out_attached = 0;
    if (!g_plugin_vm) {
        LOGE("java_get_env: JavaVM not initialized");
        return NULL;
    }

    JNIEnv *env = NULL;
    int status = (*g_plugin_vm)->GetEnv(g_plugin_vm, (void **)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*g_plugin_vm)->AttachCurrentThread(g_plugin_vm, &env, NULL) != 0) {
            LOGE("java_get_env: AttachCurrentThread failed");
            return NULL;
        }
        if (out_attached) *out_attached = 1;
    } else if (status != JNI_OK) {
        LOGE("java_get_env: GetEnv failed (%d)", status);
        return NULL;
    }
    return env;
}

__attribute__((visibility("default")))
void java_release_env(int attached) {
    if (attached && g_plugin_vm) {
        (*g_plugin_vm)->DetachCurrentThread(g_plugin_vm);
    }
}

void runner_plugins_set_jvm(JavaVM *vm) {
    g_plugin_vm = vm;
    LOGI("runner_plugins_set_jvm: cached JavaVM (%p)", vm);
}

int runner_load_plugin(const char *plugin_path) {
    if (!plugin_path || !plugin_path[0]) {
        LOGE("runner_load_plugin: empty plugin path");
        return -1;
    }

    if (g_active_plugin_handle) {
        LOGI("runner_load_plugin: active plugin present, unloading first");
        runner_unload_plugin();
    }

    /* Guarantee that core libraries are opened with RTLD_GLOBAL so mod dlopen/dlsym resolves them */
    void *gloss_h = dlopen("libGlossHook.so", RTLD_NOW | RTLD_GLOBAL);
    if (!gloss_h) {
        LOGW("runner_load_plugin: libGlossHook.so not opened globally: %s", dlerror());
    }

    void *sw_h = dlopen("libswordigo.so", RTLD_NOW | RTLD_GLOBAL);
    if (!sw_h) {
        LOGW("runner_load_plugin: libswordigo.so not opened globally: %s", dlerror());
    }

    /* Cache g_btn_cls for ButtonController/Overlay mods */
    if (g_plugin_vm) {
        int attached = 0;
        JNIEnv *env = java_get_env(&attached);
        if (env) {
            jclass local = (*env)->FindClass(env, "net/kiwi/overlay/Controller");
            if (local) {
                if (g_btn_cls) {
                    (*env)->DeleteGlobalRef(env, g_btn_cls);
                }
                g_btn_cls = (jclass)(*env)->NewGlobalRef(env, local);
                (*env)->DeleteLocalRef(env, local);
                LOGI("runner_load_plugin: cached g_btn_cls (%p)", g_btn_cls);
            } else {
                LOGW("runner_load_plugin: net/kiwi/overlay/Controller class not found");
                (*env)->ExceptionClear(env);
            }
            java_release_env(attached);
        }
    }

    LOGI("runner_load_plugin: dlopening '%s'...", plugin_path);
    void *h = dlopen(plugin_path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        LOGE("runner_load_plugin: dlopen failed for %s: %s", plugin_path, dlerror());
        return -2;
    }

    g_active_plugin_handle = h;
    strncpy(g_active_plugin_path, plugin_path, sizeof(g_active_plugin_path) - 1);
    g_active_plugin_path[sizeof(g_active_plugin_path) - 1] = '\0';

    typedef void (*mod_init_fn)(void);
    mod_init_fn init = (mod_init_fn)dlsym(h, "mod_init");
    if (init) {
        LOGI("runner_load_plugin: invoking mod_init() for '%s'", plugin_path);
        init();
        LOGI("runner_load_plugin: mod_init() finished successfully");
    } else {
        LOGW("runner_load_plugin: no mod_init() exported in '%s'", plugin_path);
    }

    return 0;
}

void runner_unload_plugin(void) {
    if (!g_active_plugin_handle) return;

    LOGI("runner_unload_plugin: unloading active plugin '%s'", g_active_plugin_path);

    typedef void (*unload_mod_fn)(void);
    unload_mod_fn unload = (unload_mod_fn)dlsym(g_active_plugin_handle, "unload_mod");
    if (unload) {
        LOGI("runner_unload_plugin: invoking unload_mod()...");
        unload();
        LOGI("runner_unload_plugin: unload_mod() finished");
    }

    dlclose(g_active_plugin_handle);
    g_active_plugin_handle = NULL;
    g_active_plugin_path[0] = '\0';

    if (g_btn_cls && g_plugin_vm) {
        int attached = 0;
        JNIEnv *env = java_get_env(&attached);
        if (env) {
            (*env)->DeleteGlobalRef(env, g_btn_cls);
            g_btn_cls = NULL;
            java_release_env(attached);
        }
    }
}
