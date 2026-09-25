#pragma once

#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>

/**
 * @file zypher_math.h
 * @brief Mathematical foundation for Zypher, Swordigo's Gen 3 procedural world mesh engine.
 */

namespace zypher {

/** @brief 2D Vector */
struct V2 {
    float x, y;
    V2() : x(0), y(0) {}
    V2(float x, float y) : x(x), y(y) {}
    
    V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
    V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
    V2 operator*(float s) const { return {x * s, y * s}; }
    V2 operator/(float s) const { return {x / s, y / s}; }
    V2 operator-() const { return {-x, -y}; }
    
    V2& operator+=(V2 o) { x += o.x; y += o.y; return *this; }
    V2& operator-=(V2 o) { x -= o.x; y -= o.y; return *this; }
    V2& operator*=(float s) { x *= s; y *= s; return *this; }
    V2& operator/=(float s) { x /= s; y /= s; return *this; }
};

inline float dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline float cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
inline float length(V2 a) { return std::sqrt(dot(a, a)); }
inline V2 normalize(V2 a) { float l = length(a); return l > 0 ? a / l : V2(0, 0); }
inline V2 lerp(V2 a, V2 b, float t) { return a + (b - a) * t; }
inline V2 mix(V2 a, V2 b, float t) { return lerp(a, b, t); }

/** @brief 3D Vector */
struct V3 {
    float x, y, z;
    V3() : x(0), y(0), z(0) {}
    V3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator/(float s) const { return {x / s, y / s, z / s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    
    V3& operator+=(V3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(V3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    V3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
};

inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { float l = length(a); return l > 0 ? a / l : V3(0, 0, 0); }
inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline V3 mix(V3 a, V3 b, float t) { return lerp(a, b, t); }

/** @brief Centripetal Catmull-Rom spline */
struct CRSpline {
    std::vector<V3> pts;

    V3 eval_segment(V3 p0, V3 p1, V3 p2, V3 p3, float t, float alpha = 0.5f) const {
        auto get_t = [&](float t_start, V3 p_a, V3 p_b) {
            float a = std::pow(p_b.x - p_a.x, 2.0f) + std::pow(p_b.y - p_a.y, 2.0f) + std::pow(p_b.z - p_a.z, 2.0f);
            float b = std::pow(a, alpha * 0.5f);
            return b + t_start;
        };

        float t0 = 0.0f;
        float t1 = get_t(t0, p0, p1);
        float t2 = get_t(t1, p1, p2);
        float t3 = get_t(t2, p2, p3);

        if (t2 - t1 < 1e-5f) return p1;

        float real_t = t1 + (t2 - t1) * t;

        V3 a1 = (t1 - t0 > 1e-5f) ? (p0 * ((t1 - real_t) / (t1 - t0)) + p1 * ((real_t - t0) / (t1 - t0))) : p1;
        V3 a2 = p1 * ((t2 - real_t) / (t2 - t1)) + p2 * ((real_t - t1) / (t2 - t1));
        V3 a3 = (t3 - t2 > 1e-5f) ? (p2 * ((t3 - real_t) / (t3 - t2)) + p3 * ((real_t - t2) / (t3 - t2))) : p2;

        V3 b1 = (t2 - t0 > 1e-5f) ? (a1 * ((t2 - real_t) / (t2 - t0)) + a2 * ((real_t - t0) / (t2 - t0))) : a2;
        V3 b2 = (t3 - t1 > 1e-5f) ? (a2 * ((t3 - real_t) / (t3 - t1)) + a3 * ((real_t - t1) / (t3 - t1))) : a2;

        return b1 * ((t2 - real_t) / (t2 - t1)) + b2 * ((real_t - t1) / (t2 - t1));
    }

    /// Evaluate at t in [0, pts.size()-1] (NOT [0,1]).
    V3 eval(float t) const {
        if (pts.empty()) return V3();
        if (pts.size() == 1) return pts[0];

        int n = (int)pts.size();
        float param = t;  // direct index-space parameter
        int i = (int)param;
        if (i >= n - 1) { i = n - 2; }
        if (i < 0) { i = 0; }

        float local_t = param - i;
        // Clamp to [0,1] for safety
        if (local_t < 0.f) local_t = 0.f;
        if (local_t > 1.f) local_t = 1.f;

        V3 p0 = i > 0 ? pts[i - 1] : pts[i] - (pts[i + 1] - pts[i]);
        V3 p1 = pts[i];
        V3 p2 = pts[i + 1];
        V3 p3 = i + 2 < n ? pts[i + 2] : pts[i + 1] + (pts[i + 1] - pts[i]);

        return eval_segment(p0, p1, p2, p3, local_t);
    }

    /// Normalized tangent at t in [0, pts.size()-1].
    V3 eval_tangent(float t) const {
        float h = 0.001f;
        float tmax = (float)((int)pts.size() - 1);
        V3 p1 = eval(std::max(0.0f, t - h));
        V3 p2 = eval(std::min(tmax, t + h));
        V3 diff = p2 - p1;
        float len = length(diff);
        if (len < 1e-6f) return V3(1, 0, 0); // fallback tangent
        return diff / len;
    }
};

/** @brief Bishop frame (parallel transport moving frame) */
struct BishopFrame { V3 t, r, s; };

inline BishopFrame bishop_init(V3 t) {
    t = normalize(t);
    V3 up = std::abs(t.z) < 0.99f ? V3(0, 0, 1) : V3(1, 0, 0);
    V3 s = normalize(cross(up, t));
    V3 r = cross(t, s);
    return {t, r, s};
}

inline BishopFrame bishop_step(const BishopFrame& prev, V3 new_t) {
    new_t = normalize(new_t);
    V3 v1 = prev.t;
    V3 v2 = new_t;
    V3 axis = cross(v1, v2);
    float dot_val = dot(v1, v2);
    
    BishopFrame next;
    next.t = new_t;

    if (dot_val > 0.9999f) {
        next.r = prev.r;
        next.s = prev.s;
    } else if (dot_val < -0.9999f) {
        next.r = -prev.r;
        next.s = -prev.s;
    } else {
        float angle = std::acos(dot_val);
        axis = normalize(axis);
        auto rot = [&](V3 v) {
            return v * std::cos(angle) + cross(axis, v) * std::sin(angle) + axis * dot(axis, v) * (1.0f - std::cos(angle));
        };
        next.r = normalize(rot(prev.r));
        next.s = normalize(rot(prev.s));
    }
    return next;
}

/** @brief Adaptive tessellation */
inline void _tessellate_spline_rec(const CRSpline& spline, float t0, float t1, float angle_tol_cos, int depth, int max_depth, std::vector<float>& out) {
    if (depth >= max_depth) return;
    
    float tm = (t0 + t1) * 0.5f;
    V3 tan0 = spline.eval_tangent(t0);
    V3 tan1 = spline.eval_tangent(t1);
    
    if (dot(tan0, tan1) < angle_tol_cos) {
        _tessellate_spline_rec(spline, t0, tm, angle_tol_cos, depth + 1, max_depth, out);
        out.push_back(tm);
        _tessellate_spline_rec(spline, tm, t1, angle_tol_cos, depth + 1, max_depth, out);
    }
}

inline std::vector<float> tessellate_spline(const CRSpline& spline, float angle_tol_deg, int min_segs, int max_segs) {
    std::vector<float> res;
    if (spline.pts.empty()) return res;
    
    res.push_back(0.0f);
    float cos_tol = std::cos(angle_tol_deg * 3.14159265f / 180.0f);
    int max_depth = 10;

    for (int i = 0; i < min_segs; ++i) {
        float t0 = (float)i / min_segs;
        float t1 = (float)(i + 1) / min_segs;
        _tessellate_spline_rec(spline, t0, t1, cos_tol, 0, max_depth, res);
        res.push_back(t1);
    }
    
    return res;
}

/** @brief 2D Convex Hull (Monotone Chain) */
inline std::vector<V2> ccw_convex_hull(std::vector<V2> pts) {
    if (pts.size() <= 2) return pts;
    std::sort(pts.begin(), pts.end(), [](V2 a, V2 b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });

    std::vector<V2> hull;
    auto cross_2d = [](V2 o, V2 a, V2 b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    for (size_t i = 0; i < pts.size(); ++i) {
        while (hull.size() >= 2 && cross_2d(hull[hull.size() - 2], hull.back(), pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }

    for (int i = (int)pts.size() - 2, t = hull.size() + 1; i >= 0; i--) {
        while (hull.size() >= (size_t)t && cross_2d(hull[hull.size() - 2], hull.back(), pts[i]) <= 0)
            hull.pop_back();
        hull.push_back(pts[i]);
    }
    hull.pop_back();
    return hull;
}

/** @brief Alpha-shape silhouette */
inline std::vector<V2> alpha_shape_2d(const std::vector<V3>& pts, float alpha) {
    std::vector<V2> pts2d;
    for (auto p : pts) pts2d.push_back({p.x, p.y});
    if (alpha <= 0.0f || pts2d.size() < 3) return ccw_convex_hull(pts2d);

    std::vector<std::pair<int, int>> edges;
    int n = pts2d.size();
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (length(pts2d[i] - pts2d[j]) > 2 * alpha) continue;
            
            V2 d = pts2d[j] - pts2d[i];
            float l = length(d);
            if (l == 0) continue;
            
            V2 m = (pts2d[i] + pts2d[j]) * 0.5f;
            float h = std::sqrt(std::max(0.0f, alpha * alpha - (l / 2) * (l / 2)));
            V2 p_perp = normalize(V2(-d.y, d.x)) * h;

            V2 c1 = m + p_perp;
            V2 c2 = m - p_perp;

            bool c1_empty = true, c2_empty = true;
            for (int k = 0; k < n; k++) {
                if (k == i || k == j) continue;
                if (length(pts2d[k] - c1) < alpha - 1e-4f) c1_empty = false;
                if (length(pts2d[k] - c2) < alpha - 1e-4f) c2_empty = false;
                if (!c1_empty && !c2_empty) break;
            }

            if (c1_empty || c2_empty) {
                edges.push_back({i, j});
            }
        }
    }
    
    if (edges.empty()) return ccw_convex_hull(pts2d);
    
    std::vector<V2> shape;
    std::vector<bool> used(edges.size(), false);
    int curr_node = edges[0].first;
    shape.push_back(pts2d[curr_node]);
    
    while (true) {
        bool found = false;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (!used[i]) {
                if (edges[i].first == curr_node) {
                    curr_node = edges[i].second;
                    used[i] = true;
                    shape.push_back(pts2d[curr_node]);
                    found = true;
                    break;
                } else if (edges[i].second == curr_node) {
                    curr_node = edges[i].first;
                    used[i] = true;
                    shape.push_back(pts2d[curr_node]);
                    found = true;
                    break;
                }
            }
        }
        if (!found) break;
    }
    
    float area = 0;
    for (size_t i = 0; i < shape.size(); ++i) {
        V2 p1 = shape[i];
        V2 p2 = shape[(i + 1) % shape.size()];
        area += (p2.x - p1.x) * (p2.y + p1.y);
    }
    if (area > 0) std::reverse(shape.begin(), shape.end());

    return shape;
}

/** @brief Ear-clip triangulation */
inline std::vector<uint16_t> triangulate_polygon(const std::vector<V2>& poly) {
    std::vector<uint16_t> res;
    if (poly.size() < 3) return res;

    std::vector<int> V(poly.size());
    for (size_t i = 0; i < poly.size(); i++) V[i] = i;

    auto cross2d = [](V2 o, V2 a, V2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    
    auto is_ear = [&](int u, int v, int w, int n, const std::vector<int>& V) {
        V2 A = poly[V[u]], B = poly[V[v]], C = poly[V[w]];
        if (cross2d(A, B, C) <= 1e-5f) return false;
        for (int p = 0; p < n; p++) {
            if (p == u || p == v || p == w) continue;
            V2 P = poly[V[p]];
            bool b1 = cross2d(A, B, P) >= 0.0f;
            bool b2 = cross2d(B, C, P) >= 0.0f;
            bool b3 = cross2d(C, A, P) >= 0.0f;
            if (b1 && b2 && b3) return false;
        }
        return true;
    };

    int n = poly.size();
    int count = 2 * n;

    for (int m = n, v = n - 1; m > 2; ) {
        if (count-- <= 0) return res;
        int u = v; if (n <= u) u = 0;
        v = u + 1; if (n <= v) v = 0;
        int w = v + 1; if (n <= w) w = 0;

        if (is_ear(u, v, w, n, V)) {
            res.push_back(V[u]);
            res.push_back(V[v]);
            res.push_back(V[w]);
            for (int s = v, t = v + 1; t < n; s++, t++) V[s] = V[t];
            n--;
            m--;
            count = 2 * m;
        }
    }
    return res;
}

/** @brief Multi-octave fBm noise */
inline float hash1d(int x) {
    x = (x << 13) ^ x;
    return (1.0f - ((x * (x * x * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
}
inline float hash2d(int x, int y) {
    return hash1d(x + y * 57329);
}
inline float noise1d(float x) {
    int i = (int)std::floor(x);
    float f = x - i;
    float u = f * f * (3.0f - 2.0f * f);
    return lerp(V2(hash1d(i), 0), V2(hash1d(i + 1), 0), u).x;
}
inline float noise2d(float x, float y) {
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    float fx = x - ix;
    float fy = y - iy;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float a = hash2d(ix, iy);
    float b = hash2d(ix + 1, iy);
    float c = hash2d(ix, iy + 1);
    float d = hash2d(ix + 1, iy + 1);

    float r1 = lerp(V2(a, 0), V2(b, 0), ux).x;
    float r2 = lerp(V2(c, 0), V2(d, 0), ux).x;
    return lerp(V2(r1, 0), V2(r2, 0), uy).x;
}
inline float fbm2d(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += noise2d(x * freq, y * freq) * amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum;
}
inline float worley2d(float x, float y, float freq) {
    x *= freq; y *= freq;
    int ix = (int)std::floor(x);
    int iy = (int)std::floor(y);
    float md = 10.0f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            V2 cell(ix + i, iy + j);
            V2 p(hash2d(cell.x, cell.y) * 0.5f + 0.5f, hash2d(cell.x * 1.3f, cell.y * 1.7f) * 0.5f + 0.5f);
            p = p + cell;
            float d = length(V2(x, y) - p);
            if (d < md) md = d;
        }
    }
    return md;
}

/** @brief Profile cross-sections */
enum class ProfileKind { SmoothFillet, SedimentaryStep, GothicMoulding, MangaChamfer };
struct ProfilePoint { float t, n; };

inline std::vector<ProfilePoint> sample_profile(ProfileKind kind, int steps) {
    std::vector<ProfilePoint> res;
    for (int i = 0; i < steps; ++i) {
        float t = (float)i / std::max(1, steps - 1);
        float n = 0;
        switch (kind) {
            case ProfileKind::SmoothFillet:
                n = 0.5f - 0.5f * std::cos(t * 3.14159265f);
                break;
            case ProfileKind::SedimentaryStep:
                n = std::floor(t * 3.0f) / 2.0f; 
                n = mix(V2(n, 0), V2(t, 0), 0.2f).x;
                break;
            case ProfileKind::GothicMoulding:
                if (t < 0.4f) n = std::pow(t / 0.4f, 2.0f) * 0.5f;
                else n = 0.5f + std::pow((t - 0.4f) / 0.6f, 0.5f) * 0.5f;
                break;
            case ProfileKind::MangaChamfer:
                n = t < 0.5f ? t * 2.0f : 1.0f;
                break;
        }
        res.push_back({t, n});
    }
    return res;
}

} // namespace zypher
