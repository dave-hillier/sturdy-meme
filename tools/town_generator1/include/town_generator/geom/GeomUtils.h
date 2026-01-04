#pragma once

#include "town_generator/geom/Point.h"
#include <cmath>
#include <optional>

namespace town_generator {
namespace geom {

/**
 * GeomUtils - Static geometry helpers, faithful port from Haxe TownGeneratorOS
 */
class GeomUtils {
public:
    /**
     * Find intersection of two lines defined by point + direction vector
     * Returns parametric t values (t1, t2) as a Point, or nullopt if parallel
     */
    static std::optional<Point> intersectLines(
        double x1, double y1, double dx1, double dy1,
        double x2, double y2, double dx2, double dy2
    ) {
        double d = dx1 * dy2 - dy1 * dx2;
        if (d == 0) {
            return std::nullopt;
        }

        double t2 = (dy1 * (x2 - x1) - dx1 * (y2 - y1)) / d;
        double t1;
        if (dx1 != 0) {
            t1 = (x2 - x1 + dx2 * t2) / dx1;
        } else {
            t1 = (y2 - y1 + dy2 * t2) / dy1;
        }

        return Point(t1, t2);
    }

    /**
     * Interpolate between two points
     */
    static Point interpolate(const Point& p1, const Point& p2, double ratio = 0.5) {
        Point d = p2.subtract(p1);
        return Point(p1.x + d.x * ratio, p1.y + d.y * ratio);
    }

    /**
     * Scalar (dot) product of two 2D vectors
     */
    static double scalar(double x1, double y1, double x2, double y2) {
        return x1 * x2 + y1 * y2;
    }

    /**
     * Cross product (z-component) of two 2D vectors
     */
    static double cross(double x1, double y1, double x2, double y2) {
        return x1 * y2 - y1 * x2;
    }

    /**
     * Distance from a point to a line
     */
    static double distance2line(double x1, double y1, double dx1, double dy1, double x0, double y0) {
        return (dx1 * y0 - dy1 * x0 + (y1 + dy1) * x1 - (x1 + dx1) * y1) /
               std::sqrt(dx1 * dx1 + dy1 * dy1);
    }

    // Equality (stateless utility class)
    bool operator==(const GeomUtils& other) const { return true; }
    bool operator!=(const GeomUtils& other) const { return false; }
};

} // namespace geom
} // namespace town_generator
