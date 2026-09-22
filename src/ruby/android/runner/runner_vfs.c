#include "runner_vfs.h"
#include "runner_scene.h"
#include "stdstring.h"
#include "Gloss.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>
#include <android/log.h>

#define LOG_TAG "RubyRunnerVFS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define HOOK_LIB_NAME "libswordigo.so"

static char g_resource_dir[1024] = {0};
static int g_hooks_installed = 0;

static GHook g_hook_newbyte_asset = NULL;
static GHook g_hook_newbyte_file = NULL;
static GHook g_hook_binaryfile = NULL;
static GHook g_hook_open_asset_fd = NULL;
static GHook g_hook_audio = NULL;
static GHook g_hook_fileexists = NULL;

void runner_set_resource_dir(const char *path) {
    if (path && path[0]) {
        snprintf(g_resource_dir, sizeof(g_resource_dir), "%s", path);
        // Strip trailing slash if present
        size_t len = strlen(g_resource_dir);
        if (len > 0 && g_resource_dir[len - 1] == '/') {
            g_resource_dir[len - 1] = '\0';
        }
    } else {
        g_resource_dir[0] = '\0';
    }
    LOGI("Resource directory set to: %s", g_resource_dir[0] ? g_resource_dir : "(vanilla)");
}

const char *runner_get_resource_dir(void) {
    return g_resource_dir;
}

static bool file_exists_on_disk(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Host-controlled VFS path resolution
// Touchfoo's engine prefixes "resources/" to asset queries (e.g. "resources/town_herohouse.scene").
// If the user's g_resource_dir points to ".../assets/resources", simply appending results in
// ".../assets/resources/resources/town_herohouse.scene" which misses.
// resolve_mod_resource() strips prefixes and probes candidate locations.
static const char *resolve_mod_resource(const char *res) {
    static __thread char resolved[1024];
    if (!g_resource_dir[0] || !res || !res[0]) return NULL;

    // Normalize backslashes to forward slashes in a local buffer
    char clean[512];
    size_t i = 0;
    while (res[i] && i + 1 < sizeof(clean)) {
        clean[i] = (res[i] == '\\') ? '/' : res[i];
        i++;
    }
    clean[i] = '\0';

    const char *p = clean;
    while (*p == '/') p++;

    // Extract bare path without "assets/resources/", "resources/", or "assets/"
    const char *bare = p;
    if (strncasecmp(bare, "assets/resources/", 17) == 0) {
        bare += 17;
    } else if (strncasecmp(bare, "resources/", 10) == 0) {
        bare += 10;
    } else if (strncasecmp(bare, "assets/", 7) == 0) {
        bare += 7;
    }
    while (*bare == '/') bare++;

    char cand[1024];

    // Candidate 1: g_resource_dir / bare
    // (e.g. g_resource_dir is ".../assets/resources", bare is "town_herohouse.scene")
    snprintf(cand, sizeof(cand), "%s/%s", g_resource_dir, bare);
    if (file_exists_on_disk(cand)) {
        snprintf(resolved, sizeof(resolved), "%s", cand);
        return resolved;
    }

    // Candidate 2: g_resource_dir / p (verbatim relative path)
    snprintf(cand, sizeof(cand), "%s/%s", g_resource_dir, p);
    if (file_exists_on_disk(cand)) {
        snprintf(resolved, sizeof(resolved), "%s", cand);
        return resolved;
    }

    // Candidate 3: g_resource_dir / "resources" / bare
    // (e.g. g_resource_dir is parent mod root or ".../assets")
    snprintf(cand, sizeof(cand), "%s/resources/%s", g_resource_dir, bare);
    if (file_exists_on_disk(cand)) {
        snprintf(resolved, sizeof(resolved), "%s", cand);
        return resolved;
    }

    // Candidate 4: g_resource_dir / "assets/resources" / bare
    // (e.g. g_resource_dir is base package directory)
    snprintf(cand, sizeof(cand), "%s/assets/resources/%s", g_resource_dir, bare);
    if (file_exists_on_disk(cand)) {
        snprintf(resolved, sizeof(resolved), "%s", cand);
        return resolved;
    }

    return NULL;
}

// ── 1. NewByteBufferFromAndroidAsset Hook ─────────────────────────────────────
typedef void* (*NewByteBufferFromAA_fn)(String *file, unsigned int *outSize);
static NewByteBufferFromAA_fn orig_NewByteBufferFromAA = NULL;

static void* hook_NewByteBufferFromAA(String *file, unsigned int *outSize) {
    const char *name = String_get(file);
    const char *custom_path = resolve_mod_resource(name);

    if (custom_path) {
        FILE *f = fopen(custom_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz >= 0) {
                void *buf = malloc((size_t)sz);
                if (buf) {
                    size_t readBytes = fread(buf, 1, (size_t)sz, f);
                    fclose(f);
                    if (outSize) *outSize = (unsigned int)readBytes;
                    LOGI("VFS -> LOADED DISK ASSET (NewByteBufferFromAA): %s (%u bytes) for '%s'",
                         custom_path, (unsigned int)readBytes, name ? name : "");
                    return buf;
                }
            }
            fclose(f);
        }
    }

    if (orig_NewByteBufferFromAA) {
        return orig_NewByteBufferFromAA(file, outSize);
    }
    return NULL;
}

// ── 2. NewByteBufferFromFile Hook ───────────────────────────────────────────
typedef void* (*NewByteBufferFromFile_fn)(String *file, unsigned int *outSize);
static NewByteBufferFromFile_fn orig_NewByteBufferFromFile = NULL;

static void* hook_NewByteBufferFromFile(String *file, unsigned int *outSize) {
    const char *name = String_get(file);
    const char *custom_path = resolve_mod_resource(name);

    if (custom_path) {
        FILE *f = fopen(custom_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz >= 0) {
                void *buf = malloc((size_t)sz);
                if (buf) {
                    size_t readBytes = fread(buf, 1, (size_t)sz, f);
                    fclose(f);
                    if (outSize) *outSize = (unsigned int)readBytes;
                    LOGI("VFS -> LOADED DISK ASSET (NewByteBufferFromFile): %s (%u bytes) for '%s'",
                         custom_path, (unsigned int)readBytes, name ? name : "");
                    return buf;
                }
            }
            fclose(f);
        }
    }

    if (orig_NewByteBufferFromFile) {
        return orig_NewByteBufferFromFile(file, outSize);
    }
    return NULL;
}

// ── 3. BinaryFile::Open Hook ────────────────────────────────────────────────
typedef unsigned int (*BinaryFile_Open_fn)(void *this_ptr, String *filename, int mode, bool use_asset);
static BinaryFile_Open_fn orig_BinaryFile_Open = NULL;

static unsigned int hook_BinaryFile_Open(void *this_ptr, String *filename, int mode, bool use_asset) {
    const char *name = String_get(filename);
    const char *custom_path = resolve_mod_resource(name);

    if (custom_path) {
        int fd = open(custom_path, O_RDONLY);
        if (fd >= 0) {
            const char *m = (mode == 1) ? "wb" : "rb";
            void *gz = gzdopen(fd, m);
            if (gz) {
                *(int *)this_ptr = 2;
#if defined(__aarch64__)
                *(void **)((char *)this_ptr + 8) = gz;
                *(int *)((char *)this_ptr + 0x10) = 0;
#else
                *(void **)((char *)this_ptr + 4) = gz;
                *(int *)((char *)this_ptr + 8) = 0;
#endif
                LOGI("VFS -> LOADED DISK ASSET (BinaryFile::Open): %s for '%s'", custom_path, name ? name : "");
                return 1;
            }
            close(fd);
        }
    }

    if (orig_BinaryFile_Open) {
        return orig_BinaryFile_Open(this_ptr, filename, mode, use_asset);
    }
    return 0;
}

// ── 4. OpenAAssetFileDescriptor Hook ────────────────────────────────────────
typedef int (*OpenAAssetFD_fn)(String *file);
static OpenAAssetFD_fn orig_OpenAAssetFD = NULL;

static int hook_OpenAAssetFD(String *file) {
    const char *name = String_get(file);
    const char *custom_path = resolve_mod_resource(name);

    if (custom_path) {
        int fd = open(custom_path, O_RDONLY);
        if (fd >= 0) {
            LOGI("VFS -> LOADED DISK ASSET (OpenAAssetFD): %s for '%s' (fd=%d)",
                 custom_path, name ? name : "", fd);
            return fd;
        }
    }

    if (orig_OpenAAssetFD) {
        return orig_OpenAAssetFD(file);
    }
    return -1;
}

// ── 5. GetAudioFileData Hook (WAV parsing) ───────────────────────────────────
typedef bool (*GetAudioFileData_fn)(String *path, int *fmt, void **data, int *size, int *rate);
static GetAudioFileData_fn orig_GetAudioFileData = NULL;

static bool hook_GetAudioFileData(String *path, int *fmt, void **data, int *size, int *rate) {
    const char *name = String_get(path);
    const char *custom_path = resolve_mod_resource(name);

    if (custom_path) {
        FILE *f = fopen(custom_path, "rb");
        if (f) {
            unsigned char hdr[0x2c];
            if (fread(hdr, 1, 0x2c, f) == 0x2c) {
                // RIFF ... WAVE ... fmt  ... data
                if (*(unsigned int *)(hdr + 0) == 0x46464952 &&
                    *(unsigned int *)(hdr + 8) == 0x45564157 &&
                    *(unsigned int *)(hdr + 12) == 0x20746d66 &&
                    *(unsigned int *)(hdr + 36) == 0x61746164) {
                    
                    unsigned int datasz = *(unsigned int *)(hdr + 40);
                    void *buf = malloc(datasz);
                    if (buf && fread(buf, 1, datasz, f) == datasz) {
                        fclose(f);
                        short ch = *(short *)(hdr + 22);
                        short bits = *(short *)(hdr + 34);
                        int sr = *(int *)(hdr + 24);

                        int bf = 0;
                        if (bits == 16) bf = (ch == 1) ? 2 : 4;
                        if (bits == 8)  bf = (ch == 1) ? 1 : 3;

                        if (fmt)  *fmt = bf;
                        if (data) *data = buf;
                        if (size) *size = (int)datasz;
                        if (rate) *rate = sr;
                        LOGI("VFS -> LOADED DISK ASSET (Audio): %s for '%s'", custom_path, name ? name : "");
                        return true;
                    }
                    if (buf) free(buf);
                }
            }
            fclose(f);
        }
    }

    if (orig_GetAudioFileData) {
        return orig_GetAudioFileData(path, fmt, data, size, rate);
    }
    return false;
}

// ── 6. FileExistsAtPath Hook ────────────────────────────────────────────────
typedef bool (*FileExistsAtPath_fn)(String *path);
static FileExistsAtPath_fn orig_FileExistsAtPath = NULL;

static bool hook_FileExistsAtPath(String *path) {
    const char *p = String_get(path);
    if (p && p[0]) {
        const char *custom_path = resolve_mod_resource(p);
        if (custom_path) {
            return true;
        }
    }

    if (orig_FileExistsAtPath) {
        return orig_FileExistsAtPath(path);
    }
    return false;
}

// ── Install & Uninstall Hooks ───────────────────────────────────────────────
void runner_install_hooks(void) {
    if (g_hooks_installed) return;

    LOGI("Initializing GlossHook for libswordigo VFS hooks...");
    GlossInit(true);

    g_hook_newbyte_asset = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver29NewByteBufferFromAndroidAssetERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEPj",
        (void *)hook_NewByteBufferFromAA,
        (void **)&orig_NewByteBufferFromAA,
        NULL
    );

    g_hook_newbyte_file = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver21NewByteBufferFromFileERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEPj",
        (void *)hook_NewByteBufferFromFile,
        (void **)&orig_NewByteBufferFromFile,
        NULL
    );

    g_hook_binaryfile = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver10BinaryFile4OpenERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEENS0_4ModeEb",
        (void *)hook_BinaryFile_Open,
        (void **)&orig_BinaryFile_Open,
        NULL
    );

    g_hook_open_asset_fd = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver24OpenAAssetFileDescriptorERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE",
        (void *)hook_OpenAAssetFD,
        (void **)&orig_OpenAAssetFD,
        NULL
    );

    g_hook_audio = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver16GetAudioFileDataERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEPNS_11AudioBuffer12BufferFormatEPPvPiSE_",
        (void *)hook_GetAudioFileData,
        (void **)&orig_GetAudioFileData,
        NULL
    );

    g_hook_fileexists = GlossHookByName(
        HOOK_LIB_NAME,
        "_ZN5Caver16FileExistsAtPathERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE",
        (void *)hook_FileExistsAtPath,
        (void **)&orig_FileExistsAtPath,
        NULL
    );

    /* Seventh hook: CaverShell::InitView, which is what makes "start this .scene in
     * game" possible. Independent of the VFS hooks and harmless when no scene was
     * requested — it simply forwards. */
    runner_install_scene_hook();

    g_hooks_installed = 1;
    LOGI("Ruby Runner VFS hooks successfully installed!");
}

void runner_uninstall_hooks(void) {
    if (!g_hooks_installed) return;
    LOGI("Uninstalling Ruby Runner VFS hooks...");
    runner_uninstall_scene_hook();
    g_resource_dir[0] = '\0';
    g_hooks_installed = 0;
}

