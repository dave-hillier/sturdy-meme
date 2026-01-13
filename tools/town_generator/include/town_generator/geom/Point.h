#pragma once

#include <cmath>
#include <functional>

namespace town_generator {
namespace geom {

/**
 * 2D Point class - replaces openfl.geom.Point
 * Faithful port from Haxe TownGeneratorOS
 */
class Point {
public:
    double x;
    double y;

    Point() : x(0), y(0) {}
    Point(double x, double y) : x(x), y(y) {}

    // Equality operators (coordinate-based, not reference-based)
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
    }

    // Approximate equality for floating point comparisons
    bool equals(const Point& other, double epsilon = 1e-9) const {
        return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon;
    }

    // Copy/clone
    Point clone() const {
        return Point(x, y);
    }

    // In-place modification (Haxe's point.set(other))
    void set(const Point& other) {
        x = other.x;
        y = other.y;
    }

    void setTo(double newX, double newY) {
        x = newX;
        y = newY;
    }

    // Offset in place
    void offset(double dx, double dy) {
        x += dx;
        y += dy;
    }

    // Arithmetic operations (return new Point)
    Point add(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    Point subtract(const Point& other) const {
        return Point(x - other.x, y - other.y);
    }

    Point scale(double f) const {
        return Point(x * f, y * f);
    }

    // In-place arithmetic (from PointExtender)
    void addEq(const Point& other) {
        x += other.x;
        y += other.y;
    }

    void subEq(const Point& other) {
        x -= other.x;
        y -= other.y;
    }

    void scaleEq(double f) {
        x *= f;
        y *= f;
    }

    // Length/distance
    double length() const {
        return std::sqrt(x * x + y * y);
    }

    static double distance(const Point& p1, const Point& p2) {
        double dx = p2.x - p1.x;
        double dy = p2.y - p1.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Midpoint between two points
    static Point midpoint(const Point& p1, const Point& p2) {
        return Point((p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0);
    }

    // Normalize (in-place and returning new)
    void normalize(double len = 1.0) {
        double l = length();
        if (l > 0) {
            x = x / l * len;
            y = y / l * len;
        }
    }

    Point norm(double len = 1.0) const {
        Point p = clone();
        p.normalize(len);
        return p;
    }

    // Rotate 90 degrees counterclockwise
    Point rotate90() const {
        return Point(-y, x);
    }

    // Dot product
    double dot(const Point& other) const {
        return x * other.x + y * other.y;
    }

    // Angle (atan2)
    double atan() const {
        return std::atan2(y, x);
    }

    // Operator overloads for convenience
    Point operator+(const Point& other) const { return add(other); }
    Point operator-(const Point& other) const { return subtract(other); }
    Point operator*(double f) const { return scale(f); }
    Point& operator+=(const Point& other) { addEq(other); return *this; }
    Point& operator-=(const Point& other) { subEq(other); return *this; }
    Point& operator*=(double f) { scaleEq(f); return *this; }
};

} // namespace geom
} // namespace town_generator

// Hash function for use in unordered containers
namespace std {
    template<>
    struct hash<town_generator::geom::Point> {
        size_t operator()(const town_generator::geom::Point& p) const {
            size_t h1 = hash<double>{}(p.x);
            size_t h2 = hash<double>{}(p.y);
            return h1 ^ (h2 << 1);
        }
    };
}
