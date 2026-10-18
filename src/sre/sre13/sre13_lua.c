/*
 * sre13_lua.c — Safe Lua error boundary & execution for Swordigo 1.4.13
 *
 * This is the Lua-coroutine safety layer that SRE v1.4.12 provides for the
 * Wastelands freeze: the host patches the engine's exported lua_resume /
 * lua_pcall entry points to these wrappers (main.cpp installs the lua_resume
 * trampoline onto sre_lua_resume_safe for every loaded libsre). Every timer
 * coroutine resume that ProgramState::Update fires therefore runs here, where
 * we can:
 *
 *   1. bound runaway scripts with an instruction-count hook (the
 *      "stuck PC loop" freeze: an infinite guest loop that never returns and
 *      starves SDL / trips the emulator's spin detectors);
 *   2. turn errors into error returns instead of panics/aborts;
 *   3. keep the Lua stack balanced so the VM survives the error.
 *
 * 1.4.13 Lua error containment (THE BOOT-FREEZE FIX):
 *   The engine's Lua signals errors with C++ exceptions (luaD_throw sets
 *   errorJmp->status then __cxa_throw(lua_longjmp*)). The emulator's custom
 *   ELF loader never registers the guest .eh_frame with the host linker, so
 *   the C++ unwinder can't find a handler -> std::terminate -> abort ->
 *   abort-recovery mis-unwinds -> stuck-yield spin (observed at boot during
 *   setApplicationViewSize -> menu.scene Lua). Every wrapper here therefore
 *   pushes a guest setjmp recovery entry before executing Lua: a Lua error
 *   hits sre_luaD_throw (hooked by offset, IDA sub_569BA8) or sre_cxa_throw
 *   (hooked __cxa_throw backstop, sre13_recovery.c) and longjmps back to the
 *   wrapper, which restores the VM state and returns an error code. The
 *   uncaught-exception -> terminate path can no longer be reached.
 *
 * ProgramState member offsets on ARM64 for Swordigo 1.4.13 (IDA 0x545480):
 *   L (lua_State*) @ +0x00, coroutine @ +0x08 (NULL = plain thread)
 *   isSuspended int @ +0x50, sleepTime float @ +0x54, completed byte @ +0x5B.
 */

#include "sre13.h"

#include <stdio.h>
#include <string.h>

/* Vendored Lua 5.1.5 headers — struct layout matches official libswordigo.so
 * (built with vanilla Lua 5.1.5 64-bit): status@0x0A, top@0x10, base@0x18,
 * ci@0x28, stack@0x40, base_ci@0x50, nCcalls@0x60, baseCcalls@0x62,
 * openupval@0x98, errorJmp@0xA8. LUA_ERRORJMP_OFFSET (0xA8) from sre_setjmp.h
 * matches offsetof(lua_State, errorJmp) exactly. */
#include "lua.h"
#include "lstate.h"
#include "lfunc.h"
#include "ldo.h"

/* Guest setjmp/longjmp + recovery stack (base/sre_setjmp.*). LUA_ERRORJMP_OFFSET
 * (0xA8) matches the 1.4.13 engine layout verified in sre13.h. */
#include "sre_setjmp.h"
#include "sre13_console.h"
#include "sre13_scene_shifter.h"
#include "sre13_rbmath.h"
#include "lualib.h"
#include "ui/sre13_button_controller.h"

/* Recovery stack — shared with sre13_recovery.c (sre_cxa_throw /
 * sre_luaD_throw longjmp to g_sre_recovery_stack[...].buf) and the host
 * (configure_recovery_context resolves these three symbols). */
sre_recovery_entry g_sre_recovery_stack[SRE_MAX_RECOVERY];
int g_sre_recovery_depth = 0;
const unsigned int g_sre_recovery_stack_bytes = sizeof(g_sre_recovery_stack);

/* DIAG (RLSW boot-freeze): counts the first luaD_throw calls so the initial
 * error message + caller can be logged without flooding the log. */
static int g_sre13_throw_diag_count = 0;
static int g_sre13_throw_depth0_count = 0;

int sre13_recovery_push(void* L) {
    if (g_sre_recovery_depth >= SRE_MAX_RECOVERY) return -1;
    int idx = g_sre_recovery_depth;
    sre_recovery_entry* e = &g_sre_recovery_stack[idx];
    e->lua_state = L;
    if (L) {
        void** ejp = (void**)((char*)L + LUA_ERRORJMP_OFFSET);
        e->saved_errorJmp = *ejp;
    } else {
        e->saved_errorJmp = 0;
    }
    g_sre_recovery_depth++;
    return idx;
}

void sre13_recovery_pop(int depth) {
    g_sre_recovery_depth = depth;
}

/* ========== luaD_throw hook (IDA sub_569BA8, no exported symbol) ==========
 * ROOT of ALL Lua error handling in the 1.4.13 engine. Original:
 *   errorJmp = *(L + 0xA8);
 *   if (errorJmp) { errorJmp->status(+12) = errcode; __cxa_throw(lua_longjmp*); }
 *   L->status(+0x0A) = errcode; panic handler; exit(1);
 * Both the C++ throw and the panic/exit path crash in the emulator (broken
 * unwind / process exit). Our replacement sets the status fields the same
 * way, then longjmps to the nearest SRE recovery point. When no recovery
 * point is armed it returns instead of crashing — the caller (lua_error /
 * luaG_runerror) sees a swallowed error, which beats terminate/abort.
 * NEVER calls the original (it is __noreturn; falling through into the
 * instruction after BL would hit unreachable code). */
void sre_luaD_throw(void* L, int errcode) {
    void** ejp = (void**)((char*)L + LUA_ERRORJMP_OFFSET);
    void* errorJmp = *ejp;

    if (errorJmp) {
        /* errorJmp is a lua_longjmp struct { int status; ... } — status@+12. */
        *(int*)((char*)errorJmp + 12) = errcode;
    } else {
        /* L->status byte @+0x0A (matches IDA: *(BYTE *)(L + 10) = errcode). */
        *((char*)L + 0x0A) = (char)errcode;
    }

    /* DIAG (RLSW boot-freeze): log the first errors with the message off the
     * Lua stack (L->top-1 when thrown from luaG_runerror / lua_error) and the
     * guest caller so the initial failing script/chunk can be identified. */
    if (g_sre13_throw_diag_count < 24) {
        g_sre13_throw_diag_count++;
        unsigned long long g_lr = 0;
        asm volatile("mov %0, x30" : "=r"(g_lr));
        char msg[160];
        msg[0] = 0;
        if (L) {
            /* lua_State: top @+0x10. TValue = { value@+0, tt@+8 };
             * LUA_TSTRING = 4; TString: next@0, tt@8, marked@9, len@16,
             * data@24 (Lua 5.1 layout, matches the engine's strings). */
            char* top = *(char**)((char*)L + 0x10);
            if (top) {
                int tt = *(int*)(top - 8);
                if (tt == 4) {
                    char* ts = *(char**)(top - 16);
                    if (ts) {
                        size_t sl = *(size_t*)(ts + 16);
                        if (sl > 150) sl = 150;
                        if (sl) { memcpy(msg, ts + 24, sl); msg[sl] = 0; }
                    }
                }
            }
        }
        fprintf(stderr,
                "[SRE13/luaD_throw] #%d errcode=%d depth=%d caller=0x%llx msg=\"%s\"\n",
                g_sre13_throw_diag_count, errcode, g_sre_recovery_depth, g_lr, msg);
        fflush(stderr);
    }

    if (g_sre_recovery_depth > 0) {
        int target = g_sre_recovery_depth - 1;
        sre_recovery_entry* entry = &g_sre_recovery_stack[target];
        if (entry->lua_state) {
            void** saved_ejp = (void**)((char*)entry->lua_state + LUA_ERRORJMP_OFFSET);
            *saved_ejp = entry->saved_errorJmp;
        }
        sre_longjmp(entry->buf, 1);
        /* never reaches here */
    }
    if (g_sre_recovery_depth == 0) {
        g_sre13_throw_depth0_count++;
        if (g_sre13_throw_depth0_count <= 5) {
            unsigned long long g_lr = 0;
            asm volatile("mov %0, x30" : "=r"(g_lr));
            fprintf(stderr,
                    "[SRE13/luaD_throw] NO-RECOVERY depth=0 caller=0x%llx errcode=%d "
                    "(count=%d) — returning into guest noreturn code!\n",
                    g_lr, errcode, g_sre13_throw_depth0_count);
            fflush(stderr);
        }
    }
    /* No recovery point — return. The caller continues after its (assumed
     * unreachable) luaD_throw call; error status is already recorded. */
}

/* ProgramState member offsets on ARM64 for Swordigo 1.4.13 */
#define PS_LUA_STATE      0x00
#define PS_COROUTINE      0x08
#define PS_GET(obj, off, type) (*(type*)((char*)(obj) + (off)))

/* Lua API function pointers — filled by sre_init_lua / sre_init_lua_ext */
typedef int   (*pfn_lua_pcall)(lua_State* L, int nargs, int nresults, int errfunc);
typedef int   (*pfn_lua_resume)(lua_State* L, int narg);
typedef void  (*pfn_lua_call)(lua_State* L, int nargs, int nresults);
typedef void  (*pfn_lua_settop)(lua_State* L, int idx);
typedef int   (*pfn_lua_gettop)(lua_State* L);
typedef const char* (*pfn_lua_tolstring)(lua_State* L, int idx, size_t* len);
typedef void  (*pfn_lua_pushstring)(lua_State* L, const char* s);
typedef void  (*pfn_lua_pushnil)(lua_State* L);
typedef int   (*pfn_lua_type)(lua_State* L, int idx);
typedef void  (*pfn_lua_error)(lua_State* L);
typedef void  (*pfn_lua_sethook)(lua_State* L, lua_Hook hook, int mask, int count);

pfn_lua_pcall       g_lua_pcall = NULL;
pfn_lua_resume      g_lua_resume = NULL;
pfn_lua_call        g_lua_call = NULL;
pfn_lua_settop      g_lua_settop = NULL;
pfn_lua_gettop      g_lua_gettop = NULL;
pfn_lua_tolstring   g_lua_tolstring = NULL;
pfn_lua_pushstring  g_lua_pushstring = NULL;
pfn_lua_pushnil     g_lua_pushnil = NULL;
pfn_lua_type        g_lua_type = NULL;
pfn_lua_error       g_lua_error = NULL;
pfn_lua_sethook     g_lua_sethook = NULL;

/* Host-visible diagnostics (read via get_symbol_vaddr, no stdio needed) */
volatile int  g_sre13_lua_error_count = 0;
char          g_sre13_last_lua_error[256] = {0};

/* Index map — MUST match main.cpp's lua_syms[] (passed to sre_init_lua):
 *   0 lua_pcall, 1 lua_resume, 2 lua_settop, 3 lua_gettop, 4 lua_tolstring,
 *   5 lua_call,  6 lua_pushstring, ... 16 lua_type, ... 20 lua_error. */
void sre_init_lua(uint64_t* funcs) {
    if (!funcs) return;
    g_lua_pcall       = (pfn_lua_pcall)funcs[0];
    g_lua_resume      = (pfn_lua_resume)funcs[1];
    g_lua_settop      = (pfn_lua_settop)funcs[2];
    g_lua_gettop      = (pfn_lua_gettop)funcs[3];
    g_lua_tolstring   = (pfn_lua_tolstring)funcs[4];
    g_lua_call        = (pfn_lua_call)funcs[5];
    g_lua_pushstring  = (pfn_lua_pushstring)funcs[6];
    g_lua_pushnil     = (pfn_lua_pushnil)funcs[13];
    g_lua_type        = (pfn_lua_type)funcs[16];
    g_lua_error       = (pfn_lua_error)funcs[20];
}

/* Extended Lua API — MUST match main.cpp's lua_ext_syms[]:
 * index 31 = lua_sethook. Called by the host right after sre_init_lua. */
void sre_init_lua_ext(uint64_t* funcs) {
    if (!funcs) return;
    g_lua_sethook = (pfn_lua_sethook)funcs[31];
}

/* ── Runaway-script watchdog (Wastelands "stuck PC loop" freeze) ──────────
 * ProgramState coroutines (Sleep/Wait loops, monster AI) are resumed from
 * ProgramState::Update every frame. A script with an accidental infinite
 * loop never returns from lua_resume: the guest spins in one JIT region,
 * the emulator logs "stuck PC"/"Spin loop", and SDL starves -> freeze.
 * Arm a Lua instruction-count hook around every resume/pcall; if the script
 * exceeds the budget, the hook raises a Lua error that lua_resume/lua_pcall
 * convert into an ordinary error return (never an abort). */
#define SRE13_RUN_AWAY_BUDGET  100000

static void sre13_lua_timeout_hook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    /* Disarm immediately: the raise below unwinds this frame. */
    if (g_lua_sethook) g_lua_sethook(L, NULL, 0, 0);
    if (g_lua_pushstring && g_lua_error) {
        g_lua_pushstring(L, "[SRE13] Runaway script limit reached (infinite loop prevented)");
        g_lua_error(L);   /* caught by the enclosing pcall/resume errorJmp */
    }
}

void sre13_arm_timeout(lua_State* L) {
    if (g_lua_sethook)
        g_lua_sethook(L, sre13_lua_timeout_hook, LUA_MASKCOUNT, SRE13_RUN_AWAY_BUDGET);
}

void sre13_disarm_timeout(lua_State* L) {
    if (g_lua_sethook) g_lua_sethook(L, NULL, 0, 0);
}

/* Copy the error string off the top of the Lua stack into the host-visible
 * diagnostics buffer (bounded, freestanding-safe). */
static void sre13_capture_error(lua_State* L) {
    if (!L || !g_lua_tolstring) return;
    size_t len = 0;
    const char* err = g_lua_tolstring(L, -1, &len);
    if (err) {
        int i;
        g_sre13_lua_error_count++;
        for (i = 0; err[i] && i < 255; i++)
            g_sre13_last_lua_error[i] = err[i];
        g_sre13_last_lua_error[i] = 0;
    }
}

/* ── Longjmp recovery helpers ──────────────────────────────────────────────
 * Each wrapper saves the VM state, pushes a recovery entry, and setjmps.
 * If sre_luaD_throw / sre_cxa_throw longjmps back (value != 0), the VM is
 * rewound to the saved baseline: close open upvalues above the saved top,
 * restore ci/top/base/nCcalls, and drop the failed function+args so the
 * caller sees a clean (error) return. luaF_close is the vendored copy
 * compiled into this library — it only touches openupval@0x70 + the upvalue
 * chain, both at stock offsets, so it is safe on the engine's lua_State. */

static void sre13_restore_vm(lua_State* L, CallInfo* saved_ci, StkId saved_top,
                             StkId saved_base, unsigned short saved_nCcalls,
                             int drop_nargs) {
    if (!L) return;
    luaF_close(L, saved_top);
    L->ci = saved_ci;
    L->top = saved_top;
    L->base = saved_base;
    L->nCcalls = saved_nCcalls;
    if (drop_nargs > 0) {
        StkId new_top = saved_top - (drop_nargs + 1);
        if (new_top >= L->stack) L->top = new_top;
    }
}

/* Idempotent module injector for live states. Checks if _G.ffi exists;
 * if not, registers ButtonController, SceneShifter, Mini, ffi, and memory. */
void sre13_ensure_injected(lua_State* L) {
    if (!L) return;
    int top = lua_gettop(L);

    /* Open standard Lua 5.1 base library if not present */
    lua_getfield(L, LUA_GLOBALSINDEX, "setmetatable");
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_settop(L, top);
        luaopen_base(L);
        lua_settop(L, top);
    } else {
        lua_settop(L, top);
    }

    /* Open standard Lua 5.1 math library if not present */
    lua_getfield(L, LUA_GLOBALSINDEX, "math");
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_settop(L, top);
        luaopen_math(L);
        lua_settop(L, top);
    } else {
        lua_settop(L, top);
    }

    /* Open standard Lua 5.1 table library if not present */
    lua_getfield(L, LUA_GLOBALSINDEX, "table");
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_settop(L, top);
        luaopen_table(L);
        lua_settop(L, top);
    } else {
        lua_settop(L, top);
    }

    /* Open standard Lua 5.1 debug library if not present */
    lua_getfield(L, LUA_GLOBALSINDEX, "debug");
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_settop(L, top);
        luaopen_debug(L);
        lua_settop(L, top);
    } else {
        lua_settop(L, top);
    }

    /* Inject rbmath module (xyz_math-1.9 + Vector3 bridges) */
    sre13_inject_rbmath(L);

    /* Fast check: is ffi already in globals? */
    lua_getfield(L, LUA_GLOBALSINDEX, "ffi");
    int has_ffi = (lua_type(L, -1) == LUA_TTABLE);
    lua_settop(L, top);
    if (has_ffi) return;

    /* First time for this live lua_State — inject custom modules into _G */
    sre13_register_button_controller(L);
    sre13_register_scene_shifter_lua(L);
    sre13_extras_inject(L);
    lua_settop(L, top);
}

/* sre_lua_call_safe — replaces engine's raw lua_call with protected lua_pcall */
void sre_lua_call_safe(void* Lv, int nargs, int nresults) {
    lua_State* L = (lua_State*)Lv;
    if (!L || !g_lua_pcall) return;

    sre13_ensure_injected(L);

    CallInfo* saved_ci = L->ci;
    StkId saved_top = L->top;
    StkId saved_base = L->base;
    unsigned short saved_nCcalls = L->nCcalls;

    int my_depth = sre13_recovery_push(L);
    if (my_depth < 0) {
        /* Recovery stack full — call without protection (fallback) */
        int res;
        sre13_arm_timeout(L);
        res = g_lua_pcall(L, nargs, nresults, 0);
        sre13_disarm_timeout(L);
        if (res != 0) {
            sre13_capture_error(L);
            if (g_lua_settop) g_lua_settop(L, -2);
        }
        return;
    }

    if (sre_setjmp(g_sre_recovery_stack[my_depth].buf) != 0) {
        /* Caught Lua error via sre_luaD_throw / sre_cxa_throw longjmp.
         * errorJmp was already restored by the throw hook. */
        sre13_recovery_pop(my_depth);
        sre13_disarm_timeout(L);
        g_sre13_lua_error_count++;
        sre13_restore_vm(L, saved_ci, saved_top, saved_base, saved_nCcalls, nargs);
        if (nresults > 0 && nresults != -1 && g_lua_pushnil) {
            int i;
            for (i = 0; i < nresults; i++) g_lua_pushnil(L);
        }
        return;
    }

    int res;
    sre13_arm_timeout(L);
    res = g_lua_pcall(L, nargs, nresults, 0);
    sre13_disarm_timeout(L);
    sre13_recovery_pop(my_depth);
    if (res != 0) {
        sre13_capture_error(L);
        if (g_lua_settop) g_lua_settop(L, -2);
        if (nresults > 0 && nresults != -1 && g_lua_pushnil) {
            int i;
            for (i = 0; i < nresults; i++) g_lua_pushnil(L);
        }
    }
}

/* sre_lua_resume_safe — protected coroutine resume */
int sre_lua_resume_safe(void* Lv, int narg) {
    lua_State* L = (lua_State*)Lv;
    if (!L || !g_lua_resume) return 0;

    CallInfo* saved_ci = L->ci;
    StkId saved_top = L->top;
    StkId saved_base = L->base;
    unsigned short saved_nCcalls = L->nCcalls;

    int my_depth = sre13_recovery_push(L);
    if (my_depth < 0) {
        int res;
        sre13_arm_timeout(L);
        res = g_lua_resume(L, narg);
        sre13_disarm_timeout(L);
        if (res != 0 && res != 1 /* LUA_YIELD */) {
            sre13_capture_error(L);
            if (g_lua_settop) g_lua_settop(L, -2);
        }
        return res;
    }

    if (sre_setjmp(g_sre_recovery_stack[my_depth].buf) != 0) {
        sre13_recovery_pop(my_depth);
        sre13_disarm_timeout(L);
        g_sre13_lua_error_count++;
        sre13_capture_error(L);
        sre13_restore_vm(L, saved_ci, saved_top, saved_base, saved_nCcalls, 0);
        return 2; /* LUA_ERRRUN */
    }

    int res;
    sre13_arm_timeout(L);
    res = g_lua_resume(L, narg);
    sre13_disarm_timeout(L);
    sre13_recovery_pop(my_depth);
    if (res != 0 && res != 1 /* LUA_YIELD */) {
        sre13_capture_error(L);
        if (g_lua_settop) g_lua_settop(L, -2);
    }
    return res;
}

/* Safe ProgramState::Execute — replaces raw lua_call with protected lua_pcall */
void sre_ProgramState_Execute(void* self, int stackIndex) {
    if (!self) return;
    lua_State* L = PS_GET(self, PS_LUA_STATE, lua_State*);
    if (!L || !g_lua_pcall) return;

    /* Ensure custom modules (ButtonController, SceneShifter, Mini, ffi, memory)
     * are registered into _G for this live state. */
    sre13_ensure_injected(L);

    /* Capture the live game lua_State and ProgramState for the per-frame console tick */
    sre13_console_set_root_and_ps(L, self);

    /* Service the live console ONCE per frame (guard inside) so game C++ APIs
     * called from console scripts run in the valid ProgramState scene context,
     * not at the shell update tail. */
    sre13_console_frame_tick(0.0f);

    if (__builtin_expect(g_sre_scene_shift_pending != 0 || g_sre_scene_shift_active != 0, 0)) {
        sre13_scene_shifter_tick();
    }

    CallInfo* saved_ci = L->ci;
    StkId saved_top = L->top;
    StkId saved_base = L->base;
    unsigned short saved_nCcalls = L->nCcalls;

    void* coroutine = PS_GET(self, PS_COROUTINE, void*);
    if (coroutine == NULL) {
        int my_depth = sre13_recovery_push(L);
        if (my_depth < 0) {
            int res;
            sre13_arm_timeout(L);
            res = g_lua_pcall(L, stackIndex, 0, 0);
            sre13_disarm_timeout(L);
            if (res != 0) {
                sre13_capture_error(L);
                if (g_lua_settop) g_lua_settop(L, -2);
            }
            return;
        }
        if (sre_setjmp(g_sre_recovery_stack[my_depth].buf) != 0) {
            sre13_recovery_pop(my_depth);
            sre13_disarm_timeout(L);
            g_sre13_lua_error_count++;
            sre13_restore_vm(L, saved_ci, saved_top, saved_base, saved_nCcalls, stackIndex);
            return;
        }
        int res;
        sre13_arm_timeout(L);
        res = g_lua_pcall(L, stackIndex, 0, 0);
        sre13_disarm_timeout(L);
        sre13_recovery_pop(my_depth);
        if (res != 0) {
            sre13_capture_error(L);
            if (g_lua_settop) g_lua_settop(L, -2);
        }
    } else {
        if (g_lua_resume) {
            int my_depth = sre13_recovery_push(L);
            if (my_depth < 0) {
                int res;
                sre13_arm_timeout(L);
                res = g_lua_resume(L, stackIndex);
                sre13_disarm_timeout(L);
                if (res != 0 && res != 1 /* LUA_YIELD */) {
                    sre13_capture_error(L);
                    if (g_lua_settop) g_lua_settop(L, -2);
                }
                return;
            }
            if (sre_setjmp(g_sre_recovery_stack[my_depth].buf) != 0) {
                sre13_recovery_pop(my_depth);
                sre13_disarm_timeout(L);
                g_sre13_lua_error_count++;
                sre13_capture_error(L);
                sre13_restore_vm(L, saved_ci, saved_top, saved_base, saved_nCcalls, 0);
                return;
            }
            int res;
            sre13_arm_timeout(L);
            res = g_lua_resume(L, stackIndex);
            sre13_disarm_timeout(L);
            sre13_recovery_pop(my_depth);
            if (res != 0 && res != 1 /* LUA_YIELD */) {
                sre13_capture_error(L);
                if (g_lua_settop) g_lua_settop(L, -2);
            }
        }
    }
}

/* Safe ProgramState::Resume */
int sre_ProgramState_Resume(void* self, int narg) {
    if (!self) return 0;
    lua_State* L = PS_GET(self, PS_LUA_STATE, lua_State*);
    if (!L || !g_lua_resume) return 0;

    sre13_ensure_injected(L);

    /* Capture the live game lua_State and ProgramState for the per-frame console tick */
    sre13_console_set_root_and_ps(L, self);

    /* Service the live console ONCE per frame (guard inside). */
    sre13_console_frame_tick(0.0f);

    if (__builtin_expect(g_sre_scene_shift_pending != 0 || g_sre_scene_shift_active != 0, 0)) {
        sre13_scene_shifter_tick();
    }

    CallInfo* saved_ci = L->ci;
    StkId saved_top = L->top;
    StkId saved_base = L->base;
    unsigned short saved_nCcalls = L->nCcalls;

    int my_depth = sre13_recovery_push(L);
    if (my_depth < 0) {
        int res;
        sre13_arm_timeout(L);
        res = g_lua_resume(L, narg);
        sre13_disarm_timeout(L);
        if (res != 0 && res != 1) {
            sre13_capture_error(L);
            if (g_lua_settop) g_lua_settop(L, -2);
        }
        return res;
    }

    if (sre_setjmp(g_sre_recovery_stack[my_depth].buf) != 0) {
        sre13_recovery_pop(my_depth);
        sre13_disarm_timeout(L);
        g_sre13_lua_error_count++;
        sre13_capture_error(L);
        sre13_restore_vm(L, saved_ci, saved_top, saved_base, saved_nCcalls, 0);
        return 2; /* LUA_ERRRUN */
    }

    int res;
    sre13_arm_timeout(L);
    res = g_lua_resume(L, narg);
    sre13_disarm_timeout(L);
    sre13_recovery_pop(my_depth);
    if (res != 0 && res != 1) {
        sre13_capture_error(L);
        if (g_lua_settop) g_lua_settop(L, -2);
    }
    return res;
}

/* Safe panic handler — logs and returns without std::terminate.
 * (sre_ProgramPanic in sre13_recovery.c is the engine's
 * Caver::ProgramPanic replacement — this separate hook is kept for the
 * lua_atpanic path where the engine registers a ProgramState panic fn.) */
void sre_ProgramState_ProgramPanic(void* L) {
    if (L && g_lua_tolstring) {
        const char* err = g_lua_tolstring((lua_State*)L, -1, NULL);
        (void)err;
    }
}