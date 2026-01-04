#pragma once

#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <cmath>

namespace town_generator2 {
namespace geom {

class Node;
using NodePtr = std::shared_ptr<Node>;

/**
 * Node - Graph node with weighted links to other nodes
 */
class Node {
public:
    std::map<NodePtr, double> links;

    Node() = default;

    void link(NodePtr node, double price = 1.0, bool symmetrical = true) {
        links[node] = price;
        if (symmetrical) {
            node->links[shared_from_this()] = price;
        }
    }

    void unlink(NodePtr node, bool symmetrical = true) {
        links.erase(node);
        if (symmetrical) {
            node->links.erase(shared_from_this());
        }
    }

    void unlinkAll() {
        for (auto& [node, _] : links) {
            node->links.erase(shared_from_this());
        }
        links.clear();
    }

    NodePtr shared_from_this() {
        // Note: Caller must ensure node is managed by shared_ptr
        // This is a simplified version - in practice we'd inherit from enable_shared_from_this
        return nullptr;  // Will be set properly when used with Graph
    }
};

/**
 * Graph - Simple graph with A* pathfinding
 */
class Graph {
public:
    std::vector<NodePtr> nodes;

    Graph() = default;

    NodePtr add(NodePtr node = nullptr) {
        if (!node) {
            node = std::make_shared<Node>();
        }
        nodes.push_back(node);
        return node;
    }

    void remove(NodePtr node) {
        // Unlink from all connected nodes
        for (auto& [linked, _] : node->links) {
            linked->links.erase(node);
        }
        node->links.clear();

        // Remove from nodes list
        nodes.erase(std::find(nodes.begin(), nodes.end(), node));
    }

    /**
     * A* pathfinding from start to goal
     * Returns path from goal to start (reverse order), or empty if no path
     */
    std::vector<NodePtr> aStar(NodePtr start, NodePtr goal,
                                const std::vector<NodePtr>* exclude = nullptr) {
        std::vector<NodePtr> closedSet;
        if (exclude) {
            closedSet = *exclude;
        }

        std::vector<NodePtr> openSet = {start};
        std::map<NodePtr, NodePtr> cameFrom;
        std::map<NodePtr, double> gScore;
        gScore[start] = 0;

        while (!openSet.empty()) {
            NodePtr current = openSet.front();
            openSet.erase(openSet.begin());

            if (current == goal) {
                return buildPath(cameFrom, current);
            }

            closedSet.push_back(current);

            double curScore = gScore[current];
            for (auto& [neighbour, linkCost] : current->links) {
                if (std::find(closedSet.begin(), closedSet.end(), neighbour) != closedSet.end()) {
                    continue;
                }

                double score = curScore + linkCost;
                bool inOpen = std::find(openSet.begin(), openSet.end(), neighbour) != openSet.end();

                if (!inOpen) {
                    openSet.push_back(neighbour);
                } else if (gScore.count(neighbour) && score >= gScore[neighbour]) {
                    continue;
                }

                cameFrom[neighbour] = current;
                gScore[neighbour] = score;
            }
        }

        return {};  // No path found
    }

    double calculatePrice(const std::vector<NodePtr>& path) {
        if (path.size() < 2) {
            return 0;
        }

        double price = 0.0;
        for (size_t i = 0; i + 1 < path.size(); ++i) {
            NodePtr current = path[i];
            NodePtr next = path[i + 1];

            auto it = current->links.find(next);
            if (it != current->links.end()) {
                price += it->second;
            } else {
                return std::nan("");
            }
        }
        return price;
    }

private:
    std::vector<NodePtr> buildPath(std::map<NodePtr, NodePtr>& cameFrom, NodePtr current) {
        std::vector<NodePtr> path = {current};

        while (cameFrom.count(current)) {
            current = cameFrom[current];
            path.push_back(current);
        }

        return path;
    }
};

} // namespace geom
} // namespace town_generator2
