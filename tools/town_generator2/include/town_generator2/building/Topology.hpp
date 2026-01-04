#pragma once

#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Graph.hpp"
#include "town_generator2/building/Patch.hpp"
#include <map>
#include <vector>
#include <algorithm>

namespace town_generator2 {
namespace building {

class Model;  // Forward declaration

/**
 * Topology - Graph-based pathfinding on patch vertices
 */
class Topology {
public:
    std::map<geom::PointPtr, geom::NodePtr> pt2node;
    std::map<geom::NodePtr, geom::PointPtr> node2pt;

    std::vector<geom::NodePtr> inner;  // Nodes within city
    std::vector<geom::NodePtr> outer;  // Nodes outside city

    Topology(Model& model);

    /**
     * Find path from one point to another using A*
     * Returns path as list of points, or empty if no path found
     */
    std::vector<geom::Point> buildPath(
        const geom::PointPtr& from,
        const geom::PointPtr& to,
        const std::vector<geom::NodePtr>* exclude = nullptr
    );

private:
    Model& model_;
    geom::Graph graph_;
    geom::PointList blocked_;

    geom::NodePtr processPoint(const geom::PointPtr& v);
};

} // namespace building
} // namespace town_generator2
