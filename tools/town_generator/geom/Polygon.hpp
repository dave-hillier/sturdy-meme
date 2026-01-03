/**
 * Ported from: Source/com/watabou/geom/Polygon.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include "../geom/Point.hpp"
#include "../geom/GeomUtils.hpp"
#include "../utils/MathUtils.hpp"
#include "../utils/Random.hpp"
#include <vector>
#include <cmath>
#include <functional>
#include <algorithm>
#include <limits>

namespace town {

// Rectangle struct for getBounds
struct Rectangle {
    float left;
    float top;
    float right;
    float bottom;

    Rectangle(float x = 0, float y = 0, float width = 0, float height = 0)
        : left(x), top(y), right(x + width), bottom(y + height) {}

    float getWidth() const { return right - left; }
    float getHeight() const { return bottom - top; }
};

class Polygon {
private:
    std::vector<Point> vertices;

    static constexpr float DELTA = 0.000001f;

public:
    // Constructors
    Polygon() : vertices() {}

    explicit Polygon(const std::vector<Point>& verts) : vertices(verts) {}

    // Copy constructor
    Polygon(const Polygon& other) : vertices(other.vertices) {}

    // Assignment operator
    Polygon& operator=(const Polygon& other) {
        vertices = other.vertices;
        return *this;
    }

    // Array-like access
    Point& operator[](size_t index) {
        return vertices[index];
    }

    const Point& operator[](size_t index) const {
        return vertices[index];
    }

    size_t size() const {
        return vertices.size();
    }

    size_t length() const {
        return vertices.size();
    }

    bool empty() const {
        return vertices.empty();
    }

    void push(const Point& p) {
        vertices.push_back(p);
    }

    void push_back(const Point& p) {
        vertices.push_back(p);
    }

    void unshift(const Point& p) {
        vertices.insert(vertices.begin(), p);
    }

    void insert(size_t index, const Point& p) {
        vertices.insert(vertices.begin() + index, p);
    }

    void splice(size_t index, size_t count) {
        vertices.erase(vertices.begin() + index, vertices.begin() + index + count);
    }

    // Remove a specific point from the polygon
    bool remove(const Point& p) {
        auto it = std::find_if(vertices.begin(), vertices.end(),
            [&p](const Point& v) { return v == p; });
        if (it != vertices.end()) {
            vertices.erase(it);
            return true;
        }
        return false;
    }

    Point last() const {
        return vertices.back();
    }

    int indexOf(const Point& p, size_t startFrom = 0) const {
        for (size_t i = startFrom; i < vertices.size(); ++i) {
            if (vertices[i] == p) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int lastIndexOf(const Point& p) const {
        for (int i = static_cast<int>(vertices.size()) - 1; i >= 0; --i) {
            if (vertices[i] == p) {
                return i;
            }
        }
        return -1;
    }

    std::vector<Point> slice(size_t start, size_t end) const {
        if (start >= vertices.size()) return {};
        end = std::min(end, vertices.size());
        return std::vector<Point>(vertices.begin() + start, vertices.begin() + end);
    }

    std::vector<Point> slice(size_t start) const {
        if (start >= vertices.size()) return {};
        return std::vector<Point>(vertices.begin() + start, vertices.end());
    }

    std::vector<Point>& data() {
        return vertices;
    }

    const std::vector<Point>& data() const {
        return vertices;
    }

    // Iterator support
    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }
    auto begin() const { return vertices.begin(); }
    auto end() const { return vertices.end(); }

    // Set method from Polygon.hx
    void set(const Polygon& p) {
        for (size_t i = 0; i < p.size(); ++i) {
            vertices[i].set(p[i]);
        }
    }

    // Computed properties as getters

    // Signed area (shoelace formula)
    float getSquare() const {
        if (vertices.empty()) return 0.0f;
        Point v1 = last();
        Point v2 = vertices[0];
        float s = v1.x * v2.y - v2.x * v1.y;
        for (size_t i = 1; i < vertices.size(); ++i) {
            v1 = v2;
            v2 = vertices[i];
            s += (v1.x * v2.y - v2.x * v1.y);
        }
        return s * 0.5f;
    }

    // Property-style accessor (Haxe uses .square)
    float square() const { return getSquare(); }

    float getPerimeter() const {
        float len = 0.0f;
        forEdge([&len](const Point& v0, const Point& v1) {
            len += Point::distance(v0, v1);
        });
        return len;
    }

    // Property-style accessor
    float perimeter() const { return getPerimeter(); }

    // for circle   = 1.00
    // for square   = 0.79
    // for triangle = 0.60
    float getCompactness() const {
        float p = getPerimeter();
        return 4.0f * static_cast<float>(M_PI) * getSquare() / (p * p);
    }

    // Property-style accessor (Haxe uses .compactness)
    float compactness() const { return getCompactness(); }

    // Faster approximation of centroid
    Point getCenter() const {
        Point c;
        for (const auto& v : vertices) {
            c.addEq(v);
        }
        c.scaleEq(1.0f / vertices.size());
        return c;
    }

    // Property-style accessor
    Point center() const { return getCenter(); }

    Point getCentroid() const {
        float x = 0.0f;
        float y = 0.0f;
        float a = 0.0f;
        forEdge([&x, &y, &a](const Point& v0, const Point& v1) {
            float f = GeomUtils::cross(v0.x, v0.y, v1.x, v1.y);
            a += f;
            x += (v0.x + v1.x) * f;
            y += (v0.y + v1.y) * f;
        });
        float s6 = 1.0f / (3.0f * a);
        return Point(s6 * x, s6 * y);
    }

    // Property-style accessor (Haxe uses .centroid)
    Point centroid() const { return getCentroid(); }

    bool contains(const Point& v) const {
        return indexOf(v) != -1;
    }

    void forEdge(std::function<void(const Point&, const Point&)> f) const {
        size_t len = vertices.size();
        for (size_t i = 0; i < len; ++i) {
            f(vertices[i], vertices[(i + 1) % len]);
        }
    }

    // Similar to forEdge, but doesn't iterate over the v(n)-v(0)
    void forSegment(std::function<void(const Point&, const Point&)> f) const {
        for (size_t i = 0; i < vertices.size() - 1; ++i) {
            f(vertices[i], vertices[i + 1]);
        }
    }

    void offset(const Point& p) {
        float dx = p.x;
        float dy = p.y;
        for (auto& v : vertices) {
            v.offset(dx, dy);
        }
    }

    void rotate(float a) {
        float cosA = std::cos(a);
        float sinA = std::sin(a);
        for (auto& v : vertices) {
            float vx = v.x * cosA - v.y * sinA;
            float vy = v.y * cosA + v.x * sinA;
            v.setTo(vx, vy);
        }
    }

    bool isConvexVertexi(int i) const {
        int len = static_cast<int>(vertices.size());
        const Point& v0 = vertices[(i + len - 1) % len];
        const Point& v1 = vertices[i];
        const Point& v2 = vertices[(i + 1) % len];
        return GeomUtils::cross(v1.x - v0.x, v1.y - v0.y, v2.x - v1.x, v2.y - v1.y) > 0;
    }

    bool isConvexVertex(const Point& v1) const {
        Point v0 = prev(v1);
        Point v2 = next(v1);
        return GeomUtils::cross(v1.x - v0.x, v1.y - v0.y, v2.x - v1.x, v2.y - v1.y) > 0;
    }

    bool isConvex() const {
        for (const auto& v : vertices) {
            if (!isConvexVertex(v)) return false;
        }
        return true;
    }

    Point smoothVertexi(int i, float f = 1.0f) const {
        const Point& v = vertices[i];
        int len = static_cast<int>(vertices.size());
        const Point& prevV = vertices[(i + len - 1) % len];
        const Point& nextV = vertices[(i + 1) % len];
        return Point(
            (prevV.x + v.x * f + nextV.x) / (2 + f),
            (prevV.y + v.y * f + nextV.y) / (2 + f)
        );
    }

    Point smoothVertex(const Point& v, float f = 1.0f) const {
        Point prevV = prev(v);
        Point nextV = next(v);
        return Point(
            prevV.x + v.x * f + nextV.x,
            prevV.y + v.y * f + nextV.y
        ).scale(1.0f / (2 + f));
    }

    // This function returns minimal distance from any of the vertices
    // to a point, not real distance from the polygon
    float distance(const Point& p) const {
        const Point& v0 = vertices[0];
        float d = Point::distance(v0, p);
        for (size_t i = 1; i < vertices.size(); ++i) {
            const Point& v1 = vertices[i];
            float d1 = Point::distance(v1, p);
            if (d1 < d) d = d1;
        }
        return d;
    }

    Polygon smoothVertexEq(float f = 1.0f) const {
        size_t len = vertices.size();
        Point v1 = vertices[len - 1];
        Point v2 = vertices[0];
        std::vector<Point> result;
        for (size_t i = 0; i < len; ++i) {
            Point v0 = v1;
            v1 = v2;
            v2 = vertices[(i + 1) % len];
            result.push_back(Point(
                (v0.x + v1.x * f + v2.x) / (2 + f),
                (v0.y + v1.y * f + v2.y) / (2 + f)
            ));
        }
        return Polygon(result);
    }

    Polygon filterShort(float threshold) const {
        size_t i = 1;
        Point v0 = vertices[0];
        Point v1 = vertices[1];
        std::vector<Point> result;
        result.push_back(v0);
        do {
            do {
                v1 = vertices[i++];
            } while (Point::distance(v0, v1) < threshold && i < vertices.size());
            result.push_back(v0 = v1);
        } while (i < vertices.size());

        return Polygon(result);
    }

    // This function insets one edge defined by its first vertex.
    // It's not very reliable, but it usually works (better for convex
    // vertices than for concave ones). It doesn't change the number
    // of vertices.
    void inset(const Point& p1, float d) {
        int i1 = indexOf(p1);
        int len = static_cast<int>(vertices.size());
        int i0 = (i1 > 0 ? i1 - 1 : len - 1);
        Point p0 = vertices[i0];
        int i2 = (i1 < len - 1 ? i1 + 1 : 0);
        Point p2 = vertices[i2];
        int i3 = (i2 < len - 1 ? i2 + 1 : 0);
        Point p3 = vertices[i3];

        Point v0 = p1.subtract(p0);
        Point v1 = p2.subtract(p1);
        Point v2 = p3.subtract(p2);

        float cos_ = v0.dot(v1) / v0.getLength() / v1.getLength();
        float z = v0.x * v1.y - v0.y * v1.x;
        float t = d / std::sqrt(1 - cos_ * cos_); // sin( acos( cos ) )
        if (z > 0) {
            t = std::min(t, v0.getLength() * 0.99f);
        } else {
            t = std::min(t, v1.getLength() * 0.5f);
        }
        t *= MathUtils::sign(z);
        vertices[i1] = p1.subtract(v0.norm(t));

        cos_ = v1.dot(v2) / v1.getLength() / v2.getLength();
        z = v1.x * v2.y - v1.y * v2.x;
        t = d / std::sqrt(1 - cos_ * cos_);
        if (z > 0) {
            t = std::min(t, v2.getLength() * 0.99f);
        } else {
            t = std::min(t, v1.getLength() * 0.5f);
        }
        vertices[i2] = p2.add(v2.norm(t));
    }

    Polygon insetAll(const std::vector<float>& d) const {
        Polygon p(*this);
        for (size_t i = 0; i < p.size(); ++i) {
            if (d[i] != 0) p.inset(p[i], d[i]);
        }
        return p;
    }

    // This function insets all edges by the same distance
    void insetEq(float d) {
        for (size_t i = 0; i < vertices.size(); ++i) {
            inset(vertices[i], d);
        }
    }

    // This function insets all edges by distances defined in an array.
    // It's kind of reliable for both convex and concave vertices, but only
    // if all distances are equal. Otherwise weird "steps" are created.
    // It does change the number of vertices.
    Polygon buffer(const std::vector<float>& d) const {
        // Creating a polygon (probably invalid) with offset edges
        Polygon q;
        size_t i = 0;
        forEdge([&q, &d, &i](const Point& v0, const Point& v1) {
            float dd = d[i++];
            if (dd == 0) {
                q.push(v0);
                q.push(v1);
            } else {
                // here we may want to do something fancier for nicer joints
                Point v = v1.subtract(v0);
                Point n = v.rotate90().norm(dd);
                q.push(v0.add(n));
                q.push(v1.add(n));
            }
        });

        // Creating a valid polygon by dealing with self-intersection:
        // we need to find intersections of every edge with every other edge
        // and add intersection point (twice - for one edge and for the other)
        bool wasCut;
        size_t lastEdge = 0;
        do {
            wasCut = false;

            size_t n = q.size();
            for (size_t ii = lastEdge; ii < n - 2; ++ii) {
                lastEdge = ii;

                const Point& p11 = q[ii];
                const Point& p12 = q[ii + 1];
                float x1 = p11.x;
                float y1 = p11.y;
                float dx1 = p12.x - x1;
                float dy1 = p12.y - y1;

                for (size_t j = ii + 2; j < (ii > 0 ? n : n - 1); ++j) {
                    const Point& p21 = q[j];
                    const Point& p22 = j < n - 1 ? q[j + 1] : q[0];
                    float x2 = p21.x;
                    float y2 = p21.y;
                    float dx2 = p22.x - x2;
                    float dy2 = p22.y - y2;

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
        std::vector<size_t> regular;
        for (size_t ii = 0; ii < q.size(); ++ii) {
            regular.push_back(ii);
        }

        Polygon bestPart;
        float bestPartSq = -std::numeric_limits<float>::infinity();

        while (!regular.empty()) {
            std::vector<int> indices;
            size_t start = regular[0];
            size_t ii = start;
            do {
                indices.push_back(static_cast<int>(ii));
                regular.erase(std::find(regular.begin(), regular.end(), ii));

                size_t nextIdx = (ii + 1) % q.size();
                const Point& v = q[nextIdx];
                int next1 = q.indexOf(v);
                if (next1 == static_cast<int>(nextIdx)) {
                    next1 = q.lastIndexOf(v);
                }
                ii = next1 == -1 ? nextIdx : static_cast<size_t>(next1);
            } while (ii != start);

            std::vector<Point> pts;
            for (int idx : indices) {
                pts.push_back(q[idx]);
            }
            Polygon p(pts);
            float s = p.getSquare();
            if (s > bestPartSq) {
                bestPart = p;
                bestPartSq = s;
            }
        }

        return bestPart;
    }

    // Another version of "buffer" function for insetting all edges
    // by the same distance (it's the best use of that function anyway)
    Polygon bufferEq(float d) const {
        std::vector<float> dd(vertices.size(), d);
        return buffer(dd);
    }

    // This function insets all edges by distances defined in an array.
    // It can't outset a polygon. Works very well for convex polygons,
    // not so much concave ones. It produces a convex polygon.
    // It does change the number vertices
    Polygon shrink(const std::vector<float>& d) const {
        Polygon q(*this);
        size_t i = 0;
        forEdge([&q, &d, &i](const Point& v1, const Point& v2) {
            float dd = d[i++];
            if (dd > 0) {
                Point v = v2.subtract(v1);
                Point n = v.rotate90().norm(dd);
                q = q.cut(v1.add(n), v2.add(n), 0)[0];
            }
        });
        return q;
    }

    Polygon shrinkEq(float d) const {
        std::vector<float> dd(vertices.size(), d);
        return shrink(dd);
    }

    // A version of "shrink" function for insetting just one edge.
    // It effectively cuts a peel along the edge.
    Polygon peel(const Point& v1, float d) const {
        int i1 = indexOf(v1);
        int i2 = i1 == static_cast<int>(vertices.size()) - 1 ? 0 : i1 + 1;
        const Point& v2 = vertices[i2];

        Point v = v2.subtract(v1);
        Point n = v.rotate90().norm(d);

        return cut(v1.add(n), v2.add(n), 0)[0];
    }

    // Simplifies the polygons leaving only n vertices
    // Note: Original Haxe method was named "simplyfy" (typo preserved)
    void simplyfy(int n) {
        int len = static_cast<int>(vertices.size());
        while (len > n) {
            int result = 0;
            float minMeasure = std::numeric_limits<float>::infinity();

            Point b = vertices[len - 1];
            Point c = vertices[0];
            for (int i = 0; i < len; ++i) {
                Point a = b;
                b = c;
                c = vertices[(i + 1) % len];
                float measure = std::abs(a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
                if (measure < minMeasure) {
                    result = i;
                    minMeasure = measure;
                }
            }

            splice(result, 1);
            len--;
        }
    }

    int findEdge(const Point& a, const Point& b) const {
        int index = indexOf(a);
        return (index != -1 && vertices[(index + 1) % vertices.size()] == b ? index : -1);
    }

    Point next(const Point& a) const {
        return vertices[(indexOf(a) + 1) % vertices.size()];
    }

    Point prev(const Point& a) const {
        int idx = indexOf(a);
        return vertices[(idx + static_cast<int>(vertices.size()) - 1) % vertices.size()];
    }

    Point vector(const Point& v) const {
        return next(v).subtract(v);
    }

    Point vectori(int i) const {
        return vertices[i == static_cast<int>(vertices.size()) - 1 ? 0 : i + 1].subtract(vertices[i]);
    }

    bool borders(const Polygon& another) const {
        int len1 = static_cast<int>(vertices.size());
        int len2 = static_cast<int>(another.size());
        for (int i = 0; i < len1; ++i) {
            int j = another.indexOf(vertices[i]);
            if (j != -1) {
                const Point& nextV = vertices[(i + 1) % len1];
                // If this cause is not true, then should return false,
                // but it doesn't work for some reason
                if (nextV == another[(j + 1) % len2] ||
                    nextV == another[(j + len2 - 1) % len2]) return true;
            }
        }
        return false;
    }

    Rectangle getBounds() const {
        Rectangle rect(vertices[0].x, vertices[0].y);
        for (const auto& v : vertices) {
            rect.left = std::min(rect.left, v.x);
            rect.right = std::max(rect.right, v.x);
            rect.top = std::min(rect.top, v.y);
            rect.bottom = std::max(rect.bottom, v.y);
        }
        return rect;
    }

    std::vector<Polygon> split(const Point& p1, const Point& p2) const {
        return spliti(indexOf(p1), indexOf(p2));
    }

    std::vector<Polygon> spliti(int i1, int i2) const {
        if (i1 > i2) {
            std::swap(i1, i2);
        }

        std::vector<Point> slice1 = slice(i1, i2 + 1);
        std::vector<Point> slice2a = slice(i2);
        std::vector<Point> slice2b = slice(0, i1 + 1);
        slice2a.insert(slice2a.end(), slice2b.begin(), slice2b.end());

        return {Polygon(slice1), Polygon(slice2a)};
    }

    std::vector<Polygon> cut(const Point& p1, const Point& p2, float gap = 0) const {
        float x1 = p1.x;
        float y1 = p1.y;
        float dx1 = p2.x - x1;
        float dy1 = p2.y - y1;

        int len = static_cast<int>(vertices.size());
        int edge1 = 0;
        float ratio1 = 0.0f;
        int edge2 = 0;
        float ratio2 = 0.0f;
        int count = 0;

        for (int i = 0; i < len; ++i) {
            const Point& v0 = vertices[i];
            const Point& v1 = vertices[(i + 1) % len];

            float x2 = v0.x;
            float y2 = v0.y;
            float dx2 = v1.x - x2;
            float dy2 = v1.y - y2;

            auto t = GeomUtils::intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2);
            if (t.has_value() && t->y >= 0 && t->y <= 1) {
                switch (count) {
                    case 0:
                        edge1 = i;
                        ratio1 = t->x;
                        break;
                    case 1:
                        edge2 = i;
                        ratio2 = t->x;
                        break;
                }
                count++;
            }
        }

        if (count == 2) {
            Point point1 = p1.add(p2.subtract(p1).scale(ratio1));
            Point point2 = p1.add(p2.subtract(p1).scale(ratio2));

            std::vector<Point> half1Pts = slice(edge1 + 1, edge2 + 1);
            Polygon half1(half1Pts);
            half1.unshift(point1);
            half1.push(point2);

            std::vector<Point> half2a = slice(edge2 + 1);
            std::vector<Point> half2b = slice(0, edge1 + 1);
            half2a.insert(half2a.end(), half2b.begin(), half2b.end());
            Polygon half2(half2a);
            half2.unshift(point2);
            half2.push(point1);

            if (gap > 0) {
                half1 = half1.peel(point2, gap / 2);
                half2 = half2.peel(point1, gap / 2);
            }

            Point v = vectori(edge1);
            return GeomUtils::cross(dx1, dy1, v.x, v.y) > 0 ?
                std::vector<Polygon>{half1, half2} :
                std::vector<Polygon>{half2, half1};
        } else {
            return {Polygon(*this)};
        }
    }

    std::vector<float> interpolate(const Point& p) const {
        float sum = 0.0f;
        std::vector<float> dd;
        for (const auto& v : vertices) {
            float d = 1.0f / Point::distance(v, p);
            sum += d;
            dd.push_back(d);
        }
        std::vector<float> result;
        for (float d : dd) {
            result.push_back(d / sum);
        }
        return result;
    }

    // Get a random vertex (matches Haxe ArrayExtender.random)
    Point random() const {
        if (vertices.empty()) return Point();
        size_t idx = static_cast<size_t>(Random::getFloat() * static_cast<float>(vertices.size()));
        return vertices[idx];
    }

    // Find the vertex that minimizes the given function
    template<typename F>
    Point min(F&& func) const {
        if (vertices.empty()) return Point();
        Point result = vertices[0];
        float minVal = func(result);
        for (size_t i = 1; i < vertices.size(); ++i) {
            float val = func(vertices[i]);
            if (val < minVal) {
                minVal = val;
                result = vertices[i];
            }
        }
        return result;
    }

    // Find the vertex that maximizes the given function
    template<typename F>
    Point max(F&& func) const {
        if (vertices.empty()) return Point();
        Point result = vertices[0];
        float maxVal = func(result);
        for (size_t i = 1; i < vertices.size(); ++i) {
            float val = func(vertices[i]);
            if (val > maxVal) {
                maxVal = val;
                result = vertices[i];
            }
        }
        return result;
    }

    // Create a copy of this polygon
    Polygon copy() const {
        return Polygon(vertices);
    }

    // Static factory methods
    static Polygon rect(float w = 1.0f, float h = 1.0f) {
        return Polygon({
            Point(-w / 2, -h / 2),
            Point(w / 2, -h / 2),
            Point(w / 2, h / 2),
            Point(-w / 2, h / 2)
        });
    }

    static Polygon regular(int n = 8, float r = 1.0f) {
        std::vector<Point> pts;
        for (int i = 0; i < n; ++i) {
            float a = static_cast<float>(i) / n * static_cast<float>(M_PI) * 2;
            pts.push_back(Point(r * std::cos(a), r * std::sin(a)));
        }
        return Polygon(pts);
    }

    static Polygon circle(float r = 1.0f) {
        return regular(16, r);
    }
};

} // namespace town
