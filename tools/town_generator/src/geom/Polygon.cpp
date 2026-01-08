#include "town_generator/geom/Polygon.h"
#include "town_generator/utils/MathUtils.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace geom {

std::vector<Polygon> Polygon::cut(const Point& p1, const Point& p2, double gap) const {
    // Faithful to MFCG: uses index-based splitting and stripe + boolean for gap

    double x1 = p1.x;
    double y1 = p1.y;
    double dx1 = p2.x - x1;
    double dy1 = p2.y - y1;

    size_t len = vertices_.size();
    int edge1 = 0;
    double ratio1 = 0.0;
    int edge2 = 0;
    double ratio2 = 0.0;
    int count = 0;

    for (size_t i = 0; i < len; ++i) {
        const Point& v0 = *vertices_[i];
        const Point& v1 = *vertices_[(i + 1) % len];

        double x2 = v0.x;
        double y2 = v0.y;
        double dx2 = v1.x - x2;
        double dy2 = v1.y - y2;

        auto t = GeomUtils::intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2);
        if (t.has_value() && t->y >= 0 && t->y <= 1) {
            if (count == 0) {
                edge1 = static_cast<int>(i);
                ratio1 = t->x;
            } else if (count == 1) {
                edge2 = static_cast<int>(i);
                ratio2 = t->x;
            }
            count++;
        }
    }

    if (count == 2) {
        Point diff = p2.subtract(p1);
        Point point1 = p1.add(diff.scale(ratio1));
        Point point2 = p1.add(diff.scale(ratio2));

        // First half
        std::vector<Point> half1_pts = slice(edge1 + 1, edge2 + 1);
        half1_pts.insert(half1_pts.begin(), point1);
        half1_pts.push_back(point2);
        Polygon half1(half1_pts);

        // Second half
        std::vector<Point> half2_pts = slice(edge2 + 1);
        std::vector<Point> start_pts = slice(0, edge1 + 1);
        half2_pts.insert(half2_pts.end(), start_pts.begin(), start_pts.end());
        half2_pts.insert(half2_pts.begin(), point2);
        half2_pts.push_back(point1);
        Polygon half2(half2_pts);

        // Apply gap using stripe + boolean subtraction (MFCG approach)
        if (gap > 0) {
            // Create cut line for stripe
            std::vector<Point> cutLine = {point1, point2};

            // Create stripe polygon along the cut line
            std::vector<Point> stripePoly = GeomUtils::stripe(cutLine, gap, 1.0);

            if (stripePoly.size() >= 3) {
                // Reverse stripe for subtraction
                std::vector<Point> stripeReversed = GeomUtils::reverse(stripePoly);

                // Subtract stripe from half1
                std::vector<Point> half1Clipped = GeomUtils::polygonIntersection(
                    half1.vertexValues(), stripeReversed, true
                );
                if (half1Clipped.size() >= 3) {
                    half1 = Polygon(half1Clipped);
                }

                // Subtract stripe from half2
                std::vector<Point> half2Clipped = GeomUtils::polygonIntersection(
                    half2.vertexValues(), stripeReversed, true
                );
                if (half2Clipped.size() >= 3) {
                    half2 = Polygon(half2Clipped);
                }
            }
        }

        Point v = vectori(edge1);
        if (GeomUtils::cross(dx1, dy1, v.x, v.y) > 0) {
            return {half1, half2};
        } else {
            return {half2, half1};
        }
    } else {
        return {deepCopy()};
    }
}

void Polygon::inset(const Point& p1, double d) {
    int i1 = indexOf(p1);
    if (i1 == -1) return;

    int len = static_cast<int>(vertices_.size());
    int i0 = (i1 > 0 ? i1 - 1 : len - 1);
    const Point& p0 = *vertices_[i0];
    int i2 = (i1 < len - 1 ? i1 + 1 : 0);
    const Point& p2 = *vertices_[i2];
    int i3 = (i2 < len - 1 ? i2 + 1 : 0);
    const Point& p3 = *vertices_[i3];

    Point v0 = p1.subtract(p0);
    Point v1 = p2.subtract(p1);
    Point v2 = p3.subtract(p2);

    double cosVal = v0.dot(v1) / v0.length() / v1.length();
    double z = v0.x * v1.y - v0.y * v1.x;
    double sinVal = std::sqrt(1 - cosVal * cosVal);
    double t = d / sinVal;

    if (z > 0) {
        t = std::min(t, v0.length() * 0.99);
    } else {
        t = std::min(t, v1.length() * 0.5);
    }
    t *= utils::MathUtils::sign(z);
    // Mutate the shared point
    vertices_[i1]->set(p1.subtract(v0.norm(t)));

    cosVal = v1.dot(v2) / v1.length() / v2.length();
    z = v1.x * v2.y - v1.y * v2.x;
    sinVal = std::sqrt(1 - cosVal * cosVal);
    t = d / sinVal;

    if (z > 0) {
        t = std::min(t, v2.length() * 0.99);
    } else {
        t = std::min(t, v1.length() * 0.5);
    }
    // Mutate the shared point
    vertices_[i2]->set(p2.add(v2.norm(t)));
}

Polygon Polygon::buffer(const std::vector<double>& d) const {
    // Creating a polygon (probably invalid) with offset edges
    Polygon q;
    size_t i = 0;
    size_t dSize = d.size();

    forEdge([&q, &d, &i, dSize](const Point& v0, const Point& v1) {
        if (i >= dSize) return;  // Bounds check
        double dd = d[i++];
        if (dd == 0) {
            q.push(v0);
            q.push(v1);
        } else {
            Point v = v1.subtract(v0);
            Point n = v.rotate90().norm(dd);
            q.push(v0.add(n));
            q.push(v1.add(n));
        }
    });

    // Creating a valid polygon by dealing with self-intersection
    bool wasCut;
    int lastEdge = 0;

    do {
        wasCut = false;
        int n = static_cast<int>(q.length());

        for (int ii = lastEdge; ii < n - 2; ++ii) {
            lastEdge = ii;

            const Point& p11 = q[ii];
            const Point& p12 = q[ii + 1];
            double x1 = p11.x;
            double y1 = p11.y;
            double dx1 = p12.x - x1;
            double dy1 = p12.y - y1;

            int jEnd = (ii > 0 ? n : n - 1);
            for (int j = ii + 2; j < jEnd; ++j) {
                const Point& p21 = q[j];
                const Point& p22 = (j < n - 1) ? q[j + 1] : q[0];
                double x2 = p21.x;
                double y2 = p21.y;
                double dx2 = p22.x - x2;
                double dy2 = p22.y - y2;

                auto intersection = GeomUtils::intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2);
                if (intersection.has_value() &&
                    intersection->x > DELTA && intersection->x < 1 - DELTA &&
                    intersection->y > DELTA && intersection->y < 1 - DELTA) {

                    Point pn(x1 + dx1 * intersection->x, y1 + dy1 * intersection->x);
                    q.insert(j + 1, pn);
                    q.insert(ii + 1, pn);

                    wasCut = true;
                    break;
                }
            }
            if (wasCut) break;
        }
    } while (wasCut);

    // Checking every part of the polygon to pick the biggest
    std::vector<int> regular;
    for (size_t ii = 0; ii < q.length(); ++ii) {
        regular.push_back(static_cast<int>(ii));
    }

    Polygon bestPart;
    double bestPartSq = -std::numeric_limits<double>::infinity();

    while (!regular.empty()) {
        std::vector<int> indices;
        int start = regular[0];
        int ii = start;

        do {
            indices.push_back(ii);
            regular.erase(std::find(regular.begin(), regular.end(), ii));

            int nextIdx = (ii + 1) % static_cast<int>(q.length());
            const Point& v = q[nextIdx];

            int next1 = q.indexOf(v);
            if (next1 == nextIdx) {
                next1 = q.lastIndexOf(v);
            }
            ii = (next1 == -1) ? nextIdx : next1;
        } while (ii != start);

        std::vector<Point> pts;
        for (int idx : indices) {
            pts.push_back(q[idx]);
        }
        Polygon p(pts);
        double s = p.square();

        if (s > bestPartSq) {
            bestPart = p;
            bestPartSq = s;
        }
    }

    return bestPart;
}

Polygon Polygon::shrink(const std::vector<double>& d) const {
    Polygon q = deepCopy();  // Use deep copy to avoid sharing
    size_t i = 0;
    size_t dSize = d.size();

    forEdge([&q, &d, &i, dSize](const Point& v1, const Point& v2) {
        if (i >= dSize) return;  // Bounds check
        double dd = d[i++];
        if (dd > 0) {
            Point v = v2.subtract(v1);
            Point n = v.rotate90().norm(dd);
            auto halves = q.cut(v1.add(n), v2.add(n), 0);
            if (!halves.empty()) {
                q = halves[0];
            }
        }
    });

    return q;
}

Polygon Polygon::peel(const Point& v1, double d) const {
    int i1 = indexOf(v1);
    if (i1 == -1) return deepCopy();

    int i2 = (i1 == static_cast<int>(vertices_.size()) - 1) ? 0 : i1 + 1;
    const Point& v2 = *vertices_[i2];

    Point v = v2.subtract(v1);
    Point n = v.rotate90().norm(d);

    auto halves = cut(v1.add(n), v2.add(n), 0);
    return halves.empty() ? deepCopy() : halves[0];
}

void Polygon::simplify(int n) {
    int len = static_cast<int>(vertices_.size());

    while (len > n) {
        int result = 0;
        double minMeasure = std::numeric_limits<double>::infinity();

        Point b = *vertices_[len - 1];
        Point c = *vertices_[0];

        for (int i = 0; i < len; ++i) {
            Point a = b;
            b = c;
            c = *vertices_[(i + 1) % len];

            double measure = std::abs(a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
            if (measure < minMeasure) {
                result = i;
                minMeasure = measure;
            }
        }

        splice(result, 1);
        len--;
    }
}

} // namespace geom
} // namespace town_generator
