#include "ruby_quick_viewport.h"
#include "gles_shaders.h"
#include "platform/pvr_loader.h"
#include "tools/scene_asset_resolver.h"
#include "tools/scene_workspace.h"
#include <QThreadPool>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QQuickWindow>
#include <QTouchEvent>
#include <QMouseEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QFile>
#include <QImage>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <atomic>
#include <functional>
#include <set>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace ruby::android {

static inline void calc_object_world_matrix(const av::SceneObject& obj, float out[16]) {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    QMatrix4x4 m;
    m.translate(obj.pos_x, obj.pos_y, obj.pos_z);
    m.rotate(obj.rot_x * kRadToDeg, 1.0f, 0.0f, 0.0f);
    m.rotate(obj.rot_z * kRadToDeg, 0.0f, 1.0f, 0.0f);
    m.rotate(obj.rot_y * kRadToDeg, 0.0f, 0.0f, 1.0f);
    float s = obj.template_scaling;
    m.scale(obj.scale_x * s, obj.scale_y * s, obj.scale_z * s);
    std::memcpy(out, m.constData(), 16 * sizeof(float));
}

static inline void calc_object_render_matrix(const av::SceneObject& obj, float out[16]) {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    QMatrix4x4 m;
    m.translate(obj.pos_x, obj.pos_y, obj.pos_z);
    m.rotate(obj.rot_x * kRadToDeg, 1.0f, 0.0f, 0.0f);
    m.rotate(obj.rot_z * kRadToDeg, 0.0f, 1.0f, 0.0f);
    m.rotate(obj.rot_y * kRadToDeg, 0.0f, 0.0f, 1.0f);
    float s = obj.template_scaling;
    m.scale(obj.scale_x * s, obj.scale_y * s, obj.scale_z * s);

    if (obj.has_model_x_rotation || obj.has_model_y_rotation) {
        if (obj.has_model_x_rotation) {
            m.rotate(obj.model_x_rotation * kRadToDeg, 1.0f, 0.0f, 0.0f);
        }
        if (obj.has_model_y_rotation) {
            m.rotate(obj.model_y_rotation * kRadToDeg, 0.0f, 1.0f, 0.0f);
        }
    }

    if (obj.has_model_origin) {
        m.translate(-obj.model_origin[0], -obj.model_origin[1], -obj.model_origin[2]);
    }

    std::memcpy(out, m.constData(), 16 * sizeof(float));
}

struct FrustumPlane {
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
    void normalize() {
        float len = std::sqrt(a * a + b * b + c * c);
        if (len > 1e-6f) {
            float inv = 1.0f / len;
            a *= inv; b *= inv; c *= inv; d *= inv;
        }
    }
    float distance(float x, float y, float z) const {
        return a * x + b * y + c * z + d;
    }
};

struct Frustum {
    FrustumPlane planes[6];

    static Frustum fromMatrix(const QMatrix4x4& m) {
        const float* d = m.constData(); // column-major
        Frustum f;
        // Left: row3 + row0
        f.planes[0].a = d[3] + d[0];
        f.planes[0].b = d[7] + d[4];
        f.planes[0].c = d[11] + d[8];
        f.planes[0].d = d[15] + d[12];
        // Right: row3 - row0
        f.planes[1].a = d[3] - d[0];
        f.planes[1].b = d[7] - d[4];
        f.planes[1].c = d[11] - d[8];
        f.planes[1].d = d[15] - d[12];
        // Bottom: row3 + row1
        f.planes[2].a = d[3] + d[1];
        f.planes[2].b = d[7] + d[5];
        f.planes[2].c = d[11] + d[9];
        f.planes[2].d = d[15] + d[13];
        // Top: row3 - row1
        f.planes[3].a = d[3] - d[1];
        f.planes[3].b = d[7] - d[5];
        f.planes[3].c = d[11] - d[9];
        f.planes[3].d = d[15] - d[13];
        // Near: row3 + row2
        f.planes[4].a = d[3] + d[2];
        f.planes[4].b = d[7] + d[6];
        f.planes[4].c = d[11] + d[10];
        f.planes[4].d = d[15] + d[14];
        // Far: row3 - row2
        f.planes[5].a = d[3] - d[2];
        f.planes[5].b = d[7] - d[6];
        f.planes[5].c = d[11] - d[10];
        f.planes[5].d = d[15] - d[14];

        for (int i = 0; i < 6; ++i) f.planes[i].normalize();
        return f;
    }

    bool isSphereVisible(float x, float y, float z, float radius) const {
        for (int i = 0; i < 6; ++i) {
            if (planes[i].distance(x, y, z) < -radius) {
                return false;
            }
        }
        return true;
    }
};

static QImage decode_image_file(const std::string& path) {
    if (path.empty()) return {};
    const fs::path file(path);
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return {};

    std::string low_path = file.string();
    for (auto& c : low_path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string ext = file.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const bool is_pvr_tex = (low_path.size() >= 8 && low_path.substr(low_path.size() - 8) == ".tex.png") ||
                            ext == ".pvr" || ext == ".tex";

    QImage result;
    if (is_pvr_tex) {
        QFile input(QString::fromStdString(file.string()));
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
        result = QImage(QString::fromStdString(file.string()));
    }

    if (result.isNull()) {
        QFile input(QString::fromStdString(file.string()));
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
    return result;
}

// ─── Parallel asset loading ────────────────────────────────────────────────
//
// Scene load used to walk every object SERIALLY on one worker: resolve the POD,
// parse it, decode each of its textures, then the next object.  Measured on this
// machine, decoding every shipped texture takes ~1.24 s and parsing every shipped
// POD ~237 ms — and a phone is several times slower — all of it on one core out
// of sixteen.  The work is embarrassingly parallel: each item is an independent
// file.
//
// No QtConcurrent dependency: a list of independent decodes only needs an atomic
// work cursor.  `fn(i)` must be safe to call concurrently and must write only to
// slot `i` of caller-owned storage, which keeps the results lock-free.
//
// Thread count is capped well below hardware_concurrency() on purpose: phones
// are big.LITTLE with tight thermal budgets, and saturating the little cores
// alongside the big ones makes a load take longer, not less.
static void parallel_for(size_t count, unsigned max_threads,
                         const std::function<void(size_t)>& fn) {
    if (count == 0) return;
    const unsigned hw = std::thread::hardware_concurrency();
    unsigned n = std::min<unsigned>(max_threads ? max_threads : (hw ? hw : 1u),
                                    static_cast<unsigned>(count));
    if (n < 1u) n = 1u;
    if (n == 1u) {
        for (size_t i = 0; i < count; ++i) fn(i);
        return;
    }
    std::atomic<size_t> cursor{0};
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; ++t) {
        pool.emplace_back([&]() {
            for (;;) {
                const size_t i = cursor.fetch_add(1, std::memory_order_relaxed);
                if (i >= count) return;
                fn(i);
            }
        });
    }
    for (auto& th : pool) th.join();
}

// Loading is I/O + decode bound, so this is not a compute-pool size.
static constexpr unsigned kSceneLoadThreads = 6;

// ─── RubyObjectListModel ───────────────────────────────────────────────────

RubyObjectListModel::RubyObjectListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int RubyObjectListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_objects.size();
}

QVariant RubyObjectListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_objects.size()) {
        return QVariant();
    }

    const auto& item = m_objects.at(index.row());
    switch (role) {
    case NameRole: return item.name;
    case TemplateRole: return item.templateName;
    case HiddenRole: return item.hidden;
    case IndexRole: return index.row();
    default: return QVariant();
    }
}

QHash<int, QByteArray> RubyObjectListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[TemplateRole] = "template";
    roles[HiddenRole] = "hidden";
    roles[IndexRole] = "index";
    return roles;
}

void RubyObjectListModel::updateFromScene(const av::SceneData& scene) {
    beginResetModel();
    m_objects.clear();
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        const auto& obj = scene.objects[i];
        SceneObjectItem item;
        item.name = obj.name.empty() ? QStringLiteral("Object #%1").arg(i) : QString::fromStdString(obj.name);
        item.templateName = QString::fromStdString(obj.template_name);
        item.hidden = obj.hidden;
        m_objects.append(item);
    }
    endResetModel();
}

void RubyObjectListModel::toggleHidden(int idx) {
    if (idx >= 0 && idx < m_objects.size()) {
        m_objects[idx].hidden = !m_objects[idx].hidden;
        QModelIndex modelIdx = index(idx);
        emit dataChanged(modelIdx, modelIdx, {HiddenRole});
    }
}

// ─── RubyViewportRenderer ──────────────────────────────────────────────────

struct MeshGpu {
    GLuint pos_vbo = 0;
    GLuint nrm_vbo = 0;
    GLuint uv_vbo = 0;
    GLuint ebo = 0;
    int vertex_count = 0;
    int index_count = 0;
    bool valid() const { return pos_vbo != 0; }
    void destroy(QOpenGLFunctions* gl) {
        if (pos_vbo) { gl->glDeleteBuffers(1, &pos_vbo); pos_vbo = 0; }
        if (nrm_vbo) { gl->glDeleteBuffers(1, &nrm_vbo); nrm_vbo = 0; }
        if (uv_vbo) { gl->glDeleteBuffers(1, &uv_vbo); uv_vbo = 0; }
        if (ebo) { gl->glDeleteBuffers(1, &ebo); ebo = 0; }
        vertex_count = 0;
        index_count = 0;
    }
};

struct GroundMeshGpu {
    std::vector<MeshGpu> meshes;
    std::vector<GLuint> textures;
};

static std::vector<fs::path> build_asset_roots(const fs::path& scene_or_model_path) {
    std::vector<fs::path> roots;
    if (scene_or_model_path.has_parent_path()) {
        fs::path p = scene_or_model_path.parent_path();
        roots.push_back(p);
        roots.push_back(p / "resources");
        roots.push_back(p / "models");
        roots.push_back(p / "background");
        roots.push_back(p / "textures");
        if (p.has_parent_path()) {
            fs::path pp = p.parent_path();
            roots.push_back(pp);
            roots.push_back(pp / "resources");
            roots.push_back(pp / "models");
            roots.push_back(pp / "background");
            roots.push_back(pp / "textures");
            roots.push_back(pp / "assets");
            roots.push_back(pp / "assets" / "resources");
            roots.push_back(pp / "assets" / "models");
            roots.push_back(pp / "assets" / "background");
        }
    }
    roots.push_back(fs::path("/sdcard/Swordigo/assets/resources"));
    roots.push_back(fs::path("/sdcard/Download/Swordigo/resources"));
    return roots;
}

class RubyViewportRenderer : public QQuickFramebufferObject::Renderer, protected QOpenGLFunctions {
public:
    RubyViewportRenderer() {
        initializeOpenGLFunctions();
        initShaders();
        initGrid();
    }

    ~RubyViewportRenderer() override {
        delete m_shader;
        delete m_unlitShader;
        delete m_outlineShader;
        if (m_gridVbo) glDeleteBuffers(1, &m_gridVbo);
        if (m_cubeVbo) glDeleteBuffers(1, &m_cubeVbo);
        for (auto& ggpu : m_sceneGroundGpu) {
            for (auto& g : ggpu.meshes) g.destroy(this);
        }
        for (auto& pair : m_podGpuCache) {
            for (auto& g : pair.second) g.destroy(this);
        }
        for (auto& g : m_modelGpuMeshes) {
            g.destroy(this);
        }
        for (auto& pair : m_texCache) {
            if (pair.second) glDeleteTextures(1, &pair.second);
        }
        if (m_bgTex) {
            glDeleteTextures(1, &m_bgTex);
            m_bgTex = 0;
        }
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(4); // 4x MSAA
        return new QOpenGLFramebufferObject(size, format);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* vp = static_cast<RubyQuickViewport*>(item);
        m_pitch = vp->m_cameraPitch;
        m_yaw = vp->m_cameraYaw;
        m_distance = vp->m_cameraDistance;
        m_target = vp->m_cameraTarget;
        m_gizmoMode = vp->m_gizmoMode;
        m_selectedObject = vp->m_selectedObject;
        m_frame = vp->m_frame;
        m_meshEditActive = vp->m_meshEditActive;
        m_meshEditObject = vp->m_meshEditObject;
        m_selectedMeshVertex = vp->m_selectedMeshVertex;
        m_meshEditPoints = vp->m_meshEditPoints;

        if (vp->m_modelLoadedFlag) {
            m_hasModel = vp->m_hasModel;
            m_hasScene = false;
            m_model = vp->m_model;
            m_modelPath = vp->m_filePath.toStdString();
            m_modelGpuValid = false;
            vp->m_modelLoadedFlag = false;
        } else if (vp->m_sceneLoadedFlag) {
            m_hasScene = vp->m_hasScene;
            m_hasModel = false;
            m_sceneObjects = vp->m_scene.objects;
            m_sceneRevision = vp->m_sceneRevision.load(std::memory_order_acquire);
            m_geomRevision  = vp->m_sceneGeometryRevision.load(std::memory_order_acquire);
            m_scenePath = vp->m_filePath.toStdString();
            m_sceneGroundGpuValid = false;
            m_boundsValid = false;
            m_preloadedPods = std::move(vp->m_preloadedPods);
            m_predecodedImages = std::move(vp->m_predecodedImages);
            vp->m_sceneLoadedFlag = false;
        } else if (m_hasScene) {
            // Only when the GUI thread says something actually changed.  This is
            // the fix: a scene sitting still now costs ZERO copies per frame,
            // where it previously cost the whole terrain once a frame.
            const uint64_t rev = vp->m_sceneRevision.load(std::memory_order_acquire);
            if (rev != m_sceneRevision) {
                m_sceneRevision = rev;
                m_sceneObjects = vp->m_scene.objects;
                m_boundsValid = false;
            }
        }

        // Ground meshes only need re-uploading when geometry actually changed,
        // and the cull bounds derive from that same geometry, so both are
        // invalidated together by the revision checks below.
        // not when an object was merely moved.  Keeping these two revisions
        // apart is what stops a nudge from destroying and rebuilding every
        // terrain buffer in the level.
        const uint64_t grev = vp->m_sceneGeometryRevision.load(std::memory_order_acquire);
        if (grev != m_geomRevision) {
            m_geomRevision = grev;
            m_sceneGroundGpuValid = false;
            m_boundsValid = false;
        }
    }

    void render() override {
        glViewport(0, 0, framebufferObject()->width(), framebufferObject()->height());
        // Dark Studio surface (#121316)
        glClearColor(0.07f, 0.075f, 0.086f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE); // Two-sided geometry for level terrain & foliage

        float aspect = float(framebufferObject()->width()) / qMax(1.0f, float(framebufferObject()->height()));
        QMatrix4x4 proj;
        proj.perspective(45.0f, aspect, 1.0f, 30000.0f);

        float radPitch = m_pitch * M_PI / 180.0f;
        float radYaw = m_yaw * M_PI / 180.0f;
        QVector3D eye = m_target + QVector3D(
            m_distance * std::cos(radPitch) * std::sin(radYaw),
            m_distance * std::sin(radPitch),
            m_distance * std::cos(radPitch) * std::cos(radYaw)
        );

        QVector3D forward = (m_target - eye).normalized();
        QVector3D up(0.0f, 1.0f, 0.0f);
        if (std::abs(QVector3D::dotProduct(forward, up)) > 0.99f) {
            up = (forward.y() > 0.0f) ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 0.0f, -1.0f);
        }

        QMatrix4x4 view;
        view.lookAt(eye, m_target, up);

        // Draw Reference Grid
        renderGrid(view, proj);

        // Upload single model GPU data if needed
        if (m_hasModel && !m_modelGpuValid) {
            m_modelGpuMeshes = upload_pod_gpu(m_model);
            m_modelTextures = load_pod_textures(m_modelPath, m_model, build_asset_roots(fs::path(m_modelPath)));
            m_modelGpuValid = true;
        }

        // Draw 3D Model or Scene
        if (m_shader && m_shader->isLinked()) {
            if (m_hasModel && m_modelGpuValid) {
                m_shader->bind();
                QVector3D sunDirView = view.mapVector(QVector3D(0.4f, 0.8f, 0.5f)).normalized();
                m_shader->setUniformValue("uSunDir", sunDirView);
                m_shader->setUniformValue("uSunColor", QVector3D(1.35f, 1.30f, 1.25f));
                m_shader->setUniformValue("uSkyColor", QVector3D(0.55f, 0.60f, 0.70f));
                m_shader->setUniformValue("uGroundColor", QVector3D(0.30f, 0.32f, 0.35f));

                draw_pod_instance(m_model, m_modelTextures, m_modelGpuMeshes, QMatrix4x4(), view, proj);
                m_shader->release();
            } else if (m_hasScene) {
                const std::vector<fs::path>& roots = asset_roots_cached(m_scenePath);

                if (!m_sceneGroundGpuValid || m_sceneGroundGpu.size() != m_sceneObjects.size()) {
                    upload_ground_meshes_gpu(roots);
                    setup_background(roots);
                }

                if (m_bgTex) {
                    renderBackground(view, proj);
                }

                m_shader->bind();
                QVector3D sunDirView = view.mapVector(QVector3D(0.4f, 0.8f, 0.5f)).normalized();
                m_shader->setUniformValue("uSunDir", sunDirView);
                m_shader->setUniformValue("uSunColor", QVector3D(1.35f, 1.30f, 1.25f));
                m_shader->setUniformValue("uSkyColor", QVector3D(0.55f, 0.60f, 0.70f));
                m_shader->setUniformValue("uGroundColor", QVector3D(0.30f, 0.32f, 0.35f));

                Frustum frustum = Frustum::fromMatrix(proj * view);
                QVector3D camPos = eye;

                for (size_t i = 0; i < m_sceneObjects.size(); ++i) {
                    const auto& obj = m_sceneObjects[i];
                    if (obj.hidden) continue;

                    // Bounds-driven culling. m_bounds is rebuilt only when the
                    // scene (or its geometry) changes, so a still scene pays
                    // nothing per frame.
                    if (!m_boundsValid || m_bounds.size() != m_sceneObjects.size())
                        rebuild_scene_bounds();

                    const BoundsEntry be = (i < m_bounds.size()) ? m_bounds[i] : BoundsEntry{};
                    if (be.known) {
                        // Distance cull measured to the NEAREST point of the
                        // object's bounds, not to its origin — a 4000-unit
                        // terrain is "here" even when its origin is 4000 away.
                        const float distToBounds = (be.center - camPos).length() - be.radius;
                        if (distToBounds > 15000.0f) continue;   // max draw radius
                        if (!frustum.isSphereVisible(be.center.x(), be.center.y(), be.center.z(), be.radius))
                            continue;
                    }
                    // else: extent unknown → draw it. Never cull what we cannot measure.

                    bool drawn = false;

                    // 1. Embedded Ground Meshes (platforms / terrain)
                    if (!obj.ground_meshes.empty() && i < m_sceneGroundGpu.size()) {
                        const auto& ggpu = m_sceneGroundGpu[i];
                        float outMat[16];
                        calc_object_world_matrix(obj, outMat);
                        QMatrix4x4 worldMat;
                        std::memcpy(worldMat.data(), outMat, 16 * sizeof(float));

                        m_shader->setUniformValue("uProj", proj);
                        m_shader->setUniformValue("uModelView", view * worldMat);

                        for (size_t mi = 0; mi < ggpu.meshes.size(); ++mi) {
                            const auto& g = ggpu.meshes[mi];
                            if (!g.valid()) continue;

                            GLuint tex = (mi < ggpu.textures.size()) ? ggpu.textures[mi] : 0;
                            if (tex) {
                                glActiveTexture(GL_TEXTURE0);
                                glBindTexture(GL_TEXTURE_2D, tex);
                                m_shader->setUniformValue("uTex", 0);
                                m_shader->setUniformValue("uHasTex", 1);
                                m_shader->setUniformValue("uDiffuseColor", QVector4D(1.0f, 1.0f, 1.0f, 1.0f));
                            } else {
                                m_shader->setUniformValue("uHasTex", 0);
                                m_shader->setUniformValue("uDiffuseColor", QVector4D(0.38f, 0.46f, 0.42f, 1.0f));
                            }
                            draw_mesh_gpu(g);
                        }
                        drawn = true;
                    }

                    // 2. POD / 3D Model Instances
                    const std::string modelName = !obj.mesh_name.empty() ? obj.mesh_name : obj.template_name;
                    if (!modelName.empty()) {
                        const std::string sp = resolve_pod_cached(modelName);
                        if (!sp.empty()) {
                            if (m_scenePodCache.find(sp) == m_scenePodCache.end()) {
                                // Move, don't copy.  A POD is ten heap arrays of vertex
                                // data per mesh and the big levels ship 14-73 MB models;
                                // the old `m_scenePodCache[sp] = m;` (plus the copy out of
                                // m_preloadedPods) duplicated every one of them on the
                                // first frame.  The preload map is consumed here because
                                // m_scenePodCache is consulted first for every later object.
                                av::PODModel m;
                                auto itPre = m_preloadedPods.find(sp);
                                if (itPre != m_preloadedPods.end()) {
                                    m = std::move(itPre->second);
                                    m_preloadedPods.erase(itPre);
                                } else {
                                    m = av::pod_load(sp, obj.template_name);
                                }
                                if (!m.meshes.empty()) {
                                    m_podTexCache[sp] = load_pod_textures(sp, m, roots);
                                    m_podGpuCache[sp] = upload_pod_gpu(m);
                                    m_scenePodCache[sp] = std::move(m);
                                    // This instance's extent is only now knowable,
                                    // so its cull bounds get a second, exact pass.
                                    m_boundsValid = false;
                                }
                            }
                            auto itM = m_scenePodCache.find(sp);
                            if (itM != m_scenePodCache.end()) {
                                float renderMat[16];
                                calc_object_render_matrix(obj, renderMat);
                                QMatrix4x4 objWorld;
                                std::memcpy(objWorld.data(), renderMat, 16 * sizeof(float));

                                QVector4D tint(1.0f, 1.0f, 1.0f, 1.0f);
                                if (obj.has_model_diffuse_color) {
                                    tint = QVector4D(obj.model_diffuse_color[0],
                                                     obj.model_diffuse_color[1],
                                                     obj.model_diffuse_color[2], 1.0f);
                                }

                                draw_pod_instance(itM->second, m_podTexCache[sp], m_podGpuCache[sp], objWorld, view, proj, tint);
                                drawn = true;
                            }
                        }
                    }

                    // 3. Logic Markers / Non-visual objects (portal, spawn, camera, triggers)
                    if (!drawn) {
                        m_shader->release();
                        float outMat[16];
                        calc_object_world_matrix(obj, outMat);
                        QMatrix4x4 objWorld;
                        std::memcpy(objWorld.data(), outMat, 16 * sizeof(float));
                        renderObjectMarker(view * objWorld, proj, obj, int(i) == m_selectedObject);
                        m_shader->bind();
                    }
                }
                m_shader->release();
            }
        }

        // Draw Selection Box & Gizmo if object selected, or Mesh Edit overlay if editing
        if (m_hasScene) {
            if (m_meshEditActive) {
                renderMeshEditOverlay(view, proj);
            } else if (m_selectedObject >= 0 && m_selectedObject < int(m_sceneObjects.size())) {
                renderSelectionHighlight(view, proj, m_sceneObjects[m_selectedObject]);
                if (m_gizmoMode > 0) {
                    renderGizmo(view, proj, m_sceneObjects[m_selectedObject]);
                }
            }
        }
    }

private:
    void initShaders() {
        m_shader = new QOpenGLShaderProgram();
        m_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, GLESShaders::vertex_shader_source());
        m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, GLESShaders::fragment_shader_source());
        m_shader->link();

        m_unlitShader = new QOpenGLShaderProgram();
        m_unlitShader->addShaderFromSourceCode(QOpenGLShader::Vertex, GLESShaders::unlit_vertex_shader_source());
        m_unlitShader->addShaderFromSourceCode(QOpenGLShader::Fragment, GLESShaders::unlit_fragment_shader_source());
        m_unlitShader->link();

        m_outlineShader = new QOpenGLShaderProgram();
        m_outlineShader->addShaderFromSourceCode(QOpenGLShader::Vertex, GLESShaders::outline_vertex_shader_source());
        m_outlineShader->addShaderFromSourceCode(QOpenGLShader::Fragment, GLESShaders::outline_fragment_shader_source());
        m_outlineShader->link();
    }

    void initGrid() {
        std::vector<float> gridVerts;
        const int extent = 25;
        const float step = 1.0f;
        for (int i = -extent; i <= extent; ++i) {
            gridVerts.push_back(float(i) * step); gridVerts.push_back(0.0f); gridVerts.push_back(-float(extent) * step);
            gridVerts.push_back(float(i) * step); gridVerts.push_back(0.0f); gridVerts.push_back(float(extent) * step);

            gridVerts.push_back(-float(extent) * step); gridVerts.push_back(0.0f); gridVerts.push_back(float(i) * step);
            gridVerts.push_back(float(extent) * step);  gridVerts.push_back(0.0f); gridVerts.push_back(float(i) * step);
        }
        m_gridVertexCount = int(gridVerts.size() / 3);
        glGenBuffers(1, &m_gridVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
        glBufferData(GL_ARRAY_BUFFER, gridVerts.size() * sizeof(float), gridVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        float cubeVerts[] = {
            // Cube wire lines (12 lines, 24 vertices)
            -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,
             0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,
             0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f,
            -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f,

            -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,
             0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,
             0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
            -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f,

            -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f,
             0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f,
             0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,
            -0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f
        };
        glGenBuffers(1, &m_cubeVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    MeshGpu upload_mesh_gpu(const av::PODMesh& mesh) {
        MeshGpu g;
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

    std::vector<MeshGpu> upload_pod_gpu(const av::PODModel& model) {
        std::vector<MeshGpu> out;
        out.reserve(model.meshes.size());
        for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
            const auto& mesh = model.meshes[mi];
            if (mesh.bones_per_vertex != 0) {
                // Bake bind-pose skin for editor display
                int node_idx = -1;
                for (int ni = 0; ni < static_cast<int>(model.nodes.size()); ++ni) {
                    if (model.nodes[ni].object_index == static_cast<int>(mi)) {
                        node_idx = ni;
                        break;
                    }
                }
                std::vector<float> skin_pos, skin_norm;
                if (node_idx >= 0 && av::skin_mesh(model, node_idx, m_frame, skin_pos, skin_norm)) {
                    av::PODMesh baked = mesh;
                    baked.positions = std::move(skin_pos);
                    baked.normals = std::move(skin_norm);
                    out.push_back(upload_mesh_gpu(baked));
                } else {
                    out.push_back(upload_mesh_gpu(mesh));
                }
            } else {
                out.push_back(upload_mesh_gpu(mesh));
            }
        }
        return out;
    }

    GLuint load_texture_any(const std::string& path) {
        if (path.empty()) return 0;
        auto it = m_texCache.find(path);
        if (it != m_texCache.end()) return it->second;

        QImage formatted;
        auto itPre = m_predecodedImages.find(path);
        if (itPre != m_predecodedImages.end()) {
            formatted = itPre->second;
        } else {
            QImage img = decode_image_file(path);
            if (img.isNull()) return 0;
            formatted = img.convertToFormat(QImage::Format_RGBA8888);
        }
        if (formatted.isNull()) return 0;

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, formatted.width(), formatted.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, formatted.constBits());
        glBindTexture(GL_TEXTURE_2D, 0);

        m_texCache[path] = tex;
        return tex;
    }

    std::vector<GLuint> load_pod_textures(const std::string& pod_path, const av::PODModel& model, const std::vector<fs::path>& extra_roots = {}) {
        std::vector<GLuint> textures(model.texture_filenames.size(), 0);
        fs::path p(pod_path);
        std::vector<fs::path> roots = extra_roots;
        if (p.has_parent_path()) roots.push_back(p.parent_path());

        for (size_t i = 0; i < model.texture_filenames.size(); ++i) {
            const std::string& tex_name = model.texture_filenames[i];
            auto cands = av::assets::texture_candidates(p, tex_name, roots);
            for (const auto& cand : cands) {
                if (fs::exists(cand)) {
                    GLuint tex = load_texture_any(cand.string());
                    if (tex) { textures[i] = tex; break; }
                }
            }
        }
        if (textures.empty() || textures[0] == 0) {
            auto cands = av::assets::texture_candidates(p, p.stem().string(), roots);
            for (const auto& cand : cands) {
                if (fs::exists(cand)) {
                    GLuint tex = load_texture_any(cand.string());
                    if (tex) {
                        if (textures.empty()) textures.push_back(tex);
                        else textures[0] = tex;
                        break;
                    }
                }
            }
        }
        return textures;
    }

    // ── Per-object world-space bounds for culling ──────────────────────────
    //
    // The cull test used to use a fixed guess — 1500 * scale for ground
    // objects, 350 * scale for everything else — centred on the object's
    // ORIGIN.  That is wrong for an editor-generated ground mesh, which is
    // authored with Position = (0,0) and its polygon in absolute world
    // coordinates: the sphere sat at the world origin while the geometry
    // spanned thousands of units.  The terrain therefore disappeared once the
    // camera travelled far enough away and NEVER came back, because no camera
    // position brings the object's origin back in front of the near plane.
    // The same thing happens to any large hand-made mesh.
    //
    // Bounds now come from real geometry: the union of the parsed ground-mesh
    // AABBs (PODMesh carries min/max straight from the vertex stream), then the
    // model's own AABB once its POD is resident, then the object's authored
    // LocalAabb.  An object whose extent is genuinely unknown is NEVER culled —
    // dropping something we cannot measure is what makes meshes vanish for good.
    struct BoundsEntry {
        QVector3D center;
        float radius = 0.0f;
        bool known = false;
    };

    static bool decode_local_aabb(const av::SceneObject& obj, float out[4]) {
        if (obj.local_aabb.empty()) return false;
        float vals[4] = {0, 0, 0, 0};
        int found = 0;
        try {
            proto::Reader reader(obj.local_aabb);
            proto::Field f;
            while (reader.read_field(f)) {
                if (f.wire_type != proto::WIRE_I32 || f.field_number < 1 || f.field_number > 4) continue;
                vals[f.field_number - 1] = f.float_val;
                if (++found == 4) break;
            }
        } catch (...) { return false; }
        if (found < 4) return false;
        out[0] = vals[0]; out[1] = vals[1]; out[2] = vals[2]; out[3] = vals[3];
        return true;
    }

    void invalidate_scene_bounds() { m_boundsValid = false; }

    void rebuild_scene_bounds() {
        m_bounds.assign(m_sceneObjects.size(), BoundsEntry{});
        for (size_t i = 0; i < m_sceneObjects.size(); ++i) {
            const auto& obj = m_sceneObjects[i];
            float lmin[3] = { 1e30f,  1e30f,  1e30f };
            float lmax[3] = {-1e30f, -1e30f, -1e30f };
            bool have = false;
            auto include = [&](float x, float y, float z) {
                lmin[0] = std::min(lmin[0], x); lmax[0] = std::max(lmax[0], x);
                lmin[1] = std::min(lmin[1], y); lmax[1] = std::max(lmax[1], y);
                lmin[2] = std::min(lmin[2], z); lmax[2] = std::max(lmax[2], z);
            };
            auto include_mesh = [&](const av::PODMesh& m) {
                if (m.min_x > m.max_x || m.min_y > m.max_y || m.min_z > m.max_z) return;
                have = true;
                include(m.min_x, m.min_y, m.min_z);
                include(m.max_x, m.max_y, m.max_z);
            };

            for (const auto& gm : obj.ground_meshes) include_mesh(gm);

            // A POD instance's extent is only knowable once the model is
            // resident.  Until then fall through to the authored LocalAabb —
            // and if there is none, leave the object uncullable rather than
            // guessing it away.
            const std::string modelName = !obj.mesh_name.empty() ? obj.mesh_name : obj.template_name;
            if (!modelName.empty()) {
                const std::string sp = resolve_pod_cached(modelName);
                if (!sp.empty()) {
                    auto it = m_scenePodCache.find(sp);
                    if (it != m_scenePodCache.end())
                        for (const auto& m : it->second.meshes) include_mesh(m);
                }
            }

            if (!have) {
                float rect[4];
                if (decode_local_aabb(obj, rect) && rect[2] > 0.0f && rect[3] > 0.0f) {
                    have = true;
                    include(rect[0], rect[1], obj.pos_z);
                    include(rect[0] + rect[2], rect[1] + rect[3], obj.pos_z);
                }
            }
            if (!have) continue;   // unknown extent → never culled

            float mat[16];
            if (!obj.ground_meshes.empty()) calc_object_world_matrix(obj, mat);
            else                            calc_object_render_matrix(obj, mat);
            QMatrix4x4 wm;
            std::memcpy(wm.data(), mat, 16 * sizeof(float));

            QVector3D mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
            for (int c = 0; c < 8; ++c) {
                const QVector3D local((c & 1) ? lmax[0] : lmin[0],
                                      (c & 2) ? lmax[1] : lmin[1],
                                      (c & 4) ? lmax[2] : lmin[2]);
                const QVector3D p = wm.map(local);
                mn.setX(std::min(mn.x(), p.x())); mx.setX(std::max(mx.x(), p.x()));
                mn.setY(std::min(mn.y(), p.y())); mx.setY(std::max(mx.y(), p.y()));
                mn.setZ(std::min(mn.z(), p.z())); mx.setZ(std::max(mx.z(), p.z()));
            }

            auto& e = m_bounds[i];
            e.known  = true;
            e.center = (mn + mx) * 0.5f;
            e.radius = (mx - mn).length() * 0.5f + 1.0f;
        }
        m_boundsValid = true;
    }

    void upload_ground_meshes_gpu(const std::vector<fs::path>& roots) {
        const bool size_mismatch = (m_sceneGroundGpu.size() != m_sceneObjects.size());
        if (size_mismatch) {
            for (auto& ggpu : m_sceneGroundGpu) {
                for (auto& g : ggpu.meshes) g.destroy(this);
            }
            m_sceneGroundGpu.clear();
            m_sceneGroundGpu.resize(m_sceneObjects.size());
        }

        fs::path scPath(m_scenePath);
        for (size_t oi = 0; oi < m_sceneObjects.size(); ++oi) {
            const auto& obj = m_sceneObjects[oi];
            if (obj.ground_meshes.empty()) continue;
            auto& ggpu = m_sceneGroundGpu[oi];

            // When actively editing a mesh and this is NOT the edited object, skip rebuilding!
            if (!size_mismatch && m_meshEditActive && m_meshEditObject >= 0 &&
                int(oi) != m_meshEditObject && !ggpu.meshes.empty()) {
                continue;
            }

            for (auto& g : ggpu.meshes) g.destroy(this);
            ggpu.meshes.clear();
            ggpu.meshes.reserve(obj.ground_meshes.size());
            for (const auto& gm : obj.ground_meshes) {
                ggpu.meshes.push_back(upload_mesh_gpu(gm));
            }

            // The texture slots are POSITIONAL: slot i belongs to
            // ground_meshes[i]. They therefore have to be cleared whenever the
            // mesh list is rebuilt, not merely resized to match.
            //
            // Reconciling the size only (resize) left a stale GLuint at every
            // index, and the `!= 0` guard below then skipped re-resolution for
            // it. So once a regeneration changed the mesh count or their order —
            // which every mesh edit does — each mesh inherited whatever texture
            // previously happened to occupy its index. The ground got
            // progressively more wrong with each edit while the *saved* scene
            // stayed correct, which is exactly the "textures go weird every time
            // I edit, but normal again after save + reload" symptom.
            ggpu.textures.assign(obj.ground_meshes.size(), 0);

            for (size_t ti = 0; ti < obj.ground_mesh_textures.size() && ti < ggpu.textures.size(); ++ti) {
                if (ggpu.textures[ti] != 0) continue; // Skip if already loaded
                const std::string& tex_name = obj.ground_mesh_textures[ti];
                if (tex_name.empty()) continue;

                auto it = m_texCache.find(tex_name);
                if (it != m_texCache.end()) {
                    ggpu.textures[ti] = it->second;
                    continue;
                }

                auto cands = av::assets::texture_candidates(scPath, tex_name, roots);
                for (const auto& cand : cands) {
                    if (fs::exists(cand)) {
                        GLuint t = load_texture_any(cand.string());
                        if (t) {
                            ggpu.textures[ti] = t;
                            m_texCache[tex_name] = t;
                            break;
                        }
                    }
                }
            }
        }
        m_sceneGroundGpuValid = true;
    }

    void setup_background(const std::vector<fs::path>& roots) {
        m_bgTex = 0;
        std::string bgName;
        for (const auto& obj : m_sceneObjects) {
            if (!obj.background_name.empty()) {
                bgName = obj.background_name;
                break;
            }
        }
        if (bgName.empty()) return;

        static const char* suffixes[] = {
            "_2x.tex.png", ".tex.png", "_2x.pvr", ".pvr", "_2x.tex", ".tex", "_2x.png", ".png", ""
        };
        for (const auto& root : roots) {
            for (const char* suf : suffixes) {
                fs::path candidate = root / (bgName + suf);
                if (fs::exists(candidate)) {
                    m_bgTex = load_texture_any(candidate.string());
                    if (m_bgTex) return;
                }
            }
        }
    }

    void renderBackground(const QMatrix4x4& /*view*/, const QMatrix4x4& /*proj*/) {
        if (!m_bgTex || !m_shader || !m_shader->isLinked()) return;

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        m_shader->bind();
        m_shader->setUniformValue("uProj", QMatrix4x4());
        m_shader->setUniformValue("uModelView", QMatrix4x4());
        m_shader->setUniformValue("uDiffuseColor", QVector4D(1.0f, 1.0f, 1.0f, 1.0f));
        m_shader->setUniformValue("uSunColor", QVector3D(0.0f, 0.0f, 0.0f));
        m_shader->setUniformValue("uSkyColor", QVector3D(1.0f, 1.0f, 1.0f));
        m_shader->setUniformValue("uGroundColor", QVector3D(1.0f, 1.0f, 1.0f));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bgTex);
        m_shader->setUniformValue("uTex", 0);
        m_shader->setUniformValue("uHasTex", 1);

        float quadVerts[] = {
            // Pos (xyz), Norm (xyz), UV (uv)
            -1.0f, -1.0f, 0.999f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             1.0f, -1.0f, 0.999f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
             1.0f,  1.0f, 0.999f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,

            -1.0f, -1.0f, 0.999f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
             1.0f,  1.0f, 0.999f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
            -1.0f,  1.0f, 0.999f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f,
        };

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), quadVerts);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), quadVerts + 3);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), quadVerts + 6);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);

        m_shader->release();
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    void draw_mesh_gpu(const MeshGpu& g) {
        if (!g.valid()) return;

        glBindBuffer(GL_ARRAY_BUFFER, g.pos_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

        if (g.nrm_vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, g.nrm_vbo);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        } else {
            glDisableVertexAttribArray(1);
            glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
        }

        if (g.uv_vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, g.uv_vbo);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        } else {
            glDisableVertexAttribArray(2);
            glVertexAttrib2f(2, 0.0f, 0.0f);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        if (g.ebo && g.index_count > 0) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
            glDrawElements(GL_TRIANGLES, g.index_count, GL_UNSIGNED_INT, nullptr);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, g.vertex_count);
        }

        glDisableVertexAttribArray(0);
        if (g.nrm_vbo) glDisableVertexAttribArray(1);
        if (g.uv_vbo) glDisableVertexAttribArray(2);
    }

    void draw_pod_instance(const av::PODModel& model,
                           const std::vector<GLuint>& textures,
                           const std::vector<MeshGpu>& gpu_meshes,
                           const QMatrix4x4& worldMatrix,
                           const QMatrix4x4& view,
                           const QMatrix4x4& proj,
                           const QVector4D& tintColor = QVector4D(1.0f, 1.0f, 1.0f, 1.0f)) {
        if (gpu_meshes.empty()) return;

        for (size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
            const auto& node = model.nodes[node_index];
            if (node.object_index < 0 || node.object_index >= static_cast<int>(model.meshes.size())) continue;
            const auto& mesh = model.meshes[node.object_index];
            if (mesh.positions.empty()) continue;
            if (node.object_index >= static_cast<int>(gpu_meshes.size()) || !gpu_meshes[node.object_index].valid()) continue;

            float node_mat[16];
            av::get_node_matrix(model, static_cast<int>(node_index), m_frame, node_mat);

            QMatrix4x4 nodeMat;
            std::memcpy(nodeMat.data(), node_mat, 16 * sizeof(float));

            QMatrix4x4 modelMat = worldMatrix;
            if (model.has_center_point) {
                modelMat.translate(-model.center_point[0], -model.center_point[1], -model.center_point[2]);
            }
            modelMat = modelMat * nodeMat;

            QVector4D diffColor = tintColor;
            GLuint tex = 0;
            if (node.material_index >= 0 && node.material_index < static_cast<int>(model.materials.size())) {
                const auto& mat = model.materials[node.material_index];
                diffColor = QVector4D(mat.diffuse[0] * tintColor.x(),
                                      mat.diffuse[1] * tintColor.y(),
                                      mat.diffuse[2] * tintColor.z(),
                                      mat.opacity * tintColor.w());
                if (mat.diffuse_texture_index >= 0 && mat.diffuse_texture_index < static_cast<int>(textures.size())) {
                    tex = textures[mat.diffuse_texture_index];
                }
            }
            if (!tex && !textures.empty()) {
                tex = textures.front();
            }

            m_shader->setUniformValue("uProj", proj);
            m_shader->setUniformValue("uModelView", view * modelMat);
            m_shader->setUniformValue("uDiffuseColor", diffColor);

            if (tex) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, tex);
                m_shader->setUniformValue("uTex", 0);
                m_shader->setUniformValue("uHasTex", 1);
            } else {
                m_shader->setUniformValue("uHasTex", 0);
            }

            draw_mesh_gpu(gpu_meshes[node.object_index]);
        }
    }

    void renderGrid(const QMatrix4x4& view, const QMatrix4x4& proj) {
        if (!m_unlitShader || !m_unlitShader->isLinked()) return;

        // In 2.5D game view (pitch near 0), ground grid viewed edge-on collapses into a single flickering line.
        // Fade grid smoothly when camera pitch is close to 0.
        float pitchFade = std::clamp(std::abs(m_pitch) / 12.0f, 0.0f, 1.0f);
        if (pitchFade < 0.02f) return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        m_unlitShader->bind();
        m_unlitShader->setUniformValue("uProj", proj);

        float gridScale = std::max(25.0f, std::pow(10.0f, std::floor(std::log10(std::max(10.0f, m_distance * 0.2f)))));
        float snapX = std::floor(m_target.x() / gridScale) * gridScale;
        float snapZ = std::floor(m_target.z() / gridScale) * gridScale;

        QMatrix4x4 gridModel;
        gridModel.translate(snapX, 0.0f, snapZ);
        gridModel.scale(gridScale, 1.0f, gridScale);

        m_unlitShader->setUniformValue("uModelView", view * gridModel);
        m_unlitShader->setUniformValue("uColor", QVector4D(0.35f, 0.40f, 0.52f, 0.45f * pitchFade));
        glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_unlitShader->release();

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void renderObjectMarker(const QMatrix4x4& mv, const QMatrix4x4& proj, const av::SceneObject& obj, bool selected) {
        if (!m_unlitShader || !m_unlitShader->isLinked()) return;
        m_unlitShader->bind();
        m_unlitShader->setUniformValue("uProj", proj);
        m_unlitShader->setUniformValue("uModelView", mv);

        if (selected) {
            // Ruby Crimson active selection outline
            m_unlitShader->setUniformValue("uColor", QVector4D(0.88f, 0.42f, 0.46f, 1.0f));
        } else if (obj.is_portal) {
            // Portal: deep luminous blue
            m_unlitShader->setUniformValue("uColor", QVector4D(0.24f, 0.60f, 0.96f, 0.85f));
        } else if (obj.is_spawn_point) {
            // Spawn point: gold
            m_unlitShader->setUniformValue("uColor", QVector4D(0.96f, 0.76f, 0.20f, 0.85f));
        } else if (obj.is_camera) {
            // Camera: cyan
            m_unlitShader->setUniformValue("uColor", QVector4D(0.20f, 0.85f, 0.80f, 0.85f));
        } else {
            // Default logic marker: subtle slate
            m_unlitShader->setUniformValue("uColor", QVector4D(0.40f, 0.44f, 0.52f, 0.5f));
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glDrawArrays(GL_LINES, 0, 24);
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_unlitShader->release();
    }

    void renderSelectionHighlight(const QMatrix4x4& view, const QMatrix4x4& proj, const av::SceneObject& obj) {
        if (!m_unlitShader || !m_unlitShader->isLinked() || !m_cubeVbo) return;

        float outMat[16];
        calc_object_world_matrix(obj, outMat);
        QMatrix4x4 baseWorld;
        std::memcpy(baseWorld.data(), outMat, 16 * sizeof(float));

        bool renderedSilhouette = false;

        // 1. Inverted-hull silhouette outline for 3D Models
        const std::string modelName = !obj.mesh_name.empty() ? obj.mesh_name : obj.template_name;
        if (!modelName.empty() && m_outlineShader && m_outlineShader->isLinked()) {
            fs::path scPath(m_scenePath);
            std::vector<fs::path> roots = build_asset_roots(scPath);
            fs::path podPath = av::assets::resolve_pod(scPath, modelName, roots);
            if (!podPath.empty()) {
                auto itM = m_scenePodCache.find(podPath.string());
                auto itG = m_podGpuCache.find(podPath.string());
                if (itM != m_scenePodCache.end() && itG != m_podGpuCache.end()) {
                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glEnable(GL_CULL_FACE);
                    glCullFace(GL_FRONT);

                    m_outlineShader->bind();
                    m_outlineShader->setUniformValue("uProj", proj);
                    m_outlineShader->setUniformValue("uOutlineWidth", 0.035f);
                    m_outlineShader->setUniformValue("uOutlineColor", QVector4D(1.0f, 0.16f, 0.38f, 0.95f));

                    float renderMat[16];
                    calc_object_render_matrix(obj, renderMat);
                    QMatrix4x4 objWorld;
                    std::memcpy(objWorld.data(), renderMat, 16 * sizeof(float));

                    for (size_t ni = 0; ni < itM->second.nodes.size(); ++ni) {
                        const auto& node = itM->second.nodes[ni];
                        if (node.object_index < 0 || node.object_index >= static_cast<int>(itM->second.meshes.size())) continue;
                        if (node.object_index >= static_cast<int>(itG->second.size()) || !itG->second[node.object_index].valid()) continue;

                        float node_mat[16];
                        av::get_node_matrix(itM->second, static_cast<int>(ni), m_frame, node_mat);
                        QMatrix4x4 nodeMat;
                        std::memcpy(nodeMat.data(), node_mat, 16 * sizeof(float));

                        QMatrix4x4 modelMat = objWorld;
                        if (itM->second.has_center_point) {
                            modelMat.translate(-itM->second.center_point[0], -itM->second.center_point[1], -itM->second.center_point[2]);
                        }
                        modelMat = modelMat * nodeMat;
                        m_outlineShader->setUniformValue("uModelView", view * modelMat);
                        draw_mesh_gpu(itG->second[node.object_index]);
                    }
                    m_outlineShader->release();

                    glCullFace(GL_BACK);
                    glDisable(GL_CULL_FACE);
                    glDepthMask(GL_TRUE);
                    glDisable(GL_BLEND);
                    renderedSilhouette = true;
                }
            }
        }

        // 2. Contour loop outline for Ground Meshes
        if (!obj.ground_polygon_points.empty() && obj.ground_polygon_points.size() >= 6) {
            std::vector<float> contourVerts;
            contourVerts.reserve(obj.ground_polygon_points.size() / 2 * 3);
            for (size_t pi = 0; pi < obj.ground_polygon_points.size(); pi += 2) {
                contourVerts.push_back(obj.ground_polygon_points[pi]);
                contourVerts.push_back(obj.ground_polygon_points[pi + 1]);
                contourVerts.push_back(0.0f);
            }

            glDisable(GL_DEPTH_TEST);
            glLineWidth(3.5f);
            m_unlitShader->bind();
            m_unlitShader->setUniformValue("uProj", proj);
            m_unlitShader->setUniformValue("uModelView", view * baseWorld);
            m_unlitShader->setUniformValue("uColor", QVector4D(1.0f, 0.16f, 0.38f, 1.0f));

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, contourVerts.data());
            glDrawArrays(GL_LINE_LOOP, 0, int(contourVerts.size() / 3));
            glDisableVertexAttribArray(0);

            m_unlitShader->release();
            glEnable(GL_DEPTH_TEST);
            renderedSilhouette = true;
        }

        // 3. Fallback wireframe bounding box if not a mesh or model
        if (!renderedSilhouette) {
            float hw = std::max(40.0f, std::abs(obj.scale_x * obj.template_scaling) * 1.55f);
            float hh = std::max(40.0f, std::abs(obj.scale_y * obj.template_scaling) * 1.55f);
            float hd = std::max(40.0f, std::abs(obj.scale_z * obj.template_scaling) * 1.55f);

            QMatrix4x4 boxWorld = baseWorld;
            boxWorld.scale(hw * 2.0f, hh * 2.0f, hd * 2.0f);

            m_unlitShader->bind();
            m_unlitShader->setUniformValue("uProj", proj);

            glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glLineWidth(2.0f);
            m_unlitShader->setUniformValue("uModelView", view * boxWorld);
            m_unlitShader->setUniformValue("uColor", QVector4D(0.20f, 0.75f, 1.0f, 0.40f));
            glDrawArrays(GL_LINES, 0, 24);

            glEnable(GL_DEPTH_TEST);
            glLineWidth(3.0f);
            m_unlitShader->setUniformValue("uColor", QVector4D(0.96f, 0.22f, 0.38f, 1.0f));
            glDrawArrays(GL_LINES, 0, 24);

            glDisableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            m_unlitShader->release();
        }

        // Center origin indicator (amber diamond / pivot)
        glDisable(GL_DEPTH_TEST);
        m_unlitShader->bind();
        m_unlitShader->setUniformValue("uProj", proj);
        QMatrix4x4 centerMat = baseWorld;
        centerMat.scale(16.0f, 16.0f, 16.0f);
        m_unlitShader->setUniformValue("uModelView", view * centerMat);
        m_unlitShader->setUniformValue("uColor", QVector4D(1.0f, 0.85f, 0.20f, 0.95f));
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glDrawArrays(GL_LINES, 0, 24);
        glDisableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        m_unlitShader->release();
        glEnable(GL_DEPTH_TEST);
    }

    void renderGizmo(const QMatrix4x4& view, const QMatrix4x4& proj, const av::SceneObject& obj) {
        if (!m_unlitShader || !m_unlitShader->isLinked()) return;
        glDisable(GL_DEPTH_TEST);
        m_unlitShader->bind();
        QMatrix4x4 model;
        model.translate(obj.pos_x, obj.pos_y, obj.pos_z);
        QMatrix4x4 mv = view * model;

        m_unlitShader->setUniformValue("uProj", proj);
        m_unlitShader->setUniformValue("uModelView", mv);

        // X Axis (Red)
        m_unlitShader->setUniformValue("uColor", QVector4D(0.95f, 0.26f, 0.21f, 1.0f));
        float xLine[] = { 0,0,0, 2.0f,0,0 };
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, xLine);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_LINES, 0, 2);

        // Y Axis (Green)
        m_unlitShader->setUniformValue("uColor", QVector4D(0.3f, 0.69f, 0.31f, 1.0f));
        float yLine[] = { 0,0,0, 0,2.0f,0 };
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, yLine);
        glDrawArrays(GL_LINES, 0, 2);

        // Z Axis (Blue)
        m_unlitShader->setUniformValue("uColor", QVector4D(0.13f, 0.59f, 0.95f, 1.0f));
        float zLine[] = { 0,0,0, 0,0,2.0f };
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, zLine);
        glDrawArrays(GL_LINES, 0, 2);

        glDisableVertexAttribArray(0);
        m_unlitShader->release();
        glEnable(GL_DEPTH_TEST);
    }

    void renderMeshEditOverlay(const QMatrix4x4& view, const QMatrix4x4& proj) {
        if (m_meshEditObject < 0 || m_meshEditObject >= int(m_sceneObjects.size())) return;
        if (m_meshEditPoints.size() < 3) return;
        if (!m_unlitShader || !m_unlitShader->isLinked()) return;

        const auto& obj = m_sceneObjects[m_meshEditObject];
        float outMat[16];
        calc_object_world_matrix(obj, outMat);
        QMatrix4x4 worldMat;
        std::memcpy(worldMat.data(), outMat, 16 * sizeof(float));

        glDisable(GL_DEPTH_TEST);
        m_unlitShader->bind();
        m_unlitShader->setUniformValue("uProj", proj);
        m_unlitShader->setUniformValue("uModelView", view * worldMat);

        // 1. Polygon Outline
        std::vector<float> outlineVerts;
        outlineVerts.reserve(m_meshEditPoints.size() * 3);
        for (const auto& pt : m_meshEditPoints) {
            outlineVerts.push_back(float(pt.x));
            outlineVerts.push_back(float(pt.y));
            outlineVerts.push_back(0.0f);
        }

        // Outer glow
        glLineWidth(4.5f);
        m_unlitShader->setUniformValue("uColor", QVector4D(0.0f, 0.90f, 1.0f, 0.35f));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, outlineVerts.data());
        glDrawArrays(GL_LINE_LOOP, 0, int(outlineVerts.size() / 3));

        // Core sharp line
        glLineWidth(2.5f);
        m_unlitShader->setUniformValue("uColor", QVector4D(0.0f, 1.0f, 0.70f, 0.95f));
        glDrawArrays(GL_LINE_LOOP, 0, int(outlineVerts.size() / 3));

        // 2. Vertex Handles (3D diamonds)
        for (size_t vi = 0; vi < m_meshEditPoints.size(); ++vi) {
            const auto& pt = m_meshEditPoints[vi];
            bool isSel = (int(vi) == m_selectedMeshVertex);

            float hSize = isSel ? 22.0f : 14.0f;
            float hx = float(pt.x), hy = float(pt.y);

            float diamond[18] = {
                hx, hy + hSize, 0.0f,   hx - hSize, hy, 0.0f,   hx, hy - hSize, 0.0f,
                hx, hy + hSize, 0.0f,   hx, hy - hSize, 0.0f,   hx + hSize, hy, 0.0f
            };

            if (isSel) {
                m_unlitShader->setUniformValue("uColor", QVector4D(1.0f, 0.85f, 0.15f, 1.0f)); // Glowing gold
            } else {
                m_unlitShader->setUniformValue("uColor", QVector4D(0.0f, 0.80f, 1.0f, 0.95f)); // Vibrant cyan
            }
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, diamond);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            float diamondBorder[24] = {
                hx, hy + hSize, 0.0f,   hx - hSize, hy, 0.0f,
                hx - hSize, hy, 0.0f,   hx, hy - hSize, 0.0f,
                hx, hy - hSize, 0.0f,   hx + hSize, hy, 0.0f,
                hx + hSize, hy, 0.0f,   hx, hy + hSize, 0.0f
            };
            glLineWidth(2.0f);
            m_unlitShader->setUniformValue("uColor", isSel ? QVector4D(1.0f, 1.0f, 1.0f, 1.0f) : QVector4D(0.0f, 0.2f, 0.4f, 1.0f));
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, diamondBorder);
            glDrawArrays(GL_LINES, 0, 8);
        }

        // 3. Edge Midpoint Indicators
        for (size_t ei = 0; ei < m_meshEditPoints.size(); ++ei) {
            size_t nextEi = (ei + 1) % m_meshEditPoints.size();
            float mx = float(m_meshEditPoints[ei].x + m_meshEditPoints[nextEi].x) * 0.5f;
            float my = float(m_meshEditPoints[ei].y + m_meshEditPoints[nextEi].y) * 0.5f;
            float mSize = 7.0f;
            float midBox[18] = {
                mx - mSize, my - mSize, 0.0f,  mx + mSize, my - mSize, 0.0f,  mx + mSize, my + mSize, 0.0f,
                mx - mSize, my - mSize, 0.0f,  mx + mSize, my + mSize, 0.0f,  mx - mSize, my + mSize, 0.0f
            };
            m_unlitShader->setUniformValue("uColor", QVector4D(1.0f, 1.0f, 1.0f, 0.65f));
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, midBox);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glDisableVertexAttribArray(0);
        m_unlitShader->release();
        glEnable(GL_DEPTH_TEST);
    }

    float m_pitch = 20.0f;
    float m_yaw = -45.0f;
    float m_distance = 15.0f;
    QVector3D m_target{0, 0, 0};
    int m_gizmoMode = 0;
    int m_selectedObject = -1;
    float m_frame = 0.0f;
    bool m_meshEditActive = false;
    int m_meshEditObject = -1;
    int m_selectedMeshVertex = -1;
    std::vector<boulder::PolygonPoint> m_meshEditPoints;

    bool m_hasScene = false;
    std::string m_scenePath;
    std::vector<av::SceneObject> m_sceneObjects;
    std::vector<GroundMeshGpu> m_sceneGroundGpu;
    bool m_sceneGroundGpuValid = false;
    // World-space cull bounds per object; rebuilt only on a scene or geometry
    // revision change (see rebuild_scene_bounds).
    std::vector<BoundsEntry> m_bounds;
    bool m_boundsValid = false;
    GLuint m_bgTex = 0;

    bool m_hasModel = false;
    std::string m_modelPath;
    av::PODModel m_model;
    bool m_modelGpuValid = false;
    std::vector<MeshGpu> m_modelGpuMeshes;
    std::vector<GLuint> m_modelTextures;

    // Caches for scenes and models
    // ── Per-frame scratch, formerly rebuilt every frame ─────────────────────
    //
    // build_asset_roots() allocates ~17 fs::path objects with string joins, and
    // av::assets::resolve_pod() takes a mutex and does two map lookups — both of
    // which ran once PER OBJECT PER FRAME (and resolve_pod twice: once for the
    // probe, once inside the render loop).  On a level with hundreds of objects
    // that is thousands of lock/unlock pairs and allocations every second, for
    // an answer that cannot change until the scene does.
    std::string m_assetRootsKey;
    std::vector<fs::path> m_assetRoots;
    std::unordered_map<std::string, std::string> m_podPathCache;   // model name -> pod path, "" = absent

    const std::vector<fs::path>& asset_roots_cached(const std::string& scene_path) {
        if (m_assetRootsKey != scene_path) {
            m_assetRootsKey  = scene_path;
            m_assetRoots     = build_asset_roots(fs::path(scene_path));
            m_podPathCache.clear();
        }
        return m_assetRoots;
    }

    // Returns a reference that stays valid until the next scene change; a new
    // insert does not invalidate references into an unordered_map.
    const std::string& resolve_pod_cached(const std::string& model_name) {
        auto it = m_podPathCache.find(model_name);
        if (it != m_podPathCache.end()) return it->second;
        const fs::path p = av::assets::resolve_pod(fs::path(m_assetRootsKey), model_name, m_assetRoots);
        std::error_code ec;
        std::string sp = (!p.empty() && fs::exists(p, ec)) ? p.string() : std::string();
        return m_podPathCache.emplace(model_name, std::move(sp)).first->second;
    }

    std::unordered_map<std::string, av::PODModel> m_scenePodCache;
    std::unordered_map<std::string, std::vector<MeshGpu>> m_podGpuCache;
    std::unordered_map<std::string, std::vector<GLuint>> m_podTexCache;
    std::unordered_map<std::string, GLuint> m_texCache;
    std::unordered_map<std::string, av::PODModel> m_preloadedPods;
    std::unordered_map<std::string, QImage> m_predecodedImages;
    // Last scene revision this renderer copied.  When it matches the item's, the
    // scene is untouched and no copy happens at all.
    uint64_t m_sceneRevision = 0;
    uint64_t m_geomRevision  = 0;

    QOpenGLShaderProgram* m_shader = nullptr;
    QOpenGLShaderProgram* m_unlitShader = nullptr;
    QOpenGLShaderProgram* m_outlineShader = nullptr;
    GLuint m_gridVbo = 0;
    int m_gridVertexCount = 0;
    GLuint m_cubeVbo = 0;
};

// ─── RubyQuickViewport Implementation ──────────────────────────────────────

RubyQuickViewport::RubyQuickViewport(QQuickItem* parent)
    : QQuickFramebufferObject(parent), m_objectModel(this)
{
    setMirrorVertically(true); // Right side up OpenGL FBO in Qt Quick!
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptTouchEvents(true);
}

RubyQuickViewport::~RubyQuickViewport() = default;

QQuickFramebufferObject::Renderer* RubyQuickViewport::createRenderer() const {
    return new RubyViewportRenderer();
}

void RubyQuickViewport::setGizmoMode(int mode) {
    if (m_meshEditActive) mode = 0;
    if (m_gizmoMode != mode) {
        m_gizmoMode = mode;
        emit gizmoModeChanged();
        update();
    }
}

int RubyQuickViewport::objectCount() const {
    if (m_hasModel) return int(m_model.meshes.size());
    return int(m_scene.objects.size());
}

float RubyQuickViewport::selectedX() const {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        return m_scene.objects[m_selectedObject].pos_x;
    }
    return m_cameraTarget.x();
}

float RubyQuickViewport::selectedY() const {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        return m_scene.objects[m_selectedObject].pos_y;
    }
    return m_cameraTarget.y();
}

float RubyQuickViewport::selectedZ() const {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        return m_scene.objects[m_selectedObject].pos_z;
    }
    return m_cameraTarget.z();
}

float RubyQuickViewport::selectedRot() const {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        return std::round(m_scene.objects[m_selectedObject].rot_y * 180.0f / M_PI);
    }
    return std::round(m_cameraYaw);
}

float RubyQuickViewport::selectedScale() const {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        return m_scene.objects[m_selectedObject].scale_x * m_scene.objects[m_selectedObject].template_scaling;
    }
    return 1.0f;
}

void RubyQuickViewport::setCameraMode(int mode) {
    if (m_cameraMode != mode) {
        m_cameraMode = mode;
        if (m_cameraMode == CameraModeGameView) {
            m_cameraPitch = 0.0f;
            m_cameraYaw = 0.0f;
        }
        emit cameraModeChanged();
        emit cameraChanged();
        update();
    }
}

void RubyQuickViewport::toggleCameraMode() {
    setCameraMode(m_cameraMode == CameraModeGameView ? CameraMode3DEdit : CameraModeGameView);
}

void RubyQuickViewport::setGameView() {
    setCameraMode(CameraModeGameView);
}

void RubyQuickViewport::set3DEditMode() {
    setCameraMode(CameraMode3DEdit);
}

void RubyQuickViewport::setNavStep(float step) {
    if (m_navStep != step && step > 0.0f) {
        m_navStep = step;
        emit navStepChanged();
    }
}

void RubyQuickViewport::cycleNavStep(int dir) {
    static const float kSteps[] = { 1.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f };
    constexpr int kCount = sizeof(kSteps) / sizeof(kSteps[0]);
    int idx = 3;
    for (int i = 0; i < kCount; ++i) {
        if (std::abs(m_navStep - kSteps[i]) < 0.1f) {
            idx = i;
            break;
        }
    }
    idx = (idx + dir + kCount) % kCount;
    m_navStep = kSteps[idx];
    emit navStepChanged();
}

void RubyQuickViewport::nudgeSelectedCoord(int axis, float delta) {
    if (m_meshEditActive) {
        if (m_selectedMeshVertex >= 0 && m_selectedMeshVertex < int(m_meshEditPoints.size())) {
            if (axis == 0) m_meshEditPoints[m_selectedMeshVertex].x += delta;
            else if (axis == 1) m_meshEditPoints[m_selectedMeshVertex].y += delta;
            m_meshDirty = true;
            liveMeshPreview();
            emit meshEditVerticesChanged();
            update();
        }
        return;
    }
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        // Coalesce, exactly as nudgeSelected() does for the gizmo pad.  Without
        // this, every tap on a coordinate button serialized the entire scene on
        // the GUI thread — on a 25 MB level that is a visible hitch per tap.
        constexpr qint64 kNudgeUndoCoalesceMs = 900;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastNudgeMs == 0 || now - m_lastNudgeMs > kNudgeUndoCoalesceMs) {
            pushUndoSnapshot();
        }
        m_lastNudgeMs = now;

        auto& obj = m_scene.objects[m_selectedObject];
        if (axis == 0) obj.pos_x += delta;
        else if (axis == 1) obj.pos_y += delta;
        else if (axis == 2) obj.pos_z += delta;
        touch_scene(/*geometry_changed=*/false);
        markDirty(true);
        emit objectTransformChanged();
        emit selectedObjectChanged();
        update();
    } else {
        if (axis == 0) m_cameraTarget.setX(m_cameraTarget.x() + delta);
        else if (axis == 1) m_cameraTarget.setY(m_cameraTarget.y() + delta);
        else if (axis == 2) m_cameraTarget.setZ(m_cameraTarget.z() + delta);
        emit cameraChanged();
        update();
    }
}

void RubyQuickViewport::rotateSelectedZ(float angleDeg) {
    if (m_meshEditActive) return; // Disallow object rotation during mesh edit mode
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        // Same coalescing as the nudge path: rotating by repeated taps is one
        // intent, not thirty undo steps.
        constexpr qint64 kRotateUndoCoalesceMs = 900;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_lastNudgeMs == 0 || now - m_lastNudgeMs > kRotateUndoCoalesceMs) {
            pushUndoSnapshot();
        }
        m_lastNudgeMs = now;

        auto& obj = m_scene.objects[m_selectedObject];
        // Tag 6 in Swordigo .scene format is rot_y (in-plane 2.5D rotation)
        obj.rot_y += angleDeg * M_PI / 180.0f;
        touch_scene(/*geometry_changed=*/false);
        markDirty(true);
        emit objectTransformChanged();
        emit selectedObjectChanged();
        update();
    } else {
        m_cameraYaw += angleDeg;
        emit cameraChanged();
        update();
    }
}

QVariantMap RubyQuickViewport::getCameraState() const {
    QVariantMap m;
    m["targetX"] = m_cameraTarget.x();
    m["targetY"] = m_cameraTarget.y();
    m["targetZ"] = m_cameraTarget.z();
    m["pitch"] = m_cameraPitch;
    m["yaw"] = m_cameraYaw;
    m["distance"] = m_cameraDistance;
    m["cameraMode"] = m_cameraMode;
    return m;
}

void RubyQuickViewport::setCameraState(const QVariantMap& state) {
    if (state.contains("targetX") && state.contains("targetY") && state.contains("targetZ")) {
        m_cameraTarget = QVector3D(state["targetX"].toFloat(), state["targetY"].toFloat(), state["targetZ"].toFloat());
    }
    if (state.contains("pitch")) m_cameraPitch = state["pitch"].toFloat();
    if (state.contains("yaw")) m_cameraYaw = state["yaw"].toFloat();
    if (state.contains("distance")) m_cameraDistance = state["distance"].toFloat();
    if (state.contains("cameraMode")) m_cameraMode = state["cameraMode"].toInt();
    emit cameraChanged();
    emit cameraModeChanged();
    update();
}

void RubyQuickViewport::placeCurrentObject() {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        duplicateObject(m_selectedObject);
        nudgeSelectedCoord(0, m_navStep);
    } else {
        addObject();
    }
}

void RubyQuickViewport::deselectObject() {
    selectObject(-1);
}

void RubyQuickViewport::deleteCurrentObject() {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        deleteObject(m_selectedObject);
    }
}

void RubyQuickViewport::frameScene() {
    if (m_hasModel) {
        m_cameraTarget = QVector3D(m_model.center_x, m_model.center_y, m_model.center_z);
        m_cameraDistance = std::max(2.0f, m_model.radius * 2.5f);
        m_cameraPitch = 15.0f;
        m_cameraYaw = -35.0f;
        m_cameraMode = CameraMode3DEdit;
    } else if (m_hasScene && !m_scene.objects.empty()) {
        const av::SceneObject* targetObj = &m_scene.objects[0];
        for (const auto& obj : m_scene.objects) {
            if (obj.is_spawn_point || obj.template_name == "player" || obj.name == "player") {
                targetObj = &obj;
                break;
            }
        }
        m_cameraTarget = QVector3D(targetObj->pos_x, targetObj->pos_y, targetObj->pos_z);
        m_cameraDistance = 650.0f;
        m_cameraPitch = 0.0f;
        m_cameraYaw = 0.0f;
        m_cameraMode = CameraModeGameView;
    } else {
        m_cameraTarget = QVector3D(0.0f, 0.0f, 0.0f);
        m_cameraDistance = 650.0f;
        m_cameraPitch = 0.0f;
        m_cameraYaw = 0.0f;
        m_cameraMode = CameraModeGameView;
    }
    emit cameraModeChanged();
    emit cameraChanged();
    update();
}

// ---------------------------------------------------------------------------
// touch_scene — the single announcement that the scene changed
//
// WHY THIS EXISTS: synchronize() runs on the render thread and the renderer
// keeps its own copy of av::SceneData.  That copy used to be unconditional, once
// per frame, in synchronize().  A SceneObject contains
// `std::vector<PODMesh> ground_meshes`, and a PODMesh is about ten separate heap
// arrays of vertex data (positions, normals, uvs, tangents, bone indices and
// weights, indices, plus a BoneBatch of three more) — so the assignment
// deep-copied the level's entire terrain geometry, sixty times a second, on the
// render thread.  That single line is why a scene world took so long to become
// usable and why the whole viewport felt heavy afterwards.
//
// It cannot simply be deleted, because it was also the ONLY thing carrying a
// transform edit to the screen: nudging an object mutates m_scene and relies on
// the next frame's copy to be seen.  So the signal is made explicit instead.
void RubyQuickViewport::touch_scene(bool geometry_changed) {
    m_sceneRevision.fetch_add(1, std::memory_order_release);
    if (geometry_changed)
        m_sceneGeometryRevision.fetch_add(1, std::memory_order_release);
}

void RubyQuickViewport::validateAndRepairScene() {
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        auto& obj = m_scene.objects[i];
        if (std::isnan(obj.pos_x) || std::isinf(obj.pos_x)) obj.pos_x = 0.0f;
        if (std::isnan(obj.pos_y) || std::isinf(obj.pos_y)) obj.pos_y = 0.0f;
        if (std::isnan(obj.pos_z) || std::isinf(obj.pos_z)) obj.pos_z = 0.0f;
        if (std::isnan(obj.rot_x) || std::isinf(obj.rot_x)) obj.rot_x = 0.0f;
        if (std::isnan(obj.rot_y) || std::isinf(obj.rot_y)) obj.rot_y = 0.0f;
        if (std::isnan(obj.rot_z) || std::isinf(obj.rot_z)) obj.rot_z = 0.0f;
        if (std::isnan(obj.scale_x) || std::isinf(obj.scale_x) || std::abs(obj.scale_x) < 1e-4f) obj.scale_x = 1.0f;
        if (std::isnan(obj.scale_y) || std::isinf(obj.scale_y) || std::abs(obj.scale_y) < 1e-4f) obj.scale_y = 1.0f;
        if (std::isnan(obj.scale_z) || std::isinf(obj.scale_z) || std::abs(obj.scale_z) < 1e-4f) obj.scale_z = 1.0f;
        if (std::isnan(obj.template_scaling) || std::isinf(obj.template_scaling) || std::abs(obj.template_scaling) < 1e-4f) obj.template_scaling = 1.0f;
        if (obj.name.empty()) {
            obj.name = av::scene_fresh_identifier(m_scene);
        }
    }
    av::scene_refresh(m_scene);
    touch_scene(/*geometry_changed=*/true);
}

bool RubyQuickViewport::loadScene(const QString& path) {
    m_filePath = path;
    m_scene = av::scene_load(path.toStdString());
    validateAndRepairScene();
    m_hasScene = !m_scene.objects.empty() || !m_scene.filepath.empty();
    m_hasModel = false;
    m_sceneLoadedFlag = true;
    m_modelLoadedFlag = false;
    m_selectedObject = -1;
    m_undoSnapshots.clear();
    m_redoSnapshots.clear();
    if (m_hasScene) {
        pushUndoSnapshot();
        m_objectModel.updateFromScene(m_scene);
    }
    frameScene();
    markDirty(false);
    emit sceneLoaded();
    emit objectListChanged();
    emit selectedObjectChanged();
    update();
    return m_hasScene;
}

void RubyQuickViewport::loadSceneAsync(const QString& path) {
    uint64_t gen = ++m_sceneLoadGen;
    m_filePath = path;
    m_isSceneLoading = true;
    m_loadingProgress = 0.1f;
    m_loadingStatus = tr("Reading scene data...");
    emit isSceneLoadingChanged();
    emit loadingProgressChanged();
    emit loadingStatusChanged();

    std::string stdPath = path.toStdString();

    QThreadPool::globalInstance()->start([this, gen, stdPath, path]() {
        // 1. Parse scene from disk in worker thread
        av::SceneData scene = av::scene_load(stdPath);
        if (m_sceneLoadGen.load() != gen) return;

        QMetaObject::invokeMethod(this, [this, gen]() {
            if (m_sceneLoadGen.load() != gen) return;
            m_loadingProgress = 0.35f;
            m_loadingStatus = tr("Validating scene entities...");
            emit loadingProgressChanged();
            emit loadingStatusChanged();
        }, Qt::QueuedConnection);

        // 2. Validate and repair scene objects in memory
        for (auto& obj : scene.objects) {
            if (obj.name.empty()) {
                obj.name = av::scene_fresh_identifier(scene);
            }
            if (std::isnan(obj.scale_x) || std::isinf(obj.scale_x) || obj.scale_x == 0.0f) obj.scale_x = 1.0f;
            if (std::isnan(obj.scale_y) || std::isinf(obj.scale_y) || obj.scale_y == 0.0f) obj.scale_y = 1.0f;
            if (std::isnan(obj.scale_z) || std::isinf(obj.scale_z) || obj.scale_z == 0.0f) obj.scale_z = 1.0f;
            if (std::isnan(obj.pos_x) || std::isinf(obj.pos_x)) obj.pos_x = 0.0f;
            if (std::isnan(obj.pos_y) || std::isinf(obj.pos_y)) obj.pos_y = 0.0f;
            if (std::isnan(obj.pos_z) || std::isinf(obj.pos_z)) obj.pos_z = 0.0f;
        }
        av::scene_refresh(scene);
        if (m_sceneLoadGen.load() != gen) return;

        // 3. Pre-load models & pre-decode textures in background
        fs::path scPath(stdPath);
        std::vector<fs::path> roots = build_asset_roots(scPath);

        std::unordered_map<std::string, av::PODModel> preloadedPods;
        std::unordered_map<std::string, QImage> predecodedImages;

        // Background texture
        std::string bgName;
        for (const auto& obj : scene.objects) {
            if (!obj.background_name.empty()) {
                bgName = obj.background_name;
                break;
            }
        }
        // ── Enumerate every asset the level needs (filesystem, one thread) ───
        //
        // Probing stays serial: it is cheap (measured ~0.5 ms for a whole level)
        // and resolve_pod/texture_candidates already mutex-guard their caches.
        // Only decode and parse are parallelized.  Dedup first, because a level
        // references the same POD and the same texture from many objects, and the
        // old loop re-checked that per object with a string-keyed map lookup.
        std::vector<std::string> pod_paths;
        std::vector<std::string> pod_hints;
        std::set<std::string>    pod_seen;
        std::vector<std::string> tex_paths;
        std::set<std::string>    tex_seen;

        auto take_texture = [&](const fs::path& base, const std::string& name) {
            if (name.empty()) return;
            for (const auto& cand : av::assets::texture_candidates(base, name, roots)) {
                std::error_code ec;
                if (!fs::exists(cand, ec)) continue;
                const std::string s = cand.string();
                if (tex_seen.insert(s).second) tex_paths.push_back(s);
                return;   // first existing candidate wins, as load_texture_any does
            }
        };

        if (!bgName.empty()) {
            static const char* bgSufs[] = {
                "_2x.tex.png", ".tex.png", "_2x.pvr", ".pvr", "_2x.tex", ".tex", "_2x.png", ".png", ""
            };
            // Same priority order setup_background() walks, so the pre-decoded
            // entry is the one it will actually ask for.  (The old loop kept
            // scanning later roots and could pre-decode a file that was never
            // used, then fail to hit the cache for the one that was.)
            bool found = false;
            for (const auto& root : roots) {
                if (found) break;
                for (const char* suf : bgSufs) {
                    std::error_code ec;
                    const fs::path cand = root / (bgName + suf);
                    if (!fs::exists(cand, ec)) continue;
                    const std::string s = cand.string();
                    if (tex_seen.insert(s).second) tex_paths.push_back(s);
                    found = true;
                    break;
                }
            }
        }

        for (const auto& obj : scene.objects)
            for (const auto& tex_name : obj.ground_mesh_textures)
                take_texture(scPath, tex_name);

        for (const auto& obj : scene.objects) {
            const std::string modelName = !obj.mesh_name.empty() ? obj.mesh_name : obj.template_name;
            if (modelName.empty()) continue;
            const fs::path podPath = av::assets::resolve_pod(scPath, modelName, roots);
            if (podPath.empty()) continue;
            std::error_code ec;
            if (!fs::exists(podPath, ec)) continue;
            const std::string sp = podPath.string();
            if (pod_seen.insert(sp).second) {
                pod_paths.push_back(sp);
                pod_hints.push_back(obj.template_name);
            }
        }
        if (m_sceneLoadGen.load() != gen) return;

        // ── Decode every texture in parallel ────────────────────────────────
        // Each worker writes only its own slot, so results need no lock.
        std::vector<QImage> decoded(tex_paths.size());
        parallel_for(tex_paths.size(), kSceneLoadThreads, [&](size_t i) {
            QImage img = decode_image_file(tex_paths[i]);
            if (!img.isNull()) decoded[i] = img.convertToFormat(QImage::Format_RGBA8888);
        });
        if (m_sceneLoadGen.load() != gen) return;

        QMetaObject::invokeMethod(this, [this, gen]() {
            if (m_sceneLoadGen.load() != gen) return;
            m_loadingProgress = 0.70f;
            m_loadingStatus = tr("Preloading 3D models...");
            emit loadingProgressChanged();
            emit loadingStatusChanged();
        }, Qt::QueuedConnection);

        // ── Parse every POD in parallel ─────────────────────────────────────
        std::vector<av::PODModel> pods(pod_paths.size());
        parallel_for(pod_paths.size(), kSceneLoadThreads, [&](size_t i) {
            av::PODModel m = av::pod_load(pod_paths[i], pod_hints[i]);
            if (!m.meshes.empty()) pods[i] = std::move(m);
        });
        if (m_sceneLoadGen.load() != gen) return;

        // A POD's texture list is only knowable once it has been parsed, so pod
        // textures are a second, smaller wave rather than the same pass.
        std::vector<std::string> pod_tex_paths;
        for (size_t i = 0; i < pods.size(); ++i) {
            for (const auto& tname : pods[i].texture_filenames) {
                if (tname.empty()) continue;
                for (const auto& cand : av::assets::texture_candidates(fs::path(pod_paths[i]),
                                                                      tname, roots)) {
                    std::error_code ec;
                    if (!fs::exists(cand, ec)) continue;
                    const std::string s = cand.string();
                    if (tex_seen.insert(s).second) pod_tex_paths.push_back(s);
                    break;
                }
            }
        }
        std::vector<QImage> pod_decoded(pod_tex_paths.size());
        parallel_for(pod_tex_paths.size(), kSceneLoadThreads, [&](size_t i) {
            QImage img = decode_image_file(pod_tex_paths[i]);
            if (!img.isNull()) pod_decoded[i] = img.convertToFormat(QImage::Format_RGBA8888);
        });
        if (m_sceneLoadGen.load() != gen) return;

        // ── Assemble (single-threaded: these maps are not concurrent) ────────
        for (size_t i = 0; i < tex_paths.size(); ++i)
            if (!decoded[i].isNull()) predecodedImages[tex_paths[i]] = std::move(decoded[i]);
        for (size_t i = 0; i < pod_tex_paths.size(); ++i)
            if (!pod_decoded[i].isNull()) predecodedImages[pod_tex_paths[i]] = std::move(pod_decoded[i]);
        for (size_t i = 0; i < pod_paths.size(); ++i)
            if (!pods[i].meshes.empty()) preloadedPods[pod_paths[i]] = std::move(pods[i]);

        if (m_sceneLoadGen.load() != gen) return;

        // 4. Commit to UI & scene state on main thread
        QMetaObject::invokeMethod(this, [this, gen, scene = std::move(scene),
                                         preloadedPods = std::move(preloadedPods),
                                         predecodedImages = std::move(predecodedImages),
                                         path]() mutable {
            if (m_sceneLoadGen.load() != gen) return;
            m_filePath = path;
            m_scene = std::move(scene);
            m_preloadedPods = std::move(preloadedPods);
            m_predecodedImages = std::move(predecodedImages);
            m_hasScene = !m_scene.objects.empty() || !m_scene.filepath.empty();
            m_hasModel = false;
            m_sceneLoadedFlag = true;
            m_modelLoadedFlag = false;
            m_selectedObject = -1;
            m_undoSnapshots.clear();
            m_redoSnapshots.clear();
            if (m_hasScene) {
                pushUndoSnapshot();
                m_objectModel.updateFromScene(m_scene);
            }
            frameScene();
            markDirty(false);

            m_loadingProgress = 1.0f;
            m_loadingStatus = tr("Ready");
            m_isSceneLoading = false;

            emit isSceneLoadingChanged();
            emit loadingProgressChanged();
            emit loadingStatusChanged();
            emit sceneLoaded();
            emit objectListChanged();
            emit selectedObjectChanged();
            update();
        }, Qt::QueuedConnection);
    });
}

bool RubyQuickViewport::loadModel(const QString& path) {
    m_filePath = path;
    m_modelName = QFileInfo(path).fileName();
    m_model = av::pod_load(path.toStdString());
    m_hasModel = !m_model.meshes.empty();
    m_hasScene = false;
    m_modelLoadedFlag = true;
    m_sceneLoadedFlag = false;
    m_selectedObject = -1;

    if (m_hasModel) {
        if (m_model.radius <= 0.0f) {
            float dx = m_model.max_x - m_model.min_x;
            float dy = m_model.max_y - m_model.min_y;
            float dz = m_model.max_z - m_model.min_z;
            m_model.radius = std::max(0.1f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
            m_model.center_x = 0.5f * (m_model.min_x + m_model.max_x);
            m_model.center_y = 0.5f * (m_model.min_y + m_model.max_y);
            m_model.center_z = 0.5f * (m_model.min_z + m_model.max_z);
        }
        m_cameraTarget = QVector3D(m_model.center_x, m_model.center_y, m_model.center_z);
        m_cameraDistance = std::max(2.0f, m_model.radius * 2.5f);
        m_cameraPitch = 15.0f;
        m_cameraYaw = -35.0f;
        m_cameraMode = CameraMode3DEdit;
    } else {
        frameScene();
    }
    emit cameraModeChanged();
    emit cameraChanged();
    emit modelLoaded();
    emit sceneLoaded();
    update();
    return m_hasModel;
}

void RubyQuickViewport::resetCamera() {
    frameScene();
}

// Undo history bound.
//
// A snapshot is the WHOLE serialized scene, so a 25 MB level costs 25 MB per
// entry.  The previous cap was 25 entries by count, which on that level is
// 625 MB of undo history — more than the Ruby GG performance charter's entire
// 500 MB runtime budget, spent on undo alone.  Bound by bytes as well as by
// count and evict oldest-first; always keep at least one entry so undo stays
// possible on a scene too large to snapshot cheaply.
static constexpr size_t kUndoHistoryMaxEntries = 25;
static constexpr size_t kUndoHistoryMaxBytes   = 192ull * 1024ull * 1024ull;

static void trim_snapshot_history(std::vector<std::vector<uint8_t>>& history) {
    size_t total = 0;
    for (const auto& s : history) total += s.size();
    while (history.size() > 1 &&
           (history.size() > kUndoHistoryMaxEntries || total > kUndoHistoryMaxBytes)) {
        total -= history.front().size();
        history.erase(history.begin());
    }
}

void RubyQuickViewport::pushUndoSnapshot() {
    // NOTE: scene_serialize() returns a std::string, so this is one unavoidable
    // intermediate buffer.  The cost that mattered was calling this per nudge,
    // not the copy into the vector — see the coalescing in nudgeSelectedCoord()
    // and rotateSelectedZ().
    const std::string b = av::scene_serialize(m_scene);
    m_undoSnapshots.emplace_back(b.begin(), b.end());
    trim_snapshot_history(m_undoSnapshots);
    m_redoSnapshots.clear();
    markDirty(true);
    emit undoStateChanged();
}

void RubyQuickViewport::markDirty(bool dirty) {
    if (m_dirty == dirty) return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void RubyQuickViewport::undo() {
    if (m_undoSnapshots.empty()) return;
    if (m_meshEditActive) discardMeshEdit();
    std::string current = av::scene_serialize(m_scene);
    m_redoSnapshots.push_back(std::vector<uint8_t>(current.begin(), current.end()));
    trim_snapshot_history(m_redoSnapshots);

    auto prev = m_undoSnapshots.back();
    m_undoSnapshots.pop_back();

    m_scene = av::scene_load_bytes(prev, m_filePath.toStdString());
    validateAndRepairScene();
    m_objectModel.updateFromScene(m_scene);
    m_selectedObject = -1;
    m_lastNudgeMs = 0;
    touch_scene(/*geometry_changed=*/true);
    emit objectListChanged();
    emit selectedObjectChanged();
    emit objectTransformChanged();
    emit undoStateChanged();
    update();
}

void RubyQuickViewport::redo() {
    if (m_redoSnapshots.empty()) return;
    if (m_meshEditActive) discardMeshEdit();
    std::string current = av::scene_serialize(m_scene);
    m_undoSnapshots.push_back(std::vector<uint8_t>(current.begin(), current.end()));
    trim_snapshot_history(m_undoSnapshots);

    auto next = m_redoSnapshots.back();
    m_redoSnapshots.pop_back();

    m_scene = av::scene_load_bytes(next, m_filePath.toStdString());
    validateAndRepairScene();
    m_objectModel.updateFromScene(m_scene);
    m_selectedObject = -1;
    m_lastNudgeMs = 0;
    touch_scene(/*geometry_changed=*/true);
    emit objectListChanged();
    emit selectedObjectChanged();
    emit objectTransformChanged();
    emit undoStateChanged();
    update();
}

void RubyQuickViewport::selectObject(int idx) {
    if (idx >= -1 && idx < int(m_scene.objects.size())) {
        m_selectedObject = idx;
        m_lastNudgeMs = 0;
        emit selectedObjectChanged();
        update();
    }
}

int RubyQuickViewport::pickObjectAt(float x, float y) {
    if (!m_hasScene || m_scene.objects.empty()) return -1;

    float aspect = float(width()) / qMax(1.0f, float(height()));
    QMatrix4x4 proj;
    proj.perspective(45.0f, aspect, 1.0f, 30000.0f);

    float radPitch = m_cameraPitch * M_PI / 180.0f;
    float radYaw = m_cameraYaw * M_PI / 180.0f;
    QVector3D eye = m_cameraTarget + QVector3D(
        m_cameraDistance * std::cos(radPitch) * std::sin(radYaw),
        m_cameraDistance * std::sin(radPitch),
        m_cameraDistance * std::cos(radPitch) * std::cos(radYaw)
    );

    QVector3D forward = (m_cameraTarget - eye).normalized();
    QVector3D up(0.0f, 1.0f, 0.0f);
    if (std::abs(QVector3D::dotProduct(forward, up)) > 0.99f) {
        up = (forward.y() > 0.0f) ? QVector3D(0.0f, 0.0f, 1.0f) : QVector3D(0.0f, 0.0f, -1.0f);
    }

    QMatrix4x4 view;
    view.lookAt(eye, m_cameraTarget, up);

    Ray ray = ruby::picking::get_mouse_ray(x, y, float(width()), float(height()), view, proj);

    int closest3DIdx = -1;
    float closest3DDist = 1e9f;

    int closestScreenIdx = -1;
    float closestScreenDist = 140.0f;

    QMatrix4x4 vp = proj * view;

    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const auto& obj = m_scene.objects[i];
        if (obj.hidden) continue;

        float hw = std::max(40.0f, std::abs(obj.scale_x * obj.template_scaling) * 1.5f);
        float hh = std::max(40.0f, std::abs(obj.scale_y * obj.template_scaling) * 1.5f);
        float hd = std::max(40.0f, std::abs(obj.scale_z * obj.template_scaling) * 1.5f);

        BoundingBox box;
        box.min = Vector3{obj.pos_x - hw, obj.pos_y - hh, obj.pos_z - hd};
        box.max = Vector3{obj.pos_x + hw, obj.pos_y + hh, obj.pos_z + hd};

        RayCollision col = ruby::picking::get_ray_collision_box(ray, box);
        if (col.hit && col.distance < closest3DDist) {
            closest3DDist = col.distance;
            closest3DIdx = static_cast<int>(i);
        }

        QVector4D clip = vp.map(QVector4D(obj.pos_x, obj.pos_y, obj.pos_z, 1.0f));
        if (clip.w() > 0.001f) {
            QVector3D ndc = clip.toVector3D() / clip.w();
            if (ndc.z() >= -1.0f && ndc.z() <= 1.0f) {
                float sx = (ndc.x() + 1.0f) * 0.5f * float(width());
                float sy = (1.0f - ndc.y()) * 0.5f * float(height());
                float sdist = std::hypot(sx - x, sy - y);
                if (sdist < closestScreenDist) {
                    closestScreenDist = sdist;
                    closestScreenIdx = static_cast<int>(i);
                }
            }
        }
    }

    int finalIdx = (closest3DIdx >= 0) ? closest3DIdx : closestScreenIdx;
    if (finalIdx >= 0) {
        selectObject(finalIdx);
    }
    return finalIdx;
}

void RubyQuickViewport::toggleObjectVisibility(int idx) {
    if (idx >= 0 && idx < int(m_scene.objects.size())) {
        m_scene.objects[idx].hidden = !m_scene.objects[idx].hidden;
        // Visibility is not geometry: the renderer must re-read the scene, but no
        // GPU buffer becomes invalid.
        touch_scene(/*geometry_changed=*/false);
        m_objectModel.toggleHidden(idx);
        emit objectListChanged();
        emit objectTransformChanged();
        update();
    }
}

void RubyQuickViewport::panCamera(float dx, float dy) {
    const float speed = m_cameraDistance * 0.018f;
    const float yaw_rad = m_cameraYaw * (M_PI / 180.0f);

    const float right_x = std::cos(yaw_rad);
    const float right_z = -std::sin(yaw_rad);

    m_cameraTarget.setX(m_cameraTarget.x() + (right_x * dx) * speed);
    m_cameraTarget.setY(m_cameraTarget.y() + dy * speed);
    m_cameraTarget.setZ(m_cameraTarget.z() + (right_z * dx) * speed);

    emit cameraChanged();
    if (m_meshEditActive) emit meshEditVerticesChanged();
    update();
}

void RubyQuickViewport::dollyCamera(float forward, float strafe) {
    if (m_meshEditActive) {
        // In mesh edit mode: right thumbstick forward/back smoothly zooms in/out!
        float zoomFactor = 1.0f - (forward * 0.04f);
        m_cameraDistance = std::clamp(m_cameraDistance * zoomFactor, 100.0f, 15000.0f);
        emit cameraChanged();
        emit meshEditVerticesChanged();
        update();
        return;
    }

    const float speed = m_cameraDistance * 0.018f;
    const float yaw_rad = m_cameraYaw * (M_PI / 180.0f);

    const float fwd_x = -std::sin(yaw_rad);
    const float fwd_z = -std::cos(yaw_rad);
    const float right_x = std::cos(yaw_rad);
    const float right_z = -std::sin(yaw_rad);

    m_cameraTarget.setX(m_cameraTarget.x() + (fwd_x * forward + right_x * strafe) * speed);
    m_cameraTarget.setZ(m_cameraTarget.z() + (fwd_z * forward + right_z * strafe) * speed);

    emit cameraChanged();
    update();
}

QVariantList RubyQuickViewport::objectsSnapshot() const {
    QVariantList out;
    out.reserve(int(m_scene.objects.size()));
    for (size_t i = 0; i < m_scene.objects.size(); ++i) {
        const auto& obj = m_scene.objects[i];

        QVariantMap row;
        row["index"] = int(i);
        row["name"] = obj.name.empty()
                          ? QStringLiteral("obj%1").arg(i)
                          : QString::fromStdString(obj.name);
        row["subtitle"] = obj.template_name.empty()
                              ? QString::fromStdString(obj.mesh_name)
                              : QString::fromStdString(obj.template_name);
        row["hidden"] = obj.hidden;
        out.append(row);
    }
    return out;
}

void RubyQuickViewport::focusObject(int idx) {
    if (idx < 0 || idx >= int(m_scene.objects.size())) return;
    const auto& obj = m_scene.objects[idx];
    m_cameraTarget = QVector3D(obj.pos_x, obj.pos_y, obj.pos_z);
    m_selectedObject = idx;
    emit selectedObjectChanged();
    update();
}

void RubyQuickViewport::addObject() {
    pushUndoSnapshot();

    size_t new_idx = av::scene_create_object(m_scene, "SceneObject");
    if (new_idx < m_scene.objects.size()) {
        auto& obj = m_scene.objects[new_idx];
        obj.pos_x = m_cameraTarget.x();
        obj.pos_y = m_cameraTarget.y();
        obj.pos_z = m_cameraTarget.z();
        obj.scale_x = 1.0f;
        obj.scale_y = 1.0f;
        obj.scale_z = 1.0f;
        obj.template_scaling = 1.0f;
    }

    validateAndRepairScene();
    m_selectedObject = static_cast<int>(new_idx);
    m_lastNudgeMs = 0;
    m_hasScene = true;
    touch_scene(/*geometry_changed=*/true);
    m_objectModel.updateFromScene(m_scene);

    emit objectListChanged();
    emit selectedObjectChanged();
    emit objectTransformChanged();
    update();
}

void RubyQuickViewport::deleteObject(int idx) {
    if (idx < 0 || idx >= int(m_scene.objects.size())) return;
    pushUndoSnapshot();

    if (m_meshEditActive && m_meshEditObject == idx) {
        discardMeshEdit();
    }

    av::scene_delete_object(m_scene, static_cast<size_t>(idx));
    validateAndRepairScene();

    if (m_selectedObject == idx) {
        m_selectedObject = -1;
    } else if (m_selectedObject > idx) {
        --m_selectedObject;
    }
    m_lastNudgeMs = 0;
    touch_scene(/*geometry_changed=*/true);

    m_objectModel.updateFromScene(m_scene);
    emit objectListChanged();
    emit selectedObjectChanged();
    emit objectTransformChanged();
    update();
}

void RubyQuickViewport::duplicateObject(int idx) {
    if (idx < 0 || idx >= int(m_scene.objects.size())) return;
    pushUndoSnapshot();

    size_t new_idx = 0;
    if (av::scene_duplicate_object(m_scene, static_cast<size_t>(idx), &new_idx)) {
        if (new_idx < m_scene.objects.size()) {
            m_scene.objects[new_idx].pos_x += 15.0f;
            m_scene.objects[new_idx].pos_y += 15.0f;
        }
        validateAndRepairScene();

        m_selectedObject = static_cast<int>(new_idx);
        touch_scene(/*geometry_changed=*/true);
        m_objectModel.updateFromScene(m_scene);

        emit objectListChanged();
        emit selectedObjectChanged();
        emit objectTransformChanged();
        update();
    }
}

bool RubyQuickViewport::canMeshEdit() const {
    if (!m_hasScene || m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return false;
    const auto& obj = m_scene.objects[m_selectedObject];
    if (!obj.ground_meshes.empty() || !obj.ground_polygon_points.empty()) return true;
    const auto& comps = obj.components.empty() ? obj.resolved_components : obj.components;
    for (const auto& comp : comps) {
        if (comp.payload_field == 110 || comp.payload_field == 111 || comp.payload_field == 112 ||
            comp.type_name == "GroundPolygon" || comp.type_name == "GroundMesh" ||
            comp.type_name == "GroundMeshGenerator") {
            return true;
        }
    }
    return false;
}

void RubyQuickViewport::meshImport(int idx) {
    m_meshEditPoints.clear();
    if (idx < 0 || idx >= int(m_scene.objects.size())) return;
    const auto& obj = m_scene.objects[idx];
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
    // Both of these were read from the generator component and then dropped, so
    // every edit rebuilt the object with mesh type 1 and zero noise regardless
    // of what it had. Mesh type 1 adds the rounded rim, so a MeshType 0 sheet
    // gained geometry it never had; and zero noise is one of the two reasons the
    // regenerated relief did not match the original.
    int mesh_type = 1;
    double horiz_noise = 0.0;
    double texture_scale = 250.0;

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
                            m_meshEditPoints.push_back({x, y});
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
                        else if (g.field_number == 6 && g.wire_type == proto::WIRE_I32) horiz_noise = g.float_val;
                        else if (g.field_number == 7 && g.wire_type == proto::WIRE_VARINT) mesh_type = int(g.varint_val);
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

    // Fallback: unique XY positions from the embedded ground meshes
    if (m_meshEditPoints.size() < 3) {
        for (const auto& gm : obj.ground_meshes) {
            for (size_t i = 0; i + 2 < gm.positions.size(); i += 3) {
                const double x = gm.positions[i], y = gm.positions[i + 1];
                bool dup = false;
                for (const auto& pt : m_meshEditPoints)
                    if (std::fabs(pt.x - x) < 0.01 && std::fabs(pt.y - y) < 0.01) { dup = true; break; }
                if (!dup && m_meshEditPoints.size() < 64) m_meshEditPoints.push_back({x, y});
            }
            if (m_meshEditPoints.size() >= 3) break;
        }
    }

    // No fabricated fallback outline here.  There used to be one — a canned
    // 200x100 rectangle — which meant that arming Mesh Edit on a ground object
    // whose polygon could not be recovered and then committing silently REPLACED
    // the object's real ground geometry with that rectangle: the mesh still
    // collided, but the original terrain was gone for good.  Desktop Ruby bails
    // instead (mesh_import returns early), so mobile does too: begin_mesh_edit
    // refuses below and the object is left untouched.

    if (!have_depth) {
        float dmin = 1e30f, dmax = -1e30f;
        for (const auto& pm : obj.ground_meshes) {
            dmin = std::min(dmin, pm.min_z); dmax = std::max(dmax, pm.max_z);
        }
        if (dmin < 1e29f && dmax > -1e29f) { min_depth = dmin; max_depth = dmax; }
    }

    m_meshEditParams = boulder::GroundMesh{};
    m_meshEditParams.polygon = m_meshEditPoints;
    m_meshEditParams.min_depth = min_depth;
    m_meshEditParams.max_depth = max_depth;
    m_meshEditParams.z = obj.pos_z;
    m_meshEditParams.top_texture = top_tex;
    m_meshEditParams.bottom_texture = bottom_tex;
    m_meshEditParams.surface_width = surface_width;
    m_meshEditParams.hat_height = hat_height;
    m_meshEditParams.hat_width_offset_1 = hat_offset_1;
    m_meshEditParams.hat_width_offset_2 = hat_offset_2;

    // Remember what the object was actually built from. Without these the
    // regeneration is a different object with the same outline.
    m_meshEditMeshType = mesh_type;
    m_meshEditNoise = horiz_noise;
    m_meshEditSeed = random_seed;
    m_meshEditTextureScale = texture_scale;
    m_meshEditMinDepth = min_depth;
    m_meshEditMaxDepth = max_depth;
    // Relief starts clean: a fresh import is a uniform slab at the object's own
    // depth, and relief is something the modder asks for. Carving recovered
    // per-node depths back out of the baked cap would need a node-to-vertex
    // correspondence the format does not guarantee, and guessing it is how you
    // silently deform terrain. The engine's own mechanism — HorizNoise over the
    // per-node depths — is carried through instead.
    m_meshEditFrontZ.clear();
    m_meshEditBackZ.clear();
    m_meshEditReliefSeed = 0;
    m_meshEditParams.texture_scale = scale_from_tm;
    m_meshEditParams.random_seed = random_seed;
    m_meshEditIds = target_ids;
    m_meshEditZ = obj.pos_z;
}

bool RubyQuickViewport::beginMeshEdit() {
    if (!canMeshEdit()) return false;
    m_meshEditObject = m_selectedObject;
    meshImport(m_meshEditObject);
    if (m_meshEditPoints.size() < 3) return false;

    m_meshSceneSaved = m_scene;
    m_meshSceneSavedValid = true;
    m_meshDirty = false;
    m_meshDragPoint = -1;
    m_meshDragging = false;
    m_meshPanning = false;

    // Save camera before aligning
    m_meshSavedCamPitch = m_cameraPitch;
    m_meshSavedCamYaw = m_cameraYaw;
    m_meshSavedCamDistance = m_cameraDistance;
    m_meshSavedCamTarget = m_cameraTarget;
    m_meshSavedCamValid = true;

    const auto& obj = m_scene.objects[m_meshEditObject];
    float obj_mat[16];
    calc_object_world_matrix(obj, obj_mat);
    double minx = 1e30, miny = 1e30, minz = 1e30;
    double maxx = -1e30, maxy = -1e30, maxz = -1e30;
    for (const auto& pt : m_meshEditPoints) {
        const float lp[3] = {float(pt.x), float(pt.y), 0.0f};
        const float wx = obj_mat[0]*lp[0] + obj_mat[4]*lp[1] + obj_mat[8]*lp[2] + obj_mat[12];
        const float wy = obj_mat[1]*lp[0] + obj_mat[5]*lp[1] + obj_mat[9]*lp[2] + obj_mat[13];
        const float wz = obj_mat[2]*lp[0] + obj_mat[6]*lp[1] + obj_mat[10]*lp[2] + obj_mat[14];
        minx = std::min(minx, (double)wx); maxx = std::max(maxx, (double)wx);
        miny = std::min(miny, (double)wy); maxy = std::max(maxy, (double)wy);
        minz = std::min(minz, (double)wz); maxz = std::max(maxz, (double)wz);
    }
    m_cameraTarget = QVector3D(float((minx + maxx) * 0.5),
                              float((miny + maxy) * 0.5),
                              float((minz + maxz) * 0.5));
    constexpr double kPi = 3.14159265358979323846;
    const double rot_y_deg = obj.rot_y * 180.0 / kPi;
    m_cameraYaw = float(-rot_y_deg);
    m_cameraPitch = 0.0f;
    const double fit = std::max({maxx - minx, maxy - miny, 100.0});
    const double vfov = 45.0 * kPi / 180.0;
    m_cameraDistance = float(std::clamp((fit * 0.5) / std::tan(vfov * 0.5) * 1.25, 200.0, 15000.0));

    m_selectedMeshVertex = 0;
    m_meshEditActive = true;
    m_gizmoMode = 0; // Turn off gizmo during mesh edit mode

    emit gizmoModeChanged();
    emit cameraChanged();
    emit meshEditActiveChanged();
    emit meshEditVerticesChanged();
    emit selectedMeshVertexChanged();
    update();
    return true;
}

// Regenerate the edited object through boulder (the real backend) and swap the
// result into the target object.
//
void RubyQuickViewport::setGroundGeneratorStatus(const QString& status) {
    if (m_groundGeneratorStatus == status) return;
    m_groundGeneratorStatus = status;
    emit groundGeneratorStatusChanged();
}

bool RubyQuickViewport::randomiseGroundRelief(float frontMagnitude, float backMagnitude,
                                              int seed) {
    if (!m_meshEditActive || m_meshEditPoints.size() < 3) return false;

    // The range defaults to the object's own depth, so "randomise" reshapes the
    // terrain without changing how thick it is. A caller can override either
    // side to sculpt thicker or thinner relief deliberately.
    double fmax = frontMagnitude > 0.0f ? double(frontMagnitude)
                                        : std::fabs(double(m_meshEditMaxDepth));
    double bmax = backMagnitude > 0.0f ? double(backMagnitude)
                                       : std::fabs(double(m_meshEditMinDepth));
    if (!(fmax > 0.0)) fmax = 45.0;
    if (!(bmax > 0.0)) bmax = 45.0;

    // seed < 0 means "pick one". Derived from the object's own seed so the result
    // is not blindly random, and reported back through groundReliefSeed() so the
    // modder can re-enter a seed whose terrain they liked.
    uint32_t s = seed < 0
                     ? (m_meshEditSeed * 1664525u +
                        (++m_meshEditReliefCounter) * 1013904223u)
                     : uint32_t(seed);
    if (s == 0) s = 1u;

    std::vector<boulderx::Node> outline;
    outline.reserve(m_meshEditPoints.size());
    for (const auto& p : m_meshEditPoints)
        outline.push_back({p.x, p.y, fmax, -bmax});
    boulderx::randomise_node_depths(outline, fmax, bmax, s);

    // randomise_node_depths works in the outline's own order and nothing is
    // reordered in between, so the depths copy straight back.
    m_meshEditFrontZ.resize(outline.size());
    m_meshEditBackZ.resize(outline.size());
    for (size_t i = 0; i < outline.size(); ++i) {
        m_meshEditFrontZ[i] = outline[i].front_depth;
        m_meshEditBackZ[i] = outline[i].back_depth;
    }
    m_meshEditReliefSeed = s;
    emit groundReliefChanged();

    // Route through the same regeneration the live drag and the commit use, so
    // what you see while sculpting is what gets committed.
    return liveMeshPreview();
}

void RubyQuickViewport::clearGroundRelief() {
    if (m_meshEditFrontZ.empty() && m_meshEditBackZ.empty()) return;
    m_meshEditFrontZ.clear();
    m_meshEditBackZ.clear();
    m_meshEditReliefSeed = 0;
    emit groundReliefChanged();
    if (m_meshEditActive) liveMeshPreview();
}

// This is the ONE place that knows how to turn an edited polygon into scene
// state, and both the live drag preview and the commit go through it.  The
// preview used to carry its own reduced copy that only replaced ground_meshes,
// ground_mesh_textures and ground_polygon_points — leaving resolved_components,
// pos_z and ground_polygon_min_depth/max_depth stale for the whole duration of a
// drag, so the inspector and the walkable-depth helpers disagreed with what was
// on screen until you pressed commit.
bool RubyQuickViewport::regenerateGroundMesh() {
    if (m_meshEditObject < 0 || m_meshEditObject >= int(m_scene.objects.size())) return false;
    if (m_meshEditPoints.size() < 3) return false;

    boulder::GroundMesh gm = m_meshEditParams;
    gm.polygon = m_meshEditPoints;
    boulder::ensure_ccw(gm.polygon);

    // Generator choice: boulder by default, boulderx when the user picks it in
    // Settings (see boulderx::ground_generator()). boulderx declines what it
    // cannot reproduce exactly — currently dome hats — and we fall back rather
    // than drop it.
    std::string bin;
    const bool want_boulderx =
        boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX;
    if (want_boulderx) {
        // A per-node outline, not a uniform slab, and built from
        // m_meshEditPoints rather than gm.polygon: boulder::ensure_ccw may have
        // reversed gm.polygon, and pairing depths with the wrong node would
        // shear the terrain. Carrying x/y/depth together in one Node means the
        // winding fix cannot desynchronise them.
        std::vector<boulderx::Node> outline;
        outline.reserve(m_meshEditPoints.size());
        const bool have_relief = m_meshEditFrontZ.size() == m_meshEditPoints.size() &&
                                 m_meshEditBackZ.size() == m_meshEditPoints.size();
        for (size_t i = 0; i < m_meshEditPoints.size(); ++i) {
            boulderx::Node n;
            n.x = m_meshEditPoints[i].x;
            n.y = m_meshEditPoints[i].y;
            // A modder's relief if they asked for one, otherwise the object's own
            // depth — so the mesh keeps the thickness it arrived with.
            n.front_depth = have_relief ? m_meshEditFrontZ[i] : double(gm.max_depth);
            n.back_depth = have_relief ? m_meshEditBackZ[i] : double(gm.min_depth);
            outline.push_back(n);
        }
        boulderx::ensure_ccw(outline);

        // Everything meshImport recovered goes back in. Previously only the mesh
        // type (hardcoded to 1) and one scalar depth were passed, so an edit
        // turned a MeshType 0 sheet into a rimmed one and rebuilt the relief
        // from zero noise — i.e. it changed the object's structure and flattened
        // its terrain, regardless of what the object was.
        boulderx::Params params;
        params.mesh_type = m_meshEditMeshType;
        params.horiz_noise = m_meshEditNoise;
        params.random_seed = m_meshEditSeed;
        params.texture_scale = m_meshEditTextureScale;
        params.surface_width = gm.surface_width;
        params.hat_height = gm.hat_height;
        params.hat_width_offset_1 = gm.hat_width_offset_1;
        params.hat_width_offset_2 = gm.hat_width_offset_2;
        params.surface_texture = gm.top_texture;
        params.front_texture = gm.bottom_texture;

        const int ids6[6] = {m_meshEditIds.polygon_id, m_meshEditIds.mesh_id,
                             m_meshEditIds.generator_id, m_meshEditIds.collision_id,
                             m_meshEditIds.tm_surface_id, m_meshEditIds.tm_front_id};
        bin = boulderx::generate_ground_mesh_object_nodes(
            outline, params, m_scene.objects[m_meshEditObject].name, !gm.hats.empty(), ids6);
    }
    if (bin.empty()) {
        const std::string swdm = boulder::serialize_swdm(gm);
        bin = boulder::generate_ground_mesh_object(
            swdm, m_scene.objects[m_meshEditObject].name, m_meshEditZ, &m_meshEditIds);
    }

    // Report which generator actually produced this mesh. The reason is only
    // consulted when boulderx was asked for: it survives from an earlier edit
    // otherwise, and showing it would attribute a decline to a run that never
    // requested boulderx. A refusal that reaches the user must be the refusal
    // that just happened.
    if (!want_boulderx) {
        setGroundGeneratorStatus(QStringLiteral("Boulder"));
    } else if (!boulderx::last_decline_reason().empty()) {
        setGroundGeneratorStatus(
            QStringLiteral("Boulder · BoulderX declined: %1")
                .arg(QString::fromStdString(boulderx::last_decline_reason())));
    } else {
        setGroundGeneratorStatus(QStringLiteral("BoulderX"));
    }

    if (bin.empty()) return false;

    std::vector<uint8_t> bytes(bin.begin(), bin.end());
    av::SceneData parsed;
    try {
        parsed = av::scene_load_bytes(bytes, m_filePath.toStdString());
    } catch (...) {
        return false;
    }
    if (parsed.objects.empty()) return false;

    auto& target = m_scene.objects[m_meshEditObject];
    const bool was_dim = target.is_dimension_object;
    auto fresh = std::move(parsed.objects[0]);

    const auto is_ground_comp = [](const av::SceneComponent& c) {
        return c.payload_field == 110 || c.payload_field == 111 ||
               c.payload_field == 112 || c.payload_field == 113 ||
               c.payload_field == 120 || c.payload_field == 121 ||
               c.type_name == "GroundPolygon" || c.type_name == "GroundMesh" ||
               c.type_name == "GroundMeshGenerator" || c.type_name == "CollisionShape" ||
               c.type_name == "TextureMapping";
    };

    std::vector<av::SceneComponent> preserved;
    for (auto& c : target.components) {
        if (!is_ground_comp(c)) preserved.push_back(std::move(c));
    }
    target.components = std::move(fresh.components);
    for (auto& c : preserved) target.components.push_back(std::move(c));

    if (was_dim) {
        // 559, NOT 253.  Verified against the decompiled extension table in
        // Caver::Proto::protobuf_AddDesc_Scene_2eproto (libswordigo arm64 1.4.13,
        // sub_41A8DC):
        //     DimensionObjectComponent::extension = 559
        //     FireEmitterComponent::extension      = 253
        // 253 was detecting a FIRE EMITTER, so an object that carried one was
        // treated as "already has a DimensionObject" and the re-add below was
        // skipped.  is_dimension_object itself is set from the ClassName (see
        // scene_loader), which is why the flag and this probe disagreed.
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
    target.pos_z = float(m_meshEditZ);
    av::scene_refresh(m_scene);

    // The re-parsed geometry wins over what scene_refresh re-derived
    target.ground_meshes = std::move(fresh.ground_meshes);
    if (!fresh.ground_mesh_textures.empty()) {
        target.ground_mesh_textures = std::move(fresh.ground_mesh_textures);
    }

    // scene_refresh does NOT re-derive the GroundPolygon fields, so write the
    // regenerated outline here — otherwise re-entering Mesh Edit after an apply
    // would import a stale polygon, and the helpers that read
    // ground_polygon_points would see outdated walkable data.
    target.ground_polygon_points.clear();
    for (const auto& pt : gm.polygon) {
        target.ground_polygon_points.push_back(float(pt.x));
        target.ground_polygon_points.push_back(float(pt.y));
    }
    target.ground_polygon_min_depth = float(gm.min_depth);
    target.ground_polygon_max_depth = float(gm.max_depth);
    // Collides / Unsafe / Friction are the object's own walkability metadata, not
    // geometry, and scene_refresh does not re-derive them — so they are left
    // exactly as they were. These two lines used to force collides=true /
    // unsafe=false, which silently turned a decorative or unsafe ground sheet
    // into solid walkable floor the moment you edited its outline.

    av::scene_mark_ground_mesh_dirty(m_scene, m_meshEditObject);
    return true;
}

bool RubyQuickViewport::liveMeshPreview() {
    if (!m_meshEditActive) return false;
    if (!regenerateGroundMesh()) return false;
    touch_scene(/*geometry_changed=*/true);
    return true;
}

bool RubyQuickViewport::applyMeshEdit() {
    if (!m_meshEditActive) return false;
    if (!regenerateGroundMesh()) return false;

    markDirty(true);

    m_meshEditActive = false;
    touch_scene(/*geometry_changed=*/true);
    m_meshSceneSavedValid = false;

    if (m_meshSavedCamValid) {
        m_cameraPitch = m_meshSavedCamPitch;
        m_cameraYaw = m_meshSavedCamYaw;
        m_cameraDistance = m_meshSavedCamDistance;
        m_cameraTarget = m_meshSavedCamTarget;
        m_meshSavedCamValid = false;
        emit cameraChanged();
    }

    emit meshEditActiveChanged();
    emit objectTransformChanged();
    emit meshEditVerticesChanged();
    update();
    return true;
}

// Helper: update texture name in a serialized MeshData proto message
static std::string update_mesh_data_texture(const std::string& orig_mesh_bytes, const std::string& new_tex_name) {
    if (new_tex_name.empty()) return orig_mesh_bytes;
    proto::Reader reader(orig_mesh_bytes);
    proto::Field f;
    proto::Writer w;
    bool wrote_mat = false;
    while (reader.read_field(f)) {
        if (f.field_number == 10 && f.wire_type == proto::WIRE_LEN) {
            proto::Reader mat_r(f.bytes_val);
            proto::Field mf;
            proto::Writer mat_w;
            bool wrote_tex = false;
            while (mat_r.read_field(mf)) {
                if (mf.field_number == 5 && mf.wire_type == proto::WIRE_LEN) {
                    proto::Reader tex_r(mf.bytes_val);
                    proto::Field tf;
                    proto::Writer tex_w;
                    bool wrote_name = false;
                    while (tex_r.read_field(tf)) {
                        if (tf.field_number == 1 && tf.wire_type == proto::WIRE_LEN) {
                            tex_w.write_string_field(1, new_tex_name);
                            wrote_name = true;
                        } else {
                            tex_w.write_field(tf);
                        }
                    }
                    if (!wrote_name) tex_w.write_string_field(1, new_tex_name);
                    mat_w.write_nested_field(5, tex_w);
                    wrote_tex = true;
                } else {
                    mat_w.write_field(mf);
                }
            }
            if (!wrote_tex) {
                proto::Writer tex_w;
                tex_w.write_string_field(1, new_tex_name);
                mat_w.write_nested_field(5, tex_w);
            }
            w.write_nested_field(10, mat_w);
            wrote_mat = true;
        } else {
            w.write_field(f);
        }
    }
    if (!wrote_mat) {
        proto::Writer tex_w;
        tex_w.write_string_field(1, new_tex_name);
        proto::Writer mat_w;
        mat_w.write_nested_field(5, tex_w);
        w.write_nested_field(10, mat_w);
    }
    return w.to_string();
}

// Helper: update texture name and scale in a TextureMapping (113) component payload
static std::string update_texture_mapping_component(
    const std::string& comp_raw,
    const std::string& tex_name,
    float scale)
{
    proto::Reader r(comp_raw);
    proto::Field f;
    proto::Writer w;
    while (r.read_field(f)) {
        if (f.field_number == 113 && f.wire_type == proto::WIRE_LEN) {
            proto::Reader tmr(f.bytes_val);
            proto::Field tmf;
            proto::Writer tmw;
            bool wrote_name = false;
            bool wrote_scale = false;
            while (tmr.read_field(tmf)) {
                if (tmf.field_number == 1 && tmf.wire_type == proto::WIRE_LEN) {
                    if (!tex_name.empty()) {
                        tmw.write_string_field(1, tex_name);
                        wrote_name = true;
                    } else {
                        tmw.write_field(tmf);
                    }
                } else if (tmf.field_number == 2 && tmf.wire_type == proto::WIRE_I32) {
                    if (scale > 0.0f) {
                        tmw.write_float_field(2, scale);
                        wrote_scale = true;
                    } else {
                        tmw.write_field(tmf);
                    }
                } else {
                    tmw.write_field(tmf);
                }
            }
            if (!wrote_name && !tex_name.empty()) {
                tmw.write_string_field(1, tex_name);
            }
            if (!wrote_scale && scale > 0.0f) {
                tmw.write_float_field(2, scale);
            }
            w.write_nested_field(113, tmw);
        } else {
            w.write_field(f);
        }
    }
    return w.to_string();
}

// Helper: update GroundMeshComponent (111) submesh materials
static std::string update_ground_mesh_component_textures(
    const std::string& comp_raw,
    const std::string& top_tex,
    const std::string& ground_tex)
{
    proto::Reader r(comp_raw);
    proto::Field f;
    proto::Writer w;
    while (r.read_field(f)) {
        if (f.field_number == 111 && f.wire_type == proto::WIRE_LEN) {
            proto::Reader gmr(f.bytes_val);
            proto::Field gf;
            proto::Writer gmw;
            while (gmr.read_field(gf)) {
                if (gf.field_number == 8 && gf.wire_type == proto::WIRE_LEN) {
                    // SurfaceMesh (Top)
                    std::string updated = !top_tex.empty() ? update_mesh_data_texture(gf.bytes_val, top_tex) : gf.bytes_val;
                    gmw.write_bytes_field(8, updated);
                } else if ((gf.field_number == 9 || gf.field_number == 6) && gf.wire_type == proto::WIRE_LEN) {
                    // FrontMesh or BaseMesh (Ground/Cliff)
                    std::string updated = !ground_tex.empty() ? update_mesh_data_texture(gf.bytes_val, ground_tex) : gf.bytes_val;
                    gmw.write_bytes_field(gf.field_number, updated);
                } else {
                    gmw.write_field(gf);
                }
            }
            w.write_nested_field(111, gmw);
        } else {
            w.write_field(f);
        }
    }
    return w.to_string();
}

bool RubyQuickViewport::setGroundMeshTextures(const QString& topTexture, const QString& groundTexture, float textureScale) {
    if (!m_hasScene || m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return false;
    if (!canMeshEdit()) return false;

    const int targetIdx = m_selectedObject;
    auto& target = m_scene.objects[targetIdx];

    pushUndoSnapshot();

    const std::string topTexStd = topTexture.trimmed().toStdString();
    const std::string groundTexStd = groundTexture.trimmed().toStdString();

    // 1. If currently in mesh edit mode on this object, update m_meshEditParams
    if (m_meshEditActive && m_meshEditObject == targetIdx) {
        if (!topTexStd.empty()) m_meshEditParams.top_texture = topTexStd;
        if (!groundTexStd.empty()) m_meshEditParams.bottom_texture = groundTexStd;
        if (textureScale > 0.0f) m_meshEditParams.texture_scale = textureScale;
        m_meshDirty = true;
        liveMeshPreview();
    }

    // 2. Identify surface_tm_id and front_tm_id from GroundMeshGenerator (112)
    uint32_t surface_tm_id = 0, front_tm_id = 0;
    for (const auto& comp : target.components) {
        if (comp.payload_field == 112 || comp.type_name == "GroundMeshGenerator") {
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
                    }
                }
            } catch (...) {}
        }
    }

    // 3. Update TextureMapping components (113)
    int tm_index = 0;
    for (auto& comp : target.components) {
        if (comp.payload_field == 113 || comp.type_name == "TextureMapping") {
            const uint32_t cid = static_cast<uint32_t>(comp.type_id);
            if (cid == surface_tm_id || (surface_tm_id == 0 && tm_index == 0)) {
                if (!topTexStd.empty() || textureScale > 0.0f) {
                    comp.raw_data = update_texture_mapping_component(comp.raw_data, topTexStd, textureScale);
                }
            } else if (cid == front_tm_id || (front_tm_id == 0 && tm_index == 1)) {
                if (!groundTexStd.empty() || textureScale > 0.0f) {
                    comp.raw_data = update_texture_mapping_component(comp.raw_data, groundTexStd, textureScale);
                }
            }
            tm_index++;
        }
    }

    // 4. Update GroundMeshComponent (111) submesh materials
    for (auto& comp : target.components) {
        if (comp.payload_field == 111 || comp.type_name == "GroundMesh") {
            comp.raw_data = update_ground_mesh_component_textures(comp.raw_data, topTexStd, groundTexStd);
        }
    }

    // 5. Update runtime ground_mesh_textures and ground_mesh_raw (IN-PLACE, NO GEOMETRY ALTERATION!)
    const size_t n = std::min(target.ground_mesh_textures.size(), target.ground_mesh_fields.size());
    for (size_t i = 0; i < n; ++i) {
        const int fld = target.ground_mesh_fields[i];
        if (fld == 8 && !topTexStd.empty()) {
            target.ground_mesh_textures[i] = topTexStd;
        } else if ((fld == 9 || fld == 6) && !groundTexStd.empty()) {
            target.ground_mesh_textures[i] = groundTexStd;
        }
        if (i < target.ground_mesh_raw.size()) {
            const std::string& tex = (fld == 8) ? topTexStd : groundTexStd;
            if (!tex.empty()) {
                target.ground_mesh_raw[i] = update_mesh_data_texture(target.ground_mesh_raw[i], tex);
            }
        }
    }
    // Fallback if ground_mesh_fields was empty
    if (target.ground_mesh_fields.empty() && !target.ground_mesh_textures.empty()) {
        if (target.ground_mesh_textures.size() >= 1 && !topTexStd.empty()) {
            target.ground_mesh_textures[0] = topTexStd;
        }
        if (target.ground_mesh_textures.size() >= 2 && !groundTexStd.empty()) {
            target.ground_mesh_textures[1] = groundTexStd;
        }
    }

    target.resolved_components = target.components;

    // 6. Announce scene geometry change and invalidate GPU caches
    touch_scene(/*geometry_changed=*/true);
    markDirty(true);
    emit objectTransformChanged();
    emit meshEditVerticesChanged();
    update();
    return true;
}

QString RubyQuickViewport::getGroundMeshTopTexture() const {
    if (!m_hasScene || m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return QString();
    if (m_meshEditActive && m_meshEditObject == m_selectedObject) {
        return QString::fromStdString(m_meshEditParams.top_texture);
    }
    const auto& obj = m_scene.objects[m_selectedObject];
    // Check runtime parsed submesh textures first
    for (size_t i = 0; i < obj.ground_mesh_textures.size(); ++i) {
        if (i < obj.ground_mesh_fields.size() && obj.ground_mesh_fields[i] == 8) {
            if (!obj.ground_mesh_textures[i].empty())
                return QString::fromStdString(obj.ground_mesh_textures[i]);
        }
    }
    const auto& comps = obj.components.empty() ? obj.resolved_components : obj.components;
    uint32_t surface_tm_id = 0;
    for (const auto& comp : comps) {
        if (comp.payload_field == 112 || comp.type_name == "GroundMeshGenerator") {
            try {
                proto::Reader wrapper(comp.raw_data);
                proto::Field f;
                while (wrapper.read_field(f)) {
                    if (f.field_number != 112 || f.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader ggc(f.bytes_val);
                    proto::Field g;
                    while (ggc.read_field(g)) {
                        if (g.field_number == 4) surface_tm_id = static_cast<uint32_t>(g.varint_val);
                    }
                }
            } catch (...) {}
        }
    }
    for (const auto& comp : comps) {
        if (comp.payload_field == 113 || comp.type_name == "TextureMapping") {
            if (surface_tm_id != 0 && comp.type_id == int(surface_tm_id)) {
                try {
                    proto::Reader wrapper(comp.raw_data);
                    proto::Field f;
                    while (wrapper.read_field(f)) {
                        if (f.field_number != 113 || f.wire_type != proto::WIRE_LEN) continue;
                        proto::Reader tmc(f.bytes_val);
                        proto::Field g;
                        while (tmc.read_field(g)) {
                            if (g.field_number == 1 && g.wire_type == proto::WIRE_LEN) return QString::fromStdString(g.bytes_val);
                        }
                    }
                } catch (...) {}
            }
        }
    }
    if (!obj.ground_mesh_textures.empty()) {
        return QString::fromStdString(obj.ground_mesh_textures.front());
    }
    return QStringLiteral("grass_subtle");
}

QString RubyQuickViewport::getGroundMeshFrontTexture() const {
    if (!m_hasScene || m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return QString();
    if (m_meshEditActive && m_meshEditObject == m_selectedObject) {
        return QString::fromStdString(m_meshEditParams.bottom_texture);
    }
    const auto& obj = m_scene.objects[m_selectedObject];
    // Check runtime parsed submesh textures first
    for (size_t i = 0; i < obj.ground_mesh_textures.size(); ++i) {
        if (i < obj.ground_mesh_fields.size() && (obj.ground_mesh_fields[i] == 9 || obj.ground_mesh_fields[i] == 6)) {
            if (!obj.ground_mesh_textures[i].empty())
                return QString::fromStdString(obj.ground_mesh_textures[i]);
        }
    }
    const auto& comps = obj.components.empty() ? obj.resolved_components : obj.components;
    uint32_t front_tm_id = 0;
    for (const auto& comp : comps) {
        if (comp.payload_field == 112 || comp.type_name == "GroundMeshGenerator") {
            try {
                proto::Reader wrapper(comp.raw_data);
                proto::Field f;
                while (wrapper.read_field(f)) {
                    if (f.field_number != 112 || f.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader ggc(f.bytes_val);
                    proto::Field g;
                    while (ggc.read_field(g)) {
                        if (g.field_number == 3) front_tm_id = static_cast<uint32_t>(g.varint_val);
                    }
                }
            } catch (...) {}
        }
    }
    for (const auto& comp : comps) {
        if (comp.payload_field == 113 || comp.type_name == "TextureMapping") {
            if (front_tm_id != 0 && comp.type_id == int(front_tm_id)) {
                try {
                    proto::Reader wrapper(comp.raw_data);
                    proto::Field f;
                    while (wrapper.read_field(f)) {
                        if (f.field_number != 113 || f.wire_type != proto::WIRE_LEN) continue;
                        proto::Reader tmc(f.bytes_val);
                        proto::Field g;
                        while (tmc.read_field(g)) {
                            if (g.field_number == 1 && g.wire_type == proto::WIRE_LEN) return QString::fromStdString(g.bytes_val);
                        }
                    }
                } catch (...) {}
            }
        }
    }
    if (!obj.ground_mesh_textures.empty()) {
        return QString::fromStdString(obj.ground_mesh_textures.back());
    }
    return QStringLiteral("maybegood");
}

float RubyQuickViewport::getGroundMeshTextureScale() const {
    if (!m_hasScene || m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return 250.0f;
    if (m_meshEditActive && m_meshEditObject == m_selectedObject) {
        return float(m_meshEditParams.texture_scale);
    }
    const auto& obj = m_scene.objects[m_selectedObject];
    const auto& comps = obj.components.empty() ? obj.resolved_components : obj.components;
    for (const auto& comp : comps) {
        if (comp.payload_field == 113 || comp.type_name == "TextureMapping") {
            try {
                proto::Reader wrapper(comp.raw_data);
                proto::Field f;
                while (wrapper.read_field(f)) {
                    if (f.field_number != 113 || f.wire_type != proto::WIRE_LEN) continue;
                    proto::Reader tmc(f.bytes_val);
                    proto::Field g;
                    while (tmc.read_field(g)) {
                        if (g.field_number == 2 && g.wire_type == proto::WIRE_I32) return g.float_val;
                    }
                }
            } catch (...) {}
        }
    }
    return 250.0f;
}

void RubyQuickViewport::discardMeshEdit() {
    if (!m_meshEditActive) return;
    if (m_meshSceneSavedValid) {
        m_scene = m_meshSceneSaved;
        av::scene_refresh(m_scene);
        if (m_meshEditObject >= 0 && m_meshEditObject < int(m_scene.objects.size())) {
            av::scene_mark_ground_mesh_dirty(m_scene, m_meshEditObject);
        }
        touch_scene(/*geometry_changed=*/true);
        m_meshSceneSavedValid = false;
    }

    m_meshEditActive = false;
    m_meshEditPoints.clear();
    m_meshDragging = false;
    m_meshDragPoint = -1;
    m_meshPanning = false;

    if (m_meshSavedCamValid) {
        m_cameraPitch = m_meshSavedCamPitch;
        m_cameraYaw = m_meshSavedCamYaw;
        m_cameraDistance = m_meshSavedCamDistance;
        m_cameraTarget = m_meshSavedCamTarget;
        m_meshSavedCamValid = false;
        emit cameraChanged();
    }

    emit meshEditActiveChanged();
    emit meshEditVerticesChanged();
    emit objectTransformChanged();
    update();
}

bool RubyQuickViewport::meshLocalToScreen(double lx, double ly, QPointF& out) const {
    if (m_meshEditObject < 0 || m_meshEditObject >= int(m_scene.objects.size())) return false;
    const auto& obj = m_scene.objects[m_meshEditObject];

    float objMat[16];
    calc_object_world_matrix(obj, objMat);

    float wp[3] = {
        objMat[0] * float(lx) + objMat[4] * float(ly) + objMat[12],
        objMat[1] * float(lx) + objMat[5] * float(ly) + objMat[13],
        objMat[2] * float(lx) + objMat[6] * float(ly) + objMat[14]
    };

    av::Camera cam;
    cam.yaw = m_cameraYaw;
    cam.pitch = m_cameraPitch;
    cam.distance = m_cameraDistance;
    cam.target[0] = m_cameraTarget.x();
    cam.target[1] = m_cameraTarget.y();
    cam.target[2] = m_cameraTarget.z();
    cam.fov = 45.0f;
    cam.near_plane = std::max(0.1f, m_cameraDistance * 0.001f);
    cam.far_plane = std::max(10000.0f, m_cameraDistance * 24.0f);

    ImVec2 s;
    if (!swk::world_to_screen(cam, std::max(1, int(width())), std::max(1, int(height())),
                              ImVec2(0, 0), wp, s))
        return false;

    out = QPointF(s.x, s.y);
    return true;
}

bool RubyQuickViewport::meshScreenRay(const QPointF& px, float origin[3], float dir[3]) const {
    av::Camera cam;
    cam.yaw = m_cameraYaw;
    cam.pitch = m_cameraPitch;
    cam.distance = m_cameraDistance;
    cam.target[0] = m_cameraTarget.x();
    cam.target[1] = m_cameraTarget.y();
    cam.target[2] = m_cameraTarget.z();
    cam.fov = 45.0f;
    cam.near_plane = std::max(0.1f, m_cameraDistance * 0.001f);
    cam.far_plane = std::max(10000.0f, m_cameraDistance * 24.0f);

    swk::screen_ray(cam, std::max(1, int(width())), std::max(1, int(height())),
                    ImVec2(0, 0), ImVec2(float(px.x()), float(px.y())), origin, dir);
    return true;
}

bool RubyQuickViewport::meshRayObjectPlane(const float origin[3], const float dir[3],
                                          double& lx, double& ly) const {
    if (m_meshEditObject < 0 || m_meshEditObject >= int(m_scene.objects.size())) return false;
    const auto& obj = m_scene.objects[m_meshEditObject];

    float mat[16];
    calc_object_world_matrix(obj, mat);
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

int RubyQuickViewport::meshHitTestVertex(const QPointF& px, float radiusPx) const {
    if (m_meshEditObject < 0 || m_meshEditObject >= int(m_scene.objects.size())) return -1;
    int bestIdx = -1;
    double bestDist2 = double(radiusPx * radiusPx);

    for (int i = 0; i < int(m_meshEditPoints.size()); ++i) {
        QPointF s;
        if (meshLocalToScreen(m_meshEditPoints[i].x, m_meshEditPoints[i].y, s)) {
            double dx = px.x() - s.x();
            double dy = px.y() - s.y();
            double d2 = dx * dx + dy * dy;
            if (d2 < bestDist2) {
                bestDist2 = d2;
                bestIdx = i;
            }
        }
    }
    return bestIdx;
}

int RubyQuickViewport::meshHitTestEdge(const QPointF& px, double& hitLx, double& hitLy, float radiusPx) const {
    if (m_meshEditObject < 0 || m_meshEditObject >= int(m_scene.objects.size())) return -1;
    if (m_meshEditPoints.size() < 3) return -1;

    float origin[3], dir[3];
    meshScreenRay(px, origin, dir);
    double lx = 0, ly = 0;
    if (!meshRayObjectPlane(origin, dir, lx, ly)) return -1;

    int bestEdge = -1;
    double bestDist2 = double(radiusPx * radiusPx);

    for (size_t i = 0; i < m_meshEditPoints.size(); ++i) {
        size_t j = (i + 1) % m_meshEditPoints.size();
        QPointF s1, s2;
        if (!meshLocalToScreen(m_meshEditPoints[i].x, m_meshEditPoints[i].y, s1) ||
            !meshLocalToScreen(m_meshEditPoints[j].x, m_meshEditPoints[j].y, s2))
            continue;

        double ex = s2.x() - s1.x(), ey = s2.y() - s1.y();
        double len2 = ex*ex + ey*ey;
        double t = len2 > 1e-9 ? ((px.x() - s1.x())*ex + (px.y() - s1.y())*ey) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        double qx = s1.x() + ex * t, qy = s1.y() + ey * t;
        double d2 = (px.x() - qx)*(px.x() - qx) + (px.y() - qy)*(px.y() - qy);
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestEdge = int(i);
        }
    }

    if (bestEdge >= 0) {
        size_t j = (bestEdge + 1) % m_meshEditPoints.size();
        const auto& p1 = m_meshEditPoints[bestEdge];
        const auto& p2 = m_meshEditPoints[j];
        double dx = p2.x - p1.x, dy = p2.y - p1.y;
        double len2 = dx*dx + dy*dy;
        double t = len2 > 1e-12 ? ((lx - p1.x)*dx + (ly - p1.y)*dy) / len2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        hitLx = p1.x + dx * t;
        hitLy = p1.y + dy * t;
    }
    return bestEdge;
}

QVariantList RubyQuickViewport::getMeshEditVertices() const {
    QVariantList list;
    if (!m_meshEditActive) return list;

    for (size_t i = 0; i < m_meshEditPoints.size(); ++i) {
        const auto& pt = m_meshEditPoints[i];
        QPointF sp;
        bool ok = meshLocalToScreen(pt.x, pt.y, sp);
        QVariantMap map;
        map["index"] = int(i);
        map["x"] = pt.x;
        map["y"] = pt.y;
        map["screenX"] = ok ? sp.x() : -999.0;
        map["screenY"] = ok ? sp.y() : -999.0;
        map["visible"] = ok && (sp.x() >= -50.0 && sp.x() <= width() + 50.0 &&
                               sp.y() >= -50.0 && sp.y() <= height() + 50.0);
        list.append(map);
    }
    return list;
}

void RubyQuickViewport::moveMeshVertex(int index, float worldDx, float worldDy) {
    if (!m_meshEditActive || index < 0 || index >= int(m_meshEditPoints.size())) return;
    m_meshEditPoints[index].x += worldDx;
    m_meshEditPoints[index].y += worldDy;
    m_meshDirty = true;
    liveMeshPreview();
    emit meshEditVerticesChanged();
    update();
}

void RubyQuickViewport::insertMeshVertex(int edgeIndex, float worldX, float worldY) {
    if (!m_meshEditActive || m_meshEditPoints.empty()) return;
    int i0 = (edgeIndex >= 0 && edgeIndex < int(m_meshEditPoints.size())) ? edgeIndex : 0;
    boulder::PolygonPoint pt{worldX, worldY};
    if (worldX == 0.0f && worldY == 0.0f) {
        int i1 = (i0 + 1) % int(m_meshEditPoints.size());
        pt.x = (m_meshEditPoints[i0].x + m_meshEditPoints[i1].x) * 0.5;
        pt.y = (m_meshEditPoints[i0].y + m_meshEditPoints[i1].y) * 0.5;
    }
    m_meshEditPoints.insert(m_meshEditPoints.begin() + i0 + 1, pt);

    // Per-node relief is indexed in parallel with the outline, so an insert has
    // to be mirrored or the two fall out of step — and a desync silently drops
    // the modder's terrain back to a flat slab. The new node inherits its
    // predecessor's depth, which is also what reads best: a vertex added to an
    // edge continues that edge rather than denting it.
    if (!m_meshEditFrontZ.empty() &&
        m_meshEditFrontZ.size() == m_meshEditPoints.size() - 1 &&
        m_meshEditBackZ.size() == m_meshEditPoints.size() - 1 &&
        i0 < int(m_meshEditFrontZ.size())) {
        m_meshEditFrontZ.insert(m_meshEditFrontZ.begin() + i0 + 1, m_meshEditFrontZ[i0]);
        m_meshEditBackZ.insert(m_meshEditBackZ.begin() + i0 + 1, m_meshEditBackZ[i0]);
    }

    m_selectedMeshVertex = i0 + 1;
    m_meshDirty = true;
    liveMeshPreview();
    emit meshEditVerticesChanged();
    emit selectedMeshVertexChanged();
    update();
}

void RubyQuickViewport::deleteMeshVertex(int index) {
    if (!m_meshEditActive || m_meshEditPoints.size() <= 3) return;
    if (index < 0 || index >= int(m_meshEditPoints.size())) return;
    m_meshEditPoints.erase(m_meshEditPoints.begin() + index);

    // Mirror the erase for per-node relief, for the same reason as the insert.
    if (!m_meshEditFrontZ.empty() &&
        m_meshEditFrontZ.size() == m_meshEditPoints.size() + 1 &&
        m_meshEditBackZ.size() == m_meshEditPoints.size() + 1) {
        m_meshEditFrontZ.erase(m_meshEditFrontZ.begin() + index);
        m_meshEditBackZ.erase(m_meshEditBackZ.begin() + index);
    }

    if (m_selectedMeshVertex >= int(m_meshEditPoints.size())) {
        m_selectedMeshVertex = int(m_meshEditPoints.size()) - 1;
    }
    m_meshDirty = true;
    liveMeshPreview();
    emit meshEditVerticesChanged();
    emit selectedMeshVertexChanged();
    update();
}

void RubyQuickViewport::selectMeshVertex(int index) {
    if (index >= 0 && index < int(m_meshEditPoints.size())) {
        m_selectedMeshVertex = index;
        emit selectedMeshVertexChanged();
        update();
    }
}

void RubyQuickViewport::selectNextMeshVertex() {
    if (!m_meshEditActive || m_meshEditPoints.empty()) return;
    m_selectedMeshVertex = (m_selectedMeshVertex + 1) % int(m_meshEditPoints.size());
    emit selectedMeshVertexChanged();
    update();
}

void RubyQuickViewport::selectPrevMeshVertex() {
    if (!m_meshEditActive || m_meshEditPoints.empty()) return;
    m_selectedMeshVertex = (m_selectedMeshVertex - 1 + int(m_meshEditPoints.size())) % int(m_meshEditPoints.size());
    emit selectedMeshVertexChanged();
    update();
}

void RubyQuickViewport::nudgeSelectedMeshVertex(float dx, float dy) {
    if (!m_meshEditActive || m_selectedMeshVertex < 0 || m_selectedMeshVertex >= int(m_meshEditPoints.size())) return;
    m_meshEditPoints[m_selectedMeshVertex].x += dx;
    m_meshEditPoints[m_selectedMeshVertex].y += dy;
    m_meshDirty = true;
    liveMeshPreview();
    emit meshEditVerticesChanged();
    update();
}

void RubyQuickViewport::dragSelected(float deltaX, float deltaY, float deltaZ) {
    if (m_meshEditActive) return; // Completely disabled during mesh edit mode
    if (m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return;
    if (!m_isDragTranslating) {
        pushUndoSnapshot();
        m_isDragTranslating = true;
    }

    const float yaw_rad = m_cameraYaw * (M_PI / 180.0f);
    const float cosY = std::cos(yaw_rad);
    const float sinY = std::sin(yaw_rad);

    float world_dx = (cosY * deltaX) - (sinY * deltaZ);
    float world_dy = deltaY;
    float world_dz = (-sinY * deltaX) - (cosY * deltaZ);

    auto& obj = m_scene.objects[m_selectedObject];
    obj.pos_x += world_dx;
    obj.pos_y += world_dy;
    obj.pos_z += world_dz;

    touch_scene(/*geometry_changed=*/false);
    emit objectTransformChanged();
    update();
}

void RubyQuickViewport::finishDrag() {
    m_isDragTranslating = false;
}

// ─── Gizmo stepping (touch-first) ──────────────────────────────────────────
//
// The landscape studio exposes a gizmo pad instead of relying on drag handles,
// which are unusable with a thumb over a 3D gizmo. Each tap is one step of the
// active mode. A rapid series of taps shares one undo snapshot so a nudge
// session does not blow away the 25-entry undo stack.
bool RubyQuickViewport::nudgeSelected(int axis, float steps) {
    if (m_meshEditActive || m_gizmoMode == 0) return false; // Completely disabled during mesh edit mode
    if (axis < 0 || axis > 2) return false;
    if (qFuzzyIsNull(steps)) return false;
    if (m_selectedObject < 0 || m_selectedObject >= int(m_scene.objects.size())) return false;

    constexpr qint64 kNudgeUndoCoalesceMs = 900;
    constexpr float  kMoveStep = 0.25f;                        // world units
    constexpr float  kRotateStep = float(M_PI / 12.0);          // 15 degrees
    constexpr float  kScaleStep = 0.05f;                        // 5%

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastNudgeMs == 0 || now - m_lastNudgeMs > kNudgeUndoCoalesceMs) {
        pushUndoSnapshot();
    }
    m_lastNudgeMs = now;

    auto& obj = m_scene.objects[m_selectedObject];

    switch (m_gizmoMode) {
    case 1: // Move
        if (axis == 0)      obj.pos_x += steps * kMoveStep;
        else if (axis == 1) obj.pos_y += steps * kMoveStep;
        else                obj.pos_z += steps * kMoveStep;
        break;

    case 2: // Rotate — the .scene format persists Rotation on Y (Tag 6) only;
            // X/Z are live-preview values that scene_save does not round-trip.
        if (axis == 0)      obj.rot_x += steps * kRotateStep;
        else if (axis == 1) obj.rot_y += steps * kRotateStep;
        else                obj.rot_z += steps * kRotateStep;
        break;

    case 3: { // Scale — Tag 7 stores ONE uniform Scaling float, so a nudge is
              // deliberately uniform on all three axes rather than a fake
              // non-uniform scale that would silently collapse on save.
        float next = obj.scale_x * (1.0f + steps * kScaleStep);
        next = std::max(0.01f, next);
        obj.scale_x = next;
        obj.scale_y = next;
        obj.scale_z = next;
        break;
    }

    default:
        return false;
    }

    emit objectTransformChanged();
    update();
    return true;
}

void RubyQuickViewport::setCameraPreset(const QString& name) {
    const QString n = name.toLower();
    if (n == QLatin1String("top")) {
        m_cameraPitch = 85.0f;
        m_cameraYaw = -90.0f;
        m_cameraMode = CameraMode3DEdit;
    } else if (n == QLatin1String("front") || n == QLatin1String("game")) {
        m_cameraPitch = 0.0f;
        m_cameraYaw = 0.0f;
        m_cameraMode = CameraModeGameView;
    } else if (n == QLatin1String("side")) {
        m_cameraPitch = 0.0f;
        m_cameraYaw = -90.0f;
        m_cameraMode = CameraMode3DEdit;
    } else { // "iso" and anything unknown
        m_cameraPitch = 25.0f;
        m_cameraYaw = -45.0f;
        m_cameraMode = CameraMode3DEdit;
    }
    emit cameraModeChanged();
    emit cameraChanged();
    update();
}

QVariantMap RubyQuickViewport::getSelectedObjectData() {
    QVariantMap map;
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        const auto& obj = m_scene.objects[m_selectedObject];
        map["name"] = QString::fromStdString(obj.name);
        map["template"] = QString::fromStdString(obj.template_name);
        map["posX"] = obj.pos_x;
        map["posY"] = obj.pos_y;
        map["posZ"] = obj.pos_z;
        // All three rotation values cross the QML boundary in DEGREES and are
        // stored internally in radians (that is what object_world_matrix and
        // scene_save expect). Previously only rotY was converted, so an
        // inspector that read rotX and wrote it back drifted by 57.3x.
        map["rotX"] = obj.rot_x * 180.0f / M_PI;
        map["rotY"] = obj.rot_y * 180.0f / M_PI;
        map["rotZ"] = obj.rot_z * 180.0f / M_PI;
        map["scaleX"] = obj.scale_x;
        map["scaleY"] = obj.scale_y;
        map["scaleZ"] = obj.scale_z;

        bool isGm = canMeshEdit();
        map["isGroundMesh"] = isGm;
        if (isGm) {
            map["topTexture"] = getGroundMeshTopTexture();
            map["groundTexture"] = getGroundMeshFrontTexture();
            map["textureScale"] = getGroundMeshTextureScale();
        }
    }
    return map;
}

// Persists the edited scene. av::scene_save writes to a temporary file and
// only then replaces the original, so a failure here cannot corrupt the scene
// the user opened.
bool RubyQuickViewport::saveScene() {
    const auto fail = [this](const QString& message) {
        if (m_lastError != message) {
            m_lastError = message;
            emit lastErrorChanged();
        }
        return false;
    };

    if (m_filePath.isEmpty()) {
        return fail(QStringLiteral("No scene file to save to"));
    }
    if (!m_hasScene) {
        return fail(QStringLiteral("No scene loaded"));
    }

    std::string error;
    if (!av::scene_save(m_filePath.toStdString(), m_scene, &error)) {
        return fail(QString::fromStdString(error.empty()
                                           ? std::string("scene_save failed")
                                           : error));
    }

    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
    markDirty(false);
    emit sceneSaved(m_filePath);
    return true;
}

void RubyQuickViewport::applyTransform(float px, float py, float pz, float rx, float ry, float rz, float sx, float sy, float sz) {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        pushUndoSnapshot();
        auto& obj = m_scene.objects[m_selectedObject];
        obj.pos_x = px;
        obj.pos_y = py;
        obj.pos_z = pz;
        // Degrees in, radians stored — symmetric with getSelectedObjectData().
        obj.rot_x = rx * M_PI / 180.0f;
        obj.rot_y = ry * M_PI / 180.0f;
        obj.rot_z = rz * M_PI / 180.0f;
        obj.scale_x = sx;
        obj.scale_y = sy;
        obj.scale_z = sz;
        touch_scene(/*geometry_changed=*/false);
        markDirty(true);
        emit objectTransformChanged();
        update();
    }
}

void RubyQuickViewport::setObjectName(const QString& name) {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        pushUndoSnapshot();
        m_scene.objects[m_selectedObject].name = name.toStdString();
        touch_scene(/*geometry_changed=*/false);
        markDirty(true);
        m_objectModel.updateFromScene(m_scene);
        emit objectListChanged();
        emit objectTransformChanged();
    }
}

void RubyQuickViewport::setObjectTemplate(const QString& tmpl) {
    if (m_selectedObject >= 0 && m_selectedObject < int(m_scene.objects.size())) {
        pushUndoSnapshot();
        m_scene.objects[m_selectedObject].template_name = tmpl.toStdString();
        touch_scene(/*geometry_changed=*/false);
        markDirty(true);
        m_objectModel.updateFromScene(m_scene);
        emit objectListChanged();
        emit objectTransformChanged();
    }
}

// ─── Touch & Gesture Interaction ───────────────────────────────────────────

void RubyQuickViewport::touchEvent(QTouchEvent* event) {
    const auto& points = event->points();
    if (event->type() == QEvent::TouchCancel || event->type() == QEvent::TouchEnd || points.isEmpty()) {
        if (m_meshEditActive) {
            if (m_meshDragging) {
                m_meshDragging = false;
                m_meshDragPoint = -1;
                liveMeshPreview();
                emit meshEditVerticesChanged();
                update();
            }
            m_meshPanning = false;
        }
        m_touchActive = false;
        m_touchMoved = false;
        m_lastTouchPointCount = 0;
        m_lastPinchDistance = 0.0;
        event->accept();
        return;
    }

    if (event->type() == QEvent::TouchBegin) {
        if (m_meshEditActive && points.size() == 1) {
            QPointF pos = points.first().position();
            int hitVert = meshHitTestVertex(pos, 44.0f);
            if (hitVert >= 0) {
                selectMeshVertex(hitVert);
                m_meshDragging = true;
                m_meshDragPoint = hitVert;

                float origin[3], dir[3];
                meshScreenRay(pos, origin, dir);
                double lx = 0, ly = 0;
                if (meshRayObjectPlane(origin, dir, lx, ly)) {
                    m_meshDragOffX = m_meshEditPoints[hitVert].x - lx;
                    m_meshDragOffY = m_meshEditPoints[hitVert].y - ly;
                } else {
                    m_meshDragOffX = 0.0;
                    m_meshDragOffY = 0.0;
                }
                m_touchActive = true;
                m_lastTouchPointCount = 1;
                m_lastTouchPos = pos;
                m_touchStartPos = pos;
                event->accept();
                update();
                return;
            }

            double hitLx = 0, hitLy = 0;
            int hitEdge = meshHitTestEdge(pos, hitLx, hitLy, 32.0f);
            if (hitEdge >= 0) {
                insertMeshVertex(hitEdge, float(hitLx), float(hitLy));
                m_meshDragging = true;
                m_meshDragPoint = hitEdge + 1;
                m_meshDragOffX = 0.0;
                m_meshDragOffY = 0.0;
                m_touchActive = true;
                m_lastTouchPointCount = 1;
                m_lastTouchPos = pos;
                m_touchStartPos = pos;
                event->accept();
                update();
                return;
            }

            m_meshPanning = true;
            m_meshPanStartPx = pos;
            m_meshPanStartTarget = m_cameraTarget;
            m_lastTouchPos = pos;
            m_touchStartPos = pos;
            m_touchActive = true;
            event->accept();
            return;
        }

        m_touchActive = true;
        m_touchMoved = false;
        m_touchStartPos = points.first().position();
        m_lastTouchPos = m_touchStartPos;
        m_lastTouchPointCount = int(points.size());
        m_lastPinchDistance = (points.size() >= 2) ? QLineF(points[0].position(), points[1].position()).length() : 0.0;
        event->accept();
        return;
    }

    // TouchUpdate: Detect pointer count transitions and re-anchor without jump
    if (int(points.size()) != m_lastTouchPointCount) {
        m_lastTouchPointCount = int(points.size());
        if (points.size() == 1) {
            m_lastTouchPos = points.first().position();
            m_lastPinchDistance = 0.0;
        } else if (points.size() >= 2) {
            m_lastTouchPos = (points[0].position() + points[1].position()) * 0.5;
            m_lastPinchDistance = QLineF(points[0].position(), points[1].position()).length();
        }
        event->accept();
        return;
    }

    if (points.size() == 1) {
        const auto& p = points.first();
        QPointF curPos = p.position();

        if (m_meshEditActive) {
            if (m_meshDragging && m_meshDragPoint >= 0 && m_meshDragPoint < int(m_meshEditPoints.size())) {
                float origin[3], dir[3];
                meshScreenRay(curPos, origin, dir);
                double lx = 0, ly = 0;
                if (meshRayObjectPlane(origin, dir, lx, ly)) {
                    m_meshEditPoints[m_meshDragPoint].x = lx + m_meshDragOffX;
                    m_meshEditPoints[m_meshDragPoint].y = ly + m_meshDragOffY;
                    m_meshDirty = true;
                    update();
                }
                event->accept();
                return;
            }

            if (m_meshPanning) {
                QPointF delta = curPos - m_meshPanStartPx;
                float fovYRad = 45.0f * M_PI / 180.0f;
                float visibleHeight = 2.0f * m_cameraDistance * std::tan(fovYRad * 0.5f);
                float worldPerPixel = visibleHeight / std::max(1.0f, float(height()));

                float radYaw = m_cameraYaw * M_PI / 180.0f;
                QVector3D right(std::cos(radYaw), 0.0f, -std::sin(radYaw));
                QVector3D up(0.0f, 1.0f, 0.0f);

                m_cameraTarget = m_meshPanStartTarget - right * (delta.x() * worldPerPixel) + up * (delta.y() * worldPerPixel);
                emit cameraChanged();
                emit meshEditVerticesChanged();
                update();
                event->accept();
                return;
            }
        }

        QPointF delta = curPos - m_lastTouchPos;
        float moveDist = std::hypot(delta.x(), delta.y());

        if (moveDist < 300.0f) { // reject erratic jump
            if (std::hypot(curPos.x() - m_touchStartPos.x(), curPos.y() - m_touchStartPos.y()) > 10.0f) {
                m_touchMoved = true;
            }

            if (m_cameraMode == CameraModeGameView) {
                // Game View 2.5D: 1-finger drag pans level along X and Y
                float fovYRad = 45.0f * M_PI / 180.0f;
                float visibleHeight = 2.0f * m_cameraDistance * std::tan(fovYRad * 0.5f);
                float worldPerPixel = visibleHeight / std::max(1.0f, float(height()));
                m_cameraTarget.setX(m_cameraTarget.x() - delta.x() * worldPerPixel);
                m_cameraTarget.setY(m_cameraTarget.y() + delta.y() * worldPerPixel);
            } else {
                // 3D Edit Mode: 1-finger drag orbits yaw and pitch
                m_cameraYaw += delta.x() * 0.35f;
                m_cameraPitch = std::clamp(m_cameraPitch - float(delta.y() * 0.35f), -85.0f, 85.0f);
            }
            emit cameraChanged();
            update();
        }
        m_lastTouchPos = curPos;
        event->accept();
    } else if (points.size() >= 2) {
        m_touchMoved = true;
        const auto& p1 = points[0];
        const auto& p2 = points[1];
        qreal dist = QLineF(p1.position(), p2.position()).length();
        QPointF center = (p1.position() + p2.position()) * 0.5;

        if (m_lastPinchDistance > 10.0 && dist > 10.0) {
            qreal ratio = m_lastPinchDistance / dist;
            ratio = std::clamp(ratio, 0.75, 1.35); // clamp per-frame zoom rate to prevent flying away
            m_cameraDistance = std::clamp(float(m_cameraDistance * ratio), 100.0f, 15000.0f);
        }
        m_lastPinchDistance = dist;

        QPointF delta = center - m_lastTouchPos;
        float moveDist = std::hypot(delta.x(), delta.y());
        if (moveDist < 300.0f) {
            float fovYRad = 45.0f * M_PI / 180.0f;
            float visibleHeight = 2.0f * m_cameraDistance * std::tan(fovYRad * 0.5f);
            float worldPerPixel = visibleHeight / std::max(1.0f, float(height()));

            if (m_cameraMode == CameraMode3DEdit) {
                float radYaw = m_cameraYaw * M_PI / 180.0f;
                QVector3D right(std::cos(radYaw), 0.0f, -std::sin(radYaw));
                QVector3D up(0.0f, 1.0f, 0.0f);
                m_cameraTarget -= right * (delta.x() * worldPerPixel);
                m_cameraTarget += up * (delta.y() * worldPerPixel);
            } else {
                m_cameraTarget.setX(m_cameraTarget.x() - delta.x() * worldPerPixel);
                m_cameraTarget.setY(m_cameraTarget.y() + delta.y() * worldPerPixel);
            }
        }
        m_lastTouchPos = center;
        emit cameraChanged();
        if (m_meshEditActive) emit meshEditVerticesChanged();
        update();
        event->accept();
    }
}

void RubyQuickViewport::mousePressEvent(QMouseEvent* event) {
    if (m_touchActive) {
        event->ignore();
        return;
    }

    if (m_meshEditActive) {
        QPointF pos = event->position();
        int hitVert = meshHitTestVertex(pos, 44.0f);
        if (hitVert >= 0) {
            selectMeshVertex(hitVert);
            m_meshDragging = true;
            m_meshDragPoint = hitVert;

            float origin[3], dir[3];
            meshScreenRay(pos, origin, dir);
            double lx = 0, ly = 0;
            if (meshRayObjectPlane(origin, dir, lx, ly)) {
                m_meshDragOffX = m_meshEditPoints[hitVert].x - lx;
                m_meshDragOffY = m_meshEditPoints[hitVert].y - ly;
            } else {
                m_meshDragOffX = 0.0;
                m_meshDragOffY = 0.0;
            }
            event->accept();
            update();
            return;
        }

        double hitLx = 0, hitLy = 0;
        int hitEdge = meshHitTestEdge(pos, hitLx, hitLy, 32.0f);
        if (hitEdge >= 0) {
            insertMeshVertex(hitEdge, float(hitLx), float(hitLy));
            m_meshDragging = true;
            m_meshDragPoint = hitEdge + 1;
            m_meshDragOffX = 0.0;
            m_meshDragOffY = 0.0;
            event->accept();
            update();
            return;
        }

        m_meshPanning = true;
        m_meshPanStartPx = pos;
        m_meshPanStartTarget = m_cameraTarget;
        event->accept();
        return;
    }

    m_lastTouchPos = event->position();
    m_touchStartPos = event->position();
    m_isDragging = true;
    m_touchMoved = false;
    event->accept();
}

void RubyQuickViewport::mouseMoveEvent(QMouseEvent* event) {
    if (m_touchActive) {
        event->ignore();
        return;
    }

    if (m_meshEditActive) {
        QPointF pos = event->position();
        if (m_meshDragging && m_meshDragPoint >= 0 && m_meshDragPoint < int(m_meshEditPoints.size())) {
            float origin[3], dir[3];
            meshScreenRay(pos, origin, dir);
            double lx = 0, ly = 0;
            if (meshRayObjectPlane(origin, dir, lx, ly)) {
                m_meshEditPoints[m_meshDragPoint].x = lx + m_meshDragOffX;
                m_meshEditPoints[m_meshDragPoint].y = ly + m_meshDragOffY;
                m_meshDirty = true;
                update();
            }
            event->accept();
            return;
        }

        if (m_meshPanning) {
            QPointF delta = pos - m_meshPanStartPx;
            float fovYRad = 45.0f * M_PI / 180.0f;
            float visibleHeight = 2.0f * m_cameraDistance * std::tan(fovYRad * 0.5f);
            float worldPerPixel = visibleHeight / std::max(1.0f, float(height()));

            float radYaw = m_cameraYaw * M_PI / 180.0f;
            QVector3D right(std::cos(radYaw), 0.0f, -std::sin(radYaw));
            QVector3D up(0.0f, 1.0f, 0.0f);

            m_cameraTarget = m_meshPanStartTarget - right * (delta.x() * worldPerPixel) + up * (delta.y() * worldPerPixel);
            emit cameraChanged();
            emit meshEditVerticesChanged();
            update();
            event->accept();
            return;
        }
    }

    if (m_isDragging) {
        QPointF delta = event->position() - m_lastTouchPos;
        if (std::hypot(event->position().x() - m_touchStartPos.x(), event->position().y() - m_touchStartPos.y()) > 8.0f) {
            m_touchMoved = true;
        }
        if (m_cameraMode == CameraModeGameView) {
            float fovYRad = 45.0f * M_PI / 180.0f;
            float visibleHeight = 2.0f * m_cameraDistance * std::tan(fovYRad * 0.5f);
            float worldPerPixel = visibleHeight / std::max(1.0f, float(height()));
            m_cameraTarget.setX(m_cameraTarget.x() - delta.x() * worldPerPixel);
            m_cameraTarget.setY(m_cameraTarget.y() + delta.y() * worldPerPixel);
        } else {
            m_cameraYaw += delta.x() * 0.35f;
            m_cameraPitch = std::clamp(m_cameraPitch - float(delta.y() * 0.35f), -85.0f, 85.0f);
        }
        m_lastTouchPos = event->position();
        emit cameraChanged();
        update();
        event->accept();
    }
}

void RubyQuickViewport::mouseReleaseEvent(QMouseEvent* event) {
    if (m_meshEditActive) {
        if (m_meshDragging) {
            m_meshDragging = false;
            m_meshDragPoint = -1;
            liveMeshPreview();
            update();
        }
        m_meshPanning = false;
        event->accept();
        return;
    }
    m_isDragging = false;
    event->accept();
}

} // namespace ruby::android
