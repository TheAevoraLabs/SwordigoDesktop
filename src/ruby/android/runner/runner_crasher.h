#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void runner_init_crasher(void);
void runner_set_crash_log_path(const char *path);

#ifdef __cplusplus
}
#endif
