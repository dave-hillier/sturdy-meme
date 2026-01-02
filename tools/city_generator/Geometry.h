// Core geometry types for city generation
// Ported from watabou's Medieval Fantasy City Generator

#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>

namespace city {

// 2D Vector/Point
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }

    bool operator==(const Vec2& other) const {
        return std::abs(x - other.x) < 1e-6f && std::abs(y - other.y) < 1e-6f;
    }
    bool operator!=(const Vec2& other) const { return !(*this == other); }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }

    Vec2 normalized() const {
        float len = length();
        if (len < 1e-6f) return {0, 0};
        return {x / len, y / len};
    }

    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
    float cross(const Vec2& other) const { return x * other.y - y * other.x; }

    // Perpendicular vector (rotated 90 degrees)
    Vec2 perpendicular() const { return {-y, x}; }

    // Rotate by angle in radians
    Vec2 rotated(float angle) const {
        float c = std::cos(angle);
        float s = std::sin(angle);
        return {x * c - y * s, x * s + y * c};
    }

    static float distance(const Vec2& a, const Vec2& b) {
        return (a - b).length();
    }

    static Vec2 lerp(const Vec2& a, const Vec2& b, float t) {
        return a + (b - a) * t;
    }
};

// Axis-aligned bounding box
struct AABB {
    Vec2 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    void expand(const Vec2& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
    }

    Vec2 center() const { return (min + max) * 0.5f; }
    Vec2 size() const { return max - min; }

    bool contains(const Vec2& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
};

// Line segment
struct Segment {
    Vec2 start;
    Vec2 end;

    Segment() = default;
    Segment(const Vec2& s, const Vec2& e) : start(s), end(e) {}

    float length() const { return Vec2::distance(start, end); }
    Vec2 direction() const { return (end - start).normalized(); }
    Vec2 midpoint() const { return (start + end) * 0.5f; }

    // Distance from point to segment
    float distanceToPoint(const Vec2& p) const {
        Vec2 d = end - start;
        float len2 = d.lengthSquared();
        if (len2 < 1e-10f) return Vec2::distance(p, start);

        float t = std::clamp(((p - start).dot(d)) / len2, 0.0f, 1.0f);
        Vec2 projection = start + d * t;
        return Vec2::distance(p, projection);
    }

    // Intersection with another segment
    std::optional<Vec2> intersect(const Segment& other) const {
        Vec2 r = end - start;
        Vec2 s = other.end - other.start;
        float rxs = r.cross(s);

        if (std::abs(rxs) < 1e-10f) return std::nullopt; // Parallel

        Vec2 qp = other.start - start;
        float t = qp.cross(s) / rxs;
        float u = qp.cross(r) / rxs;

        if (t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f) {
            return start + r * t;
        }
        return std::nullopt;
    }
};

// Polygon (closed shape with vertices)
class Polygon {
public:
    std::vector<Vec2> vertices;

    Polygon() = default;
    Polygon(std::vector<Vec2> verts) : vertices(std::move(verts)) {}

    size_t size() const { return vertices.size(); }
    bool empty() const { return vertices.empty(); }

    Vec2& operator[](size_t i) { return vertices[i]; }
    const Vec2& operator[](size_t i) const { return vertices[i]; }

    // Signed area (positive = counter-clockwise)
    float signedArea() const {
        if (vertices.size() < 3) return 0.0f;
        float area = 0.0f;
        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            area += vertices[i].x * vertices[j].y;
            area -= vertices[j].x * vertices[i].y;
        }
        return area * 0.5f;
    }

    float area() const { return std::abs(signedArea()); }

    float perimeter() const {
        float p = 0.0f;
        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            p += Vec2::distance(vertices[i], vertices[j]);
        }
        return p;
    }

    // Compactness: how close to a circle (1.0 = perfect circle)
    float compactness() const {
        float p = perimeter();
        if (p < 1e-6f) return 0.0f;
        return 4.0f * 3.14159265f * area() / (p * p);
    }

    // Centroid of the polygon
    Vec2 centroid() const {
        if (vertices.empty()) return {0, 0};
        if (vertices.size() == 1) return vertices[0];
        if (vertices.size() == 2) return (vertices[0] + vertices[1]) * 0.5f;

        Vec2 c{0, 0};
        float a = 0.0f;

        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            float cross = vertices[i].x * vertices[j].y - vertices[j].x * vertices[i].y;
            a += cross;
            c.x += (vertices[i].x + vertices[j].x) * cross;
            c.y += (vertices[i].y + vertices[j].y) * cross;
        }

        a *= 0.5f;
        if (std::abs(a) < 1e-10f) {
            // Degenerate polygon, return simple average
            for (const auto& v : vertices) c += v;
            return c / static_cast<float>(vertices.size());
        }

        c /= (6.0f * a);
        return c;
    }

    // Simple center (average of vertices)
    Vec2 center() const {
        if (vertices.empty()) return {0, 0};
        Vec2 c{0, 0};
        for (const auto& v : vertices) c += v;
        return c / static_cast<float>(vertices.size());
    }

    // Get bounding box
    AABB bounds() const {
        AABB bb;
        for (const auto& v : vertices) bb.expand(v);
        return bb;
    }

    // Check if point is inside polygon (ray casting)
    bool contains(const Vec2& p) const {
        if (vertices.size() < 3) return false;

        int crossings = 0;
        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            const Vec2& v0 = vertices[i];
            const Vec2& v1 = vertices[j];

            if ((v0.y <= p.y && v1.y > p.y) || (v1.y <= p.y && v0.y > p.y)) {
                float t = (p.y - v0.y) / (v1.y - v0.y);
                if (p.x < v0.x + t * (v1.x - v0.x)) {
                    crossings++;
                }
            }
        }
        return (crossings % 2) == 1;
    }

    // Check if polygon is convex
    bool isConvex() const {
        if (vertices.size() < 3) return false;

        bool hasPositive = false;
        bool hasNegative = false;

        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            size_t k = (i + 2) % vertices.size();

            Vec2 d1 = vertices[j] - vertices[i];
            Vec2 d2 = vertices[k] - vertices[j];
            float cross = d1.cross(d2);

            if (cross > 0) hasPositive = true;
            if (cross < 0) hasNegative = true;

            if (hasPositive && hasNegative) return false;
        }
        return true;
    }

    // Inset (shrink) polygon by distance
    Polygon inset(float distance) const {
        if (vertices.size() < 3) return *this;

        std::vector<Vec2> result;
        result.reserve(vertices.size());

        for (size_t i = 0; i < vertices.size(); i++) {
            size_t prev = (i + vertices.size() - 1) % vertices.size();
            size_t next = (i + 1) % vertices.size();

            Vec2 d1 = (vertices[i] - vertices[prev]).normalized();
            Vec2 d2 = (vertices[next] - vertices[i]).normalized();

            Vec2 n1 = d1.perpendicular();
            Vec2 n2 = d2.perpendicular();

            // Make sure normals point inward (for CCW polygon)
            if (signedArea() < 0) {
                n1 = n1 * -1.0f;
                n2 = n2 * -1.0f;
            }

            // Bisector
            Vec2 bisector = (n1 + n2).normalized();
            float angle = std::acos(std::clamp(n1.dot(n2), -1.0f, 1.0f));
            float scale = 1.0f / std::cos(angle * 0.5f);

            result.push_back(vertices[i] + bisector * distance * scale);
        }

        return Polygon(result);
    }

    // Offset (grow) polygon by distance
    Polygon offset(float distance) const {
        return inset(-distance);
    }

    // Smooth vertex by averaging with neighbors
    void smoothVertices(float factor = 0.5f) {
        if (vertices.size() < 3) return;

        std::vector<Vec2> smoothed;
        smoothed.reserve(vertices.size());

        for (size_t i = 0; i < vertices.size(); i++) {
            size_t prev = (i + vertices.size() - 1) % vertices.size();
            size_t next = (i + 1) % vertices.size();

            Vec2 avg = (vertices[prev] + vertices[next]) * 0.5f;
            smoothed.push_back(Vec2::lerp(vertices[i], avg, factor));
        }

        vertices = std::move(smoothed);
    }

    // Remove short edges
    void filterShortEdges(float minLength) {
        if (vertices.size() < 3) return;

        std::vector<Vec2> filtered;
        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            if (Vec2::distance(vertices[i], vertices[j]) >= minLength) {
                filtered.push_back(vertices[i]);
            }
        }

        if (filtered.size() >= 3) {
            vertices = std::move(filtered);
        }
    }

    // Find longest edge and return its midpoint with edge index
    std::pair<size_t, Vec2> findLongestEdge() const {
        size_t bestIdx = 0;
        float maxLen = 0.0f;

        for (size_t i = 0; i < vertices.size(); i++) {
            size_t j = (i + 1) % vertices.size();
            float len = Vec2::distance(vertices[i], vertices[j]);
            if (len > maxLen) {
                maxLen = len;
                bestIdx = i;
            }
        }

        size_t j = (bestIdx + 1) % vertices.size();
        return {bestIdx, (vertices[bestIdx] + vertices[j]) * 0.5f};
    }

    // Create rectangle
    static Polygon rect(float x, float y, float w, float h) {
        return Polygon({
            {x, y},
            {x + w, y},
            {x + w, y + h},
            {x, y + h}
        });
    }

    // Create regular polygon
    static Polygon regular(int sides, float radius, Vec2 center = {0, 0}) {
        std::vector<Vec2> verts;
        verts.reserve(sides);
        for (int i = 0; i < sides; i++) {
            float angle = 2.0f * 3.14159265f * i / sides - 3.14159265f * 0.5f;
            verts.push_back(center + Vec2{std::cos(angle), std::sin(angle)} * radius);
        }
        return Polygon(verts);
    }
};

// Circle
struct Circle {
    Vec2 center;
    float radius = 0.0f;

    Circle() = default;
    Circle(const Vec2& c, float r) : center(c), radius(r) {}

    bool contains(const Vec2& p) const {
        return Vec2::distance(center, p) <= radius;
    }

    // Circumcircle of triangle
    static Circle circumcircle(const Vec2& p1, const Vec2& p2, const Vec2& p3) {
        float ax = p1.x, ay = p1.y;
        float bx = p2.x, by = p2.y;
        float cx = p3.x, cy = p3.y;

        float d = 2.0f * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (std::abs(d) < 1e-10f) {
            // Degenerate triangle
            Vec2 center = (p1 + p2 + p3) / 3.0f;
            float r = std::max({Vec2::distance(center, p1),
                               Vec2::distance(center, p2),
                               Vec2::distance(center, p3)});
            return {center, r};
        }

        float ux = ((ax*ax + ay*ay) * (by - cy) + (bx*bx + by*by) * (cy - ay) + (cx*cx + cy*cy) * (ay - by)) / d;
        float uy = ((ax*ax + ay*ay) * (cx - bx) + (bx*bx + by*by) * (ax - cx) + (cx*cx + cy*cy) * (bx - ax)) / d;

        Vec2 center{ux, uy};
        return {center, Vec2::distance(center, p1)};
    }
};

} // namespace city
