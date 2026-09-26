// =============================================================================
// Swordfare GUI — Modern ImGui-Based In-Game Overlay  (v7.3)
//
// A sleek, modern overlay rendered on top of the running game using Dear ImGui
// (SDL3 + OpenGL3 backends). Replaces the old bitmap-drawn F3 debug panel with
// a proper dockable, styled ImGui window.
//
// Design goals:
//   • Non-bitmap: uses ImGui vector rendering — crisp at all resolutions.
//   • Dark + glassmorphism-inspired theme consistent with the launcher UI.
//   • Minimal overhead — only renders when visible.
//   • Easily extensible: future panels (controls editor, camera, mods) can be
//     added as new SwardfarePanel subclasses without touching main.cpp.
//
// Usage:
//   SwordfareGUI gui;
//   gui.init(sdl_window, gl_context);        // call once after GL context ready
//   // per-frame (after game draw, before swap):
//   gui.begin_frame();
//   if (gui.is_visible()) gui.draw_debug(fps, dt, ...);
//   gui.end_frame();
//   gui.shutdown();                          // call on exit
// =============================================================================

#pragma once

#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include "platform/gl_inc.h"

#ifdef VULKAN_BACKEND
#include "volk.h"
#else
// Stub so VkDescriptorPool / VK_NULL_HANDLE exist without the full Vulkan SDK
typedef struct VkDescriptorPool_T* VkDescriptorPool;
#define VK_NULL_HANDLE nullptr
#endif

#include "platform/srt_overlay.h"
#include "platform/gui.h"
#include "game/research/memory_research_tab.h"
#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <memory>

// ---------------------------------------------------------------------------
// Debug stats snapshot (passed to draw_debug each frame)
// ---------------------------------------------------------------------------
struct SwordfareDebugStats {
    float   fps           = 0.0f;
    float   dt_seconds    = 0.0f;
    int     frame_count   = 0;
    int     draw_calls    = 0;
    int     tex_binds     = 0;
    int     vertices      = 0;
    int     matrix_ops    = 0;
    int     state_changes = 0;
    int     tex_uploads   = 0;
    int     win_w         = 0, win_h = 0;
    int     draw_w        = 0, draw_h = 0;
    int     mouse_x       = 0, mouse_y = 0;
    float   cam_x         = 0.0f, cam_y = 0.0f, cam_z = 0.0f;
    float   cam_zoom      = 1.0f;
    float   hero_x        = 0.0f, hero_y = 0.0f, hero_z = 0.0f;
    bool    cam_active    = false;
    bool    typing_mode   = false;
    bool    game_paused   = false;
    bool    postfx_on     = false;
    const char* postfx_preset  = "Off";
    const char* scale_mode     = "Sharp-Bilinear";
    const char* binary_name    = "";
    const char* speed_label    = "1×";
    const char* graphics_api   = "OpenGL";
};

struct ImGuiInputTextCallbackData;

// ---------------------------------------------------------------------------
// SwordfareGUI — main class
// ---------------------------------------------------------------------------
class SwordfareGUI {
public:
    SwordfareGUI()  = default;
    ~SwordfareGUI() = default;

    // Lifecycle
    void init(SDL_Window* window, SDL_GLContext gl_ctx);
    void init_vulkan(SDL_Window* window, class VulkanBackend* vk_backend);
    void shutdown();
    bool is_initialized() const { return m_initialized; }

    // Event pass-through — call before your own SDL event handling
    // Returns true if ImGui consumed the event (caller should not process it).
    bool process_event(const SDL_Event& event);

    // Frame management — wrap the game's GL draw calls
    void begin_frame();
    void end_frame();     // renders ImGui draw data into the GL context

    // The ImGui context this GUI owns (with the shared SDL3/GL3 backend).
    // Used by the boot loading screen so it draws on the SAME context/backend
    // rather than creating a second one. Null until init() runs.
    void* imgui_context() const { return m_imgui_ctx; }

    // Toggle visibility (called on F3)
    void toggle_visible() { m_visible = !m_visible; }
    bool is_visible()     const { return m_visible; }

    // Mod Overlay (Kiwi-compatible custom menu) visibility
    void toggle_mod_overlay() {
        m_mod_overlay_visible = !m_mod_overlay_visible;
        m_f11_overlay_active = false;
    }
    bool is_mod_overlay_visible() const { return m_mod_overlay_visible; }
    void set_mod_overlay_visible(bool visible) { m_mod_overlay_visible = visible; }
    // F11 owns the scene/display toolbox and behaves as a real toggle.
    void toggle_scene_toolbox() {
        if (m_mod_overlay_visible && m_f11_overlay_active) {
            m_mod_overlay_visible = false;
            m_f11_overlay_active = false;
            return;
        }
        m_mod_overlay_visible = true;
        m_f11_overlay_active = true;
        m_overlay_forced_tab = 2;
    }

    // ---- Memory Research console (standalone, opened with the [Insert] key) ----
    // Moved out of the F11 toolbox into its own full-window overlay.
    void toggle_research_overlay() {
        m_research_overlay_visible = !m_research_overlay_visible;
    }
    bool is_research_overlay_visible() const { return m_research_overlay_visible; }
    void set_research_overlay_visible(bool v) { m_research_overlay_visible = v; }
    // Renders the Memory Research console when visible. Call between
    // begin_frame()/end_frame() every frame (same timing as draw_mod_overlay).
    void draw_research_overlay();

    // The in-game mini bar.  Drawn every frame (not only while the console is
    // open), because the whole point is to poke values while the console is
    // closed and the game is running.
    void draw_research_hud();

    // Draw the debug panel — call between begin_frame()/end_frame() when visible
    void draw_debug(const SwordfareDebugStats& stats);

    // Draw the custom LUA-registered buttons and overlays using ImGui vector elements
    void draw_buttons(void* guest_buttons_ptr, void* guest_overlays_ptr, bool globally_hidden = false);

    // Draw the Lua script editor
    void draw_lua_script_editor();

    // Draw the Lua script manager
    void draw_lua_script_manager();

    // Draw the remastered mod manager overlay
    void draw_mod_overlay(const std::string& save_dir);

    // Draw the F1 modern control panel/overlay
    GuiAction draw_control_panel(bool* p_open);

    void draw_about_panel(bool* p_open, float top_offset);
    void draw_help_panel(bool* p_open, float top_offset);

    // Draw the settings panel
    GuiAction draw_settings_panel(bool* p_open);
    bool m_show_about = false;
    bool m_show_help  = false;

    // ---- F6 Decoupled Graphical Options Modal ----
    void draw_graphics_options_modal(struct PostFXState& state);
    void toggle_graphics_options() { m_graphics_options_visible = !m_graphics_options_visible; }
    bool is_graphics_options_visible() const { return m_graphics_options_visible; }
    void set_graphics_options_visible(bool v) { m_graphics_options_visible = v; }
    bool m_graphics_options_visible = false;

    // ---- Memory Research Tab ----
    //
    // Call init_research_tab() once after RecoveryCatalog::instance().init()
    // and g_guest_memory is valid (same timing as init_lua_console).
    // draw_research_tab() is called from draw_mod_overlay() automatically.
    //
    void init_research_tab(const uint8_t* guest_memory, uint64_t guest_mem_size);
    void update_research_roots(const swordfare::research::LiveRoots& roots);
    bool is_research_ready() const;

    // Called once per EMULATED frame, from the emulator thread.  Applies frozen
    // values and re-resolves every tracked object against the guest's own tick,
    // so there is no wall-clock race between the editor and the game.
    void tick_research(uint64_t frame, uint8_t* guest_memory, uint64_t guest_mem_size);

    // Tell the research engine where the loaded image is.  Until this is called
    // the engine reports "0 modules mapped" rather than pretending every
    // address has a static home.
    void set_research_module(const std::string& name, uint64_t base_va,
                             uint64_t rva_begin, uint64_t rva_end,
                             const std::string& build_id);

    // Hand the research engine a loaded image's dynamic symbol table.  The
    // pointers come from the ELF loader's own module struct (dynsym / dynstr /
    // num_dynsym) and point into the guest image, so nothing is re-read from disk.
    void set_research_module_symbols(const std::string& name,
                                     const void* symtab, size_t count,
                                     const char* strtab, size_t strtab_size,
                                     bool is_64);

    // Hand the research engine a loaded image's section headers, plus the
    // loader's copy of the section-name table when it kept one.  Lets a site be
    // described by its section (and the image's own metadata blocks be skipped).
    void set_research_module_sections(const std::string& name,
                                      const void* shdrs, size_t count,
                                      const char* names, size_t names_size,
                                      bool is_64);

    // ---- Lua Console (ImGui-native, replaces old bitmap console) ----
    //
    // Call init_lua_console() once when SRE console addrs are resolved.
    // draw_lua_console() is called every frame from main loop.
    // submit_lua_console() can be called externally (e.g. from keyboard handler).
    //
    struct ConsoleEntry { std::string text; bool is_error; bool is_input; };

    void init_lua_console(
        uint8_t* guest_memory,
        uint64_t buf_addr,
        uint64_t result_addr,
        uint64_t pending_addr,
        uint64_t status_addr,
        uint64_t print_addr);

    // Scene Shifter — guest memory address wiring.
    // Call once after libsre.so is loaded (same time as init_lua_console).
    // Pass g_guest_memory as base and each symbol's VA from get_symbol_vaddr().
    // After this, the GUI reads/writes scene shifter state in guest memory
    // instead of the (always-empty) host-side weak fallback copies.
    void init_scene_shifter(
        uint8_t* guest_memory,
        uint64_t scene_list_count_va,   // int  g_sre_scene_list_count
        uint64_t scene_list_va,          // char g_sre_scene_list[256][128]
        uint64_t shift_pending_va,       // volatile int  g_sre_scene_shift_pending
        uint64_t shift_target_va,        // char g_sre_scene_shift_target[128]
        uint64_t shift_spawn_va,         // char g_sre_scene_shift_spawn[64]
        uint64_t shift_error_va,         // char g_sre_scene_shift_last_error[256]
        uint64_t shift_active_va,        // int  g_sre_scene_shift_active
        uint64_t current_scene_va,       // char g_sre_current_scene_name[128]
        const std::string& assets_dir);  // path for host-side scan fallback

    // Returns true if a command was consumed (caller should not re-process key).
    bool lua_console_key(SDL_Keycode key, const std::string& text_input);
    // Append text from SDL_EVENT_TEXT_INPUT
    void lua_console_text(const char* text);

    // Draw the full ImGui terminal window. Call between begin_frame()/end_frame().
    void draw_lua_console();

    bool is_lua_console_open() const { return m_console_open; }
    bool is_lua_console_ready() const { return m_console_ready; }
    void toggle_lua_console();
    void update_console_backend();

    // Public entry point used by the SWORDFARE_TCP_PORT debug env var
    void auto_start_tcp_server(int port) { start_tcp_server(port); }

    // Returns true if coordinates fall inside any active overlay, button, or console
    bool is_input_blocked(float mx, float my);

private:
    // Internal helpers
    void apply_swordfare_theme();
    void scan_saves(const std::string& save_dir);
    bool load_save(const std::string& path);
    bool write_save(const std::string& path);
    void console_submit(const std::string& cmd); // write to guest + set pending
    static int console_input_callback(ImGuiInputTextCallbackData* data);
    int on_console_input_callback(ImGuiInputTextCallbackData* data);

    SDL_Window*   m_window   = nullptr;
    SDL_GLContext m_gl_ctx   = nullptr;
    // ---- Memory Research Tab ----
    std::unique_ptr<swordfare::research::MemoryResearchTab> m_research_tab;

public:
    // Hand the tool fonts to a surface that draws outside this class's own
    // layout (the research console).  void* rather than ImFont* so the header
    // stays free of an ImGui dependency.
    void* tool_font_ui() const   { return m_font_ui; }
    void* tool_font_mono() const { return m_font_mono; }
private:

    class VulkanBackend* m_vk_backend = nullptr;
    bool                 m_vulkan_active = false;
    VkDescriptorPool     m_imgui_vk_descriptor_pool = VK_NULL_HANDLE;
    void*         m_imgui_ctx = nullptr;   // ImGuiContext*
    void*         m_font_main = nullptr;   // ImFont*
    void*         m_font_button = nullptr; // ImFont* (display face — dialogs only)
    // ImFont*.  Tool surfaces: `ui` is a proportional UI face (Space Grotesk),
    // `mono` is fixed-width (JetBrains Mono) for addresses / hex / values.  Both
    // were previously unassigned, which silently left every PushFont site dead.
    void*         m_font_ui = nullptr;
    void*         m_font_mono = nullptr;

    bool          m_initialized = false;
    bool          m_visible     = false;
    bool          m_mod_overlay_visible = false;
    bool          m_f11_overlay_active = false;
    bool          m_research_overlay_visible = false;
    bool          m_buttons_globally_hidden = false;

    int m_overlay_forced_tab = -1;   // -1 = none; 0=Home 1=Display 2=Scene 3=Diagnostics
    int m_overlay_tab        = 0;    // active nav-rail item in the toolbox

    // ── Remaster textures (OpenGL path only; Vulkan falls back to vector UI) ──
    GLuint m_tex_overlay_bg     = 0;   // launcher_bg.png / ui_panel.png backdrop
    GLuint m_tex_swordigo_icon  = 0;   // swordigo_default.png / icon_app.png
    int    m_tex_overlay_bg_w   = 0, m_tex_overlay_bg_h   = 0;
    int    m_tex_swordigo_icon_w = 0, m_tex_swordigo_icon_h = 0;
    GLuint m_tex_btn_wide       = 0;   // ui_button_wide.png (game button art)
    int    m_tex_btn_wide_w     = 0, m_tex_btn_wide_h     = 0;
    GLuint m_tex_panel          = 0;   // ui_panel.png (game panel art)
    int    m_tex_panel_w        = 0, m_tex_panel_h       = 0;
    GLuint m_tex_items[4]       = {0}; // item icons (sword, trinkets, potion)
    int    m_tex_items_w[4]     = {0}, m_tex_items_h[4]   = {0};

    void*         m_last_buttons_ptr = nullptr;
    void*         m_last_overlays_ptr = nullptr;

    // Save editor state
    std::string              m_save_dir;
    std::vector<std::string> m_save_files;
    int                      m_selected_save = -1;
    InventoryState           m_inventory;
    bool                     m_inventory_dirty = false;
    std::string              m_status_msg;
    float                    m_status_timer = 0.0f;

    // Smooth FPS graph data
    static constexpr int FPS_HISTORY = 90;
    float m_fps_history[FPS_HISTORY] = {};
    int   m_fps_idx = 0;

    // ---- Lua Console state ----
    bool          m_console_ready  = false;
    bool          m_console_open   = false;
    bool          m_console_focus  = false; // request ImGui focus next frame

    uint8_t*      m_guest_memory   = nullptr;
    uint64_t      m_console_buf_addr     = 0;
    uint64_t      m_console_result_addr  = 0;
    uint64_t      m_console_pending_addr = 0;
    uint64_t      m_console_status_addr  = 0;
    uint64_t      m_console_print_addr   = 0;

    // ---- Scene Shifter guest memory pointers ----
    // All non-zero after init_scene_shifter() succeeds.
    // Each is a VA in guest address space; add m_guest_memory to get host ptr.
    bool     m_scene_shifter_ready    = false;
    uint64_t m_ss_list_count_va       = 0;  // int
    uint64_t m_ss_list_va             = 0;  // char[256][128]
    uint64_t m_ss_pending_va          = 0;  // volatile int
    uint64_t m_ss_target_va           = 0;  // char[128]
    uint64_t m_ss_spawn_va            = 0;  // char[64]
    uint64_t m_ss_error_va            = 0;  // char[256]
    uint64_t m_ss_active_va           = 0;  // int
    uint64_t m_ss_current_scene_va    = 0;  // char[128]
    std::string m_ss_assets_dir;            // host path: ~/.../assets/resources

    static constexpr int CONSOLE_MAX_HISTORY = 4096;

    std::vector<ConsoleEntry>  m_console_history;
    char                       m_console_input[16384] = {};
    std::vector<std::string>   m_console_cmd_history; // up-arrow recall
    int                        m_console_hist_idx = -1;
    bool                       m_console_scroll_bottom = false;

    // ---- Lua Script Editor ----
    bool                       m_script_editor_open = false;
    char                       m_script_editor_name[128] = "mod_script.lua";
    char                       m_script_editor_buf[32768] = "";
    std::string                m_script_editor_status = "Ready";
    float                      m_script_editor_status_color[4] = {0.5f, 0.5f, 0.5f, 1.0f};

    // ---- Lua Script Manager ----
    struct LuaScriptMeta {
        std::string filename;
        bool valid;
    };
    bool                       m_script_manager_open = false;
    std::vector<LuaScriptMeta> m_script_list;

    // ---- TCP Console Server (openport command) ----
    void start_tcp_server(int port);
    void stop_tcp_server();
    void tcp_server_loop();

    std::thread                m_tcp_thread;
    std::atomic<bool>          m_tcp_running{false};
    int                        m_tcp_server_fd = -1;
    std::atomic<int>           m_tcp_client_fd{-1};

    // Command pipeline (reader thread -> main thread -> guest -> back).
    //
    // The reader thread enqueues each complete Lua chunk into m_tcp_cmd_queue.
    // The main thread (update_console_backend) drains ONE chunk at a time,
    // dispatches it to the guest, and — when the guest's result comes back —
    // writes the result to the socket and signals m_tcp_done_cv so the reader
    // (which may be waiting to keep output ordered) can continue. This replaces
    // the old single-slot m_tcp_pending_cmd design that raced on paste and
    // dropped lines. Each queued item carries the client fd it belongs to so a
    // late result is never written to a newer client.
    //
    // Connection GENERATION: a client fd can be recycled by the OS, so keying
    // ownership on the fd alone leaks a dead connection's queued/in-flight
    // results onto the next client. Every accepted connection bumps
    // m_tcp_client_gen; each queued command captures the generation it was
    // submitted under, and a result is only written to the socket when its
    // generation still matches the live connection. On connect/disconnect we
    // purge the queue and clear the in-flight slot so the dispatcher can never
    // wedge on a command whose owner has gone away.
    struct TcpCmd { std::string code; int client_fd; uint64_t gen; };
    std::deque<TcpCmd>         m_tcp_cmd_queue;      // FIFO of pending chunks
    std::mutex                 m_tcp_mutex;          // guards queue + in-flight
    std::condition_variable    m_tcp_done_cv;        // signalled on result done
    bool                       m_tcp_cmd_in_flight = false;
    int                        m_tcp_in_flight_fd  = -1; // fd that owns result
    uint64_t                   m_tcp_in_flight_gen = 0;  // gen that owns result
    uint64_t                   m_tcp_client_gen    = 0;  // current connection gen
    uint64_t                   m_tcp_completed_seq = 0;  // monotonic completion
};
