#ifndef RUNNER_PLUGINS_H
#define RUNNER_PLUGINS_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

void runner_plugins_set_jvm(JavaVM *vm);
int runner_load_plugin(const char *plugin_path);
void runner_unload_plugin(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNNER_PLUGINS_H */
