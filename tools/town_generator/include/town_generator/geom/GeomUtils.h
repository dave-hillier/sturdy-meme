#pragma once

#include "town_generator/geom/Point.h"
#include <cmath>
#include <optional>
#include <vector>

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
     * Linear interpolation (lerp) - alias for interpolate
     * Faithful to mfcg.js qa.lerp
     */
    static Point lerp(const Point& p1, const Point& p2, double t = 0.5) {
        return Point(p1.x + (p2.x - p1.x) * t, p1.y + (p2.y - p1.y) * t);
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

    /**
     * Signed area of triangle formed by three points
     * Positive if CCW, negative if CW
     */
    static double triangleArea(const Point& p0, const Point& p1, const Point& p2) {
        return 0.5 * ((p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y));
    }

    /**
     * Largest Inscribed Rectangle (LIR) aligned with a given edge
     * Based on mfcg.js Gb.lir algorithm
     * Returns 4 corners of the largest rectangle that fits inside the polygon
     * aligned to the edge starting at vertex index edgeIdx
     */
    static std::vector<Point> lir(const std::vector<Point>& poly, size_t edgeIdx);

    /**
     * Largest Inscribed Rectangle Axis-aligned (LIRA)
     * Based on mfcg.js Gb.lira algorithm
     * Tries lir for each edge and returns the one with largest area
     */
    static std::vector<Point> lira(const std::vector<Point>& poly);

    /**
     * Rotate points by angle
     */
    static std::vector<Point> rotatePoints(const std::vector<Point>& pts, double angle);

    /**
     * Calculate polygon area
     */
    static double polygonArea(const std::vector<Point>& poly);

    /**
     * Polygon intersection (AND operation)
     * Based on mfcg.js ye.and (PolyBool)
     * Returns the intersection of two polygons, or empty if no intersection
     *
     * @param polyA First polygon
     * @param polyB Second polygon
     * @param subtract If true, return A - B instead of A & B
     * @return Intersection polygon
     */
    static std::vector<Point> polygonIntersection(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB,
        bool subtract = false
    );

    /**
     * Check if point is inside polygon
     * Based on mfcg.js Gb.containsPoint
     */
    static bool containsPoint(const std::vector<Point>& poly, const Point& p, bool excludeBoundary = false);

    /**
     * Create a stripe polygon from a polyline
     * Based on mfcg.js Qd.stripe
     *
     * @param line The polyline
     * @param width The stripe width
     * @param capExtend How much to extend caps at ends (0-1)
     * @return A polygon representing the stripe
     */
    static std::vector<Point> stripe(
        const std::vector<Point>& line,
        double width,
        double capExtend = 1.0
    );

    /**
     * Circle passing through points with tangent directions
     * Based on mfcg.js Qe.getCircle
     */
    struct Circle {
        Point c;
        double r;
    };
    static Circle getCircle(const Point& p0, const Point& dir0, const Point& p1, const Point& dir1);

    /**
     * Generate arc points between two angles
     * Based on mfcg.js Qe.getArc
     *
     * @param circle The circle (center and radius)
     * @param startAngle Start angle in radians
     * @param endAngle End angle in radians
     * @param numSegments Number of segments for approximation
     * @return Points along the arc
     */
    static std::vector<Point> getArc(
        const Circle& circle,
        double startAngle,
        double endAngle,
        int numSegments = 4
    );

    /**
     * Translate polygon by offset
     */
    static std::vector<Point> translate(const std::vector<Point>& poly, double dx, double dy);

    /**
     * Reverse polygon winding
     */
    static std::vector<Point> reverse(const std::vector<Point>& poly);

    /**
     * Shrink polygon edges inward by varying amounts
     * Based on mfcg.js gd.shrink
     *
     * @param poly The polygon to shrink
     * @param amounts Shrink amounts for each edge (same length as poly)
     * @return The shrunk polygon
     */
    static std::vector<Point> shrink(const std::vector<Point>& poly, const std::vector<double>& amounts);

    /**
     * Fill an area with points using a grid pattern
     * Based on mfcg.js Ae.fillArea
     *
     * @param poly The polygon to fill
     * @param density Fill density (0-1), higher = more points
     * @param spacing Grid spacing (default 3.0)
     * @return Vector of points inside the polygon
     */
    static std::vector<Point> fillArea(const std::vector<Point>& poly, double density, double spacing = 3.0);

    // Equality (stateless utility class)
    bool operator==(const GeomUtils& other) const { return true; }
    bool operator!=(const GeomUtils& other) const { return false; }
};

} // namespace geom
} // namespace town_generator
