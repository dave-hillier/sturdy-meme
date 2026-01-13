#pragma once

#include "town_generator/geom/Point.h"
#include <vector>

namespace town_generator {
namespace geom {

/**
 * PolyBool - Boolean operations on polygons for non-convex shapes
 *
 * Faithful port from mfcg.js PolyBool.
 * Uses edge intersection and boundary tracing algorithm that handles
 * non-convex polygons correctly (unlike Sutherland-Hodgman).
 */
class PolyBool {
public:
    /**
     * Compute intersection (AND) of two polygons
     *
     * @param polyA First polygon
     * @param polyB Second polygon
     * @param returnA If no intersection, return polyA instead of empty
     * @return Intersection polygon, or empty if no intersection
     */
    static std::vector<Point> polygonAnd(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB,
        bool returnA = false
    );

    /**
     * Compute subtraction (A - B) of two polygons
     *
     * @param polyA First polygon (subject)
     * @param polyB Second polygon (clip)
     * @return Result of A - B, or polyA if B doesn't overlap
     */
    static std::vector<Point> polygonSubtract(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB
    );

private:
    /**
     * Augment two polygons by finding all intersection points
     * and inserting them into both polygons
     *
     * @param polyA First polygon
     * @param polyB Second polygon
     * @return Pair of augmented polygons with intersection points
     */
    static std::pair<std::vector<Point>, std::vector<Point>> augmentPolygons(
        const std::vector<Point>& polyA,
        const std::vector<Point>& polyB
    );

    /**
     * Check if two points are approximately equal
     */
    static bool pointsEqual(const Point& a, const Point& b, double epsilon = 0.0001);

    /**
     * Find index of a point in polygon (with tolerance)
     * @return Index or -1 if not found
     */
    static int findPointIndex(const std::vector<Point>& poly, const Point& point);

    /**
     * Check if point is inside polygon using ray casting
     */
    static bool containsPoint(const std::vector<Point>& poly, const Point& p, bool excludeBoundary = false);
};

} // namespace geom
} // namespace town_generator
