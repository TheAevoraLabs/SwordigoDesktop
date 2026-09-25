#pragma once
// ============================================================================
// viewport_3d_widget.h — Native OpenGL 3D Viewport for Ruby GG
//   Directly integrates with Swordigo's av_renderer pipeline using QOpenGLWidget.
// ============================================================================

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QUndoStack>
#include "ruby/viewport/viewport_lighting.h"
#include "ruby/viewport/ruby_gizmo.h"
#include "ruby/render/viewport_shader.h"
#include "ruby/render/water_renderer.h"
#include "ruby/render/portal_renderer.h"
#include "ruby/render/particle_engine.h"
#include "ruby/render/light_rig.h"
#include "ruby/render/fbo_chain.h"
#include "ruby/render/ssao_pass.h"
#include "ruby/render/post_pass.h"
class QImage;
class QOpenGLShaderProgram;
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <filesystem>
namespace fs = std::filesystem;
#include <QElapsedTimer>
#include "tools/pod_loader.h"
#include "tools/scene_loader.h"
#include "tools/av_renderer.h"
#include "tools/boulder.h"
#include "tools/boulderx.h"   // boulderx::ground_generator() — generator choice
#include "tools/roller.h"     // zypher::generate_terrain_object() — Gen 3 modern generator
#include "ruby/viewport/camera_bounds_gizmo.h"
#include "ruby/render/glb_model.h"

class QToolButton;

namespace ruby::viewport {

class SceneLoadingOverlay;

class Viewport3DWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit Viewport3DWidget(QWidget* parent = nullptr);
    ~Viewport3DWidget() override;

    bool load_model(const std::string& pod_path);
    bool has_model() const { return m_has_model || m_has_glb; }
    bool has_glb() const { return m_has_glb; }
    const ruby::render::GLBModel& glb_model() const { return m_glb_model; }
    ruby::render::GLBModel& glb_model() { return m_glb_model; }
    const std::string& current_model_path() const { return m_current_model_path; }
    const av::PODModel& current_model() const { return m_model; }
    bool load_scene(const std::string& scene_path);
    // When in_memory_binary is set, the given re-encoded bytes are parsed
    // instead of the file on disk while scene_path stays the scene identity
    // (used to preview unsaved FileRift edits without saving to disk).
    void load_scene_async(const std::string& scene_path,
                          const std::string* in_memory_binary = nullptr);
    void evict_scene_cache(const std::string& scene_path);
    // Smart reload: replace the structured scene in place after a save / text
    // re-encode. When the new data has the same object structure (same model,
    // template, background, ground-mesh layout per index) the GPU display lists
    // and camera are reused and only transforms/components are patched — no
    // flash, no camera reset, no rebuild. Returns false when the structure
    // changed so the caller falls back to a full load_scene().
    bool apply_scene_data(const av::SceneData& scene, const std::string& scene_path);

    // ── Scene editing (structured, RAM-only until the host saves) ────────────
    // Transform gizmo modes for the active scene: 0 = off (view only),
    // 1 = Move, 2 = Rotate, 3 = Scale. See scene_editor_notes in the docs.
    enum GizmoMode { GizmoOff = 0, GizmoMove = 1, GizmoRotate = 2, GizmoScale = 3 };
    void set_gizmo_mode(int mode);
    int  gizmo_mode() const { return m_gizmo_mode; }
    int  selected_object() const { return m_selected_scene_object; }
    // Live, in-RAM scene the gizmo edits (never touches disk). The host marks
    // the document dirty on sceneEdited() and persists through its own path.
    av::SceneData& editable_scene() { return m_scene; }
    // After the host mutates editable_scene() directly (toolbars, panels), call
    // this to refresh the GPU matrices + RAM caches for the selected object.
    void refresh_edited_object();
    // Revert the in-progress gizmo drag to its drag-start snapshot. Called when
    // the host refuses to accept the edit (e.g. the scene has unsaved FileRift
    // text edits) — otherwise the already-mutated RAM value would silently fold
    // into the next save.
    void revert_pending_scene_edit();
    // Show a single decoded texture (PVR/PNG/JPG) as a camera-facing poster
    // standing on the grid — the "texture in 3D space" viewer mode.
    bool load_texture_preview(const std::string& image_path);
    // Headless/self diagnostic: exercise the VBO upload → draw → free path and
    // the per-pod cache (parity doc §8) with a synthetic mesh. Returns true
    // when every check passes. Called with the widget's GL context current.
    bool gpu_self_test();

    // Scene identity of the structured scene currently on screen ("" = none).
    const std::string& current_scene_path() const { return m_current_scene_path; }
    // Copy of the structured scene held for `path` — from the live viewport
    // when it is showing that scene, otherwise from the path's own session
    // (the widget keeps one isolated session per open scene document; nothing
    // is shared between them). Returns false when nothing is held for path.
    bool try_get_scene(const std::string& path, av::SceneData& out) const;
    // Structured-save completion: the RAM scene WAS just written to disk byte-
    // verbatim (av::scene_save serializes it), so the viewport keeps its state
    // untouched — no reload, no re-parse, no camera or selection reset.
    void mark_scene_saved(const std::string& path);
    // Disk-side validation used before handing a scene to the engine preview:
    // guarded read, binary-format check, parse, and a serialize→decode round
    // trip. Returns false + error text when the file must not reach the game.
    static bool validate_scene_bytes(const std::vector<uint8_t>& bytes,
                                     const std::string& identity, std::string& error);
    static bool read_file_guarded(const std::string& path,
                                  std::vector<uint8_t>& out, std::string& error);
    void reset_camera();
    void set_frame(float frame);
    void set_playing(bool playing);
    void set_selected_object(int index);
    int frame_count() const;
    void set_animation_clip(int clip_index);
    QStringList animation_clips() const;
    int active_animation_clip() const;

    struct TransformState {
        float pos[3];
        float rot[3];
        float scale[3];
    };
    QUndoStack* undo_stack();
    void undo();
    void redo();
    void apply_object_transform(int object_index, const TransformState& t);

    // ── Scene-wide snapshot undo (web-editor parity) ─────────────────────────
    // Structural and component edits are undoable through the per-scene
    // QUndoStack: capture the full re-encoded scene binary BEFORE a mutation,
    // perform it, then push_scene_snapshot_undo() — the command restores either
    // snapshot through the fast in-place path (structure-preserving: camera and
    // GPU state survive) or a full rebuild for structural changes. Returns ""
    // when no scene is loaded (callers should skip the push in that case).
    std::string capture_scene_snapshot() const;
    void push_scene_snapshot_undo(std::string before, const QString& label);
    bool restore_scene_snapshot(const std::string& bytes);

    // Renderer display options (driven by the Tools > Settings panel).
    void set_wireframe(bool enabled);
    void set_grid_visible(bool visible);
    void set_render_effects(bool on);
    bool render_effects() const { return m_render_effects_enabled; }

    // Lighting rig (sun + fill + bounce + ambient + fog), adjustable live from
    // the Lighting dock — the Pod/Scene viewer equivalent of the ImGui lights.
    void set_lighting(const ViewportLighting& lighting);
    const ViewportLighting& lighting() const { return m_lighting; }
    const av::SceneData& scene() const { return m_scene; }
    // True only when a structured scene is fully loaded and interactive. During
    // an in-flight async load the widget still holds the previous scene's data
    // but is NOT ready — picks/gizmo/saves are refused so an edit can never be
    // applied to the wrong scene (the cross-scene corruption vector).
    bool has_scene() const { return m_has_scene && m_scene_ready; }

    struct CameraState {
        float pitch = 15.0f;
        float yaw   = -45.0f;
        float dist  = 120.0f;
        float target[3] = {0.0f, 20.0f, 0.0f};
    };
    CameraState camera_state() const {
        CameraState s;
        s.pitch = m_cam_pitch;
        s.yaw = m_cam_yaw;
        s.dist = m_cam_dist;
        s.target[0] = m_cam_target[0];
        s.target[1] = m_cam_target[1];
        s.target[2] = m_cam_target[2];
        return s;
    }
    void set_camera_state(const CameraState& s) {
        m_cam_pitch = s.pitch;
        m_cam_yaw = s.yaw;
        m_cam_dist = s.dist;
        m_cam_target[0] = s.target[0];
        m_cam_target[1] = s.target[1];
        m_cam_target[2] = s.target[2];
        // Also remember as the pending camera: an in-flight async scene load
        // must NOT clobber an explicitly requested per-document camera (the
        // completion handler re-applies it after adopt_session_state). Without
        // this, switching documents silently loses each scene's saved camera.
        m_pending_cam = s;
        m_has_pending_cam = true;
        update();
    }

    // Scene object manipulation (called from SceneHierarchyPanel and Inspector)
    void focus_object(int index);
    void add_scene_object(const QString& kind, const float* spawn_pos = nullptr);
    int add_ground_mesh_object(av::SceneObject obj);
    void duplicate_scene_object(int index);
    void delete_scene_object(int index);
    void set_scene_object_visibility(int index, bool visible);

    // ── Scene-object clipboard + selection utilities (parity with the ImGui
    // asset viewer): Ctrl+C copies the active object, Ctrl+V pastes it with a
    // fresh identifier and a nudge (cross-scene safe — model/background caches
    // are reloaded), Ctrl+D duplicates (copy + paste), Delete/Backspace removes
    // it, Alt+Up/Down reorders it in the scene.
    void copy_scene_selection();
    void paste_scene_selection();
    void duplicate_scene_selection();
    void delete_scene_selection();
    void move_scene_object(int direction);   // -1 = up (earlier), +1 = down
    bool has_scene_clipboard() const { return !m_scene_clipboard.empty(); }

    // ── In-scene mesh edit (projection-locked 2D polygon editor) ────────────
    void set_mesh_edit(bool on);
    bool mesh_edit_active() const { return m_mesh_edit; }
    bool can_mesh_edit() const;

    // ── Template palette / template hierarchy ──────────────────────────────
    int add_template_object(const QString& template_name,
                            const av::SceneObject* template_object = nullptr,
                            float template_scaling = 1.0f,
                            const float* spawn_pos = nullptr);
    int add_model_object(const QString& pod_path, const QString& display_name = QString(),
                         const float* spawn_pos = nullptr);
    void set_scene_object_template(int index, const QString& template_name);
    void materialize_scene_object_template(int index);
    void override_inherited_component(int index, const QString& class_name);
    void reset_scene_object_to_template(int index);

    // ── Camera Bounds (Tag 3 Scene Rectangle) ──────────────────────────────
    bool show_camera_bounds() const { return m_show_camera_bounds; }
    void toggle_show_camera_bounds(bool show);
    void set_camera_bounds(const av::CameraBounds& bounds);
    void fit_camera_bounds();
    void remove_camera_bounds();
    void frame_camera_bounds();
    bool has_camera_bounds() const;
    av::CameraBounds camera_bounds() const;

signals:
    void cameraBoundsChanged(const av::CameraBounds& bounds);
    void cameraBoundsSelected(bool selected);
    void modelLoaded(const QString& name, int meshCount, int vertCount);
    void sceneLoaded(const av::SceneData& scene);
    void sceneLoadingFailed(const QString& error);
    void frameStutterDetected(double fps, double frameTimeMs, double renderTimeMs, const QString& details);
    // An object was picked in the viewport (host syncs the hierarchy panel).
    void sceneObjectSelected(int index);
    // Real-time transform update emitted during gizmo dragging for inspector sync
    void sceneObjectTransformed(int index,
                                float px, float py, float pz,
                                float rx, float rz, float ry,
                                float sx, float sy, float sz);
    // A transform drag finished and the in-RAM scene changed. The host marks
    // the active scene document dirty and rebuilds its RAM/file buffer.
    void sceneEdited();
    // The render state of an object changed (visibility, component data, ground
    // mesh geometry, add/remove component). The host refreshes the inspector
    // panel for the affected object when it is the current selection.
    void sceneObjectRenderStateChanged(int object_index);
    // A single component field was mutated (by a tool, material pick, or other
    // non-inspector path). The host can update just that one inspector widget
    // instead of rebuilding the full component UI.
    void sceneObjectFieldChanged(int object_index, int component_index,
                                const av::SceneComponentField& field);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // ── Camera Bounds interactive state ──
    CameraBoundsGizmo m_bounds_gizmo;
    bool m_show_camera_bounds = false;
    QToolButton* m_bounds_btn = nullptr;
    bool m_camera_bounds_selected = false;
    BoundsHandle m_bounds_hover_handle = BoundsHandle::None;
    BoundsHandle m_bounds_active_handle = BoundsHandle::None;
    av::CameraBounds m_bounds_drag_initial;
    float m_bounds_drag_start_world_x = 0.0f;
    float m_bounds_drag_start_world_y = 0.0f;
    bool m_bounds_dragging = false;

    // ── Viewport Drag-and-drop state ──
    bool m_drag_hover_active = false;
    QPointF m_drag_hover_screen_pos;
    float m_drag_hover_world[3] = {0.0f, 0.0f, 0.0f};
    QString m_drag_hover_label;

    void show_viewport_context_menu(const QPoint& screen_pos);
    // ── Scene-editing state (RubyGizmo transform gizmo) ──
    QWidget* m_gizmo_bar = nullptr;
    int  m_gizmo_mode   = 0;      // 0 off, 1 move, 2 rotate, 3 scale

    // RubyGizmo: our bespoke, HiDPI-correct gizmo replacing im3d.
    ruby::gizmo::RubyGizmo m_gizmo;

    // Cursor state fed to the gizmo each frame.
    QPointF m_gizmo_cursor = QPointF(-1.0, -1.0);
    bool m_gizmo_lmb   = false;   // LMB held (fed to gizmo every frame)
    bool m_gizmo_grab  = false;   // LMB went down over the gizmo
    bool m_gizmo_edited = false;  // current drag changed values (emit once)
    bool m_gizmo_had_snapshot = false; // drag-start values captured (Esc cancel)
    float m_gizmo_snap_pos[3]   = {0,0,0};
    float m_gizmo_snap_rot[3]   = {0,0,0};  // engine fields: rot_x, rot_z, rot_y
    float m_gizmo_snap_scale[3] = {1,1,1};
    // Full-scene snapshot captured at scale-drag start so the payload geometry
    // (LocalAABB, Shape/CollisionShape) can scale with the transform and one
    // snapshot undo restores both atomically.
    std::string m_gizmo_snap_scene;
    // View-projection captured each paintGL so the gizmo render pass draws with
    // exactly the same camera as the scene.
    float m_gizmo_proj[16] = {0};
    float m_gizmo_view[16] = {0};
    float m_gizmo_eye[3]   = {0};
    QPoint m_mouse_press_pos;

    // ── Modern GLSL shader pipeline ──────────────────────────────────────────
    ruby::render::ViewportShader m_viewport_shader;
    ruby::render::LightRig       m_light_rig;        // managed light set
    float m_cached_viewproj[16] = {0}; // view*proj for shader uploads
    float m_cached_eye[3]       = {0}; // camera eye for shader fog/rim

    // ── Post-processing pipeline (FBO → SSAO → Composite) ────────────────────
    ruby::render::FboChain  m_fbo_chain;   // scene + AO framebuffers
    ruby::render::SsaoPass  m_ssao_pass;   // SSAO + blur passes
    ruby::render::PostPass  m_post_pass;   // final AO composite
    bool                    m_post_ready = false;  // true if all 3 inited OK
    int                     m_fb_width   = 0;      // physical framebuffer pixel width
    int                     m_fb_height  = 0;      // physical framebuffer pixel height

    // ── In-game dynamic effects (water, portals, particles, torch flicker) ────
    ruby::render::WaterRenderer  m_water_renderer;
    ruby::render::PortalRenderer m_portal_renderer;
    ruby::render::ParticleEngine m_particle_engine;
    bool m_render_effects_enabled = true;
    float m_effects_time = 0.0f;
    QElapsedTimer m_effects_clock;
    QToolButton* m_fx_btn = nullptr;
    GLuint m_particle_tex = 0;
    void ensure_effects_resources();


    SceneLoadingOverlay* m_loading_overlay = nullptr;
    // True once initializeGL() ran on a real GL surface. Guards the texture/
    // mesh GL calls so a load racing the first paint returns 0 instead of
    // segfaulting the GL driver with no current context.
    bool m_gl_initialized = false;
    uint64_t m_scene_load_seq = 0;
    // Per-scene load token: a load completion only lands when it is still the
    // NEWEST load for ITS OWN scene (path match + seq match), so activating
    // another document can never cancel or corrupt a scene's pending load.
    std::string m_scene_load_token;
    // Camera state (live values for the scene on screen)
    float m_cam_pitch = 15.0f;
    float m_cam_yaw   = -45.0f;
    float m_cam_dist  = 120.0f;
    float m_cam_target[3] = {0.0f, 20.0f, 0.0f};
    // Explicit per-document camera requested while a scene load is in flight.
    // Cleared when a new load starts / the completion consumes it, so an async
    // load can never overwrite a deliberately restored document camera with an
    // autofit camera or a stashed session camera.
    CameraState m_pending_cam;
    bool m_has_pending_cam = false;

    // ── Camera dynamics (asset_viewer-grade feel, no ImGui) ─────────────────
    // Orbit/pan keep their release velocity and glide with exponential damping;
    // wheel zoom is exponential and converges on the selection / spawn point.
    float m_orbit_vel_yaw = 0.0f;      // deg/s (release inertia)
    float m_orbit_vel_pitch = 0.0f;    // deg/s
    float m_pan_vel[3] = {0.0f, 0.0f, 0.0f}; // world units/s
    QElapsedTimer m_move_timer;        // dt between mouse-move events
    bool m_move_timer_valid = false;
    float m_cam_orbit_speed = 1.0f;    // user-adjustable feel multipliers
    float m_cam_pan_speed   = 1.0f;
    float m_cam_zoom_speed  = 1.0f;
    void update_camera_dynamics(float dt_seconds);

    QPoint m_last_mouse_pos;
    bool m_orbiting = false;
    bool m_panning = false;
    bool m_rmb_down = false;
    std::unordered_map<std::string, std::unique_ptr<QUndoStack>> m_scene_undo_stacks;
    QUndoStack m_default_undo_stack;
    bool m_wireframe = false;
    bool m_show_textures = true;
    bool m_show_skeleton = false;
    bool m_show_grid = true;

    // Texture-preview state (a decoded image standing in 3D space).
    bool m_has_texture = false;
    GLuint m_texture_tex = 0;
    float m_texture_aspect = 1.0f;

    // Imported (non-POD) model state: GLB/GLTF/OBJ parse into an av::PODModel;
    // embedded base-color images are decoded into GL textures indexed by the
    // node's material slot, and DCC top-origin UVs are flipped at sample time.
    bool m_has_import = false;
    bool m_import_flip_v = true;
    std::vector<GLuint> m_import_textures;

    ViewportLighting m_lighting;
    float m_frame = 0.0f;
    float m_scene_extent = 400.0f;
    int m_selected_scene_object = -1;

    // Scene-object clipboard (Ctrl+C/Ctrl+V). Multi-object vector so future
    // multi-select pastes work; currently holds the active object. m_paste_count
    // grows each paste to cascade pasted copies (ImGui viewer parity).
    std::vector<av::SceneObject> m_scene_clipboard;
    int m_paste_count = 0;

    // ── In-scene mesh edit state ────────────────────────────────────────────
    bool m_mesh_edit = false;
    int  m_mesh_edit_object = -1;                  // scene index of the edited object
    std::vector<boulder::PolygonPoint> m_mesh_points;   // live polygon (local XY, doubles)
    boulder::GroundMesh m_mesh_params;             // imported generator params (depth, textures…)
    boulder::GroundComponentIds m_mesh_ids;        // preserved GroundMesh component ids
    double m_mesh_z = 40.0;                        // depth layer (object pos_z)
    // Generator parameters that used to be dropped on import, so every edit
    // rebuilt the sheet as a MeshType 1 slab with zero noise regardless of what
    // the object was (field 7 MeshType, field 6 HorizNoise).
    int    m_mesh_type = 1;
    double m_mesh_noise = 0.0;
    // Which generator actually produced the mesh on screen ("BoulderX", or
    // "Boulder · BoulderX declined: <reason>"). The Boulder fallback is the ONE
    // remaining path that re-emits a uniform-thickness slab, so it must never be
    // silent: mobile has shown this since the decline was added
    // (RubyQuickViewport::groundGeneratorStatus), and the desktop editor falling
    // back without saying so is indistinguishable from "BoulderX is broken".
    QString m_mesh_generator_status;
    bool m_mesh_generator_declined = false;   // BoulderX was asked for and refused
    // Per-node depth, recovered from the mesh the object shipped with (see
    // mesh_import). Empty means the object's own mesh had none to recover, and
    // the edit keeps its uniform depth.
    std::vector<double> m_mesh_front_z;
    std::vector<double> m_mesh_back_z;
    std::vector<double> m_mesh_vanilla_front_z;
    std::vector<double> m_mesh_vanilla_back_z;
    av::SceneData m_mesh_scene_saved;              // R / Ctrl+Z whole-session revert target
    bool m_mesh_scene_saved_valid = false;
    bool m_mesh_dirty = false;
    int  m_mesh_drag_point = -1;
    bool m_mesh_dragging = false;
    bool m_mesh_drag_from_insert = false;          // RMB-insert drag stays alive on RMB
    double m_mesh_drag_off_x = 0.0, m_mesh_drag_off_y = 0.0;
    int  m_mesh_hover_vertex = -1;
    int  m_mesh_hover_edge = -1;
    double m_mesh_snap = 25.0;                     // grid snap units (0 = off; G toggles)
    float m_mesh_saved_cam_pitch = 15.0f, m_mesh_saved_cam_yaw = -45.0f;
    float m_mesh_saved_cam_dist = 120.0f;
    float m_mesh_saved_cam_target[3] = {0.0f, 20.0f, 0.0f};
    bool  m_mesh_saved_cam_valid = false;
    bool  m_mesh_panning = false;                  // MMB pan inside the locked view
    QPointF m_mesh_pan_start_px;
    float  m_mesh_pan_start_target[3] = {0.0f, 0.0f, 0.0f};
    QElapsedTimer m_mesh_live_timer;               // throttles live re-apply during drags
    QToolButton* m_mesh_btn = nullptr;             // gizmo-bar toggle (state sync)

    void begin_mesh_edit();
    void end_mesh_edit(bool apply);
    void mesh_import(int idx);                     // polygon + params from the object
    void mesh_sync_relief_to_polygon();            // keep per-node depth lockstep with the polygon
    bool mesh_apply();                             // boulder regenerate + GPU re-upload
    void mesh_resync_gpu(int idx);
    void mesh_revert_session();
    void draw_mesh_edit_overlay();                 // QPainter overlay in paintGL
    void mesh_update_hover(const QPointF& px);
    av::Camera mesh_camera() const;
    bool mesh_local_to_screen(const av::SceneObject& obj, double lx, double ly, QPointF& out) const;
    bool mesh_ray_object_plane(const av::SceneObject& obj, const float origin[3],
                               const float dir[3], double& lx, double& ly) const;
    bool mesh_screen_ray(const QPointF& px, float origin[3], float dir[3]) const;
    bool mesh_edit_mouse_press(QMouseEvent* e);
    bool mesh_edit_mouse_move(QMouseEvent* e);
    bool mesh_edit_mouse_release(QMouseEvent* e);
    bool mesh_edit_key(QKeyEvent* e);

    // Loaded model state
    av::PODModel m_model;
    av::SceneData m_scene;
    bool m_has_scene = false;
    bool m_scene_ready = false;   // load landed and the scene is interactive
    bool m_has_model = false;
    ruby::render::GLBModel m_glb_model;
    bool m_has_glb = false;
    std::string m_current_scene_path;
    std::string m_current_model_path;
    std::string m_current_texture_path;
    std::vector<GLuint> m_material_textures;
    std::map<std::string, av::PODModel> m_scene_models;
    std::map<std::string, std::vector<GLuint>> m_scene_model_textures;
    std::vector<std::vector<GLuint>> m_scene_ground_textures;
    std::map<std::string, GLuint> m_scene_background_textures;
    std::unordered_map<std::string, GLuint> m_gpu_tex_cache;

    // A POD mesh uploaded to GPU buffers. The geometry lives in VBOs/EBO so
    // draws skip the CPU-side client arrays, and buffers are reusable across
    // scene loads / save reloads via m_pod_gpu_cache. Replaces the old fixed-
    // function display-list capture (see parity doc §8).
    struct MeshGpu {
        GLuint pos_vbo = 0;  // positions (always)
        GLuint nrm_vbo = 0;  // normals (optional)
        GLuint uv_vbo  = 0;  // UVs (optional)
        GLuint ebo     = 0;  // index buffer (optional)
        int    index_count  = 0;
        int    vertex_count = 0;
        bool   valid() const { return pos_vbo != 0 && vertex_count > 0; }
    };

    // High-performance hardware acceleration
    struct SceneRenderObject {
        float pos[3];
        float world_matrix[16];
        float render_matrix[16];
        bool hidden;
        bool is_portal;
        bool is_spawn_point;
        bool is_camera;
        bool is_dimension_object;
        int object_index;
        std::string name;
        std::string mesh_name;
        std::string local_aabb;
        float diffuse_color[3] = {1.0f, 1.0f, 1.0f};
        bool has_diffuse_color = false;
        std::vector<GLuint> ground_textures;
        std::vector<std::string> ground_tex_names;   // per-mesh texture names (live-resync reuse)
        std::vector<MeshGpu> ground_gpu;
    };
    std::vector<SceneRenderObject> m_render_objects;

    std::vector<MeshGpu> m_model_mesh_gpu;
    std::map<std::string, std::vector<MeshGpu>> m_scene_model_gpu;
    std::vector<MeshGpu> m_all_mesh_gpu;

    // Per-pod GPU buffer cache: keyed by the resolved pod file path so that
    // re-loading a scene (or a save-triggered reload that evicts the scene
    // cache) reuses the already-uploaded VBO/EBO instead of recompiling every
    // mesh on the main thread — the dominant repeated load cost. Geometry is
    // immutable once a pod is parsed, so reuse across loads is safe.
    std::unordered_map<std::string, std::vector<MeshGpu>> m_pod_gpu_cache;

    // ── Per-document scene session ──────────────────────────────────────────
    // One fully isolated session per open scene document. Sessions NEVER share
    // scene data, camera, selection, tool mode or (non-pod) GPU state — two
    // open scenes cannot corrupt each other, and switching tabs restores each
    // document's exact editor state (0 ms — everything is already in RAM).
    struct SceneSession {
        av::SceneData scene;
        std::map<std::string, av::PODModel> scene_models;
        std::map<std::string, std::vector<GLuint>> scene_model_textures;
        std::vector<std::vector<GLuint>> scene_ground_textures;
        std::map<std::string, GLuint> scene_background_textures;
        std::vector<SceneRenderObject> render_objects;
        std::vector<MeshGpu> model_mesh_gpu;
        std::map<std::string, std::vector<MeshGpu>> scene_model_gpu;
        std::vector<MeshGpu> all_mesh_gpu;
        float scene_extent = 400.0f;
        // Per-document editor state (what used to bleed across scenes).
        bool has_editor_state = false;
        float cam_pitch = 15.0f, cam_yaw = -45.0f, cam_dist = 120.0f;
        float cam_target[3] = {0.0f, 20.0f, 0.0f};
        int   selected_object = -1;
        int   gizmo_mode = 0;
    };
    std::unordered_map<std::string, std::shared_ptr<SceneSession>> m_scene_cache;

    // Widget-local parse cache keyed by path + file size + mtime — a re-parsed
    // load only happens when the file actually changed on disk. Replaces the
    // old process-wide s_ram_scene_cache (shared mutable state between open
    // scenes was the main corruption source).
    struct ParseEntry {
        av::SceneData scene;
        uintmax_t file_size = 0;
        int64_t   mtime_ns = 0;
    };
    std::unordered_map<std::string, ParseEntry> m_parse_cache;
    std::mutex m_parse_cache_mutex;

    // Stash camera/selection/tool of the scene about to leave the screen into
    // its session; restore them from the arriving scene's session.
    void stash_current_session();
    void adopt_session_state(const std::string& scene_path);

    // Reusable skinning buffers (no per-frame heap churn)
    std::vector<float> m_scratch_positions;
    std::vector<float> m_scratch_normals;

    // High-precision frame stutter detection
    std::chrono::steady_clock::time_point m_last_frame_time{};
    std::chrono::steady_clock::time_point m_last_stutter_log_time{};

    // Animated object selection effect (lift + spring shake + subtle brightness)
    std::chrono::steady_clock::time_point m_select_anim_start{};
    int m_select_anim_obj = -1;

    void clear_mesh_gpu();
    MeshGpu upload_mesh_gpu(const av::PODMesh& mesh);
    // Upload all meshes of a pod, consulting m_pod_gpu_cache first. Returns the
    // per-node buffers (referencing the cache when already uploaded).
    std::vector<MeshGpu> upload_pod_gpu(const std::string& pod_path,
                                        const av::PODModel& model);
    void draw_mesh_gpu(const MeshGpu& g);
    void free_mesh_gpu(MeshGpu& g);

    void apply_lighting();
    void apply_fog();
    void upload_current_model_matrix();

    // ViewCube camera controller state (exact 1:1 match with ImGuizmo::ViewManipulate)
    bool m_view_cube_hover = false;
    int  m_view_cube_over_box = -1;       // 0..26 (faces, edges, corners)
    bool m_view_cube_dragging = false;
    bool m_view_cube_clicking = false;
    int  m_view_cube_anim_frames = 0;
    float m_view_cube_target_pitch = 0.0f;
    float m_view_cube_target_yaw = 0.0f;

    // Scene-editing helpers (implemented in viewport_3d_widget.cpp).
    void draw_ruby_gizmo();       // RubyGizmo transform gizmo over the selected object
    void draw_view_cube();        // Interactive camera orientation viewcube (top-right)
    int hit_test_view_cube(const QPointF& pt) const;
    void get_cube_view_matrix(float out[16]) const;
    void pick_object_at(const QPointF& pos);
    void sync_edited_object_caches();  // m_scene → render objects + session caches
    void compute_missing_normals(av::PODMesh& mesh);
    // Scene-root search list mirroring load_scene's asset roots (project dir +
    // imported library dirs) — used by paste to resolve models/textures that may
    // belong to a different scene.
    std::vector<fs::path> current_scene_roots() const;
    // Paste support: load a model/background into the RAM + GPU caches when the
    // copied object came from another scene (whose resources were evicted).
    void ensure_pasted_model_resources(const av::SceneObject& obj);
    void ensure_pasted_background_resources(const std::string& bg_name);
    // Build the SceneRenderObject for m_scene.objects[index] and append it —
    // uploads ground-mesh GPU buffers/textures when present. GL must be current.
    int append_render_object_for_index(int index);
    // Replace the render entry for one object in place (template retarget /
    // materialize changes which mesh/background it draws). Model + background
    // caches are consulted via ensure_pasted_* so nothing re-downloads.
    void rebuild_render_object_for_index(int index);
    // Mesh-accurate selection: triangle-edge wireframe + inverted-hull outline
    // drawn with the object's exact render transforms (3D, depth-tested) —
    // replaces the old axis-aligned box drawn over the object.
    void draw_selection_highlight();
    // Make the GL function-pointer table usable before the first paint.
    // QTabBar::insertTab fires currentChanged SYNCHRONOUSLY, so opening the
    // first document used to run load_model → glGenBuffers through an
    // uninitialized QOpenGLFunctions table (initializeGL had not run yet) and
    // segfaulted. This lazily makes current + initializes the table; returns
    // false only when no GL surface can be created at all.
    bool ensure_gl_ready();
    void draw_mesh_edges(const MeshGpu& g, const av::PODMesh& src, uint64_t key,
                         float r, float g_, float b, float a, float line_w);
    void draw_mesh_rim(const MeshGpu& g, const av::PODMesh& src, float offset,
                       float r, float g_, float b, float a);
    // Cached GL_LINES index buffers powering the selection wireframe (rebuilt
    // automatically when the source mesh's GPU buffers change).
    struct SelLineCache { GLuint ebo = 0; int line_count = 0; GLuint src_pos_vbo = 0; int src_vertex_count = 0; };
    std::unordered_map<uint64_t, SelLineCache> m_sel_line_cache;
    // Guarded parse (size+mtime-validated, FileRift-text recovery, exact read).
    av::SceneData parse_scene_from_disk(const std::string& scene_path);
    void draw_grid();
    void draw_scene_background();
    void draw_texture_poster();
    void draw_model();
    void draw_scene();
    void draw_pod_instance(const av::PODModel& model, const std::vector<GLuint>& textures,
                           const std::vector<MeshGpu>* gpu_meshes, float frame,
                           const float* tint_color = nullptr);
    // PVR/TEX (pvr_loader) or PNG/JPEG (QImage) — game packs ship many
    // background layers as *.tex.png, so PVR-only loading left scenes dark.
    GLuint load_texture_any(const std::string& path);
    GLuint upload_image(const QImage& image);
    std::vector<GLuint> load_pod_textures(const std::filesystem::path& pod_path, const av::PODModel& model,
                                         const std::vector<std::filesystem::path>& extra_roots = {});
    void draw_skeleton();
};

} // namespace ruby::viewport
