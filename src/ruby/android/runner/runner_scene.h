#ifndef RUBY_RUNNER_SCENE_H
#define RUBY_RUNNER_SCENE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Direct scene boot ("touchfoo test player").
 *
 * libswordigo carries a first-party test harness: CaverShell holds a boot-scene
 * std::string that Java_..._setupApplication initialises to "menu". When
 * CaverShell::InitView runs (first setApplicationViewSize call) and that string is
 * anything other than "menu", the engine skips MainMenuViewController, synthesises a
 * "testplayer"/"player" PlayerProfile whose GameState current-level is that string, and
 * boots GameViewController straight into "<name>.scene".
 *
 * We hook CaverShell::InitView and swap the field on the way in, which is the only window
 * in which the value is read. Nothing else is patched.
 *
 * The name must be the scene BASENAME without the ".scene" extension: the engine appends
 * the extension itself. It must also resolve through the runner's VFS root, i.e. live
 * under the resource directory handed to GameActivity.
 */

/* Arm a direct boot. `name` = scene name without extension. NULL/empty cancels. */
void runner_set_boot_scene(const char *name);

/* True once a scene has been armed and not yet consumed by InitView. */
bool runner_boot_scene_pending(void);

/* True when the CaverShell::InitView hook is installed, i.e. arming a scene will work. */
bool runner_boot_scene_supported(void);

/* Install the InitView hook. Called from runner_install_hooks(). */
void runner_install_scene_hook(void);

/* Drop the hook. Called from runner_uninstall_hooks(). */
void runner_uninstall_scene_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* RUBY_RUNNER_SCENE_H */
