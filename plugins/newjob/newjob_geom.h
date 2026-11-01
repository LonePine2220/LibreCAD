/*****************************************************************************/
/*  newjob_geom.h - small 2D geometry helpers for the New Job plugin         */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 2 of the License, or (at your option) any later version.  */
/*****************************************************************************/

#ifndef NEWJOB_GEOM_H
#define NEWJOB_GEOM_H

#include <QPointF>
#include <QRectF>
#include <QVector>
#include <cmath>
#include <limits>

namespace NJ {

using Ring = QVector<QPointF>;
using Poly = QVector<Ring>;     // polygon: outer ring(s) + holes, esri style
using Path = QVector<QPointF>;

inline QRectF bounds(const Poly &p) {
    double x0 = std::numeric_limits<double>::max(), y0 = x0;
    double x1 = -x0, y1 = -x0;
    for (const Ring &r : p)
        for (const QPointF &q : r) {
            x0 = std::min(x0, q.x()); y0 = std::min(y0, q.y());
            x1 = std::max(x1, q.x()); y1 = std::max(y1, q.y());
        }
    if (x0 > x1) return QRectF();
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

inline QRectF unite(const QRectF &a, const QRectF &b) {
    if (a.isNull()) return b;
    if (b.isNull()) return a;
    return a.united(b);
}

inline double ringArea(const Ring &r) {   // signed, shoelace
    double a = 0;
    for (int i = 0, n = r.size(); i < n; ++i) {
        const QPointF &p = r[i], &q = r[(i + 1) % n];
        a += p.x() * q.y() - q.x() * p.y();
    }
    return a / 2.0;
}

// Area-weighted centroid of the largest ring
inline QPointF centroid(const Poly &p) {
    const Ring *best = nullptr;
    double bestA = 0;
    for (const Ring &r : p) {
        double a = std::fabs(ringArea(r));
        if (a > bestA) { bestA = a; best = &r; }
    }
    if (!best || best->isEmpty()) {
        QRectF b = bounds(p);
        return b.center();
    }
    const Ring &r = *best;
    double a = ringArea(r), cx = 0, cy = 0;
    if (std::fabs(a) < 1e-9) return bounds(p).center();
    for (int i = 0, n = r.size(); i < n; ++i) {
        const QPointF &s = r[i], &t = r[(i + 1) % n];
        double f = s.x() * t.y() - t.x() * s.y();
        cx += (s.x() + t.x()) * f;
        cy += (s.y() + t.y()) * f;
    }
    return QPointF(cx / (6 * a), cy / (6 * a));
}

inline bool pointInRing(const QPointF &pt, const Ring &r) {
    bool in = false;
    for (int i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const QPointF &a = r[i], &b = r[j];
        if (((a.y() > pt.y()) != (b.y() > pt.y())) &&
            (pt.x() < (b.x() - a.x()) * (pt.y() - a.y()) / (b.y() - a.y()) + a.x()))
            in = !in;
    }
    return in;
}

// Even-odd over all rings handles holes
inline bool pointInPoly(const QPointF &pt, const Poly &p) {
    bool in = false;
    for (const Ring &r : p)
        if (pointInRing(pt, r)) in = !in;
    return in;
}

inline QPointF closestOnSeg(const QPointF &p, const QPointF &a, const QPointF &b) {
    double dx = b.x() - a.x(), dy = b.y() - a.y();
    double l2 = dx * dx + dy * dy;
    if (l2 <= 0) return a;
    double t = ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / l2;
    t = std::max(0.0, std::min(1.0, t));
    return QPointF(a.x() + t * dx, a.y() + t * dy);
}

inline double dist(const QPointF &a, const QPointF &b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

inline QPointF closestOnPoly(const QPointF &p, const Poly &poly, double *d = nullptr) {
    double best = std::numeric_limits<double>::max();
    QPointF bp = p;
    for (const Ring &r : poly) {
        const int n = r.size();
        if (n == 1) { double dd = dist(p, r[0]); if (dd < best) { best = dd; bp = r[0]; } continue; }
        for (int i = 0; i < n; ++i) {
            QPointF c = closestOnSeg(p, r[i], r[(i + 1) % n]);
            double dd = dist(p, c);
            if (dd < best) { best = dd; bp = c; }
        }
    }
    if (d) *d = best;
    return bp;
}

inline bool segsIntersect(const QPointF &p1, const QPointF &p2, const QPointF &p3, const QPointF &p4) {
    auto cross = [](const QPointF &o, const QPointF &a, const QPointF &b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    double d1 = cross(p3, p4, p1), d2 = cross(p3, p4, p2);
    double d3 = cross(p1, p2, p3), d4 = cross(p1, p2, p4);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

// Minimum distance between two polygons (0 if they overlap or cross)
inline double polyDistance(const Poly &a, const Poly &b) {
    for (const Ring &r : a) if (!r.isEmpty() && pointInPoly(r.first(), b)) return 0;
    for (const Ring &r : b) if (!r.isEmpty() && pointInPoly(r.first(), a)) return 0;
    double best = std::numeric_limits<double>::max();
    for (const Ring &r : a) for (const QPointF &p : r) { double d; closestOnPoly(p, b, &d); best = std::min(best, d); }
    for (const Ring &r : b) for (const QPointF &p : r) { double d; closestOnPoly(p, a, &d); best = std::min(best, d); }
    if (best > 0) {
        for (const Ring &ra : a) for (int i = 0; i < ra.size(); ++i)
            for (const Ring &rb : b) for (int j = 0; j < rb.size(); ++j)
                if (segsIntersect(ra[i], ra[(i + 1) % ra.size()], rb[j], rb[(j + 1) % rb.size()])) return 0;
    }
    return best;
}

inline bool polysIntersect(const Poly &a, const Poly &b) {
    if (!bounds(a).intersects(bounds(b))) return false;
    return polyDistance(a, b) <= 0.0;
}

// Liang-Barsky segment clip; returns false if fully outside
inline bool clipSeg(QPointF &a, QPointF &b, const QRectF &r) {
    double x0 = a.x(), y0 = a.y(), dx = b.x() - x0, dy = b.y() - y0;
    double t0 = 0, t1 = 1;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {x0 - r.left(), r.right() - x0, y0 - r.top(), r.bottom() - y0};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) { if (q[i] < 0) return false; continue; }
        double t = q[i] / p[i];
        if (p[i] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
        else          { if (t < t0) return false; if (t < t1) t1 = t; }
    }
    b = QPointF(x0 + t1 * dx, y0 + t1 * dy);
    a = QPointF(x0 + t0 * dx, y0 + t0 * dy);
    return true;
}

// Clip a polyline to a rectangle; may split into several pieces
inline QVector<Path> clipPath(const Path &path, const QRectF &r) {
    QVector<Path> out;
    Path cur;
    for (int i = 0; i + 1 < path.size(); ++i) {
        QPointF a = path[i], b = path[i + 1];
        bool aIn = r.contains(a);
        if (!clipSeg(a, b, r)) {
            if (cur.size() > 1) out.append(cur);
            cur.clear();
            continue;
        }
        if (cur.isEmpty() || !aIn || dist(cur.last(), a) > 1e-9) {
            if (cur.size() > 1) out.append(cur);
            cur.clear();
            cur.append(a);
        }
        cur.append(b);
        if (!r.contains(path[i + 1])) {
            if (cur.size() > 1) out.append(cur);
            cur.clear();
        }
    }
    if (cur.size() > 1) out.append(cur);
    return out;
}

inline double pathLength(const Path &p) {
    double l = 0;
    for (int i = 0; i + 1 < p.size(); ++i) l += dist(p[i], p[i + 1]);
    return l;
}

} // namespace NJ

#endif
