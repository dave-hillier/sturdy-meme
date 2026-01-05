#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Graph.h"
#include "town_generator/building/Patch.h"
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace town_generator {
namespace building {

// Forward declaration
class Model;
class CurtainWall;

/**
 * Topology - Street graph pathfinding, faithful port from Haxe TownGeneratorOS
 *
 * Uses PointPtr (shared_ptr<Point>) for reference semantics matching Haxe.
 * The pt2node map uses pointer identity so that mutations to shared vertices
 * are reflected correctly in pathfinding.
 */
class Topology {
private:
    Model* model_;
    geom::Graph graph_;

    std::vector<geom::PointPtr> blocked_;

public:
    // Point <-> Node mappings using pointer identity (shared_ptr)
    // This ensures mutations to shared vertices are reflected correctly
    std::unordered_map<geom::PointPtr, geom::Node*> pt2node;
    std::unordered_map<geom::Node*, geom::PointPtr> node2pt;

    std::vector<geom::Node*> inner;
    std::vector<geom::Node*> outer;

    explicit Topology(Model* model);

    std::vector<geom::Point> buildPath(
        const geom::Point& from,
        const geom::Point& to,
        const std::vector<geom::Node*>* exclude = nullptr
    );

    // PointPtr-based path building (uses pointer identity for exact node matching)
    std::vector<geom::Point> buildPath(
        const geom::PointPtr& from,
        const geom::PointPtr& to,
        const std::vector<geom::Node*>* exclude = nullptr
    );

    // Returns path as PointPtrs for mutable reference semantics (like Haxe Street type)
    std::vector<geom::PointPtr> buildPathPtrs(
        const geom::PointPtr& from,
        const geom::PointPtr& to,
        const std::vector<geom::Node*>* exclude = nullptr
    );

    // Equality
    bool operator==(const Topology& other) const {
        return model_ == other.model_;
    }

    bool operator!=(const Topology& other) const {
        return !(*this == other);
    }

private:
    geom::Node* processPoint(const geom::PointPtr& vPtr);
};

} // namespace building
} // namespace town_generator
