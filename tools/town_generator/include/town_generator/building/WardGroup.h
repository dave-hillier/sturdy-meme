#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Cell.h"
#include "town_generator/building/Block.h"
#include "town_generator/wards/Ward.h"
#include <vector>
#include <memory>

namespace town_generator {
namespace building {

class City;

/**
 * WardGroup - Groups adjacent cells with the same ward type
 *
 * Faithful port from mfcg.js WardGroup (lines 12847-12956).
 * A WardGroup allows multiple adjacent Alleys-type cells to share
 * unified geometry generation, creating more realistic block structures.
 *
 * Key concepts from reference:
 * - core: The "main" patch face that triggers geometry creation
 * - border: Combined circumference of all cells in the group
 * - blocks: Individual city blocks created by alley subdivision
 * - urban: Whether this group is within city walls
 */
class WardGroup {
public:
    // The cells in this group
    std::vector<Cell*> cells;

    // The "core" patch (first patch added, triggers geometry creation)
    Cell* core = nullptr;

    // Combined border of all cells
    geom::Polygon border;

    // City blocks after alley subdivision
    std::vector<std::unique_ptr<Block>> blocks;

    // Whether this is an urban (walled) district
    // Set to true if all border vertices are "inner" (within city or waterbody)
    bool urban = false;

    // Inner vertices along the border (faithful to mfcg.js District.inner)
    // A vertex is "inner" if withinWalls OR all adjacent cells are withinCity or waterbody
    std::vector<geom::Point> inner;

    // The model this group belongs to
    City* model = nullptr;

    // Shared alley parameters for the group (from mfcg.js District.createParams)
    wards::AlleyParams alleys;

    // Greenery level (0-1)
    double greenery = 0.0;

    // Processing mode (e.g., "Shrink" for building setback variation)
    std::string processingMode;

    WardGroup() = default;
    explicit WardGroup(City* model);

    // Add a patch to this group
    void addPatch(Cell* patch);

    // Build the group border from patch circumferences
    void buildBorder();

    // Create alley parameters for this group
    void createParams();

    // Create geometry for all blocks in this group
    // This is called once when the core patch's createGeometry is invoked
    void createGeometry();

    // Get trees spawned by blocks in this group
    std::vector<geom::Point> spawnTrees();

    // Check if a patch can be added to this group (same ward type, adjacent)
    bool canAddPatch(Cell* patch) const;

    // Get the ward type name for this group
    std::string getTypeName() const;

    // Check if a vertex is "inner" (all adjacent cells are withinCity or waterbody)
    bool isInnerVertex(const geom::Point& v) const;

    // Compute inner vertices from border (called after buildBorder)
    void computeInnerVertices();
};

/**
 * WardGroupBuilder - Creates WardGroups from a model's cells
 *
 * Faithful port from mfcg.js DistrictBuilder/WardGroup creation logic.
 * Groups adjacent cells with the same ward type into unified WardGroups.
 */
class WardGroupBuilder {
public:
    explicit WardGroupBuilder(City* model) : model_(model) {}

    // Build all ward groups from cells
    std::vector<std::unique_ptr<WardGroup>> build();

private:
    City* model_;

    // Grow a group by adding adjacent cells of the same type
    void growGroup(WardGroup* group, std::vector<Cell*>& unassigned);
};

} // namespace building
} // namespace town_generator
