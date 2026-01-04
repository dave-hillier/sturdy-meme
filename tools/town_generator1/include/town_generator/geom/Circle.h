#pragma once

namespace town_generator {
namespace geom {

/**
 * Circle - Simple circle representation, faithful port from Haxe TownGeneratorOS
 */
class Circle {
public:
    double x;
    double y;
    double r;

    Circle() : x(0), y(0), r(0) {}
    Circle(double x, double y, double r) : x(x), y(y), r(r) {}

    // Equality operators
    bool operator==(const Circle& other) const {
        return x == other.x && y == other.y && r == other.r;
    }

    bool operator!=(const Circle& other) const {
        return !(*this == other);
    }
};

} // namespace geom
} // namespace town_generator
