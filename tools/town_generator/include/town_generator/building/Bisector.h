#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include <vector>
#include <functional>

namespace town_generator {
namespace building {

/**
 * Bisector - Recursive polygon partitioning
 *
 * Faithful port from mfcg.js ji (Bisector) class (lines 19472-19640).
 * Partitions a polygon into smaller pieces using recursive bisection.
 */
class Bisector {
public:
    // The polygon to partition
    geom::Polygon poly;

    // Minimum area threshold for stopping recursion
    double minArea;

    // Variance factor for area threshold (pow(variance, |random|))
    double variance;

    // Minimum offset from edge ends for cut points
    double minOffset;

    // Minimum turn offset for cut ratio (0-0.5)
    double minTurnOffset = 1.0;

    // Callback for gap size at each cut (nullptr = 0 gap)
    std::function<double(const std::vector<geom::Point>&)> getGap = nullptr;

    // Callback for processing cut corners (e.g., semiSmooth)
    std::function<std::vector<geom::Point>(const std::vector<geom::Point>&)> processCut = nullptr;

    // Callback for checking if a polygon is atomic (can't be further subdivided)
    std::function<bool(const geom::Polygon&)> isAtomic = nullptr;

    // All cuts made during partitioning (for alley rendering)
    std::vector<std::vector<geom::Point>> cuts;

    // Constructor
    // minArea: minimum partition area
    // variance: variation factor (default 10)
    Bisector(const geom::Polygon& poly, double minArea, double variance = 10.0);

    // Partition the polygon
    // Returns list of partitioned polygons
    std::vector<geom::Polygon> partition();

private:
    // Recursive subdivision
    std::vector<geom::Polygon> subdivide(const geom::Polygon& shape);

    // Default isAtomic check: area < minArea * pow(variance, random)
    bool isSmallEnough(const geom::Polygon& shape);

    // Make a cut in the polygon
    // Returns list of halves (may return [original] if cut fails)
    std::vector<geom::Polygon> makeCut(const geom::Polygon& shape, int attempt = 0);

    // Split polygon along a cut line
    std::vector<geom::Polygon> split(
        const geom::Polygon& shape,
        int edge1,
        int edge2,
        const std::vector<geom::Point>& cutLine
    );

    // Default processCut: detect straight lines (no modification)
    std::vector<geom::Point> detectStraight(const std::vector<geom::Point>& pts);
};

} // namespace building
} // namespace town_generator
