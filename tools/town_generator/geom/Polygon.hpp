/**
 * Ported from: Source/com/watabou/geom/Polygon.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible.
 *
 * IMPORTANT: In Haxe, Point is a reference type (class). Polygon stores
 * references to Point objects, and indexOf/contains compare by identity.
 * This C++ version uses PointPtr (shared_ptr<Point>) to match those semantics.
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
#include <unordered_map>

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
    std::vector<PointPtr> vertices;

    static constexpr float DELTA = 0.000001f;

    // Index cache for O(1) indexOf lookups
    mutable std::unordered_map<const Point*, size_t> indexCache_;
    mutable bool indexCacheValid_ = false;

    void invalidateIndexCache() const { indexCacheValid_ = false; }

    void ensureIndexCache() const {
        if (!indexCacheValid_) {
            indexCache_.clear();
            for (size_t i = 0; i < vertices.size(); ++i) {
                indexCache_[vertices[i].get()] = i;
            }
            indexCacheValid_ = true;
        }
    }

public:
    // Constructors
    Polygon() : vertices() {}

    explicit Polygon(const std::vector<PointPtr>& verts) : vertices(verts) {}

    // Constructor that takes Point values and creates shared copies
    Polygon(std::initializer_list<Point> pts) {
        for (const Point& p : pts) {
            vertices.push_back(makePoint(p));
        }
    }

    // Copy constructor - shallow copy of shared_ptrs (reference semantics)
    Polygon(const Polygon& other) = default;

    // Move constructor
    Polygon(Polygon&& other) noexcept = default;

    // Assignment operators
    Polygon& operator=(const Polygon& other) = default;
    Polygon& operator=(Polygon&& other) noexcept = default;

    // Array-like access - returns shared_ptr
    PointPtr operator[](size_t index) {
        return vertices[index];
    }

    PointPtr operator[](size_t index) const {
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

    void push(PointPtr p) {
        vertices.push_back(p);
        invalidateIndexCache();
    }

    void push_back(PointPtr p) {
        vertices.push_back(p);
        invalidateIndexCache();
    }

    void unshift(PointPtr p) {
        vertices.insert(vertices.begin(), p);
        invalidateIndexCache();
    }

    void insert(size_t index, PointPtr p) {
        vertices.insert(vertices.begin() + index, p);
        invalidateIndexCache();
    }

    void splice(size_t index, size_t count) {
        vertices.erase(vertices.begin() + index, vertices.begin() + index + count);
        invalidateIndexCache();
    }

    // Remove a specific point from the polygon (by pointer identity)
    bool remove(PointPtr p) {
        auto it = std::find(vertices.begin(), vertices.end(), p);
        if (it != vertices.end()) {
            vertices.erase(it);
            invalidateIndexCache();
            return true;
        }
        return false;
    }

    PointPtr last() const {
        return vertices.back();
    }

    // Find by pointer identity (matches Haxe semantics)
    // Uses O(1) cache lookup when startFrom == 0
    int indexOf(PointPtr p, size_t startFrom = 0) const {
        if (startFrom == 0) {
            ensureIndexCache();
            auto it = indexCache_.find(p.get());
            if (it != indexCache_.end()) {
                return static_cast<int>(it->second);
            }
            return -1;
        }
        // Fallback to linear search for startFrom != 0
        for (size_t i = startFrom; i < vertices.size(); ++i) {
            if (vertices[i] == p) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int lastIndexOf(PointPtr p) const {
        for (int i = static_cast<int>(vertices.size()) - 1; i >= 0; --i) {
            if (vertices[i] == p) {
                return i;
            }
        }
        return -1;
    }

    std::vector<PointPtr> slice(size_t start, size_t end) const {
        if (start >= vertices.size()) return {};
        end = std::min(end, vertices.size());
        return std::vector<PointPtr>(vertices.begin() + start, vertices.begin() + end);
    }

    std::vector<PointPtr> slice(size_t start) const {
        if (start >= vertices.size()) return {};
        return std::vector<PointPtr>(vertices.begin() + start, vertices.end());
    }

    std::vector<PointPtr>& data() {
        return vertices;
    }

    const std::vector<PointPtr>& data() const {
        return vertices;
    }

    // Iterator support
    auto begin() { return vertices.begin(); }
    auto end() { return vertices.end(); }
    auto begin() const { return vertices.begin(); }
    auto end() const { return vertices.end(); }

    // Set method from Polygon.hx - copies coordinates from p's vertices to this polygon's vertices
    void set(const Polygon& p) {
        for (size_t i = 0; i < p.size(); ++i) {
            vertices[i]->set(*p[i]);
        }
    }

    // Computed properties as getters

    // Signed area (shoelace formula)
    float getSquare() const {
        if (vertices.empty()) return 0.0f;
        PointPtr v1 = last();
        PointPtr v2 = vertices[0];
        float s = v1->x * v2->y - v2->x * v1->y;
        for (size_t i = 1; i < vertices.size(); ++i) {
            v1 = v2;
            v2 = vertices[i];
            s += (v1->x * v2->y - v2->x * v1->y);
        }
        return s * 0.5f;
    }

    // Property-style accessor (Haxe uses .square)
    float square() const { return getSquare(); }

    float getPerimeter() const {
        float len = 0.0f;
        forEdge([&len](PointPtr v0, PointPtr v1) {
            len += Point::distance(*v0, *v1);
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
        for (const PointPtr& v : vertices) {
            c.addEq(*v);
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
        forEdge([&x, &y, &a](PointPtr v0, PointPtr v1) {
            float f = GeomUtils::cross(v0->x, v0->y, v1->x, v1->y);
            a += f;
            x += (v0->x + v1->x) * f;
            y += (v0->y + v1->y) * f;
        });
        float s6 = 1.0f / (3.0f * a);
        return Point(s6 * x, s6 * y);
    }

    // Property-style accessor (Haxe uses .centroid)
    Point centroid() const { return getCentroid(); }

    bool contains(PointPtr v) const {
        return indexOf(v) != -1;
    }

    void forEdge(std::function<void(PointPtr, PointPtr)> f) const {
        size_t len = vertices.size();
        for (size_t i = 0; i < len; ++i) {
            f(vertices[i], vertices[(i + 1) % len]);
        }
    }

    // Similar to forEdge, but doesn't iterate over the v(n)-v(0)
    void forSegment(std::function<void(PointPtr, PointPtr)> f) const {
        for (size_t i = 0; i < vertices.size() - 1; ++i) {
            f(vertices[i], vertices[i + 1]);
        }
    }

    void offset(const Point& p) {
        float dx = p.x;
        float dy = p.y;
        for (const PointPtr& v : vertices) {
            v->offset(dx, dy);
        }
    }

    void rotate(float a) {
        float cosA = std::cos(a);
        float sinA = std::sin(a);
        for (const PointPtr& v : vertices) {
            float vx = v->x * cosA - v->y * sinA;
            float vy = v->y * cosA + v->x * sinA;
            v->setTo(vx, vy);
        }
    }

    bool isConvexVertexi(int i) const {
        int len = static_cast<int>(vertices.size());
        PointPtr v0 = vertices[(i + len - 1) % len];
        PointPtr v1 = vertices[i];
        PointPtr v2 = vertices[(i + 1) % len];
        return GeomUtils::cross(v1->x - v0->x, v1->y - v0->y, v2->x - v1->x, v2->y - v1->y) > 0;
    }

    bool isConvexVertex(PointPtr v1) const {
        PointPtr v0 = prev(v1);
        PointPtr v2 = next(v1);
        return GeomUtils::cross(v1->x - v0->x, v1->y - v0->y, v2->x - v1->x, v2->y - v1->y) > 0;
    }

    bool isConvex() const {
        for (const PointPtr& v : vertices) {
            if (!isConvexVertex(v)) return false;
        }
        return true;
    }

    Point smoothVertexi(int i, float f = 1.0f) const {
        PointPtr v = vertices[i];
        int len = static_cast<int>(vertices.size());
        PointPtr prevV = vertices[(i + len - 1) % len];
        PointPtr nextV = vertices[(i + 1) % len];
        return Point(
            (prevV->x + v->x * f + nextV->x) / (2 + f),
            (prevV->y + v->y * f + nextV->y) / (2 + f)
        );
    }

    Point smoothVertex(PointPtr v, float f = 1.0f) const {
        PointPtr prevV = prev(v);
        PointPtr nextV = next(v);
        return Point(
            prevV->x + v->x * f + nextV->x,
            prevV->y + v->y * f + nextV->y
        ).scale(1.0f / (2 + f));
    }

    // This function returns minimal distance from any of the vertices
    // to a point, not real distance from the polygon
    float distance(const Point& p) const {
        if (vertices.empty()) return std::numeric_limits<float>::infinity();
        PointPtr v0 = vertices[0];
        if (!v0) return std::numeric_limits<float>::infinity();
        float d = Point::distance(*v0, p);
        for (size_t i = 1; i < vertices.size(); ++i) {
            PointPtr v1 = vertices[i];
            if (!v1) continue;
            float d1 = Point::distance(*v1, p);
            if (d1 < d) d = d1;
        }
        return d;
    }

    // Returns a new polygon with smoothed vertices
    std::vector<Point> smoothVertexEqValues(float f = 1.0f) const {
        size_t len = vertices.size();
        PointPtr v1 = vertices[len - 1];
        PointPtr v2 = vertices[0];
        std::vector<Point> result;
        for (size_t i = 0; i < len; ++i) {
            PointPtr v0 = v1;
            v1 = v2;
            v2 = vertices[(i + 1) % len];
            result.push_back(Point(
                (v0->x + v1->x * f + v2->x) / (2 + f),
                (v0->y + v1->y * f + v2->y) / (2 + f)
            ));
        }
        return result;
    }

    // This function insets one edge defined by its first vertex.
    void inset(PointPtr p1, float d) {
        int i1 = indexOf(p1);
        int len = static_cast<int>(vertices.size());
        int i0 = (i1 > 0 ? i1 - 1 : len - 1);
        PointPtr p0 = vertices[i0];
        int i2 = (i1 < len - 1 ? i1 + 1 : 0);
        PointPtr p2 = vertices[i2];
        int i3 = (i2 < len - 1 ? i2 + 1 : 0);
        PointPtr p3 = vertices[i3];

        Point v0 = p1->subtract(*p0);
        Point v1 = p2->subtract(*p1);
        Point v2 = p3->subtract(*p2);

        float cos_ = v0.dot(v1) / v0.getLength() / v1.getLength();
        float z = v0.x * v1.y - v0.y * v1.x;
        float t = d / std::sqrt(1 - cos_ * cos_);
        if (z > 0) {
            t = std::min(t, v0.getLength() * 0.99f);
        } else {
            t = std::min(t, v1.getLength() * 0.5f);
        }
        t *= MathUtils::sign(z);
        Point newP1 = p1->subtract(v0.norm(t));
        p1->x = newP1.x;
        p1->y = newP1.y;

        cos_ = v1.dot(v2) / v1.getLength() / v2.getLength();
        z = v1.x * v2.y - v1.y * v2.x;
        t = d / std::sqrt(1 - cos_ * cos_);
        if (z > 0) {
            t = std::min(t, v2.getLength() * 0.99f);
        } else {
            t = std::min(t, v1.getLength() * 0.5f);
        }
        Point newP2 = p2->add(v2.norm(t));
        p2->x = newP2.x;
        p2->y = newP2.y;
    }

    // This function insets all edges by the same distance
    void insetEq(float d) {
        for (size_t i = 0; i < vertices.size(); ++i) {
            inset(vertices[i], d);
        }
    }

    // Simplifies the polygons leaving only n vertices
    void simplyfy(int n) {
        int len = static_cast<int>(vertices.size());
        while (len > n) {
            int result = 0;
            float minMeasure = std::numeric_limits<float>::infinity();

            PointPtr b = vertices[len - 1];
            PointPtr c = vertices[0];
            for (int i = 0; i < len; ++i) {
                PointPtr a = b;
                b = c;
                c = vertices[(i + 1) % len];
                float measure = std::abs(a->x * (b->y - c->y) + b->x * (c->y - a->y) + c->x * (a->y - b->y));
                if (measure < minMeasure) {
                    result = i;
                    minMeasure = measure;
                }
            }

            splice(result, 1);
            len--;
        }
    }

    int findEdge(PointPtr a, PointPtr b) const {
        int index = indexOf(a);
        return (index != -1 && vertices[(index + 1) % vertices.size()] == b ? index : -1);
    }

    PointPtr next(PointPtr a) const {
        return vertices[(indexOf(a) + 1) % vertices.size()];
    }

    PointPtr prev(PointPtr a) const {
        int idx = indexOf(a);
        return vertices[(idx + static_cast<int>(vertices.size()) - 1) % vertices.size()];
    }

    Point vector(PointPtr v) const {
        return next(v)->subtract(*v);
    }

    Point vectori(int i) const {
        return vertices[i == static_cast<int>(vertices.size()) - 1 ? 0 : i + 1]->subtract(*vertices[i]);
    }

    bool borders(const Polygon& another) const {
        int len1 = static_cast<int>(vertices.size());
        int len2 = static_cast<int>(another.size());
        for (int i = 0; i < len1; ++i) {
            int j = another.indexOf(vertices[i]);
            if (j != -1) {
                PointPtr nextV = vertices[(i + 1) % len1];
                if (nextV == another[(j + 1) % len2] ||
                    nextV == another[(j + len2 - 1) % len2]) return true;
            }
        }
        return false;
    }

    Rectangle getBounds() const {
        Rectangle rect(vertices[0]->x, vertices[0]->y);
        for (const PointPtr& v : vertices) {
            rect.left = std::min(rect.left, v->x);
            rect.right = std::max(rect.right, v->x);
            rect.top = std::min(rect.top, v->y);
            rect.bottom = std::max(rect.bottom, v->y);
        }
        return rect;
    }

    std::vector<Polygon> split(PointPtr p1, PointPtr p2) const {
        return spliti(indexOf(p1), indexOf(p2));
    }

    std::vector<Polygon> spliti(int i1, int i2) const {
        if (i1 > i2) {
            std::swap(i1, i2);
        }

        std::vector<PointPtr> slice1 = slice(i1, i2 + 1);
        std::vector<PointPtr> slice2a = slice(i2);
        std::vector<PointPtr> slice2b = slice(0, i1 + 1);
        slice2a.insert(slice2a.end(), slice2b.begin(), slice2b.end());

        return {Polygon(slice1), Polygon(slice2a)};
    }

    // Cut the polygon along a line
    std::vector<Polygon> cut(const Point& p1, const Point& p2, float gap = 0.0f) const {
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
            PointPtr v0 = vertices[i];
            PointPtr v1i = vertices[(i + 1) % len];

            float x2 = v0->x;
            float y2 = v0->y;
            float dx2 = v1i->x - x2;
            float dy2 = v1i->y - y2;

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
            // Create shared intersection points
            PointPtr point1 = makePoint(x1 + dx1 * ratio1, y1 + dy1 * ratio1);
            PointPtr point2 = makePoint(x1 + dx1 * ratio2, y1 + dy1 * ratio2);

            // Build half1
            Polygon half1;
            std::vector<PointPtr> half1Pts = slice(edge1 + 1, edge2 + 1);
            for (const PointPtr& p : half1Pts) half1.push(p);
            half1.unshift(point1);
            half1.push(point2);

            // Build half2
            Polygon half2;
            std::vector<PointPtr> half2a = slice(edge2 + 1);
            std::vector<PointPtr> half2b = slice(0, edge1 + 1);
            for (const PointPtr& p : half2a) half2.push(p);
            for (const PointPtr& p : half2b) half2.push(p);
            half2.unshift(point2);
            half2.push(point1);

            // Handle gap if needed
            if (gap > 0) {
                half1 = half1.peelByIndex(half1.size() - 1, gap / 2);
                half2 = half2.peelByIndex(half2.size() - 1, gap / 2);
            }

            Point v = vectori(edge1);
            return GeomUtils::cross(dx1, dy1, v.x, v.y) > 0 ?
                std::vector<Polygon>{std::move(half1), std::move(half2)} :
                std::vector<Polygon>{std::move(half2), std::move(half1)};
        } else {
            return {Polygon(*this)};
        }
    }

    // Index-based peel
    Polygon peelByIndex(size_t i1, float d) const {
        size_t i2 = (i1 + 1) % vertices.size();
        PointPtr v1 = vertices[i1];
        PointPtr v2 = vertices[i2];

        Point v = v2->subtract(*v1);
        Point n = v.rotate90().norm(d);

        Point p1 = v1->add(n);
        Point p2 = v2->add(n);

        return cut(p1, p2, 0.0f)[0];
    }

    // A version of "shrink" function for insetting just one edge.
    Polygon peel(PointPtr v1, float d) const {
        int i1 = indexOf(v1);
        return peelByIndex(static_cast<size_t>(i1), d);
    }

    std::vector<float> interpolate(const Point& p) const {
        float sum = 0.0f;
        std::vector<float> dd;
        for (const PointPtr& v : vertices) {
            float d = 1.0f / Point::distance(*v, p);
            sum += d;
            dd.push_back(d);
        }
        std::vector<float> result;
        for (float d : dd) {
            result.push_back(d / sum);
        }
        return result;
    }

    // Get a random vertex
    PointPtr random() const {
        if (vertices.empty()) return nullptr;
        size_t idx = static_cast<size_t>(Random::getFloat() * static_cast<float>(vertices.size()));
        return vertices[idx];
    }

    // Find the vertex that minimizes the given function
    template<typename F>
    PointPtr min(F&& func) const {
        if (vertices.empty()) return nullptr;
        PointPtr result = vertices[0];
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
    PointPtr max(F&& func) const {
        if (vertices.empty()) return nullptr;
        PointPtr result = vertices[0];
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

    // Create a copy of this polygon (shares the same Points - reference semantics)
    Polygon copy() const {
        return Polygon(vertices);
    }

    // Shrinks the polygon by insetting each edge by the amount specified in d array.
    Polygon shrink(const std::vector<float>& d) const {
        Polygon q = *this;
        size_t i = 0;
        forEdge([&](PointPtr v1, PointPtr v2) {
            float dd = d[i++];
            if (dd > 0) {
                Point v = v2->subtract(*v1);
                Point n = v.rotate90().norm(dd);
                Point p1 = v1->add(n);
                Point p2 = v2->add(n);
                auto halves = q.cut(p1, p2, 0.0f);
                if (!halves.empty()) {
                    q = std::move(halves[0]);
                }
            }
        });
        return q;
    }

    // Shrink with initializer list
    Polygon shrink(std::initializer_list<float> d) const {
        return shrink(std::vector<float>(d));
    }

    // Buffer for concave polygons - handles self-intersections
    Polygon buffer(const std::vector<float>& d) const {
        // Create a polygon with offset edges
        Polygon q;
        size_t i = 0;
        forEdge([&](PointPtr v0, PointPtr v1) {
            float dd = d[i++];
            if (dd == 0) {
                // No offset - keep original points
                q.push(v0);
                q.push(v1);
            } else {
                // Offset the edge
                Point v = v1->subtract(*v0);
                Point n = v.rotate90().norm(dd);
                q.push(makePoint(v0->add(n)));
                q.push(makePoint(v1->add(n)));
            }
        });

        // Handle self-intersections by finding intersections
        bool wasCut;
        size_t lastEdge = 0;
        int maxIterations = 1000;
        do {
            wasCut = false;
            if (--maxIterations <= 0) break;
            size_t n = q.size();
            for (size_t edgeI = lastEdge; edgeI + 2 < n; ++edgeI) {
                lastEdge = edgeI;
                PointPtr p11 = q[edgeI];
                PointPtr p12 = q[edgeI + 1];
                float x1 = p11->x;
                float y1 = p11->y;
                float dx1 = p12->x - x1;
                float dy1 = p12->y - y1;

                size_t jEnd = (edgeI > 0) ? n : n - 1;
                for (size_t j = edgeI + 2; j < jEnd; ++j) {
                    PointPtr p21 = q[j];
                    PointPtr p22 = (j < n - 1) ? q[j + 1] : q[0];
                    float x2 = p21->x;
                    float y2 = p21->y;
                    float dx2 = p22->x - x2;
                    float dy2 = p22->y - y2;

                    auto inter = GeomUtils::intersectLines(x1, y1, dx1, dy1, x2, y2, dx2, dy2);
                    if (inter && inter->x > DELTA && inter->x < 1 - DELTA &&
                        inter->y > DELTA && inter->y < 1 - DELTA) {
                        PointPtr pn = makePoint(x1 + dx1 * inter->x, y1 + dy1 * inter->x);

                        q.insert(j + 1, pn);
                        q.insert(edgeI + 1, pn);

                        wasCut = true;
                        break;
                    }
                }
                if (wasCut) break;
            }
        } while (wasCut);

        // Find the largest polygon part
        std::vector<size_t> regular;
        for (size_t idx = 0; idx < q.size(); ++idx) regular.push_back(idx);

        Polygon bestPart;
        float bestPartSq = -std::numeric_limits<float>::infinity();

        size_t safetyOuter = q.size() * 2;
        while (!regular.empty() && safetyOuter-- > 0) {
            std::vector<size_t> indices;
            size_t start = regular[0];
            size_t curr = start;
            size_t safetyInner = q.size() * 2;
            do {
                indices.push_back(curr);
                auto it = std::find(regular.begin(), regular.end(), curr);
                if (it != regular.end()) regular.erase(it);

                size_t next = (curr + 1) % q.size();
                PointPtr v = q[next];
                int next1 = q.indexOf(v);
                if (next1 == static_cast<int>(next)) {
                    next1 = q.lastIndexOf(v);
                }
                curr = (next1 == -1) ? next : static_cast<size_t>(next1);
            } while (curr != start && !regular.empty() && --safetyInner > 0);

            // Build polygon from indices
            Polygon p;
            for (size_t idx : indices) {
                p.push(q[idx]);
            }
            float s = p.square();
            if (s > bestPartSq) {
                bestPart = std::move(p);
                bestPartSq = s;
            }
        }

        return bestPart;
    }

    // Shrinks all edges by the same distance
    Polygon shrinkEq(float d) const {
        std::vector<float> distances(vertices.size(), d);
        return shrink(distances);
    }

    // Smooths all vertices and returns a polygon with smoothed coordinates
    Polygon smoothVertexEq(float f = 1.0f) const {
        auto smoothedPts = smoothVertexEqValues(f);
        Polygon result;
        for (const Point& p : smoothedPts) {
            result.vertices.push_back(makePoint(p));
        }
        return result;
    }

    // Static factory methods for creating basic shapes

    // Creates a rectangle polygon centered at origin
    static Polygon rect(float width, float height) {
        float hw = width / 2.0f;
        float hh = height / 2.0f;
        return Polygon({
            Point(-hw, -hh),
            Point(hw, -hh),
            Point(hw, hh),
            Point(-hw, hh)
        });
    }

    // Creates a circular polygon approximation centered at origin
    static Polygon circle(float radius, int segments = 16) {
        Polygon result;
        for (int i = 0; i < segments; ++i) {
            float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(segments);
            result.vertices.push_back(makePoint(
                radius * std::cos(angle),
                radius * std::sin(angle)
            ));
        }
        return result;
    }
};

} // namespace town
