# Direct Scene Boot — Feasibility Report for Ruby Touch (Android, via libGlossHook)

**Status:** mechanism fully reverse-engineered and verified against both shipped ABIs
(`libswordigo.so` v1.4.13 `arm64-v8a` + `armeabi-v7a`, matching `OpenSwordigo/arm64_13`
and `OpenSwordigo/arm32_13`). Nothing here has been run on a device yet — §8 lists exactly
what still needs a device to confirm.

**Goal:** in Ruby Touch (the Android Qt front-end), pick a `.scene` in the file manager and
hit **Start scene in game** — the vanilla engine boots straight into that scene with a
throwaway test profile, bypassing main menu, profile select and scene picker.

**Verdict:** feasible, and *cheap*. The engine already contains the feature — Touchfoo's
internal test harness — and it is gated on a single `std::string` member of `CaverShell`.
No code patching, no function-call emulation, no SRE required. One `GlossHookByName` on an
exported symbol plus a string write is the whole implementation.

---

## 1. What Swordfare actually does today

Swordfare's flag is real but it is *not* an engine flag. `src/main.cpp`:

| Location | What happens |
| :--- | :--- |
| `src/main.cpp:8871-8878` | Parses `--scene <name>` / `--scene=<name>` into `g_boot_scene` |
| `src/main.cpp:9015-9020` | `--scene` also suppresses the launcher GUI |
| `src/main.cpp:6035-6062` | The actual mechanism |

The injection block runs **inside the `if (setupApp)` branch, immediately after
`Java_com_touchfoo_swordigo_Native_setupApplication` returns and before
`setApplicationViewSize` is called**:

```c
if (!g_boot_scene.empty()) {
    std::string sc = g_boot_scene;
    if (sc.size() > 6 && sc.substr(sc.size() - 6) == ".scene")
        sc = sc.substr(0, sc.size() - 6);                     // strip extension
    uint64_t caver_shell_offset = (swordi_abi == 13) ? 0x651700ULL : 0x6E9C20ULL;
    uint64_t shell_ptr = *(uint64_t*)(g_guest_memory + g_main_mod_64.base_addr + caver_shell_offset);
    if (shell_ptr >= 0x20000000ULL && shell_ptr < 0xE0000000ULL) {
        char* str_slot = (char*)(g_guest_memory + shell_ptr + 104);   // CaverShell + 104
        size_t len = sc.length();
        if (len <= 22) {                                       // libc++ SSO capacity
            memset(str_slot, 0, 24);
            str_slot[0] = (char)(len << 1);                    // libc++ short: size<<1
            memcpy(str_slot + 1, sc.c_str(), len);
            str_slot[1 + len] = '\0';
        }
    }
}
```

So the port does three things: read the global `boost::shared_ptr<CaverShell>` slot, then
write a libc++ short-string into `CaverShell+104`, then let the normal boot continue.

That ordering is not incidental — it is the *only* window in which the write is read (§3.4).

---

## 2. The engine-side feature: `CaverShell::InitView` and the `"menu"` sentinel

Touchfoo shipped a first-party test harness. Every build (desktop, Android) contains it.

### 2.1 The sentinel

`Java_com_touchfoo_swordigo_Native_setupApplication` — the JNI entry that Android's
`GameRenderer.onSurfaceCreated` calls — creates the shell and then, unconditionally,
writes the string `"menu"` into `CaverShell+104`:

```c
// arm64 0x507DF4, tail of setupApplication
v30 = operator new(0xA8u);                       // sizeof(CaverShell) == 0xA8
Caver::CaverShell::CaverShell(v30);
boost::shared_ptr<Caver::CaverShell>::reset<Caver::CaverShell>(&qword_651700, v30);
(*(vptr + 48))(shell);                           // slot 6: CaverShell::InitApplication()
LOBYTE(v33) = 8;                                 // libc++ short string, len 4
*(_DWORD*)((char*)&v33 + 1) = 1970169197;        // 0x756E656D -> "menu"
std::string::operator=(qword_651700 + 104, &v33);   // <-- the sentinel
```

arm32 (`0x37D378`) is identical with `dword_460E68 + 72` and the literal `"menu"`.

`"menu"` is the value that means *"just boot the main menu"*. Any other value means
*"boot into me"*.

### 2.2 The reader

`CaverShell::InitView()` — **arm64 `0x321DB4` / arm32 `0x28B500`**, both exported in
`.dynsym`:

```c
// arm64, simplified
v16 = *(uint8_t*)(this + 104);                    // libc++ short-string size byte
v18 = (v16 & 1) ? *(uint64_t*)(this + 112) : (v16 >> 1);    // string length

if (v18) {                                        // ---- member set: build "<name>.scene"
    scenePath = <member> + ".scene";              // extension appended unconditionally
} else if ((argvEnd - argvBegin) / 24 >= 1) {     // ---- no member: last argv is the scene
    scenePath = argv[argc - 1];
} else {                                          // ---- nothing: interactive file picker
    GUIFileDialog with title "Scene files", filter "*.scene", default "plains1";
}
```

then:

```c
LastPathComponent(&scenePath) -> lastPart;
bool isMenu = (lastPart == "menu.scene");          // arm64 0x321DB4+0x8C4 region
if (!isMenu) {
    PlayerProfile* prof = new PlayerProfile();
    PlayerProfile::InitWithIdentifier(prof, "testplayer");   // <-- the synthetic player
    PlayerProfile::CreateProfile(prof, "player");
    *(uint8_t*)(prof + 280) = 1;                   // "test profile" flag (Proto field 152)
    GameState* gs = *(GameState**)(prof + 240);
    std::string::operator=(gs + 248, <scene basename without ".scene">);  // "current level"
    ProfileManager::AddProfile(prof);
    GameViewController* gvc = new GameViewController();
    *(GVC + 136) = prof;                           // GVC holds the synthetic profile
    GUINavigationController::SetCurrentViewController(navController, gvc, 1, 0.4f, 0.4f);
} else {
    MainMenuViewController* mm = new MainMenuViewController();
    GUINavigationController::SetCurrentViewController(navController, mm, ...);
}
```

(arm32 `0x28B500` contains the same two literals — `"testplayer"` at `0x28B978`,
`"player"` at `0x28B992`, the `"menu.scene"` compare at `0x28B674`, the same
`new(0x120)`-equivalent profile and the same flag store at `prof+160`.)

### 2.3 "Test player" — what it really is

Not a spawned dummy entity. It is a **synthetic `PlayerProfile`**:

* identifier `"testplayer"`, display name `"player"`;
* created through the normal `PlayerProfile::CreateProfile` path, so it has a real
  `Proto::GameState` (`CreateProfile` → `LoadGameStateFromProtobufMessage`);
* its `GameState`'s current-level string is set to the scene's basename, which is what the
  standard boot path consumes;
* its "test" bool at `PlayerProfile+280` is `1` — the ctor zeroes it, and
  `SaveToProtobufMessage` writes it to `Proto::PlayerProfile` field 152 when set. It is a
  persisted marker, so a direct-boot session leaves a `testplayer` profile on disk.

### 2.4 Who actually loads the scene

`InitView` never touches `Scene::LoadFromFile`. It only installs a
`GameViewController` whose `GameState` already names the level. The load happens in the
ordinary path:

```
setApplicationViewSize (first call)
  └─ vptr+80  = CaverShell::InitView()
       └─ SetCurrentViewController(GameViewController)
            └─ GameViewController::LoadView()            [0x42D570]
                 └─ GameViewController::BackgroundLoad()  [0x42D998]
                      ├─ GameState::StateForLevelWithName(gs, *(gs+248), create_if_missing=1)
                      ├─ PathForResourceOfType(<level>, "scene")
                      ├─ Scene::LoadFromFile(path)        [0x4640A0-class]
                      ├─ GameViewController::RegisterSceneLibrary(scene)
                      ├─ GameSceneController::InitWithScene(scene)
                      ├─ GameSceneController::SpawnHeroAt(*(gs+272))
                      └─ GameSceneView::InitWithSceneController(...)
```

and the spawn name `*(GameState+272)` is empty for a fresh profile, which is fine — see
§2.5. `ViewDidAppear` (`0x42F650`) then shows the level-title notification and starts the
map node's music, which is why a custom scene can boot correctly yet show no title/music.

### 2.5 `SpawnHeroAt` falls back to `spawn_default`

`GameSceneController::SpawnHeroAt` (`0x425890`) is the reason `"testplayer"` works in *any*
scene:

```c
Scene::ObjectWithIdentifier(&o, scene, spawnName);
if (spawnName.empty() || o == nullptr) {
    Scene::ObjectWithIdentifier(&o, scene, "spawn_default");   // 26-char literal, inlined
}
```

Empty or unresolvable spawn → `spawn_default`. So a scene only needs a `spawn_default`
object to place the hero; otherwise the hero lands on whatever the residual logic gives.

### 2.6 The vtable (arm64 `_ZTVN5Caver10CaverShellE` = `0x6189E8`, arm32 = `0x43E2C4`)

Recovered from `.rela.dyn`, not guessed — vptr = `_ZTV + 16` (arm64) / `_ZTV + 8` (arm32):

| vptr + | arm64 | arm32 | Symbol |
| ---: | ---: | ---: | :--- |
| 0 | `_ZTV+16` | `_ZTV+8` | `TouchBegan(FWTouch const&)` |
| 8 | `_ZTV+24` | `_ZTV+16` | `TouchMoved` |
| 16 | `_ZTV+32` | `_ZTV+24` | `TouchEnded` |
| 24 | `_ZTV+40` | `_ZTV+32` | `TouchCancelled` |
| 32 | `_ZTV+48` | — | `~CaverShell()` (D2) |
| 40 | `_ZTV+56` | — | `~CaverShell()` (D0) |
| 48 | `_ZTV+64` | — | `InitApplication()` |
| 56 | `_ZTV+72` | — | `QuitApplication()` |
| 64 | `_ZTV+80` | — | `SuspendApplication(bool)` |
| 72 | `_ZTV+88` | — | `ResumeApplication()` |
| **80** | `_ZTV+96` | — | **`InitView()`** |
| 88 | `_ZTV+104` | — | `ReleaseView()` |
| 96 | `_ZTV+112` | — | `FWShell::ApplicationShouldQuit()` |
| 104 | `_ZTV+120` | — | `Update(float)` |
| 112 | `_ZTV+128` | — | `Render(RenderingContext*)` |

Call sites found in the shipped binary:

| Host function | Call |
| :--- | :--- |
| `Native.setupApplication` | vptr+48 `InitApplication()` |
| `Native.setApplicationViewSize` (`0x508764`) | vptr+64 `SuspendApplication(1)`, **vptr+80 `InitView()`**, vptr+104 `Update(0.0f)` |
| `Native.applicationDidBecomeActive` (`0x508C54`) | vptr+72 `ResumeApplication()` |
| `Native.applicationDidBecomeInactive` | vptr+64 `SuspendApplication(1)` |
| `Native.drawApplication` (`0x508A68`) | vptr+112 `Render(...)` |
| `Native.handleApplicationQuit` (`0x508614`) | vptr+88 `ReleaseView()` |

### 2.7 The real entry point, and where the argv path is live

`Caver::NewCaverShell(int argc, char** argv)` — arm64 `0x3219F0` / arm32 `0x28B234` — is
the desktop/iOS entry. It allocates the shell, then for `i = 1..argc-1` pushes each argv
into the shell's argument vector (`FWShell::AddArgument` on arm32). It never sets `+104`.
So on desktop, "last argv is the scene" is a live path: `swordigo mylevel.scene` boots
straight in. Android's JNI path never calls `NewCaverShell` and never populates argv, which
is why the `+104` route is the correct one for both Swordfare and Ruby Touch.

---

## 3. `CaverShell` layout (verified, both ABIs)

Cross-checked three ways per ABI (`CaverShell` ctor's stores, `InitView`'s reads,
`~CaverShell`'s destroy):

| Field | arm64 offset | arm32 offset | sizeof |
| :--- | ---: | ---: | ---: |
| **boot scene `std::string`** | **+104** | **+72** | 24 / 12 |
| `boost::shared_ptr<GUIWindow>` | +128 | +84 | 16 / 8 |
| `boost::shared_ptr<GUINavigationController>` | +144 | +92 | 16 / 8 |
| `bool suspended` | +160 | +100 | 1 |
| `sizeof(CaverShell)` | **0xA8 = 168** | **0x68 = 104** | matching `operator new` |

`FWShellPreferences` base sits at `+32` (arm64) / `+16` (arm32).

Global slots (the `boost::shared_ptr` statics registered by the library's own
`__cxa_atexit` initialiser `sub_50B4BC`):

| Global | arm64 | arm32 |
| :--- | ---: | ---: |
| `shared_ptr<RenderingContext>` | `0x6516F0` | `0x460E60` |
| **`shared_ptr<CaverShell>`** | **`0x651700`** | **`0x460E68`** |
| `shared_ptr<OnlineController_Android>` | `0x651710` | `0x460E70` |
| `shared_ptr<ProfileManager_Android>` | `0x651720` | `0x460E78` |
| `shared_ptr<StoreController_Android>` | `0x651730` | `0x460E80` |
| `handleApplicationLaunch` flag | `0x651758` | — |
| `InitView` already ran flag | `0x651759` | — |

Note the shell slot is a **local** static: it does not appear in `.dynsym`, so it can only
be reached as `GlossGetLibBias("libswordigo.so") + 0x651700`. (Swordfare hardcodes it for
the same reason.)

### 3.1 The exact window

```
setupApplication()            -> shell allocated, +104 := "menu"
                                  <<< WRITE HERE is legal
setApplicationViewSize()      -> first call: InitView() reads +104, engine enters
                                  the !isMenu branch, then Update(0.0f)
applicationDidBecomeActive()  -> ResumeApplication()
```

Writing after `InitView` has run is a no-op (the `0x651759` flag makes every later
`setApplicationViewSize` take the plain `UpdateViewSize` path). Writing before
`setupApplication` is impossible — the shell does not exist and the ctor would zero it.

---

## 4. Port to Ruby Touch

### 4.1 Where Ruby Touch stands today

The plumbing is already in place:

```
Ruby (QML file manager)
  RubyFileModel::launchGame()                        src/ruby/android/ruby_file_model.cpp:670
    -> RubyActivity.launchGameActivity(resDir)       RubyActivity.java:784
         -> GameActivity intent "resource_dir"       GameActivity.java:113
              System.loadLibrary("GlossHook")
              System.load(<extracted libopenal-soft.so>)
              System.load(<extracted libswordigo.so>)     <-- real game, native ARM
              System.loadLibrary("ruby_runner")
              GameActivity.setResourceDirectory(dir) / loadHooks()   -> runner_jni.c
              GameView + GameRenderer
GameRenderer.onSurfaceCreated -> Native.setupNativeInterface(); Native.setupApplication()
GameRenderer.onSurfaceChanged -> Native.setApplicationViewSize(w, h, ...)
```

`loadHooks()` → `runner_install_hooks()` (`runner_vfs.c`) already calls `GlossInit(true)`
and installs six `GlossHookByName` VFS hooks on the VFS symbols. A seventh hook joins them.
The VFS redirection is exactly what makes a **user-chosen on-disk `.scene`** resolvable:
`Caver::FileExistsAtPath` and `NewByteBufferFromAndroidAsset` are both redirected to
`g_resource_dir`.

### 4.2 Recommended design — hook `InitView`, not the global slot

Hooking `Caver::CaverShell::InitView` is strictly better than mirroring Swordfare's global
slot write:

| | InitView hook | Swordfare-style slot write |
| :--- | :--- | :--- |
| Version robustness | symbol-resolved from `.dynsym` | hardcoded `0x651700` / `0x6E9C20` |
| ABI robustness | symbol-resolved; no Thumb-bit guessing (arm32 `InitView` is at `0x28B501`, bit 0 set) | needs the shell offset per ABI |
| Receiver | `this` = `CaverShell*` handed to us | compute `*(bias+slot)` and validate the pointer |
| Ordering | guaranteed inside the first read | must be sequenced against `setApplicationViewSize` |
| Cost | one inline hook | none, but fragile |

With the hook, the flow becomes: *stash the requested scene in a global; on the first
`InitView` call, overwrite `this+OFF` and tail-call the original.* Everything else — the
synthetic profile, `spawn_default`, level state creation — is the engine's own code.

### 4.3 Implementation sketch

**`src/ruby/android/runner/runner_scene.h`** (new, small)

```c
#ifndef RUBY_RUNNER_SCENE_H
#define RUBY_RUNNER_SCENE_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Request a direct boot. `name` = scene name WITHOUT the ".scene" extension.
   Empty/NULL clears a previous request. Safe to call before or after loadHooks(). */
void runner_set_boot_scene(const char *name);
bool runner_boot_scene_pending(void);
void runner_install_scene_hook(void);   /* called from runner_install_hooks() */
#ifdef __cplusplus
}
#endif
#endif
```

**`src/ruby/android/runner/runner_scene.c`** (new)

```c
#include "runner_scene.h"
#include "runner_vfs.h"
#include "stdstring.h"
#include "Gloss.h"
#include <string.h>
#include <android/log.h>

#define LOG_TAG "RubyRunnerScene"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

#define HOOK_LIB_NAME "libswordigo.so"
#define SYM_INITVIEW  "_ZN5Caver10CaverShell8InitViewEv"

/* CaverShell::InitView() moves the boot-scene std::string per ABI. Verified against the
 * 1.4.13 arm64/arm32 builds (ctor zero-stores, InitView reads, ~CaverShell destroys). */
#if defined(__aarch64__)
#  define CAVERSHELL_BOOT_SCENE_OFF 104u
#else
#  define CAVERSHELL_BOOT_SCENE_OFF  72u
#endif

static char g_boot_scene[128] = {0};
static int  g_scene_consumed = 0;

void runner_set_boot_scene(const char *name) {
    g_scene_consumed = 0;
    if (name && name[0]) {
        snprintf(g_boot_scene, sizeof(g_boot_scene), "%s", name);
        LOGI("boot scene armed: '%s'", g_boot_scene);
    } else {
        g_boot_scene[0] = '\0';
    }
}

bool runner_boot_scene_pending(void) { return g_boot_scene[0] && !g_scene_consumed; }

typedef void (*InitView_fn)(void *self);
static InitView_fn orig_InitView = NULL;

static void hook_InitView(void *self) {
    if (self && runner_boot_scene_pending()) {
        String *field = (String *)((char *)self + CAVERSHELL_BOOT_SCENE_OFF);
        LOGI("InitView: replacing boot scene '%s' with '%s'",
             String_get(field), g_boot_scene);
        /* Safe read-back check: on a cold boot the engine has just written "menu" here.
         * If we do not see a plausible short string, leave the field alone rather than
         * corrupt an unrelated ABI. */
        if (String_size(field) < 32) {
            String_destroy(field);          /* frees the heap buffer if the old one was long */
            String_create(field, g_boot_scene);
            g_scene_consumed = 1;           /* engine only reads this on the first InitView */
        } else {
            LOGW("boot scene field did not look like a std::string; not touching it");
        }
    }
    if (orig_InitView) orig_InitView(self);
}

void runner_install_scene_hook(void) {
    GHook h = GlossHookByName(HOOK_LIB_NAME, SYM_INITVIEW,
                              (void *)hook_InitView, (void **)&orig_InitView, NULL);
    LOGI("InitView hook: %s", h ? "installed" : "FAILED");
}
```

Add one line to `runner_install_hooks()` in `runner_vfs.c`:

```c
    runner_install_scene_hook();
```

**`runner_jni.c`** — one more entry point, alongside the existing three:

```c
JNIEXPORT void JNICALL
Java_in_aevora_ruby_GameActivity_setBootScene(JNIEnv *env, jclass clazz, jstring sceneName) {
    (void)clazz;
    if (!sceneName) { runner_set_boot_scene(NULL); return; }
    const char *name = (*env)->GetStringUTFChars(env, sceneName, NULL);
    if (name) {
        runner_set_boot_scene(name);
        (*env)->ReleaseStringUTFChars(env, sceneName, name);
    }
}
```

**`GameActivity.java`** — declare it and accept the intent extra:

```java
public static native void setBootScene(String sceneName);

// in onCreate, after setResourceDirectory(resourceDir):
String bootScene = getIntent().getStringExtra("boot_scene");
if (bootScene != null && !bootScene.isEmpty()) setBootScene(bootScene);
```

**`RubyActivity.java`** — thread it through the existing launcher:

```java
public static void launchGameActivity(String resourceDir) { launchGameActivity(resourceDir, null); }

public static void launchGameActivity(String resourceDir, String bootScene) {
    ...
    intent.putExtra("resource_dir", resourceDir);
    if (bootScene != null) intent.putExtra("boot_scene", bootScene);
    ...
}
```

**`ruby_file_model.cpp`** — a `startSceneInGame()` next to `launchGame()`:

```cpp
void RubyFileModel::startSceneInGame(const QString& scenePath) {
    QFileInfo fi(scenePath);
    if (fi.suffix().compare("scene", Qt::CaseInsensitive) != 0) return;

    // The engine resolves assets through the runner's VFS root, so the scene must live
    // inside the resource tree that will be handed to GameActivity.
    QString resDir = fi.absolutePath();
    if (fi.absolutePath().endsWith("/resources") ||
        QFile::exists(fi.absolutePath() + "/resources/hiro.POD"))
        resDir = fi.absolutePath();

    const QString bare = fi.completeBaseName();   // no ".scene" — the engine appends it
    QJniObject jRes = QJniObject::fromString(resDir);
    QJniObject jScene = QJniObject::fromString(bare);
    QJniObject::callStaticMethod<void>(
        "in/aevora/ruby/RubyActivity", "launchGameActivity",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        jRes.object<jstring>(), jScene.object<jstring>());
}
```

Expose it via `Q_INVOKABLE` and add a **Start scene in game** action to the file-manager
context menu for `detectFileType() == "scene"` (that mapping already exists at
`ruby_file_model.cpp:692`).

### 4.4 ABI hazard you must fix while you are in there

`stdstring.h`'s `STRING_SHORT_CAP` is hardcoded to **23**, which is libc++'s SSO capacity
for **LP64 only**. libc++'s short buffer is `sizeof(__long) - 1` bytes: 23 on
`arm64-v8a`, **11 on `armeabi-v7a`**. The engine itself uses the corresponding threshold —
`NewCaverShell` splits at `len >= 0x17` on arm64 — and the arm32 build's short capacity is
correspondingly 10 characters.

Today `String_create` is only used for *reading* paths in the existing hooks, where the
long-string branch is what actually matters, so the bug is latent. The moment we write a
scene name with it, a name of 11-22 characters on arm32 (`"forest_part1"` is 12) writes
past the end of an inline buffer inside the engine's own `CaverShell`.

Fix:

```c
#if defined(__aarch64__)
#  define STRING_SHORT_CAP 23
#else
#  define STRING_SHORT_CAP 11
#endif
```

(`String_create` then sends anything longer down the `malloc` path, which is exactly what
libc++ would do. `String_destroy` must be called on the old value first so a previously
heap-allocated `"menu"`-length string — or a reused field — does not leak; it already
exists in `stdstring.h`.)

### 4.5 Name rules

* The engine **appends `".scene"` unconditionally** to the `+104` value, so pass the
  basename: `town_part1`, not `town_part1.scene`.
* The engine then does `LastPathComponent(scenePath) == "menu.scene"` to decide main-menu
  vs direct boot. A scene literally named `menu` is therefore unreachable by this route.
* A slash in the value would make `FindFileFromParentDirectories` walk a real path tree
  (`<path>/Resources`) and could call `SetResourcesPath` to something unexpected. Keep it a
  bare name.
* The value flows into `GameState+248` → `PathForResourceOfType(name, "scene")` →
  `Scene::LoadFromFile`, i.e. through the runner's VFS hooks; it must resolve under
  `g_resource_dir`.

---

## 5. Path of a boot, end to end (Ruby Touch)

```
Ruby file manager  -- Start scene in game (forest_part1.scene)
  RubyFileModel::startSceneInGame()
    resDir = <folder>, bootScene = "forest_part1"
      -> intent{resource_dir, boot_scene}
GameActivity.onCreate
  extract libswordigo.so + libopenal-soft.so from com.touchfoo.swordigo
  System.loadLibrary(GlossHook); System.load(libswordigo.so)
  System.loadLibrary(ruby_runner)
  setResourceDirectory(resDir)      -> VFS root for scene/POD/texture lookups
  loadHooks()                       -> GlossInit(true) + 6 VFS hooks + InitView hook
  setBootScene("forest_part1")      -> g_boot_scene armed
GameRenderer.onSurfaceCreated
  Native.setupNativeInterface(); Native.setupApplication()
    engine allocates CaverShell @ 0x651700 (arm64) / 0x460E68 (arm32)
    engine writes "menu" into CaverShell+104 / +72
GameRenderer.onSurfaceChanged
  Native.setApplicationViewSize(w, h, ...)
    first call -> CaverShell::InitView()
      -> OUR HOOK: +104 := "forest_part1"  (was "menu")
      -> ORIGINAL: +".scene" -> "forest_part1.scene"
                   != "menu.scene"  ->  synthetic testplayer profile
                   GameState+248 := "forest_part1"
                   SetCurrentViewController(GameViewController)
      -> Update(0.0f)
GameViewController::LoadView -> BackgroundLoad
  StateForLevelWithName("forest_part1", create-if-missing)
  Scene::LoadFromFile(...)          -> runner VFS hook serves the on-disk scene
  GameSceneController::InitWithScene
  SpawnHeroAt("")  -> falls back to "spawn_default"
```

---

## 6. Failure modes and mitigations

| # | Failure | Cause | Mitigation |
| :--- | :--- | :--- | :--- |
| 1 | Main menu appears instead of the scene | value written was `"menu"`, or the hook ran after `InitView` already executed | log the field on entry; assert it read `"menu"`; arm the scene before the GL surface exists |
| 2 | Crash in `InitView` immediately | field touched on the wrong ABI (offset 104 on arm32) or the short/long flag corrupted | per-ABI offset (compile-time), `String_size()` sanity check before writing, `String_destroy` before `String_create` |
| 3 | Scene loads but is empty / asset-less | scene not under `g_resource_dir`, or the folder is not a full resource tree | VFS-hook log lines `RubyRunnerVFS: VFS -> LOADED DISK ASSET ...` tell you immediately whether the scene resolved from disk |
| 4 | Boot OK, then the game writes into the player's real save | the direct boot always creates/uses the `testplayer` profile and its `GameState` | accepted by design (it is what the engine does); surfaces as a `testplayer` slot. Do not point it at a real profile |
| 5 | No level title / music on a custom scene | `ViewDidAppear` resolves the level through `Map::NodeForName` / `MapNode::MusicName`; an unknown level has no node | expected; note it in the UI rather than treating it as a bug |
| 6 | Resize before boot finishes double-injects | `setApplicationViewSize` fires again on rotation | `g_scene_consumed` latch + the engine's own `0x651759` one-shot flag |
| 7 | `dlopen`'d lib renamed / different engine version | symbol-based hook fails silently | `GlossHookByName` returning `NULL` logs; keep the `0x651700` + `"+104"` route documented as the fallback |
| 8 | arm32 Thumb-bit mistakes | arm32 `InitView` is at `0x28B501` (bit 0 set) | use `GlossHookByName` (mode-aware), never `GlossHookAddr` with a raw offset |

---

## 7. Why libGlossHook is the right substrate here (and where SRE is not)

Ruby Touch runs the **real** `libswordigo.so` natively on the device — no emulator, no
guest memory map. So none of the SRE machinery applies:

| SRE (Swordfare / desktop pod) | libGlossHook (Ruby Touch) |
| :--- | :--- |
| Dynarmic guest memory, `g_guest_memory + va` | real process memory, `GlossGetLibBias + rva` |
| `sre_resolve_address` / hardcoded VAs | `GlossSymbol` / `GlossHookByName` over `.dynsym` |
| Guest `call()` trampolines | direct branch to the original via `orig_InitView` |
| SRE Lua `Mini.GotoLevel` | engine's own `InitView` test path |

The library is **not** symbol-stripped in the way that matters: `libswordigo.so` 1.4.13
carries **17,519** `.dynsym` entries of which **14,503** are `Caver::*` (`.symtab` is gone,
`.dynsym` is rich). `Caver::CaverShell::InitView()` is exported in *both* ABIs, so the hook
is a symbol lookup, not an offset hunt — this is the touchfoo-unstripped advantage applied
to the mobile path.

`GlossInit(true)` is already called in `runner_install_hooks()`, and libswordigo is loaded
before ruby_runner, so `GlossHookByName` resolves immediately rather than registering a
deferred hook.

---

## 8. What still needs a device (verification plan)

1. **Cold boot, no scene request** — confirm the game still reaches the main menu, and that
   the hook's read-back log shows `"menu"` at `+104` / `+72`. This validates the offset on
   the real device for the *actual* ABI in use.
2. **Direct boot, stock scene** — start `town_part1` (or any shipped level name) and confirm
   arrival in-world, hero at `spawn_default`, and a `testplayer` profile appearing in the
   save dir.
3. **Direct boot, modified on-disk scene** — a Ruby-edited `.scene` in the resource root,
   confirming the VFS log shows `LOADED DISK ASSET` for it.
4. **Both ABIs** — `armeabi-v7a` on a 32-bit device/emulator exercises the `+72` offset and
   the `STRING_SHORT_CAP` fix.
5. **Rotation mid-boot** — no double injection, no crash.
6. **Regression** — the six existing VFS hooks still work (scene/POD/texture/audio
   redirection), i.e. adding the seventh hook did not disturb trampoline allocation.
7. **Second launch in the same process** — `GameActivity.onDestroy` kills the `:game`
   process, so state is pristine; confirm a second boot with a *different* scene works
   (i.e. `g_scene_consumed` does not leak across launches — it is process state, so it
   cannot, but the check is cheap).

---

## 9. Open questions

* **Does the direct boot skip the "profile select" screen entirely, or does it appear
  behind the transition?** Evidence says `SetCurrentViewController(GameViewController, 1,
  0.4, 0.4)` replaces the main menu outright, with no profile-select controller, so no —
  but this is a one-line visual confirmation on device.
* **`PlayerProfile+280` semantics.** It is a persisted bool (`Proto::PlayerProfile` field
  152), set only by this path. Whether the game treats it as "test profile", "cheats
  allowed", or something else is unresolved. Worth noting that a direct-boot profile may
  therefore behave differently in some game systems.
* **Can a scene be booted without creating a profile at all** — i.e. by pointing an
  existing profile's `GameState+248` at the scene and skipping the `testplayer` creation?
  That would be a strictly better UX for "preview my edit in my real save", but it means
  hooking *after* `InitView` (GVC+136 / `ProfileManager`) rather than before, and it risks
  writing a debug scene name into a real save. Out of scope for v1.
* **`--slow`** (`InitApplication` scans argv for the 6-char literal `"--slow"` and sets
  render-scale preference 3 to `0.5`) is the sibling hidden flag. Dead on Android for the
  same reason (no argv), but it is free to replicate through
  `FWShellPreferences::Set(shell+32, 3, 0.5f)` if a "slow/preview quality" toggle is ever
  wanted.
* **Does the `argv`/file-dialog branch have any use on Android?** It appears to be
  macOS/desktop-legacy that Touchfoo left compiled in. It is unreachable on Android because
  `handleApplicationLaunch` does not populate argv and `setupApplication` always sets `+104`
  to `"menu"`.

---

## 10. Evidence index

| Claim | Source |
| :--- | :--- |
| Swordfare injects `CaverShell+104` after `setupApplication` | `src/main.cpp:6035-6062` |
| `--scene` CLI, extension stripped, `<= 22` guard | `src/main.cpp:8871-8878`, `9015-9020` |
| `setupApplication` writes `"menu"` into `+104` / `+72` | `OpenSwordigo/arm64_13/.../0x507DF4__Java_..._setupApplication.c:253-258`; `arm32_13/.../0x37D378__...:171-175` |
| `InitView` reads it, appends `".scene"`, `"menu.scene"` split, `"testplayer"` profile | `arm64_13/.../0x321DB4__InitView.c`; `arm32_13/.../0x28B500__InitView.c` |
| `PlayerProfile` ctor / `+280` flag / `Proto` field 152 / `GameState` at `+240` | `0x47B9D8__PlayerProfile.c`, `0x47C5E0__SaveToProtobufMessage.c:409-420`, `0x47CBA8__LoadFromProtobufMessage.c:114` |
| Boot chain: `LoadView` → `BackgroundLoad` → `LoadFromFile` → `InitWithScene` → `SpawnHeroAt` | `0x42D570__LoadView.c`, `0x42D998__BackgroundLoad.c:430-580` |
| `spawn_default` fallback | `0x425890__SpawnHeroAt.c` |
| Scene path resolution uses `FileExistsAtPath` (already hooked by the runner) | `0x50B9C8__FindFileFromParentDirectories.c` |
| `NewCaverShell(argc, argv)` = the original argv entry | `arm64_13/.../0x3219F0__NewCaverShell.c`; `arm32_13/.../0x28B234__NewCaverShell.c` |
| Vtable slot mapping | `.rela.dyn` over `_ZTVN5Caver10CaverShellE` (`0x6189E8` / `0x43E2C4`) |
| `InitView` reachable only from `setApplicationViewSize`'s first-call branch | `0x508764__Java_..._setApplicationViewSize.c:83-95` |
| Symbol counts, exported `InitView`, per-ABI addresses | `nm -D dist/data/engine/v1.4.13/{arm64-v8a,armeabi-v7a}/libswordigo.so` |
| Ruby Touch boot plumbing (GameActivity / GameRenderer / runner hooks / VFS) | `GameActivity.java`, `GameRenderer.java`, `Native.java`, `runner_jni.c`, `runner_vfs.c`, `stdstring.{h,c}` |
| Ruby file-manager → game launch | `ruby_file_model.cpp:670-688`, `RubyActivity.java:784` |
