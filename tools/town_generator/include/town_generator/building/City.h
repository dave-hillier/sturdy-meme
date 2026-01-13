#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Voronoi.h"
#include "town_generator/geom/DCEL.h"
#include "town_generator/building/Cell.h"
#include "town_generator/building/Topology.h"
#include "town_generator/building/Canal.h"
#include "town_generator/building/WardGroup.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <memory>

namespace town_generator {

namespace wards {
    class Ward;
}

namespace building {

class CurtainWall;

/**
 * City - Master city generator, faithful port from Haxe TownGeneratorOS
 */
class City {
public:
    // City size enum
    enum class Size {
        SMALL,
        MEDIUM,
        LARGE
    };

    // Generated data
    std::vector<Cell*> cells;
    std::vector<Cell*> inner;  // Patches within walls
    Cell borderPatch;  // Cell representing outer border

    CurtainWall* citadel = nullptr;
    CurtainWall* wall = nullptr;      // Inner wall (if walled city)
    CurtainWall* border = nullptr;    // Outer border wall (always exists, provides gates)
    Cell* plaza = nullptr;

    std::vector<geom::PointPtr> gates;  // Shared pointers for reference semantics

    // Street type: vector of PointPtr for mutable reference semantics (like Haxe)
    using Street = std::vector<geom::PointPtr>;
    std::vector<Street> streets;
    std::vector<Street> roads;
    std::vector<Street> arteries;  // Consolidated streets+roads with shared vertices

    // Configuration
    bool plazaNeeded = false;
    bool citadelNeeded = false;
    bool wallsNeeded = false;
    bool templeNeeded = false;    // True if city has a cathedral/temple
    bool slumsNeeded = false;     // True if city has slums outside walls
    bool coastNeeded = false;     // True if city has a coastline
    double coastDir = 0.0;        // Direction of coast (0-2, multiplied by PI)

    // Water features
    geom::Polygon waterEdge;      // Boundary of water area (smoothed for basic display)
    geom::Polygon earthEdge;      // Boundary of land area (raw Voronoi vertices)
    geom::Polygon shore;          // Shore line where land meets water
    bool riverNeeded = false;     // Whether to generate a river/canal
    int maxDocks = 0;             // Maximum number of dock/harbour cells (faithful to mfcg.js)
    std::vector<std::unique_ptr<Canal>> canals;  // Rivers/canals

    // Get ocean polygon for rendering (smoothed except at landing areas)
    // Faithful to mfcg.js getOcean() - lazy evaluated after wards are created
    geom::Polygon getOcean() const;

    // Edge classification (faithful to mfcg.js buildDomains)
    // Each edge is represented as a pair of points (start, end)
    using Edge = std::pair<geom::Point, geom::Point>;
    std::vector<Edge> horizonE;   // Outer boundary edges (no neighbor)
    std::vector<Edge> shoreE;     // Land-water boundary edges

    // Owned wards and cells
    std::vector<std::unique_ptr<wards::Ward>> wards_;
    std::vector<std::unique_ptr<Cell>> ownedCells_;

    // Ward groups for unified geometry generation
    std::vector<std::unique_ptr<WardGroup>> wardGroups_;

    // DCEL for topological operations (circumference, edge collapse, neighbor queries)
    std::unique_ptr<geom::DCEL> dcel_;

    City(int nCells, int seed = -1);
    ~City();

    // Prevent copying
    City(const City&) = delete;
    City& operator=(const City&) = delete;

    // Move allowed
    City(City&&) noexcept = default;
    City& operator=(City&&) noexcept = default;

    void build();

    // Find cells containing a vertex (by value - coordinate match)
    std::vector<Cell*> cellsByVertex(const geom::Point& v);

    // Find cells sharing a vertex (by pointer identity - true topology)
    std::vector<Cell*> cellsByVertexPtr(const geom::PointPtr& v);

    // Find circumference of a set of cells (preserves shared vertices)
    static geom::Polygon findCircumference(const std::vector<Cell*>& cells);

    // Split cells into connected components (groups that share edges)
    // Like Ic.split in mfcg.js - uses flood fill through neighbor relationships
    static std::vector<std::vector<Cell*>> splitIntoConnectedComponents(const std::vector<Cell*>& cells);

    // Get canal width at a vertex/edge (faithful to mfcg.js getCanalWidth)
    double getCanalWidth(const geom::Point& v) const;

    // Equality
    bool operator==(const City& other) const {
        return cells.size() == other.cells.size();
    }

    bool operator!=(const City& other) const {
        return !(*this == other);
    }

private:
    int nCells_;
    double maxRadius_ = 0.0;  // Max spiral radius (b in mfcg.js)
    double offsetX_ = 0.0;    // Offset to translate to positive coordinates
    double offsetY_ = 0.0;
    std::unique_ptr<Topology> topology_;

    void buildPatches();
    void optimizeJunctions();
    void buildWalls();
    void buildDomains();     // Build horizon/shore edge classification (faithful to mfcg.js)
    void disableCoastWallSegments();  // Disable wall segments along coast (needs shoreE)
    void buildStreets();
    void tidyUpRoads();
    void createWards();
    void buildFarms();       // Build farms with sine-wave radial pattern (faithful to mfcg.js)
    void buildSlums();       // Build slums outside city walls (faithful to mfcg.js)
    void buildGeometry();
    void setEdgeData();      // Set edge types (COAST, ROAD, WALL, CANAL) on all cells
    void createWardGroups(); // Create WardGroups from adjacent same-type cells

    static std::vector<geom::Point> generateRandomPoints(int count, double width, double height);
};

} // namespace building
} // namespace town_generator
