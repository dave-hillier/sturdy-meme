/**
 * Ported from: Source/com/watabou/towngenerator/building/Topology.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../geom/Graph.hpp"
#include "../geom/Voronoi.hpp"
#include "../building/Patch.hpp"

namespace town {

// Forward declaration
class Model;

// Note: PointPtrHash is defined in Voronoi.hpp

/**
 * Custom hash for shared_ptr<Node> (used as map keys).
 */
struct NodePtrHash {
    std::size_t operator()(const std::shared_ptr<Node>& n) const {
        return std::hash<Node*>()(n.get());
    }
};

/**
 * Topology class - builds a graph representation of the town for pathfinding.
 * Maps patch vertices to graph nodes and provides A* pathfinding.
 */
class Topology {
public:
    // Maps between points and nodes using shared_ptr for proper memory management
    std::unordered_map<PointPtr, std::shared_ptr<Node>, SharedPointPtrHash> pt2node;
    std::unordered_map<std::shared_ptr<Node>, PointPtr, NodePtrHash> node2pt;

    std::vector<std::shared_ptr<Node>> inner;
    std::vector<std::shared_ptr<Node>> outer;

    /**
     * Constructs the topology graph from a town model.
     * @param model The town model containing patches, walls, gates, etc.
     */
    Topology(std::shared_ptr<Model> model);

    /**
     * Finds a path between two points using A*.
     * @param from Starting point
     * @param to Destination point
     * @param exclude Optional nodes to exclude from the search
     * @return Polygon of points representing the path, or nullptr if no path exists
     */
    std::unique_ptr<Polygon> buildPath(const Point& from, const Point& to,
                                        const std::vector<std::shared_ptr<Node>>* exclude = nullptr);

private:
    std::shared_ptr<Model> model_;
    std::unique_ptr<Graph> graph_;
    PointList blocked_;

    /**
     * Processes a vertex, creating or retrieving its corresponding node.
     * @param v The vertex to process
     * @return The node, or nullptr if the vertex is blocked
     */
    std::shared_ptr<Node> processPoint(const PointPtr& v);

    /**
     * Adds an element to a vector only if it's not already present.
     */
    template<typename T>
    void addUnique(std::vector<T>& vec, const T& elem) {
        if (std::find(vec.begin(), vec.end(), elem) == vec.end()) {
            vec.push_back(elem);
        }
    }
};

} // namespace town

// Implementation must be after Model is fully defined
// This will be included from Model.hpp after the Model class definition
