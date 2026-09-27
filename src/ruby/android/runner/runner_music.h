#pragma once

#include <jni.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void runner_init_jvm(JavaVM *vm);
void runner_install_music_hooks(void);
void runner_uninstall_music_hooks(void);

#ifdef __cplusplus
}
#endif
