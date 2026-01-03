/**
 * Ported from: Source/com/watabou/utils/PointExtender.hx
 *              and openfl.geom.Point
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include <cmath>
#include <memory>

namespace town {

// Forward declaration
class Point;

// Use shared_ptr for Point to match Haxe's reference semantics
using PointPtr = std::shared_ptr<Point>;

class Point {
private:
    static inline uint64_t nextId_ = 0;
    uint64_t id_;

public:
    float x;
    float y;

    // Constructors - each Point gets a unique ID for deterministic hashing
    Point() : id_(nextId_++), x(0.0f), y(0.0f) {}
    Point(float x, float y) : id_(nextId_++), x(x), y(y) {}
    Point(const Point& other) : id_(nextId_++), x(other.x), y(other.y) {}

    // Get unique ID for this point (used for deterministic hashing)
    uint64_t getId() const { return id_; }

    // Assignment
    Point& operator=(const Point& other) {
        x = other.x;
        y = other.y;
        return *this;
    }

    // Operators
    Point operator+(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    Point operator-(const Point& other) const {
        return Point(x - other.x, y - other.y);
    }

    Point operator*(float scalar) const {
        return Point(x * scalar, y * scalar);
    }

    // NOTE: operator== and operator!= intentionally removed.
    // In the original Haxe code, Point is a reference type and == compares identity.
    // Use valuesEqual() for explicit coordinate comparison when needed.

    static bool valuesEqual(const Point& a, const Point& b) {
        return a.x == b.x && a.y == b.y;
    }

    // OpenFL Point methods
    Point clone() const {
        return Point(x, y);
    }

    float getLength() const {
        return std::sqrt(x * x + y * y);
    }

    // Property-style accessor (Haxe uses .length)
    float length() const { return getLength(); }

    void normalize(float length = 1.0f) {
        float len = getLength();
        if (len != 0) {
            float ratio = length / len;
            x *= ratio;
            y *= ratio;
        }
    }

    Point subtract(const Point& other) const {
        return Point(x - other.x, y - other.y);
    }

    Point add(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    void offset(float dx, float dy) {
        x += dx;
        y += dy;
    }

    void setTo(float newX, float newY) {
        x = newX;
        y = newY;
    }

    // Static methods from OpenFL
    static float distance(const Point& p1, const Point& p2) {
        float dx = p2.x - p1.x;
        float dy = p2.y - p1.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Methods from PointExtender.hx
    void set(const Point& q) {
        x = q.x;
        y = q.y;
    }

    Point scale(float f) const {
        return Point(x * f, y * f);
    }

    Point norm(float length = 1.0f) const {
        Point p = clone();
        p.normalize(length);
        return p;
    }

    void addEq(const Point& q) {
        x += q.x;
        y += q.y;
    }

    void subEq(const Point& q) {
        x -= q.x;
        y -= q.y;
    }

    void scaleEq(float f) {
        x *= f;
        y *= f;
    }

    float atan() const {
        return std::atan2(y, x);
    }

    float dot(const Point& other) const {
        return x * other.x + y * other.y;
    }

    Point rotate90() const {
        return Point(-y, x);
    }
};

// Allow scalar * Point as well as Point * scalar
inline Point operator*(float scalar, const Point& p) {
    return Point(p.x * scalar, p.y * scalar);
}

// Factory functions for creating shared Points
inline PointPtr makePoint() {
    return std::make_shared<Point>();
}

inline PointPtr makePoint(float x, float y) {
    return std::make_shared<Point>(x, y);
}

inline PointPtr makePoint(const Point& p) {
    return std::make_shared<Point>(p);
}

// Explicit identity comparison (pointer equality) - matches Haxe reference semantics
inline bool sameIdentity(const PointPtr& a, const PointPtr& b) {
    return a.get() == b.get();
}

// Explicit coordinate comparison
inline bool sameCoordinates(const PointPtr& a, const PointPtr& b) {
    return Point::valuesEqual(*a, *b);
}

} // namespace town
