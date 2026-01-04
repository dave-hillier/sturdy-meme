#pragma once

#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <memory>

namespace town_generator {
namespace geom {

class Node;

/**
 * Graph - Graph with A* pathfinding, faithful port from Haxe TownGeneratorOS
 */
class Graph {
public:
    std::vector<Node*> nodes;

    Graph() = default;
    ~Graph();

    // Prevent copying to avoid double-delete of nodes
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    // Move is allowed
    Graph(Graph&& other) noexcept;
    Graph& operator=(Graph&& other) noexcept;

    Node* add(Node* node = nullptr);
    void remove(Node* node);

    std::vector<Node*> aStar(Node* start, Node* goal, const std::vector<Node*>* exclude = nullptr);
    double calculatePrice(const std::vector<Node*>& path);

private:
    std::vector<Node*> buildPath(std::map<Node*, Node*>& cameFrom, Node* current);
};

/**
 * Node - Graph node with weighted links
 */
class Node {
public:
    std::map<Node*, double> links;

    Node() = default;

    void link(Node* node, double price = 1.0, bool symmetrical = true) {
        links[node] = price;
        if (symmetrical) {
            node->links[this] = price;
        }
    }

    void unlink(Node* node, bool symmetrical = true) {
        links.erase(node);
        if (symmetrical) {
            node->links.erase(this);
        }
    }

    void unlinkAll() {
        for (auto& pair : links) {
            pair.first->links.erase(this);
        }
        links.clear();
    }

    // Equality based on identity (pointer comparison in practice)
    bool operator==(const Node& other) const {
        return this == &other;
    }

    bool operator!=(const Node& other) const {
        return !(*this == other);
    }
};

} // namespace geom
} // namespace town_generator
