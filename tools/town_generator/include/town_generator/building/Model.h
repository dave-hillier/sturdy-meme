#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Voronoi.h"
#include "town_generator/building/Patch.h"
#include "town_generator/building/Topology.h"
#include "town_generator/building/Canal.h"
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
 * Model - Master city generator, faithful port from Haxe TownGeneratorOS
 */
class Model {
public:
    // City size enum
    enum class Size {
        SMALL,
        MEDIUM,
        LARGE
    };

    // Generated data
    std::vector<Patch*> patches;
    std::vector<Patch*> inner;  // Patches within walls
    Patch borderPatch;  // Patch representing outer border

    CurtainWall* citadel = nullptr;
    CurtainWall* wall = nullptr;      // Inner wall (if walled city)
    CurtainWall* border = nullptr;    // Outer border wall (always exists, provides gates)
    Patch* plaza = nullptr;

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
    bool coastNeeded = false;     // True if city has a coastline
    double coastDir = 0.0;        // Direction of coast (0-2, multiplied by PI)

    // Water features
    geom::Polygon waterEdge;      // Boundary of water area
    geom::Polygon earthEdge;      // Boundary of land area
    geom::Polygon shore;          // Shore line where land meets water
    bool riverNeeded = false;     // Whether to generate a river/canal
    std::vector<std::unique_ptr<Canal>> canals;  // Rivers/canals

    // Owned wards and patches
    std::vector<std::unique_ptr<wards::Ward>> wards_;
    std::vector<std::unique_ptr<Patch>> ownedPatches_;

    Model(int nPatches, int seed = -1);
    ~Model();

    // Prevent copying
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Move allowed
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;

    void build();

    // Find patches containing a vertex (by value - coordinate match)
    std::vector<Patch*> patchByVertex(const geom::Point& v);

    // Find patches sharing a vertex (by pointer identity - true topology)
    std::vector<Patch*> patchByVertexPtr(const geom::PointPtr& v);

    // Find circumference of a set of patches (preserves shared vertices)
    static geom::Polygon findCircumference(const std::vector<Patch*>& patches);

    // Split patches into connected components (groups that share edges)
    // Like Ic.split in mfcg.js - uses flood fill through neighbor relationships
    static std::vector<std::vector<Patch*>> splitIntoConnectedComponents(const std::vector<Patch*>& patches);

    // Equality
    bool operator==(const Model& other) const {
        return patches.size() == other.patches.size();
    }

    bool operator!=(const Model& other) const {
        return !(*this == other);
    }

private:
    int nPatches_;
    double maxRadius_ = 0.0;  // Max spiral radius (b in mfcg.js)
    double offsetX_ = 0.0;    // Offset to translate to positive coordinates
    double offsetY_ = 0.0;
    std::unique_ptr<Topology> topology_;

    void buildPatches();
    void optimizeJunctions();
    void buildWalls();
    void buildStreets();
    void tidyUpRoads();
    void createWards();
    void buildGeometry();

    static std::vector<geom::Point> generateRandomPoints(int count, double width, double height);
};

} // namespace building
} // namespace town_generator
