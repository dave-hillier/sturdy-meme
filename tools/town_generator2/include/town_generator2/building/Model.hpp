#pragma once

#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/Voronoi.hpp"
#include "town_generator2/geom/Segment.hpp"
#include "town_generator2/building/Patch.hpp"
#include "town_generator2/building/CurtainWall.hpp"
#include "town_generator2/building/Topology.hpp"
#include "town_generator2/utils/Random.hpp"
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace town_generator2 {

namespace wards { class Ward; class Castle; }

namespace building {

using Street = geom::Polygon;

/**
 * Model - Main city generator, faithful port from Haxe TownGeneratorOS
 *
 * Ward type distribution for inner city (in order of assignment):
 * CraftsmenWard dominates, with Cathedral, Market, Park, Slum, etc. mixed in
 */
class Model {
public:
    // Configuration
    int nPatches_ = 15;
    bool plazaNeeded = false;
    bool citadelNeeded = false;
    bool wallsNeeded = false;

    // Topology for pathfinding
    std::unique_ptr<Topology> topology;

    // Generated patches
    std::vector<Patch*> patches;        // All patches (non-owning)
    std::vector<Patch*> inner;          // Patches within walls

    Patch* citadel = nullptr;           // Citadel patch (if citadelNeeded)
    Patch* plaza = nullptr;             // Central plaza (if plazaNeeded)
    geom::PointPtr center;              // City center point

    // Walls
    std::unique_ptr<CurtainWall> border;   // City border
    CurtainWall* wall = nullptr;           // Actual wall (if wallsNeeded)

    double cityRadius = 0.0;

    // All entrances including castle gates
    geom::PointList gates;

    // Streets and roads
    std::vector<Street> arteries;   // Merged unique segments
    std::vector<Street> streets;    // Streets within walls
    std::vector<Street> roads;      // Roads outside walls

    // Owned resources
    std::vector<std::unique_ptr<Patch>> ownedPatches_;
    std::vector<std::unique_ptr<wards::Ward>> ownedWards_;

    Model(int nPatches = -1, int seed = -1);
    ~Model() = default;

    // Prevent copying
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    /**
     * Find patches containing a vertex (by pointer identity)
     */
    std::vector<Patch*> patchByVertex(const geom::PointPtr& v);

    /**
     * Find circumference polygon of a set of patches
     * Returns polygon with shared vertices from patches
     */
    static geom::Polygon findCircumference(const std::vector<Patch*>& patches);

    /**
     * Get neighbor patch across edge from vertex v
     */
    Patch* getNeighbour(Patch* patch, const geom::PointPtr& v);

    /**
     * Get all neighboring patches
     */
    std::vector<Patch*> getNeighbours(Patch* patch);

    /**
     * Check if patch is enclosed (surrounded by city patches)
     */
    bool isEnclosed(Patch* patch);

private:
    void build();
    void buildPatches();
    void optimizeJunctions();
    void buildWalls();
    void buildStreets();
    void tidyUpRoads();
    void createWards();
    void buildGeometry();
};

} // namespace building
} // namespace town_generator2
