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

namespace town {

class Point {
public:
    float x;
    float y;

    // Constructors
    Point() : x(0.0f), y(0.0f) {}
    Point(float x, float y) : x(x), y(y) {}
    Point(const Point& other) : x(other.x), y(other.y) {}

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

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
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

} // namespace town
