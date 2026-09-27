#include "runner_music.h"
#include "stdstring.h"
#include "Gloss.h"

#include <stdio.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "RubyRunnerMusic"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define HOOK_LIB_NAME "libswordigo.so"

static JavaVM *g_vm = NULL;
static jclass g_portClass = NULL;
static jmethodID g_loadFile = NULL;
static jmethodID g_play = NULL;
static jmethodID g_pause = NULL;
static jmethodID g_stop = NULL;
static jmethodID g_setLooping = NULL;
static jmethodID g_setVolume = NULL;

static GHook g_hook_music_load = NULL;
static GHook g_hook_music_play = NULL;
static GHook g_hook_music_pause = NULL;
static GHook g_hook_music_stop = NULL;
static GHook g_hook_music_loop = NULL;
static GHook g_hook_music_vol = NULL;
static GHook g_hook_gms = NULL;

void runner_init_jvm(JavaVM *vm) {
    g_vm = vm;
    LOGI("runner_init_jvm: JavaVM cached (%p)", vm);
}

static JNIEnv *get_env(int *out_attached) {
    if (out_attached) *out_attached = 0;
    if (!g_vm) return NULL;

    JNIEnv *env = NULL;
    int status = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0) {
            LOGE("AttachCurrentThread failed");
            return NULL;
        }
        if (out_attached) *out_attached = 1;
    } else if (status != JNI_OK) {
        LOGE("GetEnv failed (%d)", status);
        return NULL;
    }
    return env;
}

static void release_env(int attached) {
    if (attached && g_vm) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}

static int ensure_port_ready(JNIEnv *env) {
    if (g_portClass) return 1;

    jclass local = (*env)->FindClass(env, "in/aevora/ruby/Port");
    if (!local) {
        LOGE("Could not find class in/aevora/ruby/Port");
        (*env)->ExceptionClear(env);
        return 0;
    }

    g_portClass = (jclass)(*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);

    g_loadFile   = (*env)->GetStaticMethodID(env, g_portClass, "loadFile", "(Ljava/lang/String;)Z");
    g_play       = (*env)->GetStaticMethodID(env, g_portClass, "play", "()V");
    g_pause      = (*env)->GetStaticMethodID(env, g_portClass, "pause", "()V");
    g_stop       = (*env)->GetStaticMethodID(env, g_portClass, "stop", "()V");
    g_setLooping = (*env)->GetStaticMethodID(env, g_portClass, "setLooping", "(Z)V");
    g_setVolume  = (*env)->GetStaticMethodID(env, g_portClass, "setVolume", "(F)V");

    if (!g_loadFile || !g_play || !g_pause || !g_stop || !g_setLooping || !g_setVolume) {
        LOGE("Failed finding one or more Port static methods");
        (*env)->ExceptionClear(env);
        return 0;
    }

    LOGI("Port class and methods resolved successfully");
    return 1;
}

static bool call_load_file(const char *track) {
    int attached = 0;
    JNIEnv *env = get_env(&attached);
    if (!env || !ensure_port_ready(env)) {
        release_env(attached);
        return false;
    }

    jstring jtrack = (*env)->NewStringUTF(env, track ? track : "");
    jboolean res = (*env)->CallStaticBooleanMethod(env, g_portClass, g_loadFile, jtrack);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (jtrack) (*env)->DeleteLocalRef(env, jtrack);

    release_env(attached);
    return res != JNI_FALSE;
}

static void call_void(jmethodID method) {
    int attached = 0;
    JNIEnv *env = get_env(&attached);
    if (!env || !ensure_port_ready(env)) {
        release_env(attached);
        return;
    }

    (*env)->CallStaticVoidMethod(env, g_portClass, method);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    release_env(attached);
}

static void call_set_looping(bool loop) {
    int attached = 0;
    JNIEnv *env = get_env(&attached);
    if (!env || !ensure_port_ready(env)) {
        release_env(attached);
        return;
    }

    (*env)->CallStaticVoidMethod(env, g_portClass, g_setLooping, (jboolean)loop);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    release_env(attached);
}

static void call_set_volume(float vol) {
    int attached = 0;
    JNIEnv *env = get_env(&attached);
    if (!env || !ensure_port_ready(env)) {
        release_env(attached);
        return;
    }

    (*env)->CallStaticVoidMethod(env, g_portClass, g_setVolume, (jfloat)vol);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    release_env(attached);
}

// ── MusicPlayerJNI Hooks ────────────────────────────────────────────────────
static bool hook_MusicLoadFile(String *s) {
    const char *track = s ? String_get(s) : "";
    LOGD("MusicPlayerJNI::LoadFile('%s')", track ? track : "");
    return call_load_file(track);
}

static void hook_MusicPlay(void) {
    LOGD("MusicPlayerJNI::Play()");
    call_void(g_play);
}

static void hook_MusicPause(void) {
    LOGD("MusicPlayerJNI::Pause()");
    call_void(g_pause);
}

static void hook_MusicStop(void) {
    LOGD("MusicPlayerJNI::Stop()");
    call_void(g_stop);
}

static void hook_MusicSetLooping(bool loop) {
    LOGD("MusicPlayerJNI::SetLooping(%d)", (int)loop);
    call_set_looping(loop);
}

static void hook_MusicSetVolume(float vol) {
    LOGD("MusicPlayerJNI::SetVolume(%f)", vol);
    call_set_volume(vol);
}

// ── Google Game Services Stub ───────────────────────────────────────────────
static bool hook_AndroidIsGoogleGameServicesAvailable(void) {
    return false;
}

void runner_install_music_hooks(void) {
    g_hook_music_load = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN14MusicPlayerJNI8LoadFileERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE",
        (void *)hook_MusicLoadFile,
        NULL,
        NULL
    );
    LOGI("Hook MusicPlayerJNI::LoadFile: %p", g_hook_music_load);

    g_hook_music_play = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN14MusicPlayerJNI4PlayEv",
        (void *)hook_MusicPlay,
        NULL,
        NULL
    );
    LOGI("Hook MusicPlayerJNI::Play: %p", g_hook_music_play);

    g_hook_music_pause = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN14MusicPlayerJNI5PauseEv",
        (void *)hook_MusicPause,
        NULL,
        NULL
    );
    LOGI("Hook MusicPlayerJNI::Pause: %p", g_hook_music_pause);

    g_hook_music_stop = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN14MusicPlayerJNI4StopEv",
        (void *)hook_MusicStop,
        NULL,
        NULL
    );
    LOGI("Hook MusicPlayerJNI::Stop: %p", g_hook_music_stop);

    g_hook_music_loop = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN14MusicPlayerJNI10SetLoopingEb",
        (void *)hook_MusicSetLooping,
        NULL,
        NULL
    );
    LOGI("Hook MusicPlayerJNI::SetLooping: %p", g_hook_music_loop);

    g_hook_music_vol = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN14MusicPlayerJNI9SetVolumeEf",
        (void *)hook_MusicSetVolume,
        NULL,
        NULL
    );
    LOGI("Hook MusicPlayerJNI::SetVolume: %p", g_hook_music_vol);

    g_hook_gms = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver36AndroidIsGoogleGameServicesAvailableEv",
        (void *)hook_AndroidIsGoogleGameServicesAvailable,
        NULL,
        NULL
    );
    LOGI("Hook Caver::AndroidIsGoogleGameServicesAvailable: %p", g_hook_gms);
}

void runner_uninstall_music_hooks(void) {
    if (g_hook_music_load) { GlossHookDelete(g_hook_music_load); g_hook_music_load = NULL; }
    if (g_hook_music_play) { GlossHookDelete(g_hook_music_play); g_hook_music_play = NULL; }
    if (g_hook_music_pause) { GlossHookDelete(g_hook_music_pause); g_hook_music_pause = NULL; }
    if (g_hook_music_stop) { GlossHookDelete(g_hook_music_stop); g_hook_music_stop = NULL; }
    if (g_hook_music_loop) { GlossHookDelete(g_hook_music_loop); g_hook_music_loop = NULL; }
    if (g_hook_music_vol) { GlossHookDelete(g_hook_music_vol); g_hook_music_vol = NULL; }
    if (g_hook_gms) { GlossHookDelete(g_hook_gms); g_hook_gms = NULL; }
}
