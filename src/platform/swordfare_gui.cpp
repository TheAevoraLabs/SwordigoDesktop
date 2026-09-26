// =============================================================================
// Swordfare GUI — Modern ImGui Overlay Implementation  (v7.3)
//
// Dear ImGui (SDL3 + OpenGL 3.3) overlay rendered on top of the running game.
// Manages its own ImGui context so it does not interfere with the launcher's
// context (which is already destroyed before the game boots).
//
// Design notes:
//   - Theme: dark with crimson accent (#e94560), matching the launcher.
//   - The debug window is pinned top-left at startup; user can drag it freely.
//   - All helper stubs for future panels (Controls, Camera, Mods) are marked
//     with TODO so they're easy to find.
// =============================================================================

#if defined(_WIN32) && !defined(__MINGW32__)
// Winsock must precede <windows.h>; swordfare_gui.h -> gl_inc.h pulls it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "platform/swordfare_gui.h"
#include "platform/IconsFontAwesome6.h"
#include "platform/embedded_assets.h"
#include "platform/swordfare_theme.h"
#include "platform/xpera/xpera_style.h"
#include "platform/xpera/xpera_gui.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#ifdef VULKAN_BACKEND
#define VK_NO_PROTOTYPES
#include "volk.h"
#include "platform/vulkan_backend.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#endif
#include "platform/rgc.h"
#include "platform/os_external.h"

#include <cstring>
#include "display.h"
#include "input_config.h"
#include "data_path.h"
#include "fbo_scaler.h"
#include "platform/launcher_config.h"
#include "platform/mod_manager.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#else
typedef SSIZE_T ssize_t;
#ifndef SHUT_RD
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif
/* This TU only ever reads/writes/closes sockets (free-function shims, so
   std::ofstream::close()/write() members are untouched). */
inline int close(SOCKET s)                       { return closesocket(s); }
inline int read(int s, void *b, size_t n)        { return recv((SOCKET)s, (char *)b, (int)n, 0); }
inline int write(int s, const void *b, size_t n) { return send((SOCKET)s, (const char *)b, (int)n, 0); }
#endif
#include <cmath>

bool g_sre_overlay_blocking = false;

// Guest heap size reporters (defined in jni_bridge_arm64.cpp / jni_bridge.cpp)
extern "C" uint32_t get_guest_heap_size_64();
extern "C" uint32_t get_guest_heap_size_32();

// -----------------------------------------------------------------------------
//  F3: SRE13 texture-identity telemetry  (additive, removable)
// -----------------------------------------------------------------------------
//  Declared here, not through jni_bridge.h, matching the two externs above.
//  Layout: [0] bind_calls [1] identified [2] unique_names
//          [3] trusted    [4] rejected   [5] level_generation
//  Delete this extern and the matching F3 block together.
extern "C" void sre13_tex_telemetry_snapshot(uint32_t out[6]);
// F3 diagnostic for the authored-emitter bloom key; defined in fbo_scaler.cpp.
// REMOVABLE together with the bloom block below and the function itself.
extern "C" int swordigo_bloom_emitter_report(char* out, int cap);


// Weak fallback definitions for SRE Scene Shifter symbols (resolved from guest space at runtime)
extern "C" {
    SWORDIGO_WEAK int    g_sre_scene_list_count = 0;
    SWORDIGO_WEAK char   g_sre_scene_list[256][128] = {{0}};
    SWORDIGO_WEAK volatile int   g_sre_scene_shift_pending = 0;
    SWORDIGO_WEAK char   g_sre_scene_shift_target[128] = {0};
    SWORDIGO_WEAK char   g_sre_scene_shift_spawn[64] = "start";
    SWORDIGO_WEAK char   g_sre_scene_shift_last_error[256] = {0};
    SWORDIGO_WEAK volatile int   g_sre_scene_shift_active = 0;
    SWORDIGO_WEAK char   g_sre_current_scene_name[128] = {0};
    SWORDIGO_WEAK void   sre_scene_shifter_scan_scenes(void) {}
    // AnimateIn hook trampoline pointer — set by host after relay install
    SWORDIGO_WEAK void*  g_orig_SceneLoadingView_AnimateIn = NULL;
}

// ---------------------------------------------------------------------------
// Helpers — no stdlib in SRE land, but this is host-side C++ so fine
// ---------------------------------------------------------------------------

static void swardfare_push_fps(float* history, int& idx, float value, int size) {
    history[idx] = value;
    idx = (idx + 1) % size;
}

// ---------------------------------------------------------------------------
// Theme — Swordfare Dark  (mirrors the launcher palette)
// ---------------------------------------------------------------------------

void SwordfareGUI::apply_swordfare_theme() {
    // Start from the shared design system so the in-game overlays (F1 mod
    // overlay, F3 debug/tools) share rounding, spacing, and the base palette
    // with the launcher, loading screen, and crash dialog. The Swordfare
    // crimson-accent overrides below then refine the look on top.
    sf_theme::ApplyTheme();
    ImGuiStyle& s = ImGui::GetStyle();

    // --- Geometry ---
    s.WindowRounding    = 10.0f;
    s.ChildRounding     =  8.0f;
    s.FrameRounding     =  6.0f;
    s.PopupRounding     =  8.0f;
    s.GrabRounding      =  4.0f;
    s.TabRounding       =  6.0f;
    s.ScrollbarRounding =  6.0f;

    s.WindowPadding     = ImVec2(16, 14);
    s.FramePadding      = ImVec2(10, 5);
    s.ItemSpacing       = ImVec2(10, 6);
    s.ItemInnerSpacing  = ImVec2(6,  4);
    s.IndentSpacing     = 16.0f;
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 10.0f;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.TabBorderSize     = 0.0f;

    // --- Colours ---
    ImVec4* c = s.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]          = ImVec4(0.04f,  0.05f,  0.07f,  0.92f);  // deep near-black, semi-transparent
    c[ImGuiCol_ChildBg]           = ImVec4(0.07f,  0.09f,  0.12f,  1.00f);  // #121720
    c[ImGuiCol_PopupBg]           = ImVec4(0.06f,  0.08f,  0.11f,  0.97f);
    c[ImGuiCol_FrameBg]           = ImVec4(0.10f,  0.13f,  0.18f,  1.00f);
    c[ImGuiCol_FrameBgHovered]    = ImVec4(0.13f,  0.17f,  0.24f,  1.00f);
    c[ImGuiCol_FrameBgActive]     = ImVec4(0.16f,  0.20f,  0.28f,  1.00f);

    // Title bar
    c[ImGuiCol_TitleBg]           = ImVec4(0.04f,  0.05f,  0.07f,  1.00f);
    c[ImGuiCol_TitleBgActive]     = ImVec4(0.07f,  0.08f,  0.10f,  1.00f);
    c[ImGuiCol_TitleBgCollapsed]  = ImVec4(0.04f,  0.05f,  0.07f,  0.70f);

    // Borders
    c[ImGuiCol_Border]            = ImVec4(0.914f, 0.271f, 0.376f, 0.25f);  // subtle crimson outline
    c[ImGuiCol_BorderShadow]      = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]       = ImVec4(0.04f,  0.05f,  0.07f,  0.40f);
    c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.20f,  0.23f,  0.27f,  0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.32f, 0.38f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.42f, 0.48f, 1.00f);

    // Accent — Swordfare Crimson (#e94560)
    ImVec4 accent      = ImVec4(0.914f, 0.271f, 0.376f, 1.00f);
    ImVec4 accent_dim  = ImVec4(0.914f, 0.271f, 0.376f, 0.50f);
    ImVec4 accent_pale = ImVec4(0.914f, 0.271f, 0.376f, 0.20f);

    c[ImGuiCol_CheckMark]         = accent;
    c[ImGuiCol_SliderGrab]        = ImVec4(0.35f,  0.65f,  1.00f,  0.80f);
    c[ImGuiCol_SliderGrabActive]  = ImVec4(0.35f,  0.65f,  1.00f,  1.00f);

    c[ImGuiCol_Button]            = accent;
    c[ImGuiCol_ButtonHovered]     = ImVec4(1.00f,  0.38f,  0.48f,  1.00f);
    c[ImGuiCol_ButtonActive]      = ImVec4(0.78f,  0.20f,  0.29f,  1.00f);

    c[ImGuiCol_Header]            = accent_pale;
    c[ImGuiCol_HeaderHovered]     = accent_dim;
    c[ImGuiCol_HeaderActive]      = accent;

    c[ImGuiCol_Separator]         = ImVec4(0.914f, 0.271f, 0.376f, 0.18f);
    c[ImGuiCol_SeparatorHovered]  = accent_dim;
    c[ImGuiCol_SeparatorActive]   = accent;

    c[ImGuiCol_ResizeGrip]        = accent_pale;
    c[ImGuiCol_ResizeGripHovered] = accent_dim;
    c[ImGuiCol_ResizeGripActive]  = accent;

    c[ImGuiCol_Tab]               = ImVec4(0.10f,  0.13f,  0.18f,  1.00f);
    c[ImGuiCol_TabHovered]        = accent_dim;
    c[ImGuiCol_TabSelected]       = ImVec4(0.914f, 0.271f, 0.376f, 0.80f);

    // Text
    c[ImGuiCol_Text]              = ImVec4(0.90f,  0.93f,  0.95f,  1.00f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.54f,  0.58f,  0.62f,  1.00f);

    // Table
    c[ImGuiCol_TableHeaderBg]     = ImVec4(0.10f,  0.13f,  0.18f,  1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.20f,  0.23f,  0.27f,  0.60f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.20f,  0.23f,  0.27f,  0.30f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.00f,  0.00f,  0.00f,  0.00f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f,  1.00f,  1.00f,  0.02f);

    // Plot
    c[ImGuiCol_PlotLines]         = ImVec4(0.35f,  0.65f,  1.00f,  1.00f);  // blue FPS graph
    c[ImGuiCol_PlotLinesHovered]  = ImVec4(0.914f, 0.271f, 0.376f, 1.00f);
    c[ImGuiCol_PlotHistogram]     = ImVec4(0.35f,  0.65f,  1.00f,  0.80f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.914f, 0.271f, 0.376f, 1.00f);

    // ── Xpera skin (opt-out via SWORDFARE_XPERA_UI=0) ─────────────────────
    // When enabled this fully overrides the Swordfare palette/metrics above.
    // The original theme is intentionally kept intact as the fallback.
    if (xpera::ui_enabled()) {
        xpera::apply_style();
    }
}

// ---------------------------------------------------------------------------
// init / shutdown
// ---------------------------------------------------------------------------

// =============================================================================
// Remaster texture helpers — load launcher UI artwork from the embedded assets
// (permanent: works even when deb/rpm packaging drops the launcher/ folder).
// =============================================================================
static GLuint swordfare_load_embedded_texture(const char* name, int* out_w = nullptr, int* out_h = nullptr) {
    const unsigned char* data = nullptr;
    size_t size = 0;
    if (!embedded_asset(name, &data, &size)) {
        const char* slash = strrchr(name, '/');
        if (slash) embedded_asset(slash + 1, &data, &size);
    }
    if (!data || size < 8) return 0;
    int w = 0, h = 0;
    unsigned char* px = nullptr;
    if (!asset_decode_image(data, size, &px, &w, &h)) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    asset_image_free(px);
    return tex;
}

void SwordfareGUI::init(SDL_Window* window, SDL_GLContext gl_ctx) {
    if (m_initialized) return;

    m_window  = window;
    m_gl_ctx  = gl_ctx;

    // Create a dedicated ImGui context — does not share state with the launcher
    m_imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;          // no imgui.ini on disk for the in-game overlay
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // game cursor is hidden

    // Compute DPI Scale
    float dpi_scale = 1.0f;
    int ww, wh, pw, ph;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    if (ww > 0 && pw > 0) {
        dpi_scale = (float)pw / (float)ww;
    }
    if (dpi_scale < 1.0f) dpi_scale = 1.0f;

    // Intelligent layout scaling relative to window size
    float layout_scale = 1.0f;
    if (wh >= 1440) {
        layout_scale = 1.5f;
    } else if (wh >= 1080) {
        layout_scale = 1.25f;
    } else {
        layout_scale = 1.0f;
    }

    // Find font files
    std::string main_font_path, button_font_path, fa_path;
    std::string home_dir = os_external::home_dir();

    std::vector<std::string> main_font_candidates = {
        // dist/ portable layout (standalone release — SWORDIGO_DATA_DIR)
        get_data_path("fonts/MegalopolisExtra-Regular.otf"),
        get_data_path("fonts/SpaceGrotesk-VariableFont_wght.ttf"),
        get_data_path("fonts/Inter-Regular.ttf"),
        "./data/fonts/MegalopolisExtra-Regular.otf",
        "./data/fonts/SpaceGrotesk-VariableFont_wght.ttf",
        "./data/fonts/Inter-Regular.ttf",
        "./fonts/MegalopolisExtra-Regular.otf",
        "./fonts/SpaceGrotesk-VariableFont_wght.ttf",
        "./fonts/Inter-Regular.ttf",
        "../fonts/MegalopolisExtra-Regular.otf",
        "../fonts/SpaceGrotesk-VariableFont_wght.ttf",
        "../fonts/Inter-Regular.ttf",
        // Dev paths
        "src/assets/fonts/MegalopolisExtra-Regular.otf",
        "src/assets/fonts/Redaction10-Regular.otf",
        "src/assets/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/Redaction10-Regular.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/Redaction10-Regular.otf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/src/assets/fonts/Redaction10-Regular.otf",
        "/usr/share/swordigo-desktop/src/assets/fonts/Inter-Regular.ttf"
    };
    if (!home_dir.empty()) {
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/Redaction10-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/MegalopolisExtra-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/Redaction10-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/Inter-Regular.ttf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop-launcher/src/assets/fonts/Redaction10-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop-launcher/src/assets/fonts/Inter-Regular.ttf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-launcher/src/assets/fonts/Redaction10-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/swordigo-launcher/src/assets/fonts/Inter-Regular.ttf");
        main_font_candidates.push_back(home_dir + "/.local/share/launcher/src/assets/fonts/Redaction10-Regular.otf");
        main_font_candidates.push_back(home_dir + "/.local/share/launcher/src/assets/fonts/Inter-Regular.ttf");
    }
    for (const auto& fp : main_font_candidates) {
        if (std::filesystem::exists(fp)) {
            main_font_path = fp;
            break;
        }
    }

    std::vector<std::string> button_font_candidates = {
        "src/assets/fonts/MegalopolisExtra-Regular.otf",
        "src/assets/fonts/Redaction10-Bold.otf",
        "src/assets/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/Redaction10-Bold.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/Redaction10-Bold.otf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/src/assets/fonts/Redaction10-Bold.otf",
        "/usr/share/swordigo-desktop/src/assets/fonts/Inter-Regular.ttf"
    };
    if (!home_dir.empty()) {
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/Redaction10-Bold.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/MegalopolisExtra-Regular.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/Redaction10-Bold.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/Inter-Regular.ttf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop-launcher/src/assets/fonts/Redaction10-Bold.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-desktop-launcher/src/assets/fonts/Inter-Regular.ttf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-launcher/src/assets/fonts/Redaction10-Bold.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/swordigo-launcher/src/assets/fonts/Inter-Regular.ttf");
        button_font_candidates.push_back(home_dir + "/.local/share/launcher/src/assets/fonts/Redaction10-Bold.otf");
        button_font_candidates.push_back(home_dir + "/.local/share/launcher/src/assets/fonts/Inter-Regular.ttf");
    }
    for (const auto& fp : button_font_candidates) {
        if (std::filesystem::exists(fp)) {
            button_font_path = fp;
            break;
        }
    }

    std::vector<std::string> fa_candidates = {
        // dist/ portable layout (standalone release — SWORDIGO_DATA_DIR)
        get_data_path("fonts/Font Awesome 7 Free-Solid-900.otf"),
        get_data_path("fonts/fa-solid-900.ttf"),
        "./data/fonts/Font Awesome 7 Free-Solid-900.otf",
        "./data/fonts/fa-solid-900.ttf",
        "./fonts/Font Awesome 7 Free-Solid-900.otf",
        "./fonts/fa-solid-900.ttf",
        "../fonts/Font Awesome 7 Free-Solid-900.otf",
        "../fonts/fa-solid-900.ttf",
        // Dev paths
        "src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
        "src/assets/fonts/fa-solid-900.ttf",
        "/usr/share/swordigo-desktop/launcher/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/fa-solid-900.ttf",
        "/usr/local/share/swordigo-desktop/launcher/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
        "/usr/local/share/swordigo-desktop/launcher/fonts/fa-solid-900.ttf",
        "/usr/share/swordigo-desktop/src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
        "/usr/share/swordigo-desktop/src/assets/fonts/fa-solid-900.ttf"
    };
    if (!home_dir.empty()) {
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/fa-solid-900.ttf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/fa-solid-900.ttf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-desktop-launcher/src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-desktop-launcher/src/assets/fonts/fa-solid-900.ttf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-launcher/src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf");
        fa_candidates.push_back(home_dir + "/.local/share/swordigo-launcher/src/assets/fonts/fa-solid-900.ttf");
        fa_candidates.push_back(home_dir + "/.local/share/launcher/src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf");
        fa_candidates.push_back(home_dir + "/.local/share/launcher/src/assets/fonts/fa-solid-900.ttf");
    }
    for (const auto& fp : fa_candidates) {
        if (std::filesystem::exists(fp)) {
            fa_path = fp;
            break;
        }
    }

    // ── Embedded fonts FIRST (permanent fix — baked into the binary) ──
    const unsigned char* emb_main = nullptr;    size_t emb_main_size = 0;
    const unsigned char* emb_button = nullptr;  size_t emb_button_size = 0;
    const unsigned char* emb_fa = nullptr;      size_t emb_fa_size = 0;
    const char* emb_main_names[] = {
        "fonts/MegalopolisExtra-Regular.otf", "fonts/Redaction10-Regular.otf",
        "fonts/Inter-Regular.ttf",
    };
    for (const char* n : emb_main_names)
        if (embedded_asset(n, &emb_main, &emb_main_size)) break;
    const char* emb_button_names[] = {
        "fonts/MegalopolisExtra-Regular.otf", "fonts/Redaction10-Bold.otf",
        "fonts/Inter-Regular.ttf",
    };
    for (const char* n : emb_button_names)
        if (embedded_asset(n, &emb_button, &emb_button_size)) break;
    if (!embedded_asset("fonts/fa-solid-900.ttf", &emb_fa, &emb_fa_size))
        embedded_asset("fonts/fa-solid-900.otf", &emb_fa, &emb_fa_size);

    if (emb_main && emb_main_size > 0) {
        float main_font_size = 14.0f * dpi_scale;
        // Embedded fonts are static .rodata — the atlas must NOT free() them.
        ImFontConfig emb_cfg;
        emb_cfg.FontDataOwnedByAtlas = false;
        m_font_main = io.Fonts->AddFontFromMemoryTTF((void*)emb_main, (int)emb_main_size,
                                                     main_font_size, &emb_cfg);

        // Merge FontAwesome into main font
        if (m_font_main && emb_fa && emb_fa_size > 0) {
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };
            ImFontConfig icon_cfg;
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = main_font_size;
            icon_cfg.GlyphOffset = ImVec2(0, 1);
            icon_cfg.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryTTF((void*)emb_fa, (int)emb_fa_size,
                                           main_font_size * 0.85f, &icon_cfg, icon_ranges);
        }
        std::cout << "[SwordfareGUI] Embedded main font loaded" << std::endl;
    } else if (!main_font_path.empty()) {
        float main_font_size = 14.0f * dpi_scale;
        m_font_main = io.Fonts->AddFontFromFileTTF(main_font_path.c_str(), main_font_size);

        // Merge FontAwesome into main font
        if (m_font_main && !fa_path.empty()) {
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };
            ImFontConfig icon_cfg;
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = main_font_size;
            icon_cfg.GlyphOffset = ImVec2(0, 1);
            io.Fonts->AddFontFromFileTTF(fa_path.c_str(), main_font_size * 0.85f, &icon_cfg, icon_ranges);
        }
    } else {
        m_font_main = io.Fonts->AddFontDefault();
    }

    if (emb_button && emb_button_size > 0) {
        float button_font_size = 20.0f * dpi_scale;
        ImFontConfig emb_btn_cfg;
        emb_btn_cfg.FontDataOwnedByAtlas = false;
        m_font_button = io.Fonts->AddFontFromMemoryTTF((void*)emb_button, (int)emb_button_size,
                                                       button_font_size, &emb_btn_cfg);

        // Merge FontAwesome into button font
        if (m_font_button && emb_fa && emb_fa_size > 0) {
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };
            ImFontConfig icon_cfg;
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = button_font_size;
            icon_cfg.GlyphOffset = ImVec2(0, 2);
            icon_cfg.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryTTF((void*)emb_fa, (int)emb_fa_size,
                                           button_font_size * 0.85f, &icon_cfg, icon_ranges);
        }
    } else if (!button_font_path.empty()) {
        float button_font_size = 20.0f * dpi_scale;
        m_font_button = io.Fonts->AddFontFromFileTTF(button_font_path.c_str(), button_font_size);

        // Merge FontAwesome into button font
        if (m_font_button && !fa_path.empty()) {
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };
            ImFontConfig icon_cfg;
            icon_cfg.MergeMode = true;
            icon_cfg.PixelSnapH = true;
            icon_cfg.GlyphMinAdvanceX = button_font_size;
            icon_cfg.GlyphOffset = ImVec2(0, 2);
            io.Fonts->AddFontFromFileTTF(fa_path.c_str(), button_font_size * 0.85f, &icon_cfg, icon_ranges);
        }
    } else {
        m_font_button = io.Fonts->AddFontDefault();
    }

    // ── Tool typography: a real UI face + a real monospace face ─────────────
    //
    // m_font_mono was DECLARED but never assigned, so every `if (m_font_mono)
    // PushFont(...)` in the codebase was dead code and the console fell back to
    // m_font_main — which is Megalopolis Extra, Swordigo's display lettering
    // font.  A research tool rendering addresses and hex in a comic font is not
    // just ugly: fixed-width alignment is a correctness property for a hex dump.
    //
    // Space Grotesk for chrome and labels, JetBrains Mono for anything numeric.
    {
        // Local picker: this path does not have the find_font() helper the
        // Vulkan loader defines later in its own scope.
        auto pick_font = [](const char* const* names, size_t count) -> std::string {
            for (size_t i = 0; i < count; ++i)
                if (std::filesystem::exists(names[i])) return names[i];
            return {};
        };
        const char* ui_candidates[] = {
            "src/assets/fonts/SpaceGrotesk-VariableFont_wght.ttf",
            "src/assets/fonts/static/SpaceGrotesk-Regular.ttf",
            "src/assets/fonts/Inter-Regular.ttf",
            "/usr/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf"
        };
        const char* mono_candidates[] = {
            "src/assets/fonts/JetBrainsMono-VariableFont_wght.ttf",
            "src/assets/fonts/static/JetBrainsMono-Regular.ttf",
            "/usr/share/swordigo-desktop/launcher/fonts/JetBrainsMono-Regular.ttf"
        };
        std::string ui_font_path   = pick_font(ui_candidates,   4);
        std::string mono_font_path = pick_font(mono_candidates, 3);
        const unsigned char* emb_ui = nullptr;    size_t emb_ui_size = 0;
        const unsigned char* emb_mono = nullptr;  size_t emb_mono_size = 0;
        for (const char* n : { "fonts/SpaceGrotesk-VariableFont_wght.ttf",
                               "fonts/Inter-Regular.ttf" })
            if (embedded_asset(n, &emb_ui, &emb_ui_size)) break;
        if (!embedded_asset("fonts/JetBrainsMono-VariableFont_wght.ttf", &emb_mono, &emb_mono_size))
            embedded_asset("fonts/JetBrainsMono-Italic-VariableFont_wght.ttf", &emb_mono, &emb_mono_size);

        const float ui_sz   = 15.0f * dpi_scale;
        const float mono_sz = 14.0f * dpi_scale;
        ImFontConfig emb_ui_cfg;   emb_ui_cfg.FontDataOwnedByAtlas = false;
        ImFontConfig emb_mono_cfg; emb_mono_cfg.FontDataOwnedByAtlas = false;
        auto merge_fa_into = [&](float size) {
            static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };
            ImFontConfig cfg;
            cfg.MergeMode = true; cfg.PixelSnapH = true;
            cfg.GlyphMinAdvanceX = size; cfg.GlyphOffset = ImVec2(0, 1);
            cfg.FontDataOwnedByAtlas = false;
            if (emb_fa && emb_fa_size > 0)
                io.Fonts->AddFontFromMemoryTTF((void*)emb_fa, (int)emb_fa_size,
                                               size * 0.85f, &cfg, icon_ranges);
            else if (!fa_path.empty())
                io.Fonts->AddFontFromFileTTF(fa_path.c_str(), size * 0.85f, &cfg, icon_ranges);
        };

        m_font_ui = (emb_ui && emb_ui_size > 0)
            ? io.Fonts->AddFontFromMemoryTTF((void*)emb_ui, (int)emb_ui_size, ui_sz, &emb_ui_cfg)
            : (!ui_font_path.empty()
                   ? io.Fonts->AddFontFromFileTTF(ui_font_path.c_str(), ui_sz)
                   : io.Fonts->AddFontDefault());
        if (m_font_ui) merge_fa_into(ui_sz);

        m_font_mono = (emb_mono && emb_mono_size > 0)
            ? io.Fonts->AddFontFromMemoryTTF((void*)emb_mono, (int)emb_mono_size, mono_sz, &emb_mono_cfg)
            : (!mono_font_path.empty()
                   ? io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), mono_sz)
                   : io.Fonts->AddFontDefault());
        if (m_font_mono) merge_fa_into(mono_sz);

        std::cout << "[SwordfareGUI] Tool fonts: ui=" << (m_font_ui ? "ok" : "default")
                  << " mono=" << (m_font_mono ? "ok" : "default") << std::endl;
    }

    apply_swordfare_theme();

    // Scale ImGui styles according to layout scale rather than full physical DPI scale
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(layout_scale);

    // Apply global font scale to make rasterized high-res fonts display at the correct logical size
    io.FontGlobalScale = layout_scale / dpi_scale;

    ImGui_ImplSDL3_InitForOpenGL(window, gl_ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // ── Remaster: load launcher artwork from embedded assets (GL path only) ──
    m_tex_overlay_bg = swordfare_load_embedded_texture("launcher_bg.png",
                                                        &m_tex_overlay_bg_w, &m_tex_overlay_bg_h);
    if (!m_tex_overlay_bg)
        m_tex_overlay_bg = swordfare_load_embedded_texture("ui_panel.png",
                                                           &m_tex_overlay_bg_w, &m_tex_overlay_bg_h);
    m_tex_swordigo_icon = swordfare_load_embedded_texture("icons/swordigo_default.png",
                                                          &m_tex_swordigo_icon_w, &m_tex_swordigo_icon_h);
    if (!m_tex_swordigo_icon)
        m_tex_swordigo_icon = swordfare_load_embedded_texture("icon_app.png",
                                                              &m_tex_swordigo_icon_w, &m_tex_swordigo_icon_h);
    if (m_tex_overlay_bg)
        std::cout << "[SwordfareGUI] Remaster background texture loaded ("
                  << m_tex_overlay_bg_w << "x" << m_tex_overlay_bg_h << ")" << std::endl;
    if (m_tex_swordigo_icon)
        std::cout << "[SwordfareGUI] Remaster icon texture loaded ("
                  << m_tex_swordigo_icon_w << "x" << m_tex_swordigo_icon_h << ")" << std::endl;

    // ── Remaster: game UI artwork (buttons, panels, item icons) ──
    m_tex_btn_wide = swordfare_load_embedded_texture("icons/ui_button_wide.png",
                                                     &m_tex_btn_wide_w, &m_tex_btn_wide_h);
    m_tex_panel    = swordfare_load_embedded_texture("icons/ui_panel.png",
                                                     &m_tex_panel_w, &m_tex_panel_h);
    const char* item_names[4] = {
        "icons/game/item_brasssword.png",
        "icons/game/item_firetrinket.png",
        "icons/game/item_icetrinket.png",
        "icons/game/overlayitem_healingpotion.png",
    };
    for (int i = 0; i < 4; i++)
        m_tex_items[i] = swordfare_load_embedded_texture(item_names[i],
                                                         &m_tex_items_w[i], &m_tex_items_h[i]);
    if (m_tex_btn_wide || m_tex_panel || m_tex_items[0])
        std::cout << "[SwordfareGUI] Remaster game-art textures loaded (button/panel/items)" << std::endl;

    m_initialized = true;
}

// ---------------------------------------------------------------------------
// init_vulkan — same setup as init() but wires the Dear ImGui Vulkan backend
// ---------------------------------------------------------------------------
#ifdef VULKAN_BACKEND
void SwordfareGUI::init_vulkan(SDL_Window* window, VulkanBackend* vk_backend) {
    if (m_initialized) return;

    m_window        = window;
    m_vk_backend    = vk_backend;
    m_vulkan_active = true;

    // Dedicated ImGui context — does not share state with the launcher
    m_imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // DPI / layout scale (identical logic to init)
    float dpi_scale = 1.0f;
    int ww, wh, pw, ph;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    if (ww > 0 && pw > 0) dpi_scale = (float)pw / (float)ww;
    if (dpi_scale < 1.0f) dpi_scale = 1.0f;

    float layout_scale = (wh >= 1440) ? 1.5f : (wh >= 1080) ? 1.25f : 1.0f;

    // Font candidates — same search paths as init()
    std::string home_dir = os_external::home_dir();

    auto find_font = [&](std::vector<std::string> candidates) -> std::string {
        if (!home_dir.empty()) {
            candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf");
            candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/MegalopolisExtra-Regular.otf");
            candidates.push_back(home_dir + "/.local/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf");
            candidates.push_back(home_dir + "/.local/share/swordigo-desktop/src/assets/fonts/Inter-Regular.ttf");
        }
        for (const auto& fp : candidates)
            if (std::filesystem::exists(fp)) return fp;
        return {};
    };

    std::string main_font_path = find_font({
        "src/assets/fonts/MegalopolisExtra-Regular.otf",
        "src/assets/fonts/Redaction10-Regular.otf",
        "src/assets/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf"
    });
    std::string button_font_path = find_font({
        "src/assets/fonts/MegalopolisExtra-Regular.otf",
        "src/assets/fonts/Redaction10-Bold.otf",
        "src/assets/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/launcher/fonts/MegalopolisExtra-Regular.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf"
    });
    std::string fa_path = find_font({
        "src/assets/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
        "src/assets/fonts/fa-solid-900.ttf",
        "/usr/share/swordigo-desktop/launcher/fontawesome/otfs/Font Awesome 7 Free-Solid-900.otf",
        "/usr/share/swordigo-desktop/launcher/fonts/fa-solid-900.ttf"
    });

    // ── Embedded fonts FIRST (permanent fix) ──
    const unsigned char* emb_main = nullptr;    size_t emb_main_size = 0;
    const unsigned char* emb_button = nullptr;  size_t emb_button_size = 0;
    const unsigned char* emb_fa = nullptr;      size_t emb_fa_size = 0;
    const char* emb_main_names[] = {
        "fonts/MegalopolisExtra-Regular.otf", "fonts/Redaction10-Regular.otf",
        "fonts/Inter-Regular.ttf",
    };
    for (const char* n : emb_main_names)
        if (embedded_asset(n, &emb_main, &emb_main_size)) break;
    const char* emb_button_names[] = {
        "fonts/MegalopolisExtra-Regular.otf", "fonts/Redaction10-Bold.otf",
        "fonts/Inter-Regular.ttf",
    };
    for (const char* n : emb_button_names)
        if (embedded_asset(n, &emb_button, &emb_button_size)) break;
    if (!embedded_asset("fonts/fa-solid-900.ttf", &emb_fa, &emb_fa_size))
        embedded_asset("fonts/fa-solid-900.otf", &emb_fa, &emb_fa_size);

    // Load fonts (same logic as init — embedded first, disk fallback)
    // Embedded fonts are static .rodata — the atlas must never free() them.
    ImFontConfig emb_main_cfg;
    emb_main_cfg.FontDataOwnedByAtlas = false;
    ImFontConfig emb_btn_cfg;
    emb_btn_cfg.FontDataOwnedByAtlas = false;
    auto merge_fa = [&](float size) {
        static const ImWchar icon_ranges[] = { ICON_FA_MIN, ICON_FA_MAX, 0 };
        ImFontConfig cfg;
        cfg.MergeMode = true; cfg.PixelSnapH = true;
        cfg.GlyphMinAdvanceX = size; cfg.GlyphOffset = ImVec2(0, 1);
        cfg.FontDataOwnedByAtlas = false;
        if (emb_fa && emb_fa_size > 0)
            io.Fonts->AddFontFromMemoryTTF((void*)emb_fa, (int)emb_fa_size, size * 0.85f, &cfg, icon_ranges);
        else if (!fa_path.empty())
            io.Fonts->AddFontFromFileTTF(fa_path.c_str(), size * 0.85f, &cfg, icon_ranges);
    };

    float main_sz   = 14.0f * dpi_scale;
    float button_sz = 20.0f * dpi_scale;
    m_font_main   = (emb_main && emb_main_size > 0)
                        ? io.Fonts->AddFontFromMemoryTTF((void*)emb_main, (int)emb_main_size, main_sz, &emb_main_cfg)
                        : (!main_font_path.empty()
                            ? io.Fonts->AddFontFromFileTTF(main_font_path.c_str(), main_sz)
                            : io.Fonts->AddFontDefault());
    if (m_font_main) merge_fa(main_sz);
    m_font_button = (emb_button && emb_button_size > 0)
                        ? io.Fonts->AddFontFromMemoryTTF((void*)emb_button, (int)emb_button_size, button_sz, &emb_btn_cfg)
                        : (!button_font_path.empty()
                            ? io.Fonts->AddFontFromFileTTF(button_font_path.c_str(), button_sz)
                            : io.Fonts->AddFontDefault());
    if (m_font_button) merge_fa(button_sz);

    // ── Tool typography (same rationale as the GL path above) ───────────────
    // Kept in step with the first loader: a font slot that is only populated on
    // one of two paths is how m_font_mono ended up permanently null.
    std::string ui_font_path = find_font({
        "src/assets/fonts/SpaceGrotesk-VariableFont_wght.ttf",
        "src/assets/fonts/static/SpaceGrotesk-Regular.ttf",
        "src/assets/fonts/Inter-Regular.ttf",
        "/usr/share/swordigo-desktop/launcher/fonts/Inter-Regular.ttf"
    });
    std::string mono_font_path = find_font({
        "src/assets/fonts/JetBrainsMono-VariableFont_wght.ttf",
        "src/assets/fonts/static/JetBrainsMono-Regular.ttf",
        "/usr/share/swordigo-desktop/launcher/fonts/JetBrainsMono-Regular.ttf"
    });
    const unsigned char* emb_ui = nullptr;    size_t emb_ui_size = 0;
    const unsigned char* emb_mono = nullptr;  size_t emb_mono_size = 0;
    for (const char* n : { "fonts/SpaceGrotesk-VariableFont_wght.ttf",
                           "fonts/Inter-Regular.ttf" })
        if (embedded_asset(n, &emb_ui, &emb_ui_size)) break;
    if (!embedded_asset("fonts/JetBrainsMono-VariableFont_wght.ttf", &emb_mono, &emb_mono_size))
        embedded_asset("fonts/JetBrainsMono-Italic-VariableFont_wght.ttf", &emb_mono, &emb_mono_size);
    {
        const float ui_sz   = 15.0f * dpi_scale;
        const float mono_sz = 14.0f * dpi_scale;
        ImFontConfig emb_ui_cfg;   emb_ui_cfg.FontDataOwnedByAtlas = false;
        ImFontConfig emb_mono_cfg; emb_mono_cfg.FontDataOwnedByAtlas = false;
        m_font_ui = (emb_ui && emb_ui_size > 0)
            ? io.Fonts->AddFontFromMemoryTTF((void*)emb_ui, (int)emb_ui_size, ui_sz, &emb_ui_cfg)
            : (!ui_font_path.empty()
                   ? io.Fonts->AddFontFromFileTTF(ui_font_path.c_str(), ui_sz)
                   : io.Fonts->AddFontDefault());
        if (m_font_ui) merge_fa(ui_sz);
        m_font_mono = (emb_mono && emb_mono_size > 0)
            ? io.Fonts->AddFontFromMemoryTTF((void*)emb_mono, (int)emb_mono_size, mono_sz, &emb_mono_cfg)
            : (!mono_font_path.empty()
                   ? io.Fonts->AddFontFromFileTTF(mono_font_path.c_str(), mono_sz)
                   : io.Fonts->AddFontDefault());
        if (m_font_mono) merge_fa(mono_sz);
        std::cout << "[SwordfareGUI] Tool fonts: ui=" << (m_font_ui ? "ok" : "default")
                  << " mono=" << (m_font_mono ? "ok" : "default") << std::endl;
    }

    apply_swordfare_theme();
    ImGui::GetStyle().ScaleAllSizes(layout_scale);
    io.FontGlobalScale = layout_scale / dpi_scale;

    // SDL3 Vulkan platform layer
    ImGui_ImplSDL3_InitForVulkan(window);

    // Initialize ImGui Vulkan functions with volk
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, [](const char* function_name, void* user_data) {
        return vkGetInstanceProcAddr((VkInstance)user_data, function_name);
    }, vk_backend->get_instance());

    // Create ImGui-specific descriptor pool
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets       = 1000 * 11;
    pool_info.poolSizeCount = 11;
    pool_info.pPoolSizes    = pool_sizes;
    if (vkCreateDescriptorPool(vk_backend->get_device(), &pool_info, nullptr,
                               &m_imgui_vk_descriptor_pool) != VK_SUCCESS) {
        fprintf(stderr, "[SwordfareGUI] Failed to create ImGui Vulkan descriptor pool\n");
    }

    // Wire ImGui Vulkan backend
    ImGui_ImplVulkan_InitInfo vk_init = {};
    vk_init.ApiVersion                         = VK_API_VERSION_1_0;
    vk_init.Instance                           = vk_backend->get_instance();
    vk_init.PhysicalDevice                     = vk_backend->get_physical_device();
    vk_init.Device                             = vk_backend->get_device();
    vk_init.QueueFamily                        = vk_backend->get_graphics_family();
    vk_init.Queue                              = vk_backend->get_graphics_queue();
    vk_init.DescriptorPool                     = m_imgui_vk_descriptor_pool;
    vk_init.MinImageCount                      = vk_backend->get_image_count();
    vk_init.ImageCount                         = vk_backend->get_image_count();
    vk_init.PipelineCache                      = vk_backend->get_pipeline_cache();
    vk_init.PipelineInfoMain.RenderPass        = vk_backend->get_render_pass();
    vk_init.PipelineInfoMain.MSAASamples       = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&vk_init);

    m_initialized = true;
}
#else
void SwordfareGUI::init_vulkan(SDL_Window* window, class VulkanBackend*) {
    // Vulkan backend not compiled — fall back to OpenGL
    fprintf(stderr, "[SwordfareGUI] init_vulkan called but VULKAN_BACKEND not compiled — no-op\n");
    (void)window;
}
#endif

void SwordfareGUI::shutdown() {
    if (!m_initialized) return;
    stop_tcp_server();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));
#ifdef VULKAN_BACKEND
    if (m_vulkan_active) {
        ImGui_ImplVulkan_Shutdown();
        if (m_imgui_vk_descriptor_pool != VK_NULL_HANDLE && m_vk_backend) {
            vkDestroyDescriptorPool(m_vk_backend->get_device(), m_imgui_vk_descriptor_pool, nullptr);
            m_imgui_vk_descriptor_pool = VK_NULL_HANDLE;
        }
    } else
#endif
    {
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    // Release remaster textures (GL path only)
    if (m_tex_overlay_bg)    { glDeleteTextures(1, &m_tex_overlay_bg);    m_tex_overlay_bg = 0; }
    if (m_tex_swordigo_icon) { glDeleteTextures(1, &m_tex_swordigo_icon); m_tex_swordigo_icon = 0; }
    if (m_tex_btn_wide)      { glDeleteTextures(1, &m_tex_btn_wide);      m_tex_btn_wide = 0; }
    if (m_tex_panel)         { glDeleteTextures(1, &m_tex_panel);         m_tex_panel = 0; }
    for (int i = 0; i < 4; i++)
        if (m_tex_items[i])  { glDeleteTextures(1, &m_tex_items[i]);      m_tex_items[i] = 0; }

    m_imgui_ctx   = nullptr;
    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Event processing
// ---------------------------------------------------------------------------

bool SwordfareGUI::process_event(const SDL_Event& event) {
    if (!m_initialized) return false;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));
    ImGui_ImplSDL3_ProcessEvent(&event);
    
    ImGuiIO& io = ImGui::GetIO();

    // Global overlays must remain toggleable even while ImGui captures input.
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == SDLK_F11) {
        toggle_scene_toolbox();
        return true;
    }

    // [Insert] opens the standalone Memory Research console (moved out of the
    // F11 toolbox). [Tab] cannot be used here: it is the game's "Open" virtual
    // key and is also forwarded to the guest FWKeyboard, so it stays reserved.
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.key == SDLK_INSERT) {
        toggle_research_overlay();
        return true;
    }
    // Escape closes the research console, but not while a text field inside it
    // has keyboard focus (otherwise it would swallow typed input).
    if (m_research_overlay_visible && event.type == SDL_EVENT_KEY_DOWN &&
        !event.key.repeat && event.key.key == SDLK_ESCAPE &&
        !ImGui::GetIO().WantTextInput) {
        m_research_overlay_visible = false;
        return true;
    }

    // Function keys remain application-level shortcuts even while a menu has
    // keyboard focus, so every overlay can always be closed with its hotkey.
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
        event.key.scancode >= SDL_SCANCODE_F1 &&
        event.key.scancode <= SDL_SCANCODE_F12) {
        return false;
    }

    // Capture keyboard input for every visible ImGui surface, not only debug.
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        return (m_visible || m_mod_overlay_visible || m_console_open) && io.WantCaptureKeyboard;

    // For mouse events: block if ImGui wants it OR if the click lands on any
    // SRE overlay or button area (even when the mod-menu is hidden)
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (io.WantCaptureMouse) return true;
        if (event.button.button == SDL_BUTTON_LEFT) {
            float ex = (float)event.button.x;
            float ey = (float)event.button.y;
            if (is_input_blocked(ex, ey)) return true;
        }
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// begin_frame / end_frame
// ---------------------------------------------------------------------------

void SwordfareGUI::update_console_backend() {
    if (!m_console_ready || !m_guest_memory) return;

    // ---- Guest console IPC: DELIVER first, DISPATCH second, never both ----
    //
    // The guest console is a SINGLE-SLOT, cross-thread mailbox:
    //   host: writes buf, sets status=0 then pending=1   (submit)
    //   guest: sees pending==1, runs it, writes result, sets status(1|2),
    //          clears pending=0                            (complete)
    //   host: sees status!=0, reads result, sets status=0 (consume)
    //
    // The original rewrite dispatched a NEW command at the TOP of this tick and
    // read the result at the BOTTOM of the SAME tick. Because console_submit()
    // unconditionally writes status=0, a dispatch could race ahead and WIPE a
    // completion the host had not consumed yet — so most results were silently
    // dropped and the survivors appeared one command late (the desync + the
    // ">>"-flood we observed on the wire).
    //
    // Host-only fix (no guest/Raijin change): make DELIVERY and DISPATCH
    // mutually exclusive per tick, with delivery ALWAYS winning. We only ever
    // submit the next chunk on a tick where the slot is fully idle
    // (pending==0 && status==0) AND we did not just consume a result. That
    // closes the check-then-act window: a pending completion is always read
    // and cleared on its own tick before the slot can be reused.
    bool delivered_this_tick = false;

    // ---- Unified SRE command result polling (Local GUI + TCP Console) ----
    int32_t status = *(int32_t*)(m_guest_memory + m_console_status_addr);
    if (status != 0) {
        const char* result = (const char*)(m_guest_memory + m_console_result_addr);
        std::string res_str = (result && result[0]) ? result : "";

        // Feed to GUI console history & terminal log
        if (!res_str.empty()) {
            m_console_history.push_back({res_str, (status == 2), false});
            while ((int)m_console_history.size() > CONSOLE_MAX_HISTORY)
                m_console_history.erase(m_console_history.begin());
            m_console_scroll_bottom = true;

            // Print output to main terminal for easy copying/logging
            if (status == 2) {
                std::cout << "[SRE-Lua Error] " << res_str << std::endl;
            } else {
                std::cout << "[SRE-Lua Output]\n" << res_str << std::endl;
            }
        }

        // If the command was sent by TCP, send the result back to the client
        // that OWNS it (m_tcp_in_flight_fd), not whatever client happens to be
        // connected now — a slow result must never leak to a newer client.
        // Then clear the in-flight flag and wake the reader thread (which may
        // be blocked in enqueue_wait keeping output strictly ordered).
        bool was_in_flight;
        int  owner_fd;
        uint64_t owner_gen;
        uint64_t cur_gen;
        {
            std::lock_guard<std::mutex> lk(m_tcp_mutex);
            was_in_flight = m_tcp_cmd_in_flight;
            owner_fd      = m_tcp_in_flight_fd;
            owner_gen     = m_tcp_in_flight_gen;
            cur_gen       = m_tcp_client_gen;
        }

        if (m_tcp_running && was_in_flight) {
            // Only deliver if the owning client is still the connected one:
            // BOTH the fd matches AND the connection generation matches, so a
            // result computed for a client that has since disconnected (and had
            // its fd possibly recycled by a new client) is never written to the
            // wrong client.
            if (owner_fd >= 0 && owner_fd == m_tcp_client_fd.load() &&
                owner_gen == cur_gen) {
                std::string output;
                if (status == 2) {
                    // Output error in red
                    output = "\033[1;31mError: " + res_str + "\033[0m\n\033[1;36mraijin-sdk\033[0m> ";
                } else {
                    // Normal execution output
                    output = res_str.empty() ? "\033[1;36mraijin-sdk\033[0m> " : (res_str + "\n\033[1;36mraijin-sdk\033[0m> ");
                }
                write(owner_fd, output.c_str(), output.size());
            }

            {
                std::lock_guard<std::mutex> lk(m_tcp_mutex);
                m_tcp_cmd_in_flight = false;
                m_tcp_in_flight_fd  = -1;
                m_tcp_completed_seq++;
            }
            m_tcp_done_cv.notify_all();
        }

        *(int32_t*)(m_guest_memory + m_console_status_addr) = 0;
        delivered_this_tick = true;
    }

    // ---- Dispatch the next queued TCP chunk (ONLY if we did not just consume
    //      a result this tick). Popping and submitting are done under the same
    //      lock, and we re-check the guest slot is fully idle right before
    //      console_submit() so we can never clobber an unread completion. This
    //      strict deliver-then-dispatch ordering is the host-only fix for the
    //      dropped-result / one-behind desync seen on the wire. ----
    if (m_tcp_running && !delivered_this_tick) {
        std::unique_lock<std::mutex> lk(m_tcp_mutex);
        if (!m_tcp_cmd_in_flight && !m_tcp_cmd_queue.empty()) {
            int32_t pending = *(int32_t*)(m_guest_memory + m_console_pending_addr);
            int32_t st      = *(int32_t*)(m_guest_memory + m_console_status_addr);
            if (pending == 0 && st == 0) {
                TcpCmd item = m_tcp_cmd_queue.front();
                m_tcp_cmd_queue.pop_front();
                m_tcp_cmd_in_flight = true;
                m_tcp_in_flight_fd  = item.client_fd;
                m_tcp_in_flight_gen = item.gen;
                lk.unlock();
                console_submit(item.code);
            }
        }
    }
}

void SwordfareGUI::begin_frame() {
    if (!m_initialized) return;

    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));
#ifdef VULKAN_BACKEND
    if (m_vulkan_active) {
        ImGui_ImplVulkan_NewFrame();
    } else
#endif
    {
        ImGui_ImplOpenGL3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void SwordfareGUI::end_frame() {
    if (!m_initialized) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));
    ImGui::Render();
#ifdef VULKAN_BACKEND
    if (m_vulkan_active && m_vk_backend) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_vk_backend->get_current_command_buffer());
    } else
#endif
    {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

// ---------------------------------------------------------------------------
// draw_debug — the F3 debug window
// ---------------------------------------------------------------------------

void SwordfareGUI::draw_debug(const SwordfareDebugStats& st) {
    // -- Update FPS ring buffer --
    swardfare_push_fps(m_fps_history, m_fps_idx, st.fps, FPS_HISTORY);

    ImGuiIO& io = ImGui::GetIO();
    float screen_w = io.DisplaySize.x;
    float screen_h = io.DisplaySize.y;

    // Unbound fullscreen overlay (no rigid borders, no clamping box, completely transparent canvas)
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(screen_w, screen_h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoScrollbar           |
        ImGuiWindowFlags_NoScrollWithMouse     |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoNav                 |
        ImGuiWindowFlags_NoSavedSettings       |
        ImGuiWindowFlags_NoFocusOnAppearing    |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        // THIS OVERLAY IS FULL-SCREEN AND READ-ONLY, SO IT MUST NOT WANT INPUT.
        // Without NoInputs ImGui sets io.WantCaptureMouse for the whole screen
        // while F3 is up, and main.cpp's click router feeds the mouse to the HUD
        // instead of the game - which reads as "the game is unclickable with F3
        // open". NoInputs also removes it from keyboard capture, which is what a
        // read-only debug readout wants.
        ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("##MinecraftF3DebugOverlay", nullptr, wflags)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float font_size = ImGui::GetFontSize();
    float line_height = font_size + 3.0f;

    // Subtle top accent band so the HUD reads as a designed surface, not raw text.
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen_w, 24.0f),
                                IM_COL32(20, 40, 60, 110), IM_COL32(50, 22, 34, 110),
                                IM_COL32(50, 22, 34, 0),   IM_COL32(20, 40, 60, 0));

    // Helper to draw a left-aligned line: rounded dark badge + cyan accent stripe.
    float left_x = 10.0f;
    float left_y = 10.0f;
    auto draw_left = [&](const char* text, ImU32 text_col = IM_COL32(224, 224, 224, 255)) {
        if (!text || !text[0]) {
            left_y += 5.0f;
            return;
        }
        ImVec2 tsz = ImGui::CalcTextSize(text);
        dl->AddRectFilled(
            ImVec2(left_x - 5.0f, left_y - 1.0f),
            ImVec2(left_x + tsz.x + 5.0f, left_y + tsz.y + 1.0f),
            IM_COL32(12, 14, 18, 170), 3.0f
        );
        dl->AddRectFilled(
            ImVec2(left_x - 5.0f, left_y - 1.0f),
            ImVec2(left_x - 3.0f, left_y + tsz.y + 1.0f),
            IM_COL32(0, 170, 220, 170), 1.0f
        );
        dl->AddText(ImVec2(left_x + 1.0f, left_y + 1.0f), IM_COL32(20, 20, 20, 220), text);
        dl->AddText(ImVec2(left_x, left_y), text_col, text);
        left_y += line_height;
    };

    // Helper to draw a Minecraft-style right-aligned line with semi-transparent badge & text shadow
    float right_margin = 10.0f;
    float right_y = 10.0f;
    auto draw_right = [&](const char* text, ImU32 text_col = IM_COL32(224, 224, 224, 255)) {
        if (!text || !text[0]) {
            right_y += 5.0f;
            return;
        }
        ImVec2 tsz = ImGui::CalcTextSize(text);
        float rx = screen_w - right_margin - tsz.x;
        dl->AddRectFilled(
            ImVec2(rx - 5.0f, right_y - 1.0f),
            ImVec2(rx + tsz.x + 5.0f, right_y + tsz.y + 1.0f),
            IM_COL32(12, 14, 18, 170), 3.0f
        );
        dl->AddRectFilled(
            ImVec2(rx + tsz.x + 3.0f, right_y - 1.0f),
            ImVec2(rx + tsz.x + 5.0f, right_y + tsz.y + 1.0f),
            IM_COL32(139, 61, 255, 170), 1.0f
        );
        dl->AddText(ImVec2(rx + 1.0f, right_y + 1.0f), IM_COL32(20, 20, 20, 220), text);
        dl->AddText(ImVec2(rx, right_y), text_col, text);
        right_y += line_height;
    };

    char buf[512];

    // ── LEFT SIDE (Minecraft F3 style) ───────────────────────────────────────
    // 1. Version header
    bool is_v13 = (strstr(st.binary_name, "1.4.13") != nullptr);
    snprintf(buf, sizeof(buf), "Swordigo %s (%s / x86_64 host)",
             (is_v13 ? "1.4.13" : "1.4.12"), "swordfare_boot");
    draw_left(buf, IM_COL32(255, 255, 255, 255));

    // 2. FPS line (coloured by performance: green >= 55, amber >= 30, red < 30)
    ImU32 fps_col = (st.fps >= 55.0f) ? IM_COL32(85, 255, 85, 255)
                  : (st.fps >= 30.0f) ? IM_COL32(255, 200, 50, 255)
                                      : IM_COL32(255, 85, 85, 255);
    snprintf(buf, sizeof(buf), "%.1f fps (%.2f ms) T: 60 vsync, speed: %s%s",
             st.fps, (st.fps > 0.0f ? 1000.0f / st.fps : 0.0f),
             st.speed_label, st.game_paused ? " [PAUSED]" : "");
    draw_left(buf, fps_col);

    // 3. Engine & Runtime details
    snprintf(buf, sizeof(buf), "Engine: ARM64 Dynarmic JIT | RenderGuard: 0x0 - 0x3000000");
    draw_left(buf, IM_COL32(210, 210, 210, 255));

    snprintf(buf, sizeof(buf), "Runtime: %s @ 0x2000000 | ABI %d | API: %s",
             (is_v13 ? "libsre13.so" : "libsre12.so"),
             (is_v13 ? 13 : 12),
             st.graphics_api);
    draw_left(buf, IM_COL32(210, 210, 210, 255));

    draw_left(""); // spacer

    // 4. LOADED MOD INFOS (user-requested)
    std::string active_mod = get_active_mod_name();
    if (!active_mod.empty()) {
        snprintf(buf, sizeof(buf), "[Active Mod] %s", active_mod.c_str());
        draw_left(buf, IM_COL32(255, 170, 0, 255)); // Gold / amber header

        std::string mods_dir = get_user_data_dir() + "mods";
        auto mods = modman::list_mods(mods_dir);
        const modman::ModMeta* meta = nullptr;
        for (const auto& m : mods) {
            if (m.id == active_mod) {
                meta = &m;
                break;
            }
        }
        if (meta) {
            snprintf(buf, sizeof(buf), "  Name: %s (v%s)",
                     meta->name.c_str(),
                     meta->version.empty() ? "1.0" : meta->version.c_str());
            draw_left(buf, IM_COL32(85, 255, 255, 255)); // Cyan

            snprintf(buf, sizeof(buf), "  Author: %s | Category: %s",
                     meta->author.empty() ? "Community" : meta->author.c_str(),
                     meta->category.empty() ? "General" : meta->category.c_str());
            draw_left(buf, IM_COL32(180, 220, 240, 255));
        }

        // Native mod libraries
        auto mod_libs = get_loaded_guest_mod_libs();
        if (!mod_libs.empty()) {
            std::string libs_str;
            for (size_t i = 0; i < mod_libs.size(); ++i) {
                if (i > 0) libs_str += ", ";
                libs_str += mod_libs[i];
            }
            snprintf(buf, sizeof(buf), "  Native Libs: %s (ARM64 .so injected)", libs_str.c_str());
            draw_left(buf, IM_COL32(85, 255, 85, 255)); // Green
        } else {
            draw_left("  Native Libs: None (Resource-only mod)", IM_COL32(160, 160, 160, 255));
        }

        // VFS Hierarchy status
        snprintf(buf, sizeof(buf), "  VFS Hierarchy: Active (mods/%s/resources -> base)", active_mod.c_str());
        draw_left(buf, IM_COL32(180, 220, 240, 255));

        // Mod Load Order list from launcher.toml (cached, refreshed at most every 2s)
        static LauncherConfig s_cached_lcfg;
        static double s_last_lcfg_check = -10.0;
        double cur_time = ImGui::GetTime();
        if (cur_time - s_last_lcfg_check > 2.0) {
            s_cached_lcfg = launcher_config_load();
            s_last_lcfg_check = cur_time;
        }
        const LauncherConfig& lcfg = s_cached_lcfg;

        if (!lcfg.mod_load_order.empty()) {
            std::string order_str = "[";
            for (size_t i = 0; i < lcfg.mod_load_order.size(); ++i) {
                if (i > 0) order_str += ", ";
                order_str += lcfg.mod_load_order[i];
            }
            order_str += "]";
            snprintf(buf, sizeof(buf), "  Load Order: %s", order_str.c_str());
            draw_left(buf, IM_COL32(180, 180, 180, 255));
        }
    } else {
        draw_left("[Active Mod] None (Vanilla base mode)", IM_COL32(255, 170, 0, 255));
        static LauncherConfig s_cached_vanilla_lcfg;
        static double s_last_vanilla_check = -10.0;
        double cur_time = ImGui::GetTime();
        if (cur_time - s_last_vanilla_check > 2.0) {
            s_cached_vanilla_lcfg = launcher_config_load();
            s_last_vanilla_check = cur_time;
        }
        const LauncherConfig& lcfg = s_cached_vanilla_lcfg;
        if (!lcfg.mod_load_order.empty()) {
            std::string order_str = "[";
            for (size_t i = 0; i < lcfg.mod_load_order.size(); ++i) {
                if (i > 0) order_str += ", ";
                order_str += lcfg.mod_load_order[i];
            }
            order_str += "]";
            snprintf(buf, sizeof(buf), "  Config Order: %s (inactive)", order_str.c_str());
            draw_left(buf, IM_COL32(160, 160, 160, 255));
        }
    }

    draw_left(""); // spacer

    // 5. XYZ & Block position (Minecraft style)
    snprintf(buf, sizeof(buf), "XYZ: %.3f / %.3f / %.3f", st.hero_x, st.hero_y, st.hero_z);
    draw_left(buf, IM_COL32(255, 255, 255, 255));

    snprintf(buf, sizeof(buf), "Block: %d  %d  %d", (int)st.hero_x, (int)st.hero_y, (int)st.hero_z);
    draw_left(buf, IM_COL32(210, 210, 210, 255));

    snprintf(buf, sizeof(buf), "Camera: (%.1f, %.1f, %.1f) zoom: %.2fx  focus: %s",
             st.cam_x, st.cam_y, st.cam_z, st.cam_zoom,
             st.cam_active ? "Overridden" : "Hero Follow");
    draw_left(buf, IM_COL32(210, 210, 210, 255));

    // 6. Draw statistics & PostFX
    snprintf(buf, sizeof(buf), "Draw Calls: %d | Vertices: %d | Tex Binds: %d | States: %d",
             st.draw_calls, st.vertices, st.tex_binds, st.state_changes);
    draw_left(buf, IM_COL32(180, 220, 240, 255));

    snprintf(buf, sizeof(buf), "PostFX: %s (%s) | Scaling: %s",
             st.postfx_on ? "ON" : "OFF", st.postfx_preset, st.scale_mode);
    draw_left(buf, st.postfx_on ? IM_COL32(255, 200, 85, 255) : IM_COL32(160, 160, 160, 255));

    // ── RIGHT SIDE (Minecraft F3 style, right-aligned) ────────────────────────
    // 1. GPU & Hardware
    static std::string s_gl_renderer;
    static std::string s_gl_version;
    if (s_gl_renderer.empty() && !m_vulkan_active) {
        const char* rend = (const char*)glGetString(GL_RENDERER);
        if (rend) s_gl_renderer = rend;
        const char* ver = (const char*)glGetString(GL_VERSION);
        if (ver) s_gl_version = ver;
    }

    if (!s_gl_renderer.empty()) {
        snprintf(buf, sizeof(buf), "%s", s_gl_renderer.c_str());
        draw_right(buf, IM_COL32(255, 255, 255, 255));
    } else {
        snprintf(buf, sizeof(buf), "Graphics: %s", st.graphics_api);
        draw_right(buf, IM_COL32(255, 255, 255, 255));
    }
    if (!s_gl_version.empty()) {
        snprintf(buf, sizeof(buf), "GL: %s", s_gl_version.c_str());
        draw_right(buf, IM_COL32(200, 200, 200, 255));
    }

    // 2. Display Resolution
    snprintf(buf, sizeof(buf), "Display: %dx%d (Draw: %dx%d)",
             st.win_w, st.win_h, st.draw_w, st.draw_h);
    draw_right(buf, IM_COL32(200, 200, 200, 255));

    draw_right(""); // spacer

    // 3. Memory & GC (Minecraft style: "Mem: XX% XXX/XXXMB")
    auto& rgc = RedstellGC::instance();
    uint32_t ghs64 = get_guest_heap_size_64();
    uint32_t ghs32 = get_guest_heap_size_32();
    uint32_t guest_heap = ghs64 + ghs32;
    float guest_heap_mb = (float)guest_heap / (1024.0f * 1024.0f);
    float host_ram_mb = (float)rgc.get_current_ram() / (1024.0f * 1024.0f);
    float peak_ram_mb = (float)rgc.get_peak_ram() / (1024.0f * 1024.0f);

    int guest_pct = (int)((guest_heap_mb / 128.0f) * 100.0f);
    if (guest_pct > 100) guest_pct = 100;
    snprintf(buf, sizeof(buf), "Guest Heap: %d%%  %.1f/128MB", guest_pct, guest_heap_mb);
    draw_right(buf, IM_COL32(85, 255, 85, 255)); // Green

    snprintf(buf, sizeof(buf), "Host RAM: %.1f MB (Peak: %.1f MB)", host_ram_mb, peak_ram_mb);
    draw_right(buf, IM_COL32(200, 200, 200, 255));

    snprintf(buf, sizeof(buf), "RGC: %.1f%% frag | Alloc: %lu/s | Free: %lu/s",
             rgc.get_fragmentation_estimate() * 100.0f,
             (unsigned long)rgc.get_alloc_rate(),
             (unsigned long)rgc.get_free_rate());
    draw_right(buf, IM_COL32(180, 220, 240, 255));

    snprintf(buf, sizeof(buf), "Resources: %lu (Tex: %u, POD: %u)",
             (unsigned long)rgc.get_resource_count(),
             rgc.get_texture_count(), rgc.get_pod_count());
    draw_right(buf, IM_COL32(180, 220, 240, 255));

    // ── SRE13 texture identity (REMOVABLE TEST SURFACE) ─────────────────────
    //   Answers "does the host know which texture is bound" from measurement.
    //   The diagnostic value is in the RELATIONSHIP between the two lines:
    //     * identified == 0 while binds climbs  -> the Caver::Texture offsets in
    //       sre13/hooks/texture_layout.h do not match this build. A broken
    //       layout otherwise looks identical to a working one that found
    //       nothing, which is why this is on F3 rather than in a log.
    //     * unique > 0 but trusted == 0        -> the channel works but the
    //       mapper's trust gate is refusing every name.
    //   Delete this block, the extern above, and sre13_tex_telemetry_snapshot
    //   once texture identity no longer needs proving.
    {
        uint32_t tt[6] = {0, 0, 0, 0, 0, 0};
        sre13_tex_telemetry_snapshot(tt);
        const uint32_t t_binds = tt[0], t_ident = tt[1], t_uniq = tt[2];
        const uint32_t t_trust = tt[3], t_rej = tt[4], t_level = tt[5];

        // Red when the channel is receiving binds but identifying none: that is
        // the broken-offset case and it must not look like success.
        const bool broken = (t_binds > 0u && t_ident == 0u);
        const ImU32 col = broken ? IM_COL32(255, 90, 90, 255)
                                 : (t_ident > 0u ? IM_COL32(150, 255, 170, 255)
                                                 : IM_COL32(150, 150, 150, 255));
        snprintf(buf, sizeof(buf), "TexID: %u of %u binds named", t_ident, t_binds);
        draw_right(buf, col);

        snprintf(buf, sizeof(buf), "TexID: %u distinct  L%u  gate +%u/-%u",
                 t_uniq, t_level, t_trust, t_rej);
        draw_right(buf, IM_COL32(180, 220, 240, 255));
    }

    // ── Authored-emitter bloom key (REMOVABLE TEST SURFACE) ─────────────
    //   Which emitters the bloom extract is being keyed on, in the resolved
    //   biome. "no biome resolved" is the failure that looks like success in the
    //   picture: an empty palette and a wrong threshold both produce no bloom.
    //   Delete this block, the extern above, and swordigo_bloom_emitter_report
    //   in fbo_scaler.cpp when the key no longer needs proving.
    {
        extern PostFXState g_postfx;   // defined in the main translation unit
        char ebuf[128];
        swordigo_bloom_emitter_report(ebuf, sizeof(ebuf));
        const bool keyed = g_postfx.bloom_emitter_key;
        snprintf(buf, sizeof(buf), "Bloom key: %s", keyed ? "authored emitters" : "luma ONLY");
        draw_right(buf, keyed ? IM_COL32(255, 200, 120, 255) : IM_COL32(255, 120, 120, 255));
        snprintf(buf, sizeof(buf), "  %s", ebuf);
        draw_right(buf, IM_COL32(200, 180, 140, 255));
    }

    draw_right(""); // spacer

    // 4. Subsystems
    snprintf(buf, sizeof(buf), "OpenAL Channels: %u | Open Files: %u",
             rgc.get_openal_count(), rgc.get_open_file_count());
    draw_right(buf, IM_COL32(180, 180, 180, 255));

    snprintf(buf, sizeof(buf), "Mouse: %d, %d | Frame: %d",
             st.mouse_x, st.mouse_y, st.frame_count);
    draw_right(buf, IM_COL32(180, 180, 180, 255));

    if (st.typing_mode) {
        draw_right("[TYPING MODE ACTIVE]", IM_COL32(255, 85, 85, 255));
    }

    // ── BOTTOM-LEFT: Minecraft-style Frame Time Sparkline ────────────────────
    float graph_w = 240.0f;
    float graph_h = 42.0f;
    float graph_x = 10.0f;
    float graph_y = screen_h - graph_h - 24.0f;

    if (graph_y > left_y + 10.0f) {
        // Dark background plate
        dl->AddRectFilled(
            ImVec2(graph_x - 3.0f, graph_y - 14.0f),
            ImVec2(graph_x + graph_w + 3.0f, graph_y + graph_h + 3.0f),
            IM_COL32(0, 0, 0, 150)
        );
        dl->AddText(ImVec2(graph_x, graph_y - 13.0f), IM_COL32(200, 200, 200, 255), "Frame Time (Target: 60 fps / 16.6ms)");

        // 60fps guide line
        float line_60_y = graph_y + graph_h * (1.0f - (60.0f / 80.0f));
        dl->AddLine(ImVec2(graph_x, line_60_y), ImVec2(graph_x + graph_w, line_60_y), IM_COL32(85, 255, 85, 120), 1.0f);

        // Frame bars
        float bar_w = graph_w / (float)FPS_HISTORY;
        for (int i = 0; i < FPS_HISTORY; ++i) {
            int idx = (m_fps_idx + i) % FPS_HISTORY;
            float f = m_fps_history[idx];
            if (f < 0.0f) f = 0.0f;
            if (f > 80.0f) f = 80.0f;
            float bar_h = (f / 80.0f) * graph_h;
            ImU32 bcol = (f >= 55.0f) ? IM_COL32(85, 255, 85, 200)
                       : (f >= 30.0f) ? IM_COL32(255, 200, 50, 200)
                                      : IM_COL32(255, 85, 85, 200);
            dl->AddRectFilled(
                ImVec2(graph_x + i * bar_w, graph_y + graph_h - bar_h),
                ImVec2(graph_x + (i + 1) * bar_w - 1.0f, graph_y + graph_h),
                bcol
            );
        }
    }

    // Bottom-center quick keybind reminder
    {
        const char* hint = "F1:GUI  F2:Controls  F3:Debug  F4:Scale  F5:Cam  F6:PostFX  F7:Video  \\:Type  F10:HUD";
        ImVec2 hsz = ImGui::CalcTextSize(hint);
        float hx = (screen_w - hsz.x) * 0.5f;
        float hy = screen_h - hsz.y - 8.0f;
        dl->AddRectFilled(
            ImVec2(hx - 4.0f, hy - 2.0f),
            ImVec2(hx + hsz.x + 4.0f, hy + hsz.y + 2.0f),
            IM_COL32(0, 0, 0, 130)
        );
        dl->AddText(ImVec2(hx, hy), IM_COL32(180, 180, 180, 220), hint);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------
// Custom Button Array structures (matches SreBtnSlot in sre_mini_api.c)
// ---------------------------------------------------------------------------
#define SRE_BTN_MAX       128
#define SRE_BTN_ID_LEN    32
#define SRE_BTN_LABEL_LEN 64

struct SreBtnSlot {
    char     id[SRE_BTN_ID_LEN];       /* Lua string ID */
    char     label[SRE_BTN_LABEL_LEN]; /* Display text */
    float    x, y;                      /* Normalized position (0-1) */
    float    w, h;                      /* Normalized dimensions (base-relative) */
    float    alpha;                     /* Overall alpha 0-255 */
    float    scale_x, scale_y;         /* Scaling factors */
    int      text_color;               /* Packed ARGB */
    float    text_scale;               /* Text size multiplier */
    int      bg_alpha;                 /* Background alpha (0-255) */
    int      hidden;                   /* Per-button hidden flag */
    int      clickable;                /* Whether it accepts clicks */
    int      movable;                  /* Can be dragged */
    int      snapback;                 /* Returns to original pos on release */
    float    home_x, home_y;           /* Original position (for snapback) */
    int      padding_l, padding_t, padding_r, padding_b;
    int      alignment;                /* Text alignment / gravity */
    char     overlay_id[SRE_BTN_ID_LEN]; /* Belongs to overlay */
    int      confined;                  /* Confined to overlay bounds */
    /* ---- STATE (written by host, read by SRE) ---- */
    volatile int pressed;              /* Host writes: 1=down, 0=up */
    volatile int released;             /* Host writes: 1 on release */
    volatile int dragging;             /* Host writes: 1=dragging, 0=not */
    volatile float cur_x, cur_y;       /* Current position (after drag) */
    int      active;                   /* 1 = slot in use, 0 = free */
    int      dirty;                    /* 1 = needs visual update by host */
};

#define SRE_OVERLAY_MAX 8
struct SreOverlaySlot {
    char     id[SRE_BTN_ID_LEN];
    float    x, y;
    float    w, h;
    int      bg_color;
    int      bg_alpha;
    float    corner_radius;
    int      hidden;
    int      movable;
    int      pinchable;
    float    scale_factor;
    int      pinching;
    /* Separators inside overlay */
    float    separators[8];
    int      separator_count;
    int      active;
    int      dirty;
};

void SwordfareGUI::draw_buttons(void* guest_buttons_ptr, void* guest_overlays_ptr, bool globally_hidden) {
    m_last_buttons_ptr = guest_buttons_ptr;
    m_last_overlays_ptr = guest_overlays_ptr;
    m_buttons_globally_hidden = globally_hidden;
    g_sre_overlay_blocking = false;
    
    // Diagnostic log
    static int log_ticks = 0;
    if (guest_buttons_ptr && log_ticks++ % 60 == 0) {
        FILE* f = fopen("sre_gui_debug.log", "w");
        if (f) {
            SreOverlaySlot* ovrs = static_cast<SreOverlaySlot*>(guest_overlays_ptr);
            SreBtnSlot* btns = static_cast<SreBtnSlot*>(guest_buttons_ptr);
            fprintf(f, "=== Overlays ===\n");
            if (ovrs) {
                for (int i = 0; i < SRE_OVERLAY_MAX; i++) {
                    if (ovrs[i].active) {
                        fprintf(f, "Slot %d: id='%s' active=%d hidden=%d x=%.3f y=%.3f w=%.3f h=%.3f bg_color=0x%08X bg_alpha=%d corner_radius=%.3f scale=%.3f\n",
                                i, ovrs[i].id, ovrs[i].active, ovrs[i].hidden,
                                ovrs[i].x, ovrs[i].y, ovrs[i].w, ovrs[i].h,
                                (unsigned int)ovrs[i].bg_color, ovrs[i].bg_alpha, ovrs[i].corner_radius, ovrs[i].scale_factor);
                    } else {
                        fprintf(f, "Slot %d: id='%s' active=0\n", i, ovrs[i].id);
                    }
                }
            } else {
                fprintf(f, "overlays is NULL\n");
            }
            fprintf(f, "\n=== Buttons ===\n");
            for (int i = 0; i < SRE_BTN_MAX; i++) {
                if (btns[i].active) {
                    fprintf(f, "Slot %d: id='%s' label='%s' active=%d hidden=%d overlay_id='%s' x=%.3f y=%.3f w=%.3f h=%.3f bg_alpha=%d alpha=%.3f clickable=%d\n",
                            i, btns[i].id, btns[i].label, btns[i].active, btns[i].hidden,
                            btns[i].overlay_id, btns[i].cur_x, btns[i].cur_y, btns[i].w, btns[i].h,
                            btns[i].bg_alpha, btns[i].alpha, btns[i].clickable);
                }
            }
            fclose(f);
        }
    }

    if (!m_initialized || !guest_buttons_ptr || globally_hidden) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    // Retrieve active viewport bounds for correct positioning
    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    extern int GAME_W, GAME_H;
    float game_asp = (float)GAME_W / (float)GAME_H;
    float win_asp  = win_w / win_h;
    float vp_x = 0, vp_y = 0, vp_w = win_w, vp_h = win_h;

    if (win_asp > game_asp) {
        vp_w = win_h * game_asp;
        vp_x = (win_w - vp_w) / 2;
    } else {
        vp_h = win_w / game_asp;
        vp_y = (win_h - vp_h) / 2;
    }

    SreOverlaySlot* overlays = static_cast<SreOverlaySlot*>(guest_overlays_ptr);
    SreBtnSlot* buttons = static_cast<SreBtnSlot*>(guest_buttons_ptr);

    static int last_active_count = -1;
    int current_active_count = 0;
    for (int i = 0; i < SRE_BTN_MAX; i++) {
        if (buttons[i].active) current_active_count++;
    }
    if (current_active_count != last_active_count) {
        last_active_count = current_active_count;
        std::cout << "[GUI-Debug] Active buttons count changed to " << current_active_count << std::endl;
    }

    // Determine if any full-screen / blocking overlay is active (for input gating)
    bool any_overlay_blocking = false;
    if (overlays) {
        for (int i = 0; i < SRE_OVERLAY_MAX; i++) {
            SreOverlaySlot& ovr = overlays[i];
            if (!ovr.active || ovr.hidden) continue;
            if (ovr.w >= 0.6f && ovr.h >= 0.6f) {
                any_overlay_blocking = true;
                break;
            }
        }
    }
    extern bool g_sre_overlay_blocking;
    g_sre_overlay_blocking = any_overlay_blocking;

    // Set up transparent fullscreen window for drawing and hit-testing
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | 
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_NoDecoration;

    ImVec2 mpos = io.MousePos;
    if (!is_input_blocked(mpos.x, mpos.y)) {
        window_flags |= ImGuiWindowFlags_NoInputs;
    }

    ImGui::Begin("##SreOverlayButtonsWindow", nullptr, window_flags);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // 1. Draw Overlays
    if (overlays) {
        for (int i = 0; i < SRE_OVERLAY_MAX; i++) {
            SreOverlaySlot& ovr = overlays[i];
            if (!ovr.active || ovr.hidden) continue;

            float ow = vp_w * ovr.w * ovr.scale_factor;
            float oh = vp_h * ovr.h * ovr.scale_factor;
            float ox = vp_x + vp_w * ovr.x - ow / 2.0f;
            float oy = vp_y + vp_h * ovr.y - oh / 2.0f;

            // Handle dragging & pinching on the overlay background
            if (ovr.movable || ovr.pinchable) {
                ImGui::SetCursorScreenPos(ImVec2(ox, oy));
                ImGui::PushID(ovr.id);
                ImGui::InvisibleButton("##ovr_bg", ImVec2(ow, oh));
                
                bool ovr_hovered = ImGui::IsItemHovered();
                bool ovr_active = ImGui::IsItemActive();

                if (ovr_active && ovr.movable && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 delta = io.MouseDelta;
                    if (vp_w > 0) ovr.x += delta.x / vp_w;
                    if (vp_h > 0) ovr.y += delta.y / vp_h;
                    ovr.dirty = 1;
                }

                if (ovr_hovered && ovr.pinchable) {
                    float wheel = io.MouseWheel;
                    if (wheel != 0.0f) {
                        ovr.scale_factor += wheel * 0.1f;
                        if (ovr.scale_factor < 0.2f) ovr.scale_factor = 0.2f;
                        if (ovr.scale_factor > 3.0f) ovr.scale_factor = 3.0f;
                        ovr.dirty = 1;
                    }
                }
                ImGui::PopID();
            }

            // Parse background color from packed ARGB (0xAARRGGBB)
            unsigned int c_val = (unsigned int)ovr.bg_color;
            uint8_t ba = (c_val >> 24) & 0xFF;
            uint8_t br = (c_val >> 16) & 0xFF;
            uint8_t bg_c = (c_val >> 8) & 0xFF;
            uint8_t bb = c_val & 0xFF;

            if (c_val == 0) {
                br = 22; bg_c = 27; bb = 34;
                ba = (uint8_t)ovr.bg_alpha;
            }

            ImU32 fill_col = IM_COL32(br, bg_c, bb, ba);
            float rounding = (ovr.corner_radius > 0.0f) ? ovr.corner_radius : 6.0f;
            if (ovr.w >= 0.98f && ovr.h >= 0.98f) rounding = 0.0f;

            draw_list->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + ow, oy + oh), fill_col, rounding);

            if (ovr.w < 0.98f || ovr.h < 0.98f) {
                draw_list->AddRect(ImVec2(ox, oy), ImVec2(ox + ow, oy + oh),
                                   IM_COL32(233, 69, 96, 100), rounding, 0, 1.0f);
            }

            // Separators
            for (int s = 0; s < ovr.separator_count; s++) {
                float sy = ovr.separators[s];
                float line_y = oy + oh * sy;
                draw_list->AddLine(ImVec2(ox, line_y), ImVec2(ox + ow, line_y),
                                   IM_COL32(255, 255, 255, 50), 1.5f);
            }
        }
    }

    // 2. Draw Buttons
    ImFont* btn_font = static_cast<ImFont*>(m_font_button);

    for (int i = 0; i < SRE_BTN_MAX; i++) {
        SreBtnSlot& btn = buttons[i];
        if (!btn.active || btn.hidden) continue;

        // Resolve parent overlay
        SreOverlaySlot* parent_ovr = nullptr;
        if (btn.overlay_id[0] != '\0' && overlays) {
            for (int o = 0; o < SRE_OVERLAY_MAX; o++) {
                if (overlays[o].active && strcmp(overlays[o].id, btn.overlay_id) == 0) {
                    parent_ovr = &overlays[o];
                    break;
                }
            }
            if (!parent_ovr) continue;
        }

        // Hide button if parent overlay is hidden/inactive
        if (parent_ovr && (parent_ovr->hidden || !parent_ovr->active)) continue;

        // Compute sizes, accounting for overlay scale
        float o_scale = parent_ovr ? parent_ovr->scale_factor : 1.0f;
        // Kiwi sizes controls from the shorter viewport side while positions
        // remain normalized to the full viewport.
        float square_base = std::min(vp_w, vp_h);
        float pw = square_base * btn.w * btn.scale_x * o_scale;
        float ph = square_base * btn.h * btn.scale_y * o_scale;
        float bx, by;

        if (parent_ovr) {
            float ow = vp_w * parent_ovr->w * parent_ovr->scale_factor;
            float oh = vp_h * parent_ovr->h * parent_ovr->scale_factor;
            float ox = vp_x + vp_w * parent_ovr->x - ow / 2.0f;
            float oy = vp_y + vp_h * parent_ovr->y - oh / 2.0f;
            bx = ox + ow * btn.cur_x - pw / 2.0f;
            by = oy + oh * btn.cur_y - ph / 2.0f;
        } else {
            bx = vp_x + vp_w * btn.cur_x - pw / 2.0f;
            by = vp_y + vp_h * btn.cur_y - ph / 2.0f;
        }

        float bx2 = bx + pw;
        float by2 = by + ph;
        if (bx2 < 0 || by2 < 0 || bx > win_w || by > win_h) continue;

        // Interactive Invisible Button
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        ImGui::PushID(btn.id);
        
        bool clicked = false;
        if (btn.clickable) {
            clicked = ImGui::InvisibleButton("##btn", ImVec2(pw, ph));
        } else {
            // Non-interactive spacing/dummy widget
            ImGui::Dummy(ImVec2(pw, ph));
        }

        bool hovered = btn.clickable && ImGui::IsItemHovered();
        bool pressing = btn.clickable && ImGui::IsItemActive();

        // Draw premium background style
        if (btn.bg_alpha > 0) {
            float alpha = (btn.alpha / 255.0f) * (btn.bg_alpha / 255.0f);
            
            ImU32 bg_col;
            if (pressing) {
                bg_col = IM_COL32(0, 100, 200, (uint8_t)(alpha * 255.0f * 0.9f));
            } else if (hovered) {
                bg_col = IM_COL32(40, 45, 60, (uint8_t)(alpha * 255.0f * 0.85f));
            } else {
                bg_col = IM_COL32(20, 20, 25, (uint8_t)(alpha * 255.0f * 0.75f));
            }
            
            float rounding = ph * 0.25f;
            if (rounding < 4.0f) rounding = 4.0f;
            if (rounding > 12.0f) rounding = 12.0f;

            draw_list->AddRectFilled(ImVec2(bx, by), ImVec2(bx2, by2), bg_col, rounding);

            ImU32 border_col;
            if (pressing) {
                border_col = IM_COL32(0, 180, 255, 255);
            } else if (hovered) {
                border_col = IM_COL32(0, 140, 255, 220);
            } else {
                border_col = IM_COL32(255, 255, 255, (uint8_t)(alpha * 120.0f));
            }
            draw_list->AddRect(ImVec2(bx, by), ImVec2(bx2, by2), border_col, rounding, 0, 1.5f);
        }

        // Draw label
        const char* lbl = btn.label;
        if (lbl[0] != '\0' && btn_font) {
            float fscale = btn.text_scale > 0.0f ? btn.text_scale : 1.0f;
            if (fscale > 3.0f) fscale = 3.0f;
            float fs = btn_font->LegacySize * fscale * o_scale;
            ImVec2 tsize = btn_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, lbl);

            float pl = btn.padding_l * o_scale;
            float pt = btn.padding_t * o_scale;
            float pr = btn.padding_r * o_scale;
            float pb = btn.padding_b * o_scale;

            int gravity = btn.alignment;
            float tx = bx + (pw - tsize.x) * 0.5f;
            float ty = by + (ph - tsize.y) * 0.5f;

            if (gravity != 0) {
                if ((gravity & 3) == 3 || gravity == 3) {
                    tx = bx + pl;
                } else if ((gravity & 5) == 5 || gravity == 5) {
                    tx = bx2 - tsize.x - pr;
                }
                
                if ((gravity & 48) == 48 || gravity == 48) {
                    ty = by + pt;
                } else if ((gravity & 80) == 80 || gravity == 80) {
                    ty = by2 - tsize.y - pb;
                }
            }

            unsigned int tc = (unsigned int)btn.text_color;
            uint8_t ta = (tc >> 24) & 0xFF;
            uint8_t tr = (tc >> 16) & 0xFF;
            uint8_t tg = (tc >> 8)  & 0xFF;
            uint8_t tb = tc & 0xFF;
            if (ta == 0) ta = (uint8_t)btn.alpha;
            ImU32 text_col = IM_COL32(tr, tg, tb, ta);

            draw_list->AddText(btn_font, fs, ImVec2(tx, ty), text_col, lbl);
        }

        // Input state machine
        if (btn.clickable) {
            bool completed_drag = btn.dragging != 0;
            if (pressing) {
                btn.pressed = 1;
                if (btn.movable && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    btn.dragging = 1;
                    ImVec2 delta = io.MouseDelta;
                    if (parent_ovr) {
                        float ow = vp_w * parent_ovr->w * parent_ovr->scale_factor;
                        float oh = vp_h * parent_ovr->h * parent_ovr->scale_factor;
                        if (ow > 0) btn.cur_x += delta.x / ow;
                        if (oh > 0) btn.cur_y += delta.y / oh;
                    } else {
                        if (vp_w > 0) btn.cur_x += delta.x / vp_w;
                        if (vp_h > 0) btn.cur_y += delta.y / vp_h;
                    }

                    if (btn.confined) {
                        if (parent_ovr) {
                            float min_x = (pw / 2.0f) / (vp_w * parent_ovr->w * parent_ovr->scale_factor);
                            float max_x = 1.0f - min_x;
                            float min_y = (ph / 2.0f) / (vp_h * parent_ovr->h * parent_ovr->scale_factor);
                            float max_y = 1.0f - min_y;
                            if (btn.cur_x < min_x) btn.cur_x = min_x;
                            if (btn.cur_x > max_x) btn.cur_x = max_x;
                            if (btn.cur_y < min_y) btn.cur_y = min_y;
                            if (btn.cur_y > max_y) btn.cur_y = max_y;
                        } else {
                            float min_x = (pw / 2.0f) / vp_w;
                            float max_x = 1.0f - min_x;
                            float min_y = (ph / 2.0f) / vp_h;
                            float max_y = 1.0f - min_y;
                            if (btn.cur_x < min_x) btn.cur_x = min_x;
                            if (btn.cur_x > max_x) btn.cur_x = max_x;
                            if (btn.cur_y < min_y) btn.cur_y = min_y;
                            if (btn.cur_y > max_y) btn.cur_y = max_y;
                        }
                    }
                    btn.dirty = 1;
                }
            } else {
                btn.pressed = 0;
                if (btn.dragging) {
                    btn.dragging = 0;
                    if (btn.snapback) {
                        btn.cur_x = btn.home_x;
                        btn.cur_y = btn.home_y;
                        btn.dirty = 1;
                    }
                }
            }

            // A drag release is not a click. This preserves Kiwi's polling
            // API without the Android listener bug that consumed all taps.
            if (clicked && !completed_drag) {
                btn.released = 1;
                std::cout << "[SRE GUI] ImGui Click: '" << btn.label << "' (slot " << i << ")\n";
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}


// ---------------------------------------------------------------------------
// Memory Research console (standalone overlay, [Insert])
// ---------------------------------------------------------------------------
void SwordfareGUI::draw_research_overlay() {
    if (!m_initialized || !m_research_overlay_visible) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    // ── Xpera shell ──────────────────────────────────────────────────────
    xpera::WindowSpec spec;
    spec.mode       = xpera::WindowMode::Fullscreen;
    spec.eyebrow    = "RESEARCH";
    spec.title      = "Memory Research";
    spec.subtitle   = "Live guest-memory console \u2014 catalog, decoder, watchpoints, DB";
    spec.close_hint = "Insert";

    bool open = true;
    if (xpera::begin_window("##xpera_research", spec, &open)) {
        if (!m_research_tab || !m_research_tab->is_ready()) {
            ImGui::Spacing();
            xpera::accent_rule();
            ImGui::Spacing();
            ImGui::TextColored(xpera::palette.warning,
                ICON_FA_TRIANGLE_EXCLAMATION "  Memory Research is not initialised.");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "The embedded recovery catalog is only loaded by the ARM64 + SRE boot path. "
                "Launch with SRE enabled (not --no-sre, not --openswordigo, not ARM32) and the "
                "catalog will be available here.");
        } else {
            m_research_tab->draw();
        }
    }
    xpera::end_window();

    if (!open) m_research_overlay_visible = false;
}

void SwordfareGUI::draw_research_hud() {
    if (!m_initialized || !m_research_tab) return;
    if (!m_research_tab->hud_enabled()) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    // "full screen" in the bar returns to the console, on the section the bar was
    // operating on.  Closing the console does not close the bar.
    if (m_research_tab->draw_hud()) m_research_overlay_visible = true;
}

void SwordfareGUI::draw_mod_overlay(const std::string& save_dir) {
    if (!m_initialized || !m_mod_overlay_visible) return;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    ImGuiIO& io = ImGui::GetIO();

    extern const char* mod_speed_label();

    // Forced-nav requests (from the F1 menu / Home dashboard) select a rail item.
    if (m_overlay_forced_tab >= 0) {
        m_overlay_tab        = m_overlay_forced_tab;
        m_overlay_forced_tab = -1;
    }

    // ── Xpera shell: frameless chrome, no game art, no chunky close button ──
    xpera::WindowSpec spec;
    spec.mode      = xpera::WindowMode::Fullscreen;
    spec.eyebrow   = "SWORDFARE";
    spec.title     = m_f11_overlay_active ? "Scene & Display" : "Mod Hub";
    spec.subtitle  = m_f11_overlay_active
        ? "Scene teleport, render resolution, output, upscaling and post-processing"
        : "SRE runtime, Lua tooling, diagnostics and mod compatibility";
    spec.close_hint = m_f11_overlay_active ? "F11" : "F4";

    bool open = true;
    if (xpera::begin_window("##swordfare_center", spec, &open)) {
        if (m_status_timer > 0.0f) m_status_timer -= io.DeltaTime;

        const float rail_h = ImGui::GetContentRegionAvail().y;

        // ── Left nav rail (replaces the old tab bar) ─────────────────────
        ImGui::BeginChild("##sf_nav", ImVec2(198.0f, rail_h), ImGuiChildFlags_None);
        if (xpera::nav_item(ICON_FA_HOUSE "  Home", nullptr, m_overlay_tab == 0)) m_overlay_tab = 0;
        if (xpera::nav_item(ICON_FA_DISPLAY "  Scene & Display", nullptr, m_overlay_tab == 1)) m_overlay_tab = 1;
        if (xpera::nav_item(ICON_FA_MAP "  Scene Shifter", nullptr, m_overlay_tab == 2)) m_overlay_tab = 2;
        if (xpera::nav_item(ICON_FA_TERMINAL "  Diagnostics", nullptr, m_overlay_tab == 3)) m_overlay_tab = 3;
        ImGui::Spacing();
        xpera::divider();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, xpera::palette.text_lo);
        ImGui::TextUnformatted("RESEARCH");
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, xpera::palette.text_mid);
        ImGui::TextWrapped("Live guest memory console (own window).");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        if (xpera::toolbar_button(ICON_FA_FLASK "  Open  [Insert]", false, ImVec2(-1, 32))) {
            m_research_overlay_visible = true;
            m_mod_overlay_visible = false;
            m_f11_overlay_active = false;
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 16.0f);

        // ── Content surface ──────────────────────────────────────────────
        ImGui::BeginChild("##sf_content", ImVec2(0.0f, rail_h), ImGuiChildFlags_None);
        if (m_overlay_tab == 0) {
                ImGui::Spacing();

                // ── Resolve live status ──────────────────────────────────────
                const char* cur_scene = "(unknown)";
                if (m_scene_shifter_ready && m_guest_memory && m_ss_current_scene_va) {
                    const char* p = (const char*)(m_guest_memory + m_ss_current_scene_va);
                    if (p[0]) cur_scene = p;
                }
                const bool sre_ok      = m_scene_shifter_ready || m_console_ready;
                const bool catalog_ok  = is_research_ready();
                const bool shifter_ok  = m_scene_shifter_ready;
                const bool console_ok  = m_console_ready;

                const float avail_w = ImGui::GetContentRegionAvail().x;
                const float gap     = 16.0f;
                const float col_l   = avail_w * 0.55f;
                const float col_r   = avail_w - col_l - gap;

                // ── LEFT COLUMN — branding + status grid ─────────────────
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.914f, 0.271f, 0.376f, 1.0f), "S W O R D F A R E   //   C O N T R O L   C E N T E R");
                ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.68f, 1.0f),
                    "Live SRE runtime, scene teleport, render/display and diagnostics \u2014 one workspace.");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_GAUGE_HIGH "  System status");
                ImGui::Spacing();

                auto status_card = [&](const char* icon, const char* name, bool ok, const char* detail) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.11f, 0.15f, 0.70f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
                    ImGui::BeginChild(name, ImVec2(col_l - 8.0f, 54.0f), ImGuiChildFlags_Borders);
                    ImGui::TextColored(ok ? ImVec4(0.30f, 0.90f, 0.55f, 1.0f) : ImVec4(0.90f, 0.45f, 0.30f, 1.0f),
                        "%s  %s", icon, ok ? "ONLINE" : "OFFLINE");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.82f, 0.86f, 0.92f, 1.0f), "   %s", name);
                    ImGui::TextDisabled("%s", detail);
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                };
                status_card(ICON_FA_BOLT,     "SRE runtime",      sre_ok,     "Java / native bridge + guest hooks");
                status_card(ICON_FA_FLASK,    "Recovery catalog", catalog_ok, "Embedded struct / field database");
                status_card(ICON_FA_MAP,      "Scene shifter",    shifter_ok, "Guest scene list + gateway dispatch");
                status_card(ICON_FA_TERMINAL, "Lua console",      console_ok, "ImGui-native REPL and TCP server");

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_LOCATION_DOT "  Live session");
                ImGui::Spacing();

                if (ImGui::BeginTable("##home_live", 2, ImGuiTableFlags_SizingFixedFit)) {
                    auto live_row = [&](const char* k, const char* v, ImVec4 col) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", k);
                        ImGui::TableSetColumnIndex(1); ImGui::TextColored(col, "%s", v);
                    };
                    live_row("Current scene", cur_scene, ImVec4(0.30f, 0.90f, 0.55f, 1.0f));
                    live_row("Game speed",    mod_speed_label(), ImVec4(1.0f, 0.85f, 0.30f, 1.0f));
                    live_row("Save directory", save_dir.c_str(), ImVec4(0.70f, 0.75f, 0.82f, 1.0f));
                    ImGui::EndTable();
                }
                ImGui::EndGroup();

                // ── RIGHT COLUMN — quick actions + hotkeys ───────────────
                ImGui::SameLine(0.0f, gap);
                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_SLIDERS "  Quick actions");
                ImGui::Separator();
                ImGui::Spacing();

                const float bw = (col_r > 40.0f) ? col_r - 8.0f : col_r;
                if (ImGui::Button(ICON_FA_FLASK "  Memory Research   [Insert]", ImVec2(bw, 34))) {
                    m_research_overlay_visible = true;
                    m_mod_overlay_visible = false;
                    m_f11_overlay_active = false;
                }
                if (ImGui::Button(ICON_FA_MAP "  Scene Shifter",           ImVec2(bw, 30))) m_overlay_forced_tab = 2;
                if (ImGui::Button(ICON_FA_GEAR "  Render & Display",        ImVec2(bw, 30))) m_overlay_forced_tab = 1;
                if (ImGui::Button(ICON_FA_TERMINAL "  Diagnostics & Logs",  ImVec2(bw, 30))) m_overlay_forced_tab = 3;
                ImGui::Spacing();
                if (ImGui::Button(ICON_FA_CUBE "  Close overlay   [F11]",   ImVec2(bw, 30))) {
                    m_mod_overlay_visible = false;
                    m_f11_overlay_active = false;
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_BOOK_OPEN "  Hotkeys");
                ImGui::Separator();
                static const struct { const char* k; const char* d; } kHomeKeys[] = {
                    {"F1",       "Debug launcher / menu bar"},
                    {"F3",       "Debug HUD overlay"},
                    {"F4",       "Mod Hub (this window)"},
                    {"F8 / F9",  "Pause / step one frame"},
                    {"F10",      "Toggle on-screen controls"},
                    {"F11",      "Scene & Display toolbox"},
                    {"F12",      "Fullscreen toggle"},
                    {"Insert",   "Memory Research console"},
                    {"\u0060",   "Lua console"},
                };
                if (ImGui::BeginTable("##home_keys", 2, ImGuiTableFlags_SizingFixedFit)) {
                    for (auto& hk : kHomeKeys) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(ImVec4(0.00f, 0.70f, 0.90f, 1.0f), "%-9s", hk.k);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ImVec4(0.78f, 0.82f, 0.88f, 1.0f), "%s", hk.d);
                    }
                    ImGui::EndTable();
                }
                ImGui::EndGroup();
            }

            if (m_overlay_tab == 1) {
                ImGui::Spacing();
                extern void apply_render_preset(int preset);
                extern int  g_render_preset;
                extern int  GAME_W, GAME_H;
                extern int  g_win_w, g_win_h;
                extern PostFXState  g_postfx;
                extern PostFXPreset g_postfx_preset;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.09f, 0.12f, 0.5f));
                if (ImGui::BeginChild("##options_child", ImVec2(0, 0), true)) {
                    // ── Remaster: faint game-panel artwork behind the controls ──
                    if (m_tex_panel) {
                        ImDrawList* cdl = ImGui::GetWindowDrawList();
                        ImVec2 c0 = ImGui::GetWindowPos();
                        ImVec2 c1(c0.x + ImGui::GetWindowWidth(), c0.y + ImGui::GetWindowHeight());
                        cdl->AddImage((ImTextureID)(intptr_t)m_tex_panel, c0, c1,
                                      ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 14));
                    }

                    // ── RENDER RESOLUTION ──────────────────────────────────────────────
                    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_DISPLAY "  RENDER RESOLUTION");
                    ImGui::TextDisabled("Internal FBO resolution the game renders at. Upscaled to output by the active filter.");
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text("Active render: "); ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.50f, 1.0f), "%d x %d", GAME_W, GAME_H);
                    ImGui::Spacing();

                    struct ResPreset { const char* label; const char* desc; int idx; int w; int h; };
                    static const ResPreset render_presets[] = {
                        { "4K      (3840 x 2160)", "Ultra HD — huge GPU cost",                                    10, 3840, 2160 },
                        { "1440p   (2560 x 1440)", "2K — sharp, moderate GPU cost",                              9,  2560, 1440 },
                        { "1080p   (1920 x 1080)", "Full HD — best quality/perf tradeoff",                       0,  1920, 1080 },
                        { "900p    (1600 x  900)", "High — ~30% less GPU than 1080p",                           11,  1600,  900 },
                        { "768p    (1366 x  768)", "Laptop native — balanced",                                  12,  1366,  768 },
                        { "720p    (1280 x  720)", "HD — ~44% GPU savings vs 1080p",                             1,  1280,  720 },
                        { "600p    (1024 x  600)", "Tablet — good for low-end GPU",                             13,  1024,  600 },
                        { "540p    ( 960 x  544)", "Android native — fastest, ~75% savings",                    2,   960,  544 },
                        { "480p    ( 854 x  480)", "WVGA — very fast",                                          14,   854,  480 },
                        { "360p    ( 640 x  360)", "Low — maximum performance",                                 15,   640,  360 },
                        { "240p    ( 426 x  240)", "Retro-low — extreme performance mode",                     16,   426,  240 },
                        { "WUXGA   (1920 x 1200)", "16:10 — slightly taller than 1080p",                       17,  1920, 1200 },
                        { "1440x900 (1440 x  900)", "Older widescreen laptop",                                 18,  1440,  900 },
                        { "1280x800 (1280 x  800)", "iPad-like — 16:10",                                       19,  1280,  800 },
                        { "UWQHD   (3440 x 1440)", "21:9 ultrawide",                                            20,  3440, 1440 },
                        { "UWHD    (2560 x 1080)", "21:9 ultrawide Full HD",                                    21,  2560, 1080 },
                        { "UWQHD+  (3840 x 1600)", "24:10 ultrawide workstation",                              22,  3840, 1600 },
                        { "Retina  (2880 x 1800)", "16:10 high-density laptop",                                23,  2880, 1800 },
                        { "WSXGA+  (1680 x 1050)", "16:10 desktop and laptop",                                 24,  1680, 1050 },
                        { "SXGA    (1280 x 1024)", "5:4 legacy display",                                        25,  1280, 1024 },
                        { "XGA     (1024 x  768)", "4:3 legacy display",                                        26,  1024,  768 },
                        { "Window  (match output)",  "1:1 pixel mapping — no upscaling",                        3,     0,    0 },
                    };
                    static const int render_preset_count = (int)(sizeof(render_presets)/sizeof(render_presets[0]));

                    const float preset_gap = 6.0f;
                    const int preset_cols = std::max(1, std::min(3,
                        (int)((ImGui::GetContentRegionAvail().x + preset_gap) / 250.0f)));
                    const float preset_w = (ImGui::GetContentRegionAvail().x -
                                            preset_gap * (preset_cols - 1)) / preset_cols;
                    for (int pi = 0; pi < render_preset_count; pi++) {
                        const ResPreset& pr = render_presets[pi];
                        bool selected = (g_render_preset == pr.idx);
                        if (pi % preset_cols != 0) ImGui::SameLine(0, preset_gap);
                        if (selected) {
                            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.65f, 0.22f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.42f, 0.14f, 1.0f));
                        }
                        ImGui::PushID(100 + pi);
                        if (ImGui::Button(pr.label, ImVec2(preset_w, 30))) {
                            apply_render_preset(pr.idx);
                            m_status_msg = std::string("Render res: ") + pr.label;
                            m_status_timer = 3.0f;
                        }
                        ImGui::PopID();
                        if (selected) ImGui::PopStyleColor(3);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pr.desc);
                    }

                    // Custom render resolution
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Custom render resolution:");
                    static int custom_rw = 1280, custom_rh = 720;
                    ImGui::SetNextItemWidth(120); ImGui::InputInt("W##crw", &custom_rw); ImGui::SameLine();
                    ImGui::SetNextItemWidth(120); ImGui::InputInt("H##crh", &custom_rh); ImGui::SameLine();
                    if (ImGui::Button("Apply Custom Render Res")) {
                        custom_rw = std::max(160, std::min(custom_rw, 7680));
                        custom_rh = std::max(120, std::min(custom_rh, 4320));
                        GAME_W = custom_rw & ~1; GAME_H = custom_rh & ~1;
                        extern void fbo_destroy(); extern bool fbo_init(int, int);
                        fbo_destroy(); fbo_init(GAME_W, GAME_H);
                        g_render_preset = -1;
                        m_status_msg = "Custom render res: " + std::to_string(GAME_W) + "x" + std::to_string(GAME_H);
                        m_status_timer = 3.0f;
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // ── OUTPUT / WINDOW RESOLUTION ────────────────────────────────────
                    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_IMAGE "  OUTPUT RESOLUTION (WINDOW SIZE)");
                    ImGui::TextDisabled("OS window size. The render FBO is upscaled to this by the active filter.");
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::Text("Current output: "); ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.50f, 1.0f), "%d x %d", g_win_w, g_win_h);
                    ImGui::Spacing();

                    struct OutPreset { const char* label; int w; int h; };
                    static const OutPreset out_presets[] = {
                        { "3840x2160 (4K)",  3840, 2160 }, { "2560x1440 (2K)",  2560, 1440 },
                        { "1920x1080",        1920, 1080 }, { "1600x900",         1600,  900 },
                        { "1366x768",         1366,  768 }, { "1280x720",         1280,  720 },
                        { "3440x1440 (21:9)", 3440, 1440 }, { "2560x1080 (21:9)", 2560, 1080 },
                        { "1920x1200 (16:10)",1920, 1200 }, { "1680x1050 (16:10)",1680, 1050 },
                        { "1440x900 (16:10)", 1440,  900 }, { "1280x800 (16:10)", 1280,  800 },
                        { "1280x1024 (5:4)",  1280, 1024 }, { "1024x768 (4:3)",   1024,  768 },
                        { "960x544 (native)",  960,  544 }, { "854x480",            854,  480 },
                    };
                    const int out_count = (int)(sizeof(out_presets) / sizeof(out_presets[0]));
                    const int out_cols = std::max(1, std::min(3,
                        (int)((ImGui::GetContentRegionAvail().x + 6.0f) / 210.0f)));
                    const float out_w = (ImGui::GetContentRegionAvail().x - 6.0f * (out_cols - 1)) / out_cols;
                    for (int oi = 0; oi < out_count; oi++) {
                        if (oi % out_cols != 0) ImGui::SameLine(0, 6);
                        ImGui::PushID(200 + oi);
                        if (ImGui::Button(out_presets[oi].label, ImVec2(out_w, 30))) {
                            if (m_window) {
                                SDL_SetWindowSize(m_window, out_presets[oi].w, out_presets[oi].h);
                                m_status_msg = std::string("Output: ") + out_presets[oi].label;
                                m_status_timer = 3.0f;
                            }
                        }
                        ImGui::PopID();
                    }

                    // Custom output resolution
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Custom output size:");
                    static int custom_ow = 1280, custom_oh = 720;
                    ImGui::SetNextItemWidth(120); ImGui::InputInt("W##cow", &custom_ow); ImGui::SameLine();
                    ImGui::SetNextItemWidth(120); ImGui::InputInt("H##coh", &custom_oh); ImGui::SameLine();
                    if (ImGui::Button("Apply Custom Output")) {
                        if (m_window) {
                            SDL_SetWindowSize(m_window,
                                std::max(320, std::min(custom_ow, 7680)),
                                std::max(240, std::min(custom_oh, 4320)));
                            m_status_msg = "Output: " + std::to_string(custom_ow) + "x" + std::to_string(custom_oh);
                            m_status_timer = 3.0f;
                        }
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // ── DISPLAY / FULLSCREEN ───────────────────────────────────────────
                    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_DISPLAY "  DISPLAY MODE");
                    ImGui::TextDisabled("Fullscreen, VSync and display refresh rate.");
                    ImGui::Separator();
                    ImGui::Spacing();

                    if (m_window) {
                        bool is_fs = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
                        // Display info
                        int disp_id = SDL_GetDisplayForWindow(m_window);
                        if (disp_id) {
                            const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(disp_id);
                            if (dm) {
                                ImGui::Text("Display %d  \u2014  ", disp_id);
                                ImGui::SameLine();
                                ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.50f, 1.0f),
                                                   "%d x %d @ %d Hz%s",
                                                   dm->w, dm->h, (int)(dm->refresh_rate + 0.5f),
                                                   is_fs ? "  (fullscreen)" : "  (windowed)");
                            }
                        }
                        ImGui::Spacing();

                        // Fullscreen toggle
                        if (ImGui::Button(is_fs ? "  Exit Fullscreen" : "  Enter Fullscreen",
                                          ImVec2(210, 0))) {
                            SDL_SetWindowFullscreen(m_window, !is_fs);
                            // Sync host window state so FBO/game view follow
                            int fw, fh;
                            SDL_GetWindowSize(m_window, &fw, &fh);
                            extern int g_win_w, g_win_h;
                            extern int g_draw_w, g_draw_h;
                            g_win_w = fw; g_win_h = fh;
                            SDL_GetWindowSizeInPixels(m_window, &g_draw_w, &g_draw_h);
                            m_status_msg = is_fs ? "Windowed mode" : "Fullscreen";
                            m_status_timer = 2.0f;
                        }
                        ImGui::SameLine(0, 10);
                        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Restore 16:9", ImVec2(150, 0))) {
                            int cw, ch;
                            SDL_GetWindowSize(m_window, &cw, &ch);
                            // Fit the current window into a 16:9 box at the same width
                            int nh = (int)(cw * 9.0f / 16.0f);
                            SDL_SetWindowSize(m_window, cw, nh);
                            m_status_msg = "Aspect restored to 16:9";
                            m_status_timer = 2.0f;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Resizes the window to a clean 16:9 ratio at the current width.");

                        ImGui::Spacing();

                        // ── Fullscreen display modes (professional: exact res × refresh) ──
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                                           ICON_FA_GAUGE_HIGH "  Fullscreen mode:");
                        {
                            int mode_count = 0;
                            SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(disp_id, &mode_count);
                            static int sel_mode = -1;
                            std::vector<std::string> mode_labels;
                            mode_labels.push_back("Default (native)");
                            int cur_mode = 0;
                            const SDL_DisplayMode* cur_dm = SDL_GetCurrentDisplayMode(disp_id);
                            for (int i = 0; i < mode_count; i++) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "%d x %d @ %d Hz",
                                         modes[i]->w, modes[i]->h, (int)(modes[i]->refresh_rate + 0.5f));
                                mode_labels.push_back(buf);
                                if (cur_dm && modes[i]->w == cur_dm->w && modes[i]->h == cur_dm->h &&
                                    modes[i]->refresh_rate == cur_dm->refresh_rate)
                                    cur_mode = i + 1;
                            }
                            if (sel_mode < 0 || sel_mode >= (int)mode_labels.size()) sel_mode = cur_mode;
                            ImGui::SetNextItemWidth(300);
                            if (ImGui::BeginCombo("##fsmode", mode_labels[sel_mode].c_str())) {
                                for (int i = 0; i < (int)mode_labels.size(); i++) {
                                    bool selected = (sel_mode == i);
                                    if (ImGui::Selectable(mode_labels[i].c_str(), selected)) {
                                        sel_mode = i;
                                        if (i == 0)
                                            SDL_SetWindowFullscreenMode(m_window, nullptr);
                                        else if (i - 1 < mode_count)
                                            SDL_SetWindowFullscreenMode(m_window, modes[i - 1]);
                                        SDL_SetWindowFullscreen(m_window, true);
                                        int fw, fh;
                                        SDL_GetWindowSize(m_window, &fw, &fh);
                                        extern int g_win_w, g_win_h;
                                        extern int g_draw_w, g_draw_h;
                                        g_win_w = fw; g_win_h = fh;
                                        SDL_GetWindowSizeInPixels(m_window, &g_draw_w, &g_draw_h);
                                        m_status_msg = mode_labels[i];
                                        m_status_timer = 2.0f;
                                    }
                                }
                                ImGui::EndCombo();
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Pick the exact resolution and refresh rate used in fullscreen.");
                        }

                        ImGui::Spacing();

                        // VSync toggle (OpenGL swap interval; Vulkan keeps its own queue)
                        int vsync_interval = 0;
                        SDL_GL_GetSwapInterval(&vsync_interval);
                        bool vsync_on = (vsync_interval != 0);
                        if (ImGui::Checkbox(ICON_FA_GAUGE_HIGH "  VSync (cap FPS to refresh)", &vsync_on)) {
                            SDL_GL_SetSwapInterval(vsync_on ? 1 : 0);
                            m_status_msg = vsync_on ? "VSync ON" : "VSync OFF";
                            m_status_timer = 2.0f;
                        }

                        // Aspect ratio label of current output
                        {
                            int cw, ch;
                            SDL_GetWindowSize(m_window, &cw, &ch);
                            if (cw > 0 && ch > 0) {
                                float ar = (float)cw / (float)ch;
                                const char* ar_name = "other";
                                if      (fabsf(ar - 16.0f / 9.0f) < 0.02f) ar_name = "16:9";
                                else if (fabsf(ar - 16.0f / 10.0f) < 0.02f) ar_name = "16:10";
                                else if (fabsf(ar - 4.0f / 3.0f)  < 0.02f) ar_name = "4:3";
                                else if (fabsf(ar - 21.0f / 9.0f) < 0.02f) ar_name = "21:9 ultrawide";
                                ImGui::TextDisabled("Aspect: %s  (%.3f)\nRender: %d x %d",
                                                    ar_name, ar, GAME_W, GAME_H);
                            }
                        }
                    } else {
                        ImGui::TextDisabled("(no window available)");
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // ── UPSCALE / SCALE FILTER ────────────────────────────────────────
                    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_LAYER_GROUP "  UPSCALE FILTER");
                    ImGui::TextDisabled("Algorithm used to upscale the render FBO to the output window.");
                    ImGui::Separator();
                    ImGui::Spacing();

                    extern FBOScale g_fbo_mode;
                    const char* scale_labels[] = {
                        "Sharp Bilinear   (2-pass, anti-alias)",
                        "Nearest Neighbor (crisp pixel-art)",
                        "CRT Scanline     (retro CRT effect)",
                        "FSR 1.0          (AMD FidelityFX EASU)",
                    };
                    for (int si = 0; si < 4; si++) {
                        bool sel = (g_fbo_mode == static_cast<FBOScale>(si));
                        if (sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.5f, 1.0f));
                        ImGui::PushID(300 + si);
                        if (ImGui::Selectable(scale_labels[si], sel)) {
                            g_fbo_mode = static_cast<FBOScale>(si);
                            m_status_msg = std::string("Scale: ") + scale_labels[si];
                            m_status_timer = 2.0f;
                        }
                        ImGui::PopID();
                        if (sel) ImGui::PopStyleColor();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // ── POST-FX PRESETS ───────────────────────────────────────────────
                    ImGui::TextColored(ImVec4(0.00f, 0.85f, 1.00f, 1.0f), ICON_FA_WAND_SPARKLES "  POST-PROCESSING (PostFX)");
                    ImGui::TextDisabled("Visual effects applied after the game renders. Tuned for Swordigo's art style.");
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Preset selector
                    const char* preset_labels[] = {
                        "Off", "SW+ Medium", "SW+ High (PBR)", "Atmospheric",
                        "Ethereal", "Cinematic", "Retro", "Fantasy", "Noir", "Custom"
                    };
                    ImGui::Text("Preset:"); ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", g_postfx.preset_name);
                    ImGui::Spacing();

                    for (int pi = 0; pi < (int)PostFXPreset::COUNT; pi++) {
                        bool sel = ((int)g_postfx_preset == pi);
                        if (pi % 5 != 0) ImGui::SameLine(0, 6);
                        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                        ImGui::PushID(400 + pi);
                        if (ImGui::Button(preset_labels[pi], ImVec2(138, 30))) {
                            g_postfx_preset = (PostFXPreset)pi;
                            postfx_apply_preset(g_postfx, g_postfx_preset);
                            m_status_msg = std::string("PostFX: ") + g_postfx.preset_name;
                            m_status_timer = 2.0f;
                        }
                        ImGui::PopID();
                        if (sel) ImGui::PopStyleColor();
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Fine-tune individual effects:");
                    ImGui::Spacing();

                    // Tier 1: color effects
                    ImGui::Checkbox("Vignette",    &g_postfx.vignette);
                    if (g_postfx.vignette) { ImGui::SameLine(); ImGui::SetNextItemWidth(160);
                        ImGui::SliderFloat("##vig", &g_postfx.vignette_intensity, 0.0f, 1.0f, "Strength %.2f"); }

                    ImGui::Checkbox("Bloom",       &g_postfx.bloom);
                    if (g_postfx.bloom) {
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##blmi", &g_postfx.bloom_intensity, 0.0f, 1.0f, "Intensity %.2f");
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##blmt", &g_postfx.bloom_threshold, 0.0f, 1.0f, "Threshold %.2f");
                    }

                    ImGui::Checkbox("Film Grain",  &g_postfx.film_grain);
                    if (g_postfx.film_grain) { ImGui::SameLine(); ImGui::SetNextItemWidth(160);
                        ImGui::SliderFloat("##grain", &g_postfx.grain_intensity, 0.0f, 0.5f, "Strength %.3f"); }

                    ImGui::Checkbox("Chromatic Aberration", &g_postfx.chromatic_aberration);
                    if (g_postfx.chromatic_aberration) { ImGui::SameLine(); ImGui::SetNextItemWidth(160);
                        ImGui::SliderFloat("##ca", &g_postfx.ca_offset, 0.0f, 0.02f, "Offset %.4f"); }

                    ImGui::Checkbox("Sharpen", &g_postfx.sharpen);
                    if (g_postfx.sharpen) { ImGui::SameLine(); ImGui::SetNextItemWidth(160);
                        ImGui::SliderFloat("##sharp", &g_postfx.sharpen_strength, 0.0f, 2.0f, "Strength %.2f"); }

                    ImGui::Checkbox("Color Adjust", &g_postfx.color_adjust);
                    if (g_postfx.color_adjust) {
                        ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Saturation##sat", &g_postfx.saturation, 0.0f, 3.0f);
                        ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Contrast##con",   &g_postfx.contrast,   0.0f, 3.0f);
                        ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Brightness##bri", &g_postfx.brightness,-0.5f, 0.5f);
                        ImGui::SetNextItemWidth(200); ImGui::SliderFloat("Warmth##warm",    &g_postfx.warmth,    -1.0f, 1.0f);
                    }

                    ImGui::Spacing();
                    // Tier 2: advanced effects
                    ImGui::Checkbox("God Rays",    &g_postfx.god_rays);
                    if (g_postfx.god_rays) {
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##gri", &g_postfx.god_rays_intensity, 0.0f, 1.5f, "Inten %.2f");
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##grd", &g_postfx.god_rays_decay, 0.8f, 1.0f, "Decay %.3f");
                    }

                    ImGui::Checkbox("SSAO",        &g_postfx.ssao);
                    if (g_postfx.ssao) {
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##ssaoi", &g_postfx.ssao_intensity, 0.0f, 3.0f, "Inten %.2f");
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##ssaor", &g_postfx.ssao_radius, 0.0f, 0.1f, "Radius %.3f");
                    }

                    ImGui::Checkbox("Shadows",     &g_postfx.shadows);
                    if (g_postfx.shadows) {
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##shi", &g_postfx.shadow_intensity, 0.0f, 1.0f, "Dark %.2f");
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##shs", &g_postfx.shadow_softness, 0.0f, 0.02f, "Soft %.4f");
                    }

                    ImGui::Checkbox("Outlines",    &g_postfx.outlines);
                    if (g_postfx.outlines) {
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##olt", &g_postfx.outline_thickness, 0.5f, 4.0f, "Thick %.1f");
                        ImGui::SameLine(); ImGui::SetNextItemWidth(140);
                        ImGui::SliderFloat("##oli", &g_postfx.outline_intensity, 0.0f, 1.0f, "Opac %.2f");
                    }

                    ImGui::Checkbox("Volumetric Light", &g_postfx.volumetric_light);
                    if (g_postfx.volumetric_light) { ImGui::SameLine(); ImGui::SetNextItemWidth(160);
                        ImGui::SliderFloat("##voli", &g_postfx.volumetric_intensity, 0.0f, 1.0f, "Intensity %.2f"); }

                    ImGui::Spacing();
                    // Tier 3: Remaster
                    ImGui::Checkbox("PBR Shading",    &g_postfx.pbr_enabled);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cook-Torrance BRDF + LabPBR materials. High GPU cost.");
                    ImGui::SameLine();
                    ImGui::Checkbox("Water Reflections", &g_postfx.reflections_enabled);
                    if (g_postfx.reflections_enabled) { ImGui::SameLine(); ImGui::SetNextItemWidth(160);
                        ImGui::SliderFloat("##refi", &g_postfx.reflection_intensity, 0.0f, 1.0f, "Blend %.2f"); }

                    // Status line
                    if (m_status_timer > 0.0f) {
                        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.50f, 1.0f), "\xE2\x9C\xA8 %s", m_status_msg.c_str());
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // ─────────────────────────────────────────────────────────────────
            // Scene Shifter view — ARM64 only feature
            // Scans mod + vanilla directories for .scene files and provides
            // Normal (loading screen) and Forced (instant) teleport gateways.
            // ─────────────────────────────────────────────────────────────────
            if (m_overlay_tab == 2) {
                static bool   scene_list_loaded = false;
                static int    selected_scene     = -1;
                static char   spawn_buf[64]      = "start";
                static char   filter_buf[64]     = "";

                // Resolve guest memory pointers if initialized, otherwise fallback to weak globals
                int*   p_list_count  = (m_scene_shifter_ready && m_guest_memory) ? (int*)(m_guest_memory + m_ss_list_count_va) : &g_sre_scene_list_count;
                char (*p_list)[128]  = (m_scene_shifter_ready && m_guest_memory) ? (char(*)[128])(m_guest_memory + m_ss_list_va) : g_sre_scene_list;
                volatile int* p_pending = (m_scene_shifter_ready && m_guest_memory) ? (volatile int*)(m_guest_memory + m_ss_pending_va) : &g_sre_scene_shift_pending;
                char*  p_target      = (m_scene_shifter_ready && m_guest_memory) ? (char*)(m_guest_memory + m_ss_target_va) : g_sre_scene_shift_target;
                char*  p_spawn       = (m_scene_shifter_ready && m_guest_memory) ? (char*)(m_guest_memory + m_ss_spawn_va) : g_sre_scene_shift_spawn;
                char*  p_error       = (m_scene_shifter_ready && m_guest_memory) ? (char*)(m_guest_memory + m_ss_error_va) : g_sre_scene_shift_last_error;
                int*   p_active      = (m_scene_shifter_ready && m_guest_memory) ? (int*)(m_guest_memory + m_ss_active_va) : (int*)&g_sre_scene_shift_active;
                char*  p_cur_scene   = (m_scene_shifter_ready && m_guest_memory && m_ss_current_scene_va) ? (char*)(m_guest_memory + m_ss_current_scene_va) : g_sre_current_scene_name;

                auto do_rescan = [&]() {
                    if (m_scene_shifter_ready && !m_ss_assets_dir.empty()) {
                        namespace fs = std::filesystem;
                        *p_list_count = 0;
                        auto try_scan = [&](const fs::path& dir) {
                            if (!fs::exists(dir) || !fs::is_directory(dir)) return;
                            std::error_code ec;
                            for (auto& ent : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                                if (*p_list_count >= 256) break;
                                if (!ent.is_regular_file(ec)) continue;
                                auto name = ent.path().filename().string();
                                if (name.size() <= 6 || name.substr(name.size() - 6) != ".scene") continue;
                                std::string stem = name.substr(0, name.size() - 6);
                                bool dup = false;
                                for (int i = 0; i < *p_list_count; i++) {
                                    if (std::string(p_list[i]) == stem) { dup = true; break; }
                                }
                                if (dup) continue;
                                strncpy(p_list[*p_list_count], stem.c_str(), 127);
                                p_list[*p_list_count][127] = '\0';
                                (*p_list_count)++;
                            }
                        };
                        try_scan(fs::path(m_ss_assets_dir) / "resources");
                        qsort(p_list, *p_list_count, 128, [](const void* a, const void* b) -> int {
                            return strcmp((const char*)a, (const char*)b);
                        });
                    } else {
                        sre_scene_shifter_scan_scenes();
                    }
                };

                if (!scene_list_loaded) {
                    do_rescan();
                    scene_list_loaded = true;
                }

                ImGui::Spacing();

                // ── Header row: current scene + controls ──────────────────
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
                                   ICON_FA_LOCATION_DOT " Current scene:");
                ImGui::SameLine();
                ImGui::TextUnformatted(p_cur_scene[0]
                                       ? p_cur_scene
                                       : "(unknown)");

                ImGui::SameLine(0, 20.0f);
                if (ImGui::SmallButton(ICON_FA_ARROWS_ROTATE " Refresh")) {
                    do_rescan();
                    scene_list_loaded = true;
                    selected_scene = -1;
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                   "%d scenes", *p_list_count);

                ImGui::Separator();
                ImGui::Spacing();

                // ── Filter + scene list ───────────────────────────────────
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##ss_filter", ICON_FA_MAGNIFYING_GLASS " Filter scenes...",
                                         filter_buf, sizeof(filter_buf));

                float list_h = ImGui::GetContentRegionAvail().y - 130.0f;
                if (list_h < 120.0f) list_h = 120.0f;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.06f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.15f, 0.45f, 0.85f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.55f, 1.0f,  0.7f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.3f, 0.65f, 1.0f,  0.9f));

                if (ImGui::BeginChild("##scene_list", ImVec2(0, list_h), true)) {
                    if (*p_list_count == 0) {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                            "No scenes found.\nMake sure a mod is active or vanilla assets are accessible.\n"
                            "Press Refresh to scan again.");
                    } else {
                        for (int i = 0; i < *p_list_count; i++) {
                            const char* name = p_list[i];
                            // Apply filter
                            if (filter_buf[0] && !strstr(name, filter_buf)) continue;

                            // Highlight current scene
                            bool is_current = (p_cur_scene[0] &&
                                               strcmp(name, p_cur_scene) == 0);

                            if (is_current) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.5f, 1.0f));
                            }

                            char label[160];
                            snprintf(label, sizeof(label), "%s %s",
                                     is_current ? ICON_FA_LOCATION_DOT : "   ", name);

                            bool sel = (selected_scene == i);
                            if (ImGui::Selectable(label, sel,
                                    ImGuiSelectableFlags_AllowDoubleClick)) {
                                selected_scene = i;
                                // Double-click → fill spawn and set as target
                                if (ImGui::IsMouseDoubleClicked(0)) {
                                    strncpy(p_target, name, 127);
                                    p_target[127] = '\0';
                                }
                            }

                            if (is_current) ImGui::PopStyleColor();
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(4);

                ImGui::Spacing();

                // ── Spawn point input ─────────────────────────────────────
                ImGui::SetNextItemWidth(180.0f);
                ImGui::InputTextWithHint("##ss_spawn", "Spawn point (e.g. start)",
                                         spawn_buf, sizeof(spawn_buf));
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), " \u2190 spawn");

                ImGui::Spacing();

                // ── Target display & teleport buttons ─────────────────────
                const char* target_name = (selected_scene >= 0 &&
                                           selected_scene < *p_list_count)
                                          ? p_list[selected_scene]
                                          : nullptr;

                bool can_tp = (target_name != nullptr &&
                               *p_pending == 0 &&
                               *p_active  == 0);

                if (target_name) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                                       ICON_FA_MAP_LOCATION_DOT " Target: %s", target_name);
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                                       "(select a scene above)");
                }

                if (*p_active) {
                    ImGui::SameLine(0, 16.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f),
                                       ICON_FA_SPINNER " Loading...");
                }

                ImGui::Spacing();

                if (!can_tp) ImGui::BeginDisabled();

                // ── Remaster: gateway buttons with the game's button artwork ──
                const float gw_h = ImGui::GetFrameHeight();
                const ImVec2 gw_sz(220.0f, gw_h);
                auto draw_game_button = [&](ImVec4 fallback, ImVec4 hover, ImVec4 active,
                                            ImU32 tint, const char* label, ImVec2 sz) -> bool {
                    ImVec2 bp = ImGui::GetCursorScreenPos();
                    if (m_tex_btn_wide) {
                        ImGui::GetWindowDrawList()->AddImage(
                            (ImTextureID)(intptr_t)m_tex_btn_wide,
                            bp, ImVec2(bp.x + sz.x, bp.y + sz.y),
                            ImVec2(0, 0), ImVec2(1, 1), tint);
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.12f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.20f));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Button,        fallback);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  active);
                    }
                    bool clicked = ImGui::Button(label, sz);
                    ImGui::PopStyleColor(3);
                    return clicked;
                };

                // Normal Gateway — standard loading screen transition
                bool do_normal = draw_game_button(ImVec4(0.15f, 0.45f, 0.15f, 0.85f),
                                                  ImVec4(0.2f, 0.6f,  0.2f,  1.0f),
                                                  ImVec4(0.3f, 0.75f, 0.3f,  1.0f),
                                                  IM_COL32(255, 255, 255, 190),
                                                  ICON_FA_PLAY " Normal Gateway", gw_sz);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Calls GotoLevel() \u2192 shows loading screen.\n"
                                      "Standard transition, identical to entering a portal.");

                ImGui::SameLine(0, 12.0f);

                // Forced Gateway — instant 0-frame load
                bool do_forced = draw_game_button(ImVec4(0.5f, 0.15f, 0.15f, 0.85f),
                                                  ImVec4(0.7f, 0.2f,  0.2f,  1.0f),
                                                  ImVec4(0.9f, 0.3f,  0.3f,  1.0f),
                                                  IM_COL32(255, 210, 210, 200),
                                                  ICON_FA_BOLT " Forced Gateway", gw_sz);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Calls GotoLevel() for instant scene transition.\n"
                                      "Fast teleport gateway.");

                if (!can_tp) ImGui::EndDisabled();

                // Dispatch requests — set pending for sre_scene_shifter_tick()
                if (can_tp && target_name) {
                    if (do_normal || do_forced) {
                        strncpy(p_target, target_name, 127);
                        p_target[127] = '\0';
                        strncpy(p_spawn, spawn_buf, 63);
                        p_spawn[63] = '\0';
                        // Mode: 1=normal, 2=forced
                        *p_pending = do_forced ? 2 : 1;
                    }
                }

                // Status / last error
                if (p_error[0] && strcmp(p_error, "OK") != 0) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                       ICON_FA_TRIANGLE_EXCLAMATION " %s",
                                       p_error);
                }

            }

            if (m_overlay_tab == 3) {
                ImGui::Spacing();
                if (ImGui::Button("Clear Logs")) {
                    std::string log_file_path = save_dir + "/external/sre_lua_errors.log";
                    FILE* f = fopen(log_file_path.c_str(), "w");
                    if (f) fclose(f);
                }
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Reading: external/sre_lua_errors.log");
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.05f, 0.5f));
                if (ImGui::BeginChild("##logs_child", ImVec2(0, 0), true)) {
                    std::string log_file_path = save_dir + "/external/sre_lua_errors.log";
                    FILE* f = fopen(log_file_path.c_str(), "r");
                    if (f) {
                        char buf[512];
                        while (fgets(buf, sizeof(buf), f)) {
                            size_t len = strlen(buf);
                            if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
                            if (strstr(buf, "ERROR") || strstr(buf, "exception") || strstr(buf, "failed")) {
                                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", buf);
                            } else {
                                ImGui::TextUnformatted(buf);
                            }
                        }
                        fclose(f);
                    } else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No logs recorded yet.");
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

        ImGui::EndChild();   // ##sf_content
    }
    xpera::end_window();

    if (!open) {
        m_mod_overlay_visible = false;
        m_f11_overlay_active = false;
    }
}

void SwordfareGUI::draw_graphics_options_modal(PostFXState& state) {
    if (!m_graphics_options_visible) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("  GRAPHICAL OPTIONS (F6)###GraphicsOptionsModal", &m_graphics_options_visible, flags)) {
        if (ImGui::BeginTabBar("GraphicsOptionsTabs", ImGuiTabBarFlags_None)) {
            // ==================== TAB 1: GRAPHICS PIPELINE ====================
            if (ImGui::BeginTabItem(ICON_FA_GEAR "  Graphics Pipeline")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Engine Rendering Pipeline (OpenGL 4.30+ Hardware)");
                ImGui::TextDisabled("Physical GPU passes: PBR GGX, Procedural Detail Injection (PDI), ASSAO, Volumetric Rays & Bloom.");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Active Tier: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "%s", graphics_tier_name(state.graphics_tier));
                ImGui::Spacing();

                const char* tier_labels[] = { "Low", "Medium", "High", "SW+", "SW+ High" };
                for (int t = 0; t < (int)GraphicsTier::COUNT; t++) {
                    bool sel = ((int)state.graphics_tier == t);
                    if (t > 0) ImGui::SameLine(0, 8);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.48f, 0.24f, 1.0f));
                    ImGui::PushID(500 + t);
                    if (ImGui::Button(tier_labels[t], ImVec2(130, 36))) {
                        // graphics_apply_tier() deliberately neutralises the
                        // cosmetic overlay flags (it must not inherit, say, a
                        // leftover Noir vignette), so re-apply the user's chosen
                        // filter right after to preserve the two-axis
                        // composition this modal advertises: tier x filter.
                        graphics_apply_tier(state, (GraphicsTier)t);
                        postfx_apply_filter(state, state.postfx_filter, state.postfx_filter_intensity);
                    }
                    ImGui::PopID();
                    if (sel) ImGui::PopStyleColor();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Hardware Features & Advanced Passes:");

                ImGui::Columns(2, "GfxColumns", false);

                // Column 1: Core Physical Shaders & PDI
                ImGui::Checkbox("Cook-Torrance GGX PBR", &state.pbr_enabled);
                ImGui::Checkbox("Procedural Detail Injection (PDI)", &state.procedural_detail);
                if (state.procedural_detail) {
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("PDI Strength##pdi", &state.procedural_strength, 0.0f, 2.0f, "Scale %.2f");
                }
                ImGui::Checkbox("Water Wave Reflections", &state.reflections_enabled);
                if (state.reflections_enabled) {
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("Reflect Strength##refl", &state.reflection_intensity, 0.0f, 1.0f, "Blend %.2f");
                }
                ImGui::Checkbox("Contact Shadows", &state.shadows);
                if (state.shadows) {
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("Shadow Darkness##shd", &state.shadow_intensity, 0.0f, 1.0f, "Dark %.2f");
                }

                ImGui::NextColumn();

                // Column 2: Ambient Occlusion, Rays, Bloom
                ImGui::Checkbox("Godot ASSAO (Ambient Occlusion)", &state.ssao);
                if (state.ssao) {
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("AO Intensity##ssao_i", &state.ssao_intensity, 0.0f, 2.5f, "Inten %.2f");
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("AO Radius##ssao_r", &state.ssao_radius, 0.005f, 0.08f, "Radius %.3f");
                }
                ImGui::Checkbox("3D Volumetric God Rays (SSVR)", &state.god_rays);
                if (state.god_rays) {
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("Ray Intensity##gri", &state.god_rays_intensity, 0.0f, 1.5f, "Inten %.2f");
                }
                ImGui::Checkbox("Dual-Kawase Bloom Pyramid", &state.bloom);
                if (state.bloom) {
                    ImGui::SetNextItemWidth(170);
                    ImGui::SliderFloat("Bloom Intensity##blmi", &state.bloom_intensity, 0.0f, 1.5f, "Glow %.2f");

                    // ============ BEGIN bloom-emitter-key controls ============
                    // Removable: delete this block, the four bloom_emitter_* fields in
                    // PostFXState, the uniform upload in fbo_scaler.cpp and the gate in
                    // src/shaders/graphics/bloom_extract.glsl to go back to a plain
                    // luminance cutoff for bloom. Nothing else reads any of them.
                    ImGui::Checkbox("Key bloom to authored emitters##blmk",
                                    &state.bloom_emitter_key);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Bloom only the game's own light sources: the torch (1.00,0.48,0.00),\n"
                            "this biome's lantern pods and lava (Fire), matched on the authored\n"
                            "colour the level data records, and required to be brighter than\n"
                            "their own neighbourhood.\n\n"
                            "Off = the legacy single luminance cutoff, which also blooms flat\n"
                            "bright surfaces such as sky and ice.");
                    }
                    if (state.bloom_emitter_key) {
                        ImGui::SetNextItemWidth(170);
                        ImGui::SliderFloat("Emitter Level Floor##blml", &state.bloom_emitter_luma,
                                           0.0f, 0.6f, "Floor %.2f");
                        ImGui::SetNextItemWidth(170);
                        ImGui::SliderFloat("Emitter Saturation##blms", &state.bloom_emitter_sat,
                                           0.0f, 1.5f, "Sat %.2f");
                        ImGui::SetNextItemWidth(170);
                        ImGui::SliderFloat("Emitter Local Contrast##blmc", &state.bloom_emitter_contrast,
                                           1.0f, 2.0f, "Contrast %.2f");
                        ImGui::SetNextItemWidth(170);
                        ImGui::SliderFloat("Emitter Gain##blmg", &state.bloom_emitter_gain,
                                           0.0f, 3.0f, "Gain %.2f");
                    } else {
                        ImGui::SetNextItemWidth(170);
                        ImGui::SliderFloat("Bloom Threshold##blmt", &state.bloom_threshold, 0.5f, 1.0f, "Cutoff %.2f");
                    }
                    // ============= END bloom-emitter-key controls =============
                }

                ImGui::Columns(1);
                ImGui::EndTabItem();
            }

            // ==================== TAB 2: STYLISTIC POST-EFFECTS ====================
            if (ImGui::BeginTabItem(ICON_FA_WAND_SPARKLES "  Post-Effects")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "Cosmetic Screen Filters (Zero Game Impact)");
                ImGui::TextDisabled("Artistic lens overlays applied on the backbuffer irrespective of your active graphics tier.");
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Active Filter: ");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 0.85f, 1.0f, 1.0f), "%s", postfx_filter_name(state.postfx_filter));
                ImGui::Spacing();

                const char* filter_labels[] = {
                    "None (Off)", "Retro TV (CRT)", "Mild Comic Outline",
                    "Heavy Anime Outline", "VHS Glitch", "Vignette & Film Grain"
                };

                for (int f = 0; f < (int)PostEffectFilter::COUNT; f++) {
                    bool sel = ((int)state.postfx_filter == f);
                    if (f % 3 != 0) ImGui::SameLine(0, 10);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.38f, 0.58f, 1.0f));
                    ImGui::PushID(600 + f);
                    if (ImGui::Button(filter_labels[f], ImVec2(228, 36))) {
                        postfx_apply_filter(state, (PostEffectFilter)f, state.postfx_filter_intensity);
                    }
                    ImGui::PopID();
                    if (sel) ImGui::PopStyleColor();
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Filter Global Parameters:");
                ImGui::SetNextItemWidth(300);
                if (ImGui::SliderFloat("Filter Intensity##f_inten", &state.postfx_filter_intensity, 0.0f, 2.0f, "Strength %.2f")) {
                    postfx_apply_filter(state, state.postfx_filter, state.postfx_filter_intensity);
                }

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), ICON_FA_CIRCLE_CHECK " Post-effects blend flawlessly with Low, Medium, High, SW+, and SW+ High.");
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

bool SwordfareGUI::is_input_blocked(float mx, float my) {
    if (!m_initialized) return false;
    
    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    // 1. The control-center toolbox or graphics options modal owns the display
    if (m_mod_overlay_visible || m_graphics_options_visible) return true;

    // 1b. The standalone Memory Research console is inset by 18px.
    if (m_research_overlay_visible) {
        const float inset = 18.0f;
        if (mx >= inset && mx <= win_w - inset && my >= inset && my <= win_h - inset)
            return true;
    }

    if (!m_last_buttons_ptr || m_buttons_globally_hidden) return false;

    extern int GAME_W, GAME_H;
    float game_asp = (float)GAME_W / (float)GAME_H;
    float win_asp  = win_w / win_h;
    float vp_x = 0, vp_y = 0, vp_w = win_w, vp_h = win_h;
    
    if (win_asp > game_asp) {
        vp_w = win_h * game_asp;
        vp_x = (win_w - vp_w) / 2;
    } else {
        vp_h = win_w / game_asp;
        vp_y = (win_h - vp_h) / 2;
    }
    
    SreOverlaySlot* overlays = static_cast<SreOverlaySlot*>(m_last_overlays_ptr);
    SreBtnSlot* buttons = static_cast<SreBtnSlot*>(m_last_buttons_ptr);

    // 2. Block clicks inside any active overlay background region
    if (overlays) {
        for (int i = 0; i < SRE_OVERLAY_MAX; i++) {
            SreOverlaySlot& ovr = overlays[i];
            if (!ovr.active || ovr.hidden) continue;
            float ow = vp_w * ovr.w;
            float oh = vp_h * ovr.h;
            float ox = vp_x + vp_w * ovr.x - ow / 2.0f;
            float oy = vp_y + vp_h * ovr.y - oh / 2.0f;
            if (mx >= ox && mx <= ox + ow && my >= oy && my <= oy + oh) {
                return true; // Click is inside an overlay panel — always block
            }
        }
    }

    // 3. Check if coords hit any active clickable button
    if (buttons) {
        for (int i = 0; i < SRE_BTN_MAX; i++) {
            SreBtnSlot& btn = buttons[i];
            if (!btn.active || btn.hidden || !btn.clickable) continue;

            // Resolve parent overlay
            SreOverlaySlot* parent_ovr = nullptr;
            if (btn.overlay_id[0] != '\0' && overlays) {
                for (int o = 0; o < SRE_OVERLAY_MAX; o++) {
                    if (overlays[o].active && strcmp(overlays[o].id, btn.overlay_id) == 0) {
                        parent_ovr = &overlays[o];
                        break;
                    }
                }
                if (parent_ovr && (parent_ovr->hidden || !parent_ovr->active)) continue;
            }

            float square_base = std::min(vp_w, vp_h);
            float pw = square_base * btn.w * btn.scale_x;
            float ph = square_base * btn.h * btn.scale_y;
            float bx, by;

            if (parent_ovr) {
                float ow = vp_w * parent_ovr->w;
                float oh = vp_h * parent_ovr->h;
                float ox = vp_x + vp_w * parent_ovr->x - ow / 2.0f;
                float oy = vp_y + vp_h * parent_ovr->y - oh / 2.0f;
                bx = ox + ow * btn.cur_x - pw / 2.0f;
                by = oy + oh * btn.cur_y - ph / 2.0f;
            } else {
                bx = vp_x + vp_w * btn.cur_x - pw / 2.0f;
                by = vp_y + vp_h * btn.cur_y - ph / 2.0f;
            }

            if (mx >= bx && mx <= bx + pw && my >= by && my <= by + ph) {
                return true;
            }
        }
    }
    
    return false;
}

extern SrtOverlay g_srt_overlay;

void SwordfareGUI::scan_saves(const std::string& save_dir) {
    g_srt_overlay.save_dir = get_vfs_save_dir(save_dir);
    g_srt_overlay.scan_saves();
    m_save_files = g_srt_overlay.save_files;
}

bool SwordfareGUI::load_save(const std::string& path) {
    bool ok = g_srt_overlay.load_save(path);
    if (ok) {
        m_inventory = g_srt_overlay.inventory;
        m_inventory_dirty = false;
    }
    return ok;
}

bool SwordfareGUI::write_save(const std::string& path) {
    g_srt_overlay.inventory = m_inventory;
    bool ok = g_srt_overlay.write_save(path);
    if (ok) {
        m_inventory_dirty = false;
    }
    return ok;
}

// ============================================================================
//  Swordfare GUI — Remastered Command Overlay (v2)
// ----------------------------------------------------------------------------
//  Drop-in replacement for SwordfareGUI::draw_control_panel().
//
//  WHAT CHANGED VS THE OLD VERSION
//  --------------------------------
//  1) Bar now docks to the TOP of the screen, not the bottom.
//  2) The old code pushed one single Header/HeaderHovered color pair before
//     Begin() and left it active for the *entire* window — every top-level
//     menu button and every item inside every dropdown shared the exact same
//     solid, fully-opaque highlight block. That's why clicking one button
//     made the whole section look "selected": there was nothing visually
//     distinguishing hover-state from open-state from click-state, they all
//     rendered the same full-width solid rectangle.
//     Fixed by: giving each state its own color (subtle hover tint, a
//     slightly stronger but still translucent active tint, and an underline
//     accent for the currently-open menu) instead of one flat opaque block.
//  3) Split into a real header bar (branding + top-level menus + status +
//     close) plus dedicated modal-style panels for About / Help, so the bar
//     itself stays razor-thin and everything heavy lives in popups.
//  4) All existing GuiAction values are preserved 1:1 — no gameplay-facing
//     capability was removed, only reorganized and restyled. Nothing new
//     was added to the GuiAction enum.
//
//  HEADER FILE — please add these two members to the SwordfareGUI class:
//      bool m_show_about = false;
//      bool m_show_help  = false;
//  (They're purely UI state for the new About/Help panels below and don't
//  need a GuiAction — the panels are opened/closed entirely inside this file.)
//
//  Everything else (m_initialized, m_imgui_ctx, m_font_button, GuiRenderer,
//  g_game_paused, GuiAction enum) is used exactly as it was declared before.
// ============================================================================

#include "imgui.h"
#include "imgui_internal.h"   // only used for ImGui::PushItemFlag-style tweaks; drop if you don't have it

// ---------------------------------------------------------------------------
// Brand palette — lifted from the project's own README badges (#00e5ff cyan,
// #8b3dff purple) so the overlay visually matches the rest of the project.
// ---------------------------------------------------------------------------
// NOTE: these are now aliases into the Xpera palette, so every consumer of
// this namespace (About / Help / the F1 bar) is re-skinned automatically. The
// legacy hard-coded brand values were retired in the Xpera migration.
namespace SwordfareTheme {
    inline const ImVec4 kBg          = xpera::palette.void_bg;
    inline const ImVec4 kBgPopup     = xpera::palette.overlay;
    inline const ImVec4 kBorder      = xpera::palette.border;
    inline const ImVec4 kCyan        = xpera::palette.accent;
    inline const ImVec4 kPurple      = xpera::palette.secondary;
    inline const ImVec4 kText        = xpera::palette.text_hi;
    inline const ImVec4 kTextDim     = xpera::palette.text_mid;
    inline const ImVec4 kHoverTint   = xpera::palette.elevated;
    inline const ImVec4 kActiveTint  = xpera::palette.accent_soft;
    inline const ImVec4 kDanger      = xpera::palette.danger;
    inline const ImVec4 kDangerHover = xpera::palette.danger;
    inline const ImVec4 kOk          = xpera::palette.success;
}

// Small helper: draws a 1px cyan->purple gradient line under a rect, used
// under the whole bar and under the active menu to give it a "current tab"
// accent instead of a filled highlight block.
static void DrawAccentUnderline(ImVec2 p0, ImVec2 p1) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 c0 = ImGui::ColorConvertFloat4ToU32(xpera::palette.accent);
    ImU32 c1 = ImGui::ColorConvertFloat4ToU32(xpera::palette.secondary);
    dl->AddLine(p0, ImVec2(p1.x, p0.y), c0, 1.5f);
    dl->AddRectFilledMultiColor(p0, p1, c0, c1, c1, c0);
}

// ============================================================================
//  TOP HEADER BAR  —  File · Tools · Cheats · Speed · Camera · Audio · About · Help
// ============================================================================
GuiAction SwordfareGUI::draw_control_panel(bool* p_open) {
    if (!m_initialized || !*p_open) return GUI_NONE;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    GuiAction action = GUI_NONE;
    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    extern GuiRenderer g_gui;
    extern bool g_game_paused;
    extern const char* mod_speed_label();

    float scale = win_h / 720.0f;
    if (scale < 1.0f) scale = 1.0f;

    // Modern clean dark charcoal palette
    ImVec4 kBg          = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    ImVec4 kBgPopup     = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
    ImVec4 kBorder      = ImVec4(0.20f, 0.22f, 0.26f, 0.50f);
    ImVec4 kText        = ImVec4(0.85f, 0.88f, 0.92f, 1.00f);
    ImVec4 kCyan        = xpera::palette.accent;             // Xpera indigo accent

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, 3.5f * scale)); // reduced height and padding
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f * scale, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f);

    ImGui::PushStyleColor(ImGuiCol_MenuBarBg,     kBg);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       kBgPopup);
    ImGui::PushStyleColor(ImGuiCol_Border,        kBorder);
    ImGui::PushStyleColor(ImGuiCol_Text,          kText);
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.00f, 0.55f, 1.00f, 0.20f)); // active tint
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.00f, 0.55f, 1.00f, 0.35f)); // hover tint
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.00f, 0.55f, 1.00f, 0.50f));

    ImFont* bar_font = static_cast<ImFont*>(m_font_button);
    float font_size = 9.5f * scale; // compact font size
    if (bar_font) ImGui::PushFont(bar_font);

    float bar_h = 24.0f * scale;

    if (ImGui::BeginMainMenuBar()) {
        bar_h = ImGui::GetWindowHeight();

        auto menu = [&](const char* label, auto fn) {
            if (ImGui::BeginMenu(label)) {
                fn();
                ImGui::EndMenu();
            }
        };

        // ── File ─────────────────────────────────────────────────────────
        menu("File", [&]() {
            if (ImGui::MenuItem("Save State"))        action = GUI_SAVE_STATE;
            if (ImGui::MenuItem("Load State"))        action = GUI_LOAD_STATE;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit Game"))         action = GUI_EXIT;
        });

        // ── Emulation ────────────────────────────────────────────────────
        menu("Emulation", [&]() {
            if (ImGui::MenuItem(g_game_paused ? "Resume" : "Pause", "F8"))
                action = GUI_PAUSE;
            ImGui::Separator();
            if (ImGui::MenuItem("Speed Up",   "+")) action = GUI_GAME_SPEED_UP;
            if (ImGui::MenuItem("Speed Down", "-")) action = GUI_GAME_SPEED_DOWN;
            if (ImGui::MenuItem("Speed Reset","0")) action = GUI_GAME_SPEED_RESET;
        });

        // ── Config ───────────────────────────────────────────────────────
        menu("Config", [&]() {
            if (ImGui::MenuItem("Customize Controls", "F2")) action = GUI_CUSTOMIZE_CONTROLS;
        });

        // ── Mods ─────────────────────────────────────────────────────────
        menu("Mods", [&]() {
            ImGui::TextDisabled("CHEAT TOGGLES");
            ImGui::Separator();
            // Provenance is per-control. Offsets cross-checked against
            // docs/sre13/HealthComponent.md; the rest are unverified heuristics.
            ImGui::MenuItem("God Mode       (verified)", nullptr, &g_gui.mod_god_mode);
            ImGui::TextDisabled("    HealthComponent currentHealth @ +0x78");
            ImGui::MenuItem("Infinite Mana  (partial)", nullptr, &g_gui.mod_infinite_mana);
            ImGui::TextDisabled("    ManaComponent +0x40 = max (+0x3C)");
            ImGui::MenuItem("Infinite Jump  (partial)", nullptr, &g_gui.mod_infinite_jump);
            ImGui::TextDisabled("    clears CharController air-jump count @ +0x158");
            {
                bool fly_dummy  = false;
                bool coin_dummy = false;
                ImGui::BeginDisabled();
                ImGui::MenuItem("Fly Mode       (not implemented)", nullptr, &fly_dummy);
                ImGui::MenuItem("Coin Break     (not implemented)", nullptr, &coin_dummy);
                ImGui::EndDisabled();
            }
            
            ImGui::Separator();
            
            if (ImGui::BeginMenu("Stats Editor")) {
                if (ImGui::MenuItem("Heal Full HP"))  action = GUI_MOD_HEAL_FULL;
                if (ImGui::MenuItem("Refill Mana"))   action = GUI_MOD_REFILL_MANA;
                if (ImGui::MenuItem("+100 Coins"))    action = GUI_MOD_ADD_COINS;
                ImGui::Separator();
                if (ImGui::MenuItem("Level Up (+1)")) action = GUI_MOD_LEVEL_UP;
                if (ImGui::MenuItem("Level Down (-1)")) action = GUI_MOD_LEVEL_DOWN;
                if (ImGui::MenuItem("XP +500"))       action = GUI_MOD_EXP_UP;
                if (ImGui::MenuItem("XP -500"))       action = GUI_MOD_EXP_DOWN;
                ImGui::EndMenu();
            }
            
            if (ImGui::BeginMenu("Physics Editor")) {
                ImGui::SetNextItemWidth(120.0f * scale);
                ImGui::SliderFloat("Walk Speed", &g_gui.mod_walk_speed,  0.5f, 10.0f, "%.1f");
                ImGui::SetNextItemWidth(120.0f * scale);
                ImGui::SliderFloat("Run Speed",  &g_gui.mod_run_speed,   0.5f, 10.0f, "%.1f");
                ImGui::SetNextItemWidth(120.0f * scale);
                ImGui::SliderFloat("Jump Height", &g_gui.mod_jump_height, 0.5f, 10.0f, "%.1f");
                ImGui::EndMenu();
            }
        });

        // ── Settings ─────────────────────────────────────────────────────
        menu("Settings", [&]() {
            if (ImGui::MenuItem("Toggle Cam Override", "F5")) action = GUI_TOGGLE_CAM;
            if (ImGui::MenuItem("Toggle Smooth Cam"))          action = GUI_TOGGLE_SMOOTH_CAM;
            ImGui::Separator();
            if (ImGui::MenuItem("Mute Music"))     action = GUI_MUSIC_MUTE;
            if (ImGui::MenuItem("Volume Up"))      action = GUI_MUSIC_VOL_UP;
            if (ImGui::MenuItem("Volume Down"))    action = GUI_MUSIC_VOL_DOWN;
        });

        // ── Developer ────────────────────────────────────────────────────
        menu("Developer", [&]() {
            ImGui::TextDisabled("CAVER ENGINE HUD & TOOLS");
            ImGui::Separator();
            if (ImGui::MenuItem("Touch Foo HUD (FPS/VRAM)", "Ctrl+F3"))   action = GUI_TOGGLE_TOUCHFOO_DEBUG_INFO;
            if (ImGui::MenuItem("Collision Wireframes",      "Ctrl+Shift+D")) action = GUI_TOGGLE_COLLISION_SHAPES;
            if (ImGui::MenuItem("Combat Hitboxes",          "Ctrl+Shift+W")) action = GUI_TOGGLE_COMBAT_WIREFRAME;
        });

        // ── Help ─────────────────────────────────────────────────────────
        menu("Help", [&]() {
            if (ImGui::MenuItem("Help / Hotkeys")) m_show_help = true;
            if (ImGui::MenuItem("About Mod"))      m_show_about = true;
        });

        // ── Right-aligned status + branding ──────────────────────────────
        {
            const char* st_lbl  = g_game_paused ? "PAUSED" : mod_speed_label();
            ImVec4      st_col  = g_game_paused ? xpera::palette.danger : xpera::palette.success;
            const char* sep     = "   \xC2\xB7   ";
            const char* brand   = "SWORDFARE";
            const float total   = ImGui::CalcTextSize(st_lbl).x
                                + ImGui::CalcTextSize(sep).x
                                + ImGui::CalcTextSize(brand).x;
            const float brand_x = win_w - total - 14.0f * scale;
            if (brand_x > 0.0f) {
                ImGui::SameLine(brand_x);
                ImGui::TextColored(st_col, "%s", st_lbl);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(xpera::palette.text_lo, "%s", sep);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(kCyan, "%s", brand);
            }
        }

        // Cyan\u2192purple accent under the whole bar.
        {
            const float bar_bottom = ImGui::GetWindowHeight() - 1.0f;
            DrawAccentUnderline(ImVec2(0.0f, bar_bottom), ImVec2(win_w, bar_bottom));
        }

        ImGui::EndMainMenuBar();
    }

    if (bar_font) ImGui::PopFont();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);

    // Draw the About/Help panels (self-contained, opened via the buttons above)
    if (m_show_about) draw_about_panel(&m_show_about, bar_h);
    if (m_show_help)  draw_help_panel(&m_show_help, bar_h);

    return action;
}



void SwordfareGUI::draw_lua_script_editor() {
    if (!m_initialized || !m_script_editor_open) return;

    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x;
    float H = io.DisplaySize.y;
    float editor_w = W * 0.7f;
    float editor_h = H * 0.6f;

    ImGui::SetNextWindowPos(ImVec2(W * 0.15f, H * 0.15f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(editor_w, editor_h),  ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.98f);

    // Styling matches the console
    const ImVec4 col_bg        = xpera::palette.base;
    const ImVec4 col_border    = xpera::palette.border;
    const ImVec4 col_input_bg  = xpera::palette.input;
    const ImVec4 col_accent    = xpera::palette.accent;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, col_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, col_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));

    bool pushed_mono = false;
    if (m_font_mono) { ImGui::PushFont((ImFont*)m_font_mono); pushed_mono = true; }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin(ICON_FA_CODE " Lua Script Editor", &m_script_editor_open, flags)) {
        
        // ── Top Bar ────────────────────────────────────────────────────────
        ImGui::TextColored(col_accent, ICON_FA_FILE " Filename:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(editor_w - 300.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, col_input_bg);
        ImGui::InputText("##filename", m_script_editor_name, sizeof(m_script_editor_name));
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextColored(ImVec4(m_script_editor_status_color[0], m_script_editor_status_color[1], m_script_editor_status_color[2], m_script_editor_status_color[3]), "[ %s ]", m_script_editor_status.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Editor ─────────────────────────────────────────────────────────
        float content_avail_y = ImGui::GetContentRegionAvail().y;
        float footer_h = ImGui::GetFrameHeightWithSpacing() + 16.0f; // extra padding to stop clipping
        float editor_area_h = content_avail_y - footer_h;

        ImGui::PushStyleColor(ImGuiCol_FrameBg, col_input_bg);
        ImGui::InputTextMultiline("##source", m_script_editor_buf, sizeof(m_script_editor_buf), 
                                  ImVec2(-FLT_MIN, editor_area_h), 
                                  ImGuiInputTextFlags_AllowTabInput);
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Footer ─────────────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.36f, 0.72f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.95f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.22f, 0.52f, 1.00f));
        
        if (ImGui::Button(" " ICON_FA_FLOPPY_DISK " Save Script ")) {
            std::string home = os_external::home_dir();
            if (!home.empty()) {
                std::filesystem::path save_dir = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "lua_scripts";
                std::error_code ec;
                std::filesystem::create_directories(save_dir, ec);
                
                std::filesystem::path file_path = save_dir / m_script_editor_name;
                std::ofstream out(file_path);
                if (out.is_open()) {
                    out << m_script_editor_buf;
                    out.close();
                    m_script_editor_status = "Saved OK";
                    m_script_editor_status_color[0] = 0.2f; m_script_editor_status_color[1] = 0.9f; m_script_editor_status_color[2] = 0.2f;
                } else {
                    m_script_editor_status = "Save Failed!";
                    m_script_editor_status_color[0] = 0.9f; m_script_editor_status_color[1] = 0.2f; m_script_editor_status_color[2] = 0.2f;
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(" " ICON_FA_CIRCLE_CHECK " Validate & Save ")) {
            // Heuristic syntax validator
            int brackets_sq = 0, brackets_curl = 0, brackets_paren = 0;
            int quotes_single = 0, quotes_double = 0;
            bool in_comment = false;
            
            std::string code = m_script_editor_buf;
            for (size_t i = 0; i < code.length(); i++) {
                if (in_comment && code[i] == '\n') in_comment = false;
                if (!in_comment && i + 1 < code.length() && code[i] == '-' && code[i+1] == '-') { in_comment = true; i++; continue; }
                if (in_comment) continue;

                if (code[i] == '[') brackets_sq++;
                if (code[i] == ']') brackets_sq--;
                if (code[i] == '{') brackets_curl++;
                if (code[i] == '}') brackets_curl--;
                if (code[i] == '(') brackets_paren++;
                if (code[i] == ')') brackets_paren--;
                if (code[i] == '\'') quotes_single++;
                if (code[i] == '"') quotes_double++;
            }

            if (brackets_sq != 0 || brackets_curl != 0 || brackets_paren != 0 || 
               (quotes_single % 2) != 0 || (quotes_double % 2) != 0) {
                m_script_editor_status = "Syntax Error: Unmatched Bracket/Quote";
                m_script_editor_status_color[0] = 0.9f; m_script_editor_status_color[1] = 0.2f; m_script_editor_status_color[2] = 0.2f;
            } else {
                // Syntax OK, proceed to save
                std::string home = os_external::home_dir();
                if (!home.empty()) {
                    std::filesystem::path save_dir = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "lua_scripts";
                    std::error_code ec;
                    std::filesystem::create_directories(save_dir, ec);
                    std::ofstream out(save_dir / m_script_editor_name);
                    if (out.is_open()) {
                        out << m_script_editor_buf;
                        out.close();
                        m_script_editor_status = "Validated & Saved";
                        m_script_editor_status_color[0] = 0.2f; m_script_editor_status_color[1] = 0.9f; m_script_editor_status_color[2] = 0.9f;
                    }
                }
            }
        }

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.72f, 0.36f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.95f, 0.50f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.52f, 0.22f, 1.00f));
        if (ImGui::Button(" " ICON_FA_PLAY " Run ")) {
            console_submit(m_script_editor_buf);
            m_script_editor_status = "Executed";
            m_script_editor_status_color[0] = 0.2f; m_script_editor_status_color[1] = 0.9f; m_script_editor_status_color[2] = 0.9f;
        }
        ImGui::PopStyleColor(3);
        
        ImGui::PopStyleColor(3);

        ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
        if (ImGui::Button(" " ICON_FA_XMARK " Close ")) {
            m_script_editor_open = false;
        }

    }
    ImGui::End();

    if (pushed_mono) ImGui::PopFont();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void SwordfareGUI::draw_lua_script_manager() {
    if (!m_initialized || !m_script_manager_open) return;

    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x;
    float H = io.DisplaySize.y;
    
    ImGui::SetNextWindowPos(ImVec2(W * 0.15f, H * 0.15f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(W * 0.4f, H * 0.6f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.98f);

    const ImVec4 col_bg        = xpera::palette.base;
    const ImVec4 col_border    = xpera::palette.border;
    const ImVec4 col_accent    = xpera::palette.accent;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, col_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, col_border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));

    if (ImGui::Begin(ICON_FA_FILE " Script Manager", &m_script_manager_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        
        if (ImGui::Button("Refresh")) {
            m_script_list.clear();
            std::string home = os_external::home_dir();
            if (!home.empty()) {
                std::filesystem::path save_dir = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "lua_scripts";
                std::error_code ec;
                if (std::filesystem::exists(save_dir, ec)) {
                    for (const auto& entry : std::filesystem::directory_iterator(save_dir, ec)) {
                        if (entry.path().extension() == ".lua") {
                            // Run validation
                            std::ifstream in(entry.path());
                            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                            
                            int brackets_sq = 0, brackets_curl = 0, brackets_paren = 0;
                            int quotes_single = 0, quotes_double = 0;
                            bool in_comment = false;
                            
                            for (size_t i = 0; i < content.length(); i++) {
                                if (in_comment && content[i] == '\n') in_comment = false;
                                if (!in_comment && i + 1 < content.length() && content[i] == '-' && content[i+1] == '-') { in_comment = true; i++; continue; }
                                if (in_comment) continue;
                                if (content[i] == '[') brackets_sq++;
                                if (content[i] == ']') brackets_sq--;
                                if (content[i] == '{') brackets_curl++;
                                if (content[i] == '}') brackets_curl--;
                                if (content[i] == '(') brackets_paren++;
                                if (content[i] == ')') brackets_paren--;
                                if (content[i] == '\'') quotes_single++;
                                if (content[i] == '"') quotes_double++;
                            }
                            
                            bool valid = (brackets_sq == 0 && brackets_curl == 0 && brackets_paren == 0 && (quotes_single % 2) == 0 && (quotes_double % 2) == 0);
                            m_script_list.push_back({entry.path().filename().string(), valid});
                        }
                    }
                }
            }
        }
        
        ImGui::SameLine();
        ImGui::TextColored(col_accent, " %d scripts found", (int)m_script_list.size());
        
        ImGui::Separator();
        
        ImGui::BeginChild("##scriptlist", ImVec2(0, 0), true);
        for (const auto& script : m_script_list) {
            ImGui::PushID(script.filename.c_str());
            if (script.valid) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), ICON_FA_CIRCLE_CHECK " Passed");
            } else {
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), ICON_FA_CIRCLE_XMARK " Error ");
            }
            ImGui::SameLine();
            ImGui::Text("%s", script.filename.c_str());
            ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
            
            if (ImGui::Button("Edit")) {
                std::string home = os_external::home_dir();
                if (!home.empty()) {
                    std::filesystem::path file_path = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "lua_scripts" / script.filename;
                    std::ifstream in(file_path);
                    if (in.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        strncpy(m_script_editor_name, script.filename.c_str(), sizeof(m_script_editor_name)-1);
                        strncpy(m_script_editor_buf, content.c_str(), sizeof(m_script_editor_buf)-1);
                        m_script_editor_open = true;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Run")) {
                std::string home = os_external::home_dir();
                if (!home.empty()) {
                    std::filesystem::path file_path = std::filesystem::path(home) / ".local" / "share" / "swordigo-desktop" / "lua_scripts" / script.filename;
                    std::ifstream in(file_path);
                    if (in.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        console_submit(content);
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// ============================================================================
//  ABOUT PANEL — pulled from the project README, written up fresh for the UI
// ============================================================================
void SwordfareGUI::draw_about_panel(bool* p_open, float top_offset) {
    using namespace SwordfareTheme;
    ImGui::SetNextWindowPos(ImVec2(60.0f, top_offset + 30.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgPopup);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kBg);

    if (ImGui::Begin("About Swordigo Desktop", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(kCyan, "Swordigo Desktop");
        ImGui::TextColored(kTextDim, "The Swordigo Runtime (SRT) — v7.3 \"Combatch\"");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "A native Linux port of the mobile action-adventure platformer, "
            "built around a layered runtime that treats the original ARM "
            "game binary as a gameplay kernel while progressively swapping "
            "its subsystems for clean, native C reimplementations.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(kCyan, "This overlay");
        ImGui::BulletText("Rendering, music, HUD, backgrounds and save/load");
        ImGui::BulletText("are handled by libsre.so, a from-scratch native");
        ImGui::BulletText("subsystem layer sitting above the original binary.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(kCyan, "Core Team");
        ImGui::BulletText("Lead Developer — TheMegineBraine");
        ImGui::BulletText("Developers — TheCorrectSynovian, MrSinup, X Dukinja");
        ImGui::BulletText("Designer — ETPV");
        ImGui::Spacing();
        ImGui::TextColored(kTextDim,
            "Swordigo is (c) Ville Makynen / Touch Foo. This project ships");
        ImGui::TextColored(kTextDim,
            "no original assets or binaries and is a research/preservation");
        ImGui::TextColored(kTextDim, "effort only.");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(90, 28))) *p_open = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

// ============================================================================
//  HELP PANEL — hotkey reference
// ============================================================================
void SwordfareGUI::draw_help_panel(bool* p_open, float top_offset) {
    using namespace SwordfareTheme;
    ImGui::SetNextWindowPos(ImVec2(560.0f, top_offset + 30.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 380.0f), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgPopup);
    ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kBg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kBg);

    static const struct { const char* key; const char* desc; } kHotkeys[] = {
        {"F1",  "Toggle this header / command overlay"},
        {"F2",  "Controls Editor (drag to reposition buttons)"},
        {"F3",  "Debug overlay (FPS, draw calls, player stats)"},
        {"F4",  "Toggle the SRE mod hub"},
        {"F5",  "Camera override toggle"},
        {"F6",  "Cycle PostFX presets"},
        {"F7",  "Toggle video background playback"},
        {"F8",  "Pause / Resume"},
        {"F10", "Toggle native on-screen controls"},
        {"F11", "Scene Shifter and display toolbox"},
        {"F12",          "Fullscreen toggle"},
        {"\\",           "Toggle keyboard typing mode"},
        {"Ctrl+F3",      "Touch Foo Developer HUD (FPS/VRAM)"},
        {"Ctrl+Shift+D", "Toggle 3D collision wireframes"},
        {"Ctrl+Shift+W", "Toggle combat & attack hitboxes"},
    };

    if (ImGui::Begin("Hotkeys", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(kCyan, "Keyboard Shortcuts");
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::BeginTable("hotkeys_tbl", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (auto& hk : kHotkeys) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(kCyan, "%s", hk.key);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(kText, "%s", hk.desc);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(90, 28))) *p_open = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

/*
GuiAction SwordfareGUI::draw_control_panel(bool* p_open) {
    if (!m_initialized || !*p_open) return GUI_NONE;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    GuiAction action = GUI_NONE;
    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    extern GuiRenderer g_gui;
    extern bool g_game_paused;

    // Scale bar height with display size (32px logical minimum)
    float scale  = win_h / 544.0f;
    if (scale < 1.0f) scale = 1.0f;
    float bar_h  = 32.0f * scale;

    // ---------------------------------------------------------------
    // Bottom menu bar window (no title, no scrollbar, no resize)
    // ---------------------------------------------------------------
    ImGui::SetNextWindowPos(ImVec2(0.0f, win_h - bar_h));
    ImGui::SetNextWindowSize(ImVec2(win_w, bar_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * scale, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.063f, 0.090f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,  ImVec4(0.068f, 0.078f, 0.110f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.18f,  0.54f,  0.82f,  0.55f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.38f, 0.68f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.14f, 0.30f, 0.58f, 0.90f));

    // Scale font for this bar
    ImFont* bar_font = static_cast<ImFont*>(m_font_button);
    float font_size  = 13.0f * scale;
    if (bar_font) ImGui::PushFont(bar_font);

    ImGui::Begin("##sfw_menubar", nullptr,
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize     |
        ImGuiWindowFlags_NoMove        | ImGuiWindowFlags_NoScrollbar  |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Vertically center content in bar
    float text_h = font_size;
    float pad_y  = (bar_h - text_h) * 0.5f;
    ImGui::SetCursorPosY(pad_y > 0 ? pad_y : 2.0f);

    // Helper: begin a popup menu item in the bar
    auto bar_menu = [&](const char* label, auto fn) {
        if (ImGui::BeginMenu(label)) {
            fn();
            ImGui::EndMenu();
        }
    };

    // ── Game ──────────────────────────────────────────────────────
    bar_menu(g_game_paused ? "Game [PAUSED]" : "Game", [&]() {
        if (ImGui::MenuItem(g_game_paused ? "Resume" : "Pause", "F8"))
            action = GUI_PAUSE;
        ImGui::Separator();
        if (ImGui::MenuItem("Speed Up",   "+")) action = GUI_GAME_SPEED_UP;
        if (ImGui::MenuItem("Speed Down", "-")) action = GUI_GAME_SPEED_DOWN;
        if (ImGui::MenuItem("Speed Reset","0")) action = GUI_GAME_SPEED_RESET;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit Game"))       action = GUI_EXIT;
    });

    ImGui::SameLine();

    // ── Cheats ────────────────────────────────────────────────────
    bar_menu("Cheats", [&]() {
        ImGui::MenuItem(g_gui.mod_god_mode      ? "[ON]  God Mode"      : "[OFF] God Mode",      nullptr, &g_gui.mod_god_mode);
        ImGui::MenuItem(g_gui.mod_infinite_mana ? "[ON]  Infinite Mana" : "[OFF] Infinite Mana", nullptr, &g_gui.mod_infinite_mana);
        ImGui::MenuItem(g_gui.mod_fly_mode      ? "[ON]  Fly Mode"      : "[OFF] Fly Mode",      nullptr, &g_gui.mod_fly_mode);
        ImGui::MenuItem(g_gui.mod_infinite_jump ? "[ON]  Infinite Jump" : "[OFF] Infinite Jump", nullptr, &g_gui.mod_infinite_jump);
        ImGui::MenuItem(g_gui.mod_coin_break    ? "[ON]  Coin Break"    : "[OFF] Coin Break",    nullptr, &g_gui.mod_coin_break);
        ImGui::Separator();
        if (ImGui::MenuItem("Heal Full HP"))    action = GUI_MOD_HEAL_FULL;
        if (ImGui::MenuItem("Refill Mana"))     action = GUI_MOD_REFILL_MANA;
        if (ImGui::MenuItem("+100 Coins"))      action = GUI_MOD_ADD_COINS;
        ImGui::Separator();
        if (ImGui::MenuItem("Level +1"))        action = GUI_MOD_LEVEL_UP;
        if (ImGui::MenuItem("Level -1"))        action = GUI_MOD_LEVEL_DOWN;
        if (ImGui::MenuItem("XP +500"))         action = GUI_MOD_EXP_UP;
        if (ImGui::MenuItem("XP -500"))         action = GUI_MOD_EXP_DOWN;
    });

    ImGui::SameLine();

    // ── Speed ─────────────────────────────────────────────────────
    bar_menu("Speed", [&]() {
        ImGui::SetNextItemWidth(160.0f * scale);
        ImGui::SliderFloat("Walk", &g_gui.mod_walk_speed,   0.5f, 10.0f, "%.1f");
        ImGui::SetNextItemWidth(160.0f * scale);
        ImGui::SliderFloat("Run",  &g_gui.mod_run_speed,    0.5f, 10.0f, "%.1f");
        ImGui::SetNextItemWidth(160.0f * scale);
        ImGui::SliderFloat("Jump", &g_gui.mod_jump_height,  0.5f, 10.0f, "%.1f");
        ImGui::Separator();
        if (ImGui::MenuItem("Speed Up"))   action = GUI_GAME_SPEED_UP;
        if (ImGui::MenuItem("Speed Down")) action = GUI_GAME_SPEED_DOWN;
        if (ImGui::MenuItem("Speed Reset"))action = GUI_GAME_SPEED_RESET;
    });

    ImGui::SameLine();

    // ── Camera ────────────────────────────────────────────────────
    bar_menu("Camera", [&]() {
        if (ImGui::MenuItem("Toggle Cam Override", "F5"))  action = GUI_TOGGLE_CAM;
        if (ImGui::MenuItem("Toggle Smooth Cam"))          action = GUI_TOGGLE_SMOOTH_CAM;
    });

    ImGui::SameLine();

    // ── Audio ─────────────────────────────────────────────────────
    bar_menu("Audio", [&]() {
        if (ImGui::MenuItem("Mute / Unmute"))  action = GUI_MUSIC_MUTE;
        if (ImGui::MenuItem("Volume Up"))      action = GUI_MUSIC_VOL_UP;
        if (ImGui::MenuItem("Volume Down"))    action = GUI_MUSIC_VOL_DOWN;
    });

    ImGui::SameLine();

    // ── Controls ──────────────────────────────────────────────────
    bar_menu("Controls", [&]() {
        if (ImGui::MenuItem("Customize Controls", "F2")) action = GUI_CUSTOMIZE_CONTROLS;
        if (ImGui::MenuItem("Save State"))               action = GUI_SAVE_STATE;
        if (ImGui::MenuItem("Load State"))               action = GUI_LOAD_STATE;
    });

    // Right-aligned close button
    const char* close_lbl = " [F1] Close ";
    ImVec2 close_sz = ImGui::CalcTextSize(close_lbl);
    ImGui::SetCursorPosX(win_w - close_sz.x - 10.0f * scale);
    ImGui::SetCursorPosY(pad_y > 0 ? pad_y : 2.0f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.14f, 0.20f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.18f, 0.22f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.18f, 0.22f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.95f, 0.38f, 0.42f, 1.00f));
    if (ImGui::SmallButton(close_lbl)) *p_open = false;
    ImGui::PopStyleColor(4);

    ImGui::End();

    if (bar_font) ImGui::PopFont();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);

    return action;
}

GuiAction SwordfareGUI::draw_settings_panel(bool* p_open) {
    if (!m_initialized || !*p_open) return GUI_NONE;
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(m_imgui_ctx));

    GuiAction action = GUI_NONE;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(450.0f, 500.0f), ImGuiCond_FirstUseEver);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 15));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.086f, 0.106f, 0.133f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.54f, 0.82f, 0.60f)); // Blue theme for settings
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.086f, 0.106f, 0.133f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.086f, 0.106f, 0.133f, 1.0f));

    extern GuiRenderer g_gui;

    if (ImGui::Begin("Swordfare Control Panel (F1)", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.18f, 0.54f, 0.82f, 1.0f), "GAMEPLAY CHEATS");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("God Mode (Invincible)", &g_gui.mod_god_mode);
        ImGui::Checkbox("Infinite Mana", &g_gui.mod_infinite_mana);
        ImGui::Checkbox("Fly Mode (Noclip-ish)", &g_gui.mod_fly_mode);
        ImGui::Checkbox("Infinite Jump", &g_gui.mod_infinite_jump);
        ImGui::Checkbox("Coin Break (Insta-break objects)", &g_gui.mod_coin_break);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.18f, 0.54f, 0.82f, 1.0f), "MOVEMENT & PHYSICS MULTIPLIERS");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SliderFloat("Walk Speed", &g_gui.mod_walk_speed, 0.5f, 10.0f, "%.1f");
        ImGui::SliderFloat("Run Speed", &g_gui.mod_run_speed, 0.5f, 10.0f, "%.1f");
        ImGui::SliderFloat("Jump Height", &g_gui.mod_jump_height, 0.5f, 10.0f, "%.1f");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.18f, 0.54f, 0.82f, 1.0f), "QUICK ACTIONS");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Heal Full HP", ImVec2(130, 30))) action = GUI_MOD_HEAL_FULL;
        ImGui::SameLine();
        if (ImGui::Button("Refill Mana", ImVec2(130, 30))) action = GUI_MOD_REFILL_MANA;
        ImGui::SameLine();
        if (ImGui::Button("+100 Coins", ImVec2(130, 30))) action = GUI_MOD_ADD_COINS;

        ImGui::Spacing();
        if (ImGui::Button("Level Up (+1)", ImVec2(130, 30))) action = GUI_MOD_LEVEL_UP;
        ImGui::SameLine();
        if (ImGui::Button("Level Down (-1)", ImVec2(130, 30))) action = GUI_MOD_LEVEL_DOWN;

        ImGui::Spacing();
        if (ImGui::Button("XP +500", ImVec2(130, 30))) action = GUI_MOD_EXP_UP;
        ImGui::SameLine();
        if (ImGui::Button("XP -500", ImVec2(130, 30))) action = GUI_MOD_EXP_DOWN;

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.18f, 0.54f, 0.82f, 1.0f), "AUDIO & EMULATION SETTINGS");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Mute Music", ImVec2(130, 30))) action = GUI_MUSIC_MUTE;
        ImGui::SameLine();
        if (ImGui::Button("Volume Up", ImVec2(130, 30))) action = GUI_MUSIC_VOL_UP;
        ImGui::SameLine();
        if (ImGui::Button("Volume Down", ImVec2(130, 30))) action = GUI_MUSIC_VOL_DOWN;

        ImGui::Spacing();
        if (ImGui::Button("Pause/Resume Game", ImVec2(160, 30))) action = GUI_PAUSE;
        ImGui::SameLine();
        if (ImGui::Button("Customize Controls", ImVec2(160, 30))) action = GUI_CUSTOMIZE_CONTROLS;

        ImGui::Spacing();
        if (ImGui::Button("Toggle Cam Override", ImVec2(160, 30))) action = GUI_TOGGLE_CAM;
        ImGui::SameLine();
        if (ImGui::Button("Toggle Smooth Cam", ImVec2(160, 30))) action = GUI_TOGGLE_SMOOTH_CAM;

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.18f, 0.54f, 0.82f, 1.0f), "GAME SPEED");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Game Speed Up", ImVec2(130, 30))) action = GUI_GAME_SPEED_UP;
        ImGui::SameLine();
        if (ImGui::Button("Game Speed Down", ImVec2(130, 30))) action = GUI_GAME_SPEED_DOWN;
        ImGui::SameLine();
        if (ImGui::Button("Reset Speed", ImVec2(130, 30))) action = GUI_GAME_SPEED_RESET;

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Exit Game", ImVec2(100, 30))) action = GUI_EXIT;
    }
    ImGui::End();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);

    return action;
}
*/

// =============================================================================
// SwordfareGUI — Lua Console (ImGui-native, remastered)
// =============================================================================

void SwordfareGUI::init_lua_console(
    uint8_t* guest_memory,
    uint64_t buf_addr,   uint64_t result_addr,
    uint64_t pending_addr, uint64_t status_addr,
    uint64_t print_addr)
{
    m_guest_memory         = guest_memory;
    m_console_buf_addr     = buf_addr;
    m_console_result_addr  = result_addr;
    m_console_pending_addr = pending_addr;
    m_console_status_addr  = status_addr;
    m_console_print_addr   = print_addr;
    m_console_ready        = (buf_addr != 0);

    if (m_console_ready) {
        m_console_history.push_back({"Raijin  •  World's first Swordigo Lua console  •  Swordfare subsystem", false, false});
        m_console_history.push_back({"Type Lua and press Enter. Up/Down for history. Backtick (`) to close.", false, false});
    }
}

// ---------------------------------------------------------------------------
// SwordfareGUI::init_scene_shifter
// ---------------------------------------------------------------------------
// Wires up all scene shifter globals in guest memory.  After this call the
// GUI reads/writes those globals directly via (m_guest_memory + VA) instead
// of the always-zero host-side weak fallback copies.
//
// Also runs an initial host-side filesystem scan (std::filesystem) of
//   assets_dir/resources/  and, if present,  data_dir/mods/<mod>/resources/
// to populate g_sre_scene_list in guest memory so the panel shows scenes
// immediately without needing the guest scan function.
// ---------------------------------------------------------------------------
#include <filesystem>
void SwordfareGUI::init_scene_shifter(
    uint8_t* guest_memory,
    uint64_t scene_list_count_va,
    uint64_t scene_list_va,
    uint64_t shift_pending_va,
    uint64_t shift_target_va,
    uint64_t shift_spawn_va,
    uint64_t shift_error_va,
    uint64_t shift_active_va,
    uint64_t current_scene_va,
    const std::string& assets_dir)
{
    namespace fs = std::filesystem;

    // Store guest memory base (may already be set by init_lua_console)
    if (!m_guest_memory) m_guest_memory = guest_memory;

    m_ss_list_count_va    = scene_list_count_va;
    m_ss_list_va          = scene_list_va;
    m_ss_pending_va       = shift_pending_va;
    m_ss_target_va        = shift_target_va;
    m_ss_spawn_va         = shift_spawn_va;
    m_ss_error_va         = shift_error_va;
    m_ss_active_va        = shift_active_va;
    m_ss_current_scene_va = current_scene_va;
    m_ss_assets_dir       = assets_dir;  // e.g. ~/.local/share/swordigo-desktop/assets

    m_scene_shifter_ready = (guest_memory &&
                             scene_list_count_va &&
                             scene_list_va &&
                             shift_pending_va &&
                             shift_target_va);

    if (!m_scene_shifter_ready) {
        fprintf(stderr, "[SwordfareGUI] init_scene_shifter: missing VAs — scene shifter disabled\n");
        return;
    }

    // Run host-side scan immediately so the list is populated on first open.
    // We write directly into guest memory (g_sre_scene_list / g_sre_scene_list_count).
    int*  g_count = (int*)(guest_memory + scene_list_count_va);
    char (*g_list)[128] = (char(*)[128])(guest_memory + scene_list_va);
    *g_count = 0;

    auto try_scan = [&](const fs::path& dir) {
        if (!fs::exists(dir) || !fs::is_directory(dir)) return;
        std::error_code ec;
        for (auto& ent : fs::recursive_directory_iterator(dir,
                fs::directory_options::skip_permission_denied, ec)) {
            if (*g_count >= 256) break;
            if (!ent.is_regular_file(ec)) continue;
            auto name = ent.path().filename().string();
            if (name.size() <= 6) continue;
            if (name.substr(name.size() - 6) != ".scene") continue;
            // strip extension
            std::string stem = name.substr(0, name.size() - 6);
            // deduplicate
            bool dup = false;
            for (int i = 0; i < *g_count; i++) {
                if (std::string(g_list[i]) == stem) { dup = true; break; }
            }
            if (dup) continue;
            strncpy(g_list[*g_count], stem.c_str(), 127);
            g_list[*g_count][127] = '\0';
            (*g_count)++;
        }
    };

    // 1. Vanilla assets/resources/
    try_scan(fs::path(assets_dir) / "resources");

    // 2. Sort alphabetically for readability
    qsort(g_list, *g_count, 128, [](const void* a, const void* b) -> int {
        return strcmp((const char*)a, (const char*)b);
    });

    fprintf(stderr, "[SwordfareGUI] Scene Shifter ready — %d scenes found in %s/resources\n",
            *g_count, assets_dir.c_str());
}

void SwordfareGUI::toggle_lua_console() {
    if (!m_console_ready) return;
    m_console_open  = !m_console_open;
    m_console_focus = m_console_open;
    m_console_scroll_bottom = true;
}

void SwordfareGUI::console_submit(const std::string& cmd) {
    if (cmd.empty() || !m_guest_memory || !m_console_buf_addr) return;

    // Intercept 'openport' command
    if (cmd == "openport" || cmd.rfind("openport ", 0) == 0) {
        m_console_history.push_back({"> " + cmd, false, true});
        int port = 12345;
        if (cmd.size() > 9) {
            try {
                port = std::stoi(cmd.substr(9));
            } catch (...) {
                m_console_history.push_back({"[TCP-Console] Error: Invalid port number.", true, false});
                return;
            }
        }
        start_tcp_server(port);
        return;
    }

    std::cout << "[SRE-Lua] > " << cmd << std::endl;
    m_console_history.push_back({"> " + cmd, false, true});
    while ((int)m_console_history.size() > CONSOLE_MAX_HISTORY)
        m_console_history.erase(m_console_history.begin());

    if (m_console_cmd_history.empty() || m_console_cmd_history.back() != cmd)
        m_console_cmd_history.push_back(cmd);
    m_console_hist_idx = -1;

    char* buf = (char*)(m_guest_memory + m_console_buf_addr);
    size_t len = cmd.size();
    if (len > 4094) len = 4094;
    memcpy(buf, cmd.c_str(), len);
    buf[len] = 0;

    *(int32_t*)(m_guest_memory + m_console_status_addr)  = 0;
    *(int32_t*)(m_guest_memory + m_console_pending_addr) = 1;
    m_console_scroll_bottom = true;
}

void SwordfareGUI::lua_console_text(const char* text) {
    if (!m_console_open || !text) return;
    size_t cur = strlen(m_console_input);
    size_t add = strlen(text);
    if (cur + add < sizeof(m_console_input) - 1)
        strcat(m_console_input, text);
}

bool SwordfareGUI::lua_console_key(SDL_Keycode key, const std::string& /*unused*/) {
    if (!m_console_open) return false;

    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (m_console_input[0]) { console_submit(m_console_input); m_console_input[0] = 0; }
        return true;
    }
    if (key == SDLK_BACKSPACE) {
        size_t len = strlen(m_console_input);
        if (len) m_console_input[len-1] = 0;
        return true;
    }
    if (key == SDLK_ESCAPE) { m_console_open = false; return true; }
    if (key == SDLK_UP) {
        if (!m_console_cmd_history.empty()) {
            if (m_console_hist_idx < 0)
                m_console_hist_idx = (int)m_console_cmd_history.size() - 1;
            else if (m_console_hist_idx > 0)
                m_console_hist_idx--;
            strncpy(m_console_input, m_console_cmd_history[m_console_hist_idx].c_str(),
                    sizeof(m_console_input)-1);
        }
        return true;
    }
    if (key == SDLK_DOWN) {
        if (m_console_hist_idx >= 0) {
            m_console_hist_idx++;
            if (m_console_hist_idx >= (int)m_console_cmd_history.size()) {
                m_console_hist_idx = -1; m_console_input[0] = 0;
            } else {
                strncpy(m_console_input, m_console_cmd_history[m_console_hist_idx].c_str(),
                        sizeof(m_console_input)-1);
            }
        }
        return true;
    }
    return true; // consume all keys when console is open
}

int SwordfareGUI::console_input_callback(ImGuiInputTextCallbackData* data) {
    SwordfareGUI* gui = (SwordfareGUI*)data->UserData;
    return gui->on_console_input_callback(data);
}

int SwordfareGUI::on_console_input_callback(ImGuiInputTextCallbackData* data) {
    // 1. History Recall via Up / Down arrow keys
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        int prev_idx = m_console_hist_idx;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (m_console_hist_idx < 0) {
                m_console_hist_idx = (int)m_console_cmd_history.size() - 1;
            } else if (m_console_hist_idx > 0) {
                m_console_hist_idx--;
            }
        } else if (data->EventKey == ImGuiKey_DownArrow) {
            if (m_console_hist_idx >= 0) {
                m_console_hist_idx++;
                if (m_console_hist_idx >= (int)m_console_cmd_history.size()) {
                    m_console_hist_idx = -1;
                }
            }
        }

        if (prev_idx != m_console_hist_idx) {
            std::string cmd = (m_console_hist_idx >= 0) ? m_console_cmd_history[m_console_hist_idx] : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, cmd.c_str());
            data->CursorPos = data->SelectionStart = data->SelectionEnd = (int)cmd.size();
        }
    }

    // 2. Tab completion / Indent
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        // Insert 4 spaces at cursor position
        data->InsertChars(data->CursorPos, "    ");
    }

    // 3. Ctrl+L to Clear Screen & Ctrl+Up/Down for History Recall (for Multiline support)
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L)) {
            m_console_history.clear();
        }
        
        bool up_pressed = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
        bool down_pressed = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
        if (io.KeyCtrl && (up_pressed || down_pressed)) {
            int prev_idx = m_console_hist_idx;
            if (up_pressed) {
                if (m_console_hist_idx < 0) {
                    m_console_hist_idx = (int)m_console_cmd_history.size() - 1;
                } else if (m_console_hist_idx > 0) {
                    m_console_hist_idx--;
                }
            } else if (down_pressed) {
                if (m_console_hist_idx >= 0) {
                    m_console_hist_idx++;
                    if (m_console_hist_idx >= (int)m_console_cmd_history.size()) {
                        m_console_hist_idx = -1;
                    }
                }
            }

            if (prev_idx != m_console_hist_idx) {
                std::string cmd = (m_console_hist_idx >= 0) ? m_console_cmd_history[m_console_hist_idx] : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, cmd.c_str());
                data->CursorPos = data->SelectionStart = data->SelectionEnd = (int)cmd.size();
            }
        }
    }

    return 0;
}

void SwordfareGUI::draw_lua_console() {
    if (!m_initialized || !m_console_open || !m_console_ready) return;

    ImGuiIO& io = ImGui::GetIO();
    float W        = io.DisplaySize.x;
    float H        = io.DisplaySize.y;
    float panel_h  = H * 0.50f;               // taller for bigger context
    float font_scale = 0.82f;                 // compact — smaller than the global UI font

    ImGui::SetNextWindowPos(ImVec2(0, H - panel_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(W, panel_h),    ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);

    // ── Colour palette ─────────────────────────────────────────────────────
    const ImVec4 col_bg        = xpera::palette.void_bg;    // app backdrop
    const ImVec4 col_border    = xpera::palette.border;
    const ImVec4 col_header_bg = xpera::palette.surface;    // header strip
    const ImVec4 col_input_bg  = xpera::palette.input;
    const ImVec4 col_sep       = xpera::palette.border_soft;
    const ImVec4 col_prompt    = xpera::palette.accent;     // indigo prompt
    const ImVec4 col_out       = xpera::palette.text_hi;    // primary output
    const ImVec4 col_err       = xpera::palette.danger;     // error
    const ImVec4 col_meta      = xpera::palette.text_lo;    // dim info
    const ImVec4 col_title     = xpera::palette.accent;     // title accent

    ImGui::PushStyleColor(ImGuiCol_WindowBg,        col_bg);
    ImGui::PushStyleColor(ImGuiCol_Border,          col_border);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,         col_input_bg);
    ImGui::PushStyleColor(ImGuiCol_Text,            ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,     ImVec4(0.03f, 0.03f, 0.05f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,   ImVec4(0.18f, 0.22f, 0.40f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.28f, 0.36f, 0.60f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  col_prompt);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,     ImVec2(6.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,    8.0f);

    bool pushed_mono = false;
    if (m_font_mono) { ImGui::PushFont((ImFont*)m_font_mono); pushed_mono = true; }

    // Apply compact font scale
    ImGui::SetWindowFontScale(font_scale);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize   |
                             ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse  | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##SRE_LuaConsole", nullptr, flags)) {

        ImGui::SetWindowFontScale(font_scale);

        // ── Header strip ────────────────────────────────────────────────────
        float line_h = ImGui::GetTextLineHeight();
        float hdr_h  = line_h + 10.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetWindowPos(),
            ImVec2(ImGui::GetWindowPos().x + W, ImGui::GetWindowPos().y + hdr_h),
            ImGui::ColorConvertFloat4ToU32(col_header_bg)
        );

        ImGui::SetCursorPos(ImVec2(10.0f, 5.0f));
        ImGui::TextColored(col_title, ICON_FA_TERMINAL " raijin");
        ImGui::SameLine(0, 2);
        ImGui::TextColored(col_meta, "  swordfare lua-console v8  |  lua 5.1  |  full gamestate  |  ` to close");

        float btn_x = W - 320.0f;
        ImGui::SameLine(btn_x);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.10f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.24f, 0.44f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.14f, 0.30f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          col_meta);
        if (ImGui::SmallButton(" " ICON_FA_FILE " Manager ")) m_script_manager_open = !m_script_manager_open;
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton(" " ICON_FA_CODE " Editor ")) m_script_editor_open = !m_script_editor_open;
        ImGui::SameLine(0, 6);
        if (ImGui::SmallButton(" " ICON_FA_CIRCLE_XMARK " Clear ")) m_console_history.clear();
        ImGui::SameLine(0, 6);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.30f, 0.30f, 1.0f));
        if (ImGui::SmallButton("  " ICON_FA_XMARK "  ")) m_console_open = false;
        ImGui::PopStyleColor(5);

        // thin separator line under header
        ImVec2 sep_a(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + hdr_h);
        ImVec2 sep_b(sep_a.x + W, sep_a.y);
        ImGui::GetWindowDrawList()->AddLine(sep_a, sep_b,
            ImGui::ColorConvertFloat4ToU32(col_sep), 1.0f);

        // ── Scrollback area ─────────────────────────────────────────────────
        ImGui::SetCursorPos(ImVec2(0.0f, hdr_h + 2.0f));

        // Input row height: multiline (3 lines) + padding + separator
        float input_inner_h = line_h * 3.0f + 8.0f;
        float input_area_h  = input_inner_h + 6.0f + line_h + 6.0f; // +prompt row + gaps
        float scroll_h = panel_h - hdr_h - input_area_h - 8.0f;
        if (scroll_h < 40.0f) scroll_h = 40.0f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, col_bg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        ImGui::BeginChild("##scroll", ImVec2(0, scroll_h), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::SetWindowFontScale(font_scale);

        for (auto& e : m_console_history) {
            if (e.is_input) {
                ImGui::TextColored(col_prompt, ">");
                ImGui::SameLine(0, 5);
                // user command in slightly brighter white
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.95f, 1.0f), "%s", e.text.c_str() + 2);
            } else if (e.is_error) {
                ImGui::TextColored(col_err, "%s", e.text.c_str());
            } else {
                // dim for banner/info lines vs normal output
                bool is_banner = (m_console_history.size() > 1 &&
                                  (&e == &m_console_history[0] || &e == &m_console_history[1]));
                ImGui::TextColored(is_banner ? col_meta : col_out, "%s", e.text.c_str());
            }
        }
        if (m_console_scroll_bottom) {
            ImGui::SetScrollHereY(1.0f);
            m_console_scroll_bottom = false;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(); // ChildBg

        // thin separator above input
        float cur_y = ImGui::GetCursorPosY();
        ImVec2 isep_a(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + cur_y);
        ImVec2 isep_b(isep_a.x + W, isep_a.y);
        ImGui::GetWindowDrawList()->AddLine(isep_a, isep_b,
            ImGui::ColorConvertFloat4ToU32(col_sep), 1.0f);
        ImGui::SetCursorPosY(cur_y + 2.0f);

        // ── Input row ────────────────────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

        // Prompt glyph
        ImGui::SetCursorPosX(6.0f);
        ImGui::TextColored(col_prompt, ">>>");
        ImGui::SameLine(0, 6);

        if (m_console_focus) { ImGui::SetKeyboardFocusHere(); m_console_focus = false; }

        // Multiline input — 3 visible lines, grows via scrollbar inside
        float run_btn_w = 52.0f;
        float input_w   = W - ImGui::GetCursorPosX() - run_btn_w - 12.0f;

        ImGui::PushStyleColor(ImGuiCol_FrameBg, col_input_bg);
        ImGui::PushStyleColor(ImGuiCol_Border,  col_sep);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        bool submit = ImGui::InputTextMultiline(
            "##input", m_console_input, sizeof(m_console_input),
            ImVec2(input_w, input_inner_h),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CtrlEnterForNewLine |
            ImGuiInputTextFlags_EscapeClearsAll |
            ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackAlways,
            console_input_callback, this
        );

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        ImGui::SameLine(0, 6);

        // Run button
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.12f, 0.36f, 0.72f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.95f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.08f, 0.22f, 0.52f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
        if (ImGui::Button(" " ICON_FA_PLAY " ##run", ImVec2(run_btn_w, input_inner_h)) && m_console_input[0]) {
            // strip trailing newline that Enter adds in multiline mode
            int len = (int)strlen(m_console_input);
            while (len > 0 && (m_console_input[len-1] == '\n' || m_console_input[len-1] == '\r'))
                m_console_input[--len] = '\0';
            if (len > 0) {
                console_submit(m_console_input);
                m_console_input[0] = 0;
                m_console_focus    = true;
            }
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(); // WindowPadding (input row)

        if (submit && m_console_input[0]) {
            int len = (int)strlen(m_console_input);
            while (len > 0 && (m_console_input[len-1] == '\n' || m_console_input[len-1] == '\r'))
                m_console_input[--len] = '\0';
            if (len > 0) {
                console_submit(m_console_input);
                m_console_input[0] = 0;
                m_console_focus    = true;
            }
        }
    }
    ImGui::End();

    if (pushed_mono) ImGui::PopFont();
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(8);
}

void SwordfareGUI::start_tcp_server(int port) {
    stop_tcp_server();

    m_tcp_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_tcp_server_fd < 0) {
        m_console_history.push_back({"[TCP-Console] Error: Failed to create socket.", true, false});
        return;
    }

    int opt = 1;
    setsockopt(m_tcp_server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(m_tcp_server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        m_console_history.push_back({"[TCP-Console] Error: Failed to bind to port " + std::to_string(port), true, false});
        close(m_tcp_server_fd);
        m_tcp_server_fd = -1;
        return;
    }

    if (listen(m_tcp_server_fd, 3) < 0) {
        m_console_history.push_back({"[TCP-Console] Error: Failed to listen on socket.", true, false});
        close(m_tcp_server_fd);
        m_tcp_server_fd = -1;
        return;
    }

    m_tcp_running = true;
    m_tcp_thread = std::thread(&SwordfareGUI::tcp_server_loop, this);
    m_console_history.push_back({"[TCP-Console] Server started on port " + std::to_string(port) + ". Run 'nc localhost " + std::to_string(port) + "' to connect.", false, false});
}

void SwordfareGUI::stop_tcp_server() {
    m_tcp_running = false;
    // Wake the reader thread if it is parked in the back-pressure CV wait so it
    // observes m_tcp_running == false and unwinds instead of hanging the join.
    m_tcp_done_cv.notify_all();
    {
        std::lock_guard<std::mutex> lk(m_tcp_mutex);
        m_tcp_cmd_queue.clear();
        m_tcp_cmd_in_flight = false;
        m_tcp_in_flight_fd  = -1;
    }
    if (m_tcp_server_fd >= 0) {
        ::shutdown(m_tcp_server_fd, SHUT_RDWR);
        close(m_tcp_server_fd);
        m_tcp_server_fd = -1;
    }
    
    int client_fd = m_tcp_client_fd.load();
    if (client_fd >= 0) {
        ::shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
        m_tcp_client_fd = -1;
    }

    if (m_tcp_thread.joinable()) {
        m_tcp_thread.join();
    }
}

static bool is_lua_chunk_complete(const std::string& code) {
    int braces = 0;
    int parens = 0;
    int brackets = 0; // [ ]
    int blocks = 0;
    bool pending_elseif = false; // true between an `elseif` and its `then`

    size_t i = 0;
    size_t n = code.length();
    while (i < n) {
        char ch = code[i];
        
        // 1. Skip comments
        if (ch == '-' && i + 1 < n && code[i + 1] == '-') {
            i += 2;
            // Check for multi-line comment: --[[ ... ]] or --[=[ ... ]=]
            if (i < n && code[i] == '[') {
                size_t start = i;
                size_t equals_count = 0;
                i++;
                while (i < n && code[i] == '=') {
                    equals_count++;
                    i++;
                }
                if (i < n && code[i] == '[') {
                    // It is a multi-line comment. Find matching closer: ]=...]
                    i++;
                    std::string closer = "]" + std::string(equals_count, '=') + "]";
                    size_t closer_pos = code.find(closer, i);
                    if (closer_pos != std::string::npos) {
                        i = closer_pos + closer.length();
                    } else {
                        i = n; // Incomplete multi-line comment
                    }
                    continue;
                }
                // Not a multi-line comment, fall back to single-line comment
                i = start;
            }
            // Single-line comment: skip to newline
            while (i < n && code[i] != '\n') {
                i++;
            }
            continue;
        }
        
        // 2. Skip string literals
        if (ch == '\'' || ch == '"') {
            char quote = ch;
            i++;
            while (i < n) {
                if (code[i] == '\\' && i + 1 < n) {
                    i += 2; // skip escaped char
                    continue;
                }
                if (code[i] == quote) {
                    i++;
                    break;
                }
                i++;
            }
            continue;
        }
        
        // 3. Skip Lua long brackets (strings): [[ ... ]] or [=[ ... ]=]
        if (ch == '[') {
            size_t start = i;
            size_t equals_count = 0;
            i++;
            while (i < n && code[i] == '=') {
                equals_count++;
                i++;
            }
            if (i < n && code[i] == '[') {
                i++;
                std::string closer = "]" + std::string(equals_count, '=') + "]";
                size_t closer_pos = code.find(closer, i);
                if (closer_pos != std::string::npos) {
                    i = closer_pos + closer.length();
                } else {
                    i = n; // Incomplete long string
                }
                continue;
            }
            i = start; // Just a regular bracket [
        }
        
        // 4. Track braces, parens, brackets
        if (ch == '{') braces++;
        else if (ch == '}') braces--;
        else if (ch == '(') parens++;
        else if (ch == ')') parens--;
        else if (ch == '[') brackets++;
        else if (ch == ']') brackets--;
        
        // 5. Track Lua block keywords.
        //
        //   Openers:  function / do / then / repeat  -> blocks++
        //   Closers:  end / until                    -> blocks--
        //   elseif / else are NEITHER — they live inside an already-open
        //   if-block and must not change the depth (the old code did
        //   `elseif -> blocks--`, which made every if/elseif/end report as
        //   unbalanced and falsely "complete", the core multiline bug).
        //
        //   Note: a bare `then`/`do` that is part of `elseif ... then` still
        //   correctly pairs with the block's single `end`, because the `if`
        //   only opened one level via its first `then`. To keep depth right we
        //   only count the FIRST `then` of an if-chain: track whether we are
        //   between `elseif`/`else` and its `then` and skip that `then`.
        if (isalpha((unsigned char)ch) || ch == '_') {
            std::string word;
            while (i < n && (isalnum((unsigned char)code[i]) || code[i] == '_')) {
                word += code[i];
                i++;
            }
            if (word == "function" || word == "do" || word == "repeat") {
                blocks++;
            } else if (word == "then") {
                // `then` closes an `if`/`elseif` condition. Only the if's
                // first `then` opens a block; an `elseif ... then` reuses the
                // same block level.
                if (pending_elseif) {
                    pending_elseif = false;   // elseif's then: no depth change
                } else {
                    blocks++;                 // if/while ... then: opens block
                }
            } else if (word == "end" || word == "until") {
                blocks--;
            } else if (word == "elseif") {
                pending_elseif = true;        // its following `then` is neutral
            }
            // `else` and everything else: no depth change.
            continue;
        }
        
        i++;
    }
    return (braces <= 0 && parens <= 0 && brackets <= 0 && blocks <= 0);
}

// Wrap a bare expression so the console prints its value, LuaJIT/lua5.1
// interactive style. If `code` is a single expression (e.g. `1+2`, `ffi.base()`,
// `player.hp`) rather than a statement, evaluating it as a chunk yields nothing;
// prefixing `return ` makes the result visible. We only do this when:
//   * the chunk is a single line (no embedded newline), AND
//   * it does not already start with a statement keyword or `return`, AND
//   * it does not contain a top-level `=` assignment (`==`/`~=`/`<=`/`>=` are ok).
// On failure to compile as an expression, the caller falls back to the raw
// chunk, so this is always safe.
static std::string maybe_wrap_expression(const std::string& code) {
    // Trim.
    size_t a = code.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return code;
    size_t b = code.find_last_not_of(" \t\r\n");
    std::string s = code.substr(a, b - a + 1);

    if (s.find('\n') != std::string::npos) return code;   // multi-line: leave as-is

    static const char* kw[] = {
        "return", "local", "if", "for", "while", "repeat", "do", "function",
        "break", "goto", "end", "else", "elseif", "then", nullptr
    };
    for (int i = 0; kw[i]; i++) {
        size_t klen = strlen(kw[i]);
        if (s.size() >= klen && s.compare(0, klen, kw[i]) == 0 &&
            (s.size() == klen || !(isalnum((unsigned char)s[klen]) || s[klen] == '_')))
            return code;   // already a statement
    }

    // Detect a top-level assignment `=` (not ==, ~=, <=, >=). Respect strings.
    bool in_str = false; char q = 0; int depth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (in_str) {
            if (c == '\\') { i++; continue; }
            if (c == q) in_str = false;
            continue;
        }
        if (c == '\'' || c == '"') { in_str = true; q = c; continue; }
        if (c == '(' || c == '{' || c == '[') depth++;
        else if (c == ')' || c == '}' || c == ']') depth--;
        else if (c == '=' && depth == 0) {
            char prev = (i > 0) ? s[i-1] : 0;
            char next = (i+1 < s.size()) ? s[i+1] : 0;
            if (prev != '=' && prev != '~' && prev != '<' && prev != '>' &&
                next != '=')
                return code;   // assignment statement, don't wrap
        }
    }

    return "return " + s;
}

void SwordfareGUI::tcp_server_loop() {
    while (m_tcp_running) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(m_tcp_server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (!m_tcp_running) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Handle client connection (support one client at a time for simplicity)
        int old_client = m_tcp_client_fd.exchange(client_fd);
        if (old_client >= 0) {
            const char* msg = "Another client connected. Disconnecting...\n";
            write(old_client, msg, strlen(msg));
            close(old_client);
        }

        // New connection generation. Purge any backlog from a previous client
        // and clear the in-flight slot so a dead client's queued/in-flight
        // results are never delivered to THIS client and the dispatcher cannot
        // stay wedged waiting on an owner that has gone away.
        uint64_t my_gen;
        {
            std::lock_guard<std::mutex> lk(m_tcp_mutex);
            my_gen = ++m_tcp_client_gen;
            m_tcp_cmd_queue.clear();
            m_tcp_cmd_in_flight = false;
            m_tcp_in_flight_fd  = -1;
            m_tcp_in_flight_gen = 0;
        }
        m_tcp_done_cv.notify_all();

        // Welcome greeting
        const char* greeting = 
            "\033[1;36m=====================================================\033[0m\n"
            "\033[1;32m      RAIJIN Lua SDK Console (Swordfare TCP Server)  \033[0m\n"
            "\033[1;36m=====================================================\033[0m\n"
            " \033[33m*\033[0m Enter Lua commands or blocks to evaluate dynamically.\n"
            " \033[33m*\033[0m Supports full multiline input (braces, quotes, functions).\n"
            " \033[33m*\033[0m Type \033[1;31m.\033[0m on a new line to clear current input buffer.\n"
            " \033[33m*\033[0m Type \033[1;32mclear\033[0m to clear screen.\n"
            " \033[33m*\033[0m Type \033[1;31mexit\033[0m or \033[1;31mquit\033[0m to close connection.\n\n"
            "\033[1;36mraijin-sdk\033[0m> ";
        write(client_fd, greeting, strlen(greeting));

        // Read loop for client commands
        char read_buf[4096];
        std::string line_accumulator;
        std::string multiline_cmd;
        while (m_tcp_running && m_tcp_client_fd.load() == client_fd) {
            ssize_t bytes_read = read(client_fd, read_buf, sizeof(read_buf) - 1);
            if (bytes_read <= 0) {
                // Client disconnected or read error
                break;
            }
            read_buf[bytes_read] = '\0';
            // Append by explicit length so embedded NULs in a paste don't
            // truncate the accumulator (operator+= on a char* stops at NUL).
            line_accumulator.append(read_buf, (size_t)bytes_read);

            // Process any complete lines
            size_t newline_pos;
            while ((newline_pos = line_accumulator.find('\n')) != std::string::npos) {
                std::string line = line_accumulator.substr(0, newline_pos);
                line_accumulator.erase(0, newline_pos + 1);

                // Strip carriage return if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                // Strip leading/trailing whitespaces
                size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos) {
                    line = line.substr(first);
                    size_t last = line.find_last_not_of(" \t");
                    line = line.substr(0, last + 1);
                } else {
                    line.clear();
                }

                // Check for input buffer reset command
                if (line == ".") {
                    multiline_cmd.clear();
                    const char* reset_msg = "Input buffer cleared.\n\033[1;36mraijin-sdk\033[0m> ";
                    write(client_fd, reset_msg, strlen(reset_msg));
                    continue;
                }

                if (line == "exit" || line == "quit") {
                    const char* bye = "Goodbye!\n";
                    write(client_fd, bye, strlen(bye));
                    close(client_fd);
                    m_tcp_client_fd.compare_exchange_strong(client_fd, -1);
                    break;
                }

                if (line == "clear") {
                    // Send ANSI clear screen command
                    const char* clear_ansi = "\033[2J\033[H\033[1;36mraijin-sdk\033[0m> ";
                    write(client_fd, clear_ansi, strlen(clear_ansi));
                    multiline_cmd.clear();
                    continue;
                }

                // Accumulate block line
                if (multiline_cmd.empty()) {
                    multiline_cmd = line;
                } else {
                    multiline_cmd += "\n" + line;
                }

                // Execute block if it is complete
                bool execute = false;
                if (multiline_cmd.empty()) {
                    execute = true;
                } else {
                    if (is_lua_chunk_complete(multiline_cmd)) {
                        execute = true;
                    }
                }

                if (execute) {
                    if (!multiline_cmd.empty()) {
                        // Auto-wrap a bare expression (e.g. `1+2`, `ffi.base()`)
                        // so the console prints its value like an interactive
                        // REPL. Multi-line/statement chunks pass through.
                        std::string chunk = maybe_wrap_expression(multiline_cmd);

                        // Enqueue the chunk (FIFO). The main thread drains one
                        // at a time and returns each result + prompt in order.
                        // We do NOT busy-wait on a single slot anymore — that
                        // was the multiline-paste bug (races/dropped lines).
                        //
                        // Back-pressure: if the client floods us (huge paste),
                        // wait — bounded — until the queue drains below a cap so
                        // we never grow unboundedly, but without ever dropping a
                        // line. The wait is interruptible on disconnect/shutdown.
                        {
                            std::unique_lock<std::mutex> lk(m_tcp_mutex);
                            const size_t kMaxQueued = 256;
                            m_tcp_done_cv.wait_for(
                                lk, std::chrono::milliseconds(250),
                                [&]{
                                    return !m_tcp_running ||
                                           m_tcp_client_fd.load() != client_fd ||
                                           m_tcp_cmd_queue.size() < kMaxQueued;
                                });
                            if (!m_tcp_running || m_tcp_client_fd.load() != client_fd) {
                                break;
                            }
                            m_tcp_cmd_queue.push_back({chunk, client_fd, my_gen});
                        }

                        multiline_cmd.clear();
                        // The prompt for THIS command is emitted by the main
                        // thread once the guest returns its result, so output
                        // and prompt stay correctly ordered even under paste.
                    } else {
                        // Empty line in a clean state: just re-emit the prompt.
                        const char* prompt = "\033[1;36mraijin-sdk\033[0m> ";
                        write(client_fd, prompt, strlen(prompt));
                    }
                } else {
                    // Incomplete block: keep accumulating lines. Only emit a
                    // continuation prompt when we have genuinely CONSUMED all
                    // buffered input and are now waiting on the human to type
                    // the next line. When a whole multi-line block was pasted
                    // in a single write (line_accumulator still holds more
                    // complete lines), emitting a ">>" per interior line just
                    // floods the client with "  >>   >>   >>" and desyncs the
                    // display — so we suppress it until the buffer drains.
                    if (line_accumulator.find('\n') == std::string::npos) {
                        const char* cont_prompt = "\033[1;33m          \033[0m>> ";
                        write(client_fd, cont_prompt, strlen(cont_prompt));
                    }
                }
            }
        }

        // Cleanup this client connection if we broke out of the read loop.
        // Also purge any pending/in-flight work belonging to THIS connection so
        // the main-thread dispatcher never stays wedged (m_tcp_cmd_in_flight
        // stuck true) waiting on a client that has gone away, and a late guest
        // result for this connection is dropped rather than leaked to the next
        // client. Bumping the generation invalidates any in-flight result.
        {
            std::lock_guard<std::mutex> lk(m_tcp_mutex);
            ++m_tcp_client_gen;
            m_tcp_cmd_queue.clear();
            m_tcp_cmd_in_flight = false;
            m_tcp_in_flight_fd  = -1;
            m_tcp_in_flight_gen = 0;
        }
        m_tcp_done_cv.notify_all();

        if (m_tcp_client_fd.load() == client_fd) {
            close(client_fd);
            m_tcp_client_fd.store(-1);
        }
    }
}

// =============================================================================
// Memory Research Tab — SwordfareGUI bridge methods
// =============================================================================

void SwordfareGUI::init_research_tab(const uint8_t* guest_memory,
                                      uint64_t       guest_mem_size) {
    if (!m_research_tab)
        m_research_tab = std::make_unique<swordfare::research::MemoryResearchTab>();
    // Tool typography, not the game's display face.  Passed in rather than
    // looked up so the console has no dependency on this class's internals.
    swordfare::research::MemoryResearchTab::ConsoleFonts fonts;
    fonts.body = m_font_ui ? m_font_ui : m_font_main;
    fonts.mono = m_font_mono;
    m_research_tab->set_fonts(fonts);
    m_research_tab->init(guest_memory, guest_mem_size);
}

void SwordfareGUI::update_research_roots(const swordfare::research::LiveRoots& roots) {
    if (m_research_tab) m_research_tab->update_roots(roots);
}

bool SwordfareGUI::is_research_ready() const {
    return m_research_tab && m_research_tab->is_ready();
}

void SwordfareGUI::tick_research(uint64_t frame, uint8_t* guest_memory,
                                 uint64_t guest_mem_size) {
    if (m_research_tab) m_research_tab->tick(frame, guest_memory, guest_mem_size);
}

void SwordfareGUI::set_research_module(const std::string& name, uint64_t base_va,
                                       uint64_t rva_begin, uint64_t rva_end,
                                       const std::string& build_id) {
    if (!m_research_tab) return;
    swordfare::research::ModuleExtent e;
    e.name      = name;
    e.base_va   = base_va;
    e.rva_begin = rva_begin;
    e.rva_end   = rva_end;
    e.build_id  = build_id;
    m_research_tab->register_module(e);
}

void SwordfareGUI::set_research_module_symbols(const std::string& name,
                                               const void* symtab, size_t count,
                                               const char* strtab, size_t strtab_size,
                                               bool is_64) {
    if (!m_research_tab) return;
    const bool ok = m_research_tab->register_module_symbols(name, symtab, count,
                                                           strtab, strtab_size, is_64);
    std::cout << "[Research] symbols for " << name << ": "
              << (ok ? "indexed" : "none available")
              << " (" << count << " dynsym entries)" << std::endl;
}

void SwordfareGUI::set_research_module_sections(const std::string& name,
                                                const void* shdrs, size_t count,
                                                const char* names, size_t names_size,
                                                bool is_64) {
    if (!m_research_tab) return;
    const bool ok = m_research_tab->register_module_sections(name, shdrs, count,
                                                            names, names_size, is_64);
    std::cout << "[Research] sections for " << name << ": "
              << (ok ? "indexed" : "none available")
              << " (" << count << " shdr entries, names "
              << (names && names_size ? "kept" : "absent") << ")" << std::endl;
}
