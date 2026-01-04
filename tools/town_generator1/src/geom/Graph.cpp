#include "town_generator/geom/Graph.h"
#include <limits>

namespace town_generator {
namespace geom {

Graph::~Graph() {
    for (auto* node : nodes) {
        delete node;
    }
}

Graph::Graph(Graph&& other) noexcept : nodes(std::move(other.nodes)) {
    other.nodes.clear();
}

Graph& Graph::operator=(Graph&& other) noexcept {
    if (this != &other) {
        for (auto* node : nodes) {
            delete node;
        }
        nodes = std::move(other.nodes);
        other.nodes.clear();
    }
    return *this;
}

Node* Graph::add(Node* node) {
    if (node == nullptr) {
        node = new Node();
    }
    nodes.push_back(node);
    return node;
}

void Graph::remove(Node* node) {
    node->unlinkAll();
    auto it = std::find(nodes.begin(), nodes.end(), node);
    if (it != nodes.end()) {
        nodes.erase(it);
        delete node;
    }
}

std::vector<Node*> Graph::aStar(Node* start, Node* goal, const std::vector<Node*>* exclude) {
    std::vector<Node*> closedSet;
    if (exclude != nullptr) {
        closedSet = *exclude;
    }

    std::vector<Node*> openSet = {start};
    std::map<Node*, Node*> cameFrom;
    std::map<Node*, double> gScore;
    gScore[start] = 0;

    while (!openSet.empty()) {
        Node* current = openSet.front();
        openSet.erase(openSet.begin());

        if (current == goal) {
            return buildPath(cameFrom, current);
        }

        closedSet.push_back(current);

        double curScore = gScore[current];

        for (auto& pair : current->links) {
            Node* neighbour = pair.first;
            double linkCost = pair.second;

            if (std::find(closedSet.begin(), closedSet.end(), neighbour) != closedSet.end()) {
                continue;
            }

            double score = curScore + linkCost;

            auto openIt = std::find(openSet.begin(), openSet.end(), neighbour);
            if (openIt == openSet.end()) {
                openSet.push_back(neighbour);
            } else if (gScore.count(neighbour) && score >= gScore[neighbour]) {
                continue;
            }

            cameFrom[neighbour] = current;
            gScore[neighbour] = score;
        }
    }

    return {}; // No path found
}

std::vector<Node*> Graph::buildPath(std::map<Node*, Node*>& cameFrom, Node* current) {
    std::vector<Node*> path = {current};

    while (cameFrom.count(current)) {
        current = cameFrom[current];
        path.push_back(current);
    }

    return path;
}

double Graph::calculatePrice(const std::vector<Node*>& path) {
    if (path.size() < 2) {
        return 0.0;
    }

    double price = 0.0;
    for (size_t i = 0; i < path.size() - 1; ++i) {
        Node* current = path[i];
        Node* next = path[i + 1];

        if (current->links.count(next)) {
            price += current->links[next];
        } else {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

    return price;
}

} // namespace geom
} // namespace town_generator
