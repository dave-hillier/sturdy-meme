#pragma once

namespace town_generator2 {
namespace geom {

/**
 * Circle - Simple circle defined by center and radius
 */
struct Circle {
    double x = 0;
    double y = 0;
    double r = 0;

    Circle() = default;
    Circle(double x_, double y_, double r_) : x(x_), y(y_), r(r_) {}
};

} // namespace geom
} // namespace town_generator2
