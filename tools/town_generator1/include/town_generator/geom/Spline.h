#pragma once

#include "town_generator/geom/Point.h"
#include <vector>

namespace town_generator {
namespace geom {

/**
 * Spline - Bezier spline utilities, faithful port from Haxe TownGeneratorOS
 */
class Spline {
public:
    static constexpr double curvature = 0.1;

    // Start curve: returns [control, p1]
    static std::vector<Point> startCurve(const Point& p0, const Point& p1, const Point& p2) {
        Point tangent = p2.subtract(p0);
        Point control = p1.subtract(tangent.scale(curvature));
        return {control, p1};
    }

    // End curve: returns [control, p2]
    static std::vector<Point> endCurve(const Point& p0, const Point& p1, const Point& p2) {
        Point tangent = p2.subtract(p0);
        Point control = p1.add(tangent.scale(curvature));
        return {control, p2};
    }

    // Mid curve: returns [p1a, p12, p2a, p2]
    static std::vector<Point> midCurve(const Point& p0, const Point& p1, const Point& p2, const Point& p3) {
        Point tangent1 = p2.subtract(p0);
        Point tangent2 = p3.subtract(p1);

        Point p1a = p1.add(tangent1.scale(curvature));
        Point p2a = p2.subtract(tangent2.scale(curvature));
        Point p12 = p1a.add(p2a).scale(0.5);

        return {p1a, p12, p2a, p2};
    }

    // Equality (stateless utility class)
    bool operator==(const Spline& other) const { return true; }
    bool operator!=(const Spline& other) const { return false; }
};

} // namespace geom
} // namespace town_generator
