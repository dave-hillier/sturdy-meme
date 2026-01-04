#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Voronoi.h"
#include "town_generator/building/Patch.h"
#include "town_generator/building/Topology.h"
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
    std::vector<std::vector<geom::Point>> streets;
    std::vector<std::vector<geom::Point>> roads;
    std::vector<std::vector<geom::Point>> arteries;

    // Configuration
    bool plazaNeeded = false;
    bool citadelNeeded = false;
    bool wallsNeeded = false;

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

    // Equality
    bool operator==(const Model& other) const {
        return patches.size() == other.patches.size();
    }

    bool operator!=(const Model& other) const {
        return !(*this == other);
    }

private:
    int nPatches_;
    std::unique_ptr<Topology> topology_;

    void buildPatches();
    void optimizeJunctions();
    void buildWalls();
    void buildStreets();
    void buildRoads();
    void createWards();
    void buildGeometry();

    static std::vector<geom::Point> generateRandomPoints(int count, double width, double height);
};

} // namespace building
} // namespace town_generator
