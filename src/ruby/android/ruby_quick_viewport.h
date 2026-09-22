#pragma once

#include <QQuickFramebufferObject>
#include <QAbstractListModel>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <cmath>
#include <cstdint>
#include "tools/scene_loader.h"
#include "tools/pod_loader.h"
#include "tools/boulder.h"
#include "tools/boulderx.h"   // boulderx::ground_generator() — generator choice
#include "ruby/viewport/ruby_gizmo.h"
#include "ruby/viewport/ruby_picking.h"

namespace ruby::android {

class RubyQuickViewport;

struct SceneObjectItem {
    QString name;
    QString templateName;
    bool hidden = false;
};

class RubyObjectListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        TemplateRole,
        HiddenRole,
        IndexRole
    };

    explicit RubyObjectListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void updateFromScene(const av::SceneData& scene);
    void toggleHidden(int idx);

private:
    QVector<SceneObjectItem> m_objects;
};

class RubyQuickViewport : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(int gizmoMode READ gizmoMode WRITE setGizmoMode NOTIFY gizmoModeChanged)
    Q_PROPERTY(int selectedObject READ selectedObject WRITE selectObject NOTIFY selectedObjectChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY sceneLoaded)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(QObject* objectListModel READ objectListModel CONSTANT)
    /// True when the in-memory scene differs from the file on disk.
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool isModel READ isModel NOTIFY modelLoaded)
    Q_PROPERTY(int meshCount READ meshCount NOTIFY modelLoaded)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY modelLoaded)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY modelLoaded)
    Q_PROPERTY(QString modelName READ modelName NOTIFY modelLoaded)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY modelLoaded)
    Q_PROPERTY(float frame READ frame WRITE setFrame NOTIFY frameChanged)
    Q_PROPERTY(float modelRadius READ modelRadius NOTIFY modelLoaded)
    Q_PROPERTY(bool isSceneLoading READ isSceneLoading NOTIFY isSceneLoadingChanged)
    Q_PROPERTY(float loadingProgress READ loadingProgress NOTIFY loadingProgressChanged)
    Q_PROPERTY(QString loadingStatus READ loadingStatus NOTIFY loadingStatusChanged)

public:
    explicit RubyQuickViewport(QQuickItem* parent = nullptr);
    ~RubyQuickViewport() override;

    Renderer* createRenderer() const override;

    int gizmoMode() const { return m_gizmoMode; }
    void setGizmoMode(int mode);

    int selectedObject() const { return m_selectedObject; }
    int objectCount() const;
    bool canUndo() const { return !m_undoSnapshots.empty(); }
    bool canRedo() const { return !m_redoSnapshots.empty(); }
    QObject* objectListModel() { return &m_objectModel; }
    bool isDirty() const { return m_dirty; }
    QString lastError() const { return m_lastError; }
    bool isModel() const { return m_hasModel; }
    int meshCount() const { return int(m_model.meshes.size()); }
    int vertexCount() const { return m_model.total_vertices; }
    int triangleCount() const { return m_model.total_faces; }
    QString modelName() const { return m_modelName; }
    int frameCount() const { return m_model.num_frames; }
    float frame() const { return m_frame; }
    void setFrame(float f) {
        if (m_frame != f) {
            m_frame = f;
            emit frameChanged();
            update();
        }
    }
    float modelRadius() const { return m_model.radius; }
    bool isSceneLoading() const { return m_isSceneLoading; }
    float loadingProgress() const { return m_loadingProgress; }
    QString loadingStatus() const { return m_loadingStatus; }

    Q_INVOKABLE bool loadScene(const QString& path);
    Q_INVOKABLE void loadSceneAsync(const QString& path);
    Q_INVOKABLE bool loadModel(const QString& path);
    Q_INVOKABLE void resetCamera();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void selectObject(int idx);
    Q_INVOKABLE void toggleObjectVisibility(int idx);
    Q_INVOKABLE QVariantMap getSelectedObjectData();

    // ── Added for the QML port ───────────────────────────────────────────
    // NavPad (QML port of MobileDPadWidget) needs an analog camera entry
    // point; these two reproduce MobileViewportWidget::on_dpad_pan / on_dpad_dolly
    // numerically exactly so the QML pad and the legacy widget path match.
    Q_INVOKABLE void panCamera(float dx, float dy);
    Q_INVOKABLE void dollyCamera(float forward, float strafe);

    // HierarchyDrawer (QML port of MobileHierarchyDrawer) needs a snapshot it
    // can filter in QML, plus the object mutations its Focus/Copy/Delete/Add
    // buttons request. The legacy QWidget emitted those as signals that had no
    // consumer anywhere in the tree; here they actually mutate the scene.
    Q_INVOKABLE QVariantList objectsSnapshot() const;
    Q_INVOKABLE void focusObject(int idx);
    Q_INVOKABLE void addObject();
    Q_INVOKABLE void deleteObject(int idx);
    Q_INVOKABLE void duplicateObject(int idx);

    // Touch-first gizmo stepping for the landscape studio's gizmo pad. One step
    // is 0.25 units (Move), 15 degrees (Rotate) or 5% (Scale), scaled by
    // `steps`. axis: 0=X, 1=Y, 2=Z. Mode 0 (View) is a no-op.
    Q_INVOKABLE bool nudgeSelected(int axis, float steps);

    enum CameraMode {
        CameraModeGameView = 0,
        CameraMode3DEdit = 1
    };
    Q_ENUM(CameraMode)

    Q_PROPERTY(int cameraMode READ cameraMode WRITE setCameraMode NOTIFY cameraModeChanged)
    Q_PROPERTY(bool isGameView READ isGameView NOTIFY cameraModeChanged)
    Q_PROPERTY(float cameraX READ cameraX NOTIFY cameraChanged)
    Q_PROPERTY(float cameraY READ cameraY NOTIFY cameraChanged)
    Q_PROPERTY(float cameraZ READ cameraZ NOTIFY cameraChanged)
    Q_PROPERTY(float cameraPitch READ cameraPitch NOTIFY cameraChanged)
    Q_PROPERTY(float cameraYaw READ cameraYaw NOTIFY cameraChanged)
    Q_PROPERTY(float cameraDistance READ cameraDistance NOTIFY cameraChanged)
    Q_PROPERTY(float navStep READ navStep WRITE setNavStep NOTIFY navStepChanged)
    Q_PROPERTY(bool meshEditActive READ meshEditActive NOTIFY meshEditActiveChanged)
    Q_PROPERTY(bool canMeshEdit READ canMeshEdit NOTIFY selectedObjectChanged)
    Q_PROPERTY(int selectedMeshVertex READ selectedMeshVertex NOTIFY selectedMeshVertexChanged)
    Q_PROPERTY(int meshVertexCount READ meshVertexCount NOTIFY meshEditVerticesChanged)
    // Which generator actually produced the last regenerated ground mesh, and
    // why it fell back if it did. boulderx declines meshes it cannot reproduce
    // faithfully and the editor silently uses boulder instead, so without this
    // the HUD would claim "BoulderX" for output boulder actually made.
    Q_PROPERTY(QString groundGeneratorStatus READ groundGeneratorStatus
                   NOTIFY groundGeneratorStatusChanged)

    bool meshEditActive() const { return m_meshEditActive; }
    bool canMeshEdit() const;
    int selectedMeshVertex() const { return m_selectedMeshVertex; }
    int meshVertexCount() const { return int(m_meshEditPoints.size()); }
    QString groundGeneratorStatus() const { return m_groundGeneratorStatus; }
    void setGroundGeneratorStatus(const QString& status);

    Q_INVOKABLE bool beginMeshEdit();
    Q_INVOKABLE bool applyMeshEdit();
    Q_INVOKABLE void discardMeshEdit();
    Q_INVOKABLE QVariantList getMeshEditVertices() const;
    Q_INVOKABLE void moveMeshVertex(int index, float worldDx, float worldDy);
    Q_INVOKABLE void insertMeshVertex(int edgeIndex, float worldX, float worldY);
    Q_INVOKABLE void deleteMeshVertex(int index);
    Q_INVOKABLE void selectMeshVertex(int index);
    Q_INVOKABLE void selectNextMeshVertex();
    Q_INVOKABLE void selectPrevMeshVertex();
    Q_INVOKABLE void nudgeSelectedMeshVertex(float dx, float dy);
    Q_INVOKABLE bool setGroundMeshTextures(const QString& topTexture, const QString& groundTexture, float textureScale = -1.0f);

    // ── Terrain relief ──────────────────────────────────────────────────────
    // A ground sheet is drafted in 2D, so Z cannot be drawn on it: every node
    // would get the same depth and the mesh would read as a flat plate. These
    // give the outline relief inside the object's own depth range, without
    // changing its thickness. Returns false when there is no outline to work on.
    //
    // `seed` is reported back through groundReliefSeed() and makes the result
    // reproducible — re-entering a seed you liked gives that mesh back.
    Q_INVOKABLE bool randomiseGroundRelief(float frontMagnitude, float backMagnitude,
                                           int seed = -1);
    // Clears the per-node relief, returning to a uniform slab at the object's
    // own depth. This is the state an object starts in when its Z cannot be
    // recovered.
    Q_INVOKABLE void clearGroundRelief();
    Q_INVOKABLE bool hasGroundRelief() const { return !m_meshEditFrontZ.empty(); }
    Q_INVOKABLE int groundReliefSeed() const { return int(m_meshEditReliefSeed); }
    // Depth range the relief is drawn from, i.e. what the object already uses.
    Q_INVOKABLE float groundReliefFrontMax() const { return float(m_meshEditMaxDepth); }
    Q_INVOKABLE float groundReliefBackMax() const { return float(std::fabs(m_meshEditMinDepth)); }
    Q_INVOKABLE QString getGroundMeshTopTexture() const;
    Q_INVOKABLE QString getGroundMeshFrontTexture() const;
    Q_INVOKABLE float getGroundMeshTextureScale() const;

    // Continuous hybrid joystick / touch drag for selected object
    Q_INVOKABLE void dragSelected(float deltaX, float deltaY, float deltaZ);
    Q_INVOKABLE void finishDrag();

    Q_PROPERTY(float selectedX READ selectedX NOTIFY selectedObjectChanged)
    Q_PROPERTY(float selectedY READ selectedY NOTIFY selectedObjectChanged)
    Q_PROPERTY(float selectedZ READ selectedZ NOTIFY selectedObjectChanged)
    Q_PROPERTY(float selectedRot READ selectedRot NOTIFY selectedObjectChanged)
    Q_PROPERTY(float selectedScale READ selectedScale NOTIFY selectedObjectChanged)

    // Camera presets for touch: "iso" (default), "top", "front", "side".
    // Only the orbit angles change; distance and target are preserved.
    Q_INVOKABLE void setCameraPreset(const QString& name);
    Q_INVOKABLE void applyTransform(float px, float py, float pz, float rx, float ry, float rz, float sx, float sy, float sz);
    Q_INVOKABLE void setObjectName(const QString& name);
    Q_INVOKABLE void setObjectTemplate(const QString& tmpl);

    // Camera mode & direct navigation invokables
    Q_INVOKABLE void setCameraMode(int mode);
    Q_INVOKABLE void toggleCameraMode();
    Q_INVOKABLE void setGameView();
    Q_INVOKABLE void set3DEditMode();
    Q_INVOKABLE void setNavStep(float step);
    Q_INVOKABLE void cycleNavStep(int dir);
    Q_INVOKABLE void nudgeSelectedCoord(int axis, float delta);
    Q_INVOKABLE void rotateSelectedZ(float angleDeg);
    Q_INVOKABLE void placeCurrentObject();
    Q_INVOKABLE void deselectObject();
    Q_INVOKABLE void deleteCurrentObject();
    Q_INVOKABLE void frameScene();
    Q_INVOKABLE int pickObjectAt(float x, float y);
    Q_INVOKABLE QVariantMap getCameraState() const;
    Q_INVOKABLE void setCameraState(const QVariantMap& state);

    int cameraMode() const { return m_cameraMode; }
    bool isGameView() const { return m_cameraMode == CameraModeGameView; }
    float cameraX() const { return m_cameraTarget.x(); }
    float cameraY() const { return m_cameraTarget.y(); }
    float cameraZ() const { return m_cameraTarget.z(); }
    float cameraPitch() const { return m_cameraPitch; }
    float cameraYaw() const { return m_cameraYaw; }
    float cameraDistance() const { return m_cameraDistance; }
    float navStep() const { return m_navStep; }

    float selectedX() const;
    float selectedY() const;
    float selectedZ() const;
    float selectedRot() const;
    float selectedScale() const;

    /// Serialize the in-memory scene back over m_filePath via av::scene_save,
    /// which writes a temp file and then replaces the original — a failed save
    /// leaves the file on disk untouched. Returns false and fills lastError()
    /// on failure.
    Q_INVOKABLE bool saveScene();

signals:
    void cameraModeChanged();
    void cameraChanged();
    void navStepChanged();
    void gizmoModeChanged();
    void selectedObjectChanged();
    void sceneLoaded();
    void modelLoaded();
    void undoStateChanged();
    /// Emitted whenever the object list itself changes (load / add / delete /
    /// duplicate / rename) so QML outliners can re-snapshot.
    void objectListChanged();
    /// Emitted whenever the selected object's own data changes (transform,
    /// name, template, hidden) so an inspector can refresh its fields without
    /// re-snapshotting the whole list.
    void objectTransformChanged();
    void dirtyChanged();
    void lastErrorChanged();
    void sceneSaved(const QString& path);
    void frameChanged();
    void meshEditActiveChanged();
    void meshEditVerticesChanged();
    void selectedMeshVertexChanged();
    void groundGeneratorStatusChanged();
    void groundReliefChanged();
    void isSceneLoadingChanged();
    void loadingProgressChanged();
    void loadingStatusChanged();

protected:
    void touchEvent(QTouchEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    friend class RubyViewportRenderer;

    void pushUndoSnapshot();
    void markDirty(bool dirty);
    void validateAndRepairScene();
    void meshImport(int idx);

    // THE ONLY WAY to announce that m_scene changed.
    //
    // `geometry_changed` distinguishes the two kinds of edit that used to be
    // conflated:
    //   * true  — object added / removed / reordered, ground mesh geometry or its
    //             textures edited, undo / redo, a fresh scene.  The renderer must
    //             re-upload GPU buffers.
    //   * false — transform, name, visibility.  The renderer must re-read the
    //             scene, but every GPU buffer it already owns is still valid.
    //
    // Every mutation of m_scene has to call this, including transform-only ones:
    // before this existed, the per-frame unconditional copy in synchronize() was
    // the only thing that carried a nudge to the screen.
    void touch_scene(bool geometry_changed);
    bool meshLocalToScreen(double lx, double ly, QPointF& out) const;
    bool meshScreenRay(const QPointF& px, float origin[3], float dir[3]) const;
    bool meshRayObjectPlane(const float origin[3], const float dir[3], double& lx, double& ly) const;
    int meshHitTestVertex(const QPointF& px, float radiusPx = 40.0f) const;
    int meshHitTestEdge(const QPointF& px, double& hitLx, double& hitLy, float radiusPx = 30.0f) const;
    // Regenerate the edited object's ground geometry through boulder and swap
    // the result into the scene object.  Shared by the live drag preview and the
    // commit so the two CANNOT drift apart — the preview used to be a reduced
    // copy of the commit and left resolved_components, pos_z and the walkable
    // depth range stale for the whole duration of a drag.
    bool regenerateGroundMesh();
    bool liveMeshPreview();

    // ── Scene-change signalling to the render thread ─────────────────────────
    //
    // synchronize() runs on the Qt Quick render thread and the renderer keeps its
    // own copy of the scene.  Copying that unconditionally is what made every
    // scene heavy: av::SceneObject holds `std::vector<PODMesh> ground_meshes`,
    // and a PODMesh is ~10 separate heap arrays of vertex data, so one
    // assignment deep-copies the whole terrain of the level — every frame.
    //
    // So the renderer copies only when a revision it has not seen yet arrives.
    // `m_sceneGeometryRevision` is bumped only when geometry actually changed,
    // which keeps a pure transform edit from rebuilding every ground mesh on the
    // GPU (the other half of the same problem).
    //
    // Both are atomic because they are written from the GUI thread and read from
    // the render thread.
    std::atomic<uint64_t> m_sceneRevision{1};
    std::atomic<uint64_t> m_sceneGeometryRevision{1};
    bool m_meshEditActive = false;
    int m_meshEditObject = -1;
    int m_selectedMeshVertex = -1;
    QString m_groundGeneratorStatus;
    // Generator parameters recovered from the object being edited. These are
    // what the engine actually builds terrain from, and dropping any of them is
    // what made an edit both flatten the relief (noise/seed) and quietly change
    // the object's structure (mesh type).
    int      m_meshEditMeshType = 1;
    double   m_meshEditNoise = 0.0;
    uint32_t m_meshEditSeed = 1291618994u;
    double   m_meshEditTextureScale = 250.0;
    double   m_meshEditMinDepth = -45.0;
    double   m_meshEditMaxDepth = 45.0;
    // Per-node relief. Empty means "uniform slab", which is what an edit does
    // when the object's Z cannot be recovered — never a silent flatten.
    std::vector<double> m_meshEditFrontZ;
    std::vector<double> m_meshEditBackZ;
    uint32_t m_meshEditReliefSeed = 0;
    // Advances on each "randomise" so repeated taps give different terrain while
    // an explicit seed stays reproducible.
    uint32_t m_meshEditReliefCounter = 0;
    std::vector<boulder::PolygonPoint> m_meshEditPoints;
    boulder::GroundMesh m_meshEditParams;
    boulder::GroundComponentIds m_meshEditIds;
    double m_meshEditZ = 0.0;
    bool m_isDragTranslating = false;

    // Mesh edit session snapshot and camera preservation
    av::SceneData m_meshSceneSaved;
    bool m_meshSceneSavedValid = false;
    float m_meshSavedCamPitch = 0.0f;
    float m_meshSavedCamYaw = 0.0f;
    float m_meshSavedCamDistance = 800.0f;
    QVector3D m_meshSavedCamTarget{0.0f, 0.0f, 0.0f};
    bool m_meshSavedCamValid = false;
    bool m_meshDirty = false;

    // Mesh edit direct touch manipulation
    bool m_meshDragging = false;
    int m_meshDragPoint = -1;
    double m_meshDragOffX = 0.0;
    double m_meshDragOffY = 0.0;
    bool m_meshPanning = false;
    QPointF m_meshPanStartPx;
    QVector3D m_meshPanStartTarget{0.0f, 0.0f, 0.0f};
    qint64 m_lastMeshPreviewMs = 0;

    int m_gizmoMode = 0; // 0=Off/View, 1=Move, 2=Rotate, 3=Scale
    int m_selectedObject = -1;

    QString m_filePath;
    QString m_modelName;
    av::SceneData m_scene;
    bool m_hasScene = false;
    bool m_sceneLoadedFlag = false;
    bool m_dirty = false;
    QString m_lastError;
    av::PODModel m_model;
    bool m_hasModel = false;
    bool m_modelLoadedFlag = false;
    float m_frame = 0.0f;

    bool m_isSceneLoading = false;
    float m_loadingProgress = 0.0f;
    QString m_loadingStatus;
    std::atomic<uint64_t> m_sceneLoadGen{0};
    std::unordered_map<std::string, av::PODModel> m_preloadedPods;
    std::unordered_map<std::string, QImage> m_predecodedImages;

    // Snapshot Undo/Redo stack (whole-scene diffing)
    std::vector<std::vector<uint8_t>> m_undoSnapshots;
    std::vector<std::vector<uint8_t>> m_redoSnapshots;

    RubyObjectListModel m_objectModel;

    // Camera parameters
    int m_cameraMode = 0; // CameraModeGameView = 0, CameraMode3DEdit = 1
    float m_cameraPitch = 0.0f;
    float m_cameraYaw = 0.0f;
    float m_cameraDistance = 800.0f;
    QVector3D m_cameraTarget{0.0f, 0.0f, 0.0f};
    float m_navStep = 25.0f;

    // Touch interaction tracking
    QPointF m_lastTouchPos;
    QPointF m_touchStartPos;
    bool m_isDragging = false;
    bool m_touchMoved = false;
    bool m_touchActive = false;
    int m_lastTouchPointCount = 0;
    qreal m_lastPinchDistance = 0.0;

    // Timestamp of the last gizmo-pad nudge, used to coalesce a rapid series
    // of taps into a single undo snapshot.
    qint64 m_lastNudgeMs = 0;
};

} // namespace ruby::android
