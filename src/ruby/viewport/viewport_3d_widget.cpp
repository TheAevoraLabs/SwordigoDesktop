// ============================================================================
// viewport_3d_widget.cpp — Native OpenGL 3D Viewport Implementation
// ============================================================================

#include "viewport_3d_widget.h"
#include "scene_loading_overlay.h"
#include "ruby/render/light_rig.h"
#include "ruby/render/material_params.h"
#include <QOpenGLExtraFunctions>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QApplication>
#include <QToolButton>
#include <QHBoxLayout>
#include <QCursor>
#include <QPainter>
#include <QPolygonF>
#include <QPainterPath>
#include "ruby/core/project_context.h"
#include "platform/pvr_loader.h"
#include "tools/gltf_glb.h"
#include "tools/image_decode.h"   // WebP fallback for EXT_texture_webp (Qt may lack the plugin)
#include "tools/obj_loader.h"
#include "tools/fbx_import.h"
#include "tools/scene_asset_resolver.h"
#include "tools/scene_workspace.h"
#include "tools/filerift.h"
#include <cfloat>
#include <unordered_map>
#include <unordered_set>
#include <future>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iostream>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QSaveFile>
#include <QUndoCommand>
#include <QMenu>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include "ruby_picking.h"
#include "ruby/math/ruby_math.h"
// RubyGizmo — our bespoke HiDPI-correct gizmo (no im3d dependency).
// The header is already pulled in via viewport_3d_widget.h.

namespace fs = std::filesystem;

namespace {
class TransformUndoCommand : public QUndoCommand {
public:
    TransformUndoCommand(ruby::viewport::Viewport3DWidget* vp, int object_index,
                         const ruby::viewport::Viewport3DWidget::TransformState& old_t,
                         const ruby::viewport::Viewport3DWidget::TransformState& new_t,
                         QUndoCommand* parent = nullptr)
        : QUndoCommand(parent), m_vp(vp), m_object_index(object_index),
          m_old_t(old_t), m_new_t(new_t) {
        setText(QString("Transform Object %1").arg(object_index));
    }

    void undo() override {
        if (m_vp) m_vp->apply_object_transform(m_object_index, m_old_t);
    }

    void redo() override {
        if (!m_first) {
            if (m_vp) m_vp->apply_object_transform(m_object_index, m_new_t);
        }
        m_first = false;
    }

private:
    ruby::viewport::Viewport3DWidget* m_vp = nullptr;
    int m_object_index = -1;
    ruby::viewport::Viewport3DWidget::TransformState m_old_t;
    ruby::viewport::Viewport3DWidget::TransformState m_new_t;
    bool m_first = true;
};

// Scene-wide snapshot undo (web-editor model): the undo unit is the whole
// re-encoded scene binary, so EVERY mutation kind (structural, component,
// visibility, mesh-commit) is undoable through the same per-scene QUndoStack.
// Restore prefers the fast in-place apply (structure-preserving, camera kept);
// structural changes fall back to a full rebuild from the snapshot bytes.
class SceneSnapshotUndoCommand : public QUndoCommand {
public:
    SceneSnapshotUndoCommand(ruby::viewport::Viewport3DWidget* vp,
                             std::string before, std::string after,
                             const QString& label, QUndoCommand* parent = nullptr)
        : QUndoCommand(parent), m_vp(vp),
          m_before(std::move(before)), m_after(std::move(after)) {
        setText(label);
    }

    void undo() override {
        if (m_vp) m_vp->restore_scene_snapshot(m_before);
    }

    void redo() override {
        if (!m_first) {
            if (m_vp) m_vp->restore_scene_snapshot(m_after);
        }
        m_first = false;
    }

private:
    ruby::viewport::Viewport3DWidget* m_vp = nullptr;
    std::string m_before;
    std::string m_after;
    bool m_first = true;
};

// ── High-Performance RAM Caches (offload SSD I/O directly into RAM) ──
// NOTE: only *immutable* data is cached here. Scene bytes are deliberately NOT
// process-global anymore — scenes live in per-document sessions inside the
// widget (SceneSession), so two open scenes can never share/corrupt state.
static std::unordered_map<std::string, QImage> s_ram_image_cache;
static std::mutex s_ram_image_mutex;

static std::unordered_map<std::string, av::PODModel> s_ram_pod_cache;
static std::mutex s_ram_pod_mutex;

// Guarded file read: exact-size read with hard error reporting. Returns false
// (with a message) when the file cannot be fully read — callers must never
// treat a short/truncated read as "empty scene".
static bool guarded_read_file(const std::string& path, std::vector<uint8_t>& out,
                              std::string& error) {
    out.clear();
    error.clear();
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(path), ec)) {
        error = "not a regular file: " + path;
        return false;
    }
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        error = "cannot open for read: " + path;
        return false;
    }
    const std::streamoff size = in.tellg();
    if (size <= 0) {
        error = "empty or unreadable: " + path;
        return false;
    }
    out.resize(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(out.data()), size);
    if (!in || in.gcount() != size) {
        out.clear();
        error = "short read (expected " + std::to_string(size) + " bytes): " + path;
        return false;
    }
    return true;
}

// Parse scene bytes into a SceneData. A FileRift text file (banner at byte 0)
// is re-encoded in memory first — the disk file is left untouched.
static av::SceneData parse_scene_bytes(const std::vector<uint8_t>& buf,
                                       const std::string& scene_path,
                                       const std::vector<std::string>& extra_roots,
                                       std::string& error) {
    error.clear();
    av::SceneData scene;
    if (buf.empty()) { error = "no bytes to parse"; return scene; }
    const size_t head = std::min<size_t>(buf.size(), 64);
    std::string banner(reinterpret_cast<const char*>(buf.data()), head);
    try {
        if (banner.rfind("## FileRift decoded", 0) == 0) {
            std::cerr << "[viewport] warning: " << scene_path
                      << " is FileRift text, not binary — re-encoding in memory\n";
            const std::string bin = ::filerift::recode_markup(
                std::string(buf.begin(), buf.end()), "scene");
            scene = av::scene_load_bytes(
                std::vector<uint8_t>(bin.begin(), bin.end()), scene_path, extra_roots);
        } else {
            scene = av::scene_load_bytes(buf, scene_path, extra_roots);
        }
    } catch (const std::exception& e) {
        error = std::string("parse failed: ") + e.what();
        return av::SceneData{};
    } catch (...) {
        error = "parse failed (unknown error)";
        return av::SceneData{};
    }
    if (scene.objects.empty()) error = "parsed scene contains no objects";
    return scene;
}

static void get_process_perf_info(double& out_rss_mb, double& out_vms_mb, int& out_threads) {
    out_rss_mb = 0.0;
    out_vms_mb = 0.0;
    out_threads = 1;
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        unsigned long size_pages = 0, resident_pages = 0;
        if (statm >> size_pages >> resident_pages) {
            long page_size = sysconf(_SC_PAGESIZE);
            out_vms_mb = (size_pages * page_size) / (1024.0 * 1024.0);
            out_rss_mb = (resident_pages * page_size) / (1024.0 * 1024.0);
        }
    }
    std::ifstream status("/proc/self/status");
    if (status.is_open()) {
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("Threads:", 0) == 0) {
                std::istringstream iss(line.substr(8));
                iss >> out_threads;
                break;
            }
        }
    }
#endif
}

static std::string strip_image_extensions(const std::string& name) {
    std::string s = name;
    while (true) {
        std::string low = s;
        for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.size() >= 8 && low.substr(low.size() - 8) == ".tex.png") {
            s = s.substr(0, s.size() - 8);
        } else if (low.size() >= 4 && (low.rfind(".png") == low.size() - 4 ||
                                       low.rfind(".pvr") == low.size() - 4 ||
                                       low.rfind(".tex") == low.size() - 4 ||
                                       low.rfind(".jpg") == low.size() - 4)) {
            s = s.substr(0, s.size() - 4);
        } else {
            break;
        }
    }
    return s;
}

static bool parse_local_aabb(const std::string& bytes, float out[4]) {
    if (bytes.empty()) return false;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    try {
        proto::Reader r(bytes);
        proto::Field f;
        while (r.read_field(f)) {
            if (f.wire_type != proto::WIRE_I32) continue;
            if (f.field_number == 1)      out[0] = f.float_val;
            else if (f.field_number == 2) out[1] = f.float_val;
            else if (f.field_number == 3) out[2] = f.float_val;
            else if (f.field_number == 4) out[3] = f.float_val;
        }
        return true;
    } catch (...) { return false; }
}

static QImage decode_image_file_to_ram(const std::string& path) {
    if (path.empty()) return {};
    {
        std::lock_guard<std::mutex> lock(s_ram_image_mutex);
        auto it = s_ram_image_cache.find(path);
        if (it != s_ram_image_cache.end()) return it->second;
    }
    const fs::path file(path);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};

    std::string low_path = file.string();
    for (auto& c : low_path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::string ext = file.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const bool is_custom_format = (low_path.size() >= 8 && low_path.substr(low_path.size() - 8) == ".tex.png") ||
                                  ext == ".pvr" || ext == ".tex";

    QImage result;
    if (is_custom_format) {
        QFile input(file.string().c_str());
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll();
            std::vector<uint8_t> rgba; int w = 0, h = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                   static_cast<size_t>(bytes.size()), rgba, w, h) && w > 0 && h > 0) {
                QImage decoded(rgba.data(), w, h, QImage::Format_RGBA8888);
                result = decoded.copy();
            }
        }
    }

    if (result.isNull()) {
        result = QImage(file.string().c_str());
    }

    // Fallback: file named something.png or other extension might actually be a gzipped .tex container
    if (result.isNull()) {
        QFile input(file.string().c_str());
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll();
            std::vector<uint8_t> rgba; int w = 0, h = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                   static_cast<size_t>(bytes.size()), rgba, w, h) && w > 0 && h > 0) {
                QImage decoded(rgba.data(), w, h, QImage::Format_RGBA8888);
                result = decoded.copy();
            }
        }
    }

    if (!result.isNull()) {
        std::lock_guard<std::mutex> lock(s_ram_image_mutex);
        s_ram_image_cache[path] = result;
    }
    return result;
}

static av::PODModel load_pod_to_ram(const std::string& path, const std::string& template_name) {
    const std::string key = path + "#" + template_name;
    {
        std::lock_guard<std::mutex> lock(s_ram_pod_mutex);
        auto it = s_ram_pod_cache.find(key);
        if (it != s_ram_pod_cache.end()) return it->second;
    }
    av::PODModel model = av::pod_load(path, template_name);
    if (!model.meshes.empty()) {
        std::lock_guard<std::mutex> lock(s_ram_pod_mutex);
        s_ram_pod_cache[key] = model;
    }
    return model;
}

// ============================================================================
// Transform-gizmo support math. Pure functions — screen conventions must match
// paintGL (vertical FOV 45°, orbit camera, device-independent width()/height()
// logical pixels like every other mouse/pick path in this widget).
// ============================================================================
static constexpr float kGizmoPi = 3.14159265358979323846f;

struct GizmoCamera {
    float pitch = 0.0f, yaw = 0.0f, dist = 100.0f;
    float target[3] = {0, 0, 0};
    int w = 1, h = 1;
};

static void gizmo_eye(const GizmoCamera& c, float e[3]) {
    const float sp = sinf(c.pitch * kGizmoPi / 180.0f);
    const float cp = cosf(c.pitch * kGizmoPi / 180.0f);
    const float sy = sinf(c.yaw * kGizmoPi / 180.0f);
    const float cy = cosf(c.yaw * kGizmoPi / 180.0f);
    e[0] = c.target[0] + c.dist * cp * sy;
    e[1] = c.target[1] + c.dist * sp;
    e[2] = c.target[2] + c.dist * cp * cy;
}

static av::Camera gizmo_av_camera(const GizmoCamera& c) {
    av::Camera cam;
    cam.yaw = c.yaw;
    cam.pitch = c.pitch;
    cam.distance = c.dist;
    cam.target[0] = c.target[0];
    cam.target[1] = c.target[1];
    cam.target[2] = c.target[2];
    cam.fov = 45.0f;
    return cam;
}

} // namespace

namespace ruby::viewport {

Viewport3DWidget::Viewport3DWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    m_loading_overlay = new SceneLoadingOverlay(this);

    // ── Scene transform toolbar (top-left overlay) ──
    // View = click-select only; Move / Rotate / Scale arm the gizmo. Tab cycles
    // the modes, Esc returns to View. Fires set_gizmo_mode so state stays in
    // sync with the keyboard path.
    m_gizmo_bar = new QWidget(this);
    m_gizmo_bar->setObjectName(QStringLiteral("SceneGizmoBar"));
    m_gizmo_bar->setStyleSheet(QStringLiteral(
        "QWidget#SceneGizmoBar { background: rgba(22,24,30,215);"
        "  border: 1px solid #2c3140; border-radius: 7px; }"
        "QToolButton { color: #c9ced8; background: transparent; border: none;"
        "  border-radius: 4px; padding: 3px 9px; font-size: 11px; }"
        "QToolButton:hover { background: #2b3040; }"
        "QToolButton:checked { background: #35608f; color: #ffffff; }"));
    auto* gizmo_layout = new QHBoxLayout(m_gizmo_bar);
    gizmo_layout->setContentsMargins(3, 3, 3, 3);
    gizmo_layout->setSpacing(1);
    const char* modes[4] = {"View", "Move", "Rotate", "Scale"};
    for (int m = 0; m < 4; ++m) {
        auto* btn = new QToolButton(m_gizmo_bar);
        btn->setText(QString::fromLatin1(modes[m]));
        btn->setCheckable(true);
        btn->setProperty("mode", m);
        btn->setToolTip(m == 0 ? QStringLiteral("Click objects to select (Tab cycles tools, Esc = View)")
                               : QString::fromLatin1("Drag the %1 gizmo handle").arg(modes[m]));
        connect(btn, &QToolButton::clicked, this,
                [this, m](bool) { set_gizmo_mode(m); });
        gizmo_layout->addWidget(btn);
    }
    // In-scene mesh edit (M): projection-locked 2D polygon editor on the
    // selected ground-mesh object. State syncs with set_mesh_edit() so the
    // button reflects keyboard toggles too.
    auto* mesh_btn = new QToolButton(m_gizmo_bar);
    mesh_btn->setText("Mesh");
    mesh_btn->setCheckable(true);
    // mode = -1 keeps set_gizmo_mode()'s force-check from claiming this button:
    // that loop checks property("mode") == gizmo mode, and a button with NO
    // mode property defaults to 0 — which made the Mesh button look permanently
    // clicked whenever the View tool was active (incl. inside begin_mesh_edit).
    mesh_btn->setProperty("mode", -1);
    mesh_btn->setToolTip(QStringLiteral(
        "In-scene mesh edit — locks the camera to the selected ground mesh's "
        "polygon plane (pure 2D front view) and edits its vertices in place (M). "
        "Esc/M commits, R reverts."));
    connect(mesh_btn, &QToolButton::clicked, this, [this](bool on) { set_mesh_edit(on); });
    m_mesh_btn = mesh_btn;
    gizmo_layout->addWidget(mesh_btn);

    auto* fx_btn = new QToolButton(m_gizmo_bar);
    fx_btn->setText(QStringLiteral("FX"));
    fx_btn->setCheckable(true);
    fx_btn->setChecked(m_render_effects_enabled);
    fx_btn->setProperty("mode", -1);
    fx_btn->setToolTip(QStringLiteral(
        "Toggle in-game dynamic effects (water simulation, torch particles, portal vortex, and torch light flicker)."));
    connect(fx_btn, &QToolButton::clicked, this, [this](bool on) { set_render_effects(on); });
    gizmo_layout->addWidget(fx_btn);
    m_fx_btn = fx_btn;

    auto* bounds_btn = new QToolButton(m_gizmo_bar);
    bounds_btn->setText(QStringLiteral("Bounds"));
    bounds_btn->setCheckable(true);
    bounds_btn->setChecked(m_show_camera_bounds);
    bounds_btn->setProperty("mode", -1);
    bounds_btn->setToolTip(QStringLiteral(
        "Toggle Camera Bounds (Scene level boundaries) display, shroud, and interactive handles."));
    connect(bounds_btn, &QToolButton::clicked, this, [this](bool on) { toggle_show_camera_bounds(on); });
    gizmo_layout->addWidget(bounds_btn);
    m_bounds_btn = bounds_btn;

    m_gizmo_bar->adjustSize();
    m_gizmo_bar->move(174, 12);
    m_gizmo_bar->setVisible(false);
}

Viewport3DWidget::~Viewport3DWidget() {
    makeCurrent();
    // Free GPU mesh buffers across active widget, scene sessions, and pod cache
    std::unordered_set<GLuint> freed_buffers;
    auto safe_free = [&](MeshGpu& g) {
        if (g.pos_vbo && !freed_buffers.count(g.pos_vbo)) {
            freed_buffers.insert(g.pos_vbo);
            free_mesh_gpu(g);
        }
    };
    for (auto& g : m_all_mesh_gpu) safe_free(g);
    m_all_mesh_gpu.clear();
    m_model_mesh_gpu.clear();
    m_scene_model_gpu.clear();

    for (auto& [path, cached_st] : m_scene_cache) {
        if (!cached_st) continue;
        for (auto& g : cached_st->all_mesh_gpu) safe_free(g);
        for (auto& ro : cached_st->render_objects) {
            for (auto& g : ro.ground_gpu) safe_free(g);
        }
    }
    m_scene_cache.clear();

    for (auto& pair : m_pod_gpu_cache)
        for (auto& g : pair.second) safe_free(g);
    m_pod_gpu_cache.clear();

    std::unordered_set<GLuint> tex_set;
    if (m_texture_tex) tex_set.insert(m_texture_tex);
    for (GLuint t : m_material_textures) if (t) tex_set.insert(t);
    for (GLuint t : m_import_textures) if (t) tex_set.insert(t);
    for (auto& pair : m_scene_model_textures)
        for (GLuint t : pair.second) if (t) tex_set.insert(t);
    for (auto& vec : m_scene_ground_textures)
        for (GLuint t : vec) if (t) tex_set.insert(t);
    for (auto& pair : m_scene_background_textures)
        if (pair.second) tex_set.insert(pair.second);
    for (auto& [path, cached_st] : m_scene_cache) {
        if (!cached_st) continue;
        for (auto& pair : cached_st->scene_model_textures)
            for (GLuint t : pair.second) if (t) tex_set.insert(t);
        for (auto& vec : cached_st->scene_ground_textures)
            for (GLuint t : vec) if (t) tex_set.insert(t);
        for (auto& pair : cached_st->scene_background_textures)
            if (pair.second) tex_set.insert(pair.second);
    }
    m_scene_cache.clear();
    for (auto& pair : m_gpu_tex_cache)
        if (pair.second) tex_set.insert(pair.second);
    m_gpu_tex_cache.clear();
    for (GLuint t : tex_set) glDeleteTextures(1, &t);
    // Selection-wireframe line index buffers (GL context is current).
    for (auto& [key, lc] : m_sel_line_cache)
        if (lc.ebo) glDeleteBuffers(1, &lc.ebo);
    m_sel_line_cache.clear();
    // RubyGizmo renderer resources (GL context is current).
    m_gizmo.destroy_gl();
    m_water_renderer.shutdown();
    m_portal_renderer.shutdown();
    m_particle_engine.shutdown();
    doneCurrent();
}

void Viewport3DWidget::clear_mesh_gpu() {
    // Free GPU buffers NOT owned by the persistent per-pod cache or any active scene session.
    // Buffers in m_pod_gpu_cache and m_scene_cache stay alive so switching tabs or re-loading
    // scenes reuses them without corrupting inactive scene sessions.
    std::unordered_set<GLuint> protected_vbos;
    for (const auto& [p, gpus] : m_pod_gpu_cache) {
        for (const auto& g : gpus) {
            if (g.pos_vbo) protected_vbos.insert(g.pos_vbo);
        }
    }
    for (const auto& [path, st] : m_scene_cache) {
        if (!st) continue;
        for (const auto& g : st->all_mesh_gpu) {
            if (g.pos_vbo) protected_vbos.insert(g.pos_vbo);
        }
        for (const auto& ro : st->render_objects) {
            for (const auto& g : ro.ground_gpu) {
                if (g.pos_vbo) protected_vbos.insert(g.pos_vbo);
            }
        }
    }
    for (auto& g : m_all_mesh_gpu) {
        if (g.pos_vbo && !protected_vbos.count(g.pos_vbo)) free_mesh_gpu(g);
    }
    m_all_mesh_gpu.clear();
    m_model_mesh_gpu.clear();
    m_scene_model_gpu.clear();
}

void Viewport3DWidget::evict_scene_cache(const std::string& scene_path) {
    if (scene_path.empty()) return;

    // A save / close invalidates every in-flight load of this scene: bump the
    // global seq AND the per-scene token so a stale thread can never commit.
    ++m_scene_load_seq;
    if (m_scene_load_token == scene_path) m_scene_load_token.clear();

    // Drop the widget-local parse record: after any save / re-encode the next
    // load must re-parse the fresh bytes. (Sessions holding unsaved editor
    // state are intentionally NOT destroyed here — evict() is only about
    // invalidating parsed disk state.)
    {
        std::lock_guard<std::mutex> lock(m_parse_cache_mutex);
        m_parse_cache.erase(scene_path);
    }

    m_scene_undo_stacks.erase(scene_path);

    auto it = m_scene_cache.find(scene_path);
    if (it != m_scene_cache.end()) {
        auto st = it->second;
        m_scene_cache.erase(it);
        // Free ground-mesh buffers owned solely by this scene cache entry.
        // Protected if present in m_pod_gpu_cache or any other active scene session.
        makeCurrent();
        if (st) {
            std::unordered_set<GLuint> protected_vbos;
            for (const auto& [p, gpus] : m_pod_gpu_cache)
                for (const auto& g : gpus) if (g.pos_vbo) protected_vbos.insert(g.pos_vbo);
            for (const auto& [rem_path, rem_st] : m_scene_cache) {
                if (!rem_st) continue;
                for (const auto& g : rem_st->all_mesh_gpu)
                    if (g.pos_vbo) protected_vbos.insert(g.pos_vbo);
                for (const auto& ro : rem_st->render_objects)
                    for (const auto& g : ro.ground_gpu)
                        if (g.pos_vbo) protected_vbos.insert(g.pos_vbo);
            }
            for (auto& g : st->all_mesh_gpu)
                if (g.pos_vbo && !protected_vbos.count(g.pos_vbo)) free_mesh_gpu(g);
        }
        doneCurrent();
    }

    if (m_current_scene_path == scene_path) {
        m_has_scene = false;
        m_scene_ready = false;
        m_current_scene_path.clear();
        m_scene = av::SceneData{};
        m_render_objects.clear();
        m_all_mesh_gpu.clear();
        m_model_mesh_gpu.clear();
        m_scene_model_gpu.clear();
    }
    update();
}

// ── Scene-session isolation API ────────────────────────────────────────────

bool Viewport3DWidget::read_file_guarded(const std::string& path,
                                         std::vector<uint8_t>& out, std::string& error) {
    return guarded_read_file(path, out, error);
}

bool Viewport3DWidget::validate_scene_bytes(const std::vector<uint8_t>& bytes,
                                            const std::string& identity, std::string& error) {
    error.clear();
    if (bytes.empty()) { error = "file is empty: " + identity; return false; }
    const size_t head = std::min<size_t>(bytes.size(), 64);
    const std::string banner(reinterpret_cast<const char*>(bytes.data()), head);
    if (banner.rfind("## FileRift decoded", 0) == 0) {
        error = "file is FileRift text, not a binary .scene: " + identity;
        return false;
    }
    std::vector<std::string> extra_roots;
    const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
    if (!pdir.empty()) extra_roots.push_back(pdir);

    std::string parse_err;
    av::SceneData scene = parse_scene_bytes(bytes, identity, extra_roots, parse_err);
    if (scene.objects.empty()) {
        error = "scene does not parse (" + (parse_err.empty() ? std::string("no objects") : parse_err) + "): " + identity;
        return false;
    }
    // Round trip: serialize the parsed scene, decode it back as FileRift
    // markup, and re-encode. Any failure means the bytes are not a sound,
    // fully-decodable scene — never hand them to the engine.
    try {
        const std::string bin = av::scene_serialize(scene);
        const std::string markup = ::filerift::decode_protobuf(bin, "scene");
        if (markup.empty()) { error = "round-trip decode produced no markup: " + identity; return false; }
        const std::string rebin = ::filerift::recode_markup(markup, "scene");
        if (rebin.empty()) { error = "round-trip re-encode produced no binary: " + identity; return false; }
    } catch (const std::exception& e) {
        error = std::string("round-trip validation failed: ") + e.what() + " — " + identity;
        return false;
    } catch (...) {
        error = "round-trip validation failed (unknown error) — " + identity;
        return false;
    }
    return true;
}

bool Viewport3DWidget::try_get_scene(const std::string& path, av::SceneData& out) const {
    if (!path.empty() && m_has_scene && m_scene_ready && m_current_scene_path == path) {
        out = m_scene;   // live viewport state wins (it may hold unsaved edits)
        return true;
    }
    auto it = m_scene_cache.find(path);
    if (it != m_scene_cache.end() && it->second) {
        out = it->second->scene;
        return !out.objects.empty();
    }
    return false;
}

void Viewport3DWidget::mark_scene_saved(const std::string& path) {
    if (path.empty()) return;
    // The structured save wrote av::scene_serialize(RAM scene) byte-verbatim
    // to `path` (av::scene_save = temp file + rename + verified by
    // scene_smart_save_test). Nothing to re-parse, nothing to rebuild: the
    // viewport keeps camera, selection, GPU state and edits exactly as they
    // are. Only the parse record is refreshed so a later explicit reload gets
    // the new bytes if the user ever asks for one.
    {
        std::lock_guard<std::mutex> lock(m_parse_cache_mutex);
        m_parse_cache.erase(path);
    }
    if (m_current_scene_path == path && m_has_scene) {
        auto it = m_scene_cache.find(path);
        if (it != m_scene_cache.end() && it->second)
            it->second->scene = m_scene;   // session = saved truth
    }
}

av::SceneData Viewport3DWidget::parse_scene_from_disk(const std::string& scene_path) {
    // Widget-local, size+mtime validated parse cache — replaces the shared
    // process-global scene cache. One scene's bytes can never be served for
    // another's.
    uintmax_t fsize = 0;
    int64_t mtime_ns = 0;
    {
        std::error_code ec;
        const fs::path file(scene_path);
        if (fs::is_regular_file(file, ec)) {
            fsize = fs::file_size(file, ec);
            if (!ec) {
                const auto t = fs::last_write_time(file, ec);
                if (!ec)
                    mtime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   t.time_since_epoch()).count();
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_parse_cache_mutex);
        auto it = m_parse_cache.find(scene_path);
        if (it != m_parse_cache.end() && it->second.file_size == fsize &&
            it->second.mtime_ns == mtime_ns && !it->second.scene.objects.empty()) {
            return it->second.scene;
        }
    }

    std::vector<std::string> extra_roots;
    const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
    if (!pdir.empty()) extra_roots.push_back(pdir);

    std::vector<uint8_t> buf;
    std::string read_err;
    if (!guarded_read_file(scene_path, buf, read_err)) {
        std::cerr << "[viewport] " << read_err << "\n";
        return av::SceneData{};
    }
    std::string parse_err;
    av::SceneData scene = parse_scene_bytes(buf, scene_path, extra_roots, parse_err);
    if (scene.objects.empty()) {
        std::cerr << "[viewport] " << scene_path << ": " << parse_err << "\n";
        return av::SceneData{};
    }
    std::lock_guard<std::mutex> lock(m_parse_cache_mutex);
    ParseEntry e;
    e.scene = scene;
    e.file_size = fsize;
    e.mtime_ns = mtime_ns;
    m_parse_cache[scene_path] = std::move(e);
    return scene;
}

Viewport3DWidget::MeshGpu Viewport3DWidget::upload_mesh_gpu(const av::PODMesh& mesh) {
    MeshGpu g;
    // Never touch the GL driver without a live, initialized surface (guards a
    // load racing the first paint — QTabBar::currentChanged path).
    if (!context() || !m_gl_initialized) return g;
    if (mesh.positions.empty()) return g;
    g.vertex_count = static_cast<int>(mesh.positions.size() / 3);
    glGenBuffers(1, &g.pos_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g.pos_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.positions.size() * sizeof(float)),
                 mesh.positions.data(), GL_STATIC_DRAW);
    if (!mesh.normals.empty()) {
        glGenBuffers(1, &g.nrm_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.nrm_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.normals.size() * sizeof(float)),
                     mesh.normals.data(), GL_STATIC_DRAW);
    }
    if (!mesh.uvs.empty()) {
        glGenBuffers(1, &g.uv_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g.uv_vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.uvs.size() * sizeof(float)),
                     mesh.uvs.data(), GL_STATIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!mesh.indices.empty()) {
        g.index_count = static_cast<int>(mesh.indices.size());
        glGenBuffers(1, &g.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                     mesh.indices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    return g;
}

std::vector<Viewport3DWidget::MeshGpu>
Viewport3DWidget::upload_pod_gpu(const std::string& pod_path, const av::PODModel& model) {
    std::vector<MeshGpu> out;
    out.reserve(model.meshes.size());
    // Per-pod GPU cache: a pod's geometry is immutable, so re-loading a scene
    // (or re-opening the same model) reuses the uploaded buffers instead of
    // recompiling every mesh on the main thread — the dominant load cost.
    if (!pod_path.empty()) {
        auto it = m_pod_gpu_cache.find(pod_path);
        if (it != m_pod_gpu_cache.end() && it->second.size() == model.meshes.size()) {
            return it->second;
        }
    }
    for (const auto& mesh : model.meshes) {
        if (mesh.bones_per_vertex != 0) {
            // Bake the bind-pose skin for editor display (deterministic per
            // pod — safe to cache); the animated path re-skins per frame and
            // draws from client arrays in draw_pod_instance / draw_model.
            MeshGpu g;
            int node_idx = -1;
            for (int ni = 0; ni < static_cast<int>(model.nodes.size()); ++ni) {
                if (model.nodes[ni].object_index == static_cast<int>(&mesh - &model.meshes[0])) {
                    node_idx = ni;
                    break;
                }
            }
            std::vector<float> skin_pos, skin_norm;
            if (node_idx >= 0 && av::skin_mesh(model, node_idx, 0.0f, skin_pos, skin_norm)) {
                av::PODMesh baked = mesh;
                baked.positions = std::move(skin_pos);
                baked.normals = std::move(skin_norm);
                g = upload_mesh_gpu(baked);
            }
            out.push_back(g);
        } else {
            out.push_back(upload_mesh_gpu(mesh));
        }
    }
    if (!pod_path.empty()) {
        m_pod_gpu_cache.emplace(pod_path, out);
    }
    return out;
}

void Viewport3DWidget::draw_mesh_gpu(const MeshGpu& g) {
    if (!g.valid()) return;

    const bool shader_on = m_viewport_shader.ready();

    // Setup position
    glBindBuffer(GL_ARRAY_BUFFER, g.pos_vbo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, nullptr);
    if (shader_on) {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    }

    // Setup normal
    if (g.nrm_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g.nrm_vbo);
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, nullptr);
        if (shader_on) {
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        }
    } else if (shader_on) {
        glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
    }

    // Setup UV
    if (g.uv_vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, g.uv_vbo);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, nullptr);
        if (shader_on) {
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        }
    } else if (shader_on) {
        glVertexAttrib2f(2, 0.0f, 0.0f);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (g.ebo && g.index_count > 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(g.index_count), GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g.vertex_count));
    }

    // Teardown
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (shader_on) {
        glDisableVertexAttribArray(0);
        if (g.nrm_vbo) glDisableVertexAttribArray(1);
        if (g.uv_vbo)  glDisableVertexAttribArray(2);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Viewport3DWidget::free_mesh_gpu(MeshGpu& g) {
    if (g.pos_vbo) glDeleteBuffers(1, &g.pos_vbo);
    if (g.nrm_vbo) glDeleteBuffers(1, &g.nrm_vbo);
    if (g.uv_vbo)  glDeleteBuffers(1, &g.uv_vbo);
    if (g.ebo)     glDeleteBuffers(1, &g.ebo);
    g = MeshGpu();
}

// ─── GPU self-diagnostic (parity doc §8) ────────────────────────────────────
// Exercises the VBO path with a synthetic mesh: upload → draw (no GL error) →
// free, plus the per-pod cache reuse that makes reloads skip recompilation.
bool Viewport3DWidget::gpu_self_test() {
    if (!context()) return false;
    makeCurrent();
    int fail = 0;
    auto ok = [&fail](bool cond, const char* msg) {
        std::printf("  %s %s\n", cond ? "PASS" : "FAIL", msg);
        if (!cond) ++fail;
    };

    av::PODModel model;
    av::PODMesh tri;
    tri.positions = {0,0,0, 1,0,0, 0,1,0};
    tri.normals   = {0,0,1, 0,0,1, 0,0,1};
    tri.uvs       = {0,0, 1,0, 0,1};
    tri.indices   = {0,1,2};
    model.meshes.push_back(tri);

    MeshGpu g = upload_mesh_gpu(tri);
    ok(g.valid() && g.pos_vbo != 0, "upload_mesh_gpu creates a valid VBO");
    ok(g.index_count == 3, "upload_mesh_gpu uploads the index buffer");
    if (g.valid()) {
        glBindBuffer(GL_ARRAY_BUFFER, g.pos_vbo);
        const GLuint live = glIsBuffer(g.pos_vbo);
        ok(live == GL_TRUE, "created VBO is a live GL buffer");
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        draw_mesh_gpu(g);
        ok(glGetError() == GL_NO_ERROR, "draw_mesh_gpu leaves no GL error");
        free_mesh_gpu(g);
        ok(!g.valid(), "free_mesh_gpu releases the buffers");
    }

    auto first  = upload_pod_gpu("/probe/pod_a.pod", model);
    auto second = upload_pod_gpu("/probe/pod_a.pod", model);
    auto other  = upload_pod_gpu("/probe/pod_b.pod", model);
    ok(!first.empty() && first[0].valid(), "upload_pod_gpu uploads node meshes");
    if (!first.empty() && !second.empty())
        ok(second[0].pos_vbo == first[0].pos_vbo, "per-pod cache reuses VBOs on reload");
    if (!first.empty() && !other.empty())
        ok(other[0].pos_vbo != first[0].pos_vbo, "different pod gets its own buffers");

    // Teardown: free the cache-owned buffers exactly as the destructor does.
    for (auto& pair : m_pod_gpu_cache)
        for (auto& gg : pair.second) free_mesh_gpu(gg);
    m_pod_gpu_cache.clear();

    doneCurrent();
    std::printf("  %s\n", fail == 0 ? "gpu_self_test: ALL PASS" : "gpu_self_test: FAILED");
    return fail == 0;
}

bool Viewport3DWidget::ensure_gl_ready() {
    if (m_gl_initialized) return true;
    // QOpenGLWidget creates its context lazily — makeCurrent() before the
    // first paint is legal (offscreen use). Initialize the QOpenGLFunctions
    // table HERE instead of waiting for initializeGL(), so early document
    // opens (sync insertTab → activate_document) cannot call GL through
    // uninitialized function pointers.
    makeCurrent();
    if (!context()) return false;
    initializeOpenGLFunctions();
    m_gl_initialized = true;
    return true;
}

void Viewport3DWidget::initializeGL() {
    initializeOpenGLFunctions();
    m_gl_initialized = true;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);

    glClearColor(0.07f, 0.075f, 0.086f, 1.0f); // #121316 dark studio background

    // ── Modern GLSL shader pipeline (Phase 1: shader bridge) ────────────────
    if (!m_viewport_shader.init()) {
        fprintf(stderr, "[Viewport3D] WARNING: Shader init failed — falling back to fixed-function.\n");
    }

    // ── Post-processing: FBO + SSAO + Composite ──────────────────────────────
    {
        const int w = std::max(1, static_cast<int>(width() * devicePixelRatio()));
        const int h = std::max(1, static_cast<int>(height() * devicePixelRatio()));
        const bool fbo_ok  = m_fbo_chain.init(w, h);
        const bool ssao_ok = m_ssao_pass.init();
        const bool post_ok = m_post_pass.init();
        m_post_ready = fbo_ok && ssao_ok && post_ok;
        if (!m_post_ready)
            fprintf(stderr, "[Viewport3D] WARNING: Post-processing init failed — rendering without SSAO.\n");
    }

    // ── RubyGizmo renderer (bespoke HiDPI-correct gizmo, no external deps) ──
    m_gizmo.init_gl();

    // ── In-game dynamic effects (water, portals, particles) ──────────────────
    m_water_renderer.init();
    m_portal_renderer.init();
    m_particle_engine.init();
    m_effects_clock.start();
}

void Viewport3DWidget::resizeGL(int w, int h) {
    const float dpr = static_cast<float>(devicePixelRatio());
    const int pw = std::max(1, static_cast<int>(std::round(w * dpr)));
    const int ph = std::max(1, static_cast<int>(std::round(h * dpr)));
    m_fb_width  = pw;
    m_fb_height = ph;
    glViewport(0, 0, pw, ph);
    if (m_post_ready) m_fbo_chain.resize(pw, ph);
}


void Viewport3DWidget::resizeEvent(QResizeEvent* event) {
    QOpenGLWidget::resizeEvent(event);   // let the GL context track the new size
    if (m_loading_overlay) {
        m_loading_overlay->setGeometry(rect());
    }
    if (m_gizmo_bar) {
        m_gizmo_bar->adjustSize();
        m_gizmo_bar->move(174, 12);
    }
    update();                            // repaint at the new size immediately
}

void Viewport3DWidget::paintGL() {
    const auto render_start = std::chrono::steady_clock::now();
    double frame_interval_ms = 0.0;
    if (m_last_frame_time.time_since_epoch().count() > 0) {
        frame_interval_ms = std::chrono::duration<double, std::milli>(render_start - m_last_frame_time).count();
    }
    m_last_frame_time = render_start;

    // Camera inertia: keep gliding after the mouse stops (asset_viewer feel).
    if ((std::abs(m_orbit_vel_yaw) > 0.01f || std::abs(m_orbit_vel_pitch) > 0.01f ||
        std::abs(m_pan_vel[0]) > 1e-3f || std::abs(m_pan_vel[1]) > 1e-3f ||
        std::abs(m_pan_vel[2]) > 1e-3f)) {
        float dt = 1.0f / 60.0f;
        if (m_move_timer.isValid()) {
            const qint64 n = m_move_timer.restart();
            if (n > 0) dt = std::min(0.1f, float(n) / 1000.0f);
        }
        update_camera_dynamics(dt);
        update();   // keep repainting while inertia is active
    }

    // ── In-game effects dynamic clock ─────────────────────────────────────────
    float effects_dt = 0.016f;
    if (m_effects_clock.isValid()) {
        const qint64 elapsed_ms = m_effects_clock.restart();
        if (elapsed_ms > 0) effects_dt = std::min(0.1f, float(elapsed_ms) / 1000.0f);
    } else {
        m_effects_clock.start();
    }
    if (m_render_effects_enabled) {
        m_effects_time += effects_dt;
    }

    // Determine physical framebuffer pixel dimensions (HiDPI / DPR-aware)
    const float dpr = static_cast<float>(devicePixelRatio());
    const int fb_w = std::max(1, static_cast<int>(std::round(width() * dpr)));
    const int fb_h = std::max(1, static_cast<int>(std::round(height() * dpr)));
    m_fb_width  = fb_w;
    m_fb_height = fb_h;

    // ── Post-processing: redirect scene rendering to FBO ──────────────────────
    // If SSAO is available, redirect all geometry to the scene FBO.
    // On failure (m_post_ready=false), geometry renders directly to screen.
    if (m_post_ready) {
        if (m_fbo_chain.m_width != fb_w || m_fbo_chain.m_height != fb_h) {
            m_fbo_chain.resize(fb_w, fb_h);
        }
        m_fbo_chain.bind_scene();   // binds + clears scene FBO
        glViewport(0, 0, fb_w, fb_h);
    } else {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, fb_w, fb_h);
    }

    // Setup viewport matrix calculations
    const float aspect = fb_w / static_cast<float>(fb_h);
    const float fov = 45.0f * (3.14159265f / 180.0f);
    // Ruby scenes are measured in world units, not model units: distant
    // backgrounds/terrain routinely exceed the old 2,000-unit clip plane.
    const float near_z = std::max(0.1f, m_cam_dist * 0.001f);
    const float far_z = std::max({10000.0f, m_cam_dist * 24.0f, m_scene_extent * 4.0f});


    // Projection matrix (Perspective)
    float f = 1.0f / std::tan(fov * 0.5f);
    float proj[16] = {
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far_z + near_z) / (near_z - far_z), -1,
        0, 0, (2.0f * far_z * near_z) / (near_z - far_z), 0
    };

    // Inverse projection — needed by SSAO to reconstruct view-space positions from depth.
    // Derivation (column-major perspective matrix P, P[col*4+row]):
    //   P[0]=f/a, P[5]=f, P[10]=A=(far+near)/(near-far), P[11]=-1, P[14]=B=2fn/(near-far)
    // P_inv (column-major):
    //   P_inv[0]  = a/f   = 1/P[0]
    //   P_inv[5]  = 1/f   = 1/P[5]
    //   P_inv[11] = 1/B   = 1/P[14]   ← was wrong before
    //   P_inv[14] = -1               ← was wrong before (had 1/A)
    //   P_inv[15] = A/B   = P[10]/P[14] ← was wrong before (had -1/B)
    float inv_proj[16] = {};
    inv_proj[0]  = 1.0f / proj[0];             // a/f
    inv_proj[5]  = 1.0f / proj[5];             // 1/f
    inv_proj[11] = 1.0f / proj[14];            // 1/B
    inv_proj[14] = -1.0f;                      // -1
    inv_proj[15] = proj[10] / proj[14];        // A/B


    // Calculate camera eye position
    const float rad_pitch = m_cam_pitch * (3.14159265f / 180.0f);
    const float rad_yaw   = m_cam_yaw * (3.14159265f / 180.0f);

    float eye_x = m_cam_target[0] + m_cam_dist * std::cos(rad_pitch) * std::sin(rad_yaw);
    float eye_y = m_cam_target[1] + m_cam_dist * std::sin(rad_pitch);
    float eye_z = m_cam_target[2] + m_cam_dist * std::cos(rad_pitch) * std::cos(rad_yaw);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    const float forward_x = m_cam_target[0] - eye_x;
    const float forward_y = m_cam_target[1] - eye_y;
    const float forward_z = m_cam_target[2] - eye_z;
    const float forward_len = std::sqrt(forward_x * forward_x + forward_y * forward_y + forward_z * forward_z);
    const float fx = forward_x / forward_len, fy = forward_y / forward_len, fz = forward_z / forward_len;
    float sx = -fz, sy = 0.0f, sz = fx;
    const float s_len = std::sqrt(sx * sx + sz * sz);
    if (s_len > 1e-6f) { sx /= s_len; sz /= s_len; }
    const float upx = sy * fz - sz * fy, upy = sz * fx - sx * fz, upz = sx * fy - sy * fx;
    const float view[16] = {
        sx, upx, -fx, 0, sy, upy, -fy, 0, sz, upz, -fz, 0,
        -(sx * eye_x + sy * eye_y + sz * eye_z),
        -(upx * eye_x + upy * eye_y + upz * eye_z),
        fx * eye_x + fy * eye_y + fz * eye_z, 1
    };
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view);
    // Capture camera matrices + eye for the RubyGizmo pass this frame.
    std::memcpy(m_gizmo_proj, proj, sizeof(proj));
    std::memcpy(m_gizmo_view, view, sizeof(view));
    m_gizmo_eye[0] = eye_x;
    m_gizmo_eye[1] = eye_y;
    m_gizmo_eye[2] = eye_z;

    // Compute VP matrix for shader pipeline
    float vp[16] = {};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                vp[col * 4 + row] += proj[k * 4 + row] * view[col * 4 + k];
            }
        }
    }
    std::memcpy(m_cached_viewproj, vp, sizeof(vp));
    m_cached_eye[0] = eye_x;
    m_cached_eye[1] = eye_y;
    m_cached_eye[2] = eye_z;

    // Upload all per-frame shader uniforms while program is bound.
    // Qt requires bind() before any setUniformValue; uploading while released silently no-ops.
    if (m_viewport_shader.ready()) {
        m_viewport_shader.bind();
        m_viewport_shader.setProj(proj);
        apply_lighting();   // uploads dir lights, ambient, world-up (shader-path)
        apply_fog();        // uploads fog params (shader-path)
        m_viewport_shader.release();
    } else {
        apply_lighting();   // fixed-function GL path only
        apply_fog();
    }

    if (m_has_scene) draw_scene_background();
    if (m_show_grid) draw_grid();
    if (m_has_texture) {
        draw_texture_poster();
    } else {
        if (m_viewport_shader.ready()) m_viewport_shader.bind();
        if (m_has_scene) draw_scene();
        if (m_has_model) draw_model();
        else if (m_has_glb) {
            upload_current_model_matrix();
            m_glb_model.draw(m_wireframe, m_show_textures, m_viewport_shader.ready() ? &m_viewport_shader : nullptr);
        }
        if (m_viewport_shader.ready()) m_viewport_shader.release();
        if (m_has_model && m_show_skeleton) draw_skeleton();
    }

    // ── In-game dynamic effects transparent pass ──────────────────────────────
    // Rendered forward with proper depth testing against opaque scene geometry.
    // Order: 1. Water fluid sheets  2. Portal vortices  3. Particle systems
    if (m_has_scene && m_scene_ready && m_render_effects_enabled) {
        ensure_effects_resources();
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        // 1. Water & fluid sheets
        m_water_renderer.render(m_scene.waters, m_scene.objects, m_gpu_tex_cache, m_cached_viewproj, m_effects_time);
        // 2. Animated portal vortex swirls
        m_portal_renderer.render(m_scene.objects, view, m_cached_viewproj, m_effects_time);
        // 3. Dynamic particles (torches, flames, emitters)
        m_particle_engine.update_and_render(effects_dt, view, m_cached_viewproj, m_particle_tex);
    }

    // ── Selection highlight & transform gizmo ──────────────────────────────────
    // Rendered directly into scene FBO with full native depth testing against scene_depth_tex.
    // Both use glDepthMask(GL_FALSE) so scene depth is untouched for SSAO.
    // Pass 1 (GL_GREATER) renders faint greyish occluded silhouettes through obstacles.
    // Pass 2 (GL_LEQUAL) renders vibrant colored outlines/handles in direct line of sight.
    if (m_has_scene && m_scene_ready && !m_mesh_edit &&
        m_selected_scene_object >= 0 &&
        m_selected_scene_object < (int)m_scene.objects.size()) {
        draw_selection_highlight();
    }
    if (m_has_scene && m_scene_ready && m_gizmo_mode != GizmoOff && !m_mesh_edit) {
        draw_ruby_gizmo();
    }

    // ── Post-processing: SSAO → composite to screen ───────────────────────────
    // Geometry + transparents + overlays have been rendered into scene FBO.
    //   1. Run SSAO pass (scene_depth → ao_raw → ao_blur)
    //   2. Composite to Qt's framebuffer (scene_color × ao)
    if (m_post_ready) {
        // SSAO — reads scene depth, writes blurred AO
        m_ssao_pass.render(m_fbo_chain, fb_w, fb_h, proj, inv_proj);

        // Composite — bind Qt's default FBO and blit scene×AO
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, fb_w, fb_h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_post_pass.render(m_fbo_chain, 0.45f);
    }

    // ── 3D ViewCube (top-right camera controller) ──
    if (m_view_cube_anim_frames > 0) {
        --m_view_cube_anim_frames;
        m_cam_pitch += (m_view_cube_target_pitch - m_cam_pitch) * 0.25f;
        // Wrap yaw diff so it rotates the shortest path
        float dyaw = m_view_cube_target_yaw - m_cam_yaw;
        while (dyaw > 180.0f) dyaw -= 360.0f;
        while (dyaw < -180.0f) dyaw += 360.0f;
        m_cam_yaw += dyaw * 0.25f;
        update();
    }
    draw_view_cube();

    // In-scene mesh editor overlay: QPainter composited over the GL pass
    // (2D grid on the object plane, polygon outline, vertex handles, hints).
    if (m_mesh_edit) draw_mesh_edit_overlay();

    // ── Camera Bounds Overlay (Level boundaries letterbox & handles) ──
    if (m_has_scene && m_show_camera_bounds && !m_mesh_edit) {
        av::CameraBounds cb;
        if (av::scene_get_camera_bounds(m_scene, cb)) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            m_bounds_gizmo.draw(p, cb, width(), height(), m_gizmo_view, m_gizmo_proj,
                                m_bounds_hover_handle, m_bounds_active_handle, m_camera_bounds_selected);
            p.end();
        }
    }

    // ── Drag-and-drop Smart Placement Reticle Overlay ──
    if (m_drag_hover_active) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        float sx = 0.0f, sy = 0.0f;
        if (CameraBoundsGizmo::world_to_screen(m_drag_hover_world[0], m_drag_hover_world[1], m_drag_hover_world[2],
                                               width(), height(), m_gizmo_view, m_gizmo_proj, sx, sy)) {
            p.setPen(QPen(QColor(80, 210, 255, 230), 2.0));
            p.setBrush(QColor(40, 160, 255, 60));
            p.drawEllipse(QPointF(sx, sy), 18.0, 18.0);
            p.drawLine(QPointF(sx - 24, sy), QPointF(sx + 24, sy));
            p.drawLine(QPointF(sx, sy - 24), QPointF(sx, sy + 24));

            QString badge = m_drag_hover_label.isEmpty() ? QStringLiteral("Drop Object") : m_drag_hover_label;
            badge += QStringLiteral("\n(%1, %2)").arg(m_drag_hover_world[0], 0, 'f', 1).arg(m_drag_hover_world[1], 0, 'f', 1);
            QFont f = p.font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            QRectF badge_rect(sx + 24, sy - 20, 140, 36);
            p.setPen(QColor(30, 45, 65, 220));
            p.setBrush(QColor(15, 22, 35, 220));
            p.drawRoundedRect(badge_rect, 5.0, 5.0);
            p.setPen(QColor(220, 240, 255, 255));
            p.drawText(badge_rect, Qt::AlignCenter, badge);
        }
        p.end();
    }

    if (m_gizmo_bar) m_gizmo_bar->setVisible(m_has_scene);

    if (m_render_effects_enabled && m_has_scene && m_scene_ready) {
        update(); // Continuous 60fps fluid & particle animation loop
    }

    const auto render_end = std::chrono::steady_clock::now();
    const double render_time_ms = std::chrono::duration<double, std::milli>(render_end - render_start).count();

    // Frame stutter perf logging is disabled
    (void)render_time_ms;
    (void)frame_interval_ms;
}

void Viewport3DWidget::set_lighting(const ViewportLighting& lighting) {
    m_lighting = lighting;
    update();
}

void Viewport3DWidget::set_render_effects(bool on) {
    m_render_effects_enabled = on;
    if (m_fx_btn) m_fx_btn->setChecked(on);
    if (!on) {
        m_particle_engine.clear();
    } else {
        if (!m_effects_clock.isValid()) m_effects_clock.start();
        else m_effects_clock.restart();
    }
    update();
}

void Viewport3DWidget::ensure_effects_resources() {
    if (!m_has_scene) return;

    // 1. Sync particle engine with active scene objects
    m_particle_engine.sync_scene(m_scene);

    // 2. Load water textures if referenced
    for (const auto& w : m_scene.waters) {
        if (!w.texture.empty() && m_gpu_tex_cache.find(w.texture) == m_gpu_tex_cache.end()) {
            for (const auto& root : current_scene_roots()) {
                const fs::path pvr = root / (w.texture + ".pvr");
                const fs::path tex_png = root / (w.texture + ".tex.png");
                const fs::path png = root / (w.texture + ".png");
                std::error_code ec;
                if (fs::exists(pvr, ec)) {
                    GLuint t = load_texture_any(pvr.string());
                    if (t) { m_gpu_tex_cache[w.texture] = t; break; }
                } else if (fs::exists(tex_png, ec)) {
                    GLuint t = load_texture_any(tex_png.string());
                    if (t) { m_gpu_tex_cache[w.texture] = t; break; }
                } else if (fs::exists(png, ec)) {
                    GLuint t = load_texture_any(png.string());
                    if (t) { m_gpu_tex_cache[w.texture] = t; break; }
                }
            }
        }
    }

    // 3. Cache particle texture if present
    if (!m_particle_tex) {
        for (const auto& root : current_scene_roots()) {
            for (const char* name : {"particle", "flare", "smoke", "fire"}) {
                const fs::path pvr = root / (std::string(name) + ".pvr");
                const fs::path png = root / (std::string(name) + ".png");
                std::error_code ec;
                if (fs::exists(pvr, ec)) {
                    m_particle_tex = load_texture_any(pvr.string());
                    if (m_particle_tex) break;
                } else if (fs::exists(png, ec)) {
                    m_particle_tex = load_texture_any(png.string());
                    if (m_particle_tex) break;
                }
            }
            if (m_particle_tex) break;
        }
    }
}

// ── Per-document editor-state stash/adopt ──────────────────────────────────
// Every scene document owns its camera, selection and tool mode. Stashing on
// scene-out / adopting on scene-in makes two open scenes fully independent.
void Viewport3DWidget::stash_current_session() {
    if (!m_has_scene || !m_scene_ready || m_current_scene_path.empty()) return;
    auto it = m_scene_cache.find(m_current_scene_path);
    std::shared_ptr<SceneSession> st;
    if (it != m_scene_cache.end() && it->second) {
        st = it->second;
    } else {
        st = std::make_shared<SceneSession>();
        m_scene_cache[m_current_scene_path] = st;
    }
    st->scene = m_scene;
    st->scene_models = m_scene_models;
    st->scene_model_textures = m_scene_model_textures;
    st->scene_ground_textures = m_scene_ground_textures;
    st->scene_background_textures = m_scene_background_textures;
    st->render_objects = m_render_objects;
    st->model_mesh_gpu = m_model_mesh_gpu;
    st->scene_model_gpu = m_scene_model_gpu;
    st->all_mesh_gpu = m_all_mesh_gpu;
    st->scene_extent = m_scene_extent;
    st->has_editor_state = true;
    st->cam_pitch = m_cam_pitch;
    st->cam_yaw = m_cam_yaw;
    st->cam_dist = m_cam_dist;
    st->cam_target[0] = m_cam_target[0];
    st->cam_target[1] = m_cam_target[1];
    st->cam_target[2] = m_cam_target[2];
    st->selected_object = m_selected_scene_object;
    st->gizmo_mode = m_gizmo_mode;
    // Kill any in-flight drag/motion so it can never leak into the next scene.
    m_orbit_vel_yaw = m_orbit_vel_pitch = 0.0f;
    m_pan_vel[0] = m_pan_vel[1] = m_pan_vel[2] = 0.0f;
    m_orbiting = m_panning = false;
    m_gizmo_lmb = m_gizmo_grab = false;
}

void Viewport3DWidget::adopt_session_state(const std::string& scene_path) {
    auto it = m_scene_cache.find(scene_path);
    if (it == m_scene_cache.end() || !it->second || !it->second->has_editor_state) {
        m_selected_scene_object = -1;
        m_gizmo_mode = GizmoOff;
        if (m_gizmo_bar) {
            const auto btns = m_gizmo_bar->findChildren<QToolButton*>();
            for (QToolButton* b : btns) {
                const int m = b->property("mode").toInt();
                if (m >= 0) b->setChecked(m == m_gizmo_mode);
            }
        }
        return;
    }
    const SceneSession& st = *it->second;
    // An explicitly requested per-document camera (set_camera_state during this
    // load) wins over the stashed session camera — never overwrite it.
    if (!m_has_pending_cam) {
        m_cam_pitch = st.cam_pitch;
        m_cam_yaw = st.cam_yaw;
        m_cam_dist = st.cam_dist;
        m_cam_target[0] = st.cam_target[0];
        m_cam_target[1] = st.cam_target[1];
        m_cam_target[2] = st.cam_target[2];
    }
    m_selected_scene_object = std::clamp(st.selected_object, -1,
                                         static_cast<int>(m_scene.objects.size()) - 1);
    m_gizmo_mode = std::clamp(st.gizmo_mode, 0, 3);
    if (m_gizmo_bar) {
        const auto btns = m_gizmo_bar->findChildren<QToolButton*>();
        for (QToolButton* b : btns) {
            const int m = b->property("mode").toInt();
            if (m >= 0) b->setChecked(m == m_gizmo_mode);
        }
    }
}

// Configure the three-light rig (sun key, opposite fill, cool bounce) plus
// scene ambient and shared specular.
//
// Fixed-function path: drives glLightfv / glMaterialfv for gizmo overlays,
//   wireframes, and any draw-call that doesn't use the modern shader.
// Modern shader path: delegates to LightRig::build_from_viewport_lighting()
//   + LightRig::upload_to_shader() — all uniform transform math is there.
void Viewport3DWidget::apply_lighting() {
    const float pi = 3.14159265358979323846f;
    const float ky = m_lighting.key_yaw * pi / 180.0f;
    const float kp = m_lighting.key_pitch * pi / 180.0f;

    const float amb = std::clamp(m_lighting.ambient, 0.0f, 2.0f);
    const GLfloat model_ambient[4] = {amb * 0.85f, amb * 0.87f, amb * 0.95f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, model_ambient);

    // Sun (key): warm directional light from the user's yaw/pitch.
    const float kp_cos = std::cos(kp);
    const float key_dir[4] = {kp_cos * std::cos(ky), std::sin(kp), kp_cos * std::sin(ky), 0.0f};
    const float ki = std::clamp(m_lighting.key_intensity, 0.0f, 4.0f);
    const GLfloat key_diffuse[4]  = {ki * 1.0f,  ki * 0.95f, ki * 0.88f, 1.0f};
    const GLfloat key_specular[4] = {ki * 0.35f, ki * 0.33f, ki * 0.30f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, key_dir);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  key_diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, key_specular);
    glEnable(GL_LIGHT0);

    // Fill: opposite azimuth, low elevation — lifts shadow sides.
    const float fi     = std::clamp(m_lighting.fill_intensity, 0.0f, 2.0f);
    const float fy     = ky + pi;
    const float fp     = -kp * 0.35f;
    const float fp_cos = std::cos(fp);
    const float fill_dir[4]    = {fp_cos * std::cos(fy), std::sin(fp), fp_cos * std::sin(fy), 0.0f};
    const GLfloat fill_diffuse[4] = {fi * 0.55f, fi * 0.62f, fi * 0.80f, 1.0f};
    glLightfv(GL_LIGHT1, GL_POSITION, fill_dir);
    glLightfv(GL_LIGHT1, GL_DIFFUSE,  fill_diffuse);
    glLightfv(GL_LIGHT1, GL_SPECULAR, fill_diffuse);
    glEnable(GL_LIGHT1);

    // Bounce: cool light from straight above so tops of models/terrain read.
    const float bi = std::clamp(m_lighting.bounce_intensity, 0.0f, 1.0f);
    const GLfloat bounce_dir[4]    = {0.0f, 1.0f, 0.0f, 0.0f};
    const GLfloat bounce_diffuse[4]= {bi * 0.75f, bi * 0.85f, bi * 1.0f, 1.0f};
    glLightfv(GL_LIGHT2, GL_POSITION, bounce_dir);
    glLightfv(GL_LIGHT2, GL_DIFFUSE,  bounce_diffuse);
    glLightfv(GL_LIGHT2, GL_SPECULAR, bounce_diffuse);
    glEnable(GL_LIGHT2);

    // Shared material: glColor drives ambient+diffuse, soft highlight.
    const GLfloat specular[4]  = {0.16f, 0.16f, 0.18f, 1.0f};
    const GLfloat shininess[1] = {28.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR,  specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHTING);

    // ── Modern shader path — delegate entirely to LightRig ───────────────────
    // LightRig::build_from_viewport_lighting rebuilds the 3-light rig from the
    // panel values. LightRig::upload_to_shader transforms directions to view
    // space and calls all setDirLights/setAmbient/setFog/setExposure in one go.
    // The shader must already be bound by the caller (paintGL binds it before
    // calling apply_lighting).
    const ViewportLighting& live_lighting = m_lighting;
    if (m_viewport_shader.ready()) {
        m_light_rig.build_from_viewport_lighting(live_lighting);
        m_light_rig.upload_to_shader(m_viewport_shader, m_gizmo_view);

        // Upload up to 8 active point lights in view space
        if (m_has_scene && !m_scene.lights.empty()) {
            struct CandidateLight {
                render::ViewportShader::PointLightData pld;
                float dist_sq;
            };
            std::vector<CandidateLight> candidates;
            candidates.reserve(m_scene.lights.size());

            // Access active fire emitters to match dynamic flicker
            const auto& emitters = m_particle_engine.emitters();

            for (const auto& l : m_scene.lights) {
                if (l.type != 3) continue; // 3 = Point light

                // Check owning object visibility if assigned
                if (l.object_index >= 0 && l.object_index < (int)m_scene.objects.size()) {
                    if (m_scene.objects[l.object_index].hidden) continue;
                }

                // Transform world pos to view space using column-major m_gizmo_view
                const float px = l.pos[0], py = l.pos[1], pz = l.pos[2];
                const float vx = m_gizmo_view[0]*px + m_gizmo_view[4]*py + m_gizmo_view[8]*pz  + m_gizmo_view[12];
                const float vy = m_gizmo_view[1]*px + m_gizmo_view[5]*py + m_gizmo_view[9]*pz  + m_gizmo_view[13];
                const float vz = m_gizmo_view[2]*px + m_gizmo_view[6]*py + m_gizmo_view[10]*pz + m_gizmo_view[14];

                const float radius = l.radius > 0.0f ? l.radius : 280.0f;
                // Cull point lights that are completely behind the camera beyond their radius
                if (vz > radius) continue;

                float intensity = l.intensity;
                // Only fire-linked lights flicker! (Torches, braziers, campfires)
                // Ambient lights, glowing crystals, lanterns, etc. stay steady.
                if (m_render_effects_enabled && l.flicker) {
                    float flicker_mult = 1.0f;
                    // Check if matched to an active fire emitter
                    bool found_emitter = false;
                    for (const auto& em : emitters) {
                        if (em.object_id == l.object_index && em.is_fire) {
                            flicker_mult = em.light_intensity_current;
                            found_emitter = true;
                            break;
                        }
                    }
                    if (!found_emitter) {
                        flicker_mult = 0.82f + 0.18f * (std::sin(m_effects_time * 6.5f) * 0.65f + std::sin(m_effects_time * 17.8f) * 0.35f);
                    }
                    intensity *= flicker_mult;
                }

                CandidateLight cl;
                cl.pld.pos_view[0] = vx;
                cl.pld.pos_view[1] = vy;
                cl.pld.pos_view[2] = vz;
                cl.pld.color[0] = l.color[0] * intensity;
                cl.pld.color[1] = l.color[1] * intensity;
                cl.pld.color[2] = l.color[2] * intensity;
                cl.pld.radius = radius;
                cl.dist_sq = vx*vx + vy*vy + vz*vz;

                candidates.push_back(cl);
            }

            // Prioritize the 8 closest point lights to the camera eye
            if (candidates.size() > 8) {
                std::partial_sort(candidates.begin(), candidates.begin() + 8, candidates.end(),
                                  [](const CandidateLight& a, const CandidateLight& b) {
                                      return a.dist_sq < b.dist_sq;
                                  });
                candidates.resize(8);
            }

            std::vector<render::ViewportShader::PointLightData> pts;
            pts.reserve(candidates.size());
            for (const auto& c : candidates) {
                pts.push_back(c.pld);
            }

            m_viewport_shader.setPointLights(pts.data(), static_cast<int>(pts.size()));
        } else {
            m_viewport_shader.setPointLights(nullptr, 0);
        }
    }
}

void Viewport3DWidget::apply_fog() {
    const bool lit_mode = (m_has_model || m_has_scene) && !m_has_texture;
    const bool fog_on = m_lighting.fog_enabled && lit_mode;
    const float color[3] = {0.07f, 0.075f, 0.086f};
    const float density = std::clamp(m_lighting.fog_density, 0.0f, 0.01f);

    if (fog_on) {
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_EXP2);
        const GLfloat gl_color[4] = {color[0], color[1], color[2], 1.0f};
        glFogfv(GL_FOG_COLOR, gl_color);
        glFogf(GL_FOG_DENSITY, density);
    } else {
        glDisable(GL_FOG);
    }
    // Shader fog is set by LightRig::upload_to_shader() called from apply_lighting().
}

void Viewport3DWidget::upload_current_model_matrix() {
    if (!m_viewport_shader.ready()) return;
    GLfloat mv[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    // GL_MODELVIEW_MATRIX is exactly view*model — feed it as uModelView.
    // The shader does: gl_Position = uProj * (uModelView * aPos)  — no double-transform.
    m_viewport_shader.setModelView(mv);
}

// Ground meshes frequently ship without normals; build smooth vertex normals
// by accumulating face normals so scene terrain shades under the light rig.
void Viewport3DWidget::compute_missing_normals(av::PODMesh& mesh) {
    const size_t nv = mesh.positions.size() / 3;
    if (nv == 0 || !mesh.normals.empty()) return;
    mesh.normals.assign(nv * 3, 0.0f);
    auto add_face = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
        if (i0 >= nv || i1 >= nv || i2 >= nv) return;
        const float* p0 = &mesh.positions[i0 * 3];
        const float* p1 = &mesh.positions[i1 * 3];
        const float* p2 = &mesh.positions[i2 * 3];
        float e1x = p1[0]-p0[0], e1y = p1[1]-p0[1], e1z = p1[2]-p0[2];
        float e2x = p2[0]-p0[0], e2y = p2[1]-p0[1], e2z = p2[2]-p0[2];
        float nx = e1y*e2z - e1z*e2y;
        float ny = e1z*e2x - e1x*e2z;
        float nz = e1x*e2y - e1y*e2x;
        mesh.normals[i0*3+0] += nx; mesh.normals[i0*3+1] += ny; mesh.normals[i0*3+2] += nz;
        mesh.normals[i1*3+0] += nx; mesh.normals[i1*3+1] += ny; mesh.normals[i1*3+2] += nz;
        mesh.normals[i2*3+0] += nx; mesh.normals[i2*3+1] += ny; mesh.normals[i2*3+2] += nz;
    };
    if (!mesh.indices.empty()) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            add_face(mesh.indices[i], mesh.indices[i+1], mesh.indices[i+2]);
    } else {
        for (size_t i = 0; i + 2 < nv; i += 3) add_face(uint32_t(i), uint32_t(i+1), uint32_t(i+2));
    }
    for (size_t v = 0; v < nv; ++v) {
        float nx = mesh.normals[v*3+0], ny = mesh.normals[v*3+1], nz = mesh.normals[v*3+2];
        const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) { mesh.normals[v*3+0] = nx/len; mesh.normals[v*3+1] = ny/len; mesh.normals[v*3+2] = nz/len; }
        else { mesh.normals[v*3+0] = 0.0f; mesh.normals[v*3+1] = 1.0f; mesh.normals[v*3+2] = 0.0f; }
    }
}

void Viewport3DWidget::draw_texture_poster() {
    if (!m_texture_tex) return;

    // Poster stands vertically on the grid at the origin and always faces the
    // camera (yaw only — it never tilts with pitch). Basis built like the
    // reference editor's buildBackground().
    const float pi = 3.14159265358979323846f;
    const float yaw = m_cam_yaw * pi / 180.0f;
    const float pitch = m_cam_pitch * pi / 180.0f;
    const float cos_p = std::cos(pitch);
    const float eye[3] = {
        m_cam_target[0] + m_cam_dist * cos_p * std::sin(yaw),
        m_cam_target[1] + m_cam_dist * std::sin(pitch),
        m_cam_target[2] + m_cam_dist * cos_p * std::cos(yaw),
    };
    float n[3] = {eye[0]-m_cam_target[0], 0.0f, eye[2]-m_cam_target[2]};
    float nl = std::sqrt(n[0]*n[0] + n[2]*n[2]);
    if (nl < 1e-6f) nl = 1.0f;
    n[0] /= nl; n[2] /= nl;
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {n[2], 0.0f, -n[0]};
    float upv[3] = {0.0f, 1.0f, 0.0f};

    // Fit the poster: longest axis ~55% of scene depth, aspect preserved so
    // panoramic textures never stretch into mile-wide quads.
    const float longest = std::max(160.0f, m_scene_extent * 0.55f);
    const float w = m_texture_aspect >= 1.0f ? longest : longest * m_texture_aspect;
    const float h = m_texture_aspect >= 1.0f ? longest / m_texture_aspect : longest;
    const float sw = w * 0.5f, sh = h * 0.5f;
    float cx = m_cam_target[0];
    float cy = m_cam_target[1] + sh * 0.5f;
    float cz = m_cam_target[2];

    float M[16];
    M[0]=right[0]*sw; M[4]=upv[0]*sh; M[8] =n[0]; M[12]=cx;
    M[1]=right[1]*sw; M[5]=upv[1]*sh; M[9] =n[1]; M[13]=cy;
    M[2]=right[2]*sw; M[6]=upv[2]*sh; M[10]=n[2]; M[14]=cz;
    M[3]=0; M[7]=0; M[11]=0; M[15]=1;

    glPushMatrix();
    glMultMatrixf(M);
    glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, m_texture_tex); glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1, 1); glVertex3f( 1.0f, -1.0f, 0.0f);
    glTexCoord2f(1, 0); glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(0, 0); glVertex3f(-1.0f,  1.0f, 0.0f);
    glEnd(); glDisable(GL_TEXTURE_2D);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}

void Viewport3DWidget::draw_scene_background() {
    const av::SceneObject* background = nullptr;
    GLuint bg_tex = 0;
    for (const auto& object : m_scene.objects) {
        if (!object.hidden && !object.background_name.empty()) {
            auto it = m_scene_background_textures.find(object.background_name);
            if (it != m_scene_background_textures.end() && it->second) {
                background = &object;
                bg_tex = it->second;
                break;
            }
        }
    }
    if (!background) {
        for (const auto& object : m_scene.objects) {
            if (!object.background_name.empty()) {
                auto it = m_scene_background_textures.find(object.background_name);
                if (it != m_scene_background_textures.end() && it->second) {
                    background = &object;
                    bg_tex = it->second;
                    break;
                }
            }
        }
    }
    if (!background || !bg_tex) return;

    // Same spherical camera basis as paintGL. The quad is a camera-facing plane
    // placed inside the far clipping plane, shifted by the background object's
    // transform so it parallax-scrolls with the level.
    const float pi = 3.14159265358979323846f;
    const float yaw = m_cam_yaw * pi / 180.0f;
    const float pitch = m_cam_pitch * pi / 180.0f;
    const float cos_p = std::cos(pitch);
    const float eye[3] = {
        m_cam_target[0] + m_cam_dist * cos_p * std::sin(yaw),
        m_cam_target[1] + m_cam_dist * std::sin(pitch),
        m_cam_target[2] + m_cam_dist * cos_p * std::cos(yaw),
    };
    float fwd[3] = {m_cam_target[0]-eye[0], m_cam_target[1]-eye[1], m_cam_target[2]-eye[2]};
    float fl = std::sqrt(fwd[0]*fwd[0] + fwd[1]*fwd[1] + fwd[2]*fwd[2]);
    if (fl < 1e-6f) fl = 1.0f;
    fwd[0] /= fl; fwd[1] /= fl; fwd[2] /= fl;

    const float world_up[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {
        world_up[1]*fwd[2] - world_up[2]*fwd[1],
        world_up[2]*fwd[0] - world_up[0]*fwd[2],
        world_up[0]*fwd[1] - world_up[1]*fwd[0],
    };
    float rl = std::sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
    if (rl < 1e-6f) rl = 1.0f;
    right[0] /= rl; right[1] /= rl; right[2] /= rl;
    float upv[3] = {
        right[1]*fwd[2] - right[2]*fwd[1],
        right[2]*fwd[0] - right[0]*fwd[2],
        right[0]*fwd[1] - right[1]*fwd[0],
    };
    float ul = std::sqrt(upv[0]*upv[0] + upv[1]*upv[1] + upv[2]*upv[2]);
    if (ul < 1e-6f) { upv[0] = 0.0f; upv[1] = 1.0f; upv[2] = 0.0f; }
    else { upv[0] /= ul; upv[1] /= ul; upv[2] /= ul; }

    const float aspect = width() / static_cast<float>(std::max(1, height()));
    const float near_z = std::max(0.1f, m_cam_dist * 0.001f);
    const float far_z = std::max({10000.0f, m_cam_dist * 24.0f, m_scene_extent * 4.0f});
    const float dist = (far_z - near_z) * 0.85f + near_z;
    const float half_h = dist * std::tan(45.0f * pi / 360.0f);
    const float half_w = half_h * aspect;
    const float sw = half_w * 1.5f, sh = half_h * 1.5f;

    // Parallax from the background object's transform
    const float u = std::fabs(background->pos_z) > 0.001f ? background->pos_z : 1.0f;
    float shift_x = 0.5f * half_w * (background->pos_x / u);
    float shift_y = 0.5f * half_h * (background->pos_y / u);
    const float max_shift_x = half_w * 0.15f, max_shift_y = half_h * 0.15f;
    shift_x = std::clamp(shift_x, -max_shift_x, max_shift_x);
    shift_y = std::clamp(shift_y, -max_shift_y, max_shift_y);

    const float cx = eye[0] + fwd[0]*dist + right[0]*shift_x + upv[0]*shift_y;
    const float cy = eye[1] + fwd[1]*dist + right[1]*shift_x + upv[1]*shift_y;
    const float cz = eye[2] + fwd[2]*dist + right[2]*shift_x + upv[2]*shift_y;

    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, bg_tex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f);
    glVertex3f(cx - right[0]*sw - upv[0]*sh,
               cy - right[1]*sw - upv[1]*sh,
               cz - right[2]*sw - upv[2]*sh);

    glTexCoord2f(1.0f, 1.0f);
    glVertex3f(cx + right[0]*sw - upv[0]*sh,
               cy + right[1]*sw - upv[1]*sh,
               cz + right[2]*sw - upv[2]*sh);

    glTexCoord2f(1.0f, 0.0f);
    glVertex3f(cx + right[0]*sw + upv[0]*sh,
               cy + right[1]*sw + upv[1]*sh,
               cz + right[2]*sw + upv[2]*sh);

    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(cx - right[0]*sw + upv[0]*sh,
               cy - right[1]*sw + upv[1]*sh,
               cz - right[2]*sw + upv[2]*sh);
    glEnd();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    if (m_lighting.fog_enabled) glEnable(GL_FOG);
}

void Viewport3DWidget::draw_scene() {
    glEnable(GL_LIGHTING);
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);

    // Camera position for distance culling
    const float rad_pitch = m_cam_pitch * (3.14159265f / 180.0f);
    const float rad_yaw   = m_cam_yaw * (3.14159265f / 180.0f);
    const float eye_x = m_cam_target[0] + m_cam_dist * std::cos(rad_pitch) * std::sin(rad_yaw);
    const float eye_y = m_cam_target[1] + m_cam_dist * std::sin(rad_pitch);
    const float eye_z = m_cam_target[2] + m_cam_dist * std::cos(rad_pitch) * std::cos(rad_yaw);
    const float far_z = std::max({10000.0f, m_cam_dist * 24.0f, m_scene_extent * 4.0f});
    const float max_vis_dist_sq = (far_z * 2.0f) * (far_z * 2.0f);

    for (const auto& obj : m_render_objects) {
        if (obj.hidden) continue;

        const float dx = obj.pos[0] - eye_x;
        const float dy = obj.pos[1] - eye_y;
        const float dz = obj.pos[2] - eye_z;
        if (dx*dx + dy*dy + dz*dz > max_vis_dist_sq) continue;

        glEnable(GL_LIGHTING);

        // Selection indicator: sleek node dot + subtle pulse halo (no blurry crosshairs)
        // plus animation: playful lift + spring shake + subtle brightness flash
        // Selection indicator: sleek node dot + vibrant blue outline with violet tint glow
        // plus animation: playful visual-only pop up lift + spring shake + brightness flash
        const bool is_selected = (obj.object_index == m_selected_scene_object);
        float anim_flash = 1.0f;
        float anim_lift = 0.0f;
        float anim_shake_x = 0.0f;
        float anim_shake_z = 0.0f;

        if (is_selected && m_select_anim_obj == obj.object_index) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - m_select_anim_start).count();
            const float kAnimDur = 0.35f;
            if (elapsed < kAnimDur) {
                float t = elapsed / kAnimDur;
                float decay = (1.0f - t);
                anim_flash = 1.0f + 0.35f * decay;
                anim_lift = std::sin(t * 3.14159265f) * 3.5f * decay;
                anim_shake_x = std::sin(t * 3.14159265f * 6.0f) * 1.8f * decay;
                anim_shake_z = std::cos(t * 3.14159265f * 6.0f) * 1.2f * decay;
                update();
            }
        }

        // Draw ground meshes via VBO/EBO using world_matrix (object-local XY)
        if (!obj.ground_gpu.empty()) {
            glPushMatrix();
            glMultMatrixf(obj.world_matrix);
            if (is_selected && (anim_lift != 0.0f || anim_shake_x != 0.0f)) {
                glTranslatef(anim_shake_x, anim_lift, anim_shake_z);
            }
            upload_current_model_matrix();
            for (size_t mesh_index = 0; mesh_index < obj.ground_gpu.size(); ++mesh_index) {
                const MeshGpu& g = obj.ground_gpu[mesh_index];
                if (!g.valid()) continue;
                GLuint texture = (m_show_textures && mesh_index < obj.ground_textures.size()) ? obj.ground_textures[mesh_index] : 0;
                if (texture) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glColor4f(anim_flash, anim_flash, anim_flash, 1.0f);
                    if (m_viewport_shader.ready()) {
                        m_viewport_shader.setHasTexture(true);
                        m_viewport_shader.setTexture(0);
                        m_viewport_shader.setMaterialColor(anim_flash, anim_flash, anim_flash, 1.0f);
                    }
                } else {
                    glColor4f(0.30f * anim_flash, 0.63f * anim_flash, 0.45f * anim_flash, 1.0f);
                    if (m_viewport_shader.ready()) {
                        m_viewport_shader.setHasTexture(false);
                        m_viewport_shader.setMaterialColor(0.30f * anim_flash, 0.63f * anim_flash, 0.45f * anim_flash, 1.0f);
                    }
                }
                draw_mesh_gpu(g);
                if (texture) glDisable(GL_TEXTURE_2D);
            }
            glPopMatrix();
        }

        // Draw POD model using render_matrix (includes ModelComponent's baked Y-rotation and template_scaling)
        if (!obj.mesh_name.empty()) {
            auto mit = m_scene_models.find(obj.mesh_name);
            if (mit != m_scene_models.end()) {
                glPushMatrix();
                glMultMatrixf(obj.render_matrix);
                if (is_selected && (anim_lift != 0.0f || anim_shake_x != 0.0f)) {
                    glTranslatef(anim_shake_x, anim_lift, anim_shake_z);
                }
                if (is_selected && anim_flash > 1.01f) {
                    glColor4f(anim_flash, anim_flash, anim_flash, 1.0f);
                }
                static const std::vector<GLuint> empty_tex;
                auto tit = m_scene_model_textures.find(obj.mesh_name);
                const auto& texs = (tit != m_scene_model_textures.end()) ? tit->second : empty_tex;
                auto lit = m_scene_model_gpu.find(obj.mesh_name);
                const std::vector<MeshGpu>* gpu = (lit != m_scene_model_gpu.end()) ? &lit->second : nullptr;
                const float* tint = obj.has_diffuse_color ? obj.diffuse_color : nullptr;
                draw_pod_instance(mit->second, texs, gpu, m_frame, tint);
                glPopMatrix();
            }
        }

        // Non-visual marker / logic objects (spawn point, portal, trigger, camera, collider)
        if (obj.ground_gpu.empty() && obj.mesh_name.empty()) {
            if (is_selected) {
                // When selected, visual highlight is rendered in draw_selection_highlight()
                // with two-pass depth testing (faint when occluded, crisp when visible).
                continue;
            }
            if (m_viewport_shader.ready()) m_viewport_shader.release();
            glPushMatrix();
            glMultMatrixf(obj.world_matrix);
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Determine object role and distinctive visual color palette:
            // - Spawn Point: vibrant emerald green (#40F060)
            // - Portal: neon purple/magenta (#D040FF)
            // - Camera / Cinematic: amber yellow (#FFB820)
            // - Secret / Trigger: vibrant cyan (#20E0F0)
            // - Colliders / Non-visual logic: warm coral / slate
            std::string low_name = obj.name;
            for (char& c : low_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            float mr = 0.45f, mg = 0.65f, mb = 0.95f; // default slate
            bool is_spawn = obj.is_spawn_point || low_name.rfind("spawn", 0) == 0;
            bool is_portal = obj.is_portal || low_name.rfind("portal", 0) == 0;
            bool is_cam = obj.is_camera || low_name.rfind("cam", 0) == 0;
            bool is_secret = low_name.rfind("secret", 0) == 0;
            bool is_sewer = low_name.rfind("sewer", 0) == 0;
            bool is_light = false;
            if (m_has_scene && obj.object_index >= 0 && obj.object_index < (int)m_scene.objects.size()) {
                const auto& so = m_scene.objects[obj.object_index];
                const auto& comps = so.resolved_components.empty() ? so.components : so.resolved_components;
                for (const auto& c : comps) {
                    if (c.type_name.find("Light") != std::string::npos || c.payload_field == 130 || c.type_id == 130) {
                        is_light = true;
                        break;
                    }
                }
            }
            if (!is_light && (low_name.find("light") != std::string::npos || low_name.find("glow") != std::string::npos)) {
                is_light = true;
            }

            if (is_spawn) {
                mr = 0.25f; mg = 0.94f; mb = 0.38f; // emerald green
            } else if (is_portal) {
                mr = 0.88f; mg = 0.25f; mb = 0.96f; // vibrant magenta
            } else if (is_cam) {
                mr = 1.00f; mg = 0.72f; mb = 0.15f; // amber gold
            } else if (is_light) {
                mr = 1.00f; mg = 0.88f; mb = 0.30f; // luminous warm golden yellow for light sources
            } else if (is_secret || is_sewer) {
                mr = 0.15f; mg = 0.88f; mb = 0.95f; // bright cyan
            }

            if (is_selected) {
                mr = std::min(1.0f, mr * 1.25f);
                mg = std::min(1.0f, mg * 1.25f);
                mb = std::min(1.0f, mb * 1.25f);
            }

            // 1. Draw 3D diamond / octahedron marker in world space
            const float dsize = is_selected ? 6.5f : 4.5f;
            glColor4f(mr, mg, mb, is_selected ? 0.95f : 0.80f);
            glLineWidth(is_selected ? 2.5f : 1.6f);
            glBegin(GL_LINES);
            // Octahedron wireframe (upper pyramid)
            glVertex3f(0, dsize, 0); glVertex3f(-dsize*0.7f, 0, 0);
            glVertex3f(0, dsize, 0); glVertex3f( dsize*0.7f, 0, 0);
            glVertex3f(0, dsize, 0); glVertex3f(0, 0, -dsize*0.7f);
            glVertex3f(0, dsize, 0); glVertex3f(0, 0,  dsize*0.7f);
            // Lower pyramid
            glVertex3f(0, -dsize, 0); glVertex3f(-dsize*0.7f, 0, 0);
            glVertex3f(0, -dsize, 0); glVertex3f( dsize*0.7f, 0, 0);
            glVertex3f(0, -dsize, 0); glVertex3f(0, 0, -dsize*0.7f);
            glVertex3f(0, -dsize, 0); glVertex3f(0, 0,  dsize*0.7f);
            // Equator
            glVertex3f(-dsize*0.7f, 0, 0); glVertex3f(0, 0,  dsize*0.7f);
            glVertex3f(0, 0,  dsize*0.7f); glVertex3f( dsize*0.7f, 0, 0);
            glVertex3f( dsize*0.7f, 0, 0); glVertex3f(0, 0, -dsize*0.7f);
            glVertex3f(0, 0, -dsize*0.7f); glVertex3f(-dsize*0.7f, 0, 0);
            glEnd();

            // 2. Draw Trigger / Zone volume box if local_aabb exists or if spawn/portal
            float aabb[4] = {0, 0, 0, 0};
            bool has_aabb = parse_local_aabb(obj.local_aabb, aabb);
            if (has_aabb || is_spawn || is_portal) {
                float ax = 0.0f, ay = 0.0f, aw = 40.0f, ah = 70.0f;
                if (has_aabb) {
                    ax = aabb[0]; ay = aabb[1]; aw = aabb[2]; ah = aabb[3];
                }
                const float x0 = ax, y0 = ay, x1 = ax + aw, y1 = ay + ah;
                const float zt = std::max(6.0f, aw * 0.14f);

                glColor4f(mr, mg, mb, is_selected ? 0.65f : 0.35f);
                glLineWidth(is_selected ? 2.0f : 1.2f);
                glBegin(GL_LINES);
                // Front rectangle (at -zt)
                glVertex3f(x0, y0, -zt); glVertex3f(x1, y0, -zt);
                glVertex3f(x1, y0, -zt); glVertex3f(x1, y1, -zt);
                glVertex3f(x1, y1, -zt); glVertex3f(x0, y1, -zt);
                glVertex3f(x0, y1, -zt); glVertex3f(x0, y0, -zt);
                // Back rectangle (at +zt)
                glVertex3f(x0, y0,  zt); glVertex3f(x1, y0,  zt);
                glVertex3f(x1, y0,  zt); glVertex3f(x1, y1,  zt);
                glVertex3f(x1, y1,  zt); glVertex3f(x0, y1,  zt);
                glVertex3f(x0, y1,  zt); glVertex3f(x0, y0,  zt);
                // Connecting edges
                glVertex3f(x0, y0, -zt); glVertex3f(x0, y0,  zt);
                glVertex3f(x1, y0, -zt); glVertex3f(x1, y0,  zt);
                glVertex3f(x1, y1, -zt); glVertex3f(x1, y1,  zt);
                glVertex3f(x0, y1, -zt); glVertex3f(x0, y1,  zt);
                glEnd();

                // Direction pointer arrow for spawn points
                if (is_spawn) {
                    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
                    float arr_len = 16.0f;
                    glColor4f(0.25f, 0.94f, 0.38f, 0.90f);
                    glBegin(GL_LINES);
                    glVertex3f(cx, cy, 0); glVertex3f(cx + arr_len, cy, 0);
                    glVertex3f(cx + arr_len, cy, 0); glVertex3f(cx + arr_len - 5.0f, cy + 4.0f, 0);
                    glVertex3f(cx + arr_len, cy, 0); glVertex3f(cx + arr_len - 5.0f, cy - 4.0f, 0);
                    glEnd();
                }
            }

            glLineWidth(1.0f);
            glDisable(GL_BLEND);
            glEnable(GL_LIGHTING);
            glPopMatrix();
            if (m_viewport_shader.ready()) m_viewport_shader.bind();
        }

        // Selection visuals now live in draw_selection_highlight(): the exact
        // 3D mesh edges + inverted-hull rim of the object (see
        // docs/ruby/RESEARCH_camera_gizmo_selection_saving.md §1.3) — never a
        // flat box over the object.
        (void)is_selected;
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh-accurate selection highlight
//
// Highlights the ACTUAL geometry of the selected object in 3D:
//   1. crisp triangle-edge wireframe (bright blue) drawn with the exact
//      render transforms (render_matrix + node matrices) used by the shaded
//      pass, so edges sit exactly on the visible mesh;
//   2. an inverted-hull rim (violet) — back faces drawn with vertices pushed
//      out along their normals — the classic editor silhouette.
// Non-visual objects (spawn/portal/trigger) have no mesh and keep their
// role markers drawn in draw_scene().
// ─────────────────────────────────────────────────────────────────────────────
void Viewport3DWidget::draw_selection_highlight() {
    const int idx = m_selected_scene_object;
    if (idx < 0 || idx >= (int)m_scene.objects.size()) return;
    const SceneRenderObject* ro = nullptr;
    for (const auto& r : m_render_objects)
        if (r.object_index == idx) { ro = &r; break; }
    if (!ro) return;

    // Select-flash animation envelope (same as the fill pass).
    float anim_flash = 1.0f;
    if (m_select_anim_obj == idx) {
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - m_select_anim_start).count();
        if (elapsed < 0.35f) anim_flash = 1.0f + 0.35f * (1.0f - elapsed / 0.35f);
    }

    // Distance-scaled rim offset (world units → ~6‱ of the view distance).
    float rim_off = 0.05f;
    {
        const float dx = ro->pos[0] - m_gizmo_eye[0];
        const float dy = ro->pos[1] - m_gizmo_eye[1];
        const float dz = ro->pos[2] - m_gizmo_eye[2];
        rim_off = std::clamp(0.006f * std::sqrt(dx*dx + dy*dy + dz*dz), 0.02f, 4.0f);
    }

    const float edge_r = 0.16f * anim_flash, edge_g = 0.82f * anim_flash, edge_b = 1.00f * anim_flash;
    const float rim_r  = 0.72f * anim_flash, rim_g  = 0.25f * anim_flash, rim_b  = 0.98f * anim_flash;
    const float occ_r  = 0.55f * anim_flash, occ_g  = 0.58f * anim_flash, occ_b  = 0.62f * anim_flash;

    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);            // overlay pass: never writes depth
    glDisable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Geometry renderer parameterized by rim/edge opacity, line width, and color
    auto draw_geom = [&](float er, float eg, float eb, float rim_alpha, float edge_alpha, float line_w, bool include_rim, bool is_occluded) {
        // ── POD model objects: per-node edges + rim ─────────────────────────
        if (!ro->mesh_name.empty()) {
            auto mit = m_scene_models.find(ro->mesh_name);
            if (mit != m_scene_models.end()) {
                const av::PODModel& model = mit->second;
                glPushMatrix();
                glMultMatrixf(ro->render_matrix);

                auto git = m_scene_model_gpu.find(ro->mesh_name);
                const std::vector<MeshGpu>* gpu = (git != m_scene_model_gpu.end()) ? &git->second : nullptr;

                for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
                    const auto& node = model.nodes[ni];
                    if (node.object_index < 0 || node.object_index >= (int)model.meshes.size()) continue;
                    const av::PODMesh& mesh = model.meshes[node.object_index];
                    if (mesh.positions.empty()) continue;

                    glPushMatrix();
                    if (model.has_center_point)
                        glTranslatef(-model.center_point[0], -model.center_point[1], -model.center_point[2]);
                    float node_matrix[16];
                    av::get_node_matrix(model, ni, m_frame, node_matrix);
                    glMultMatrixf(node_matrix);

                    const MeshGpu* g = nullptr;
                    if (gpu && node.object_index < (int)gpu->size()) g = &(*gpu)[node.object_index];
                    if (g && g->valid()) {
                        const uint64_t key = (0x504F44ull << 32) | (uint64_t)(uint32_t)g->pos_vbo;
                        if (include_rim && rim_alpha > 0.0f) {
                            draw_mesh_rim(*g, mesh, rim_off, rim_r, rim_g, rim_b, rim_alpha);
                        }
                        draw_mesh_edges(*g, mesh, key, er, eg, eb, edge_alpha, line_w);
                    }
                    glPopMatrix();
                }
                glPopMatrix();
            }
        }

        // ── Ground-mesh objects: edges of the real terrain triangles ────────
        if (!ro->ground_gpu.empty() && idx < (int)m_scene.objects.size()) {
            glPushMatrix();
            glMultMatrixf(ro->world_matrix);
            const av::SceneObject& object = m_scene.objects[idx];
            for (size_t mi = 0; mi < ro->ground_gpu.size() && mi < object.ground_meshes.size(); ++mi) {
                const MeshGpu& g = ro->ground_gpu[mi];
                if (!g.valid()) continue;
                const uint64_t key = ((uint64_t)(uint32_t)idx << 32) | (0x4772ull << 16) | (uint64_t)(uint32_t)mi;
                if (include_rim && rim_alpha > 0.0f) {
                    draw_mesh_rim(g, object.ground_meshes[mi], rim_off, rim_r, rim_g, rim_b, rim_alpha * 0.8f);
                }
                draw_mesh_edges(g, object.ground_meshes[mi], key, er, eg, eb, edge_alpha, line_w);
            }
            glPopMatrix();
        }

        // ── Non-visual marker / logic objects (DirectionalLight, spawn, portal, trigger, zone) ──
        if (ro->mesh_name.empty() && ro->ground_gpu.empty() && idx < (int)m_scene.objects.size()) {
            glPushMatrix();
            glMultMatrixf(ro->world_matrix);

            std::string low_name = ro->name;
            for (char& c : low_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            float mr = 0.45f, mg = 0.65f, mb = 0.95f;
            bool is_spawn = ro->is_spawn_point || low_name.rfind("spawn", 0) == 0;
            bool is_portal = ro->is_portal || low_name.rfind("portal", 0) == 0;
            bool is_cam = ro->is_camera || low_name.rfind("cam", 0) == 0;
            bool is_secret = low_name.rfind("secret", 0) == 0;
            bool is_sewer = low_name.rfind("sewer", 0) == 0;
            bool is_light = false;
            const auto& so = m_scene.objects[idx];
            const auto& comps = so.resolved_components.empty() ? so.components : so.resolved_components;
            for (const auto& c : comps) {
                if (c.type_name.find("Light") != std::string::npos || c.payload_field == 130 || c.type_id == 130) {
                    is_light = true; break;
                }
            }
            if (!is_light && (low_name.find("light") != std::string::npos || low_name.find("glow") != std::string::npos)) {
                is_light = true;
            }

            if (is_spawn) { mr = 0.25f; mg = 0.94f; mb = 0.38f; }
            else if (is_portal) { mr = 0.88f; mg = 0.25f; mb = 0.96f; }
            else if (is_cam) { mr = 1.00f; mg = 0.72f; mb = 0.15f; }
            else if (is_light) { mr = 1.00f; mg = 0.88f; mb = 0.30f; }
            else if (is_secret || is_sewer) { mr = 0.15f; mg = 0.88f; mb = 0.95f; }

            if (is_occluded) {
                float lum = 0.299f * mr + 0.587f * mg + 0.114f * mb;
                mr = occ_r * 0.85f + lum * 0.15f;
                mg = occ_g * 0.85f + lum * 0.15f;
                mb = occ_b * 0.85f + lum * 0.15f;
            }

            const float dsize = 6.5f;
            glColor4f(mr * anim_flash, mg * anim_flash, mb * anim_flash, edge_alpha);
            glLineWidth(line_w);

            glBegin(GL_LINES);
            // Octahedron wireframe
            glVertex3f(0, dsize, 0); glVertex3f(-dsize*0.7f, 0, 0);
            glVertex3f(0, dsize, 0); glVertex3f( dsize*0.7f, 0, 0);
            glVertex3f(0, dsize, 0); glVertex3f(0, 0, -dsize*0.7f);
            glVertex3f(0, dsize, 0); glVertex3f(0, 0,  dsize*0.7f);
            glVertex3f(0, -dsize, 0); glVertex3f(-dsize*0.7f, 0, 0);
            glVertex3f(0, -dsize, 0); glVertex3f( dsize*0.7f, 0, 0);
            glVertex3f(0, -dsize, 0); glVertex3f(0, 0, -dsize*0.7f);
            glVertex3f(0, -dsize, 0); glVertex3f(0, 0,  dsize*0.7f);
            glVertex3f(-dsize*0.7f, 0, 0); glVertex3f(0, 0,  dsize*0.7f);
            glVertex3f(0, 0,  dsize*0.7f); glVertex3f( dsize*0.7f, 0, 0);
            glVertex3f( dsize*0.7f, 0, 0); glVertex3f(0, 0, -dsize*0.7f);
            glVertex3f(0, 0, -dsize*0.7f); glVertex3f(-dsize*0.7f, 0, 0);
            glEnd();

            // LocalAabb volume box
            float aabb[4] = {0, 0, 0, 0};
            bool has_aabb = parse_local_aabb(ro->local_aabb, aabb);
            if (has_aabb || is_spawn || is_portal) {
                float ax = 0.0f, ay = 0.0f, aw = 40.0f, ah = 70.0f;
                if (has_aabb) { ax = aabb[0]; ay = aabb[1]; aw = aabb[2]; ah = aabb[3]; }
                const float x0 = ax, y0 = ay, x1 = ax + aw, y1 = ay + ah;
                const float zt = std::max(6.0f, aw * 0.14f);

                glBegin(GL_LINES);
                // Front rectangle
                glVertex3f(x0, y0, -zt); glVertex3f(x1, y0, -zt);
                glVertex3f(x1, y0, -zt); glVertex3f(x1, y1, -zt);
                glVertex3f(x1, y1, -zt); glVertex3f(x0, y1, -zt);
                glVertex3f(x0, y1, -zt); glVertex3f(x0, y0, -zt);
                // Back rectangle
                glVertex3f(x0, y0,  zt); glVertex3f(x1, y0,  zt);
                glVertex3f(x1, y0,  zt); glVertex3f(x1, y1,  zt);
                glVertex3f(x1, y1,  zt); glVertex3f(x0, y1,  zt);
                glVertex3f(x0, y1,  zt); glVertex3f(x0, y0,  zt);
                // Connecting edges
                glVertex3f(x0, y0, -zt); glVertex3f(x0, y0,  zt);
                glVertex3f(x1, y0, -zt); glVertex3f(x1, y0,  zt);
                glVertex3f(x1, y1, -zt); glVertex3f(x1, y1,  zt);
                glVertex3f(x0, y1, -zt); glVertex3f(x0, y1,  zt);
                glEnd();
            }
            glPopMatrix();
        }
    };

    // Pass 1: Occluded / Behind scene geometry — faint greyish outline (~8% alpha, thin 1.0px lines, no rim)
    glDepthFunc(GL_GREATER);
    draw_geom(occ_r, occ_g, occ_b, 0.0f, 0.08f, 1.0f, false, true);

    // Pass 2: Visible / Direct line-of-sight — vibrant cyan / colored glow (~95% alpha, crisp 1.6px lines + rim glow)
    glDepthFunc(GL_LEQUAL);
    draw_geom(edge_r, edge_g, edge_b, 0.45f, 0.95f, 1.6f, true, false);

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

// Crisp wireframe of a mesh's triangle edges. `src` is the CPU-side geometry
// (positions + indices); the GL_LINES index buffer is built once per source
// GPU buffer and cached in m_sel_line_cache (auto-rebuilt if the mesh is
// re-uploaded).
void Viewport3DWidget::draw_mesh_edges(const MeshGpu& g, const av::PODMesh& src, uint64_t key,
                                       float r, float g_, float b, float a, float line_w) {
    if (!g.valid() || src.positions.empty()) return;
    auto it = m_sel_line_cache.find(key);
    if (it == m_sel_line_cache.end() || it->second.ebo == 0 ||
        it->second.src_pos_vbo != g.pos_vbo ||
        it->second.src_vertex_count != g.vertex_count) {
        // Build GL_LINES indices from the triangle indices (or a sequential
        // triangle fan when the mesh is non-indexed).
        const size_t nv = src.positions.size() / 3;
        const size_t tri = src.indices.empty() ? nv / 3 : src.indices.size() / 3;
        std::vector<uint32_t> lines;
        lines.reserve(tri * 6);
        for (size_t t = 0; t < tri; ++t) {
            const uint32_t i0 = src.indices.empty() ? (uint32_t)(t*3)   : src.indices[t*3];
            const uint32_t i1 = src.indices.empty() ? (uint32_t)(t*3+1) : src.indices[t*3+1];
            const uint32_t i2 = src.indices.empty() ? (uint32_t)(t*3+2) : src.indices[t*3+2];
            if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
            lines.push_back(i0); lines.push_back(i1);
            lines.push_back(i1); lines.push_back(i2);
            lines.push_back(i2); lines.push_back(i0);
        }
        if (lines.empty()) return;

        GLuint ebo = 0;
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(lines.size() * sizeof(uint32_t)),
                     lines.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        if (it != m_sel_line_cache.end() && it->second.ebo)
            glDeleteBuffers(1, &it->second.ebo);
        SelLineCache c;
        c.ebo = ebo;
        c.line_count = (int)lines.size();
        c.src_pos_vbo = g.pos_vbo;
        c.src_vertex_count = g.vertex_count;
        it = m_sel_line_cache.insert_or_assign(key, c).first;
    }

    glColor4f(r, g_, b, a);
    glLineWidth(line_w);
    glEnableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, g.pos_vbo);
    glVertexPointer(3, GL_FLOAT, 0, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.ebo);
    glDrawElements(GL_LINES, it->second.line_count, GL_UNSIGNED_INT, nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Inverted-hull rim: back faces of the mesh drawn with vertices displaced along
// their normals, additive blended → a violet silhouette hugging the geometry.
void Viewport3DWidget::draw_mesh_rim(const MeshGpu& g, const av::PODMesh& src, float offset,
                                     float r, float g_, float b, float a) {
    if (!g.valid() || src.positions.empty()) return;
    if (src.normals.size() < src.positions.size()) return;   // no normals → skip
    const size_t nv = src.positions.size();
    if (nv / 3 > 120000) return;                             // giant meshes: skip
    m_scratch_positions.resize(nv);
    const float* p = src.positions.data();
    const float* n = src.normals.data();
    for (size_t i = 0; i < nv; i += 3) {
        float nx = n[i], ny = n[i+1], nz = n[i+2];
        const float l = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (l > 1e-9f) { nx /= l; ny /= l; nz /= l; }
        m_scratch_positions[i]   = p[i]   + nx * offset;
        m_scratch_positions[i+1] = p[i+1] + ny * offset;
        m_scratch_positions[i+2] = p[i+2] + nz * offset;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, m_scratch_positions.data());
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(r, g_, b, a);
    if (!src.indices.empty())
        glDrawElements(GL_TRIANGLES, (GLsizei)src.indices.size(), GL_UNSIGNED_INT, src.indices.data());
    else
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(nv / 3));
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void Viewport3DWidget::draw_pod_instance(const av::PODModel& model, const std::vector<GLuint>& textures,
                                        const std::vector<MeshGpu>* gpu_meshes, float frame,
                                        const float* tint_color) {
    glEnable(GL_LIGHTING);
    for (int node_index = 0; node_index < static_cast<int>(model.nodes.size()); ++node_index) {
        const auto& node = model.nodes[node_index];
        if (node.object_index < 0 || node.object_index >= static_cast<int>(model.meshes.size())) continue;
        const auto& mesh = model.meshes[node.object_index];
        if (mesh.positions.empty()) continue;

        const bool has_bones = (mesh.bones_per_vertex > 0);
        bool skinned = false;
        if (has_bones) {
            if (frame == 0.0f && gpu_meshes && node.object_index < static_cast<int>(gpu_meshes->size()) && (*gpu_meshes)[node.object_index].valid()) {
                skinned = false;
            } else {
                skinned = av::skin_mesh(model, node_index, frame, m_scratch_positions, m_scratch_normals);
            }
        }

        float node_matrix[16];
        av::get_node_matrix(model, node_index, frame, node_matrix);

        glPushMatrix();
        if (model.has_center_point) {
            glTranslatef(-model.center_point[0], -model.center_point[1], -model.center_point[2]);
        }
        glMultMatrixf(node_matrix);

        GLuint texture = 0;
        GLfloat color[] = {1, 1, 1, 1};
        if (node.material_index >= 0 && node.material_index < static_cast<int>(model.materials.size())) {
            const auto& material = model.materials[node.material_index];
            color[0] = material.diffuse[0];
            color[1] = material.diffuse[1];
            color[2] = material.diffuse[2];
            color[3] = material.opacity;
            if (m_show_textures && material.diffuse_texture_index >= 0 && material.diffuse_texture_index < static_cast<int>(textures.size())) {
                texture = textures[material.diffuse_texture_index];
            }
        }
        if (!texture && m_show_textures && !textures.empty()) {
            texture = textures.front();
        }

        if (tint_color) {
            color[0] *= tint_color[0];
            color[1] *= tint_color[1];
            color[2] *= tint_color[2];
        }

        glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, color);
        glColor4fv(color);
        if (texture) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        upload_current_model_matrix();
        if (m_viewport_shader.ready()) {
            m_viewport_shader.setHasTexture(texture != 0);
            if (texture != 0) m_viewport_shader.setTexture(0);
            m_viewport_shader.setMaterialColor(color[0], color[1], color[2], color[3]);
        }

        const bool shader_on = m_viewport_shader.ready();

        if (skinned) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, m_scratch_positions.data());
            if (shader_on) {
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, m_scratch_positions.data());
            }
            if (!m_scratch_normals.empty()) {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, 0, m_scratch_normals.data());
                if (shader_on) {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, m_scratch_normals.data());
                }
            } else if (shader_on) {
                glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
            }
            if (texture && !mesh.uvs.empty()) {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, 0, mesh.uvs.data());
                if (shader_on) {
                    glEnableVertexAttribArray(2);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
                }
            } else if (shader_on) {
                glVertexAttrib2f(2, 0.0f, 0.0f);
            }
            if (!mesh.indices.empty()) {
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, mesh.indices.data());
            } else {
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_scratch_positions.size() / 3));
            }
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            if (shader_on) {
                glDisableVertexAttribArray(0);
                if (!m_scratch_normals.empty()) glDisableVertexAttribArray(1);
                if (texture && !mesh.uvs.empty()) glDisableVertexAttribArray(2);
            }
        } else if (gpu_meshes && node.object_index < static_cast<int>(gpu_meshes->size()) && (*gpu_meshes)[node.object_index].valid()) {
            draw_mesh_gpu((*gpu_meshes)[node.object_index]);
        } else {
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, mesh.positions.data());
            if (shader_on) {
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
            }
            if (!mesh.normals.empty()) {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, 0, mesh.normals.data());
                if (shader_on) {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, mesh.normals.data());
                }
            } else if (shader_on) {
                glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
            }
            if (texture && !mesh.uvs.empty()) {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, 0, mesh.uvs.data());
                if (shader_on) {
                    glEnableVertexAttribArray(2);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
                }
            } else if (shader_on) {
                glVertexAttrib2f(2, 0.0f, 0.0f);
            }
            if (!mesh.indices.empty()) {
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, mesh.indices.data());
            } else {
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.positions.size() / 3));
            }
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            if (shader_on) {
                glDisableVertexAttribArray(0);
                if (!mesh.normals.empty()) glDisableVertexAttribArray(1);
                if (texture && !mesh.uvs.empty()) glDisableVertexAttribArray(2);
            }
        }

        if (texture) glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }
    glDisable(GL_LIGHTING);
}

GLuint Viewport3DWidget::load_texture_any(const std::string& path) {
    if (path.empty()) return 0;
    // Never touch the GL driver without a live, initialized surface — a
    // double-click asset open that races first-paint used to call
    // glGenTextures with no current context and segfault the process.
    if (!context() || !m_gl_initialized) return 0;
    auto it = m_gpu_tex_cache.find(path);
    if (it != m_gpu_tex_cache.end() && it->second) {
        if (glIsTexture(it->second)) return it->second;
        m_gpu_tex_cache.erase(it);
    }

    QImage image = decode_image_file_to_ram(path);
    if (image.isNull()) return 0;
    GLuint tex = upload_image(image);
    if (tex) m_gpu_tex_cache[path] = tex;
    return tex;
}

GLuint Viewport3DWidget::upload_image(const QImage& image) {
    if (!context() || !m_gl_initialized) return 0;   // no live GL surface yet
    QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull()) return 0;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

std::vector<GLuint> Viewport3DWidget::load_pod_textures(const fs::path& pod_path, const av::PODModel& model,
                                                        const std::vector<fs::path>& extra_roots) {
    std::vector<GLuint> textures(model.texture_filenames.size(), 0);
    for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
        for (const auto& candidate : av::assets::texture_candidates(pod_path, model.texture_filenames[i], extra_roots)) {
            if (fs::exists(candidate) && (textures[i] = load_texture_any(candidate.string()))) break;
        }
    }
    if (textures.empty() || textures[0] == 0) {
        for (const auto& candidate : av::assets::texture_candidates(pod_path, pod_path.stem().string(), extra_roots)) {
            if (!fs::exists(candidate)) continue;
            const GLuint tex = load_texture_any(candidate.string());
            if (!tex) continue;
            if (textures.empty()) textures.push_back(tex); else textures[0] = tex;
            break;
        }
    }
    return textures;
}

void Viewport3DWidget::draw_grid() {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // Spacious reference grid: always wide gaps, bounded around camera target,
    // and fading smoothly to transparent so distant horizon lines never alias or moire.
    float grid_step = 100.0f;
    while (grid_step < m_cam_dist * 0.25f) grid_step *= 2.0f;

    const int half_lines = 15;
    const float snap_x = std::round(m_cam_target[0] / grid_step) * grid_step;
    const float snap_z = std::round(m_cam_target[2] / grid_step) * grid_step;
    const float extent = half_lines * grid_step;

    glBegin(GL_LINES);
    // Lines parallel to X axis
    for (int i = -half_lines; i <= half_lines; ++i) {
        const float z = snap_z + i * grid_step;
        const float line_fade = 1.0f - std::abs(static_cast<float>(i)) / static_cast<float>(half_lines);
        const float base_a = 0.35f * line_fade * line_fade;
        if (base_a <= 0.005f) continue;

        const bool is_x_axis = std::abs(z) < (grid_step * 0.4f);
        const float cr = is_x_axis ? 0.88f : 0.35f;
        const float cg = is_x_axis ? 0.42f : 0.38f;
        const float cb = is_x_axis ? 0.46f : 0.42f;
        const float a  = is_x_axis ? std::min(1.0f, base_a * 1.5f) : base_a;

        glColor4f(cr, cg, cb, 0.0f);
        glVertex3f(snap_x - extent, 0.0f, z);
        glColor4f(cr, cg, cb, a);
        glVertex3f(snap_x, 0.0f, z);

        glVertex3f(snap_x, 0.0f, z);
        glColor4f(cr, cg, cb, 0.0f);
        glVertex3f(snap_x + extent, 0.0f, z);
    }

    // Lines parallel to Z axis
    for (int i = -half_lines; i <= half_lines; ++i) {
        const float x = snap_x + i * grid_step;
        const float line_fade = 1.0f - std::abs(static_cast<float>(i)) / static_cast<float>(half_lines);
        const float base_a = 0.35f * line_fade * line_fade;
        if (base_a <= 0.005f) continue;

        const bool is_z_axis = std::abs(x) < (grid_step * 0.4f);
        const float cr = is_z_axis ? 0.38f : 0.35f;
        const float cg = is_z_axis ? 0.69f : 0.38f;
        const float cb = is_z_axis ? 0.94f : 0.42f;
        const float a  = is_z_axis ? std::min(1.0f, base_a * 1.5f) : base_a;

        glColor4f(cr, cg, cb, 0.0f);
        glVertex3f(x, 0.0f, snap_z - extent);
        glColor4f(cr, cg, cb, a);
        glVertex3f(x, 0.0f, snap_z);

        glVertex3f(x, 0.0f, snap_z);
        glColor4f(cr, cg, cb, 0.0f);
        glVertex3f(x, 0.0f, snap_z + extent);
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Viewport3DWidget::draw_model() {
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);
    glEnable(GL_LIGHTING);

    for (int node_index = 0; node_index < static_cast<int>(m_model.nodes.size()); ++node_index) {
        const auto& node = m_model.nodes[node_index];
        if (node.object_index < 0 || node.object_index >= static_cast<int>(m_model.meshes.size())) continue;
        const auto& mesh = m_model.meshes[node.object_index];
        if (mesh.positions.empty()) continue;

        const bool has_bones = (mesh.bones_per_vertex > 0);
        bool skinned = false;
        if (has_bones) {
            if (m_frame == 0.0f && node.object_index < static_cast<int>(m_model_mesh_gpu.size()) && m_model_mesh_gpu[node.object_index].valid()) {
                skinned = false;
            } else {
                skinned = av::skin_mesh(m_model, node_index, m_frame, m_scratch_positions, m_scratch_normals);
            }
        }

        float matrix[16];
        av::get_node_matrix(m_model, node_index, m_frame, matrix);
        glPushMatrix();
        if (m_model.has_center_point) {
            glTranslatef(-m_model.center_point[0], -m_model.center_point[1], -m_model.center_point[2]);
        }
        glMultMatrixf(matrix);

        const int material_index = node.material_index;
        GLuint texture = 0;
        if (material_index >= 0 && material_index < static_cast<int>(m_model.materials.size())) {
            const auto& material = m_model.materials[material_index];
            glColor4f(material.diffuse[0], material.diffuse[1], material.diffuse[2], material.opacity);
            const GLfloat diffuse[] = {material.diffuse[0], material.diffuse[1], material.diffuse[2], material.opacity};
            glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
            if (m_show_textures) {
                if (m_has_import) {
                    if (material_index < static_cast<int>(m_import_textures.size()))
                        texture = m_import_textures[material_index];
                } else if (material.diffuse_texture_index >= 0 &&
                           material.diffuse_texture_index < static_cast<int>(m_material_textures.size())) {
                    texture = m_material_textures[material.diffuse_texture_index];
                }
            }
        } else {
            glColor3f(0.72f, 0.75f, 0.8f);
        }

        if (!texture && m_show_textures) {
            if (m_has_import && !m_import_textures.empty()) {
                texture = m_import_textures.front();
            } else if (!m_material_textures.empty()) {
                texture = m_material_textures.front();
            }
        }

        if (texture) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        upload_current_model_matrix();
        if (m_viewport_shader.ready()) {
            m_viewport_shader.setHasTexture(texture != 0);
            if (texture != 0) m_viewport_shader.setTexture(0);
            if (material_index >= 0 && material_index < static_cast<int>(m_model.materials.size())) {
                const auto& mat = m_model.materials[material_index];
                m_viewport_shader.setMaterialColor(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.opacity);
            } else {
                m_viewport_shader.setMaterialColor(0.72f, 0.75f, 0.8f, 1.0f);
            }
        }

        const bool shader_on = m_viewport_shader.ready();

        if (skinned) {
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, m_scratch_positions.data());
            if (shader_on) {
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, m_scratch_positions.data());
            }
            if (!m_scratch_normals.empty()) {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, 0, m_scratch_normals.data());
                if (shader_on) {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, m_scratch_normals.data());
                }
            } else if (shader_on) {
                glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
            }
            if (texture && !mesh.uvs.empty()) {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, 0, mesh.uvs.data());
                if (shader_on) {
                    glEnableVertexAttribArray(2);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
                }
            } else if (shader_on) {
                glVertexAttrib2f(2, 0.0f, 0.0f);
            }
            if (!mesh.indices.empty()) {
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, mesh.indices.data());
            } else {
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_scratch_positions.size() / 3));
            }
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            if (shader_on) {
                glDisableVertexAttribArray(0);
                if (!m_scratch_normals.empty()) glDisableVertexAttribArray(1);
                if (texture && !mesh.uvs.empty()) glDisableVertexAttribArray(2);
            }
        } else if (node.object_index < static_cast<int>(m_model_mesh_gpu.size()) && m_model_mesh_gpu[node.object_index].valid()) {
            draw_mesh_gpu(m_model_mesh_gpu[node.object_index]);
        } else {
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, mesh.positions.data());
            if (shader_on) {
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, mesh.positions.data());
            }
            if (!mesh.normals.empty()) {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_FLOAT, 0, mesh.normals.data());
                if (shader_on) {
                    glEnableVertexAttribArray(1);
                    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, mesh.normals.data());
                }
            } else if (shader_on) {
                glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
            }
            if (texture && !mesh.uvs.empty()) {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, 0, mesh.uvs.data());
                if (shader_on) {
                    glEnableVertexAttribArray(2);
                    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, mesh.uvs.data());
                }
            } else if (shader_on) {
                glVertexAttrib2f(2, 0.0f, 0.0f);
            }
            if (!mesh.indices.empty()) {
                glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices.size()), GL_UNSIGNED_INT, mesh.indices.data());
            } else {
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.positions.size() / 3));
            }
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_NORMAL_ARRAY);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            if (shader_on) {
                glDisableVertexAttribArray(0);
                if (!mesh.normals.empty()) glDisableVertexAttribArray(1);
                if (texture && !mesh.uvs.empty()) glDisableVertexAttribArray(2);
            }
        }

        if (texture) glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }
    glDisable(GL_LIGHTING);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void Viewport3DWidget::draw_skeleton() {
    glColor3f(0.88f, 0.42f, 0.46f);
    glLineWidth(2.0f); glBegin(GL_LINES);
    for (int i = 0; i < static_cast<int>(m_model.nodes.size()); ++i) {
        const auto& node = m_model.nodes[i];
        if (node.parent_index < 0 || node.parent_index >= static_cast<int>(m_model.nodes.size())) continue;
        float child[16], parent[16]; av::get_node_matrix(m_model, i, m_frame, child); av::get_node_matrix(m_model, node.parent_index, m_frame, parent);
        glVertex3f(child[12], child[13], child[14]); glVertex3f(parent[12], parent[13], parent[14]);
    }
    glEnd(); glLineWidth(1.0f);
}

bool Viewport3DWidget::load_model(const std::string& model_path) {
    if (model_path.empty()) return false;
    // GL must be usable before any upload (see ensure_gl_ready — this load can
    // run synchronously from QTabBar::currentChanged before the first paint).
    if (!ensure_gl_ready()) return false;
    // A model view must own the viewport: cancel any in-flight async scene
    // load so its completion can never land later and silently replace the
    // model with a scene (the "pod opens but I see no model" clobber). The
    // seq/token guard drops the stale completion.
    ++m_scene_load_seq;
    m_scene_load_token.clear();
    if (m_loading_overlay) m_loading_overlay->hide_loading();
    if (m_has_model && m_current_model_path == model_path) {
        bool valid = true;
        for (GLuint t : m_material_textures) {
            if (t != 0 && !glIsTexture(t)) { valid = false; break; }
        }
        for (const MeshGpu& g : m_model_mesh_gpu) {
            if (g.pos_vbo != 0 && !glIsBuffer(g.pos_vbo)) { valid = false; break; }
        }
        if (valid) {
            update();
            return true;
        }
    }
    m_current_model_path = model_path;
    const fs::path file(model_path);
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const bool is_gl = (ext == ".glb" || ext == ".gltf");
    if (is_gl) {
        makeCurrent();
        m_has_model = false;
        m_has_glb = false;
        m_has_texture = false;
        m_has_import = false;
        std::string glb_err;
        if (!m_glb_model.load_from_file(model_path, &glb_err)) {
            return false;
        }
        m_has_glb = true;
        m_cam_target[0] = m_glb_model.center().x();
        m_cam_target[1] = m_glb_model.center().y();
        m_cam_target[2] = m_glb_model.center().z();
        m_cam_dist = std::max(30.0f, m_glb_model.radius() * 2.5f);
        update();
        return true;
    }

    m_has_glb = false;
    m_glb_model.clear();

    // Parse POD / OBJ / FBX into the same av::PODModel scene graph.
    av::PODModel loaded;
    std::string err;
    bool ok = false;
    const bool is_pod = (ext == ".pod");
    std::vector<av::GLTFImageBuffer> gltf_images;
    av::GLTFPBRInfo pbr;
    if (is_pod) {
        loaded = load_pod_to_ram(model_path, "");
        ok = !loaded.meshes.empty();
    } else if (ext == ".obj") {
        ok = av::obj_load(model_path, loaded, &err);
    } else if (ext == ".fbx") {
        loaded = av::fbx_load(model_path);
        ok = !loaded.meshes.empty();
    } else {
        return false;
    }
    if (!ok || loaded.meshes.empty()) return false;

    makeCurrent();
    // Free ephemeral GL state before binding the new model (cached textures in m_gpu_tex_cache are kept).
    m_texture_tex = 0;
    m_material_textures.clear();
    for (GLuint tex : m_import_textures) if (tex) glDeleteTextures(1, &tex);
    m_import_textures.clear();

    m_model = std::move(loaded);
    m_has_import = !is_pod;
    m_has_texture = false;

    if (m_has_import) {
        // Decode embedded glTF base-color images (PNG/JPEG payloads) and bind
        // one GL texture per material slot so imported models look right.
        m_import_textures.assign(m_model.materials.size(), 0);
        if (is_gl && !pbr.materials.empty()) {
            for (size_t i = 0; i < m_import_textures.size() && i < pbr.materials.size(); ++i) {
                if (pbr.materials[i].base_tex < 0) continue;
                int payload = -1;
                for (size_t k = 0; k < pbr.image_gltf_index.size(); ++k)
                    if (pbr.image_gltf_index[k] == pbr.materials[i].base_tex) { payload = int(k); break; }
                if (payload < 0 || payload >= int(pbr.images.size())) continue;
                const auto& image = pbr.images[payload];
                if (image.data.empty()) continue;
                QImage decoded = QImage::fromData(QByteArray(
                    reinterpret_cast<const char*>(image.data.data()), int(image.data.size())));
                if (decoded.isNull() && av::bytes_are_webp(image.data.data(), image.data.size())) {
                    // EXT_texture_webp REPLACES the core texture, so a model can
                    // require WebP and ship no PNG. Qt decodes it only when its
                    // webp image-format plugin happens to be installed; libwebp
                    // is the dependency we control. .copy() detaches the QImage
                    // from the temporary buffer.
                    std::vector<uint8_t> rgba;
                    int ww = 0, hh = 0;
                    if (av::webp_decode_rgba(image.data.data(), image.data.size(), rgba, ww, hh) && !rgba.empty())
                        decoded = QImage(rgba.data(), ww, hh, ww * 4, QImage::Format_RGBA8888).copy();
                }
                if (!decoded.isNull()) m_import_textures[i] = upload_image(decoded);
            }
        }
    } else {
        // POD: load each referenced texture (fallback to a same-stem PVR).
        m_material_textures.assign(m_model.texture_filenames.size(), 0);
        for (size_t i = 0; i < m_model.texture_filenames.size(); ++i) {
            for (const auto& texture_path : av::assets::texture_candidates(file, m_model.texture_filenames[i])) {
                if (!fs::exists(texture_path)) continue;
                m_material_textures[i] = load_texture_any(texture_path.string());
                if (m_material_textures[i]) break;
            }
        }
        if (m_material_textures.empty() || m_material_textures[0] == 0) {
            const auto fallbacks = av::assets::texture_candidates(file, file.stem().string());
            for (const auto& texture_path : fallbacks) if (fs::exists(texture_path)) {
                const GLuint texture = load_texture_any(texture_path.string());
                if (texture) { if (m_material_textures.empty()) m_material_textures.push_back(texture); else m_material_textures[0] = texture; break; }
            }
        }
    }

    // Ensure sane bounds for the camera frame (importers may not set radius).
    if (m_model.radius <= 0.0f) {
        float mn[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
        float mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const auto& mesh : m_model.meshes) {
            for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
                for (int a = 0; a < 3; ++a) {
                    mn[a] = std::min(mn[a], mesh.positions[i + a]);
                    mx[a] = std::max(mx[a], mesh.positions[i + a]);
                }
            }
        }
        if (mn[0] > mx[0]) { mn[0] = -50; mx[0] = 50; mn[1] = -50; mx[1] = 50; mn[2] = -50; mx[2] = 50; }
        m_model.center_x = (mn[0] + mx[0]) * 0.5f;
        m_model.center_y = (mn[1] + mx[1]) * 0.5f;
        m_model.center_z = (mn[2] + mx[2]) * 0.5f;
        const float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
        m_model.radius = std::max(0.1f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    clear_mesh_gpu();
    // VBO/EBO upload, cached per source path so re-opening the same model skips
    // the (previously dominant) display-list recompile on the main thread.
    m_model_mesh_gpu = upload_pod_gpu(model_path, m_model);
    for (const auto& g : m_model_mesh_gpu)
        if (g.valid()) m_all_mesh_gpu.push_back(g);
    doneCurrent();

    m_has_model = true;
    m_has_scene = false;
    m_scene_ready = false;
    m_scene_extent = std::max(10.0f, m_model.radius * 6.0f);

    // Frame camera to model bounds.
    m_cam_target[0] = m_model.center_x;
    m_cam_target[1] = m_model.center_y;
    m_cam_target[2] = m_model.center_z;
    m_cam_dist = std::max(0.1f, m_model.radius * 2.5f);

    emit modelLoaded(QString::fromStdString(file.filename().string()),
                     static_cast<int>(m_model.meshes.size()),
                     m_model.total_vertices);

    update();
    return true;
}

bool Viewport3DWidget::load_scene(const std::string& scene_path) {
    if (scene_path.empty()) return false;
    // GL must be usable before any upload (see ensure_gl_ready — this load can
    // run synchronously from QTabBar::currentChanged before the first paint).
    if (!ensure_gl_ready()) return false;
    // Leaving the current scene aborts any in-flight mesh-edit session without
    // applying it (the edits belonged to the scene being replaced).
    if (m_mesh_edit) end_mesh_edit(false);
    // A fresh synchronous load discards any pending camera from an earlier
    // set_camera_state — the caller applies its own camera again after this
    // returns (activate_document), and adopt_session_state below so falls back
    // to the session camera until then.
    m_has_pending_cam = false;
    if (m_has_scene && m_current_scene_path == scene_path) {
        update();
        return true;
    }

    // Per-document editor state: stash what is on screen into ITS session
    // (camera, selection, tool), then restore the arriving scene's own state.
    stash_current_session();

    // Remember what was on screen: reloading the SAME path from disk (a save
    // round-trip) must keep the user's camera instead of re-framing it.
    const std::string prev_scene_path = m_current_scene_path;
    const bool prev_had_scene = m_has_scene;

    auto cache_it = m_scene_cache.find(scene_path);
    if (cache_it != m_scene_cache.end() && cache_it->second) {
        auto cached = cache_it->second;
        m_current_scene_path = scene_path;
        m_scene = cached->scene;
        m_scene_models = cached->scene_models;
        m_scene_model_textures = cached->scene_model_textures;
        m_scene_ground_textures = cached->scene_ground_textures;
        m_scene_background_textures = cached->scene_background_textures;
        m_render_objects = cached->render_objects;
        m_model_mesh_gpu = cached->model_mesh_gpu;
        m_scene_model_gpu = cached->scene_model_gpu;
        m_all_mesh_gpu = cached->all_mesh_gpu;
        m_scene_extent = cached->scene_extent;

        m_has_scene = true;
        m_scene_ready = true;
        m_has_model = false;
        m_has_texture = false;
        m_has_import = false;
        adopt_session_state(scene_path);

        emit sceneLoaded(m_scene);
        update();
        return true;
    }

    m_current_scene_path = scene_path;

    av::SceneData loaded = parse_scene_from_disk(scene_path);
    if (loaded.objects.empty()) return false;
    m_scene = std::move(loaded);
    m_has_scene = true; m_scene_ready = true;
    m_has_model = false; m_has_texture = false; m_has_import = false;
    m_scene_models.clear(); m_scene_model_textures.clear(); m_scene_ground_textures.clear(); m_scene_background_textures.clear();
    m_scene_model_gpu.clear();

    // ── STEP 1: Multi-threaded Asset Loading across all CPU cores ──
    // Build comprehensive search roots from scene directory, project context, and all imported libraries
    std::vector<fs::path> scene_roots;
    const std::string project_dir = ruby::core::ProjectContext::instance().project_dir();
    if (!project_dir.empty()) {
        scene_roots.push_back(fs::path(project_dir));
        scene_roots.push_back(fs::path(project_dir) / "resources");
        scene_roots.push_back(fs::path(project_dir) / "models");
        scene_roots.push_back(fs::path(project_dir) / "assets");
        scene_roots.push_back(fs::path(project_dir) / "assets" / "resources");
        scene_roots.push_back(fs::path(project_dir) / "assets" / "models");
    }
    for (const auto& lib_path : m_scene.imported_library_paths) {
        if (!lib_path.empty()) {
            fs::path lp(lib_path);
            scene_roots.push_back(lp.parent_path());
            scene_roots.push_back(lp.parent_path() / "models");
            scene_roots.push_back(lp.parent_path() / "resources");
            scene_roots.push_back(lp.parent_path().parent_path());
            scene_roots.push_back(lp.parent_path().parent_path() / "resources");
            scene_roots.push_back(lp.parent_path().parent_path() / "models");
        }
    }

    // 1A. Unique models
    std::unordered_map<std::string, std::string> unique_models;
    for (const auto& obj : m_scene.objects) {
        if (!obj.mesh_name.empty() && !unique_models.count(obj.mesh_name)) {
            unique_models.emplace(obj.mesh_name, obj.template_name);
        }
    }

    struct ModelTask {
        std::string mesh_name;
        std::string template_name;
        fs::path pod_path;
    };
    std::vector<ModelTask> model_tasks;
    for (const auto& [mesh_name, template_name] : unique_models) {
        fs::path pod_path = av::assets::resolve_pod(fs::path(scene_path), mesh_name, scene_roots);
        if (!pod_path.empty()) {
            model_tasks.push_back({mesh_name, template_name, pod_path});
        }
    }

    std::vector<std::future<std::pair<std::string, av::PODModel>>> model_futures;
    for (const auto& task : model_tasks) {
        model_futures.push_back(std::async(std::launch::async, [task]() {
            av::PODModel m = load_pod_to_ram(task.pod_path.string(), task.template_name);
            return std::make_pair(task.mesh_name, std::move(m));
        }));
    }

    // 1B. Missing normals on ground meshes in parallel threads
    const size_t num_obj = m_scene.objects.size();
    const unsigned int hw_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::future<void>> normal_futures;
    size_t chunk_size = (num_obj + hw_threads - 1) / hw_threads;
    for (unsigned int t = 0; t < hw_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(start + chunk_size, num_obj);
        if (start < end) {
            normal_futures.push_back(std::async(std::launch::async, [this, start, end]() {
                for (size_t oi = start; oi < end; ++oi) {
                    for (auto& gmesh : m_scene.objects[oi].ground_meshes) {
                        compute_missing_normals(gmesh);
                    }
                }
            }));
        }
    }

    // 1C. Collect and pre-decode unique ground mesh textures and background textures in parallel
    std::unordered_set<std::string> unique_ground_names;
    for (const auto& obj : m_scene.objects) {
        for (const auto& name : obj.ground_mesh_textures) {
            if (!name.empty()) unique_ground_names.insert(name);
        }
    }

    std::unordered_map<std::string, std::string> resolved_ground_paths;
    std::unordered_set<std::string> textures_to_decode;
    for (const auto& name : unique_ground_names) {
        for (const auto& cand : av::assets::texture_candidates(fs::path(scene_path), name, scene_roots)) {
            std::error_code ec;
            if (fs::is_regular_file(cand, ec)) {
                resolved_ground_paths[name] = cand.string();
                textures_to_decode.insert(cand.string());
                break;
            }
        }
    }

    const QString home = QDir::homePath();
    std::vector<fs::path> roots = {
        fs::path(scene_path).parent_path(),
        fs::path(scene_path).parent_path() / "resources",
        fs::path(scene_path).parent_path().parent_path(),
        fs::path(scene_path).parent_path().parent_path() / "resources",
        fs::path(home.toStdString()) / "resources",
        fs::path(home.toStdString()) / "SwordigoRefresh" / "assets" / "resources",
        fs::path(home.toStdString()) / "SwordigoDesktop" / "assets",
        fs::path(home.toStdString()) / "SwordigoDesktop" / "resources",
        fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets",
        fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "resources",
        fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "background",
        fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "resources" / "background",
    };
    for (const auto& r : scene_roots) roots.push_back(r);
    static const char* suffixes[] = {
        "_2x.tex.png", ".tex.png", "_2x.pvr", ".pvr", "_2x.tex", ".tex", "_2x.png", ".png", ""
    };

    std::unordered_set<std::string> unique_bg_names;
    for (const auto& object : m_scene.objects) {
        if (!object.background_name.empty()) unique_bg_names.insert(object.background_name);
    }

    std::unordered_map<std::string, std::string> resolved_bg_paths;
    for (const auto& bg_name : unique_bg_names) {
        std::vector<std::string> name_variants = {bg_name};
        std::string stripped = strip_image_extensions(bg_name);
        if (!stripped.empty() && stripped != bg_name) {
            name_variants.push_back(stripped);
        }
        if (stripped.size() > 3 && stripped.rfind("_2x") == stripped.size() - 3) {
            name_variants.push_back(stripped.substr(0, stripped.size() - 3));
        } else {
            name_variants.push_back(stripped + "_2x");
        }
        for (const auto& root : roots) {
            for (const auto& name_var : name_variants) {
                for (const char* suffix : suffixes) {
                    const fs::path candidate = root / (name_var + suffix);
                    std::error_code ec;
                    if (fs::is_regular_file(candidate, ec)) {
                        resolved_bg_paths[bg_name] = candidate.string();
                        textures_to_decode.insert(candidate.string());
                        break;
                    }
                }
                if (resolved_bg_paths.count(bg_name)) break;
            }
            if (resolved_bg_paths.count(bg_name)) break;
        }
    }

    std::vector<std::future<void>> tex_futures;
    for (const auto& tex_path : textures_to_decode) {
        tex_futures.push_back(std::async(std::launch::async, [tex_path]() {
            decode_image_file_to_ram(tex_path);
        }));
    }

    for (auto& f : model_futures) {
        auto pair = f.get();
        if (!pair.second.meshes.empty()) {
            m_scene_models.emplace(pair.first, std::move(pair.second));
        }
    }

    std::vector<std::future<void>> model_tex_futures;
    for (const auto& [mesh_name, model] : m_scene_models) {
        const fs::path pod_path = av::assets::resolve_pod(fs::path(scene_path), mesh_name, scene_roots);
        for (const auto& tex_name : model.texture_filenames) {
            for (const auto& cand : av::assets::texture_candidates(pod_path, tex_name, scene_roots)) {
                std::error_code ec;
                if (fs::is_regular_file(cand, ec)) {
                    model_tex_futures.push_back(std::async(std::launch::async, [cand]() {
                        decode_image_file_to_ram(cand.string());
                    }));
                    break;
                }
            }
        }
    }

    for (auto& f : normal_futures) f.get();
    for (auto& f : tex_futures) f.get();
    for (auto& f : model_tex_futures) f.get();

    // ── STEP 2: Main thread GL upload (VBO/EBO) ──
    makeCurrent();
    clear_mesh_gpu();

    for (auto& [mesh_name, model] : m_scene_models) {
        // VBO/EBO upload, cached per pod path (see m_pod_gpu_cache) — re-loading
        // a scene reuses these buffers instead of recompiling every mesh.
        const fs::path pod_path = av::assets::resolve_pod(fs::path(scene_path), mesh_name, scene_roots);
        std::vector<MeshGpu> gpus = upload_pod_gpu(pod_path.string(), model);
        for (const auto& g : gpus)
            if (g.valid()) m_all_mesh_gpu.push_back(g);
        m_scene_model_gpu.emplace(mesh_name, std::move(gpus));
        m_scene_model_textures.emplace(mesh_name, load_pod_textures(pod_path, model, scene_roots));
    }

    std::unordered_map<std::string, GLuint> ground_tex_cache;
    m_scene_ground_textures.resize(m_scene.objects.size());
    for (size_t object_index = 0; object_index < m_scene.objects.size(); ++object_index) {
        auto& object = m_scene.objects[object_index];
        auto& textures = m_scene_ground_textures[object_index];
        textures.resize(object.ground_meshes.size(), 0);
        for (size_t mesh_index = 0; mesh_index < object.ground_meshes.size(); ++mesh_index) {
            const std::string name = mesh_index < object.ground_mesh_textures.size() ? object.ground_mesh_textures[mesh_index] : std::string();
            if (name.empty()) continue;
            auto it = ground_tex_cache.find(name);
            if (it != ground_tex_cache.end()) {
                textures[mesh_index] = it->second;
                continue;
            }
            GLuint tex = 0;
            auto it_path = resolved_ground_paths.find(name);
            if (it_path != resolved_ground_paths.end()) {
                tex = load_texture_any(it_path->second);
            }
            ground_tex_cache[name] = tex;
            textures[mesh_index] = tex;
        }
    }

    m_render_objects.clear();
    m_render_objects.reserve(m_scene.objects.size());
    for (size_t object_index = 0; object_index < m_scene.objects.size(); ++object_index) {
        const auto& object = m_scene.objects[object_index];
        SceneRenderObject ro;
        ro.pos[0] = object.pos_x; ro.pos[1] = object.pos_y; ro.pos[2] = object.pos_z;
        swk::object_world_matrix(object, ro.world_matrix);
        swk::object_render_matrix(object, ro.render_matrix);
        ro.hidden = object.hidden;
        ro.is_portal = object.is_portal;
        ro.is_spawn_point = object.is_spawn_point;
        ro.is_camera = object.is_camera;
        ro.is_dimension_object = object.is_dimension_object;
        ro.object_index = static_cast<int>(object_index);
        ro.name = object.name;
        ro.mesh_name = object.mesh_name;
        ro.local_aabb = object.local_aabb;
        ro.has_diffuse_color = object.has_model_diffuse_color;
        ro.diffuse_color[0] = object.model_diffuse_color[0];
        ro.diffuse_color[1] = object.model_diffuse_color[1];
        ro.diffuse_color[2] = object.model_diffuse_color[2];

        ro.ground_textures = m_scene_ground_textures[object_index];
        ro.ground_gpu.reserve(object.ground_meshes.size());
        for (const auto& gmesh : object.ground_meshes) {
            MeshGpu g = upload_mesh_gpu(gmesh);   // scene-embedded; no pod cache
            if (g.valid()) {
                ro.ground_gpu.push_back(g);
                m_all_mesh_gpu.push_back(g);
            } else {
                ro.ground_gpu.push_back(MeshGpu{});
            }
        }
        m_render_objects.push_back(std::move(ro));
    }

    for (const auto& bg_name : unique_bg_names) {
        if (m_scene_background_textures.count(bg_name)) continue;
        GLuint texture = 0;
        auto it = resolved_bg_paths.find(bg_name);
        if (it != resolved_bg_paths.end()) {
            texture = load_texture_any(it->second);
        }
        m_scene_background_textures.emplace(bg_name, texture);
    }
    doneCurrent();

    const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0], dy = m_scene.bounds_max[1] - m_scene.bounds_min[1], dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
    m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);
    // Reloading the same scene (e.g. after a save) keeps the user's camera;
    // only frame the scene on genuine first loads / scene switches.
    if (!(prev_had_scene && prev_scene_path == scene_path)) {
        m_cam_target[0] = (m_scene.bounds_min[0] + m_scene.bounds_max[0]) * .5f; m_cam_target[1] = 0.0f; m_cam_target[2] = (m_scene.bounds_min[2] + m_scene.bounds_max[2]) * .5f;
        m_cam_dist = std::max(120.0f, m_scene_extent * 0.75f);
    }
    adopt_session_state(scene_path);

    auto cached = std::make_shared<SceneSession>();
    cached->scene = m_scene;
    cached->scene_models = m_scene_models;
    cached->scene_model_textures = m_scene_model_textures;
    cached->scene_ground_textures = m_scene_ground_textures;
    cached->scene_background_textures = m_scene_background_textures;
    cached->render_objects = m_render_objects;
    cached->model_mesh_gpu = m_model_mesh_gpu;
    cached->scene_model_gpu = m_scene_model_gpu;
    cached->all_mesh_gpu = m_all_mesh_gpu;
    cached->scene_extent = m_scene_extent;
    m_scene_cache[scene_path] = cached;

    emit sceneLoaded(m_scene);
    update();
    return true;
}

// True when `next` can reuse `cur`'s GPU state: same objects in the same order
// with the same models, templates, backgrounds, ground-mesh layout and ground
// mesh raw geometry. Only then is an in-place structured swap safe (transforms
// and components may still differ — those are re-applied to the render state).
static bool scene_structure_matches(const av::SceneData& cur, const av::SceneData& next) {
    if (cur.objects.size() != next.objects.size()) return false;
    for (size_t i = 0; i < cur.objects.size(); ++i) {
        const av::SceneObject& a = cur.objects[i];
        const av::SceneObject& b = next.objects[i];
        if (a.mesh_name != b.mesh_name) return false;
        if (a.template_name != b.template_name) return false;
        if (a.background_name != b.background_name) return false;
        if (a.ground_meshes.size() != b.ground_meshes.size()) return false;
        if (a.ground_mesh_raw.size() != b.ground_mesh_raw.size()) return false;
        for (size_t g = 0; g < a.ground_mesh_raw.size(); ++g) {
            if (a.ground_mesh_raw[g] != b.ground_mesh_raw[g]) return false;
        }
        if (a.ground_mesh_textures != b.ground_mesh_textures) return false;
    }
    return true;
}

bool Viewport3DWidget::apply_scene_data(const av::SceneData& scene,
                                        const std::string& scene_path) {
    // A scene swap while Mesh Edit is armed would leave the projection-locked
    // camera pointing at a polygon that no longer exists ("everything
    // disappears"). End the session before touching the scene; the caller that
    // wants to keep the work (save) has already committed via set_mesh_edit(false).
    if (m_mesh_edit) end_mesh_edit(false);
    if (!m_has_scene || !m_scene_ready || m_current_scene_path != scene_path) {
        return false;   // not showing this scene (or still loading) — caller falls back
    }
    const bool perf = std::getenv("RUBY_GG_PERF") != nullptr;
    auto pt0 = std::chrono::steady_clock::now();
    auto perf_ms = [&](const char* tag) {
        if (!perf) return;
        const double d = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pt0).count();
        std::fprintf(stderr, "[perf apply] %s: %.1f ms\n", tag, d);
        pt0 = std::chrono::steady_clock::now();
    };
    if (!scene_structure_matches(m_scene, scene)) return false;
    perf_ms("structure_matches");

    // In-place structured swap: transforms/components may differ but every GPU
    // asset (model display lists, model/ground/background textures) is still
    // valid, so nothing needs rebuilding — and the camera is never touched.
    const float keep_pitch = m_cam_pitch, keep_yaw = m_cam_yaw, keep_dist = m_cam_dist;
    const float keep_target[3] = {m_cam_target[0], m_cam_target[1], m_cam_target[2]};

    m_scene = scene;
    m_scene.filepath = scene_path;
    m_scene.filename = fs::path(scene_path).filename().string();
    perf_ms("scene assign");

    // Patch the render objects from the new transforms/visibility.
    for (auto& ro : m_render_objects) {
        const int idx = ro.object_index;
        if (idx < 0 || idx >= (int)m_scene.objects.size()) continue;
        const av::SceneObject& o = m_scene.objects[idx];
        ro.pos[0] = o.pos_x; ro.pos[1] = o.pos_y; ro.pos[2] = o.pos_z;
        swk::object_world_matrix(o, ro.world_matrix);
        swk::object_render_matrix(o, ro.render_matrix);
        ro.hidden = o.hidden;
        ro.is_portal = o.is_portal;
        ro.is_dimension_object = o.is_dimension_object;
    }
    perf_ms("render objects");

    const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0];
    const float dy = m_scene.bounds_max[1] - m_scene.bounds_min[1];
    const float dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
    m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);

    // RAM caches must agree with the new structured state so tab switches and
    // later gizmo edits start from it.
    auto wit = m_scene_cache.find(scene_path);
    if (wit != m_scene_cache.end() && wit->second) {
        wit->second->scene = m_scene;
        wit->second->render_objects = m_render_objects;
        wit->second->scene_extent = m_scene_extent;
    }
    perf_ms("cache write");
    // (No global cache write: the session above IS this scene's state. Scenes
    // are isolated per document — nothing lands in a shared store.)

    // Restore the camera — the whole point of the in-place path.
    m_cam_pitch = keep_pitch; m_cam_yaw = keep_yaw; m_cam_dist = keep_dist;
    m_cam_target[0] = keep_target[0]; m_cam_target[1] = keep_target[1]; m_cam_target[2] = keep_target[2];

    emit sceneLoaded(m_scene);
    perf_ms("emit sceneLoaded");
    update();
    perf_ms("update");
    return true;
}

void Viewport3DWidget::load_scene_async(const std::string& scene_path,
                                        const std::string* in_memory_binary) {
    // When a re-encoded binary is supplied the caller's bytes win over any
    // cache or same-path shortcut (they represent unsaved FileRift edits).
    const bool from_bytes = in_memory_binary != nullptr;
    if (scene_path.empty()) return;
    if (m_mesh_edit) end_mesh_edit(false);   // switching scenes aborts the session
    if (!from_bytes && m_has_scene && m_current_scene_path == scene_path) {
        update();
        return;
    }

    // Reloading the SAME path (save round-trip) must keep the user's camera.
    const std::string prev_scene_path = m_current_scene_path;
    const bool prev_had_scene = m_has_scene;

    stash_current_session();

    // ── Instant Tab Restore: If this scene is already cached in RAM, restore in 0 ms ──
    if (!from_bytes) {
        auto cache_it = m_scene_cache.find(scene_path);
        if (cache_it != m_scene_cache.end() && cache_it->second) {
            auto cached = cache_it->second;
            m_current_scene_path = scene_path;
            m_scene = cached->scene;
            m_scene_models = cached->scene_models;
            m_scene_model_textures = cached->scene_model_textures;
            m_scene_ground_textures = cached->scene_ground_textures;
            m_scene_background_textures = cached->scene_background_textures;
            m_render_objects = cached->render_objects;
            m_model_mesh_gpu = cached->model_mesh_gpu;
            m_scene_model_gpu = cached->scene_model_gpu;
            m_all_mesh_gpu = cached->all_mesh_gpu;
            m_scene_extent = cached->scene_extent;

            m_has_scene = true;
            m_scene_ready = true;
            m_has_model = false;
            m_has_texture = false;
            m_has_import = false;
            adopt_session_state(scene_path);

            if (m_loading_overlay) {
                m_loading_overlay->hide_loading();
            }
            emit sceneLoaded(m_scene);
            update();
            return;
        }
    }

    m_current_scene_path = scene_path;
    // Per-scene load token: (path, seq). A load can only be cancelled by a
    // NEWER load OF THE SAME SCENE — activating another document no longer
    // drops this scene's completion (the old single global seq did).
    uint64_t seq = ++m_scene_load_seq;
    m_scene_load_token = scene_path;
    m_scene_ready = false;   // NOT interactive until this load lands
    // Drop any stale pending camera: this async load owns the frame context.
    // activate_document sets camera_state AFTER scheduling the load, so a fresh
    // pending camera (if any) is recorded again below and honored on completion.
    m_has_pending_cam = false;

    std::string fname = fs::path(scene_path).filename().string();
    if (m_loading_overlay) {
        m_loading_overlay->show_loading("Loading Scene", QString::fromStdString(fname));
    }

    const std::string bytes_copy = from_bytes ? *in_memory_binary : std::string();
    std::thread([this, scene_path, seq, prev_scene_path, prev_had_scene, bytes_copy]() {
        av::SceneData loaded;
        if (!bytes_copy.empty()) {
            // Parse a re-encoded binary supplied by the caller (unsaved FileRift
            // edits) in memory — no temp file, no disk touch. The real path
            // stays the scene's identity for asset resolution + saves.
            std::vector<std::string> extra_roots;
            const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
            if (!pdir.empty()) extra_roots.push_back(pdir);
            std::vector<uint8_t> raw(bytes_copy.begin(), bytes_copy.end());
            std::string parse_err;
            loaded = parse_scene_bytes(raw, scene_path, extra_roots, parse_err);
            if (!loaded.objects.empty())
                std::cerr << "[viewport] in-memory re-encode loaded: " << scene_path << "\n";
        } else {
            loaded = parse_scene_from_disk(scene_path);
        }
        if (loaded.objects.empty()) {
            QMetaObject::invokeMethod(this, [this, seq, scene_path]() {
                if (seq != m_scene_load_seq || m_scene_load_token != scene_path) return;
                if (m_loading_overlay) m_loading_overlay->hide_loading();
                m_has_scene = false;
                m_scene_ready = false;
                m_current_scene_path.clear();
                m_has_pending_cam = false;   // never carry a camera across loads
                emit sceneLoadingFailed("Failed to parse scene file");
            }, Qt::QueuedConnection);
            return;
        }

        std::vector<fs::path> scene_roots;
        const std::string project_dir = ruby::core::ProjectContext::instance().project_dir();
        if (!project_dir.empty()) {
            scene_roots.push_back(fs::path(project_dir));
            scene_roots.push_back(fs::path(project_dir) / "resources");
            scene_roots.push_back(fs::path(project_dir) / "models");
            scene_roots.push_back(fs::path(project_dir) / "assets");
            scene_roots.push_back(fs::path(project_dir) / "assets" / "resources");
            scene_roots.push_back(fs::path(project_dir) / "assets" / "models");
        }
        for (const auto& lib_path : loaded.imported_library_paths) {
            if (!lib_path.empty()) {
                fs::path lp(lib_path);
                scene_roots.push_back(lp.parent_path());
                scene_roots.push_back(lp.parent_path() / "models");
                scene_roots.push_back(lp.parent_path() / "resources");
                scene_roots.push_back(lp.parent_path().parent_path());
                scene_roots.push_back(lp.parent_path().parent_path() / "resources");
                scene_roots.push_back(lp.parent_path().parent_path() / "models");
            }
        }

        std::unordered_map<std::string, std::string> unique_models;
        for (const auto& obj : loaded.objects) {
            if (!obj.mesh_name.empty() && !unique_models.count(obj.mesh_name)) {
                unique_models.emplace(obj.mesh_name, obj.template_name);
            }
        }

        struct ModelTask {
            std::string mesh_name;
            std::string template_name;
            fs::path pod_path;
        };
        std::vector<ModelTask> model_tasks;
        for (const auto& [mesh_name, template_name] : unique_models) {
            fs::path pod_path = av::assets::resolve_pod(fs::path(scene_path), mesh_name, scene_roots);
            if (!pod_path.empty()) {
                model_tasks.push_back({mesh_name, template_name, pod_path});
            }
        }

        std::vector<std::future<std::pair<std::string, av::PODModel>>> model_futures;
        for (const auto& task : model_tasks) {
            model_futures.push_back(std::async(std::launch::async, [task]() {
                av::PODModel m = load_pod_to_ram(task.pod_path.string(), task.template_name);
                return std::make_pair(task.mesh_name, std::move(m));
            }));
        }

        const size_t num_obj = loaded.objects.size();
        const unsigned int hw_threads = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::future<void>> normal_futures;
        size_t chunk_size = (num_obj + hw_threads - 1) / hw_threads;
        for (unsigned int t = 0; t < hw_threads; ++t) {
            size_t start = t * chunk_size;
            size_t end = std::min(start + chunk_size, num_obj);
            if (start < end) {
                normal_futures.push_back(std::async(std::launch::async, [this, &loaded, start, end]() {
                    for (size_t oi = start; oi < end; ++oi) {
                        for (auto& gmesh : loaded.objects[oi].ground_meshes) {
                            compute_missing_normals(gmesh);
                        }
                    }
                }));
            }
        }

        // 1C. Collect and pre-decode unique ground mesh textures and background textures in parallel
        std::unordered_set<std::string> unique_ground_names;
        for (const auto& obj : loaded.objects) {
            for (const auto& name : obj.ground_mesh_textures) {
                if (!name.empty()) unique_ground_names.insert(name);
            }
        }

        std::unordered_map<std::string, std::string> resolved_ground_paths;
        std::unordered_set<std::string> textures_to_decode;
        for (const auto& name : unique_ground_names) {
            for (const auto& cand : av::assets::texture_candidates(fs::path(scene_path), name, scene_roots)) {
                std::error_code ec;
                if (fs::is_regular_file(cand, ec)) {
                    resolved_ground_paths[name] = cand.string();
                    textures_to_decode.insert(cand.string());
                    break;
                }
            }
        }

        const QString home = QDir::homePath();
        std::vector<fs::path> roots = {
            fs::path(scene_path).parent_path(),
            fs::path(scene_path).parent_path() / "resources",
            fs::path(scene_path).parent_path().parent_path(),
            fs::path(scene_path).parent_path().parent_path() / "resources",
            fs::path(home.toStdString()) / "resources",
            fs::path(home.toStdString()) / "SwordigoRefresh" / "assets" / "resources",
            fs::path(home.toStdString()) / "SwordigoDesktop" / "assets",
            fs::path(home.toStdString()) / "SwordigoDesktop" / "resources",
            fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets",
            fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "resources",
            fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "background",
            fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "resources" / "background",
        };
        for (const auto& r : scene_roots) roots.push_back(r);
        static const char* suffixes[] = {
            "_2x.tex.png", ".tex.png", "_2x.pvr", ".pvr", "_2x.tex", ".tex", "_2x.png", ".png", ""
        };

        std::unordered_set<std::string> unique_bg_names;
        for (const auto& object : loaded.objects) {
            if (!object.background_name.empty()) unique_bg_names.insert(object.background_name);
        }

        std::unordered_map<std::string, std::string> resolved_bg_paths;
        for (const auto& bg_name : unique_bg_names) {
            std::vector<std::string> name_variants = {bg_name};
            std::string stripped = strip_image_extensions(bg_name);
            if (!stripped.empty() && stripped != bg_name) {
                name_variants.push_back(stripped);
            }
            if (stripped.size() > 3 && stripped.rfind("_2x") == stripped.size() - 3) {
                name_variants.push_back(stripped.substr(0, stripped.size() - 3));
            } else {
                name_variants.push_back(stripped + "_2x");
            }
            for (const auto& root : roots) {
                for (const auto& name_var : name_variants) {
                    for (const char* suffix : suffixes) {
                        const fs::path candidate = root / (name_var + suffix);
                        std::error_code ec;
                        if (fs::is_regular_file(candidate, ec)) {
                            resolved_bg_paths[bg_name] = candidate.string();
                            textures_to_decode.insert(candidate.string());
                            break;
                        }
                    }
                    if (resolved_bg_paths.count(bg_name)) break;
                }
                if (resolved_bg_paths.count(bg_name)) break;
            }
        }

        std::vector<std::future<void>> tex_futures;
        for (const auto& tex_path : textures_to_decode) {
            tex_futures.push_back(std::async(std::launch::async, [tex_path]() {
                decode_image_file_to_ram(tex_path);
            }));
        }

        std::map<std::string, av::PODModel> pre_models;
        for (auto& f : model_futures) {
            auto pair = f.get();
            if (!pair.second.meshes.empty()) {
                pre_models.emplace(pair.first, std::move(pair.second));
            }
        }

        std::vector<std::future<void>> model_tex_futures;
        for (const auto& [mesh_name, model] : pre_models) {
            const fs::path pod_path = av::assets::resolve_pod(fs::path(scene_path), mesh_name, scene_roots);
            for (const auto& tex_name : model.texture_filenames) {
                for (const auto& cand : av::assets::texture_candidates(pod_path, tex_name, scene_roots)) {
                    std::error_code ec;
                    if (fs::is_regular_file(cand, ec)) {
                        model_tex_futures.push_back(std::async(std::launch::async, [cand]() {
                            decode_image_file_to_ram(cand.string());
                        }));
                        break;
                    }
                }
            }
        }

        for (auto& f : normal_futures) f.get();
        for (auto& f : tex_futures) f.get();
        for (auto& f : model_tex_futures) f.get();

        QMetaObject::invokeMethod(this, [this, seq, scene_path, roots, scene_roots,
                                         prev_scene_path, prev_had_scene,
                                         resolved_ground_paths = std::move(resolved_ground_paths),
                                         resolved_bg_paths = std::move(resolved_bg_paths),
                                         unique_bg_names = std::move(unique_bg_names),
                                         loaded = std::move(loaded),
                                         pre_models = std::move(pre_models)]() mutable {
            // Land only if this is still the newest load for THIS scene.
            if (seq != m_scene_load_seq || m_scene_load_token != scene_path) return;

            m_scene = std::move(loaded);
            m_scene_models = std::move(pre_models);
            m_has_scene = true; m_scene_ready = true;
            m_has_model = false; m_has_texture = false; m_has_import = false;
            m_scene_model_textures.clear(); m_scene_ground_textures.clear(); m_scene_background_textures.clear();
            m_scene_model_gpu.clear();

            // This completion can land before the first paint — initialize the
            // GL function table here too (see ensure_gl_ready), and refuse the
            // GPU section when no surface can be created at all.
            if (!ensure_gl_ready()) return;
            makeCurrent();
            clear_mesh_gpu();

            for (auto& [mesh_name, model] : m_scene_models) {
                // VBO/EBO upload, cached per pod path — reusing buffers across
                // scene loads / save reloads (m_pod_gpu_cache).
                const fs::path pod_path = av::assets::resolve_pod(fs::path(scene_path), mesh_name, scene_roots);
                std::vector<MeshGpu> gpus = upload_pod_gpu(pod_path.string(), model);
                for (const auto& g : gpus)
                    if (g.valid()) m_all_mesh_gpu.push_back(g);
                m_scene_model_gpu.emplace(mesh_name, std::move(gpus));
                m_scene_model_textures.emplace(mesh_name, load_pod_textures(pod_path, model, scene_roots));
            }

            std::unordered_map<std::string, GLuint> ground_tex_cache;
            m_scene_ground_textures.resize(m_scene.objects.size());
            for (size_t object_index = 0; object_index < m_scene.objects.size(); ++object_index) {
                auto& object = m_scene.objects[object_index];
                auto& textures = m_scene_ground_textures[object_index];
                textures.resize(object.ground_meshes.size(), 0);
                for (size_t mesh_index = 0; mesh_index < object.ground_meshes.size(); ++mesh_index) {
                    const std::string name = mesh_index < object.ground_mesh_textures.size() ? object.ground_mesh_textures[mesh_index] : std::string();
                    if (name.empty()) continue;
                    auto it = ground_tex_cache.find(name);
                    if (it != ground_tex_cache.end()) {
                        textures[mesh_index] = it->second;
                        continue;
                    }
                    GLuint tex = 0;
                    auto it_path = resolved_ground_paths.find(name);
                    if (it_path != resolved_ground_paths.end()) {
                        tex = load_texture_any(it_path->second);
                    }
                    ground_tex_cache[name] = tex;
                    textures[mesh_index] = tex;
                }
            }

            m_render_objects.clear();
            m_render_objects.reserve(m_scene.objects.size());
            for (size_t object_index = 0; object_index < m_scene.objects.size(); ++object_index) {
                const auto& object = m_scene.objects[object_index];
                SceneRenderObject ro;
                ro.pos[0] = object.pos_x; ro.pos[1] = object.pos_y; ro.pos[2] = object.pos_z;
                swk::object_world_matrix(object, ro.world_matrix);
                swk::object_render_matrix(object, ro.render_matrix);
                ro.hidden = object.hidden;
                ro.is_portal = object.is_portal;
                ro.is_spawn_point = object.is_spawn_point;
                ro.is_camera = object.is_camera;
                ro.is_dimension_object = object.is_dimension_object;
                ro.object_index = static_cast<int>(object_index);
                ro.name = object.name;
                ro.mesh_name = object.mesh_name;
                ro.local_aabb = object.local_aabb;
                ro.has_diffuse_color = object.has_model_diffuse_color;
                ro.diffuse_color[0] = object.model_diffuse_color[0];
                ro.diffuse_color[1] = object.model_diffuse_color[1];
                ro.diffuse_color[2] = object.model_diffuse_color[2];

                ro.ground_textures = m_scene_ground_textures[object_index];
                ro.ground_gpu.reserve(object.ground_meshes.size());
                for (const auto& gmesh : object.ground_meshes) {
                    MeshGpu g = upload_mesh_gpu(gmesh);   // scene-embedded; no pod cache
                    if (g.valid()) {
                        ro.ground_gpu.push_back(g);
                        m_all_mesh_gpu.push_back(g);
                    } else {
                        ro.ground_gpu.push_back(MeshGpu{});
                    }
                }
                m_render_objects.push_back(std::move(ro));
            }

            for (const auto& bg_name : unique_bg_names) {
                if (m_scene_background_textures.count(bg_name)) continue;
                GLuint texture = 0;
                auto it = resolved_bg_paths.find(bg_name);
                if (it != resolved_bg_paths.end()) {
                    texture = load_texture_any(it->second);
                }
                m_scene_background_textures.emplace(bg_name, texture);
            }
            doneCurrent();

            const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0], dy = m_scene.bounds_max[1] - m_scene.bounds_min[1], dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
            m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);
            // Reloading the same scene (e.g. after a save) keeps the user's
            // camera; only frame the scene on genuine first loads / switches.
            // An explicit per-document camera (set_camera_state for this load)
            // always wins over auto-framing AND the stashed session camera.
            if (!m_has_pending_cam && !(prev_had_scene && prev_scene_path == scene_path)) {
                m_cam_target[0] = (m_scene.bounds_min[0] + m_scene.bounds_max[0]) * .5f; m_cam_target[1] = 0.0f; m_cam_target[2] = (m_scene.bounds_min[2] + m_scene.bounds_max[2]) * .5f;
                m_cam_dist = std::max(120.0f, m_scene_extent * 0.75f);
            }

            // ── Save to persistent RAM Scene cache so tab switching is 0ms ──
            if (!(prev_had_scene && prev_scene_path == scene_path)) {
                // (camera framing already applied above)
            }
            adopt_session_state(scene_path);
            // The caller's explicit camera (restored while this load was in
            // flight) must not be lost to framing/session adoption above.
            if (m_has_pending_cam) {
                m_cam_pitch = m_pending_cam.pitch;
                m_cam_yaw = m_pending_cam.yaw;
                m_cam_dist = m_pending_cam.dist;
                m_cam_target[0] = m_pending_cam.target[0];
                m_cam_target[1] = m_pending_cam.target[1];
                m_cam_target[2] = m_pending_cam.target[2];
                m_has_pending_cam = false;
            }
            auto cached = std::make_shared<SceneSession>();
            cached->scene = m_scene;
            cached->scene_models = m_scene_models;
            cached->scene_model_textures = m_scene_model_textures;
            cached->scene_ground_textures = m_scene_ground_textures;
            cached->scene_background_textures = m_scene_background_textures;
            cached->render_objects = m_render_objects;
            cached->model_mesh_gpu = m_model_mesh_gpu;
            cached->scene_model_gpu = m_scene_model_gpu;
            cached->all_mesh_gpu = m_all_mesh_gpu;
            cached->scene_extent = m_scene_extent;
            m_scene_cache[scene_path] = cached;

            if (m_loading_overlay) {
                m_loading_overlay->hide_loading();
            }
            emit sceneLoaded(m_scene);
            update();
        }, Qt::QueuedConnection);
    }).detach();
}

bool Viewport3DWidget::load_texture_preview(const std::string& image_path) {
    if (image_path.empty()) return false;
    if (!ensure_gl_ready()) return false;
    // Texture preview owns the viewport: cancel any in-flight async scene load
    // (same clobber guard as load_model).
    ++m_scene_load_seq;
    m_scene_load_token.clear();
    if (m_loading_overlay) m_loading_overlay->hide_loading();
    makeCurrent();
    m_material_textures.clear();
    for (GLuint tex : m_import_textures) if (tex) glDeleteTextures(1, &tex);
    m_import_textures.clear();
    m_has_import = false;
    m_texture_tex = load_texture_any(image_path);
    if (!m_texture_tex) { doneCurrent(); return false; }

    // Determine the image aspect for a non-stretched poster.
    int img_w = 1, img_h = 1;
    const fs::path file(image_path);
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".pvr" || ext == ".tex") {
        QFile input(image_path.c_str());
        if (input.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = input.readAll();
            std::vector<uint8_t> rgba; int w = 0, h = 0;
            if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()),
                                   static_cast<size_t>(bytes.size()), rgba, w, h) && w > 0 && h > 0) {
                img_w = w; img_h = h;
            }
        }
    } else {
        QImage probe(image_path.c_str());
        if (!probe.isNull()) { img_w = probe.width(); img_h = probe.height(); }
    }
    m_texture_aspect = img_h > 0 ? float(img_w) / float(img_h) : 1.0f;
    doneCurrent();

    m_has_texture = true;
    m_has_scene = false;
    m_scene_ready = false;
    m_has_model = false;
    m_scene_extent = 800.0f;
    m_cam_target[0] = 0.0f; m_cam_target[1] = 0.0f; m_cam_target[2] = 0.0f;
    m_cam_dist = 700.0f;
    m_cam_yaw = -35.0f;
    m_cam_pitch = 18.0f;
    update();
    return true;
}

void Viewport3DWidget::set_wireframe(bool enabled) { m_wireframe = enabled; update(); }
void Viewport3DWidget::set_grid_visible(bool visible) { m_show_grid = visible; update(); }

void Viewport3DWidget::reset_camera() {
    m_cam_pitch = 15.0f;
    m_cam_yaw = -45.0f;
    m_cam_dist = 120.0f;
    m_cam_target[0] = 0.0f;
    m_cam_target[1] = 20.0f;
    m_cam_target[2] = 0.0f;
    update();
}

void Viewport3DWidget::set_frame(float frame) {
    m_frame = std::max(0.0f, frame);
    if (m_has_glb) {
        m_glb_model.update_animation(m_frame / 30.0f);
    }
    update();
}
void Viewport3DWidget::set_playing(bool) {}
void Viewport3DWidget::set_selected_object(int index) {
    // Picking another object mid-session commits the mesh edit first (the
    // polygon stays on the object; the new object becomes the edit target on
    // the next Mesh toggle).
    if (m_mesh_edit && index != m_mesh_edit_object) end_mesh_edit(true);
    if (m_selected_scene_object != index && index >= 0) {
        m_select_anim_start = std::chrono::steady_clock::now();
        m_select_anim_obj = index;
    }
    m_selected_scene_object = index;
    update();
}
int Viewport3DWidget::frame_count() const {
    if (m_has_glb) {
        float dur = m_glb_model.current_animation_duration();
        return std::max(1, static_cast<int>(std::round(dur * 30.0f)) + 1);
    }
    return std::max(1, m_model.num_frames);
}

void Viewport3DWidget::set_animation_clip(int clip_index) {
    if (m_has_glb) {
        m_glb_model.set_active_animation(clip_index);
        m_frame = 0.0f;
        m_glb_model.update_animation(0.0f);
        update();
    }
}

QStringList Viewport3DWidget::animation_clips() const {
    QStringList list;
    if (m_has_glb) {
        const auto& anims = m_glb_model.animations();
        for (const auto& a : anims) {
            list.append(QString::fromStdString(a.name.empty() ? "Clip" : a.name));
        }
    }
    return list;
}

int Viewport3DWidget::active_animation_clip() const {
    if (m_has_glb) {
        return m_glb_model.active_animation();
    }
    return 0;
}

// ── Camera dynamics (asset_viewer.cpp-grade feel, no ImGui) ────────────────
// Orbit/pan carry release velocity with exponential damping; zoom is
// exponential (pow(0.94, wheel)) and converges on the selected object (or the
// spawn point when nothing is selected) — exactly the policies that make the
// ImGui edition's camera feel right. dt in seconds.
void Viewport3DWidget::update_camera_dynamics(float dt) {
    if (dt <= 0.0f) return;
    const float damp = std::exp(-8.0f * dt);   // glide decay per second

    if (std::abs(m_orbit_vel_yaw) > 0.01f || std::abs(m_orbit_vel_pitch) > 0.01f) {
        m_cam_yaw   -= m_orbit_vel_yaw * dt;
        m_cam_pitch += m_orbit_vel_pitch * dt;
        m_cam_pitch = std::clamp(m_cam_pitch, -89.0f, 89.0f);
        m_orbit_vel_yaw *= damp;
        m_orbit_vel_pitch *= damp;
        if (std::abs(m_orbit_vel_yaw) < 0.01f) m_orbit_vel_yaw = 0.0f;
        if (std::abs(m_orbit_vel_pitch) < 0.01f) m_orbit_vel_pitch = 0.0f;
    }
    if (std::abs(m_pan_vel[0]) > 1e-3f || std::abs(m_pan_vel[1]) > 1e-3f ||
        std::abs(m_pan_vel[2]) > 1e-3f) {
        m_cam_target[0] += m_pan_vel[0] * dt;
        m_cam_target[1] += m_pan_vel[1] * dt;
        m_cam_target[2] += m_pan_vel[2] * dt;
        m_pan_vel[0] *= damp; m_pan_vel[1] *= damp; m_pan_vel[2] *= damp;
        if (std::abs(m_pan_vel[0]) < 1e-3f) m_pan_vel[0] = 0.0f;
        if (std::abs(m_pan_vel[1]) < 1e-3f) m_pan_vel[1] = 0.0f;
        if (std::abs(m_pan_vel[2]) < 1e-3f) m_pan_vel[2] = 0.0f;
    }
}

// World point the zoom should converge on: the selected object, else the
// spawn point (asset_viewer policy — "zoom heads toward the playable area").
static bool zoom_focus_point(const av::SceneData& scene, int selected,
                             float out[3]) {
    if (selected >= 0 && selected < (int)scene.objects.size()) {
        out[0] = scene.objects[selected].pos_x;
        out[1] = scene.objects[selected].pos_y;
        out[2] = scene.objects[selected].pos_z;
        return true;
    }
    int spawn_idx = -1, first_spawn = -1;
    for (int i = 0; i < (int)scene.objects.size(); ++i) {
        if (!scene.objects[i].is_spawn_point) continue;
        if (first_spawn < 0) first_spawn = i;
        if (scene.objects[i].name == "spawn_default") { spawn_idx = i; break; }
    }
    if (spawn_idx < 0) spawn_idx = first_spawn;
    if (spawn_idx < 0) return false;
    out[0] = scene.objects[spawn_idx].pos_x;
    out[1] = scene.objects[spawn_idx].pos_y;
    out[2] = scene.objects[spawn_idx].pos_z;
    return true;
}

void Viewport3DWidget::mousePressEvent(QMouseEvent* event) {
    if (m_mesh_edit && mesh_edit_mouse_press(event)) { update(); return; }
    m_mouse_press_pos = event->pos();
    m_last_mouse_pos = event->pos();
    m_gizmo_cursor = event->position();
    const float pad = 12.0f;
    const float size = 150.0f;
    const QPointF& pt = event->position();
    const bool in_cube_area = (pt.x() >= pad && pt.x() <= (pad + size) &&
                               pt.y() >= pad && pt.y() <= (pad + size));

    if (event->button() == Qt::LeftButton) {
        // 0. Check Camera Bounds handles if visible
        if (m_has_scene && m_show_camera_bounds && !m_mesh_edit) {
            av::CameraBounds cb;
            if (av::scene_get_camera_bounds(m_scene, cb)) {
                BoundsHandle h = m_bounds_gizmo.hit_test(event->position(), cb,
                                                         width(), height(),
                                                         m_gizmo_view, m_gizmo_proj);
                if (h != BoundsHandle::None) {
                    m_bounds_active_handle = h;
                    m_bounds_dragging = true;
                    m_camera_bounds_selected = true;
                    m_bounds_drag_initial = cb;
                    float wx = 0.0f, wy = 0.0f;
                    CameraBoundsGizmo::screen_to_world_xy(event->position().x(), event->position().y(),
                                                          width(), height(),
                                                          m_gizmo_view, m_gizmo_proj,
                                                          wx, wy);
                    m_bounds_drag_start_world_x = wx;
                    m_bounds_drag_start_world_y = wy;
                    set_selected_object(-1);
                    emit cameraBoundsSelected(true);
                    update();
                    return;
                }
            }
        }

        // 1. Check if user clicked on or inside the 3D ViewCube (top-left camera controller)
        if (in_cube_area) {
            int cube_box = hit_test_view_cube(pt);
            m_view_cube_over_box = cube_box;
            m_view_cube_clicking = (cube_box >= 0 && cube_box != 13);
            m_view_cube_dragging = true;
            m_orbiting = false;
            m_gizmo_lmb = false;
            m_gizmo_grab = false;
            update();
            return;
        }

        // 2. With a transform tool armed, check if mouse is over gizmo handles.
        const bool gizmo_armed = m_gizmo_mode != GizmoOff && has_scene() &&
                                 m_selected_scene_object >= 0 &&
                                 m_selected_scene_object < (int)m_scene.objects.size();
        bool over_gizmo = false;
        if (gizmo_armed) {
            const auto& o = m_scene.objects[m_selected_scene_object];
            float pos[3]     = { o.pos_x,   o.pos_y,   o.pos_z };
            float rot_deg[3] = { o.rot_x,   o.rot_z,   o.rot_y };
            float scl[3]     = { o.scale_x, o.scale_y, o.scale_z };
            ruby::gizmo::Mode gmode = ruby::gizmo::Mode::None;
            if (m_gizmo_mode == GizmoMove) gmode = ruby::gizmo::Mode::Translate;
            else if (m_gizmo_mode == GizmoRotate) gmode = ruby::gizmo::Mode::Rotate;
            else if (m_gizmo_mode == GizmoScale) gmode = ruby::gizmo::Mode::Scale;

            m_gizmo.set_cursor(static_cast<float>(m_gizmo_cursor.x()),
                               static_cast<float>(m_gizmo_cursor.y()),
                               false);
            over_gizmo = (m_gizmo.hit_test(gmode, pos, rot_deg, scl) != 0) || m_gizmo.is_hovering();
        }

        if (over_gizmo) {
            m_gizmo_lmb  = true;
            m_gizmo_grab = true;
            m_orbiting   = false;
            setCursor(QCursor(Qt::ClosedHandCursor));
            update();
            return;
        }
        m_orbiting = true;
        m_move_timer.start();
        m_move_timer_valid = true;
        m_orbit_vel_yaw = m_orbit_vel_pitch = 0.0f;
    } else if (event->button() == Qt::RightButton) {
        if (in_cube_area) return;
        m_rmb_down = true;
        m_move_timer.start();
        m_move_timer_valid = true;
    } else if (event->button() == Qt::MiddleButton) {
        if (in_cube_area) return;
        m_panning = true;
        m_move_timer.start();
        m_move_timer_valid = true;
        m_pan_vel[0] = m_pan_vel[1] = m_pan_vel[2] = 0.0f;
    }
}

void Viewport3DWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_mesh_edit) { mesh_edit_mouse_move(event); update(); return; }
    m_gizmo_cursor = event->position();

    // While camera bounds is being dragged, update camera bounds
    if (m_bounds_dragging && m_bounds_active_handle != BoundsHandle::None && m_has_scene) {
        float curr_wx = 0.0f, curr_wy = 0.0f;
        if (CameraBoundsGizmo::screen_to_world_xy(event->position().x(), event->position().y(),
                                                  width(), height(),
                                                  m_gizmo_view, m_gizmo_proj,
                                                  curr_wx, curr_wy)) {
            float dwx = curr_wx - m_bounds_drag_start_world_x;
            float dwy = curr_wy - m_bounds_drag_start_world_y;
            av::CameraBounds new_cb = CameraBoundsGizmo::calculate_drag(m_bounds_drag_initial,
                                                                        m_bounds_active_handle,
                                                                        dwx, dwy);
            av::scene_set_camera_bounds(m_scene, new_cb);
            emit cameraBoundsChanged(new_cb);
            emit sceneEdited();
            update();
            return;
        }
    }

    // While the gizmo is being dragged it owns the mouse.
    // (is_active() is only true mid-drag now; a stale post-release true can no
    // longer swallow every move — see RubyGizmo::draw_ruby_gizmo sync block.)
    if (m_gizmo_lmb || m_gizmo.is_active()) {
        setCursor(QCursor(Qt::ClosedHandCursor));
        update();
        return;
    }

    // Passive gizmo hover (no buttons): gizmo tracks the exact handle hit.
    if (m_gizmo_mode != GizmoOff && has_scene() && !m_orbiting && !m_panning &&
        m_selected_scene_object >= 0 &&
        m_selected_scene_object < (int)m_scene.objects.size() &&
        !m_view_cube_dragging && !m_rmb_down) {
        const auto& o = m_scene.objects[m_selected_scene_object];
        float pos[3]     = { o.pos_x,   o.pos_y,   o.pos_z };
        float rot_deg[3] = { o.rot_x,   o.rot_z,   o.rot_y };
        float scl[3]     = { o.scale_x, o.scale_y, o.scale_z };
        ruby::gizmo::Mode gmode = ruby::gizmo::Mode::None;
        if (m_gizmo_mode == GizmoMove) gmode = ruby::gizmo::Mode::Translate;
        else if (m_gizmo_mode == GizmoRotate) gmode = ruby::gizmo::Mode::Rotate;
        else if (m_gizmo_mode == GizmoScale) gmode = ruby::gizmo::Mode::Scale;

        m_gizmo.set_cursor(static_cast<float>(m_gizmo_cursor.x()),
                           static_cast<float>(m_gizmo_cursor.y()),
                           false);
        int hit = m_gizmo.hit_test(gmode, pos, rot_deg, scl);
        setCursor(hit != 0 ? QCursor(Qt::SizeAllCursor) : QCursor(Qt::ArrowCursor));
        update();
        return;
    }

    // While ViewCube is being dragged, it orbits the camera and owns the mouse.
    if (m_view_cube_dragging) {
        const int dx = event->pos().x() - m_last_mouse_pos.x();
        const int dy = event->pos().y() - m_last_mouse_pos.y();
        m_last_mouse_pos = event->pos();

        const int travelled = (event->pos() - m_mouse_press_pos).manhattanLength();
        if (travelled > 4) {
            m_view_cube_clicking = false;
        }
        m_cam_yaw -= dx * 0.4f;
        m_cam_pitch += dy * 0.4f;
        m_cam_pitch = std::clamp(m_cam_pitch, -89.0f, 89.0f);
        setCursor(QCursor(Qt::ClosedHandCursor));
        update();
        return;
    }

    // ViewCube hover test
    const float pad = 12.0f;
    const float size = 150.0f;
    const QPointF& pt = event->position();
    const bool in_cube_area = (pt.x() >= pad && pt.x() <= (pad + size) &&
                               pt.y() >= pad && pt.y() <= (pad + size));
    int cube_box = in_cube_area ? hit_test_view_cube(pt) : -1;
    if (cube_box != m_view_cube_over_box) {
        m_view_cube_over_box = cube_box;
        m_view_cube_hover = (cube_box >= 0);
        update();
    }
    if ((m_view_cube_hover || in_cube_area) && !m_orbiting && !m_panning) {
        setCursor(QCursor(Qt::PointingHandCursor));
        return;
    }

    // Passive gizmo hover (no buttons): gizmo tracks the exact handle hit.
    if (m_gizmo_mode != GizmoOff && has_scene() && !m_orbiting && !m_panning &&
        m_selected_scene_object >= 0 &&
        m_selected_scene_object < (int)m_scene.objects.size()) {
        const auto& o = m_scene.objects[m_selected_scene_object];
        float pos[3]     = { o.pos_x,   o.pos_y,   o.pos_z };
        float rot_deg[3] = { o.rot_x,   o.rot_z,   o.rot_y };
        float scl[3]     = { o.scale_x, o.scale_y, o.scale_z };
        ruby::gizmo::Mode gmode = ruby::gizmo::Mode::None;
        if (m_gizmo_mode == GizmoMove) gmode = ruby::gizmo::Mode::Translate;
        else if (m_gizmo_mode == GizmoRotate) gmode = ruby::gizmo::Mode::Rotate;
        else if (m_gizmo_mode == GizmoScale) gmode = ruby::gizmo::Mode::Scale;

        m_gizmo.set_cursor(static_cast<float>(m_gizmo_cursor.x()),
                           static_cast<float>(m_gizmo_cursor.y()),
                           false);
        int hit = m_gizmo.hit_test(gmode, pos, rot_deg, scl);
        setCursor(hit != 0 ? QCursor(Qt::SizeAllCursor) : QCursor(Qt::ArrowCursor));
        update();
        return;
    }

    // Passive camera bounds hover test
    if (m_has_scene && m_show_camera_bounds && !m_orbiting && !m_panning && !m_mesh_edit) {
        av::CameraBounds cb;
        if (av::scene_get_camera_bounds(m_scene, cb)) {
            BoundsHandle h = m_bounds_gizmo.hit_test(event->position(), cb,
                                                     width(), height(),
                                                     m_gizmo_view, m_gizmo_proj);
            if (h != m_bounds_hover_handle) {
                m_bounds_hover_handle = h;
                update();
            }
            if (h != BoundsHandle::None) {
                switch (h) {
                    case BoundsHandle::Center: setCursor(QCursor(Qt::SizeAllCursor)); break;
                    case BoundsHandle::Left:
                    case BoundsHandle::Right: setCursor(QCursor(Qt::SizeHorCursor)); break;
                    case BoundsHandle::Bottom:
                    case BoundsHandle::Top: setCursor(QCursor(Qt::SizeVerCursor)); break;
                    case BoundsHandle::CornerBL:
                    case BoundsHandle::CornerTR: setCursor(QCursor(Qt::SizeBDiagCursor)); break;
                    case BoundsHandle::CornerBR:
                    case BoundsHandle::CornerTL: setCursor(QCursor(Qt::SizeFDiagCursor)); break;
                    default: break;
                }
                return;
            }
        }
    }

    const int dx = event->pos().x() - m_last_mouse_pos.x();
    const int dy = event->pos().y() - m_last_mouse_pos.y();
    m_last_mouse_pos = event->pos();

    if (m_rmb_down) {
        m_cam_yaw   -= dx * 0.4f;
        m_cam_pitch += dy * 0.4f;
        m_cam_pitch = std::clamp(m_cam_pitch, -89.0f, 89.0f);
        update();
        return;
    }

    if (m_orbiting) {
        // Velocity estimate from event spacing → release inertia (asset_viewer).
        float dt = 1.0f / 60.0f;
        if (m_move_timer_valid && m_move_timer.isValid()) {
            const qint64 n = m_move_timer.restart();
            if (n > 0) dt = std::clamp(float(n) / 1000.0f, 1.0f / 500.0f, 0.1f);
        }
        const float inst_yaw_v   = (dx * 0.5f * m_cam_orbit_speed) / dt;
        const float inst_pitch_v = (dy * 0.5f * m_cam_orbit_speed) / dt;
        m_orbit_vel_yaw   = 0.6f * inst_yaw_v   + 0.4f * m_orbit_vel_yaw;
        m_orbit_vel_pitch = 0.6f * inst_pitch_v + 0.4f * m_orbit_vel_pitch;
        m_cam_yaw   -= dx * 0.5f * m_cam_orbit_speed;
        m_cam_pitch += dy * 0.5f * m_cam_orbit_speed;
        m_cam_pitch = std::clamp(m_cam_pitch, -89.0f, 89.0f);
        update();
    } else if (m_panning) {
        // Camera-local Right/Up pan (asset_viewer.cpp:3497-3510): includes the
        // pitch term in Up so panning is always screen-parallel.
        const float pan_speed = m_cam_dist * 0.003f * m_cam_pan_speed;
        const float rad_yaw = m_cam_yaw * (3.14159265f / 180.0f);
        const float rad_pitch = m_cam_pitch * (3.14159265f / 180.0f);

        const float rx = std::cos(rad_yaw);
        const float rz = -std::sin(rad_yaw);
        const float ux = -std::sin(rad_pitch) * std::sin(rad_yaw);
        const float uy =  std::cos(rad_pitch);
        const float uz = -std::sin(rad_pitch) * std::cos(rad_yaw);

        const float step_x = -rx * dx * pan_speed + ux * dy * pan_speed;
        const float step_y =                    uy * dy * pan_speed;
        const float step_z = -rz * dx * pan_speed + uz * dy * pan_speed;
        m_cam_target[0] += step_x;
        m_cam_target[1] += step_y;
        m_cam_target[2] += step_z;

        float dt = 1.0f / 60.0f;
        if (m_move_timer_valid && m_move_timer.isValid()) {
            const qint64 n = m_move_timer.restart();
            if (n > 0) dt = std::clamp(float(n) / 1000.0f, 1.0f / 500.0f, 0.1f);
        }
        m_pan_vel[0] = 0.6f * (step_x / dt) + 0.4f * m_pan_vel[0];
        m_pan_vel[1] = 0.6f * (step_y / dt) + 0.4f * m_pan_vel[1];
        m_pan_vel[2] = 0.6f * (step_z / dt) + 0.4f * m_pan_vel[2];
        update();
    }
}

void Viewport3DWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_mesh_edit) { mesh_edit_mouse_release(event); update(); return; }
    if (m_bounds_dragging) {
        m_bounds_dragging = false;
        m_bounds_active_handle = BoundsHandle::None;
        setCursor(QCursor(Qt::ArrowCursor));
        update();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const bool was_cube_dragging = m_view_cube_dragging;
        const bool was_cube_clicking = m_view_cube_clicking;
        m_view_cube_dragging = false;
        m_view_cube_clicking = false;
        const bool was_orbiting = m_orbiting;
        const bool gizmo_was_active = m_gizmo_grab || m_gizmo.is_active();
        m_orbiting  = false;
        m_gizmo_lmb = false;
        m_gizmo_grab = false;
        // Feed the gizmo the release NOW: its internal drag state only ends on
        // the next draw(), which needs a repaint. Without this, releasing a
        // gizmo drag without another update() left the gizmo active — the
        // cursor stayed a closed hand and every mouse move was swallowed, so
        // the camera appeared frozen until an unrelated repaint happened.
        m_gizmo.set_cursor(static_cast<float>(m_gizmo_cursor.x()),
                           static_cast<float>(m_gizmo_cursor.y()),
                           false);
        if (m_gizmo_edited) {          // a transform drag just finished
            m_gizmo_edited = false;
            if (m_gizmo_had_snapshot && m_selected_scene_object >= 0 &&
                m_selected_scene_object < static_cast<int>(m_scene.objects.size())) {
                TransformState old_t;
                old_t.pos[0] = m_gizmo_snap_pos[0];
                old_t.pos[1] = m_gizmo_snap_pos[1];
                old_t.pos[2] = m_gizmo_snap_pos[2];
                old_t.rot[0] = m_gizmo_snap_rot[0];
                old_t.rot[1] = m_gizmo_snap_rot[1];
                old_t.rot[2] = m_gizmo_snap_rot[2];
                old_t.scale[0] = m_gizmo_snap_scale[0];
                old_t.scale[1] = m_gizmo_snap_scale[1];
                old_t.scale[2] = m_gizmo_snap_scale[2];

                if (m_gizmo_mode == GizmoScale) {
                    // Scale drag → snapshot undo: the payload geometry must
                    // scale WITH the transform (web editor `scaleObjectData`
                    // parity) and undo must restore BOTH, which the per-axis
                    // TransformUndoCommand cannot do. The "before" snapshot
                    // was captured at drag start (gizmo draw).
                    auto& o = m_scene.objects[m_selected_scene_object];
                    auto ratio = [](float old_v, float new_v) {
                        return std::fabs(old_v) > 1e-6f ? new_v / old_v : 1.0f;
                    };
                    av::scene_scale_object_payload(
                        m_scene, static_cast<size_t>(m_selected_scene_object),
                        ratio(m_gizmo_snap_scale[0], o.scale_x),
                        ratio(m_gizmo_snap_scale[1], o.scale_y),
                        ratio(m_gizmo_snap_scale[2], o.scale_z));
                    sync_edited_object_caches();
                    if (!m_gizmo_snap_scene.empty()) {
                        undo_stack()->push(new SceneSnapshotUndoCommand(
                            this, m_gizmo_snap_scene, av::scene_serialize(m_scene),
                            QString("Scale Object %1").arg(m_selected_scene_object)));
                        m_gizmo_snap_scene.clear();
                    }
                } else {
                    const auto& o = m_scene.objects[m_selected_scene_object];
                    TransformState new_t;
                    new_t.pos[0] = o.pos_x; new_t.pos[1] = o.pos_y; new_t.pos[2] = o.pos_z;
                    new_t.rot[0] = o.rot_x; new_t.rot[1] = o.rot_z; new_t.rot[2] = o.rot_y;
                    new_t.scale[0] = o.scale_x; new_t.scale[1] = o.scale_y; new_t.scale[2] = o.scale_z;

                    undo_stack()->push(new TransformUndoCommand(this, m_selected_scene_object, old_t, new_t));
                }
            }
            m_gizmo_had_snapshot = false;
            m_gizmo_snap_scene.clear();
            emit sceneEdited();
        }
        setCursor(QCursor(Qt::ArrowCursor));

        // If user tapped a specific ViewCube sub-box without dragging, smoothly align camera to it
        if (was_cube_clicking && m_view_cube_over_box >= 0 && m_view_cube_over_box != 13) {
            int cx = m_view_cube_over_box / 9;
            int cy = (m_view_cube_over_box - cx * 9) / 3;
            int cz = m_view_cube_over_box % 3;
            float dx = 1.0f - static_cast<float>(cx);
            float dy = 1.0f - static_cast<float>(cy);
            float dz = 1.0f - static_cast<float>(cz);
            float len = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (len > 1e-4f) {
                dx /= len; dy /= len; dz /= len;
                if (std::abs(dy) > 0.99f) {
                    m_view_cube_target_pitch = (dy > 0.0f) ? 89.0f : -89.0f;
                    m_view_cube_target_yaw = m_cam_yaw;
                } else {
                    m_view_cube_target_pitch = std::asin(std::clamp(dy, -0.999f, 0.999f)) * (180.0f / 3.14159265f);
                    m_view_cube_target_yaw = std::atan2(dx, dz) * (180.0f / 3.14159265f);
                }
                m_view_cube_anim_frames = 20;
                update();
                return;
            }
        }

        // A click (press+release with no travel) selects / deselects objects.
        // Selection is allowed when LMB was released and no transform drag was
        // in flight (gizmo_was_active). was_orbiting is irrelevant here — a
        // tap on empty space with no tool armed should still pick, and a tap
        // on an object with a tool armed but no handle hit should switch to
        // that object.
        const int travelled = (event->pos() - m_mouse_press_pos).manhattanLength();
        if (!gizmo_was_active && !was_cube_dragging && travelled <= 6 && m_has_scene) {
            pick_object_at(event->position());
            if (m_selected_scene_object < 0) {
                av::CameraBounds cb;
                if (m_show_camera_bounds && av::scene_get_camera_bounds(m_scene, cb)) {
                    BoundsHandle h = m_bounds_gizmo.hit_test(event->position(), cb,
                                                             width(), height(),
                                                             m_gizmo_view, m_gizmo_proj);
                    m_camera_bounds_selected = (h != BoundsHandle::None);
                    emit cameraBoundsSelected(m_camera_bounds_selected);
                } else {
                    m_camera_bounds_selected = false;
                    emit cameraBoundsSelected(false);
                }
            } else {
                m_camera_bounds_selected = false;
                emit cameraBoundsSelected(false);
            }
        }
        // An orbit/pan that ended keeps its inertia; a click kills it.
        if (travelled <= 6) {
            m_orbit_vel_yaw = m_orbit_vel_pitch = 0.0f;
            m_pan_vel[0] = m_pan_vel[1] = m_pan_vel[2] = 0.0f;
        }
    }
    if (event->button() == Qt::RightButton) {
        m_rmb_down = false;
        const int travelled = (event->pos() - m_mouse_press_pos).manhattanLength();
        if (travelled <= 6 && m_has_scene) {
            show_viewport_context_menu(event->globalPosition().toPoint());
        }
    }
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
    }
    // Always repaint after a release so the gizmo's internal drag state is
    // flushed (the release feed above) and the cursor/hover visuals refresh.
    update();
}

void Viewport3DWidget::leaveEvent(QEvent* event) {
    m_view_cube_hover = false;
    m_view_cube_over_box = -1;
    m_view_cube_dragging = false;
    m_view_cube_clicking = false;
    m_rmb_down = false;
    if (m_gizmo_lmb || m_gizmo.is_active()) {
        m_gizmo_lmb  = false;
        m_gizmo_grab = false;
        // End the gizmo's internal drag synchronously (it sees the release on
        // the next draw) so the cursor can never stay stuck as a closed hand
        // after the mouse leaves the viewport mid-drag.
        m_gizmo.set_cursor(static_cast<float>(m_gizmo_cursor.x()),
                           static_cast<float>(m_gizmo_cursor.y()),
                           false);
        if (m_gizmo_edited) {
            m_gizmo_edited = false;
            emit sceneEdited();
        }
        m_gizmo_snap_scene.clear();
        update();
    }
    setCursor(QCursor(Qt::ArrowCursor));
    QOpenGLWidget::leaveEvent(event);
}

void Viewport3DWidget::wheelEvent(QWheelEvent* event) {
    const float delta_y = event->angleDelta().y();
    if (std::abs(delta_y) < 1e-3f) return;

    // Exponential zoom: smooth at any distance, with
    // a near-plane-tied floor so the camera can never dolly through the focus.
    const float factor = std::pow(0.94f, delta_y / 120.0f * m_cam_zoom_speed);
    const float old_dist = m_cam_dist;
    const float min_dist = std::max(0.005f, m_cam_dist * 0.001f);
    const float max_dist = std::max(100000.0f, m_scene_extent * 10.0f);
    const float new_dist = std::clamp(old_dist * factor, min_dist, max_dist);
    const float delta_dist = old_dist - new_dist;

    // Keep the world point under the mouse cursor stationary for scene mode.
    // In model viewer mode (single model inspection), keep the focus centered.
    if (!m_has_model && !m_has_glb) {
        const float w = static_cast<float>(std::max(1, width()));
        const float h = static_cast<float>(std::max(1, height()));
        const QPointF mouse_pos = event->position();
        const float ndc_x = (2.0f * static_cast<float>(mouse_pos.x()) / w) - 1.0f;
        const float ndc_y = 1.0f - (2.0f * static_cast<float>(mouse_pos.y()) / h);
        constexpr float pi = 3.14159265f;
        constexpr float fov = 45.0f * (pi / 180.0f);
        const float f = 1.0f / std::tan(fov * 0.5f);
        const float aspect = w / h;
        const float vx = (ndc_x * aspect) / f;
        const float vy = ndc_y / f;
        const float rad_pitch = m_cam_pitch * (pi / 180.0f);
        const float rad_yaw   = m_cam_yaw * (pi / 180.0f);
        const float rx = std::cos(rad_yaw), ry = 0.0f, rz = -std::sin(rad_yaw);
        const float ux = -std::sin(rad_pitch) * std::sin(rad_yaw);
        const float uy =  std::cos(rad_pitch);
        const float uz = -std::sin(rad_pitch) * std::cos(rad_yaw);
        m_cam_target[0] += delta_dist * (vx * rx + vy * ux);
        m_cam_target[1] += delta_dist * (vx * ry + vy * uy);
        m_cam_target[2] += delta_dist * (vx * rz + vy * uz);
    }

    m_cam_dist = new_dist;
    update();
}

void Viewport3DWidget::keyPressEvent(QKeyEvent* event) {
    if (m_mesh_edit && mesh_edit_key(event)) { event->accept(); return; }
    if (event->key() == Qt::Key_Escape) {
        const bool in_drag = m_gizmo_lmb || m_gizmo.is_active();
        if (in_drag && m_gizmo_had_snapshot && m_selected_scene_object >= 0 &&
            m_selected_scene_object < (int)m_scene.objects.size()) {
            // Cancel the live drag, restoring values captured at drag-start.
            av::SceneObject& o = m_scene.objects[m_selected_scene_object];
            o.pos_x = m_gizmo_snap_pos[0];   o.pos_y = m_gizmo_snap_pos[1];   o.pos_z = m_gizmo_snap_pos[2];
            o.rot_x = m_gizmo_snap_rot[0];   o.rot_z = m_gizmo_snap_rot[1];   o.rot_y = m_gizmo_snap_rot[2];
            o.scale_x = m_gizmo_snap_scale[0]; o.scale_y = m_gizmo_snap_scale[1]; o.scale_z = m_gizmo_snap_scale[2];
            m_gizmo_lmb = false;
            m_gizmo_grab = false;
            m_gizmo_had_snapshot = false;
            m_gizmo_edited = false;
            sync_edited_object_caches();
            update();
        } else {
            set_gizmo_mode(GizmoOff);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Tab) {
        set_gizmo_mode((m_gizmo_mode + 1) % 4);   // View → Move → Rotate → Scale
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Q && event->modifiers() == Qt::NoModifier) {
        m_gizmo.toggle_coord_space();
        update();
        event->accept();
        return;
    }

    // In-session QUndoStack: Ctrl+Z (Undo) and Ctrl+Y / Ctrl+Shift+Z (Redo)
    if (event->matches(QKeySequence::Undo) ||
        (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_Z && !event->modifiers().testFlag(Qt::ShiftModifier))) {
        if (undo_stack()->canUndo()) {
            undo_stack()->undo();
            event->accept();
            return;
        }
    }
    if (event->matches(QKeySequence::Redo) ||
        (event->modifiers().testFlag(Qt::ControlModifier) && (event->key() == Qt::Key_Y || (event->key() == Qt::Key_Z && event->modifiers().testFlag(Qt::ShiftModifier))))) {
        if (undo_stack()->canRedo()) {
            undo_stack()->redo();
            event->accept();
            return;
        }
    }

    // Scene-object clipboard + editing shortcuts (ImGui asset_viewer parity):
    // Ctrl+C copy, Ctrl+V paste, Ctrl+D duplicate, Delete/Backspace remove,
    // Alt+Up/Down reorder the active object.
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (event->key() == Qt::Key_C) { copy_scene_selection();   event->accept(); return; }
        if (event->key() == Qt::Key_V) { paste_scene_selection();  event->accept(); return; }
        if (event->key() == Qt::Key_D) { duplicate_scene_selection(); event->accept(); return; }
    }
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        delete_scene_selection();
        event->accept();
        return;
    }
    if (event->modifiers().testFlag(Qt::AltModifier)) {
        if (event->key() == Qt::Key_Up)   { move_scene_object(-1); event->accept(); return; }
        if (event->key() == Qt::Key_Down) { move_scene_object( 1); event->accept(); return; }
    }

    // M toggles the in-scene mesh editor (ground-mesh polygon editing with the
    // camera locked to the object's plane).
    if (event->key() == Qt::Key_M && has_scene()) {
        set_mesh_edit(!m_mesh_edit);
        event->accept();
        return;
    }

    // Viewport Free-Fly Mode: holding RMB + WASD keys moves the camera in 3D space (Shift * 3.5x)
    if (m_rmb_down) {
        float fly_speed = (m_cam_dist * 0.05f + 5.0f);
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            fly_speed *= 3.5f;
        }

        const float pi = 3.14159265f;
        const float rad_pitch = m_cam_pitch * (pi / 180.0f);
        const float rad_yaw   = m_cam_yaw * (pi / 180.0f);

        // Forward and right camera basis vectors
        const float fx = -std::sin(rad_yaw) * std::cos(rad_pitch);
        const float fy = -std::sin(rad_pitch);
        const float fz = -std::cos(rad_yaw) * std::cos(rad_pitch);

        const float rx = std::cos(rad_yaw);
        const float rz = -std::sin(rad_yaw);

        if (event->key() == Qt::Key_W) {
            m_cam_target[0] += fx * fly_speed;
            m_cam_target[1] += fy * fly_speed;
            m_cam_target[2] += fz * fly_speed;
            update(); event->accept(); return;
        }
        if (event->key() == Qt::Key_S) {
            m_cam_target[0] -= fx * fly_speed;
            m_cam_target[1] -= fy * fly_speed;
            m_cam_target[2] -= fz * fly_speed;
            update(); event->accept(); return;
        }
        if (event->key() == Qt::Key_A) {
            m_cam_target[0] -= rx * fly_speed;
            m_cam_target[2] -= rz * fly_speed;
            update(); event->accept(); return;
        }
        if (event->key() == Qt::Key_D) {
            m_cam_target[0] += rx * fly_speed;
            m_cam_target[2] += rz * fly_speed;
            update(); event->accept(); return;
        }
    }

    // Viewport Hotkeys: W or T (Translate), E or R (Rotate), R or S (Scale), F (Focus), Enter (Commit)
    if (event->key() == Qt::Key_W || event->key() == Qt::Key_T) {
        set_gizmo_mode(GizmoMove);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_E) {
        set_gizmo_mode(GizmoRotate);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_R) {
        if (m_gizmo_mode == GizmoRotate) {
            set_gizmo_mode(GizmoScale);
        } else {
            set_gizmo_mode(GizmoRotate);
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_S) {
        set_gizmo_mode(GizmoScale);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        if (m_selected_scene_object >= 0 && m_selected_scene_object < static_cast<int>(m_scene.objects.size())) {
            focus_object(m_selected_scene_object);
        } else if (m_has_model) {
            m_cam_target[0] = m_model.center_x;
            m_cam_target[1] = m_model.center_y;
            m_cam_target[2] = m_model.center_z;
            m_cam_dist = std::max(0.1f, m_model.radius * 2.5f);
            update();
        } else if (m_has_glb) {
            m_cam_target[0] = m_glb_model.center().x();
            m_cam_target[1] = m_glb_model.center().y();
            m_cam_target[2] = m_glb_model.center().z();
            m_cam_dist = std::max(0.1f, m_glb_model.radius() * 2.5f);
            update();
        } else {
            reset_camera();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        set_selected_object(-1);
        emit sceneObjectSelected(-1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_B) { m_show_skeleton = !m_show_skeleton; update(); event->accept(); return; }
    QOpenGLWidget::keyPressEvent(event);
}

// ============================================================================
// Scene editing — transform gizmo (structured, RAM-only until host saves)
// ============================================================================

void Viewport3DWidget::set_gizmo_mode(int mode) {
    // Cancel any live drag when switching tools.
    m_gizmo_lmb = false;
    m_gizmo_grab = false;
    m_gizmo_had_snapshot = false;
    m_gizmo_edited = false;
    m_gizmo_mode = std::clamp(mode, 0, 3);
    if (m_gizmo_bar) {
        const auto btns = m_gizmo_bar->findChildren<QToolButton*>();
        for (QToolButton* b : btns) {
            const int m = b->property("mode").toInt();
            if (m >= 0) b->setChecked(m == m_gizmo_mode);
        }
    }
    const char* hint =
        m_gizmo_mode == GizmoMove   ? "Move tool — drag an arrow (hold Ctrl to snap to 1-unit grid)"
        : m_gizmo_mode == GizmoRotate ? "Rotate tool — drag a ring (Ctrl = snap 15°)"
        : m_gizmo_mode == GizmoScale ? "Scale tool — drag a cube tip for per-axis scale, centre ball for uniform"
        : "View tool — click to select objects; LMB drag orbits, RMB pans, wheel zooms";
    ruby::core::ProjectContext::instance().set_status(hint);
    update();
}

void Viewport3DWidget::refresh_edited_object() {
    sync_edited_object_caches();
}

void Viewport3DWidget::revert_pending_scene_edit() {
    if (!m_gizmo_had_snapshot || m_selected_scene_object < 0 ||
        m_selected_scene_object >= (int)m_scene.objects.size()) {
        return;
    }
    av::SceneObject& o = m_scene.objects[m_selected_scene_object];
    o.pos_x = m_gizmo_snap_pos[0];   o.pos_y = m_gizmo_snap_pos[1];   o.pos_z = m_gizmo_snap_pos[2];
    o.rot_x = m_gizmo_snap_rot[0];   o.rot_z = m_gizmo_snap_rot[1];   o.rot_y = m_gizmo_snap_rot[2];
    o.scale_x = m_gizmo_snap_scale[0]; o.scale_y = m_gizmo_snap_scale[1]; o.scale_z = m_gizmo_snap_scale[2];
    m_gizmo_lmb = false;
    m_gizmo_grab = false;
    m_gizmo_had_snapshot = false;
    m_gizmo_edited = false;
    m_gizmo_snap_scene.clear();
    sync_edited_object_caches();
    update();
}

// A tiny helper capturing the widget's orbit camera in gizmo terms.
static inline void viewport_gizmo_cam(const Viewport3DWidget& w, GizmoCamera& gc) {
    gc.pitch = w.camera_state().pitch;   // uses public accessor to stay decoupled
    gc.yaw   = w.camera_state().yaw;
    gc.dist  = w.camera_state().dist;
    gc.target[0] = w.camera_state().target[0];
    gc.target[1] = w.camera_state().target[1];
    gc.target[2] = w.camera_state().target[2];
    gc.w = w.width();
    gc.h = std::max(1, w.height());
}

std::string Viewport3DWidget::capture_scene_snapshot() const {
    if (!m_has_scene) return std::string();
    return av::scene_serialize(m_scene);
}

void Viewport3DWidget::push_scene_snapshot_undo(std::string before, const QString& label) {
    if (!m_has_scene || before.empty()) return;
    const std::string after = av::scene_serialize(m_scene);
    if (after == before) return;   // nothing actually changed — don't pollute the stack
    undo_stack()->push(new SceneSnapshotUndoCommand(this, std::move(before), after, label));
}

bool Viewport3DWidget::restore_scene_snapshot(const std::string& bytes) {
    if (bytes.empty() || !m_has_scene || m_current_scene_path.empty()) return false;

    // Decode the snapshot with the same identity/roots as a normal load so
    // asset resolution (models, textures, libraries) behaves identically.
    std::vector<uint8_t> raw(bytes.begin(), bytes.end());
    std::vector<std::string> extra_roots;
    const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
    if (!pdir.empty()) extra_roots.push_back(pdir);
    std::string parse_err;
    av::SceneData parsed = parse_scene_bytes(raw, m_current_scene_path, extra_roots, parse_err);
    if (parsed.objects.empty()) return false;

    bool ok;
    if (apply_scene_data(parsed, m_current_scene_path)) {
        // Fast path: same structure — transforms/components/visibility patched
        // in place, camera + GPU state kept. Re-apply the selected object's
        // render state so component-driven display changes show immediately.
        if (m_selected_scene_object >= 0 &&
            m_selected_scene_object < static_cast<int>(m_scene.objects.size()))
            refresh_edited_object();
        ok = true;
    } else {
        // Structural change (object added/removed/reordered, ground-mesh
        // geometry changed): full rebuild from the snapshot bytes.
        load_scene_async(m_current_scene_path, &bytes);
        ok = true;
    }
    emit sceneEdited();   // undo/redo moved the doc away from disk state
    update();
    return ok;
}

void Viewport3DWidget::apply_object_transform(int object_index, const TransformState& t) {
    if (!has_scene() || object_index < 0 || object_index >= static_cast<int>(m_scene.objects.size())) return;
    auto& o = m_scene.objects[object_index];
    o.pos_x = t.pos[0];     o.pos_y = t.pos[1];     o.pos_z = t.pos[2];
    o.rot_x = t.rot[0];     o.rot_z = t.rot[1];     o.rot_y = t.rot[2];
    o.scale_x = t.scale[0]; o.scale_y = t.scale[1]; o.scale_z = t.scale[2];
    sync_edited_object_caches();
    emit sceneObjectTransformed(object_index, o.pos_x, o.pos_y, o.pos_z,
                               o.rot_x, o.rot_z, o.rot_y,
                               o.scale_x, o.scale_y, o.scale_z);
    emit sceneEdited();
    update();
}

QUndoStack* Viewport3DWidget::undo_stack() {
    if (!m_current_scene_path.empty()) {
        auto& stack = m_scene_undo_stacks[m_current_scene_path];
        if (!stack) stack = std::make_unique<QUndoStack>(this);
        return stack.get();
    }
    return &m_default_undo_stack;
}

void Viewport3DWidget::undo() {
    if (undo_stack()->canUndo()) undo_stack()->undo();
}

void Viewport3DWidget::redo() {
    if (undo_stack()->canRedo()) undo_stack()->redo();
}

void Viewport3DWidget::pick_object_at(const QPointF& pos) {
    if (!has_scene()) return;

    // Camera eye and target in world space
    const float rad_pitch = m_cam_pitch * (3.14159265f / 180.0f);
    const float rad_yaw   = m_cam_yaw * (3.14159265f / 180.0f);
    const float eye_x = m_cam_target[0] + m_cam_dist * std::cos(rad_pitch) * std::sin(rad_yaw);
    const float eye_y = m_cam_target[1] + m_cam_dist * std::sin(rad_pitch);
    const float eye_z = m_cam_target[2] + m_cam_dist * std::cos(rad_pitch) * std::cos(rad_yaw);

    Vector3 eye{ eye_x, eye_y, eye_z };
    Vector3 target{ m_cam_target[0], m_cam_target[1], m_cam_target[2] };

    // Geometrically unproject exact ray directly from camera eye through screen cursor
    Ray ray = ruby::picking::get_camera_ray(
        static_cast<float>(pos.x()), static_cast<float>(pos.y()),
        static_cast<float>(width()), static_cast<float>(height()),
        eye, target, 45.0f);

    int best_hit_index = -1;
    float best_hit_dist = 1e30f;

    for (int index = 0; index < static_cast<int>(m_scene.objects.size()); ++index) {
        const auto& obj = m_scene.objects[index];
        if (obj.hidden) continue;

        // Skip background sky planes and dimension rift objects (cannot be clicked in viewport)
        if (!obj.background_name.empty() || obj.is_dimension_object) continue;
        std::string low_name = obj.name;
        for (char& c : low_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low_name.find("background") != std::string::npos || low_name.find("sky") != std::string::npos) {
            continue;
        }

        const bool has_ground = !obj.ground_meshes.empty();
        const bool in_cache = !obj.mesh_name.empty() && m_scene_models.count(obj.mesh_name) > 0;
        const bool is_marker = (obj.is_portal || obj.is_spawn_point || obj.is_camera ||
                                low_name.rfind("spawn", 0) == 0 || low_name.rfind("portal", 0) == 0 ||
                                low_name.rfind("cam", 0) == 0 || low_name.rfind("secret", 0) == 0);

        if (!has_ground && !in_cache && !is_marker) continue;

        // 1. Ground meshes (drawn with world_matrix)
        if (has_ground) {
            float world_mat[16];
            swk::object_world_matrix(obj, world_mat);
            Matrix rmat = ruby::math::from_gl(world_mat);

            for (const auto& gm : obj.ground_meshes) {
                if (gm.positions.empty()) continue;

                RayCollision c = ruby::picking::get_ray_collision_triangles(
                    ray, gm.positions.data(), 3 * sizeof(float),
                    gm.positions.size() / 3,
                    gm.indices.empty() ? nullptr : gm.indices.data(),
                    gm.indices.size(), &rmat);

                if (c.hit && c.distance < best_hit_dist) {
                    best_hit_dist = c.distance;
                    best_hit_index = index;
                }
            }
        }

        // 2. POD Models (drawn with render_matrix * center_point * node_matrix)
        if (in_cache) {
            float render_mat[16];
            swk::object_render_matrix(obj, render_mat);

            const auto& model = m_scene_models.find(obj.mesh_name)->second;
            for (int node_index = 0; node_index < static_cast<int>(model.nodes.size()); ++node_index) {
                const auto& node = model.nodes[node_index];
                if (node.object_index < 0 || node.object_index >= static_cast<int>(model.meshes.size())) continue;
                const auto& mesh = model.meshes[node.object_index];
                if (mesh.positions.empty()) continue;

                float node_mat[16];
                av::get_node_matrix(model, node_index, 0.0f, node_mat);

                // Replicate OpenGL transform hierarchy: render_matrix * T(-center_point) * node_matrix
                float M1[16];
                if (model.has_center_point) {
                    float Tcenter[16];
                    av::mat4_translate(Tcenter, -model.center_point[0], -model.center_point[1], -model.center_point[2]);
                    av::mat4_multiply(M1, render_mat, Tcenter);
                } else {
                    std::memcpy(M1, render_mat, sizeof(M1));
                }
                float Mfinal[16];
                av::mat4_multiply(Mfinal, M1, node_mat);

                Matrix rmat = ruby::math::from_gl(Mfinal);

                RayCollision c = ruby::picking::get_ray_collision_triangles(
                    ray, mesh.positions.data(), 3 * sizeof(float),
                    mesh.positions.size() / 3,
                    mesh.indices.empty() ? nullptr : mesh.indices.data(),
                    mesh.indices.size(), &rmat);

                if (c.hit && c.distance < best_hit_dist) {
                    best_hit_dist = c.distance;
                    best_hit_index = index;
                }
            }
        }

        // 3. Logic / Marker entities without geometry (portal, spawn point, camera marker)
        if (!has_ground && !in_cache && is_marker) {
            float world_mat[16];
            swk::object_world_matrix(obj, world_mat);
            Matrix rmat = ruby::math::from_gl(world_mat);

            const float half_size = 30.0f * std::max(0.2f, std::abs(obj.scale_x * obj.template_scaling));
            BoundingBox local_marker_box{
                Vector3{-half_size, -half_size, -half_size},
                Vector3{ half_size,  half_size,  half_size}
            };

            Vector3 wmin{ 1e30f,  1e30f,  1e30f};
            Vector3 wmax{-1e30f, -1e30f, -1e30f};
            for (int c = 0; c < 8; ++c) {
                Vector3 corner{
                    (c & 1) ? local_marker_box.max.x : local_marker_box.min.x,
                    (c & 2) ? local_marker_box.max.y : local_marker_box.min.y,
                    (c & 4) ? local_marker_box.max.z : local_marker_box.min.z
                };
                Vector3 wcorner = Vector3Transform(corner, rmat);
                wmin.x = std::min(wmin.x, wcorner.x); wmin.y = std::min(wmin.y, wcorner.y); wmin.z = std::min(wmin.z, wcorner.z);
                wmax.x = std::max(wmax.x, wcorner.x); wmax.y = std::max(wmax.y, wcorner.y); wmax.z = std::max(wmax.z, wcorner.z);
            }

            BoundingBox world_box{wmin, wmax};
            RayCollision box_hit = ruby::picking::get_ray_collision_box(ray, world_box);
            if (box_hit.hit && box_hit.distance < best_hit_dist) {
                best_hit_dist = box_hit.distance;
                best_hit_index = index;
            }
        }
    }

    set_selected_object(best_hit_index);
    emit sceneObjectSelected(best_hit_index);
}

void Viewport3DWidget::sync_edited_object_caches() {
    const int idx = m_selected_scene_object;
    if (!has_scene() || idx < 0 || (size_t)idx >= m_scene.objects.size()) return;
    av::SceneObject& o = m_scene.objects[idx];

    // GPU-side render objects (drawn every frame).
    for (auto& ro : m_render_objects) {
        if (ro.object_index != idx) continue;
        ro.pos[0] = o.pos_x; ro.pos[1] = o.pos_y; ro.pos[2] = o.pos_z;
        swk::object_world_matrix(o, ro.world_matrix);
        swk::object_render_matrix(o, ro.render_matrix);
    }

    // Widget-scoped RAM cache (tab switching back to this scene must keep edits).
    auto wit = m_scene_cache.find(m_current_scene_path);
    if (wit != m_scene_cache.end() && wit->second) {
        auto& st = *wit->second;
        if (idx < (int)st.scene.objects.size()) {
            st.scene.objects[idx] = o;
            for (auto& ro : st.render_objects) {
                if (ro.object_index != idx) continue;
                ro.pos[0] = o.pos_x; ro.pos[1] = o.pos_y; ro.pos[2] = o.pos_z;
                swk::object_world_matrix(o, ro.world_matrix);
                swk::object_render_matrix(o, ro.render_matrix);
            }
        }
    }

    // (Edits live in m_scene + this scene's own session. The old process-wide
    // cache is gone — writing the edited object into a shared map keyed only
    // by path was how edits leaked between two open scenes.)
}

// ============================================================================
// Scene editing — RubyGizmo transform gizmo
//   draw_ruby_gizmo() feeds the camera matrices + cursor position to the
//   bespoke RubyGizmo renderer, reads back the mutated transform fields,
//   and writes them into the in-RAM av::SceneObject (no disk writes here).
// ============================================================================

void Viewport3DWidget::draw_ruby_gizmo() {
    if (!has_scene()) return;
    const int sel = m_selected_scene_object;
    if (sel < 0 || sel >= (int)m_scene.objects.size()) return;
    av::SceneObject& o = m_scene.objects[sel];

    // Feed the gizmo with this frame's camera (captured just before draw_scene).
    const float dpr = static_cast<float>(devicePixelRatio());
    m_gizmo.set_camera(m_gizmo_view, m_gizmo_proj, m_gizmo_eye,
                       static_cast<float>(width()),
                       static_cast<float>(height()),
                       dpr);

    // Default cursor to viewport centre when not yet moved.
    if (m_gizmo_cursor.x() < 0.0 || m_gizmo_cursor.y() < 0.0)
        m_gizmo_cursor = QPointF(width() * 0.5, height() * 0.5);
    m_gizmo.set_cursor(static_cast<float>(m_gizmo_cursor.x()),
                       static_cast<float>(m_gizmo_cursor.y()),
                       m_gizmo_lmb);

    // Ctrl-snap: 1 unit translate, 15° rotate, 0.1 scale step.
    const bool snap = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    m_gizmo.set_snap(snap ? 1.0f  : 0.0f,
                     snap ? 15.0f : 0.0f,
                     snap ? 0.1f  : 0.0f);

    // Collect the current transform as flat arrays for the gizmo.
    float pos[3]     = { o.pos_x,   o.pos_y,   o.pos_z };
    // rot_deg = { rot_x, rot_z, rot_y } — engine field order.
    float rot_deg[3] = { o.rot_x,   o.rot_z,   o.rot_y };
    float scl[3]     = { o.scale_x, o.scale_y, o.scale_z };

    // Map GizmoMode → RubyGizmo::Mode.
    ruby::gizmo::Mode gmode = ruby::gizmo::Mode::None;
    switch (m_gizmo_mode) {
        case GizmoMove:   gmode = ruby::gizmo::Mode::Translate; break;
        case GizmoRotate: gmode = ruby::gizmo::Mode::Rotate;    break;
        case GizmoScale:  gmode = ruby::gizmo::Mode::Scale;     break;
        default: break;
    }

    const bool was_active = m_gizmo.is_active();

    // Draw and interact — the gizmo writes back into pos/rot_deg/scl.
    auto result = m_gizmo.draw(gmode, pos, rot_deg, scl);

    if (result.active) {
        if (!was_active) {
            // Drag just started — snapshot current transform for Esc-cancel.
            m_gizmo_snap_pos[0]   = o.pos_x;   m_gizmo_snap_pos[1]   = o.pos_y;   m_gizmo_snap_pos[2]   = o.pos_z;
            m_gizmo_snap_rot[0]   = o.rot_x;   m_gizmo_snap_rot[1]   = o.rot_z;   m_gizmo_snap_rot[2]   = o.rot_y;
            m_gizmo_snap_scale[0] = o.scale_x; m_gizmo_snap_scale[1] = o.scale_y;            m_gizmo_snap_scale[2] = o.scale_z;
            m_gizmo_had_snapshot  = true;
            // Scale drags also snapshot the whole scene so the payload
            // geometry (LocalAABB, shapes) can scale with the transform and
            // undo restores both atomically.
            if (m_gizmo_mode == GizmoScale)
                m_gizmo_snap_scene = capture_scene_snapshot();
        }
    }

    if (result.changed) {
        // Ground / Surface Snapping: holding Shift while translating an object casts a downward ray
        // to snap the object's base flush to the terrain surface underneath it.
        const bool surface_snap = QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
        if (gmode == ruby::gizmo::Mode::Translate && surface_snap) {
            Ray down_ray;
            down_ray.position = Vector3{pos[0], pos[1] + 250.0f, pos[2]};
            down_ray.direction = Vector3{0.0f, -1.0f, 0.0f};
            RayCollision best_ground = {0};

            for (size_t i = 0; i < m_scene.objects.size(); ++i) {
                if (static_cast<int>(i) == sel) continue;
                const auto& other = m_scene.objects[i];
                if (other.hidden || other.ground_meshes.empty()) continue;

                float model_mat[16];
                swk::object_world_matrix(other, model_mat);
                Matrix rmat = ruby::math::from_gl(model_mat);

                for (const auto& gm : other.ground_meshes) {
                    if (gm.positions.empty()) continue;
                    RayCollision c = ruby::picking::get_ray_collision_triangles(
                        down_ray, gm.positions.data(), 3 * sizeof(float),
                        gm.positions.size() / 3,
                        gm.indices.empty() ? nullptr : gm.indices.data(),
                        gm.indices.size(), &rmat);
                    if (c.hit && (!best_ground.hit || c.distance < best_ground.distance)) {
                        best_ground = c;
                    }
                }
            }

            if (best_ground.hit) {
                // Compute base offset so the object's bottom rests directly on the surface
                float bottom_offset = 0.0f;
                if (!o.mesh_name.empty()) {
                    auto mit = m_scene_models.find(o.mesh_name);
                    if (mit != m_scene_models.end()) {
                        bottom_offset = mit->second.min_y * scl[1];
                    }
                } else if (!o.local_aabb.empty()) {
                    float aabb[4];
                    if (parse_local_aabb(o.local_aabb, aabb)) {
                        bottom_offset = aabb[1] * scl[1];
                    }
                }
                pos[1] = best_ground.point.y - bottom_offset;

                // If Alt is also held, align object normal to surface normal
                if (QApplication::keyboardModifiers().testFlag(Qt::AltModifier) &&
                    Vector3Length(best_ground.normal) > 1e-4f) {
                    Vector3 norm = Vector3Normalize(best_ground.normal);
                    Quaternion q_up = QuaternionFromVector3ToVector3(Vector3{0.0f, 1.0f, 0.0f}, norm);
                    Matrix rot_m = QuaternionToMatrix(q_up);
                    float rot_m9[9] = {
                        rot_m.m0, rot_m.m1, rot_m.m2,
                        rot_m.m4, rot_m.m5, rot_m.m6,
                        rot_m.m8, rot_m.m9, rot_m.m10
                    };
                    ruby::gizmo::RubyGizmo::rotation_matrix_to_euler(rot_m9, rot_deg);
                }
            }
        }

        // Write back gizmo output into the scene object.
        o.pos_x   = pos[0];     o.pos_y   = pos[1];     o.pos_z   = pos[2];
        o.rot_x   = rot_deg[0]; o.rot_z   = rot_deg[1]; o.rot_y   = rot_deg[2];
        o.scale_x = scl[0];     o.scale_y = scl[1];     o.scale_z = scl[2];
        m_gizmo_edited = true;
        sync_edited_object_caches();
        emit sceneObjectTransformed(sel, o.pos_x, o.pos_y, o.pos_z,
                                   o.rot_x, o.rot_z, o.rot_y,
                                   o.scale_x, o.scale_y, o.scale_z);
    }
}

void Viewport3DWidget::focus_object(int index) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size())) return;
    const auto& o = m_scene.objects[index];
    m_cam_target[0] = o.pos_x;
    m_cam_target[1] = o.pos_y;
    m_cam_target[2] = o.pos_z;
    // Keep the user's current zoom distance (the old forced 50–300 clamp
    // snapped the camera every focus click).
    update();
}

void Viewport3DWidget::set_scene_object_visibility(int index, bool visible) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size())) return;
    const std::string before = capture_scene_snapshot();
    m_scene.objects[index].hidden = !visible;
    for (auto& ro : m_render_objects) {
        if (ro.object_index == index) ro.hidden = !visible;
    }
    sync_edited_object_caches();
    update();
    emit sceneObjectRenderStateChanged(index);
    emit sceneEdited();
    push_scene_snapshot_undo(before, QString("Toggle Visibility %1").arg(index));
}

void Viewport3DWidget::delete_scene_object(int index) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size())) return;
    const std::string before = capture_scene_snapshot();
    // Deleting the object being mesh-edited aborts the session (unapplied).
    if (m_mesh_edit && m_mesh_edit_object == index) end_mesh_edit(false);
    m_scene.objects.erase(m_scene.objects.begin() + index);

    // Re-index render objects
    m_render_objects.erase(
        std::remove_if(m_render_objects.begin(), m_render_objects.end(),
                       [index](const SceneRenderObject& ro) { return ro.object_index == index; }),
        m_render_objects.end());
    for (auto& ro : m_render_objects) {
        if (ro.object_index > index) ro.object_index--;
    }

    if (m_selected_scene_object == index) {
        set_selected_object(-1);
        emit sceneObjectSelected(-1);
    } else if (m_selected_scene_object > index) {
        set_selected_object(m_selected_scene_object - 1);
        emit sceneObjectSelected(m_selected_scene_object);
    }
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, QString("Delete Object %1").arg(index));
}

void Viewport3DWidget::duplicate_scene_object(int index) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size())) return;
    const std::string before = capture_scene_snapshot();
    av::SceneObject dup = m_scene.objects[index];
    dup.name += "_copy";
    dup.pos_x += 10.0f;
    dup.pos_y += 5.0f;
    m_scene.objects.push_back(dup);

    int new_idx = static_cast<int>(m_scene.objects.size() - 1);
    SceneRenderObject ro;
    ro.pos[0] = dup.pos_x; ro.pos[1] = dup.pos_y; ro.pos[2] = dup.pos_z;
    swk::object_world_matrix(dup, ro.world_matrix);
    swk::object_render_matrix(dup, ro.render_matrix);
    ro.hidden = dup.hidden;
    ro.is_portal = dup.is_portal;
    ro.is_spawn_point = dup.is_spawn_point;
    ro.is_camera = dup.is_camera;
    ro.is_dimension_object = dup.is_dimension_object;
    ro.object_index = new_idx;
    ro.name = dup.name;
    ro.mesh_name = dup.mesh_name;
    ro.local_aabb = dup.local_aabb;
    ro.has_diffuse_color = dup.has_model_diffuse_color;
    ro.diffuse_color[0] = dup.model_diffuse_color[0];
    ro.diffuse_color[1] = dup.model_diffuse_color[1];
    ro.diffuse_color[2] = dup.model_diffuse_color[2];
    m_render_objects.push_back(std::move(ro));

    set_selected_object(new_idx);
    emit sceneObjectSelected(new_idx);
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, QString("Duplicate Object %1").arg(index));
}

// ============================================================================
// Scene-object clipboard + selection utilities (ImGui asset_viewer parity)
//   Ctrl+C / Ctrl+V / Ctrl+D / Delete / Alt+Up-Down. Copy is a RAM clipboard
//   that survives scene switches; paste always uses scene_fresh_identifier and
//   reloads model/background caches so cross-scene pastes render immediately.
// ============================================================================

void Viewport3DWidget::copy_scene_selection() {
    if (!has_scene() || m_selected_scene_object < 0 ||
        m_selected_scene_object >= static_cast<int>(m_scene.objects.size()))
        return;
    m_scene_clipboard.clear();
    m_scene_clipboard.push_back(m_scene.objects[m_selected_scene_object]);
    m_paste_count = 0;   // next paste starts the cascade from +24 again
}

void Viewport3DWidget::paste_scene_selection() {
    if (!has_scene() || m_scene_clipboard.empty()) return;
    const std::string before = capture_scene_snapshot();

    // Ground-mesh GPU upload needs a current GL context; model/background
    // texture loads use it too. Refuse when no surface can be created at all.
    if (!ensure_gl_ready()) return;
    makeCurrent();

    const float nudge = 24.0f * static_cast<float>(++m_paste_count);
    const size_t paste_start = m_scene.objects.size();
    std::vector<int> pasted_indices;
    pasted_indices.reserve(m_scene_clipboard.size());

    for (const auto& copied : m_scene_clipboard) {
        av::SceneObject pasted = copied;
        pasted.name = av::scene_fresh_identifier(m_scene);
        pasted.pos_x += nudge;
        pasted.pos_y += nudge;
        m_scene.objects.push_back(std::move(pasted));
        const int idx = static_cast<int>(m_scene.objects.size() - 1);

        // The copied object may come from a different scene whose resources
        // were evicted when select_file() ran — reload so the pasted object
        // actually renders (same fix as the ImGui viewer's cross-scene paste).
        // Same-scene paste is a no-op: the names are already in the caches.
        ensure_pasted_model_resources(m_scene.objects[idx]);
        ensure_pasted_background_resources(m_scene.objects[idx].background_name);
        pasted_indices.push_back(idx);
    }

    for (int idx : pasted_indices)
        append_render_object_for_index(idx);

    doneCurrent();
    set_selected_object(pasted_indices.back());
    emit sceneObjectSelected(pasted_indices.back());
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, "Paste Object");
}

void Viewport3DWidget::duplicate_scene_selection() {
    // ImGui parity: duplicate = copy + paste (so a following Ctrl+V pastes the
    // duplicate again, cascading by the nudge).
    if (!has_scene() || m_selected_scene_object < 0) return;
    copy_scene_selection();
    paste_scene_selection();
}

void Viewport3DWidget::delete_scene_selection() {
    if (!has_scene() || m_selected_scene_object < 0 ||
        m_selected_scene_object >= static_cast<int>(m_scene.objects.size()))
        return;
    delete_scene_object(m_selected_scene_object);   // fixes selection + emits sceneEdited
}

void Viewport3DWidget::move_scene_object(int direction) {
    if (!has_scene()) return;
    const int from = m_selected_scene_object;
    const int to = from + direction;
    if (from < 0 || to < 0 || to >= static_cast<int>(m_scene.objects.size())) return;
    const std::string before = capture_scene_snapshot();

    av::scene_move_object(m_scene, static_cast<size_t>(from), static_cast<size_t>(to));

    // Reindex the parallel render-object array: the moved object takes `to`;
    // the objects between the two slots shift one step toward the vacated one.
    for (auto& ro : m_render_objects) {
        if (ro.object_index == from) {
            ro.object_index = to;
        } else if (direction > 0 && ro.object_index > from && ro.object_index <= to) {
            --ro.object_index;
        } else if (direction < 0 && ro.object_index >= to && ro.object_index < from) {
            ++ro.object_index;
        }
    }

    set_selected_object(to);
    emit sceneObjectSelected(to);
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, QString("Reorder Object").arg(to));
}

std::vector<fs::path> Viewport3DWidget::current_scene_roots() const {
    // Mirror of the root list load_scene builds (project dir + imported libs)
    // so paste resolves models/textures exactly like a fresh scene load.
    std::vector<fs::path> scene_roots;
    const std::string project_dir = ruby::core::ProjectContext::instance().project_dir();
    if (!project_dir.empty()) {
        const fs::path p(project_dir);
        scene_roots.push_back(p);
        scene_roots.push_back(p / "resources");
        scene_roots.push_back(p / "models");
        scene_roots.push_back(p / "assets");
        scene_roots.push_back(p / "assets" / "resources");
        scene_roots.push_back(p / "assets" / "models");
    }
    for (const auto& lib_path : m_scene.imported_library_paths) {
        if (lib_path.empty()) continue;
        const fs::path lp(lib_path);
        scene_roots.push_back(lp.parent_path());
        scene_roots.push_back(lp.parent_path() / "models");
        scene_roots.push_back(lp.parent_path() / "resources");
        scene_roots.push_back(lp.parent_path().parent_path());
        scene_roots.push_back(lp.parent_path().parent_path() / "resources");
        scene_roots.push_back(lp.parent_path().parent_path() / "models");
    }
    return scene_roots;
}

void Viewport3DWidget::ensure_pasted_model_resources(const av::SceneObject& obj) {
    const std::string& mesh_name = obj.mesh_name;
    if (mesh_name.empty() || m_scene_models.count(mesh_name) > 0) return;

    const std::vector<fs::path> scene_roots = current_scene_roots();
    const fs::path pod_path =
        av::assets::resolve_pod(fs::path(m_current_scene_path), mesh_name, scene_roots);
    if (pod_path.empty()) return;

    av::PODModel model = load_pod_to_ram(pod_path.string(), obj.template_name);
    if (model.meshes.empty()) return;
    m_scene_models.emplace(mesh_name, model);

    if (!ensure_gl_ready()) return;
    makeCurrent();
    std::vector<MeshGpu> gpus = upload_pod_gpu(pod_path.string(), model);
    for (const auto& g : gpus)
        if (g.valid()) m_all_mesh_gpu.push_back(g);
    m_scene_model_gpu.emplace(mesh_name, std::move(gpus));
    m_scene_model_textures.emplace(mesh_name, load_pod_textures(pod_path, model, scene_roots));
    doneCurrent();
}

void Viewport3DWidget::ensure_pasted_background_resources(const std::string& bg_name) {
    if (bg_name.empty() || m_scene_background_textures.count(bg_name) > 0) return;

    // Candidate-name variants + roots + suffixes mirror the load path exactly.
    std::vector<std::string> name_variants = {bg_name};
    const std::string stripped = strip_image_extensions(bg_name);
    if (!stripped.empty() && stripped != bg_name) name_variants.push_back(stripped);
    if (stripped.size() > 3 && stripped.rfind("_2x") == stripped.size() - 3)
        name_variants.push_back(stripped.substr(0, stripped.size() - 3));
    else
        name_variants.push_back(stripped + "_2x");

    static const char* suffixes[] = {
        "_2x.tex.png", ".tex.png", "_2x.pvr", ".pvr", "_2x.tex", ".tex", "_2x.png", ".png", ""
    };
    std::vector<fs::path> roots = current_scene_roots();
    const QString home = QDir::homePath();
    roots.push_back(fs::path(home.toStdString()) / "resources");
    roots.push_back(fs::path(home.toStdString()) / "SwordigoRefresh" / "assets" / "resources");
    roots.push_back(fs::path(home.toStdString()) / "SwordigoDesktop" / "assets");
    roots.push_back(fs::path(home.toStdString()) / "SwordigoDesktop" / "resources");
    roots.push_back(fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets");
    roots.push_back(fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "resources");
    roots.push_back(fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "background");
    roots.push_back(fs::path(home.toStdString()) / ".local" / "share" / "swordigo-desktop" / "assets" / "resources" / "background");

    GLuint texture = 0;
    for (const auto& root : roots) {
        for (const auto& name_var : name_variants) {
            for (const char* suffix : suffixes) {
                const fs::path candidate = root / (name_var + suffix);
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) {
                    texture = load_texture_any(candidate.string());
                    if (texture) break;
                }
            }
            if (texture) break;
        }
        if (texture) break;
    }
    m_scene_background_textures.emplace(bg_name, texture);
}

int Viewport3DWidget::append_render_object_for_index(int index) {
    if (index < 0 || index >= static_cast<int>(m_scene.objects.size())) return -1;
    const av::SceneObject& obj = m_scene.objects[index];

    SceneRenderObject ro;
    ro.pos[0] = obj.pos_x; ro.pos[1] = obj.pos_y; ro.pos[2] = obj.pos_z;
    swk::object_world_matrix(obj, ro.world_matrix);
    swk::object_render_matrix(obj, ro.render_matrix);
    ro.hidden = obj.hidden;
    ro.is_portal = obj.is_portal;
    ro.is_spawn_point = obj.is_spawn_point;
    ro.is_camera = obj.is_camera;
    ro.is_dimension_object = obj.is_dimension_object;
    ro.object_index = index;
    ro.name = obj.name;
    ro.mesh_name = obj.mesh_name;
    ro.local_aabb = obj.local_aabb;
    ro.has_diffuse_color = obj.has_model_diffuse_color;
    ro.diffuse_color[0] = obj.model_diffuse_color[0];
    ro.diffuse_color[1] = obj.model_diffuse_color[1];
    if (m_scene_ground_textures.size() <= static_cast<size_t>(index))
        m_scene_ground_textures.resize(index + 1);

    if (!obj.ground_meshes.empty()) {
        auto& textures = m_scene_ground_textures[index];
        textures.resize(obj.ground_meshes.size(), 0);
        const std::vector<fs::path> scene_roots = current_scene_roots();
        for (size_t mi = 0; mi < obj.ground_meshes.size(); ++mi) {
            const std::string name =
                mi < obj.ground_mesh_textures.size() ? obj.ground_mesh_textures[mi] : std::string();
            if (name.empty()) continue;
            for (const auto& candidate : av::assets::texture_candidates(fs::path(m_current_scene_path), name, scene_roots)) {
                if (fs::exists(candidate)) {
                    GLuint tex = load_texture_any(candidate.string());
                    if (tex) {
                        textures[mi] = tex;
                        break;
                    }
                }
            }
        }
        ro.ground_textures = textures;
        ro.ground_tex_names.reserve(obj.ground_meshes.size());
        for (size_t gi = 0; gi < obj.ground_meshes.size(); ++gi)
            ro.ground_tex_names.push_back(gi < obj.ground_mesh_textures.size()
                                             ? obj.ground_mesh_textures[gi] : std::string());
        ro.ground_gpu.reserve(obj.ground_meshes.size());
        for (const auto& gmesh : obj.ground_meshes) {
            MeshGpu g = upload_mesh_gpu(gmesh);   // scene-embedded; no pod cache
            if (g.valid()) {
                ro.ground_gpu.push_back(g);
                m_all_mesh_gpu.push_back(g);
            } else {
                ro.ground_gpu.push_back(MeshGpu{});
            }
        }
    }

    m_render_objects.push_back(std::move(ro));
    return index;
}

void Viewport3DWidget::rebuild_render_object_for_index(int index) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size()))
        return;
    ensure_gl_ready();
    makeCurrent();
    // Drop the stale entry (find by object_index — the array parallels the
    // scene, but defensive lookup costs nothing) and splice a fresh build at
    // the same position so draw order and selection stay stable.
    auto it = std::find_if(m_render_objects.begin(), m_render_objects.end(),
                           [index](const SceneRenderObject& ro) {
                               return ro.object_index == index;
                           });
    const size_t pos = it == m_render_objects.end()
                           ? m_render_objects.size()
                           : static_cast<size_t>(std::distance(m_render_objects.begin(), it));
    if (it != m_render_objects.end()) m_render_objects.erase(it);
    append_render_object_for_index(index);
    if (pos < m_render_objects.size()) {   // appended at the end; splice to pos
        SceneRenderObject fresh = std::move(m_render_objects.back());
        m_render_objects.pop_back();
        m_render_objects.insert(m_render_objects.begin() +
                                    static_cast<std::ptrdiff_t>(pos),
                                std::move(fresh));
    }
    doneCurrent();
}

// ============================================================================
// In-scene mesh editor (ImGui asset_viewer parity, "3D projection lock")
//   Select a ground-mesh object and arm Mesh Edit (toolbar / M). The camera
//   snaps to the polygon's own plane — pitch 0, yaw from rot_y, target at the
//   polygon centre, distance fitted — a pure 2D front view along local Z.
//   Drag vertices, RMB an edge to insert a node (and keep dragging it), RMB/Del
//   a vertex to remove it, G toggles grid snap, arrows nudge, MMB pans,
//   wheel zooms. Esc/M/Enter commits (regenerates the GroundMesh through
//   boulder and re-uploads the GPU buffers live); R or Ctrl+Z reverts the
//   whole session to the pre-edit snapshot.
// ============================================================================

bool Viewport3DWidget::can_mesh_edit() const {
    if (!has_scene() || m_selected_scene_object < 0 ||
        m_selected_scene_object >= (int)m_scene.objects.size())
        return false;
    // Any object with embedded ground meshes is editable (the polygon comes
    // from its GroundPolygonComponent when present, else from mesh vertices).
    return !m_scene.objects[m_selected_scene_object].ground_meshes.empty();
}

void Viewport3DWidget::set_mesh_edit(bool on) {
    if (on) {
        if (!m_mesh_edit) begin_mesh_edit();
        if (m_mesh_btn) m_mesh_btn->setChecked(m_mesh_edit);
    } else {
        if (m_mesh_edit) end_mesh_edit(true);
        if (m_mesh_btn) m_mesh_btn->setChecked(false);
    }
}

av::Camera Viewport3DWidget::mesh_camera() const {
    av::Camera cam;
    cam.yaw = m_cam_yaw;
    cam.pitch = m_cam_pitch;
    cam.distance = m_cam_dist;
    cam.target[0] = m_cam_target[0];
    cam.target[1] = m_cam_target[1];
    cam.target[2] = m_cam_target[2];
    cam.fov = 45.0f;
    cam.near_plane = std::max(0.1f, m_cam_dist * 0.001f);
    cam.far_plane = std::max(10000.0f, m_cam_dist * 24.0f);
    return cam;
}

bool Viewport3DWidget::mesh_local_to_screen(const av::SceneObject& obj, double lx, double ly,
                                            QPointF& out) const {
    float obj_mat[16];
    swk::object_world_matrix(obj, obj_mat);
    const float wp[3] = { obj_mat[0]*(float)lx + obj_mat[4]*(float)ly + obj_mat[12],
                          obj_mat[1]*(float)lx + obj_mat[5]*(float)ly + obj_mat[13],
                          obj_mat[2]*(float)lx + obj_mat[6]*(float)ly + obj_mat[14] };
    ImVec2 s;
    if (!swk::world_to_screen(mesh_camera(), std::max(1, width()), std::max(1, height()),
                              ImVec2(0, 0), wp, s))
        return false;
    out = QPointF(s.x, s.y);
    return true;
}

bool Viewport3DWidget::mesh_screen_ray(const QPointF& px, float origin[3], float dir[3]) const {
    swk::screen_ray(mesh_camera(), std::max(1, width()), std::max(1, height()),
                    ImVec2(0, 0), ImVec2((float)px.x(), (float)px.y()), origin, dir);
    return true;
}

bool Viewport3DWidget::mesh_ray_object_plane(const av::SceneObject& obj, const float origin[3],
                                             const float dir[3], double& lx, double& ly) const {
    float mat[16];
    swk::object_world_matrix(obj, mat);
    const float px = mat[12], py = mat[13], pz = mat[14];
    const float ax = mat[0], ay = mat[1], az = mat[2];
    const float bx = mat[4], by = mat[5], bz = mat[6];
    float nx = ay*bz - az*by, ny = az*bx - ax*bz, nz = ax*by - ay*bx;
    const float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (nl < 1e-6f) return false;
    nx /= nl; ny /= nl; nz /= nl;
    const float denom = nx*dir[0] + ny*dir[1] + nz*dir[2];
    if (std::fabs(denom) < 1e-5f) return false;
    const float t = (nx*(px - origin[0]) + ny*(py - origin[1]) + nz*(pz - origin[2])) / denom;
    if (t < 0.0f) return false;
    const float hx = origin[0] + dir[0]*t - px;
    const float hy = origin[1] + dir[1]*t - py;
    const float hz = origin[2] + dir[2]*t - pz;
    const float al2 = ax*ax + ay*ay + az*az;
    const float bl2 = bx*bx + by*by + bz*bz;
    if (al2 < 1e-8f || bl2 < 1e-8f) return false;
    lx = (hx*ax + hy*ay + hz*az) / al2;
    ly = (hx*bx + hy*by + hz*bz) / bl2;
    return true;
}

// Import an existing GroundMesh object's polygon into the editor.
// Authoritatively extracts TextureMapping, GroundMeshGenerator parameters
// (surface width, random seed, hat offsets), Min/Max depth, and component IDs
// to prevent metadata loss and save corruption on apply.
void Viewport3DWidget::mesh_import(int idx) {
    m_mesh_points.clear();
    if (idx < 0 || idx >= (int)m_scene.objects.size()) return;
    const av::SceneObject& obj = m_scene.objects[idx];
    float min_depth = -45.0f, max_depth = 45.0f;
    bool have_depth = false;

    const auto& comps = obj.components.empty() ? obj.resolved_components : obj.components;

    boulder::GroundComponentIds target_ids;
    uint32_t surface_tm_id = 0, front_tm_id = 0;
    float surface_width = 80.0f;
    float hat_height = 25.0f;
    float hat_offset_1 = 5.0f;
    float hat_offset_2 = 5.0f;
    uint32_t random_seed = 1291618994u;

    // Pass 1: Extract GroundPolygon, GroundMesh, GroundMeshGenerator, CollisionShape
    for (const auto& comp : comps) {
        const int payload = comp.payload_field;
        // GroundPolygon (110)
        if (payload == 110 || comp.type_name == "GroundPolygon") {
            if (comp.type_id > 0) target_ids.polygon_id = comp.type_id;
            try {
                proto::Reader wrapper(comp.raw_data);
                proto::Field f;
                while (wrapper.read_field(f)) {
                    if (f.field_number != 110 || f.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader gpc(f.bytes_val);
                    proto::Field g;
                    while (gpc.read_field(g)) {
                        if (g.field_number == 4 && g.wire_type == proto::WIRE_I32) {
                            min_depth = g.float_val; have_depth = true; continue;
                        } else if (g.field_number == 5 && g.wire_type == proto::WIRE_I32) {
                            max_depth = g.float_val; have_depth = true; continue;
                        } else if (g.field_number != 2 || g.wire_type != proto::WIRE_LEN) {
                            continue;
                        }
                        proto::Reader poly(g.bytes_val);
                        proto::Field p;
                        while (poly.read_field(p)) {
                            if (p.field_number != 1 || p.wire_type != proto::WIRE_LEN) continue;  // Vertex
                            proto::Reader vec2(p.bytes_val);
                            proto::Field v;
                            float x = 0, y = 0;
                            while (vec2.read_field(v)) {
                                if (v.field_number == 1) x = v.float_val;
                                else if (v.field_number == 2) y = v.float_val;
                            }
                            m_mesh_points.push_back({x, y});
                        }
                    }
                }
            } catch (...) {}
        }
        // GroundMesh (111)
        else if (payload == 111 || comp.type_name == "GroundMesh") {
            if (comp.type_id > 0) target_ids.mesh_id = comp.type_id;
        }
        // GroundMeshGenerator (112)
        else if (payload == 112 || comp.type_name == "GroundMeshGenerator") {
            if (comp.type_id > 0) target_ids.generator_id = comp.type_id;
            try {
                proto::Reader wrapper(comp.raw_data);
                proto::Field f;
                while (wrapper.read_field(f)) {
                    if (f.field_number != 112 || f.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader ggc(f.bytes_val);
                    proto::Field g;
                    while (ggc.read_field(g)) {
                        if (g.field_number == 3) front_tm_id = static_cast<uint32_t>(g.varint_val);
                        else if (g.field_number == 4) surface_tm_id = static_cast<uint32_t>(g.varint_val);
                        else if (g.field_number == 5) random_seed = static_cast<uint32_t>(g.varint_val);
                        else if (g.field_number == 8 && g.wire_type == proto::WIRE_I32) surface_width = g.float_val;
                        else if (g.field_number == 9 && g.wire_type == proto::WIRE_I32) hat_height = g.float_val;
                        else if (g.field_number == 10 && g.wire_type == proto::WIRE_I32) hat_offset_1 = g.float_val;
                        else if (g.field_number == 11 && g.wire_type == proto::WIRE_I32) hat_offset_2 = g.float_val;
                    }
                }
            } catch (...) {}
        }
        // CollisionShape (120/121)
        else if (payload == 120 || payload == 121 || comp.type_name == "CollisionShape") {
            if (comp.type_id > 0) target_ids.collision_id = comp.type_id;
        }
    }

    if (surface_tm_id > 0) target_ids.tm_surface_id = static_cast<int>(surface_tm_id);
    if (front_tm_id > 0) target_ids.tm_front_id = static_cast<int>(front_tm_id);

    // Pass 2: Extract TextureMapping (113) matching surface_tm_id and front_tm_id
    std::string top_tex_from_tm, front_tex_from_tm;
    float scale_from_tm = 250.0f;
    for (const auto& comp : comps) {
        const int payload = comp.payload_field;
        if (payload == 113 || comp.type_name == "TextureMapping") {
            const uint32_t tm_id = static_cast<uint32_t>(comp.type_id);
            std::string tname;
            float tscale = 250.0f;
            try {
                proto::Reader wrapper(comp.raw_data);
                proto::Field f;
                while (wrapper.read_field(f)) {
                    if (f.field_number != 113 || f.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader tmc(f.bytes_val);
                    proto::Field g;
                    while (tmc.read_field(g)) {
                        if (g.field_number == 1 && g.wire_type == proto::WIRE_LEN) tname = g.bytes_val;
                        else if (g.field_number == 2 && g.wire_type == proto::WIRE_I32) tscale = g.float_val;
                    }
                }
            } catch (...) {}
            if (tm_id == surface_tm_id || (surface_tm_id == 0 && top_tex_from_tm.empty())) {
                top_tex_from_tm = tname;
                scale_from_tm = tscale;
                if (comp.type_id > 0) target_ids.tm_surface_id = comp.type_id;
            } else if (tm_id == front_tm_id || (front_tm_id == 0 && front_tex_from_tm.empty())) {
                front_tex_from_tm = tname;
                if (comp.type_id > 0) target_ids.tm_front_id = comp.type_id;
            }
        }
    }

    std::string top_tex = top_tex_from_tm;
    std::string bottom_tex = front_tex_from_tm;

    // Fallback to mesh textures if TextureMapping was not populated
    if (top_tex.empty() || bottom_tex.empty()) {
        const size_t n = std::min({obj.ground_meshes.size(), obj.ground_mesh_textures.size(),
                                   obj.ground_mesh_fields.size()});
        for (size_t i = 0; i < n; ++i) {
            const std::string& tex = obj.ground_mesh_textures[i];
            if (tex.empty()) continue;
            const int src_field = obj.ground_mesh_fields[i];
            if (src_field == 9) {
                if (bottom_tex.empty()) bottom_tex = tex;
            } else if (src_field == 8) {
                if (top_tex.empty()) top_tex = tex;
                else if (bottom_tex.empty()) bottom_tex = tex;
            } else if (src_field == 6 && bottom_tex.empty()) {
                bottom_tex = tex;
            }
        }
    }
    if (top_tex.empty()) top_tex = bottom_tex;
    if (bottom_tex.empty()) bottom_tex = top_tex;
    if (top_tex.empty()) top_tex = "fire_grass";
    if (bottom_tex.empty()) bottom_tex = "graveyard_ground";

    // Fallback: unique XY positions from the embedded ground meshes.
    if (m_mesh_points.size() < 3) {
        for (const auto& gm : obj.ground_meshes) {
            for (size_t i = 0; i + 2 < gm.positions.size(); i += 3) {
                const double x = gm.positions[i], y = gm.positions[i + 1];
                bool dup = false;
                for (const auto& pt : m_mesh_points)
                    if (std::fabs(pt.x - x) < 0.01 && std::fabs(pt.y - y) < 0.01) { dup = true; break; }
                if (!dup && m_mesh_points.size() < 64) m_mesh_points.push_back({x, y});
            }
            if (m_mesh_points.size() >= 3) break;
        }
    }
    if (m_mesh_points.size() < 3) return;

    if (!have_depth) {
        float dmin = 1e30f, dmax = -1e30f;
        for (const auto& pm : obj.ground_meshes) {
            dmin = std::min(dmin, pm.min_z); dmax = std::max(dmax, pm.max_z);
        }
        if (dmin < 1e29f && dmax > -1e29f) { min_depth = dmin; max_depth = dmax; }
    }

    m_mesh_params = boulder::GroundMesh{};
    m_mesh_params.polygon = m_mesh_points;
    m_mesh_params.min_depth = min_depth;
    m_mesh_params.max_depth = max_depth;
    m_mesh_params.z = obj.pos_z;
    m_mesh_params.top_texture = top_tex;
    m_mesh_params.bottom_texture = bottom_tex;
    m_mesh_params.surface_width = surface_width;
    m_mesh_params.hat_height = hat_height;
    m_mesh_params.hat_width_offset_1 = hat_offset_1;
    m_mesh_params.hat_width_offset_2 = hat_offset_2;
    m_mesh_params.texture_scale = scale_from_tm;
    m_mesh_params.random_seed = random_seed;
    m_mesh_ids = target_ids;
    m_mesh_z = obj.pos_z;
}

void Viewport3DWidget::begin_mesh_edit() {
    if (!has_scene() || m_selected_scene_object < 0 ||
        m_selected_scene_object >= (int)m_scene.objects.size())
        return;
    const int idx = m_selected_scene_object;
    mesh_import(idx);
    if (m_mesh_points.size() < 3) return;

    m_mesh_scene_saved = m_scene;
    m_mesh_scene_saved_valid = true;
    m_mesh_dirty = false;
    m_mesh_drag_point = -1;
    m_mesh_dragging = false;
    m_mesh_drag_from_insert = false;
    m_mesh_hover_vertex = -1;
    m_mesh_hover_edge = -1;
    m_mesh_edit_object = idx;

    // Save the camera, then lock it to the polygon plane (projection lock).
    m_mesh_saved_cam_pitch = m_cam_pitch;
    m_mesh_saved_cam_yaw = m_cam_yaw;
    m_mesh_saved_cam_dist = m_cam_dist;
    m_mesh_saved_cam_target[0] = m_cam_target[0];
    m_mesh_saved_cam_target[1] = m_cam_target[1];
    m_mesh_saved_cam_target[2] = m_cam_target[2];
    m_mesh_saved_cam_valid = true;

    const av::SceneObject& obj = m_scene.objects[idx];
    float obj_mat[16];
    swk::object_world_matrix(obj, obj_mat);
    double minx = 1e30, miny = 1e30, minz = 1e30;
    double maxx = -1e30, maxy = -1e30, maxz = -1e30;
    for (const auto& pt : m_mesh_points) {
        const float lp[3] = {(float)pt.x, (float)pt.y, 0.0f};
        const float wx = obj_mat[0]*lp[0] + obj_mat[4]*lp[1] + obj_mat[8]*lp[2] + obj_mat[12];
        const float wy = obj_mat[1]*lp[0] + obj_mat[5]*lp[1] + obj_mat[9]*lp[2] + obj_mat[13];
        const float wz = obj_mat[2]*lp[0] + obj_mat[6]*lp[1] + obj_mat[10]*lp[2] + obj_mat[14];
        minx = std::min(minx, (double)wx); maxx = std::max(maxx, (double)wx);
        miny = std::min(miny, (double)wy); maxy = std::max(maxy, (double)wy);
        minz = std::min(minz, (double)wz); maxz = std::max(maxz, (double)wz);
    }
    m_cam_target[0] = (float)((minx + maxx) * 0.5);
    m_cam_target[1] = (float)((miny + maxy) * 0.5);
    m_cam_target[2] = (float)((minz + maxz) * 0.5);
    constexpr double kPi = 3.14159265358979;
    const double rot_y_deg = obj.rot_y * 180.0 / kPi;
    m_cam_yaw = (float)(-rot_y_deg);
    m_cam_pitch = 0.0f;
    const double fit = std::max({maxx - minx, maxy - miny, 1.0});
    const double vfov = 45.0 * kPi / 180.0;
    m_cam_dist = (float)std::clamp((fit * 0.5) / std::tan(vfov * 0.5) * 1.1, 3.0, 5000.0);

    m_mesh_edit = true;
    set_gizmo_mode(GizmoOff);
    m_mesh_live_timer.start();
    update();
}

// Re-upload the edited object's ground-mesh GPU buffers, reusing GL textures
// whose names are unchanged since the last apply (the live preview regenerates
// ~11x/sec and must not re-decode from disk every tick). GL must be current.
void Viewport3DWidget::mesh_resync_gpu(int idx) {
    if (idx < 0 || idx >= (int)m_scene.objects.size()) return;
    const av::SceneObject& obj = m_scene.objects[idx];

    for (auto& ro : m_render_objects) {
        if (ro.object_index != idx) continue;

        const std::vector<GLuint> old_tex = ro.ground_textures;
        const std::vector<std::string> old_names = ro.ground_tex_names;

        for (auto& g : ro.ground_gpu) free_mesh_gpu(g);
        ro.ground_gpu.clear();
        ro.ground_textures.clear();
        ro.ground_tex_names.clear();

        if (m_scene_ground_textures.size() <= static_cast<size_t>(idx))
            m_scene_ground_textures.resize(idx + 1);
        auto& session_tex = m_scene_ground_textures[idx];
        session_tex.assign(obj.ground_meshes.size(), 0);

        const std::vector<fs::path> scene_roots = current_scene_roots();
        for (size_t mi = 0; mi < obj.ground_meshes.size(); ++mi) {
            const std::string name = mi < obj.ground_mesh_textures.size()
                                         ? obj.ground_mesh_textures[mi] : std::string();
            GLuint tex = 0;
            if (mi < old_names.size() && mi < old_tex.size() &&
                old_names[mi] == name && old_tex[mi]) {
                tex = old_tex[mi];
            }
            if (!tex && !name.empty()) {
                for (const auto& candidate :
                     av::assets::texture_candidates(fs::path(m_current_scene_path), name, scene_roots)) {
                    if (fs::exists(candidate)) {
                        tex = load_texture_any(candidate.string());
                        if (tex) break;
                    }
                }
            }
            ro.ground_textures.push_back(tex);
            ro.ground_tex_names.push_back(name);
            session_tex[mi] = tex;

            MeshGpu g = upload_mesh_gpu(obj.ground_meshes[mi]);
            if (g.valid()) {
                ro.ground_gpu.push_back(g);
                m_all_mesh_gpu.push_back(g);
            } else {
                ro.ground_gpu.push_back(MeshGpu{});
            }
        }
        // NOTE: old textures are deliberately NOT deleted here. Every ground
        // texture id comes from load_texture_any(), which caches ONE GL id per
        // path in m_gpu_tex_cache — the same id is shared by every object and
        // scene session that uses that file (and by the cache itself). Deleting
        // it would blank the texture for all of them (the "white texture" bug).
        // The cache owns the ids; they are freed together at teardown. Dropping
        // the reference here is enough — the next apply reuses ids whose names
        // are unchanged, so live ticks never re-decode from disk.
        break;
    }
    update();
}

// Regenerate the object's GroundMesh geometry from the live polygon through
// boulder (the real backend), swap the ground components + meshes into the
// object, preserve every non-ground component, and re-upload the GPU buffers.
bool Viewport3DWidget::mesh_apply() {
    if (m_mesh_edit_object < 0 || m_mesh_edit_object >= (int)m_scene.objects.size()) return false;
    if (m_mesh_points.size() < 3) return false;

    boulder::GroundMesh gm = m_mesh_params;
    gm.polygon = m_mesh_points;
    boulder::ensure_ccw(gm.polygon);
    // Generator choice — boulderx::set_ground_generator() or
    // RUBY_GROUND_GENERATOR=boulderx (boulder stays the default). boulderx is
    // the faithful Caver port that also handles concave outlines, arcs and
    // per-node depth. It declines requests it cannot reproduce exactly (dome
    // hats today), and we fall straight back to boulder rather than emit an
    // object with geometry missing.
    std::string bin;
    if (boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX) {
        std::vector<std::pair<double, double>> xy;
        xy.reserve(gm.polygon.size());
        for (const auto& p : gm.polygon) xy.emplace_back(p.x, p.y);
        const int ids6[6] = {m_mesh_ids.polygon_id, m_mesh_ids.mesh_id,
                             m_mesh_ids.generator_id, m_mesh_ids.collision_id,
                             m_mesh_ids.tm_surface_id, m_mesh_ids.tm_front_id};
        bin = boulderx::generate_ground_mesh_object_flat(
            xy, 1, std::fabs(gm.max_depth), gm.top_texture, gm.bottom_texture,
            m_scene.objects[m_mesh_edit_object].name, !gm.hats.empty(), ids6);
    }
    if (bin.empty()) {
        const std::string swdm = boulder::serialize_swdm(gm);
        bin = boulder::generate_ground_mesh_object(
            swdm, m_scene.objects[m_mesh_edit_object].name, m_mesh_z, &m_mesh_ids);
    }
    if (bin.empty()) return false;

    std::vector<uint8_t> bytes(bin.begin(), bin.end());
    av::SceneData parsed;
    try {
        parsed = av::scene_load_bytes(bytes, m_current_scene_path);
    } catch (const std::exception&) {
        return false;
    }
    if (parsed.objects.empty()) return false;

    av::SceneObject& target = m_scene.objects[m_mesh_edit_object];
    const bool was_dim = target.is_dimension_object;
    av::SceneObject fresh = std::move(parsed.objects[0]);

    const auto is_ground_comp = [](const av::SceneComponent& c) {
        return c.payload_field == 110 || c.payload_field == 111 ||
               c.payload_field == 112 || c.payload_field == 113 ||
               c.payload_field == 120 || c.payload_field == 121 ||
               c.type_name == "GroundPolygon" || c.type_name == "GroundMesh" ||
               c.type_name == "GroundMeshGenerator" || c.type_name == "CollisionShape" ||
               c.type_name == "TextureMapping";
    };
    std::vector<av::SceneComponent> preserved;
    for (auto& c : target.components)
        if (!is_ground_comp(c)) preserved.push_back(std::move(c));
    target.components = std::move(fresh.components);
    for (auto& c : preserved) target.components.push_back(std::move(c));

    if (was_dim) {
        // 559, NOT 253 — see the note in ruby_quick_viewport.cpp.  253 is
        // FireEmitterComponent, so this probe was answering "does this object
        // have a fire?" and skipping the DimensionObject re-add for anything
        // that did.
        bool has_dim = false;
        for (const auto& c : target.components) {
            if (c.type_name == "DimensionObject" || c.payload_field == 559) {
                has_dim = true;
                break;
            }
        }
        if (!has_dim) {
            int instance_id = 1;
            for (const auto& c : target.components)
                instance_id = std::max(instance_id, c.type_id + 1);
            proto::Writer w;
            w.write_string_field(1, "DimensionObject");
            w.write_varint_field(2, static_cast<uint64_t>(instance_id));
            av::SceneComponent comp;
            comp.type_name = "DimensionObject";
            comp.type_id = instance_id;
            comp.raw_data = w.to_string();
            target.components.push_back(std::move(comp));
        }
    }

    target.resolved_components = target.components;
    target.pos_z = (float)m_mesh_z;
    av::scene_refresh(m_scene);
    // The re-parsed geometry (SurfaceMesh / FrontMesh / hats) wins over what
    // scene_refresh re-derived, so the edited polygon renders immediately.
    target.ground_meshes = std::move(fresh.ground_meshes);
    if (!fresh.ground_mesh_textures.empty())
        target.ground_mesh_textures = std::move(fresh.ground_mesh_textures);
    // scene_refresh does NOT re-derive the GroundPolygon fields, so write the
    // regenerated outline here — otherwise re-entering Mesh Edit after an apply
    // would import a stale polygon (and the collision/terrain helpers that do
    // read ground_polygon_points would see outdated walkable data).
    target.ground_polygon_points.clear();
    for (const auto& pt : gm.polygon) {
        target.ground_polygon_points.push_back((float)pt.x);
        target.ground_polygon_points.push_back((float)pt.y);
    }
    target.ground_polygon_min_depth = (float)gm.min_depth;
    target.ground_polygon_max_depth = (float)gm.max_depth;
    target.ground_polygon_collides  = true;
    target.ground_polygon_unsafe    = false;
    av::scene_mark_ground_mesh_dirty(m_scene, m_mesh_edit_object);

    if (ensure_gl_ready()) {
        makeCurrent();
        mesh_resync_gpu(m_mesh_edit_object);
        doneCurrent();
    }
    return true;
}

void Viewport3DWidget::end_mesh_edit(bool apply) {
    if (!m_mesh_edit) return;
    if (apply && m_mesh_dirty && m_mesh_edit_object >= 0) {
        const std::string before = capture_scene_snapshot();
        if (!mesh_apply()) return;   // keep the session open so it can be fixed
        emit sceneObjectRenderStateChanged(m_mesh_edit_object);
        emit sceneEdited();
        push_scene_snapshot_undo(before, QString("Edit Ground Mesh %1").arg(m_mesh_edit_object));
    }
    m_mesh_edit = false;
    m_mesh_edit_object = -1;
    m_mesh_drag_point = -1;
    m_mesh_dragging = false;
    m_mesh_drag_from_insert = false;
    m_mesh_dirty = false;
    m_mesh_scene_saved_valid = false;
    if (m_mesh_btn) m_mesh_btn->setChecked(false);
    if (m_mesh_saved_cam_valid) {
        m_cam_pitch = m_mesh_saved_cam_pitch;
        m_cam_yaw = m_mesh_saved_cam_yaw;
        m_cam_dist = m_mesh_saved_cam_dist;
        m_cam_target[0] = m_mesh_saved_cam_target[0];
        m_cam_target[1] = m_mesh_saved_cam_target[1];
        m_cam_target[2] = m_mesh_saved_cam_target[2];
        m_mesh_saved_cam_valid = false;
    }
    update();
}

// One-key undo for the whole session: restore the pre-edit scene snapshot and
// exit WITHOUT applying (the ImGui editor's R / Ctrl+Z contract).
void Viewport3DWidget::mesh_revert_session() {
    if (!m_mesh_edit) return;
    if (m_mesh_scene_saved_valid) {
        m_scene = m_mesh_scene_saved;
        av::scene_refresh(m_scene);
        const int idx = m_mesh_edit_object;
        if (idx >= 0 && idx < (int)m_scene.objects.size() && ensure_gl_ready()) {
            makeCurrent();
            mesh_resync_gpu(idx);
            doneCurrent();
        }
    }
    m_mesh_edit = false;
    m_mesh_edit_object = -1;
    m_mesh_drag_point = -1;
    m_mesh_dragging = false;
    m_mesh_drag_from_insert = false;
    m_mesh_dirty = false;
    m_mesh_scene_saved_valid = false;
    if (m_mesh_btn) m_mesh_btn->setChecked(false);
    if (m_mesh_saved_cam_valid) {
        m_cam_pitch = m_mesh_saved_cam_pitch;
        m_cam_yaw = m_mesh_saved_cam_yaw;
        m_cam_dist = m_mesh_saved_cam_dist;
        m_cam_target[0] = m_mesh_saved_cam_target[0];
        m_cam_target[1] = m_mesh_saved_cam_target[1];
        m_cam_target[2] = m_mesh_saved_cam_target[2];
        m_mesh_saved_cam_valid = false;
    }
    update();
}

// Hover targets: vertices win over edges, 11px / 10px grab radii (generous at
// any DPI). Off-screen vertices are skipped but their ORIGINAL indices are
// kept, so a vertex dragged beyond the viewport edge never aliases a wrong
// node.
void Viewport3DWidget::mesh_update_hover(const QPointF& px) {
    m_mesh_hover_vertex = -1;
    m_mesh_hover_edge = -1;
    if (m_mesh_edit_object < 0 || m_mesh_edit_object >= (int)m_scene.objects.size()) return;
    const av::SceneObject& obj = m_scene.objects[m_mesh_edit_object];
    if (m_mesh_points.size() < 3) return;

    struct SP { QPointF p; int oi; };
    std::vector<SP> pts;
    pts.reserve(m_mesh_points.size());
    for (int oi = 0; oi < (int)m_mesh_points.size(); ++oi) {
        QPointF s;
        if (mesh_local_to_screen(obj, m_mesh_points[oi].x, m_mesh_points[oi].y, s))
            pts.push_back({s, oi});
    }
    if (pts.size() < 3) return;

    double best_edge_d2 = 10.0 * 10.0;
    for (size_t i = 0; i < pts.size(); ++i) {
        const size_t j = (i + 1) % pts.size();
        const double ex = pts[j].p.x() - pts[i].p.x(), ey = pts[j].p.y() - pts[i].p.y();
        const double len2 = ex*ex + ey*ey;
        double t = len2 > 1e-9 ? ((px.x() - pts[i].p.x())*ex + (px.y() - pts[i].p.y())*ey)/len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        const double qx = pts[i].p.x() + ex*t, qy = pts[i].p.y() + ey*t;
        const double d2 = (px.x() - qx)*(px.x() - qx) + (px.y() - qy)*(px.y() - qy);
        if (d2 < best_edge_d2) { best_edge_d2 = d2; m_mesh_hover_edge = pts[i].oi; }
        const double vdx = px.x() - pts[i].p.x(), vdy = px.y() - pts[i].p.y();
        if (vdx*vdx + vdy*vdy <= 11.0*11.0) m_mesh_hover_vertex = pts[i].oi;
    }
}

bool Viewport3DWidget::mesh_edit_mouse_press(QMouseEvent* e) {
    if (m_mesh_edit_object < 0 || m_mesh_edit_object >= (int)m_scene.objects.size()) return true;
    const av::SceneObject& obj = m_scene.objects[m_mesh_edit_object];
    const QPointF px = e->position();

    float origin[3], dir[3];
    mesh_screen_ray(px, origin, dir);
    double lx = 0, ly = 0;
    const bool hit_plane = mesh_ray_object_plane(obj, origin, dir, lx, ly);

    if (e->button() == Qt::LeftButton) {
        if (m_mesh_hover_vertex >= 0 && m_mesh_hover_vertex < (int)m_mesh_points.size() && hit_plane) {
            m_mesh_drag_point = m_mesh_hover_vertex;
            m_mesh_dragging = true;
            m_mesh_drag_from_insert = false;
            // Keep the vertex's offset from the cursor (no snap-to-centre jump).
            m_mesh_drag_off_x = m_mesh_points[m_mesh_drag_point].x - lx;
            m_mesh_drag_off_y = m_mesh_points[m_mesh_drag_point].y - ly;
        } else {
            m_mesh_drag_point = -1;
            m_mesh_dragging = false;
        }
        return true;
    }
    if (e->button() == Qt::RightButton) {
        if (m_mesh_hover_vertex >= 0 && m_mesh_points.size() > 3) {
            m_mesh_points.erase(m_mesh_points.begin() + m_mesh_hover_vertex);
            m_mesh_drag_point = -1;   // indices shifted — drop any stale drag
            m_mesh_dragging = false;
            m_mesh_dirty = true;
        } else if (m_mesh_hover_edge >= 0 && hit_plane) {
            // Insert a node at the click spot, snapped onto the hovered edge,
            // and grab it immediately so the user can drag it into place.
            const int a = m_mesh_hover_edge;
            const int b = (a + 1) % (int)m_mesh_points.size();
            const double ax = m_mesh_points[a].x, ay = m_mesh_points[a].y;
            const double bx = m_mesh_points[b].x, by = m_mesh_points[b].y;
            const double dx = bx - ax, dy = by - ay;
            const double len2 = dx*dx + dy*dy;
            double t = len2 > 1e-12 ? ((lx - ax)*dx + (ly - ay)*dy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            m_mesh_points.insert(m_mesh_points.begin() + b, {ax + dx*t, ay + dy*t});
            m_mesh_drag_point = b;    // grab the new node right away
            m_mesh_dragging = true;
            m_mesh_drag_from_insert = true;  // keep it grabbed while RMB is held
            m_mesh_drag_off_x = 0.0;
            m_mesh_drag_off_y = 0.0;
            m_mesh_dirty = true;
        }
        return true;
    }
    if (e->button() == Qt::MiddleButton) {
        m_mesh_panning = true;
        m_mesh_pan_start_px = px;
        m_mesh_pan_start_target[0] = m_cam_target[0];
        m_mesh_pan_start_target[1] = m_cam_target[1];
        m_mesh_pan_start_target[2] = m_cam_target[2];
        return true;
    }
    return true;
}

bool Viewport3DWidget::mesh_edit_mouse_move(QMouseEvent* e) {
    const QPointF px = e->position();
    mesh_update_hover(px);

    if (m_mesh_panning) {
        const double scale = m_cam_dist * 0.003 * m_cam_pan_speed;
        const double rad_yaw = m_cam_yaw * (3.14159265358979 / 180.0);
        const double rx = std::cos(rad_yaw), rz = -std::sin(rad_yaw);
        const double dx = px.x() - m_mesh_pan_start_px.x();
        const double dy = px.y() - m_mesh_pan_start_px.y();
        m_cam_target[0] = (float)(m_mesh_pan_start_target[0] - rx * dx * scale);
        m_cam_target[1] = (float)(m_mesh_pan_start_target[1] + dy * scale);
        m_cam_target[2] = (float)(m_mesh_pan_start_target[2] - rz * dx * scale);
        return true;
    }

    if (m_mesh_dragging && m_mesh_drag_point >= 0 && m_mesh_drag_point < (int)m_mesh_points.size()) {
        const bool held = e->buttons().testFlag(Qt::LeftButton) ||
                          (m_mesh_drag_from_insert && e->buttons().testFlag(Qt::RightButton));
        if (!held) {
            m_mesh_dragging = false;
            m_mesh_drag_point = -1;
            m_mesh_drag_from_insert = false;
            return true;
        }
        float origin[3], dir[3];
        mesh_screen_ray(px, origin, dir);
        double lx = 0, ly = 0;
        const av::SceneObject& obj = m_scene.objects[m_mesh_edit_object];
        if (mesh_ray_object_plane(obj, origin, dir, lx, ly)) {
            double nx = lx + m_mesh_drag_off_x;
            double ny = ly + m_mesh_drag_off_y;
            if (m_mesh_snap > 0.0) {
                nx = std::round(nx / m_mesh_snap) * m_mesh_snap;
                ny = std::round(ny / m_mesh_snap) * m_mesh_snap;
            }
            m_mesh_points[m_mesh_drag_point] = {nx, ny};
            m_mesh_dirty = true;
            // Throttled live re-apply: the mesh reshapes as you drag (~90 ms),
            // same feel as the ImGui inline editor.
            if (m_mesh_live_timer.elapsed() >= 90) {
                m_mesh_live_timer.restart();
                mesh_apply();
            }
        }
    }
    return true;
}

bool Viewport3DWidget::mesh_edit_mouse_release(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) m_mesh_panning = false;
    if (m_mesh_dragging) {
        m_mesh_dragging = false;
        m_mesh_drag_point = -1;
        m_mesh_drag_from_insert = false;
    }
    return true;
}

bool Viewport3DWidget::mesh_edit_key(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_M ||
        e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        end_mesh_edit(true);   // commit
        return true;
    }
    if (e->key() == Qt::Key_R ||
        (e->key() == Qt::Key_Z && e->modifiers().testFlag(Qt::ControlModifier))) {
        mesh_revert_session();   // whole-session undo
        return true;
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (m_mesh_hover_vertex >= 0 && m_mesh_points.size() > 3) {
            m_mesh_points.erase(m_mesh_points.begin() + m_mesh_hover_vertex);
            m_mesh_drag_point = -1;
            m_mesh_dragging = false;
            m_mesh_dirty = true;
            mesh_apply();
            update();
        }
        return true;   // never delete the OBJECT while mesh-editing
    }
    if (e->key() == Qt::Key_G) {
        m_mesh_snap = (m_mesh_snap > 0.0) ? 0.0 : 25.0;
        update();
        return true;
    }
    if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
        e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
        int v = (m_mesh_hover_vertex >= 0) ? m_mesh_hover_vertex : m_mesh_drag_point;
        if (v >= 0 && v < (int)m_mesh_points.size()) {
            const double step = (m_mesh_snap > 0.0) ? m_mesh_snap : 1.0;
            if (e->key() == Qt::Key_Left)  m_mesh_points[v].x -= step;
            if (e->key() == Qt::Key_Right) m_mesh_points[v].x += step;
            if (e->key() == Qt::Key_Up)    m_mesh_points[v].y += step;
            if (e->key() == Qt::Key_Down)  m_mesh_points[v].y -= step;
            m_mesh_dirty = true;
            mesh_apply();
            update();
        }
        return true;
    }
    if (e->modifiers().testFlag(Qt::ControlModifier) && e->key() != Qt::Key_Z) return true;
    return false;
}

// QPainter overlay composited over the GL pass: 2D grid on the object plane,
// polygon fill + outline, draggable vertex handles with index labels, hover
// states, the SNAP indicator, and the mode hint bar.
void Viewport3DWidget::draw_mesh_edit_overlay() {
    if (m_mesh_edit_object < 0 || m_mesh_edit_object >= (int)m_scene.objects.size()) return;
    const av::SceneObject& obj = m_scene.objects[m_mesh_edit_object];
    if (m_mesh_points.size() < 3) return;

    struct SP { QPointF p; int oi; };
    std::vector<SP> pts;
    pts.reserve(m_mesh_points.size());
    for (int oi = 0; oi < (int)m_mesh_points.size(); ++oi) {
        QPointF s;
        if (mesh_local_to_screen(obj, m_mesh_points[oi].x, m_mesh_points[oi].y, s))
            pts.push_back({s, oi});
    }
    if (pts.size() < 3) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const float w = (float)std::max(1, width());
    const float h = (float)std::max(1, height());
    constexpr double kPi = 3.14159265358979;

    // ── 2D grid on the object plane (screen-space, adapts to zoom) ──
    const av::Camera cam = mesh_camera();
    const float px_per_wu = (h * 0.5f) /
        std::max(1e-3f, cam.distance * std::tan((float)(cam.fov * kPi / 360.0)));
    float step = 50.0f;
    while (step * px_per_wu < 34.0f) step *= 2.0f;
    while (step * px_per_wu > 96.0f) step *= 0.5f;
    double lminx = 1e30, lmaxx = -1e30, lminy = 1e30, lmaxy = -1e30;
    for (const auto& pt : m_mesh_points) {
        lminx = std::min(lminx, pt.x); lmaxx = std::max(lmaxx, pt.x);
        lminy = std::min(lminy, pt.y); lmaxy = std::max(lmaxy, pt.y);
    }
    const double margin_wu = 40.0 / std::max(1e-3, (double)px_per_wu);
    lminx -= margin_wu; lmaxx += margin_wu;
    lminy -= margin_wu; lmaxy += margin_wu;
    auto proj = [&](double lx, double ly, QPointF& out) -> bool {
        return mesh_local_to_screen(obj, lx, ly, out);
    };
    QPointF ga, gb;
    p.setPen(QPen(QColor(150, 190, 170, 46), 1.0));
    const int k0 = (int)std::floor(lminx / step), k1 = (int)std::ceil(lmaxx / step);
    const int j0 = (int)std::floor(lminy / step), j1 = (int)std::ceil(lmaxy / step);
    for (int k = k0; k <= k1; ++k) {
        if (!proj(k * step, lminy, ga) || !proj(k * step, lmaxy, gb)) continue;
        p.drawLine(ga, gb);
    }
    for (int j = j0; j <= j1; ++j) {
        if (!proj(lminx, j * step, ga) || !proj(lmaxx, j * step, gb)) continue;
        p.drawLine(ga, gb);
    }
    if (proj(0.0, lminy, ga) && proj(0.0, lmaxy, gb)) {   // local X axis
        p.setPen(QPen(QColor(235, 110, 110, 80), 1.5));
        p.drawLine(ga, gb);
    }
    if (proj(lminx, 0.0, ga) && proj(lmaxx, 0.0, gb)) {   // local Y axis
        p.setPen(QPen(QColor(110, 200, 120, 80), 1.5));
        p.drawLine(ga, gb);
    }

    // ── Polygon fill + outline; hovered edge glows gold ──
    QPolygonF poly;
    for (const auto& sp : pts) poly << sp.p;
    QPainterPath fill_path;
    fill_path.addPolygon(poly);
    p.fillPath(fill_path, QColor(70, 130, 90, 70));
    for (size_t i = 0; i < pts.size(); ++i) {
        const size_t j = (i + 1) % pts.size();
        const bool hover = (m_mesh_hover_edge == pts[i].oi && m_mesh_hover_vertex < 0);
        p.setPen(QPen(hover ? QColor(255, 200, 60, 255) : QColor(120, 210, 150, 255),
                      hover ? 3.5 : 2.0));
        p.drawLine(pts[i].p, pts[j].p);
    }

    // ── Vertex handles with index labels ──
    for (const auto& sp : pts) {
        const bool hover = (sp.oi == m_mesh_hover_vertex);
        const bool dragging = (sp.oi == m_mesh_drag_point && m_mesh_dragging);
        const float r = (hover || dragging) ? 6.5f : 5.0f;
        const QColor col = dragging ? QColor(255, 255, 120, 255)
                          : hover   ? QColor(255, 190, 120, 255)
                          :           QColor(255, 130, 70, 255);
        p.setBrush(col);
        p.setPen(QPen(QColor(20, 20, 30, 255), 1.5));
        p.drawEllipse(sp.p, r, r);
        p.setPen(QColor(225, 235, 245, 190));
        QFont f = p.font();
        f.setPixelSize(11);
        p.setFont(f);
        p.drawText(QPointF(sp.p.x() + 7.0, sp.p.y() - 11.0), QString::number(sp.oi));
    }

    // ── Mode tag + SNAP indicator + hint bar ──
    QFont tag_f = p.font();
    tag_f.setPixelSize(12); tag_f.setBold(true);
    p.setFont(tag_f);
    p.setPen(QColor(110, 210, 255, 255));
    p.drawText(QPointF(w - 118.0, 24.0), QStringLiteral("MESH EDIT"));
    QFont hint_f = p.font();
    hint_f.setPixelSize(11); hint_f.setBold(false);
    p.setFont(hint_f);
    p.setPen(m_mesh_snap > 0.0 ? QColor(150, 255, 170, 255) : QColor(200, 205, 215, 255));
    p.drawText(QPointF(w - 210.0, 42.0),
               m_mesh_snap > 0.0
                   ? QStringLiteral("SNAP %1 (G)").arg(m_mesh_snap, 0, 'f', 0)
                   : QStringLiteral("SNAP OFF (G)"));
    p.fillRect(QRectF(0, h - 24.0, w, 24.0), QColor(18, 21, 28, 215));
    p.setPen(QColor(160, 175, 195, 255));
    p.drawText(QPointF(8.0, h - 8.0),
               QStringLiteral("LMB drag vertex \xc2\xb7 RMB edge = new node \xc2\xb7 "
                              "RMB/Del vertex = remove \xc2\xb7 G snap \xc2\xb7 arrows nudge \xc2\xb7 "
                              "Esc/M done \xc2\xb7 R revert"));
    p.end();
}

void Viewport3DWidget::add_scene_object(const QString& kind, const float* spawn_pos) {
    if (!has_scene()) return;
    const std::string before = capture_scene_snapshot();
    av::SceneObject obj;

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (spawn_pos) {
        x = spawn_pos[0];
        y = spawn_pos[1];
        z = spawn_pos[2];
    } else {
        x = m_cam_target[0];
        z = 0.0f;
        y = av::scene_terrain_top_y(m_scene, x);
        if (std::abs(y - m_cam_target[1]) > 300.0f) {
            y = m_cam_target[1];
        }
    }

    const std::string ident = av::scene_fresh_identifier(m_scene);

    if (kind == QStringLiteral("Spawn")) {
        obj = av::scene_build_spawn_object(ident, x, y);
        obj.pos_z = z;
    } else if (kind == QStringLiteral("Portal")) {
        obj = av::scene_build_portal_object(ident, x, y);
        obj.pos_z = z;
    } else {
        obj.name = ident;
        obj.pos_x = x;
        obj.pos_y = y;
        obj.pos_z = z;
        obj.scale_x = obj.scale_y = obj.scale_z = 1.0f;
        obj.local_aabb = av::scene_build_local_aabb(-20.0f, -20.0f, 20.0f, 20.0f);
    }

    m_scene.objects.push_back(std::move(obj));
    const int new_idx = static_cast<int>(m_scene.objects.size() - 1);

    if (m_scene_ground_textures.size() <= static_cast<size_t>(new_idx)) {
        m_scene_ground_textures.resize(new_idx + 1);
    }

    av::scene_refresh(m_scene);
    ensure_pasted_model_resources(m_scene.objects[new_idx]);
    ensure_pasted_background_resources(m_scene.objects[new_idx].background_name);
    append_render_object_for_index(new_idx);

    const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0];
    const float dy = m_scene.bounds_max[1] - m_scene.bounds_min[1];
    const float dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
    m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);

    auto wit = m_scene_cache.find(m_current_scene_path);
    if (wit != m_scene_cache.end() && wit->second) {
        wit->second->scene = m_scene;
        wit->second->render_objects = m_render_objects;
        wit->second->scene_ground_textures = m_scene_ground_textures;
        wit->second->all_mesh_gpu = m_all_mesh_gpu;
        wit->second->scene_extent = m_scene_extent;
    }

    set_selected_object(new_idx);
    emit sceneObjectSelected(new_idx);
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, QStringLiteral("Add %1").arg(kind));
}

int Viewport3DWidget::add_template_object(const QString& template_name,
                                          const av::SceneObject* template_object,
                                          float template_scaling,
                                          const float* spawn_pos) {
    if (!has_scene() || template_name.isEmpty()) return -1;
    const std::string before = capture_scene_snapshot();
    const std::string name = template_name.toStdString();

    av::SceneObject obj;
    obj.template_name = name;
    obj.name = av::scene_fresh_identifier(m_scene);

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (spawn_pos) {
        x = spawn_pos[0];
        y = spawn_pos[1];
        z = spawn_pos[2];
    } else {
        x = m_cam_target[0];
        z = 0.0f;
        y = av::scene_terrain_top_y(m_scene, x);
        if (std::abs(y - m_cam_target[1]) > 300.0f) {
            y = m_cam_target[1];
        }
    }
    obj.pos_x = x;
    obj.pos_y = y;
    obj.pos_z = z;
    obj.scale_x = obj.scale_y = obj.scale_z = 1.0f;
    obj.template_scaling = template_scaling;

    if (template_object) {
        obj.components = template_object->components;
        obj.local_aabb = template_object->local_aabb;
    }
    if (obj.local_aabb.empty()) {
        obj.local_aabb = av::scene_build_local_aabb(-25.0f, -25.0f, 25.0f, 25.0f);
    }

    m_scene.objects.push_back(std::move(obj));
    const int new_idx = static_cast<int>(m_scene.objects.size() - 1);

    if (m_scene_ground_textures.size() <= static_cast<size_t>(new_idx)) {
        m_scene_ground_textures.resize(new_idx + 1);
    }

    av::scene_refresh(m_scene);
    ensure_pasted_model_resources(m_scene.objects[new_idx]);
    ensure_pasted_background_resources(m_scene.objects[new_idx].background_name);
    append_render_object_for_index(new_idx);

    const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0];
    const float dy = m_scene.bounds_max[1] - m_scene.bounds_min[1];
    const float dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
    m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);

    auto wit = m_scene_cache.find(m_current_scene_path);
    if (wit != m_scene_cache.end() && wit->second) {
        wit->second->scene = m_scene;
        wit->second->render_objects = m_render_objects;
        wit->second->scene_ground_textures = m_scene_ground_textures;
        wit->second->all_mesh_gpu = m_all_mesh_gpu;
        wit->second->scene_extent = m_scene_extent;
    }

    set_selected_object(new_idx);
    emit sceneObjectSelected(new_idx);
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, QStringLiteral("Add Template ") + template_name);
    return new_idx;
}

int Viewport3DWidget::add_model_object(const QString& pod_path, const QString& display_name,
                                      const float* spawn_pos) {
    if (!has_scene() || pod_path.isEmpty()) return -1;
    const std::string before = capture_scene_snapshot();

    av::PODModel model = load_pod_to_ram(pod_path.toStdString(), "");
    const bool has_bounds = !model.meshes.empty();
    const fs::path pod(pod_path.toStdString());
    const std::string mesh_name = pod.stem().string();

    std::string base_name = display_name.isEmpty() ? mesh_name : display_name.toStdString();
    if (base_name.empty()) base_name = mesh_name;

    av::SceneObject obj = av::scene_build_pod_object(pod_path.toStdString(), base_name);

    // Deduplicate object name against live scene
    {
        bool taken = false;
        for (const auto& o : m_scene.objects) {
            if (o.name == obj.name) { taken = true; break; }
        }
        int suffix = 2;
        while (taken) {
            const std::string cand = base_name + "_" + std::to_string(suffix++);
            taken = false;
            for (const auto& o : m_scene.objects) {
                if (o.name == cand) { taken = true; break; }
            }
            if (!taken) obj.name = cand;
        }
    }

    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (spawn_pos) {
        x = spawn_pos[0];
        y = spawn_pos[1];
        z = spawn_pos[2];
    } else {
        x = m_cam_target[0];
        z = 0.0f;
        y = av::scene_terrain_top_y(m_scene, x);
        if (std::abs(y - m_cam_target[1]) > 300.0f) {
            y = m_cam_target[1];
        }
    }

    // Model lifting: lift model so its bottom sits on ground
    if (has_bounds && model.min_y < 0.0f) {
        y += (-model.min_y);
    }

    obj.pos_x = x;
    obj.pos_y = y;
    obj.pos_z = z;
    obj.scale_x = obj.scale_y = obj.scale_z = 1.0f;

    m_scene.objects.push_back(std::move(obj));
    const int new_idx = static_cast<int>(m_scene.objects.size() - 1);

    if (m_scene_ground_textures.size() <= static_cast<size_t>(new_idx)) {
        m_scene_ground_textures.resize(new_idx + 1);
    }

    av::scene_refresh(m_scene);
    ensure_pasted_model_resources(m_scene.objects[new_idx]);
    append_render_object_for_index(new_idx);

    const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0];
    const float dy = m_scene.bounds_max[1] - m_scene.bounds_min[1];
    const float dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
    m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);

    auto wit = m_scene_cache.find(m_current_scene_path);
    if (wit != m_scene_cache.end() && wit->second) {
        wit->second->scene = m_scene;
        wit->second->render_objects = m_render_objects;
        wit->second->scene_ground_textures = m_scene_ground_textures;
        wit->second->all_mesh_gpu = m_all_mesh_gpu;
        wit->second->scene_extent = m_scene_extent;
    }

    set_selected_object(new_idx);
    emit sceneObjectSelected(new_idx);
    update();
    emit sceneEdited();
    push_scene_snapshot_undo(before, QStringLiteral("Add Model ") + QString::fromStdString(base_name));
    return new_idx;
}

void Viewport3DWidget::set_scene_object_template(int index, const QString& template_name) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size()))
        return;
    const std::string before = capture_scene_snapshot();
    av::scene_set_object_template(m_scene, static_cast<size_t>(index),
                                  template_name.toStdString());
    ensure_pasted_model_resources(m_scene.objects[index]);
    ensure_pasted_background_resources(m_scene.objects[index].background_name);
    rebuild_render_object_for_index(index);
    update();
    emit sceneObjectRenderStateChanged(index);
    emit sceneEdited();
    push_scene_snapshot_undo(before,
        QString("Template: %1").arg(template_name.isEmpty() ? QStringLiteral("unlink") : template_name));
}

void Viewport3DWidget::materialize_scene_object_template(int index) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size()))
        return;
    if (m_scene.objects[index].template_name.empty()) return;
    const std::string before = capture_scene_snapshot();
    av::scene_materialize_object_template(m_scene, static_cast<size_t>(index));
    ensure_pasted_model_resources(m_scene.objects[index]);
    ensure_pasted_background_resources(m_scene.objects[index].background_name);
    rebuild_render_object_for_index(index);
    update();
    emit sceneObjectRenderStateChanged(index);
    emit sceneEdited();
    push_scene_snapshot_undo(before, "Unlink Template (Materialize)");
}

void Viewport3DWidget::override_inherited_component(int index, const QString& class_name) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size()))
        return;
    const std::string before = capture_scene_snapshot();   // MUST precede the mutation
    if (!av::scene_override_inherited_component(m_scene, static_cast<size_t>(index),
                                                class_name.toStdString()))
        return;
    ensure_pasted_model_resources(m_scene.objects[index]);
    rebuild_render_object_for_index(index);
    update();
    emit sceneObjectRenderStateChanged(index);
    emit sceneEdited();
    push_scene_snapshot_undo(before, "Override Inherited Component");
}

void Viewport3DWidget::reset_scene_object_to_template(int index) {
    if (!has_scene() || index < 0 || index >= static_cast<int>(m_scene.objects.size()))
        return;
    if (m_scene.objects[index].template_name.empty()) return;
    const std::string before = capture_scene_snapshot();
    if (!av::scene_apply_clean_template(m_scene, static_cast<size_t>(index)))
        return;
    ensure_pasted_model_resources(m_scene.objects[index]);
    ensure_pasted_background_resources(m_scene.objects[index].background_name);
    rebuild_render_object_for_index(index);
    update();
    emit sceneObjectRenderStateChanged(index);
    emit sceneEdited();
    push_scene_snapshot_undo(before, "Reset to Template");
}

int Viewport3DWidget::add_ground_mesh_object(av::SceneObject obj) {
    if (!has_scene()) return -1;
    makeCurrent();

    int new_idx = static_cast<int>(m_scene.objects.size());
    m_scene.objects.push_back(obj);

    // Resolve and upload ground textures
    std::vector<fs::path> scene_roots;
    const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
    if (!pdir.empty()) scene_roots.push_back(fs::path(pdir));
    if (!m_current_scene_path.empty()) {
        scene_roots.push_back(fs::path(m_current_scene_path).parent_path());
    }

    if (m_scene_ground_textures.size() <= static_cast<size_t>(new_idx)) {
        m_scene_ground_textures.resize(new_idx + 1);
    }
    auto& textures = m_scene_ground_textures[new_idx];
    textures.resize(obj.ground_meshes.size(), 0);
    for (size_t mesh_index = 0; mesh_index < obj.ground_meshes.size(); ++mesh_index) {
        const std::string name = mesh_index < obj.ground_mesh_textures.size() ? obj.ground_mesh_textures[mesh_index] : std::string();
        if (name.empty()) continue;
        for (const auto& candidate : av::assets::texture_candidates(fs::path(m_current_scene_path), name, scene_roots)) {
            if (fs::exists(candidate)) {
                GLuint tex = load_texture_any(candidate.string());
                if (tex) {
                    textures[mesh_index] = tex;
                    break;
                }
            }
        }
    }

    // Build render object
    SceneRenderObject ro;
    ro.pos[0] = obj.pos_x; ro.pos[1] = obj.pos_y; ro.pos[2] = obj.pos_z;
    swk::object_world_matrix(obj, ro.world_matrix);
    swk::object_render_matrix(obj, ro.render_matrix);
    ro.hidden = obj.hidden;
    ro.is_portal = obj.is_portal;
    ro.is_spawn_point = obj.is_spawn_point;
    ro.is_camera = obj.is_camera;
    ro.is_dimension_object = obj.is_dimension_object;
    ro.object_index = new_idx;
    ro.name = obj.name;
    ro.mesh_name = obj.mesh_name;
    ro.local_aabb = obj.local_aabb;
    ro.has_diffuse_color = obj.has_model_diffuse_color;
    ro.diffuse_color[0] = obj.model_diffuse_color[0];
    ro.diffuse_color[1] = obj.model_diffuse_color[1];
    ro.diffuse_color[2] = obj.model_diffuse_color[2];
    ro.ground_textures = textures;
    ro.ground_tex_names.reserve(obj.ground_meshes.size());
    for (size_t gi = 0; gi < obj.ground_meshes.size(); ++gi)
        ro.ground_tex_names.push_back(gi < obj.ground_mesh_textures.size()
                                         ? obj.ground_mesh_textures[gi] : std::string());

    ro.ground_gpu.reserve(obj.ground_meshes.size());
    for (const auto& gmesh : obj.ground_meshes) {
        MeshGpu g = upload_mesh_gpu(gmesh);
        if (g.valid()) {
            ro.ground_gpu.push_back(g);
            m_all_mesh_gpu.push_back(g);
        } else {
            ro.ground_gpu.push_back(MeshGpu{});
        }
    }

    m_render_objects.push_back(std::move(ro));

    av::scene_refresh(m_scene);
    const float dx = m_scene.bounds_max[0] - m_scene.bounds_min[0];
    const float dy = m_scene.bounds_max[1] - m_scene.bounds_min[1];
    const float dz = m_scene.bounds_max[2] - m_scene.bounds_min[2];
    m_scene_extent = std::max(1000.0f, std::sqrt(dx * dx + dy * dy + dz * dz) * 1.5f);

    auto wit = m_scene_cache.find(m_current_scene_path);
    if (wit != m_scene_cache.end() && wit->second) {
        wit->second->scene = m_scene;
        wit->second->render_objects = m_render_objects;
        wit->second->scene_ground_textures = m_scene_ground_textures;
        wit->second->all_mesh_gpu = m_all_mesh_gpu;
        wit->second->scene_extent = m_scene_extent;
    }

    set_selected_object(new_idx);
    emit sceneObjectSelected(new_idx);
    doneCurrent();

    update();
    emit sceneEdited();
    return new_idx;
}

// ============================================================================
// 3D ViewCube (Interactive Camera Orientation Cube)
// ============================================================================

// ============================================================================
// 3D ViewCube (1:1 Port of ImGuizmo::ViewManipulate Camera Controller)
// ============================================================================

namespace {
// Projection from 3D to 2D screen coordinates in the ViewCube widget
static QPointF project_view_cube_pt(const float p[3], const float view[16], const float proj[16],
                                     float cx, float cy, float half_s) {
    float vp[4] = {
        p[0]*view[0] + p[1]*view[4] + p[2]*view[8]  + view[12],
        p[0]*view[1] + p[1]*view[5] + p[2]*view[9]  + view[13],
        p[0]*view[2] + p[1]*view[6] + p[2]*view[10] + view[14],
        1.0f
    };
    float clip[4] = {
        vp[0]*proj[0] + vp[1]*proj[4] + vp[2]*proj[8]  + vp[3]*proj[12],
        vp[0]*proj[1] + vp[1]*proj[5] + vp[2]*proj[9]  + vp[3]*proj[13],
        vp[0]*proj[2] + vp[1]*proj[6] + vp[2]*proj[10] + vp[3]*proj[14],
        vp[0]*proj[3] + vp[1]*proj[7] + vp[2]*proj[11] + vp[3]*proj[15]
    };
    float inv_w = (std::abs(clip[3]) > 1e-6f) ? (1.0f / clip[3]) : 1.0f;
    float ndc_x = clip[0] * inv_w;
    float ndc_y = clip[1] * inv_w;
    return QPointF(cx + ndc_x * half_s, cy - ndc_y * half_s);
}
}

void Viewport3DWidget::get_cube_view_matrix(float out[16]) const {
    const float rad_pitch = m_cam_pitch * (3.14159265f / 180.0f);
    const float rad_yaw   = m_cam_yaw * (3.14159265f / 180.0f);

    float eye_x = std::cos(rad_pitch) * std::sin(rad_yaw);
    float eye_y = std::sin(rad_pitch);
    float eye_z = std::cos(rad_pitch) * std::cos(rad_yaw);

    const float forward_x = -eye_x;
    const float forward_y = -eye_y;
    const float forward_z = -eye_z;
    const float forward_len = std::sqrt(forward_x * forward_x + forward_y * forward_y + forward_z * forward_z);
    const float fx = (forward_len > 1e-6f) ? (forward_x / forward_len) : 0.0f;
    const float fy = (forward_len > 1e-6f) ? (forward_y / forward_len) : 0.0f;
    const float fz = (forward_len > 1e-6f) ? (forward_z / forward_len) : -1.0f;

    float sx = -fz, sy = 0.0f, sz = fx;
    const float s_len = std::sqrt(sx * sx + sz * sz);
    if (s_len > 1e-6f) { sx /= s_len; sz /= s_len; }
    const float upx = sy * fz - sz * fy, upy = sz * fx - sx * fz, upz = sx * fy - sy * fx;

    out[0] = sx;   out[1] = upx;  out[2] = -fx;  out[3] = 0.0f;
    out[4] = sy;   out[5] = upy;  out[6] = -fy;  out[7] = 0.0f;
    out[8] = sz;   out[9] = upz;  out[10]= -fz;  out[11]= 0.0f;
    out[12]= 0.0f; out[13]= 0.0f; out[14]= 0.0f; out[15]= 1.0f;
}

int Viewport3DWidget::hit_test_view_cube(const QPointF& pt) const {
    const float pad = 12.0f;
    const float size = 150.0f;
    const float cx = pad + size * 0.5f;
    const float cy = pad + size * 0.5f;

    const float dx = static_cast<float>(pt.x()) - cx;
    const float dy = static_cast<float>(pt.y()) - cy;
    if (std::abs(dx) > size * 0.5f || std::abs(dy) > size * 0.5f) return -1;

    // ViewCube camera view matrix (shared 1:1 with rendering)
    float cube_view[16];
    get_cube_view_matrix(cube_view);

    // Orthographic projection matrix
    const float ortho_dim = 1.45f;
    float cube_proj[16] = {
        1.0f / ortho_dim, 0, 0, 0,
        0, 1.0f / ortho_dim, 0, 0,
        0, 0, -1.0f / 10.0f, 0,
        0, 0, 0, 1
    };

    static const float dir_unary[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    static const float panel_pos[9][2] = {
        {0.75f,0.75f}, {0.25f,0.75f}, {0.0f,0.75f},
        {0.75f,0.25f}, {0.25f,0.25f}, {0.0f,0.25f},
        {0.75f,0.0f},  {0.25f,0.0f},  {0.0f,0.0f}
    };
    static const float panel_sz[9][2] = {
        {0.25f,0.25f}, {0.50f,0.25f}, {0.25f,0.25f},
        {0.25f,0.50f}, {0.50f,0.50f}, {0.25f,0.50f},
        {0.25f,0.25f}, {0.50f,0.25f}, {0.25f,0.25f}
    };

    int best_box = -1;
    float max_depth = -1e9f;

    // Test each face panel
    for (int iFace = 0; iFace < 6; iFace++) {
        const int norm_idx = (iFace % 3);
        const int perp_x   = (norm_idx + 1) % 3;
        const int perp_y   = (norm_idx + 2) % 3;
        const float invert = (iFace > 2) ? -1.0f : 1.0f;

        const float n[3] = { dir_unary[norm_idx][0] * invert,
                             dir_unary[norm_idx][1] * invert,
                             dir_unary[norm_idx][2] * invert };

        // Back-face culling: row 2 of cube_view is -fwd (facing towards camera in eye space)
        float view_norm_z = n[0]*cube_view[2] + n[1]*cube_view[6] + n[2]*cube_view[10];
        if (view_norm_z <= 0.0f) continue;

        const float dx_v[3] = { dir_unary[perp_x][0], dir_unary[perp_x][1], dir_unary[perp_x][2] };
        const float dy_v[3] = { dir_unary[perp_y][0], dir_unary[perp_y][1], dir_unary[perp_y][2] };
        const float origin[3] = { dir_unary[norm_idx][0] - dx_v[0] - dy_v[0],
                                  dir_unary[norm_idx][1] - dx_v[1] - dy_v[1],
                                  dir_unary[norm_idx][2] - dx_v[2] - dy_v[2] };
        const float idx_vec_x[3] = { dx_v[0] * invert, dx_v[1] * invert, dx_v[2] * invert };
        const float idx_vec_y[3] = { dy_v[0] * invert, dy_v[1] * invert, dy_v[2] * invert };
        const float box_origin[3] = { dir_unary[norm_idx][0] * -invert - idx_vec_x[0] - idx_vec_y[0],
                                      dir_unary[norm_idx][1] * -invert - idx_vec_x[1] - idx_vec_y[1],
                                      dir_unary[norm_idx][2] * -invert - idx_vec_x[2] - idx_vec_y[2] };

        for (int iPanel = 0; iPanel < 9; ++iPanel) {
            float bx = box_origin[0] + idx_vec_x[0]*(iPanel%3) + idx_vec_y[0]*(iPanel/3) + 1.0f;
            float by = box_origin[1] + idx_vec_x[1]*(iPanel%3) + idx_vec_y[1]*(iPanel/3) + 1.0f;
            float bz = box_origin[2] + idx_vec_x[2]*(iPanel%3) + idx_vec_y[2]*(iPanel/3) + 1.0f;
            int box_id = static_cast<int>(std::round(bx * 9.0f + by * 3.0f + bz));
            if (box_id < 0 || box_id >= 27) continue;

            const float px = panel_pos[iPanel][0] * 2.0f;
            const float py = panel_pos[iPanel][1] * 2.0f;
            const float sx = panel_sz[iPanel][0] * 2.0f;
            const float sy = panel_sz[iPanel][1] * 2.0f;

            float panel_corners[4][3] = {
                { (dx_v[0]*px + dy_v[0]*py + origin[0]) * 0.5f * invert,
                  (dx_v[1]*px + dy_v[1]*py + origin[1]) * 0.5f * invert,
                  (dx_v[2]*px + dy_v[2]*py + origin[2]) * 0.5f * invert },
                { (dx_v[0]*px + dy_v[0]*(py+sy) + origin[0]) * 0.5f * invert,
                  (dx_v[1]*px + dy_v[1]*(py+sy) + origin[1]) * 0.5f * invert,
                  (dx_v[2]*px + dy_v[2]*(py+sy) + origin[2]) * 0.5f * invert },
                { (dx_v[0]*(px+sx) + dy_v[0]*(py+sy) + origin[0]) * 0.5f * invert,
                  (dx_v[1]*(px+sx) + dy_v[1]*(py+sy) + origin[1]) * 0.5f * invert,
                  (dx_v[2]*(px+sx) + dy_v[2]*(py+sy) + origin[2]) * 0.5f * invert },
                { (dx_v[0]*(px+sx) + dy_v[0]*py + origin[0]) * 0.5f * invert,
                  (dx_v[1]*(px+sx) + dy_v[1]*py + origin[1]) * 0.5f * invert,
                  (dx_v[2]*(px+sx) + dy_v[2]*py + origin[2]) * 0.5f * invert }
            };

            QPointF sc[4];
            for (int c = 0; c < 4; ++c) {
                sc[c] = project_view_cube_pt(panel_corners[c], cube_view, cube_proj, cx, cy, size * 0.5f);
            }

            // Test if point is inside convex quad (winding-agnostic cross product sign test)
            bool inside = true;
            int sign = 0;
            for (int e = 0; e < 4; ++e) {
                int next = (e + 1) % 4;
                float cross = (sc[next].x() - sc[e].x()) * (pt.y() - sc[e].y()) -
                              (sc[next].y() - sc[e].y()) * (pt.x() - sc[e].x());
                if (std::abs(cross) > 1e-4f) {
                    int cur_sign = (cross > 0.0f) ? 1 : -1;
                    if (sign == 0) sign = cur_sign;
                    else if (sign != cur_sign) { inside = false; break; }
                }
            }

            if (inside) {
                float view_z = cube_view[2]*panel_corners[0][0] + cube_view[6]*panel_corners[0][1] + cube_view[10]*panel_corners[0][2];
                if (view_z > max_depth) {
                    max_depth = view_z;
                    best_box = box_id;
                }
            }
        }
    }
    return best_box;
}

void Viewport3DWidget::draw_view_cube() {
    const float pad = 12.0f;
    const float size = 150.0f;
    const int vp_w = width();
    const int vp_h = height();
    if (vp_w < 180 || vp_h < 180) return;

    GLint orig_viewport[4];
    glGetIntegerv(GL_VIEWPORT, orig_viewport);

    // Compute exact DPI / framebuffer scale factor:
    // orig_viewport[2] and [3] are physical framebuffer dimensions
    const float scale_x = static_cast<float>(orig_viewport[2]) / static_cast<float>(std::max(1, vp_w));
    const float scale_y = static_cast<float>(orig_viewport[3]) / static_cast<float>(std::max(1, vp_h));

    // OpenGL viewport origin is bottom-left
    const GLint vx = orig_viewport[0] + static_cast<GLint>(pad * scale_x);
    const GLint vy = orig_viewport[1] + static_cast<GLint>((vp_h - pad - size) * scale_y);
    const GLsizei vw = static_cast<GLsizei>(size * scale_x);
    const GLsizei vh = static_cast<GLsizei>(size * scale_y);

    glViewport(vx, vy, vw, vh);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    const float ortho_dim = 1.45f;
    glOrtho(-ortho_dim, ortho_dim, -ortho_dim, ortho_dim, -10.0f, 10.0f);

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ── 1. Background dark translucent backing pill/card (matching ImGuizmo background) ──
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glColor4f(0.07f, 0.086f, 0.117f, 0.88f); // #12161EEB
    glBegin(GL_QUADS);
    glVertex3f(-ortho_dim, -ortho_dim, -9.0f);
    glVertex3f( ortho_dim, -ortho_dim, -9.0f);
    glVertex3f( ortho_dim,  ortho_dim, -9.0f);
    glVertex3f(-ortho_dim,  ortho_dim, -9.0f);
    glEnd();

    // Subtle dark border around view cube card
    glColor4f(0.20f, 0.24f, 0.32f, 0.80f);
    glLineWidth(1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(-ortho_dim, -ortho_dim, -8.9f);
    glVertex3f( ortho_dim, -ortho_dim, -8.9f);
    glVertex3f( ortho_dim,  ortho_dim, -8.9f);
    glVertex3f(-ortho_dim,  ortho_dim, -8.9f);
    glEnd();

    // ── 2. Rotate the cube matching camera orientation ──
    float cube_view[16];
    get_cube_view_matrix(cube_view);
    glMultMatrixf(cube_view);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);

    // ── 2. Render the 9 panels for each of the 6 faces (26 clickable sub-boxes) ──
    static const float dir_unary[3][3] = { {1,0,0}, {0,1,0}, {0,0,1} };
    static const float panel_pos[9][2] = {
        {0.75f,0.75f}, {0.25f,0.75f}, {0.0f,0.75f},
        {0.75f,0.25f}, {0.25f,0.25f}, {0.0f,0.25f},
        {0.75f,0.0f},  {0.25f,0.0f},  {0.0f,0.0f}
    };
    static const float panel_sz[9][2] = {
        {0.25f,0.25f}, {0.50f,0.25f}, {0.25f,0.25f},
        {0.25f,0.50f}, {0.50f,0.50f}, {0.25f,0.50f},
        {0.25f,0.25f}, {0.50f,0.25f}, {0.25f,0.25f}
    };
    // Face base colors (X=Red, Y=Green, Z=Blue)
    static const float face_colors[3][3] = {
        {0.86f, 0.22f, 0.22f}, // X (Red)
        {0.22f, 0.78f, 0.28f}, // Y (Green)
        {0.20f, 0.44f, 0.90f}  // Z (Blue)
    };

    for (int iFace = 0; iFace < 6; ++iFace) {
        const int norm_idx = (iFace % 3);
        const int perp_x   = (norm_idx + 1) % 3;
        const int perp_y   = (norm_idx + 2) % 3;
        const float invert = (iFace > 2) ? -1.0f : 1.0f;

        const float n[3] = { dir_unary[norm_idx][0] * invert,
                             dir_unary[norm_idx][1] * invert,
                             dir_unary[norm_idx][2] * invert };

        const float dx[3] = { dir_unary[perp_x][0], dir_unary[perp_x][1], dir_unary[perp_x][2] };
        const float dy[3] = { dir_unary[perp_y][0], dir_unary[perp_y][1], dir_unary[perp_y][2] };
        const float origin[3] = { dir_unary[norm_idx][0] - dx[0] - dy[0],
                                  dir_unary[norm_idx][1] - dx[1] - dy[1],
                                  dir_unary[norm_idx][2] - dx[2] - dy[2] };
        const float idx_vec_x[3] = { dx[0] * invert, dx[1] * invert, dx[2] * invert };
        const float idx_vec_y[3] = { dy[0] * invert, dy[1] * invert, dy[2] * invert };
        const float box_origin[3] = { dir_unary[norm_idx][0] * -invert - idx_vec_x[0] - idx_vec_y[0],
                                      dir_unary[norm_idx][1] * -invert - idx_vec_x[1] - idx_vec_y[1],
                                      dir_unary[norm_idx][2] * -invert - idx_vec_x[2] - idx_vec_y[2] };

        for (int iPanel = 0; iPanel < 9; ++iPanel) {
            float bx = box_origin[0] + idx_vec_x[0]*(iPanel%3) + idx_vec_y[0]*(iPanel/3) + 1.0f;
            float by = box_origin[1] + idx_vec_x[1]*(iPanel%3) + idx_vec_y[1]*(iPanel/3) + 1.0f;
            float bz = box_origin[2] + idx_vec_x[2]*(iPanel%3) + idx_vec_y[2]*(iPanel/3) + 1.0f;
            int box_id = static_cast<int>(std::round(bx * 9.0f + by * 3.0f + bz));

            const float px = panel_pos[iPanel][0] * 2.0f;
            const float py = panel_pos[iPanel][1] * 2.0f;
            const float sx = panel_sz[iPanel][0] * 2.0f;
            const float sy = panel_sz[iPanel][1] * 2.0f;

            float p0[3] = { (dx[0]*px + dy[0]*py + origin[0]) * 0.5f * invert,
                            (dx[1]*px + dy[1]*py + origin[1]) * 0.5f * invert,
                            (dx[2]*px + dy[2]*py + origin[2]) * 0.5f * invert };
            float p1[3] = { (dx[0]*px + dy[0]*(py+sy) + origin[0]) * 0.5f * invert,
                            (dx[1]*px + dy[1]*(py+sy) + origin[1]) * 0.5f * invert,
                            (dx[2]*px + dy[2]*(py+sy) + origin[2]) * 0.5f * invert };
            float p2[3] = { (dx[0]*(px+sx) + dy[0]*(py+sy) + origin[0]) * 0.5f * invert,
                            (dx[1]*(px+sx) + dy[1]*(py+sy) + origin[1]) * 0.5f * invert,
                            (dx[2]*(px+sx) + dy[2]*(py+sy) + origin[2]) * 0.5f * invert };
            float p3[3] = { (dx[0]*(px+sx) + dy[0]*py + origin[0]) * 0.5f * invert,
                            (dx[1]*(px+sx) + dy[1]*py + origin[1]) * 0.5f * invert,
                            (dx[2]*(px+sx) + dy[2]*py + origin[2]) * 0.5f * invert };

            bool is_hot = (m_view_cube_over_box == box_id);
            float base_r = face_colors[norm_idx][0];
            float base_g = face_colors[norm_idx][1];
            float base_b = face_colors[norm_idx][2];

            if (is_hot) {
                // Gold / bright selection color (ImGuizmo SELECTION)
                glColor4f(1.0f, 0.88f, 0.20f, 0.98f);
            } else {
                // Dim sub-panel shading: center face is slightly lighter
                float factor = (iPanel == 4) ? 0.90f : 0.65f;
                glColor4f(base_r * factor, base_g * factor, base_b * factor, 0.82f);
            }

            glBegin(GL_QUADS);
            glVertex3fv(p0);
            glVertex3fv(p1);
            glVertex3fv(p2);
            glVertex3fv(p3);
            glEnd();

            // Crisp border
            glColor4f(0.12f, 0.14f, 0.18f, 0.90f);
            glLineWidth(1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex3fv(p0);
            glVertex3fv(p1);
            glVertex3fv(p2);
            glVertex3fv(p3);
            glEnd();

            // Face center label letters ('X', 'Y', 'Z')
            if (iPanel == 4) {
                glColor4f(1.0f, 1.0f, 1.0f, 0.95f);
                glLineWidth(2.2f);
                const float lsz = 0.12f;
                float pc[3] = {
                    (p0[0] + p1[0] + p2[0] + p3[0]) * 0.25f + n[0] * 0.02f,
                    (p0[1] + p1[1] + p2[1] + p3[1]) * 0.25f + n[1] * 0.02f,
                    (p0[2] + p1[2] + p2[2] + p3[2]) * 0.25f + n[2] * 0.02f
                };
                float tx[3] = { dx[0] * lsz, dx[1] * lsz, dx[2] * lsz };
                float ty[3] = { dy[0] * lsz, dy[1] * lsz, dy[2] * lsz };

                glBegin(GL_LINES);
                if (norm_idx == 0) { // 'X'
                    glVertex3f(pc[0] - tx[0] - ty[0], pc[1] - tx[1] - ty[1], pc[2] - tx[2] - ty[2]);
                    glVertex3f(pc[0] + tx[0] + ty[0], pc[1] + tx[1] + ty[1], pc[2] + tx[2] + ty[2]);
                    glVertex3f(pc[0] - tx[0] + ty[0], pc[1] - tx[1] + ty[1], pc[2] - tx[2] + ty[2]);
                    glVertex3f(pc[0] + tx[0] - ty[0], pc[1] + tx[1] - ty[1], pc[2] + tx[2] - ty[2]);
                } else if (norm_idx == 1) { // 'Y'
                    glVertex3f(pc[0] - tx[0] + ty[0], pc[1] - tx[1] + ty[1], pc[2] - tx[2] + ty[2]);
                    glVertex3f(pc[0], pc[1], pc[2]);
                    glVertex3f(pc[0] + tx[0] + ty[0], pc[1] + tx[1] + ty[1], pc[2] + tx[2] + ty[2]);
                    glVertex3f(pc[0], pc[1], pc[2]);
                    glVertex3f(pc[0], pc[1], pc[2]);
                    glVertex3f(pc[0] - ty[0], pc[1] - ty[1], pc[2] - ty[2]);
                } else if (norm_idx == 2) { // 'Z'
                    glVertex3f(pc[0] - tx[0] + ty[0], pc[1] - tx[1] + ty[1], pc[2] - tx[2] + ty[2]);
                    glVertex3f(pc[0] + tx[0] + ty[0], pc[1] + tx[1] + ty[1], pc[2] + tx[2] + ty[2]);
                    glVertex3f(pc[0] + tx[0] + ty[0], pc[1] + tx[1] + ty[1], pc[2] + tx[2] + ty[2]);
                    glVertex3f(pc[0] - tx[0] - ty[0], pc[1] - tx[1] - ty[1], pc[2] - tx[2] - ty[2]);
                    glVertex3f(pc[0] - tx[0] - ty[0], pc[1] - tx[1] - ty[1], pc[2] - tx[2] - ty[2]);
                    glVertex3f(pc[0] + tx[0] - ty[0], pc[1] + tx[1] - ty[1], pc[2] + tx[2] - ty[2]);
                }
                glEnd();
                glLineWidth(1.0f);
            }
        }
    }

    // ── 3. Coordinate axis indicators extending from the cube ──
    glLineWidth(2.5f);
    glBegin(GL_LINES);
    // X (Red)
    glColor4f(1.0f, 0.22f, 0.22f, 0.95f);
    glVertex3f(0, 0, 0); glVertex3f(1.15f, 0, 0);
    // Y (Green)
    glColor4f(0.22f, 1.0f, 0.30f, 0.95f);
    glVertex3f(0, 0, 0); glVertex3f(0, 1.15f, 0);
    // Z (Blue)
    glColor4f(0.25f, 0.55f, 1.0f, 0.95f);
    glVertex3f(0, 0, 0); glVertex3f(0, 0, 1.15f);
    glEnd();
    glLineWidth(1.0f);

    // Restore state and viewport
    glDisable(GL_BLEND);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glViewport(orig_viewport[0], orig_viewport[1], orig_viewport[2], orig_viewport[3]);
    glEnable(GL_LIGHTING);
}

// ============================================================================
// Camera Bounds & Smart Placement Operations
// ============================================================================

void Viewport3DWidget::toggle_show_camera_bounds(bool show) {
    m_show_camera_bounds = show;
    if (m_bounds_btn && m_bounds_btn->isChecked() != show) {
        QSignalBlocker blocker(m_bounds_btn);
        m_bounds_btn->setChecked(show);
    }
    if (!show) {
        m_camera_bounds_selected = false;
        m_bounds_hover_handle = BoundsHandle::None;
        m_bounds_active_handle = BoundsHandle::None;
        m_bounds_dragging = false;
        unsetCursor();
    }
    update();
}

bool Viewport3DWidget::has_camera_bounds() const {
    if (!has_scene()) return false;
    av::CameraBounds cb;
    return av::scene_get_camera_bounds(m_scene, cb);
}

av::CameraBounds Viewport3DWidget::camera_bounds() const {
    av::CameraBounds cb;
    if (has_scene()) {
        av::scene_get_camera_bounds(m_scene, cb);
    }
    return cb;
}

void Viewport3DWidget::set_camera_bounds(const av::CameraBounds& cb) {
    if (!has_scene()) return;
    const std::string before = capture_scene_snapshot();
    av::scene_set_camera_bounds(m_scene, cb);
    emit cameraBoundsChanged(cb);
    emit sceneEdited();
    update();
    push_scene_snapshot_undo(before, QStringLiteral("Set Camera Bounds"));
}

void Viewport3DWidget::fit_camera_bounds() {
    if (!has_scene()) return;
    av::CameraBounds cb = av::scene_fit_camera_bounds_to_level(m_scene);
    set_camera_bounds(cb);
    toggle_show_camera_bounds(true);
}

void Viewport3DWidget::remove_camera_bounds() {
    if (!has_scene()) return;
    const std::string before = capture_scene_snapshot();
    av::scene_remove_camera_bounds(m_scene);
    av::CameraBounds empty;
    emit cameraBoundsChanged(empty);
    emit sceneEdited();
    toggle_show_camera_bounds(false);
    update();
    push_scene_snapshot_undo(before, QStringLiteral("Remove Camera Bounds"));
}

void Viewport3DWidget::frame_camera_bounds() {
    if (!has_scene()) return;
    av::CameraBounds cb;
    if (!av::scene_get_camera_bounds(m_scene, cb)) return;
    toggle_show_camera_bounds(true);
    m_cam_target[0] = cb.center_x();
    m_cam_target[1] = cb.center_y();
    m_cam_target[2] = 0.0f;
    m_cam_pitch = 0.0f;
    m_cam_yaw = 0.0f;
    const float max_dim = std::max(cb.width, cb.height * 1.33f);
    m_cam_dist = std::max(300.0f, max_dim * 1.25f);
    update();
}

void Viewport3DWidget::show_viewport_context_menu(const QPoint& screen_pos) {
    if (!has_scene()) return;

    QPoint widget_pos = mapFromGlobal(screen_pos);
    float wx = 0.0f, wy = 0.0f;
    CameraBoundsGizmo::screen_to_world_xy(widget_pos.x(), widget_pos.y(),
                                          width(), height(),
                                          m_gizmo_view, m_gizmo_proj,
                                          wx, wy);

    float snap_y = av::scene_terrain_top_y(m_scene, wx);
    if (std::abs(snap_y - wy) < 150.0f) {
        wy = snap_y;
    }
    const float spawn_pos[3] = { wx, wy, 0.0f };

    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #1e222b; color: #dce1e8; border: 1px solid #333a48; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 5px 24px 5px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #3b82f6; color: #ffffff; }"
        "QMenu::separator { height: 1px; background: #2f3542; margin: 4px 8px; }"
    ));

    QMenu* add_sub = menu.addMenu(QStringLiteral("Add Object Here"));
    add_sub->addAction(QStringLiteral("Spawn Point"), this, [this, spawn_pos]() {
        add_scene_object(QStringLiteral("Spawn"), spawn_pos);
    });
    add_sub->addAction(QStringLiteral("Portal Gate"), this, [this, spawn_pos]() {
        add_scene_object(QStringLiteral("Portal"), spawn_pos);
    });
    add_sub->addAction(QStringLiteral("Empty Object"), this, [this, spawn_pos]() {
        add_scene_object(QStringLiteral("Empty"), spawn_pos);
    });
    add_sub->addAction(QStringLiteral("Choose Model (.pod)..."), this, [this, spawn_pos]() {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select Model"),
                                                          QString(), QStringLiteral("POD Models (*.pod)"));
        if (!file.isEmpty()) {
            add_model_object(file, QString(), spawn_pos);
        }
    });

    menu.addSeparator();

    QMenu* bounds_sub = menu.addMenu(QStringLiteral("Camera Bounds"));
    bounds_sub->addAction(QStringLiteral("Fit to Scene Level"), this, [this]() {
        fit_camera_bounds();
    });
    bounds_sub->addAction(QStringLiteral("Frame Bounds in View"), this, [this]() {
        frame_camera_bounds();
    });
    bounds_sub->addAction(m_show_camera_bounds ? QStringLiteral("Hide Bounds Shroud") : QStringLiteral("Show Bounds Shroud"),
                          this, [this]() {
        toggle_show_camera_bounds(!m_show_camera_bounds);
    });
    if (has_camera_bounds()) {
        bounds_sub->addAction(QStringLiteral("Remove Camera Bounds"), this, [this]() {
            remove_camera_bounds();
        });
    } else {
        bounds_sub->addAction(QStringLiteral("Create Camera Bounds"), this, [this]() {
            fit_camera_bounds();
        });
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("Center Camera Here"), this, [this, wx, wy]() {
        m_cam_target[0] = wx;
        m_cam_target[1] = wy;
        m_cam_target[2] = 0.0f;
        update();
    });
    menu.addAction(QStringLiteral("Reset View"), this, [this]() {
        reset_camera();
    });

    menu.exec(screen_pos);
}

// ── Drag & Drop Overrides ──

void Viewport3DWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (!m_has_scene) {
        event->ignore();
        return;
    }
    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(QStringLiteral("application/x-ruby-template")) ||
        mime->hasFormat(QStringLiteral("application/x-ruby-model")) ||
        mime->hasUrls() || mime->hasText()) {
        event->acceptProposedAction();
        m_drag_hover_active = true;
        update();
    } else {
        event->ignore();
    }
}

void Viewport3DWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (!m_has_scene) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    m_drag_hover_screen_pos = event->position();
    float wx = 0.0f, wy = 0.0f;
    CameraBoundsGizmo::screen_to_world_xy(event->position().x(), event->position().y(),
                                          width(), height(),
                                          m_gizmo_view, m_gizmo_proj,
                                          wx, wy);
    float snap_y = av::scene_terrain_top_y(m_scene, wx);
    if (std::abs(snap_y - wy) < 150.0f) {
        wy = snap_y;
    }
    m_drag_hover_world[0] = wx;
    m_drag_hover_world[1] = wy;
    m_drag_hover_world[2] = 0.0f;

    const QMimeData* mime = event->mimeData();
    if (mime->hasFormat(QStringLiteral("application/x-ruby-template"))) {
        m_drag_hover_label = QString::fromUtf8(mime->data(QStringLiteral("application/x-ruby-template")));
    } else if (mime->hasFormat(QStringLiteral("application/x-ruby-model"))) {
        m_drag_hover_label = QFileInfo(QString::fromUtf8(mime->data(QStringLiteral("application/x-ruby-model")))).baseName();
    } else if (mime->hasUrls() && !mime->urls().isEmpty()) {
        m_drag_hover_label = mime->urls().first().fileName();
    } else if (mime->hasText()) {
        m_drag_hover_label = mime->text();
    }
    m_drag_hover_active = true;
    update();
}

void Viewport3DWidget::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    m_drag_hover_active = false;
    update();
}

void Viewport3DWidget::dropEvent(QDropEvent* event) {
    m_drag_hover_active = false;
    if (!m_has_scene) {
        event->ignore();
        return;
    }
    const QMimeData* mime = event->mimeData();
    float wx = 0.0f, wy = 0.0f;
    CameraBoundsGizmo::screen_to_world_xy(event->position().x(), event->position().y(),
                                          width(), height(),
                                          m_gizmo_view, m_gizmo_proj,
                                          wx, wy);
    float snap_y = av::scene_terrain_top_y(m_scene, wx);
    if (std::abs(snap_y - wy) < 150.0f) {
        wy = snap_y;
    }
    const float spawn_pos[3] = { wx, wy, 0.0f };

    if (mime->hasFormat(QStringLiteral("application/x-ruby-template"))) {
        QString templ = QString::fromUtf8(mime->data(QStringLiteral("application/x-ruby-template")));
        add_template_object(templ, nullptr, 1.0f, spawn_pos);
        event->acceptProposedAction();
    } else if (mime->hasFormat(QStringLiteral("application/x-ruby-model"))) {
        QString pod = QString::fromUtf8(mime->data(QStringLiteral("application/x-ruby-model")));
        add_model_object(pod, QString(), spawn_pos);
        event->acceptProposedAction();
    } else if (mime->hasUrls()) {
        bool handled = false;
        for (const QUrl& url : mime->urls()) {
            QString path = url.toLocalFile();
            if (path.endsWith(QStringLiteral(".pod"), Qt::CaseInsensitive)) {
                add_model_object(path, QString(), spawn_pos);
                handled = true;
                break;
            }
        }
        if (handled) event->acceptProposedAction();
        else event->ignore();
    } else if (mime->hasText()) {
        QString txt = mime->text().trimmed();
        if (txt.endsWith(QStringLiteral(".pod"), Qt::CaseInsensitive)) {
            add_model_object(txt, QString(), spawn_pos);
            event->acceptProposedAction();
        } else {
            add_template_object(txt, nullptr, 1.0f, spawn_pos);
            event->acceptProposedAction();
        }
    } else {
        event->ignore();
    }
    update();
}

} // namespace ruby::viewport
