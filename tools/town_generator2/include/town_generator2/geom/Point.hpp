#pragma once

#include <cmath>
#include <memory>
#include <vector>

namespace town_generator2 {
namespace geom {

/**
 * Point - 2D point with mutation methods for reference semantics.
 *
 * In the original Haxe code, Point is a reference type - when multiple
 * structures share the same Point instance, mutations are visible to all.
 * We use shared_ptr<Point> throughout to replicate this behavior.
 */
struct Point {
    double x = 0.0;
    double y = 0.0;

    Point() = default;
    Point(double x_, double y_) : x(x_), y(y_) {}

    // Immutable operations - return new Point
    Point add(const Point& p) const { return Point(x + p.x, y + p.y); }
    Point subtract(const Point& p) const { return Point(x - p.x, y - p.y); }
    Point scale(double f) const { return Point(x * f, y * f); }
    Point rotate90() const { return Point(-y, x); }

    Point clone() const { return Point(x, y); }

    // In-place mutation operations (like Haxe's addEq, scaleEq, etc.)
    void addEq(const Point& p) { x += p.x; y += p.y; }
    void subEq(const Point& p) { x -= p.x; y -= p.y; }
    void scaleEq(double f) { x *= f; y *= f; }

    void set(const Point& p) { x = p.x; y = p.y; }
    void setTo(double x_, double y_) { x = x_; y = y_; }
    void offset(double dx, double dy) { x += dx; y += dy; }

    // Properties
    double length() const { return std::sqrt(x * x + y * y); }
    double lengthSquared() const { return x * x + y * y; }

    // Normalize in-place
    void normalize(double len = 1.0) {
        double l = length();
        if (l > 0) {
            double scale = len / l;
            x *= scale;
            y *= scale;
        }
    }

    // Return normalized copy
    Point norm(double len = 1.0) const {
        Point p = clone();
        p.normalize(len);
        return p;
    }

    // Angle
    double atan() const { return std::atan2(y, x); }

    // Dot product
    double dot(const Point& p) const { return x * p.x + y * p.y; }

    // Static distance
    static double distance(const Point& a, const Point& b) {
        double dx = b.x - a.x;
        double dy = b.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    static double distanceSquared(const Point& a, const Point& b) {
        double dx = b.x - a.x;
        double dy = b.y - a.y;
        return dx * dx + dy * dy;
    }

    // Comparison (value equality)
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
    bool operator!=(const Point& other) const { return !(*this == other); }

    // Approximate equality
    bool equals(const Point& other, double epsilon = 1e-9) const {
        return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon;
    }
};

// Shared pointer type for reference semantics
using PointPtr = std::shared_ptr<Point>;
using PointList = std::vector<PointPtr>;

// Factory functions
inline PointPtr makePoint(double x = 0, double y = 0) {
    return std::make_shared<Point>(x, y);
}

inline PointPtr makePoint(const Point& p) {
    return std::make_shared<Point>(p.x, p.y);
}

} // namespace geom
} // namespace town_generator2
