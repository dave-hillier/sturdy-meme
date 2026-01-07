#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Cell.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <memory>

namespace town_generator {
namespace building {

class City;

/**
 * District - Groups multiple cells into a cohesive area with shared parameters
 * Faithful port from mfcg.js District class
 *
 * Districts allow adjacent cells of the same ward type to share:
 * - Alley parameters (minSq, gridChaos, sizeChaos, blockSize)
 * - Visual styling
 * - Building density patterns
 */
class District {
public:
    // The cells in this district
    std::vector<Cell*> cells;

    // The unified ward type for this district
    wards::Ward* ward = nullptr;

    // Shared alley parameters for all cells in district
    wards::AlleyParams alleys;

    // Greenery level (0-1)
    double greenery = 0.0;

    // Whether this is an urban (walled) district
    bool urban = false;

    // The model this district belongs to
    City* model = nullptr;

    // Border of the district (circumference of all cells)
    geom::Polygon border;

    // District type (from ward)
    std::string type;

    District() = default;

    // Create a district from a starting patch
    explicit District(Cell* startPatch, City* model);

    // Build the district by growing from start patch to include neighbors of same type
    void build();

    // Create the alley parameters for this district (faithful to mfcg.js createParams)
    void createParams();

    // Get the combined shape of all cells
    geom::Polygon getShape() const;

    // Create geometry for all cells in district
    void createGeometry();
};

/**
 * DistrictBuilder - Creates districts from a model's cells
 * Faithful port from mfcg.js DistrictBuilder
 */
class DistrictBuilder {
public:
    explicit DistrictBuilder(City* model) : model_(model) {}

    // Build all districts from cells
    std::vector<std::unique_ptr<District>> build();

private:
    City* model_;

    // Find all cells that can be grouped into a district starting from seed
    std::vector<Cell*> growDistrict(Cell* seed, std::vector<Cell*>& unassigned);
};

} // namespace building
} // namespace town_generator
