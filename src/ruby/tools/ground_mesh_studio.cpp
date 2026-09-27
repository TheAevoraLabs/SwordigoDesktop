// ============================================================================
// ground_mesh_studio.cpp — Ground Mesh Studio implementation
// ============================================================================

#include "ground_mesh_studio.h"

#include "tools/boulder.h"
#include "tools/boulderx.h"   // sheet dialect + the shared relief randomiser
#include "ruby/core/project_context.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ruby::tools {

// ─── Segment helpers (simple-polygon enforcement) ───────────────────────────
namespace {
// Orientation of c relative to the directed line a→b.
int orient(const QPointF& a, const QPointF& b, const QPointF& c) {
    const double v = (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x());
    return (v > 1e-9) ? 1 : (v < -1e-9) ? -1 : 0;
}
// Proper intersection only: genuine crossings, never shared endpoints or
// collinear touches (adjacent polygon edges legitimately share vertices).
bool seg_cross(const QPointF& a, const QPointF& b, const QPointF& c, const QPointF& d) {
    const int d1 = orient(c, d, a), d2 = orient(c, d, b);
    const int d3 = orient(a, b, c), d4 = orient(a, b, d);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
} // namespace

// ─── GroundMeshCanvas ───────────────────────────────────────────────────────

GroundMeshCanvas::GroundMeshCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 260);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setStyleSheet("background:#15171d;");
    m_flash_timer = new QTimer(this);
    m_flash_timer->setInterval(90);
    connect(m_flash_timer, &QTimer::timeout, this, [this] {
        m_rejected_flash -= 0.09f;
        if (m_rejected_flash <= 0.0f) {
            m_rejected_flash = 0.0f;
            m_flash_timer->stop();
        }
        update();
    });
}

QPointF GroundMeshCanvas::to_world(const QPointF& pixel) const {
    const QPointF center = QPointF(width() / 2.0, height() / 2.0);
    const QPointF delta = pixel - center;
    return QPointF(m_center_world.x() + delta.x() / m_scale,
                   m_center_world.y() - delta.y() / m_scale); // y up
}

QPointF GroundMeshCanvas::to_pixel(const QPointF& world) const {
    const QPointF center = QPointF(width() / 2.0, height() / 2.0);
    return QPointF(center.x() + (world.x() - m_center_world.x()) * m_scale,
                   center.y() - (world.y() - m_center_world.y()) * m_scale);
}

int GroundMeshCanvas::pick_vertex(const QPointF& pixel, float tolerance_px) const {
    for (int i = m_points.size() - 1; i >= 0; --i) {
        const QPointF p = to_pixel(m_points[i]);
        const float dx = float(p.x() - pixel.x()), dy = float(p.y() - pixel.y());
        if (std::sqrt(dx * dx + dy * dy) <= tolerance_px) return i;
    }
    return -1;
}

int GroundMeshCanvas::pick_edge(const QPointF& pixel, float tolerance_px, QPointF* out_projected) const {
    const int n = m_points.size();
    if (n < 2) return -1;
    const int count = (n >= 3) ? n : 1;
    int best_edge = -1;
    float best_d = tolerance_px;
    QPointF best_proj;

    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % n;
        const QPointF a = to_pixel(m_points[i]);
        const QPointF b = to_pixel(m_points[j]);
        const float abx = float(b.x() - a.x());
        const float aby = float(b.y() - a.y());
        const float len2 = abx * abx + aby * aby;
        if (len2 < 1e-6f) continue;
        float t = ((float(pixel.x() - a.x())) * abx + (float(pixel.y() - a.y())) * aby) / len2;
        t = std::clamp(t, 0.04f, 0.96f);
        const QPointF proj(a.x() + abx * t, a.y() + aby * t);
        const float d = std::hypot(float(pixel.x() - proj.x()), float(pixel.y() - proj.y()));
        if (d < best_d) {
            best_d = d;
            best_edge = i;
            best_proj = proj;
        }
    }

    if (best_edge >= 0) {
        if (out_projected) *out_projected = to_world(best_proj);
        return best_edge;
    }
    return -1;
}

bool GroundMeshCanvas::polygon_is_simple(const QVector<QPointF>& pts) {
    const int n = pts.size();
    if (n < 4) return true;   // triangles can never self-intersect
    for (int i = 0; i < n; ++i) {
        const QPointF& a = pts[i];
        const QPointF& b = pts[(i + 1) % n];
        for (int j = i + 1; j < n; ++j) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;  // adjacent edges
            const QPointF& c = pts[j];
            const QPointF& d = pts[(j + 1) % n];
            if (seg_cross(a, b, c, d)) return false;
        }
    }
    return true;
}

void GroundMeshCanvas::set_depth_extents(float min_depth, float max_depth) {
    m_min_depth = min_depth;
    m_max_depth = max_depth;
    update();
}

void GroundMeshCanvas::set_textures(const QString& top, const QString& front) {
    m_top_tex_name = top;
    m_front_tex_name = front;
    update();
}

void GroundMeshCanvas::set_extrusion_preview(bool enabled) {
    m_preview_3d = enabled;
    update();
}

bool GroundMeshCanvas::append_would_cross(const QPointF& p) const {
    const int n = m_points.size();
    if (n < 2) return false;
    const QPointF& last = m_points.last();
    const QPointF& first = m_points.first();
    for (int i = 0; i < n; ++i) {
        const QPointF& a = m_points[i];
        const QPointF& b = m_points[(i + 1) % n];
        if (seg_cross(last, p, a, b)) return true;
        if (seg_cross(p, first, a, b)) return true;
    }
    return false;
}

void GroundMeshCanvas::rebuild_path() {
    m_path = QPainterPath();
    if (m_points.size() < 3) {
        m_path_valid = true;
        return;
    }
    QPolygonF shape;
    shape.reserve(m_points.size());
    for (const auto& p : m_points) shape << to_pixel(p);
    m_path.addPolygon(shape);
    m_path_valid = true;
}

void GroundMeshCanvas::fit_view() {
    const float avail_w = float(std::max(100, width()));
    const float avail_h = float(std::max(100, height()));

    if (!m_points.isEmpty()) {
        float min_x = float(m_points[0].x()), max_x = float(m_points[0].x());
        float min_y = float(m_points[0].y()), max_y = float(m_points[0].y());
        for (const auto& p : m_points) {
            min_x = std::min(min_x, float(p.x())); max_x = std::max(max_x, float(p.x()));
            min_y = std::min(min_y, float(p.y())); max_y = std::max(max_y, float(p.y()));
        }
        // At minimum, span across at least ~750x450 world units so small initial sketches don't over-zoom
        const float span_x = std::max(750.0f, max_x - min_x);
        const float span_y = std::max(450.0f, max_y - min_y);
        m_scale = std::clamp(std::min(avail_w / span_x, avail_h / span_y) * 0.80f,
                             0.05f, 6.0f);
        m_center_world = QPointF((min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f);
    } else {
        // Default empty view: calibrated to standard Swordigo level-geometry scale (~1100x750 world units).
        // A standard platform (e.g. 550 wide x 300 high) fits comfortably with ample room to sketch.
        m_scale = std::clamp(std::min(avail_w / 1100.0f, avail_h / 750.0f) * 0.85f,
                             0.1f, 1.5f);
        m_center_world = QPointF(0.0f, 0.0f);
    }
    update();
}

std::vector<std::pair<float, float>> GroundMeshCanvas::polygon() const {
    std::vector<std::pair<float, float>> out;
    out.reserve(size_t(m_points.size()));
    for (const auto& p : m_points) out.emplace_back(float(p.x()), float(p.y()));
    return out;
}

void GroundMeshCanvas::set_polygon(const std::vector<std::pair<float, float>>& points) {
    m_points.clear();
    for (const auto& p : points) m_points.append(QPointF(p.first, p.second));
    m_path_valid = false;
    m_is_simple = polygon_is_simple(m_points);
    fit_view();
    emit changed();
}

void GroundMeshCanvas::clear() {
    if (m_points.isEmpty()) return;
    m_undo.push_back(m_points);
    m_points.clear();
    m_path_valid = false;
    m_is_simple = true;
    m_selected_vertex = -1;
    m_dragging = -1;
    m_hover_vertex = -1;
    m_hover_edge = -1;
    m_panning = false;
    emit changed();
}

void GroundMeshCanvas::undo() {
    if (m_undo.isEmpty()) return;
    m_points = m_undo.takeLast();
    m_path_valid = false;
    m_is_simple = polygon_is_simple(m_points);
    m_selected_vertex = -1;
    m_dragging = -1;
    m_hover_vertex = -1;
    m_hover_edge = -1;
    m_panning = false;
    emit changed();
}

void GroundMeshCanvas::reverse_winding() {
    if (m_points.size() < 3) return;
    m_undo.push_back(m_points);
    std::reverse(m_points.begin(), m_points.end());
    m_path_valid = false;
    m_is_simple = polygon_is_simple(m_points);
    m_selected_vertex = -1;
    m_dragging = -1;
    m_hover_vertex = -1;
    m_hover_edge = -1;
    emit changed();
}

void GroundMeshCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor bg(0x15, 0x17, 0x1d);
    const QColor grid_major(0x26, 0x2a, 0x33);
    const QColor grid_minor(0x1d, 0x20, 0x28);
    const QColor axis_x(0x8a, 0x44, 0x4c);
    const QColor axis_y(0x3f, 0x6f, 0x94);
    const QColor fill(0x2e, 0x7d, 0x4f, 130);
    const QColor edge(0x7fd9a0);
    const QColor vertex_fill(0x35, 0xd0, 0x7a);
    const QColor vertex_edge(0xd0, 0xff, 0xe4);
    const QColor hover(0xff, 0xd7, 0x66);
    const QColor ghost(0x6f, 0x76, 0x85);

    painter.fillRect(rect(), bg);

    // Grid: adaptive major step so lines never crowd at high zoom.
    double step = 50.0;
    while (step * m_scale < 22.0) step *= 2.0;
    while (step * m_scale > 220.0) step /= 2.0;
    const QPointF tl = to_world(QPointF(0, 0));
    const QPointF br = to_world(QPointF(width(), height()));
    QPen minor(grid_minor, 1.0);
    QPen major(grid_major, 1.0);
    const double start_x = std::floor(tl.x() / step) * step;
    const double end_x = std::ceil(br.x() / step) * step;
    const double start_y = std::floor(br.y() / step) * step;
    const double end_y = std::ceil(tl.y() / step) * step;
    for (double x = start_x; x <= end_x + 0.5; x += step) {
        const bool is_axis = std::fabs(x) < step * 0.25;
        painter.setPen(is_axis ? QPen(axis_y, 1.4) : (std::fmod(x / step, 5.0) == 0.0 ? major : minor));
        painter.drawLine(to_pixel(QPointF(x, br.y())), to_pixel(QPointF(x, tl.y())));
    }
    for (double y = start_y; y <= end_y + 0.5; y += step) {
        const bool is_axis = std::fabs(y) < step * 0.25;
        painter.setPen(is_axis ? QPen(axis_x, 1.4) : (std::fmod(y / step, 5.0) == 0.0 ? major : minor));
        painter.drawLine(to_pixel(QPointF(tl.x(), y)), to_pixel(QPointF(br.x(), y)));
    }

    const int n = m_points.size();
    QVector<QPointF> front_px;
    front_px.reserve(n);
    for (const auto& p : m_points) front_px.push_back(to_pixel(p));

    if (n >= 3) {
        if (m_is_simple && m_preview_3d) {
            // ── 2.5D Extruded 3D Ground Mesh Volume Preview ──
            const float depth_span = std::clamp(m_max_depth - m_min_depth, 10.0f, 400.0f);
            const float rad = 30.0f * (3.14159265f / 180.0f);
            const float foreshorten = 0.35f;
            const QPointF z_step(std::cos(rad) * depth_span * foreshorten * m_scale,
                                 -std::sin(rad) * depth_span * foreshorten * m_scale);

            QVector<QPointF> back_px;
            back_px.reserve(n);
            for (const auto& fp : front_px) back_px.push_back(fp + z_step);

            // Compute polygon winding (signed area in y-up world space)
            double signed_area = 0.0;
            for (int i = 0; i < n; ++i) {
                const QPointF& p1 = m_points[i];
                const QPointF& p2 = m_points[(i + 1) % n];
                signed_area += (p2.x() - p1.x()) * (p2.y() + p1.y());
            }
            const bool is_ccw = (signed_area < 0.0);

            // 1. Back face polygon (shadow silhouette in the distance)
            painter.setPen(QPen(QColor(0x1e, 0x32, 0x24, 110), 1.0, Qt::DashLine));
            painter.setBrush(QColor(0x12, 0x1a, 0x16, 175));
            painter.drawPolygon(back_px.data(), back_px.size());

            // 2. Extruded side / top quads connecting front to back
            for (int i = 0; i < n; ++i) {
                const int nxt = (i + 1) % n;
                const double ex = front_px[nxt].x() - front_px[i].x();
                const double ey = front_px[nxt].y() - front_px[i].y();
                const double cross = ex * z_step.y() - ey * z_step.x();

                // Check visible in oblique projection (cross < 0 in screen coords)
                if (cross < 0.0) {
                    const double wx = m_points[nxt].x() - m_points[i].x();
                    const double wy = m_points[nxt].y() - m_points[i].y();
                    const double wlen = std::hypot(wx, wy);
                    const double ny = (wlen > 1e-4) ? ((is_ccw ? wx : -wx) / wlen) : 0.0;

                    QPolygonF quad;
                    quad << front_px[i] << front_px[nxt] << back_px[nxt] << back_px[i];

                    if (ny > 0.15) {
                        // Top Walking Surface (Grass)
                        painter.setPen(QPen(QColor(0x52, 0xc4, 0x84), 1.4));
                        painter.setBrush(QColor(0x2f, 0x8a, 0x52, 230));
                        painter.drawPolygon(quad);
                        // Top crest ridge line
                        painter.setPen(QPen(QColor(0x7e, 0xfa, 0xa5), 2.0));
                        painter.drawLine(back_px[i], back_px[nxt]);
                    } else {
                        // Extruded Rock / Bedrock Wall
                        painter.setPen(QPen(QColor(0x38, 0x46, 0x58, 200), 1.0));
                        painter.setBrush(QColor(0x22, 0x2b, 0x36, 235));
                        painter.drawPolygon(quad);
                    }
                }
            }

            // 3. Front face polygon (main editable plane)
            painter.setPen(QPen(edge, 2.0));
            painter.setBrush(fill);
            painter.drawPolygon(front_px.data(), front_px.size());

            // 4. Highlight walkable top edges on the front face
            for (int i = 0; i < n; ++i) {
                const int nxt = (i + 1) % n;
                const double wx = m_points[nxt].x() - m_points[i].x();
                const double wy = m_points[nxt].y() - m_points[i].y();
                const double wlen = std::hypot(wx, wy);
                const double ny = (wlen > 1e-4) ? ((is_ccw ? wx : -wx) / wlen) : 0.0;
                if (ny > 0.15) {
                    painter.setPen(QPen(QColor(0x82, 0xf7, 0xa8), 3.0));
                    painter.drawLine(front_px[i], front_px[nxt]);
                }
            }

            // Top-right HUD Pill
            const QString hud_text = QString("3D Mesh Preview · Depth: [%1 .. %2] (span %3) · %4 / %5")
                .arg(m_min_depth, 0, 'f', 0)
                .arg(m_max_depth, 0, 'f', 0)
                .arg(depth_span, 0, 'f', 0)
                .arg(m_top_tex_name.isEmpty() ? "fire_grass" : m_top_tex_name)
                .arg(m_front_tex_name.isEmpty() ? "graveyard_ground" : m_front_tex_name);
            const QFontMetrics fm(painter.font());
            const int badge_w = fm.horizontalAdvance(hud_text) + 20;
            const QRectF badge_rect(width() - badge_w - 12, 10, badge_w, 24);
            painter.setPen(QPen(QColor(0x3e, 0x48, 0x5a), 1.0));
            painter.setBrush(QColor(0x18, 0x1c, 0x24, 220));
            painter.drawRoundedRect(badge_rect, 4.0, 4.0);
            painter.setPen(QColor(0x61, 0xaf, 0xef));
            painter.drawText(badge_rect, Qt::AlignCenter, hud_text);

        } else if (m_is_simple) {
            // 2D Preview fallback
            painter.setPen(QPen(edge, 2.0));
            painter.setBrush(fill);
            painter.drawPolygon(front_px.data(), front_px.size());
        } else {
            // Self-intersecting polygon: warning stroke
            painter.setPen(QPen(QColor(0xff, 0x66, 0x6b), 2.0, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(front_px.data(), front_px.size());
            painter.setPen(QPen(QColor(0xff, 0x66, 0x6b), 1.0));
            painter.drawText(QPointF(12, height() - 10),
                             "Polygon is self-intersecting — move vertices or undo to make it simple.");
        }
    } else if (!m_points.isEmpty()) {
        // Incomplete polygon: draw the chain + ghost segment to the cursor.
        painter.setPen(QPen(ghost, 1.6, Qt::DashLine));
        for (int i = 1; i < n; ++i)
            painter.drawLine(front_px[i - 1], front_px[i]);
        if (m_hovering) painter.drawLine(front_px.last(), to_pixel(m_mouse_world));
    }

    // Edge hover / middle node insert preview (between two existing nodes)
    if (m_hover_edge >= 0 && m_hover_vertex < 0 && n >= 2 && m_dragging < 0) {
        const int i = m_hover_edge;
        const int j = (i + 1) % n;
        const QPointF ea = front_px[i];
        const QPointF eb = front_px[j];
        const QPointF mid = to_pixel(m_hover_edge_point);

        painter.setPen(QPen(QColor(0xff, 0xd7, 0x66, 220), 3.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(ea, eb);

        // Draw diamond handle at the insertion point
        QPolygonF diamond;
        diamond << QPointF(mid.x(), mid.y() - 6.5)
                << QPointF(mid.x() + 6.5, mid.y())
                << QPointF(mid.x(), mid.y() + 6.5)
                << QPointF(mid.x() - 6.5, mid.y());
        painter.setPen(QPen(QColor(0x15, 0x17, 0x1d), 1.4));
        painter.setBrush(QColor(0xff, 0xd7, 0x66));
        painter.drawPolygon(diamond);

        const QString badge = QString("+ Insert Node (%1, %2)")
            .arg(m_hover_edge_point.x(), 0, 'f', 1)
            .arg(m_hover_edge_point.y(), 0, 'f', 1);
        const QFontMetrics fm(painter.font());
        const int bw = fm.horizontalAdvance(badge) + 12;
        const QRectF badge_r(mid.x() + 10.0, mid.y() - 20.0, bw, 18.0);
        painter.setPen(QPen(QColor(0x3e, 0x48, 0x5a), 1.0));
        painter.setBrush(QColor(0x18, 0x1c, 0x24, 230));
        painter.drawRoundedRect(badge_r, 3.0, 3.0);
        painter.setPen(QColor(0xff, 0xd7, 0x66));
        painter.drawText(badge_r, Qt::AlignCenter, badge);
    }

    // Vertex handles on front face
    for (int i = 0; i < n; ++i) {
        const QPointF p = front_px[i];
        const bool is_hover = (i == m_hover_vertex) || (i == m_dragging) || (i == m_selected_vertex);
        painter.setPen(QPen(is_hover ? hover : vertex_edge, 1.6));
        painter.setBrush(is_hover ? hover : vertex_fill);
        painter.drawEllipse(p, 5.0, 5.0);
        if (i == m_selected_vertex) {
            painter.setPen(QPen(QColor(0xff, 0xff, 0xff), 1.2, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(p, 8.5, 8.5);
        }
        painter.setPen(QPen(QColor(0x9a, 0xa3, 0xb2), 1.0));
        painter.drawText(QPointF(p.x() + 9.0, p.y() - 7.0), QString::number(i + 1));

        if (is_hover) {
            const QString coord = QString("%1, %2")
                .arg(m_points[i].x(), 0, 'f', 1)
                .arg(m_points[i].y(), 0, 'f', 1);
            painter.setPen(QPen(QColor(0xff, 0xd7, 0x66), 1.0));
            painter.drawText(QPointF(p.x() + 9.0, p.y() + 15.0), coord);
        }
    }

    painter.setPen(QColor(0x8a, 0x92, 0xa4));
    painter.drawText(QPointF(12, 20), m_points.isEmpty()
        ? "Click to place vertices · wheel zooms · right-drag pans"
        : QString("%1 vertices · Click edge to insert node · Drag to move · Del to delete · Right-drag pans").arg(n));

    // Transient "crossing rejected" feedback after a blocked click/drag.
    if (m_rejected_flash > 0.0f && n >= 2) {
        painter.setPen(QPen(QColor(0xff, 0x8f, 0x92), 1.4));
        painter.drawText(QPointF(12, 38),
                         "Can't place a vertex there — it would cross an existing edge.");
    }

    // Scale reference bar in bottom-left corner (indicates Swordigo world units & Hero proportion)
    const double bar_units = (100.0 * m_scale < 35.0) ? 500.0 : (100.0 * m_scale > 160.0 ? 50.0 : 100.0);
    const float bar_px = float(bar_units * m_scale);
    const float bar_x = 14.0f;
    const float bar_y = height() - 14.0f;
    painter.setPen(QPen(QColor(0x8a, 0x93, 0xa5), 1.4));
    painter.drawLine(QPointF(bar_x, bar_y), QPointF(bar_x + bar_px, bar_y));
    painter.drawLine(QPointF(bar_x, bar_y - 4.0), QPointF(bar_x, bar_y + 4.0));
    painter.drawLine(QPointF(bar_x + bar_px, bar_y - 4.0), QPointF(bar_x + bar_px, bar_y + 4.0));
    painter.setPen(QColor(0x8a, 0x93, 0xa5));
    painter.drawText(QPointF(bar_x + 6.0, bar_y - 5.0),
                     QString("%1 units (Hero height ≈ 45)").arg(int(bar_units)));
}

void GroundMeshCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus();
    const QPointF pixel = event->position();
    const int handle = pick_vertex(pixel, 10.0f);

    if (event->button() == Qt::LeftButton) {
        if (handle >= 0) {
            m_drag_pre = m_points;
            m_undo_pushed = false;
            m_dragging = handle;
            m_selected_vertex = handle;
            update();
            return;
        }

        // 1) Click on or near an edge: insert a node between existing vertices
        QPointF proj_world;
        const int edge = (m_points.size() >= 2) ? pick_edge(pixel, 14.0f, &proj_world) : -1;
        if (edge >= 0) {
            const int insert_at = edge + 1;
            m_undo.push_back(m_points);
            m_undo_pushed = true;
            m_points.insert(m_points.begin() + insert_at, proj_world);
            m_path_valid = false;
            m_is_simple = polygon_is_simple(m_points);
            m_dragging = insert_at;
            m_drag_pre = m_points;
            m_selected_vertex = insert_at;
            emit changed();
            update();
            return;
        }

        // 2) Click in empty space
        const QPointF world = to_world(pixel);
        const int n = m_points.size();
        if (n < 3) {
            if (append_would_cross(world)) {
                m_rejected_flash = 0.6f;
                if (m_flash_timer) m_flash_timer->start();
                update();
                return;
            }
            m_undo.push_back(m_points);
            m_undo_pushed = true;
            m_points.append(world);
            m_path_valid = false;
            m_is_simple = polygon_is_simple(m_points);
            m_dragging = m_points.size() - 1;
            m_drag_pre = m_points;
            m_selected_vertex = m_dragging;
            emit changed();
            update();
            return;
        }

        // 3) Closed polygon (n >= 3):
        // Insert into the closest edge so the polygon outline stays consistent without tangling
        double best_d2 = 1e30;
        int insert_at = n;
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            const double ax = m_points[i].x(), ay = m_points[i].y();
            const double bx = m_points[j].x(), by = m_points[j].y();
            const double dx = bx - ax, dy = by - ay;
            const double len2 = dx * dx + dy * dy;
            double t = len2 > 1e-12 ? ((world.x() - ax) * dx + (world.y() - ay) * dy) / len2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double ex = ax + dx * t - world.x();
            const double ey = ay + dy * t - world.y();
            const double d2 = ex * ex + ey * ey;
            if (d2 < best_d2) { best_d2 = d2; insert_at = i + 1; }
        }

        QVector<QPointF> candidate = m_points;
        candidate.insert(candidate.begin() + insert_at, world);
        if (!polygon_is_simple(candidate)) {
            m_rejected_flash = 0.6f;
            if (m_flash_timer) m_flash_timer->start();
            update();
            return;
        }

        m_undo.push_back(m_points);
        m_undo_pushed = true;
        m_points.insert(m_points.begin() + insert_at, world);
        m_path_valid = false;
        m_is_simple = true;
        m_dragging = insert_at;
        m_drag_pre = m_points;
        m_selected_vertex = insert_at;
        emit changed();
        update();

    } else if (event->button() == Qt::RightButton) {
        if (handle >= 0) {
            // Right-click directly on a vertex handle: delete this node
            m_undo.push_back(m_points);
            m_points.removeAt(handle);
            m_path_valid = false;
            m_is_simple = polygon_is_simple(m_points);
            if (m_selected_vertex == handle) m_selected_vertex = -1;
            else if (m_selected_vertex > handle) --m_selected_vertex;
            emit changed();
            update();
        } else {
            // Right-click on empty space: pan canvas (NEVER delete points!)
            m_panning = true;
            m_pan_start_pixel = pixel;
            m_pan_start_center = m_center_world;
            setCursor(Qt::ClosedHandCursor);
        }
    } else if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_pan_start_pixel = pixel;
        m_pan_start_center = m_center_world;
        setCursor(Qt::ClosedHandCursor);
    }
}

void GroundMeshCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    mousePressEvent(event);
}

void GroundMeshCanvas::mouseMoveEvent(QMouseEvent* event) {
    m_hovering = true;
    const QPointF pixel = event->position();
    m_mouse_world = to_world(pixel);

    if (m_panning) {
        const QPointF delta = pixel - m_pan_start_pixel;
        m_center_world = QPointF(m_pan_start_center.x() - delta.x() / m_scale,
                                 m_pan_start_center.y() + delta.y() / m_scale);
        update();
        return;
    }

    if (m_dragging >= 0 && m_dragging < m_points.size()) {
        const QPointF before = m_points[m_dragging];
        QPointF world = to_world(pixel);
        if (event->modifiers() & Qt::ShiftModifier) {
            world = QPointF(std::round(world.x() / 50.0) * 50.0,
                            std::round(world.y() / 50.0) * 50.0);
        }
        QVector<QPointF> candidate = m_points;
        candidate[m_dragging] = world;
        if (polygon_is_simple(candidate)) {
            m_points[m_dragging] = world;
            m_path_valid = false;
            m_is_simple = true;
            m_rejected_flash = 0.0f;
        } else if (m_points[m_dragging] != before) {
            m_rejected_flash = 0.35f;
            if (m_flash_timer) m_flash_timer->start();
        }
    } else {
        m_hover_vertex = pick_vertex(pixel, 10.0f);
        m_hover_edge = (m_hover_vertex < 0 && m_points.size() >= 2)
            ? pick_edge(pixel, 14.0f, &m_hover_edge_point)
            : -1;

        if (m_hover_vertex >= 0) {
            setCursor(Qt::SizeAllCursor);
        } else if (m_hover_edge >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::CrossCursor);
        }
    }
    update();
}

void GroundMeshCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_dragging >= 0) {
        if (!m_undo_pushed && m_dragging < m_drag_pre.size() &&
            m_points[m_dragging] != m_drag_pre[m_dragging]) {
            m_undo.push_back(m_drag_pre);
        }
        m_dragging = -1;
        m_rejected_flash = 0.0f;
        m_is_simple = polygon_is_simple(m_points);
        emit changed();
        update();
    }
    if (m_panning && (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)) {
        m_panning = false;
        unsetCursor();
        update();
    }
}

void GroundMeshCanvas::wheelEvent(QWheelEvent* event) {
    const QPointF anchor_world = to_world(event->position());
    const double factor = std::pow(1.0015, event->angleDelta().y());
    m_scale = std::clamp(float(m_scale * factor), 0.05f, 15.0f);
    // Keep the world point under the cursor stationary while zooming.
    const QPointF anchor_pixel = event->position();
    const QPointF center = QPointF(width() / 2.0, height() / 2.0);
    const QPointF delta = anchor_pixel - center;
    m_center_world = QPointF(anchor_world.x() - delta.x() / m_scale,
                             anchor_world.y() + delta.y() / m_scale);
    update();
}

void GroundMeshCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_selected_vertex >= 0 && m_selected_vertex < m_points.size()) {
            m_undo.push_back(m_points);
            m_points.removeAt(m_selected_vertex);
            m_selected_vertex = -1;
            m_path_valid = false;
            m_is_simple = polygon_is_simple(m_points);
            emit changed();
            update();
            event->accept();
            return;
        } else if (!m_points.isEmpty()) {
            m_undo.push_back(m_points);
            m_points.removeLast();
            m_path_valid = false;
            m_is_simple = polygon_is_simple(m_points);
            emit changed();
            update();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier)) {
        undo(); event->accept(); return;
    }
    if (event->key() == Qt::Key_F) { fit_view(); event->accept(); return; }
    QWidget::keyPressEvent(event);
}

// ─── GroundMeshStudio page ─────────────────────────────────────────────────

GroundMeshStudio::GroundMeshStudio(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_hint = new QLabel("Draw a walkable ground polygon on the Swordigo XY plane, then export a .swdm "
                        "blob with the same serializer the scene editor uses.", this);
    m_hint->setWordWrap(true);
    root->addWidget(m_hint);

    auto* body = new QHBoxLayout();
    body->setSpacing(10);

    // Canvas area (stretches).
    m_canvas = new GroundMeshCanvas(this);
    body->addWidget(m_canvas, 1);

    // Property sidebar.
    auto* sidebar = new QScrollArea(this);
    sidebar->setWidgetResizable(true);
    sidebar->setMinimumWidth(300);
    sidebar->setMaximumWidth(380);
    auto* panel = new QWidget(sidebar);
    auto* form = new QVBoxLayout(panel);
    form->setContentsMargins(6, 6, 6, 6);
    form->setSpacing(8);

    auto* mesh_box = new QGroupBox("Ground Mesh Properties", panel);
    auto* mesh_form = new QFormLayout(mesh_box);
    m_name = new QLineEdit("ground_mesh", mesh_box);
    m_name->setPlaceholderText("object identifier, e.g. ground_01");
    m_top_texture = new QLineEdit("fire_grass", mesh_box);
    m_top_texture->setPlaceholderText("PVR stem, e.g. fire_grass");
    m_front_texture = new QLineEdit("graveyard_ground", mesh_box);
    m_front_texture->setPlaceholderText("PVR stem, e.g. graveyard_ground");
    m_pos_x = new QDoubleSpinBox(mesh_box);
    m_pos_x->setRange(-1000000.0, 1000000.0); m_pos_x->setValue(0.0); m_pos_x->setDecimals(1);
    m_pos_y = new QDoubleSpinBox(mesh_box);
    m_pos_y->setRange(-1000000.0, 1000000.0); m_pos_y->setValue(0.0); m_pos_y->setDecimals(1);
    m_world_z = new QDoubleSpinBox(mesh_box);
    m_world_z->setRange(-100000.0, 100000.0); m_world_z->setValue(0.0); m_world_z->setDecimals(1);
    m_depth_min = new QDoubleSpinBox(mesh_box);
    m_depth_min->setRange(-100000.0, 100000.0); m_depth_min->setValue(-45.0); m_depth_min->setDecimals(1);
    m_depth_max = new QDoubleSpinBox(mesh_box);
    m_depth_max->setRange(-100000.0, 100000.0); m_depth_max->setValue(45.0); m_depth_max->setDecimals(1);

    // Which generator rebuilds this sheet's mesh (and, when BoulderX, which
    // dialect a save writes). Same two-value choice as the mobile settings row;
    // both go through boulderx::set_ground_generator(), so this is the desktop
    // half of one setting rather than a second, divergent one.
    m_generator = new QComboBox(mesh_box);
    m_generator->addItem("Boulder (v1 sheet)", "boulder");
    m_generator->addItem("Zenith (engine-faithful, v2 sheet)", "boulderx");
    m_generator->setCurrentIndex(
        boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX ? 1 : 0);

    // Terrain relief. The canvas is 2D, so Z cannot be drawn: these two fields
    // are the range the randomiser shapes per-node depth inside.
    m_relief_front = new QDoubleSpinBox(mesh_box);
    m_relief_front->setRange(0.0, 100000.0); m_relief_front->setValue(45.0);
    m_relief_front->setDecimals(1);
    m_relief_back = new QDoubleSpinBox(mesh_box);
    m_relief_back->setRange(0.0, 100000.0); m_relief_back->setValue(45.0);
    m_relief_back->setDecimals(1);
    auto* relief_btn = new QPushButton("Randomise Z", mesh_box);
    auto* relief_clear = new QPushButton("Flatten Z", mesh_box);
    auto* relief_row = new QWidget(mesh_box);
    auto* relief_layout = new QHBoxLayout(relief_row);
    relief_layout->setContentsMargins(0, 0, 0, 0);
    relief_layout->addWidget(relief_btn); relief_layout->addWidget(relief_clear);
    m_relief_status = new QLabel("Flat slab — no per-node Z yet.", mesh_box);
    m_relief_status->setWordWrap(true);

    // #will be unlocked after — per-node Z is staged off for this release
    // (boulderx::kPerNodeReliefUnlocked). The controls stay visible and
    // labelled so the feature is discoverable, but disabled so no press can
    // reshape a sheet behind the flat-Z contract. "Flatten Z" stays live: it
    // only ever moves a sheet TOWARDS the flat slab.
    if (!boulderx::kPerNodeReliefUnlocked) {
        const QString staged = QStringLiteral("#will be unlocked after");
        relief_btn->setText(QStringLiteral("Randomise Z — %1").arg(staged));
        relief_btn->setEnabled(false);
        relief_btn->setToolTip(QStringLiteral(
            "Per-node Z sculpting is staged off for this release — %1.")
            .arg(staged));
        m_relief_front->setEnabled(false);
        m_relief_back->setEnabled(false);
        m_relief_front->setToolTip(QStringLiteral(
            "Range for Randomise Z, staged off for this release — %1.").arg(staged));
        m_relief_back->setToolTip(QStringLiteral(
            "Range for Randomise Z, staged off for this release — %1.").arg(staged));
    }

    mesh_form->addRow("Generator", m_generator);
    mesh_form->addRow("Object name", m_name);
    mesh_form->addRow("Top texture", m_top_texture);
    mesh_form->addRow("Front texture", m_front_texture);
    mesh_form->addRow("Object X", m_pos_x);
    mesh_form->addRow("Object Y", m_pos_y);
    mesh_form->addRow("Object Depth (Z)", m_world_z);
    mesh_form->addRow("Min depth", m_depth_min);
    mesh_form->addRow("Max depth", m_depth_max);
    mesh_form->addRow("Randomise +Z", m_relief_front);
    mesh_form->addRow("Randomise -Z", m_relief_back);
    mesh_form->addRow(relief_row);
    mesh_form->addRow(m_relief_status);
    form->addWidget(mesh_box);

    auto* edit_box = new QGroupBox("Polygon", panel);
    auto* edit_form = new QFormLayout(edit_box);
    m_summary = new QLabel("No vertices yet.", panel);
    m_summary->setWordWrap(true);
    auto* preview_check = new QCheckBox("3D Extruded Preview", edit_box);
    preview_check->setChecked(true);
    auto* new_btn = new QPushButton("New / Clear", edit_box);
    auto* undo_btn = new QPushButton("Undo", edit_box);
    auto* reverse_btn = new QPushButton("Reverse Winding", edit_box);
    auto* fit_btn = new QPushButton("Fit View", edit_box);
    auto* row1 = new QWidget(edit_box);
    auto* row1l = new QHBoxLayout(row1); row1l->setContentsMargins(0, 0, 0, 0);
    row1l->addWidget(new_btn); row1l->addWidget(undo_btn);
    auto* row2 = new QWidget(edit_box);
    auto* row2l = new QHBoxLayout(row2); row2l->setContentsMargins(0, 0, 0, 0);
    row2l->addWidget(reverse_btn); row2l->addWidget(fit_btn);
    edit_form->addRow(m_summary);
    edit_form->addRow(preview_check);
    edit_form->addRow(row1);
    edit_form->addRow(row2);
    form->addWidget(edit_box);

    auto* text_box = new QGroupBox("Vertex List (editable)", panel);
    auto* text_layout = new QVBoxLayout(text_box);
    m_points_editor = new QPlainTextEdit(text_box);
    m_points_editor->setPlaceholderText("x,y per line — e.g.\n0,0\n800,0\n800,-400");
    m_points_editor->setMaximumHeight(130);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_points_editor->setFont(mono);
    text_layout->addWidget(m_points_editor);
    form->addWidget(text_box);

    auto* save_btn = new QPushButton("Save Ground Mesh (.swdm / .gmesh)", panel);
    save_btn->setObjectName("brandButton");
    save_btn->setMinimumHeight(36);
    form->addWidget(save_btn);
    auto* add_btn = new QPushButton("Add to Scene…", panel);
    add_btn->setObjectName("brandButton");
    add_btn->setMinimumHeight(36);
    form->addWidget(add_btn);
    form->addStretch();
    sidebar->setWidget(panel);
    body->addWidget(sidebar);

    root->addLayout(body, 1);

    m_canvas->set_depth_extents(float(m_depth_min->value()), float(m_depth_max->value()));
    m_canvas->set_textures(m_top_texture->text().trimmed(), m_front_texture->text().trimmed());

    connect(m_canvas, &GroundMeshCanvas::changed, this, &GroundMeshStudio::refresh_summary);
    connect(preview_check, &QCheckBox::toggled, m_canvas, &GroundMeshCanvas::set_extrusion_preview);
    connect(m_depth_min, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        m_canvas->set_depth_extents(float(m_depth_min->value()), float(m_depth_max->value()));
    });
    connect(m_depth_max, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        m_canvas->set_depth_extents(float(m_depth_min->value()), float(m_depth_max->value()));
    });
    connect(m_top_texture, &QLineEdit::textChanged, this, [this](const QString&) {
        m_canvas->set_textures(m_top_texture->text().trimmed(), m_front_texture->text().trimmed());
    });
    connect(m_front_texture, &QLineEdit::textChanged, this, [this](const QString&) {
        m_canvas->set_textures(m_top_texture->text().trimmed(), m_front_texture->text().trimmed());
    });
    connect(new_btn, &QPushButton::clicked, this, &GroundMeshStudio::clear_canvas);
    connect(undo_btn, &QPushButton::clicked, this, &GroundMeshStudio::undo_canvas);
    connect(reverse_btn, &QPushButton::clicked, m_canvas, &GroundMeshCanvas::reverse_winding);
    connect(fit_btn, &QPushButton::clicked, m_canvas, [this] { m_canvas->fit_view(); });
    connect(save_btn, &QPushButton::clicked, this, &GroundMeshStudio::save_mesh);
    connect(add_btn, &QPushButton::clicked, this, &GroundMeshStudio::add_to_scene);
    connect(m_points_editor, &QPlainTextEdit::textChanged, this, &GroundMeshStudio::load_from_text);
    connect(relief_btn, &QPushButton::clicked, this, &GroundMeshStudio::randomise_relief);
    connect(relief_clear, &QPushButton::clicked, this, &GroundMeshStudio::clear_relief);
    connect(m_generator, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        boulderx::set_ground_generator(index == 1 ? boulderx::GroundGenerator::BoulderX
                                                  : boulderx::GroundGenerator::Boulder);
        sync_relief_to_polygon();
    });
    // A ring edit invalidates per-node depths that no longer describe a vertex,
    // so relief is re-synchronised (and dropped when it has become uniform)
    // rather than left to pair a depth with the wrong node.
    connect(m_canvas, &GroundMeshCanvas::changed, this, &GroundMeshStudio::sync_relief_to_polygon);
    sync_relief_to_polygon();

    refresh_summary();
}

void GroundMeshStudio::set_scene_camera_focus(double x, double y) {
    if (m_pos_x) m_pos_x->setValue(x);
    if (m_pos_y) m_pos_y->setValue(y);
}

std::vector<std::pair<float, float>> GroundMeshStudio::polygon() const {
    return m_canvas->polygon();
}

void GroundMeshStudio::refresh_summary() {
    if (m_syncing) return;
    const auto points = m_canvas->polygon();
    const bool simple = m_canvas->is_simple();
    m_summary->setText(QString("%1 vertices  ·  polygon %2 %3")
        .arg(int(points.size()))
        .arg(points.size() >= 3 ? "is" : "not yet")
        .arg(points.size() >= 3 ? (simple ? "closed (valid simple mesh)" : "closed (self-intersecting!)") : "closed"));
    if (m_points_editor->hasFocus()) return;
    QString text;
    text.reserve(points.size() * 24);
    for (const auto& p : points)
        text += QString("%1, %2\n").arg(double(p.first), 0, 'f', 1).arg(double(p.second), 0, 'f', 1);
    if (m_points_editor->toPlainText() == text) return;
    m_syncing = true;
    m_points_editor->setPlainText(text);
    m_syncing = false;
}

void GroundMeshStudio::load_from_text() {
    if (m_syncing) return;
    m_syncing = true;
    m_canvas->set_polygon([this] {
        std::vector<std::pair<float, float>> points;
        const QStringList lines = m_points_editor->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const auto& line : lines) {
            const QStringList parts = line.split(',');
            if (parts.size() == 2) {
                bool ok_x = false, ok_y = false;
                const float x = parts[0].trimmed().toFloat(&ok_x);
                const float y = parts[1].trimmed().toFloat(&ok_y);
                if (ok_x && ok_y) points.emplace_back(x, y);
            }
        }
        return points;
    }());
    m_syncing = false;
    refresh_summary();
}

// ── Sheet text: which dialect can express this sheet? ────────────────────────
//
// v1 (boulder) stores ONE depth pair for the whole sheet. v2 (boulderx) stores a
// depth per node. So a sheet with any per-node relief can only be written as v2,
// and writing it as v1 would silently flatten the Z the modder sculpted. The
// converse is not a problem: v1 sheets load in both, so an untouched slab stays
// readable by every tool.
namespace {

bool sheet_has_relief(const QVector<double>& front_z, const QVector<double>& back_z) {
    if (front_z.isEmpty() || front_z.size() != back_z.size()) return false;
    for (int i = 1; i < front_z.size(); ++i) {
        if (std::fabs(front_z[i] - front_z[0]) > 1e-6 ||
            std::fabs(back_z[i] - back_z[0]) > 1e-6)
            return true;
    }
    return false;
}

std::string build_sheet_text(const std::vector<std::pair<float, float>>& points,
                             const QVector<double>& front_z, const QVector<double>& back_z,
                             double min_depth, double max_depth, double surface_width,
                             int mesh_type, uint32_t random_seed,
                             const std::string& top_tex, const std::string& front_tex,
                             double world_z, const std::string& identifier,
                             bool force_v2, bool* wrote_v2) {
    const double front = std::max(min_depth, max_depth);
    const double back = std::min(min_depth, max_depth);
    const bool per_node = sheet_has_relief(front_z, back_z);
    const bool v2 = per_node || force_v2;
    if (wrote_v2) *wrote_v2 = v2;

    if (!v2) {
        boulder::GroundMesh gm;
        for (const auto& p : points) gm.polygon.push_back({double(p.first), double(p.second)});
        boulder::ensure_ccw(gm.polygon);
        gm.min_depth = back;
        gm.max_depth = front;
        gm.surface_width = surface_width;
        gm.top_texture = top_tex;
        gm.bottom_texture = front_tex;
        gm.z = world_z;
        gm.random_seed = random_seed;
        return boulder::serialize_swdm(gm);
    }

    boulderx::SwdmDocument doc;
    doc.identifier = identifier;
    doc.params.mesh_type = mesh_type;
    doc.params.surface_width = surface_width;
    doc.params.random_seed = random_seed;
    doc.params.surface_texture = top_tex;
    doc.params.front_texture = front_tex;
    doc.outline.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        boulderx::Node n;
        n.x = points[i].first;
        n.y = points[i].second;
        // A node without its own depth (no relief yet) uses the slab's own pair,
        // so toggling the generator does not change geometry by itself.
        n.front_depth = (i < size_t(front_z.size())) ? front_z[int(i)] : front;
        n.back_depth = (i < size_t(back_z.size())) ? back_z[int(i)] : back;
        doc.outline.push_back(n);
    }
    return boulderx::serialize_swdm(doc);
}

} // namespace

void GroundMeshStudio::sync_relief_to_polygon() {
    if (m_relief_syncing) return;
    m_relief_syncing = true;
    const int n = m_canvas->vertex_count();
    if (m_front_z.size() != n) {
        m_front_z.resize(n);
        m_back_z.resize(n);
        for (int i = 0; i < n; ++i) {
            m_front_z[i] = m_depth_max->value();
            m_back_z[i] = m_depth_min->value();
        }
    }
    if (!sheet_has_relief(m_front_z, m_back_z)) {
        m_front_z.clear();
        m_back_z.clear();
        m_relief_seed = 0;
    }
    m_relief_syncing = false;

    if (m_relief_status) {
        const bool relief = sheet_has_relief(m_front_z, m_back_z);
        m_relief_status->setText(relief
            ? QString("Per-node Z active — seed %1. Exports as a Zenith sheet (v2); "
                      "Boulder cannot open it.").arg(m_relief_seed)
            : QString("Uniform slab at Min/Max depth — exports as a v1 sheet, which "
                      "Boulder and Zenith both read.%1")
                  .arg(boulderx::kPerNodeReliefUnlocked
                           ? QString()
                           : QStringLiteral(" Per-node Z: #will be unlocked after.")));
    }
}

void GroundMeshStudio::randomise_relief() {
    // #will be unlocked after — guarded at the slot too, so a stale connection
    // or a keyboard shortcut cannot reach the sculptor while it is staged.
    if (!boulderx::kPerNodeReliefUnlocked) {
        if (m_relief_status)
            m_relief_status->setText(QStringLiteral(
                "Per-node Z is staged off for this release — #will be unlocked "
                "after. The sheet stays a uniform slab."));
        return;
    }
    if (m_canvas->vertex_count() < 3) {
        m_summary->setText("Need at least three vertices before shaping terrain relief.");
        return;
    }
    const auto points = m_canvas->polygon();
    std::vector<boulderx::Node> outline;
    outline.reserve(points.size());
    for (const auto& p : points) outline.push_back({double(p.first), double(p.second), 0.0, 0.0});

    const double front = m_relief_front->value() > 0.0 ? m_relief_front->value()
                                                       : std::fabs(m_depth_max->value());
    const double back = m_relief_back->value() > 0.0 ? m_relief_back->value()
                                                     : std::fabs(m_depth_min->value());

    // Seed advances on every press so repeated taps give new terrain, and the
    // value is shown so the same terrain can be reproduced from it.
    m_relief_seed = m_relief_seed ? m_relief_seed * 1664525u + 1013904223u
                                  : 12345u;
    boulderx::randomise_node_depths(outline, front, back, m_relief_seed);

    m_front_z.resize(int(outline.size()));
    m_back_z.resize(int(outline.size()));
    for (size_t i = 0; i < outline.size(); ++i) {
        m_front_z[int(i)] = outline[i].front_depth;
        m_back_z[int(i)] = outline[i].back_depth;
    }
    sync_relief_to_polygon();
    m_canvas->update();
    refresh_summary();
}

void GroundMeshStudio::clear_relief() {
    m_front_z.clear();
    m_back_z.clear();
    m_relief_seed = 0;
    sync_relief_to_polygon();
    m_canvas->update();
    refresh_summary();
}

void GroundMeshStudio::clear_canvas() { m_canvas->clear(); }

void GroundMeshStudio::undo_canvas() { m_canvas->undo(); }

// "Add to Scene…": generate a complete ground-mesh object (GroundPolygon +
// GroundMesh + Generator + CollisionShape + TextureMappings) as binary and hand
// it to the host to paste into the open scene's RAM. The host owns saving.
void GroundMeshStudio::add_to_scene() {
    const auto points = m_canvas->polygon();
    if (points.size() < 3) {
        m_summary->setText("Need at least three vertices before adding to the scene.");
        return;
    }
    if (!m_canvas->is_simple()) {
        m_summary->setText("Polygon is self-intersecting — undo or move vertices until it is simple.");
        return;
    }
    QString name = m_name->text().trimmed();
    if (name.isEmpty()) name = "ground_mesh";

    const bool want_boulderx =
        boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX;
    bool wrote_v2 = false;
    // MeshType 1 and the editor's default seed are what this panel has always
    // built with (it exposes neither field); the point of routing through
    // build_sheet_text is the per-node depth and the dialect, not new defaults.
    const std::string swdm = build_sheet_text(
        points, m_front_z, m_back_z, m_depth_min->value(), m_depth_max->value(), 80.0, 1,
        1291618994u,
        m_top_texture->text().toStdString(), m_front_texture->text().toStdString(),
        m_world_z->value(), name.toStdString(), want_boulderx, &wrote_v2);

    // The generator the user picked rebuilds the object; the sheet's dialect is
    // what it must be able to express. BoulderX reads both, so a v1 sheet in
    // BoulderX mode is fine — and a v2 sheet in Boulder mode is refused below
    // with the reason, rather than flattened.
    std::string bin;
    if (want_boulderx) {
        bin = boulderx::generate_ground_mesh_object_swdm(swdm, name.toStdString());
    } else {
        bin = boulder::generate_ground_mesh_object(swdm, name.toStdString(), m_world_z->value());
    }
    if (bin.empty()) {
        const std::string reason = want_boulderx ? boulderx::last_decline_reason()
                                                 : boulder::last_parse_error();
        m_summary->setText(QString("Ground mesh generator rejected the polygon (%1): %2")
            .arg(want_boulderx ? "Zenith" : "Boulder")
            .arg(QString::fromStdString(reason)));
        return;
    }
    emit groundMeshAddToScene(name, QByteArray(bin.data(), int(bin.size())),
                              m_pos_x->value(), m_pos_y->value(), m_world_z->value());
    m_summary->setText(QString("Ground mesh object '%1' ready — pasted into the open scene (Ctrl+S to save).")
        .arg(name));
}

void GroundMeshStudio::save_mesh() {
    const auto points = m_canvas->polygon();
    if (points.size() < 3) {
        m_summary->setText("Need at least three vertices before exporting (validation failed).");
        return;
    }
    if (!m_canvas->is_simple()) {
        m_summary->setText("Polygon is self-intersecting — undo or move vertices until it is simple.");
        return;
    }
    QString mesh_name = m_name->text().trimmed();
    if (mesh_name.isEmpty()) mesh_name = "ground_mesh";
    bool wrote_v2 = false;
    const std::string blob = build_sheet_text(
        points, m_front_z, m_back_z, m_depth_min->value(), m_depth_max->value(), 80.0, 1,
        1291618994u, m_top_texture->text().toStdString(),
        m_front_texture->text().toStdString(), m_world_z->value(),
        mesh_name.toStdString(),
        boulderx::ground_generator() == boulderx::GroundGenerator::BoulderX, &wrote_v2);
    if (blob.empty()) {
        m_summary->setText("Ground mesh validation failed (the sheet could not be serialized).");
        return;
    }
    QString base_dir = QString::fromStdString(ruby::core::ProjectContext::instance().project_dir());
    if (base_dir.isEmpty() || !QDir(base_dir).exists()) {
        const std::string& afile = ruby::core::ProjectContext::instance().active_file();
        if (!afile.empty()) {
            QFileInfo fi(QString::fromStdString(afile));
            if (fi.exists() || fi.dir().exists()) base_dir = fi.dir().path();
        }
    }
    if (base_dir.isEmpty() || !QDir(base_dir).exists()) base_dir = QDir::currentPath();

    const QString suggested = QDir(base_dir).filePath(mesh_name + "_ground.swdm");
    const QString path = QFileDialog::getSaveFileName(this, "Save ground mesh", suggested,
                                                      "Ground mesh (*.swdm *.gmesh)");
    if (path.isEmpty()) return;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        m_summary->setText("Could not write: " + path);
        return;
    }
    out.write(blob.data(), qint64(blob.size()));
    m_summary->setText(QString("Saved %1 (%2 bytes, %3). Wire it into a scene object to place it.")
        .arg(QFileInfo(path).fileName())
        .arg(int(blob.size()))
        .arg(wrote_v2 ? QStringLiteral("Zenith sheet, per-node Z")
                      : QStringLiteral("sheet readable by both generators")));
    emit groundMeshSaved(path);
}

} // namespace ruby::tools
