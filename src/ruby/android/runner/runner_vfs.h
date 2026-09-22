#ifndef RUBY_RUNNER_VFS_H
#define RUBY_RUNNER_VFS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void runner_set_resource_dir(const char *path);
const char *runner_get_resource_dir(void);
void runner_install_hooks(void);
void runner_uninstall_hooks(void);

#ifdef __cplusplus
}
#endif

#endif // RUBY_RUNNER_VFS_H
