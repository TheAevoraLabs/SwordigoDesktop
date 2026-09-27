#include "runner_crasher.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <unwind.h>
#include <android/log.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

#define LOG_TAG "RubyCrashCatcher"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#if defined(__arm__)
#define GET_PC(ctx) ((ctx)->uc_mcontext.arm_pc)
#elif defined(__aarch64__)
#define GET_PC(ctx) ((ctx)->uc_mcontext.pc)
#elif defined(__i386__)
#define GET_PC(ctx) ((ctx)->uc_mcontext.gregs[REG_EIP])
#elif defined(__x86_64__)
#define GET_PC(ctx) ((ctx)->uc_mcontext.gregs[REG_RIP])
#else
#define GET_PC(ctx) 0
#endif

static struct sigaction g_old_sa[NSIG];
static char g_crash_log_path[512] = {0};

struct BacktraceState {
    int current_depth;
    int fd;
};

static const char *get_basename(const char *path) {
    if (!path) return "unknown_lib";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void log_both(int fd, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", buf);
    if (fd >= 0) {
        write(fd, buf, (size_t)n);
        write(fd, "\n", 1);
    }
}

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context* context, void* arg) {
    struct BacktraceState* state = (struct BacktraceState*)arg;
    uintptr_t pc = _Unwind_GetIP(context);

    if (pc) {
        Dl_info info;
        if (dladdr((void*)pc, &info) != 0) {
            const char *lib_name = get_basename(info.dli_fname);
            const char *sym_name = info.dli_sname ? info.dli_sname : "<stripped_symbol>";
            uintptr_t offset = pc - (uintptr_t)info.dli_fbase;

            log_both(state->fd,
                     "    #%02d pc %08zx  %s (%s + 0x%zx)",
                     state->current_depth, offset, lib_name, sym_name,
                     (info.dli_sname ? (pc - (uintptr_t)info.dli_saddr) : offset));
        } else {
            log_both(state->fd, "    #%02d pc %08zx  <unknown>", state->current_depth, pc);
        }
    }
    state->current_depth++;
    return _URC_NO_REASON;
}

static void native_crash_handler(int sig, siginfo_t *info, void *context) {
    ucontext_t *uc = (ucontext_t *)context;
    uintptr_t pc = GET_PC(uc);

    int fd = -1;
    if (g_crash_log_path[0] != '\0') {
        fd = open(g_crash_log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }

    time_t now = time(NULL);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    log_both(fd, "=== Ruby Touch Native Crash Catcher ===");
    log_both(fd, "Timestamp: %s", tbuf);
    log_both(fd, "Signal: %d (%s) at address %p", sig, strsignal(sig), info->si_addr);

    Dl_info dl_info;
    if (pc != 0 && dladdr((void*)pc, &dl_info) != 0) {
        const char *lib_name = get_basename(dl_info.dli_fname);
        const char *sym_name = dl_info.dli_sname ? dl_info.dli_sname : "<stripped>";
        uintptr_t offset = pc - (uintptr_t)dl_info.dli_fbase;
        log_both(fd, "Faulting Instruction: pc %08zx | %s | %s", offset, lib_name, sym_name);
    }

    log_both(fd, "--- Stack Trace ---");
    struct BacktraceState state = {0, fd};
    _Unwind_Backtrace(unwind_callback, &state);
    log_both(fd, "--- Stack Trace End ---");

    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }

    if (g_old_sa[sig].sa_flags & SA_SIGINFO) {
        if (g_old_sa[sig].sa_sigaction != NULL) {
            g_old_sa[sig].sa_sigaction(sig, info, context);
        }
    } else if (g_old_sa[sig].sa_handler != SIG_DFL && g_old_sa[sig].sa_handler != SIG_IGN) {
        g_old_sa[sig].sa_handler(sig);
    }
}

void runner_init_crasher(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sa.sa_sigaction = native_crash_handler;

    int signals[] = { SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS };
    for (int i = 0; i < (int)(sizeof(signals) / sizeof(signals[0])); i++) {
        sigaction(signals[i], &sa, &g_old_sa[signals[i]]);
    }
    LOGI("Native crash handler installed successfully");
}

void runner_set_crash_log_path(const char *path) {
    if (!path) {
        g_crash_log_path[0] = '\0';
        return;
    }
    strncpy(g_crash_log_path, path, sizeof(g_crash_log_path) - 1);
    g_crash_log_path[sizeof(g_crash_log_path) - 1] = '\0';
    LOGI("Native crash log path set to %s", g_crash_log_path);
}
