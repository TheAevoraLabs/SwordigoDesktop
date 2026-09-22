#include "runner_vfs.h"
#include "runner_scene.h"
#include <jni.h>
#include <android/log.h>

#define LOG_TAG "RubyRunnerJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

JNIEXPORT void JNICALL
Java_in_aevora_ruby_GameActivity_setResourceDirectory(JNIEnv *env, jclass clazz, jstring dirPath) {
    (void)clazz;
    if (dirPath) {
        const char *path = (*env)->GetStringUTFChars(env, dirPath, NULL);
        if (path) {
            runner_set_resource_dir(path);
            (*env)->ReleaseStringUTFChars(env, dirPath, path);
        }
    } else {
        runner_set_resource_dir(NULL);
    }
}

JNIEXPORT void JNICALL
Java_in_aevora_ruby_GameActivity_loadHooks(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    LOGI("Java requested loadHooks");
    runner_install_hooks();
}

JNIEXPORT void JNICALL
Java_in_aevora_ruby_GameActivity_unloadHooks(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    LOGI("Java requested unloadHooks");
    runner_uninstall_hooks();
}

/* Arm a direct boot into a specific scene. Call after loadHooks() and before the GL
 * surface reports its first size to the engine. The scene name is the basename without
 * the ".scene" extension — the engine appends it. */
JNIEXPORT void JNICALL
Java_in_aevora_ruby_GameActivity_setBootScene(JNIEnv *env, jclass clazz, jstring sceneName) {
    (void)clazz;
    if (!sceneName) {
        runner_set_boot_scene(NULL);
        return;
    }
    const char *name = (*env)->GetStringUTFChars(env, sceneName, NULL);
    if (name) {
        runner_set_boot_scene(name);
        (*env)->ReleaseStringUTFChars(env, sceneName, name);
    }
}

/* Reports whether the engine hook that makes direct scene boot possible is in place. */
JNIEXPORT jboolean JNICALL
Java_in_aevora_ruby_GameActivity_isBootSceneReady(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    return runner_boot_scene_supported();
}
