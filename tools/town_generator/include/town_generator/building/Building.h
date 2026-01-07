#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include <vector>

namespace town_generator {
namespace building {

/**
 * Building - Creates L-shaped and complex building footprints
 *
 * Faithful port from mfcg.js Jd class (Building).
 * Creates building shapes by subdividing rectangular lots into a grid
 * and generating organic L-shaped plans.
 */
class Building {
public:
    /**
     * Create a building footprint from a quadrilateral lot
     *
     * @param quad The input quadrilateral (must have exactly 4 vertices)
     * @param minSq Minimum area per grid cell
     * @param hasFront If true, use front-facing plan (buildings touch front edge)
     * @param symmetric If true, use symmetric plan
     * @param gap Gap factor for grid subdivision (default 0.6)
     * @return The building outline, or empty polygon if creation fails
     *
     * Faithful to mfcg.js Jd.create (lines 9754-9808)
     */
    static geom::Polygon create(
        const geom::Polygon& quad,
        double minSq,
        bool hasFront = false,
        bool symmetric = false,
        double gap = 0.6
    );

    /**
     * Generate a plan (grid of filled/empty cells) for a building
     *
     * @param width Grid width
     * @param height Grid height
     * @param stopProb Probability to stop filling (default 0.5)
     * @return Boolean vector where true = filled cell
     *
     * Faithful to mfcg.js Jd.getPlan
     */
    static std::vector<bool> getPlan(int width, int height, double stopProb = 0.5);

    /**
     * Generate a front-facing plan (buildings always touch front edge)
     *
     * @param width Grid width
     * @param height Grid height
     * @return Boolean vector where true = filled cell
     *
     * Faithful to mfcg.js Jd.getPlanFront
     */
    static std::vector<bool> getPlanFront(int width, int height);

    /**
     * Generate a symmetric plan (mirrored left-right)
     *
     * @param width Grid width
     * @param height Grid height
     * @return Boolean vector where true = filled cell
     *
     * Faithful to mfcg.js Jd.getPlanSym
     */
    static std::vector<bool> getPlanSym(int width, int height);

    /**
     * Compute the circumference (outer boundary) of a set of grid cells
     *
     * @param cells Vector of quadrilateral cells
     * @return The merged outline polygon
     *
     * Faithful to mfcg.js Jd.circumference
     */
    static std::vector<geom::Point> circumference(const std::vector<geom::Polygon>& cells);

    // Note: grid() function is in Cutter class (Cutter::grid) per MFCG organization
};

} // namespace building
} // namespace town_generator
