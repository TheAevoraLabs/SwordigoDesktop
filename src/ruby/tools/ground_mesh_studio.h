#pragma once
// ============================================================================
// ground_mesh_studio.h — Ground Mesh Studio (Ruby GG)
//   A roomy, full-area authoring tab for Swordigo ground meshes. Instead of
//   typing raw "x,y" lines into a text box, the modder clicks vertices onto a
//   2D canvas (the Swordigo XY plane), drags them, and exports a .swdm blob
//   through the same boulder serializer the scene editor uses.
// ============================================================================

#include <QPainterPath>
#include <QPointF>
#include <QVector>
#include <QWidget>
#include <utility>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;

namespace ruby::tools {

// ─── Click-to-draw polygon canvas ──────────────────────────────────────────
// Left-click empty space appends a vertex; dragging a handle moves it;
// right-click a handle deletes it; wheel zooms; Del/Ctrl+Z act on selection.
class GroundMeshCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit GroundMeshCanvas(QWidget* parent = nullptr);

    std::vector<std::pair<float, float>> polygon() const;
    void set_polygon(const std::vector<std::pair<float, float>>& points);
    void clear();
    void undo();
    void reverse_winding();
    void fit_view();
    int vertex_count() const { return int(m_points.size()); }
    float scale() const { return m_scale; }
    QPointF center_world() const { return m_center_world; }
    // A ground mesh polygon MUST be simple (no crossing edges): boulder's
    // triangulator fans from vertex 0 and would emit garbage otherwise. This is
    // also what keeps repaints cheap (see polygon_is_simple / m_path cache).
    bool is_simple() const { return m_is_simple; }
    bool has_points() const { return !m_points.isEmpty(); }
    void set_depth_extents(float min_depth, float max_depth);
    void set_textures(const QString& top, const QString& front);
    void set_extrusion_preview(bool enabled);
    bool extrusion_preview() const { return m_preview_3d; }
    // True when appending a vertex at `world` would make the polygon
    // self-intersect (used by the editor to gate clicks / ghost previews).
    bool would_cross_at(const QPointF& world) const { return append_would_cross(world); }

signals:
    void changed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPointF to_world(const QPointF& pixel) const;
    QPointF to_pixel(const QPointF& world) const;
    int pick_vertex(const QPointF& pixel, float tolerance_px) const;
    int pick_edge(const QPointF& pixel, float tolerance_px, QPointF* out_projected = nullptr) const;
    // Rebuild the cached fill/stroke path from the current points (cheap for
    // simple polygons; the path's flattened fill is reused across repaints).
    void rebuild_path();
    // True when adding `p` after the current last vertex (closing to first)
    // would make the polygon self-intersect. O(n), used on every append.
    bool append_would_cross(const QPointF& p) const;
    // True when the given points form a simple (non-self-intersecting) polygon.
    static bool polygon_is_simple(const QVector<QPointF>& pts);

    float m_scale = 0.85f;      // pixels per world unit (Swordigo gameplay scale)
    QPointF m_center_world;     // world coordinates under the widget center
    QVector<QPointF> m_points;  // world XY, y-up
    QVector<QVector<QPointF>> m_undo;
    QVector<QPointF> m_drag_pre; // snapshot taken before a handle drag begins
    bool m_undo_pushed = false;  // true once the current edit pushed its snapshot
    int m_dragging = -1;
    bool m_hovering = false;
    int m_hover_vertex = -1;
    int m_hover_edge = -1;
    QPointF m_hover_edge_point;  // world coordinates of projected point on hovered edge
    int m_selected_vertex = -1;
    bool m_panning = false;
    QPointF m_pan_start_pixel;
    QPointF m_pan_start_center;
    QPointF m_mouse_world;
    QPainterPath m_path;         // cached polygon fill+stroke (world→pixel space)
    bool m_path_valid = false;   // m_path matches m_points
    float m_rejected_flash = 0.0f; // seconds of "crossing rejected" hint to show
    QTimer* m_flash_timer = nullptr;
    float m_min_depth = -45.0f;
    float m_max_depth = 45.0f;
    QString m_top_tex_name = "fire_grass";
    QString m_front_tex_name = "graveyard_ground";
    bool m_preview_3d = true;
    bool m_is_simple = true;
};

// ─── Full studio page: canvas + property side panel ────────────────────────
class GroundMeshStudio final : public QWidget {
    Q_OBJECT

public:
    explicit GroundMeshStudio(QWidget* parent = nullptr);

    std::vector<std::pair<float, float>> polygon() const;
    void set_scene_camera_focus(double x, double y);

signals:
    void groundMeshSaved(const QString& path);
    // User pressed "Add to Scene…": carry the fully-generated ground-mesh
    // object as binary (Scene field 1 = one Object) plus the placement the
    // panel chose, so the host can paste it into the open scene RAM.
    void groundMeshAddToScene(const QString& identifier,
                              const QByteArray& scene_bytes,
                              double pos_x, double pos_y, double depth);

private slots:
    void refresh_summary();
    void save_mesh();
    void add_to_scene();
    void load_from_text();
    void clear_canvas();
    void undo_canvas();
    // Terrain relief: the canvas is 2D, so a node's depth cannot be drawn on it.
    // Randomise gives every node its own depth inside the +/-Z fields below
    // (boulderx::randomise_node_depths — the same generator the in-scene mesh
    // editor uses), Clear puts the sheet back to a uniform slab.
    void randomise_relief();
    void clear_relief();
    // Drops per-node relief whose vertex no longer exists, so an edit to the ring
    // cannot pair a depth with the wrong node.
    void sync_relief_to_polygon();


private:
    GroundMeshCanvas* m_canvas = nullptr;
    QLabel* m_hint = nullptr;
    QLabel* m_summary = nullptr;
    QLineEdit* m_top_texture = nullptr;
    QLineEdit* m_front_texture = nullptr;
    QDoubleSpinBox* m_world_z = nullptr;   // object Depth (Z layer)
    QDoubleSpinBox* m_depth_min = nullptr;
    QDoubleSpinBox* m_depth_max = nullptr;
    QDoubleSpinBox* m_pos_x = nullptr;     // object Position.X
    QDoubleSpinBox* m_pos_y = nullptr;     // object Position.Y
    QLineEdit*      m_name = nullptr;      // object Identifier
    QPlainTextEdit* m_points_editor = nullptr;
    // Which generator this sheet is authored for. BoulderX writes format v2
    // (per-node depth); Boulder writes v1, which both generators can read.
    QComboBox*      m_generator = nullptr;
    QDoubleSpinBox* m_relief_front = nullptr;   // max +Z magnitude for relief
    QDoubleSpinBox* m_relief_back = nullptr;    // max -Z magnitude for relief
    QLabel*         m_relief_status = nullptr;
    /// Per-node relief, parallel to the canvas polygon. Empty means "uniform
    /// slab at Min/Max depth", which is what a sheet has until you sculpt it.
    QVector<double> m_front_z;
    QVector<double> m_back_z;
    uint32_t        m_relief_seed = 0;
    bool m_syncing = false;
    bool m_relief_syncing = false;
};

} // namespace ruby::tools
