#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include <vector>
#include <functional>

namespace town_generator {
namespace utils {

/**
 * Bisector - Recursive polygon subdivision for city block generation
 *
 * Faithful port from mfcg.js Bisector (com.watabou.mfcg.utils.Bisector).
 * Recursively bisects a polygon into smaller blocks by cutting perpendicular
 * to the longest axis of the oriented bounding box.
 */
class Bisector {
public:
    // The polygon to subdivide
    std::vector<geom::Point> poly;

    // Minimum area threshold for stopping subdivision
    double minArea;

    // Variance factor for random size variation
    double variance;

    // Minimum offset from edge (sqrt(minArea))
    double minOffset;

    // Minimum turn offset for detecting straight cuts
    double minTurnOffset = 1.0;

    // Recorded cuts (alleys)
    std::vector<std::vector<geom::Point>> cuts;

    // Optional callback to get gap width for a cut
    std::function<double(const std::vector<geom::Point>&)> getGap;

    // Optional callback to process cut line (e.g., smooth curves)
    std::function<std::vector<geom::Point>(const std::vector<geom::Point>&)> processCut;

    // Optional callback to check if polygon is atomic (shouldn't be subdivided)
    std::function<bool(const std::vector<geom::Point>&)> isAtomic;

    /**
     * Constructor
     * @param poly The polygon to subdivide
     * @param minArea Minimum area threshold
     * @param variance Variance factor (default 10)
     */
    Bisector(const std::vector<geom::Point>& poly, double minArea, double variance = 10.0);

    /**
     * Partition the polygon into blocks
     * @return Vector of block polygons
     */
    std::vector<std::vector<geom::Point>> partition();

private:
    /**
     * Recursively subdivide a polygon
     * @param poly The polygon to subdivide
     * @return Vector of subdivided polygons
     */
    std::vector<std::vector<geom::Point>> subdivide(const std::vector<geom::Point>& poly);

    /**
     * Check if polygon is small enough (default isAtomic implementation)
     * @param poly The polygon to check
     * @return true if polygon area is below threshold
     */
    bool isSmallEnough(const std::vector<geom::Point>& poly);

    /**
     * Make a cut through the polygon
     * @param poly The polygon to cut
     * @param attempt Retry attempt number (for rotation fallback)
     * @return Vector of resulting polygons (1 if cut failed, 2 if successful)
     */
    std::vector<std::vector<geom::Point>> makeCut(const std::vector<geom::Point>& poly, int attempt = 0);

    /**
     * Split polygon at two edges using a cut line
     * @param poly The polygon (will be modified)
     * @param edge1 First edge index
     * @param edge2 Second edge index
     * @param cutLine Points defining the cut
     * @return Two resulting polygons
     */
    std::vector<std::vector<geom::Point>> split(
        std::vector<geom::Point> poly,
        int edge1,
        int edge2,
        const std::vector<geom::Point>& cutLine
    );

    /**
     * Default cut processor - detects and simplifies straight cuts
     * @param cut The cut line (3 points)
     * @return Processed cut line
     */
    std::vector<geom::Point> detectStraight(const std::vector<geom::Point>& cut);

    /**
     * Apply gap to split halves using stripe subtraction
     * Faithful to mfcg.js: PolyCreate.stripe + PolyBool.and(half, revert(stripe))
     * @param halves The two split polygon halves
     * @param cutLine The cut line used for the split
     * @return Modified halves with gap applied
     */
    std::vector<std::vector<geom::Point>> applyGap(
        const std::vector<std::vector<geom::Point>>& halves,
        const std::vector<geom::Point>& cutLine
    );
};

} // namespace utils
} // namespace town_generator
