#include "town_generator/building/Topology.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include <algorithm>
#include <limits>

namespace town_generator {
namespace building {

Topology::Topology(Model* model) : model_(model) {
    // Building a list of all blocked points (shore + walls excluding gates)
    blocked_.clear();

    if (model_->citadel) {
        for (const auto& pPtr : model_->citadel->shape) {
            blocked_.push_back(*pPtr);
        }
    }

    if (model_->wall) {
        for (const auto& pPtr : model_->wall->shape) {
            blocked_.push_back(*pPtr);
        }
    }

    // Remove gates from blocked list
    for (const auto& gate : model_->gates) {
        auto it = std::find(blocked_.begin(), blocked_.end(), gate);
        if (it != blocked_.end()) {
            blocked_.erase(it);
        }
    }

    const auto& border = model_->border.shape;

    for (auto* patch : model_->patches) {
        bool withinCity = patch->withinCity;

        geom::Point v1 = patch->shape.last();
        geom::Node* n1 = processPoint(v1);

        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::Point v0 = v1;
            v1 = patch->shape[i];
            geom::Node* n0 = n1;
            n1 = processPoint(v1);

            if (n0 != nullptr && !border.contains(v0)) {
                if (withinCity) {
                    if (std::find(inner.begin(), inner.end(), n0) == inner.end()) {
                        inner.push_back(n0);
                    }
                } else {
                    if (std::find(outer.begin(), outer.end(), n0) == outer.end()) {
                        outer.push_back(n0);
                    }
                }
            }

            if (n1 != nullptr && !border.contains(v1)) {
                if (withinCity) {
                    if (std::find(inner.begin(), inner.end(), n1) == inner.end()) {
                        inner.push_back(n1);
                    }
                } else {
                    if (std::find(outer.begin(), outer.end(), n1) == outer.end()) {
                        outer.push_back(n1);
                    }
                }
            }

            if (n0 != nullptr && n1 != nullptr) {
                n0->link(n1, geom::Point::distance(v0, v1));
            }
        }
    }
}

geom::Node* Topology::processPoint(const geom::Point& v) {
    geom::Node* n = nullptr;

    auto it = pt2node.find(v);
    if (it != pt2node.end()) {
        n = it->second;
    } else {
        n = graph_.add();
        pt2node[v] = n;
        node2pt[n] = v;
    }

    // Return nullptr if point is blocked
    for (const auto& blocked : blocked_) {
        if (blocked == v) {
            return nullptr;
        }
    }

    return n;
}

std::vector<geom::Point> Topology::buildPath(
    const geom::Point& from,
    const geom::Point& to,
    const std::vector<geom::Node*>* exclude
) {
    // Find nodes for exact match or nearest node
    geom::Node* fromNode = nullptr;
    geom::Node* toNode = nullptr;

    auto fromIt = pt2node.find(from);
    if (fromIt != pt2node.end()) {
        fromNode = fromIt->second;
    } else {
        // Find nearest inner node
        double minDist = std::numeric_limits<double>::infinity();
        for (auto* n : inner) {
            auto it = node2pt.find(n);
            if (it != node2pt.end()) {
                double d = geom::Point::distance(from, it->second);
                if (d < minDist) {
                    minDist = d;
                    fromNode = n;
                }
            }
        }
    }

    auto toIt = pt2node.find(to);
    if (toIt != pt2node.end()) {
        toNode = toIt->second;
    } else {
        // Find nearest inner node
        double minDist = std::numeric_limits<double>::infinity();
        for (auto* n : inner) {
            auto it = node2pt.find(n);
            if (it != node2pt.end()) {
                double d = geom::Point::distance(to, it->second);
                if (d < minDist) {
                    minDist = d;
                    toNode = n;
                }
            }
        }
    }

    if (!fromNode || !toNode) {
        return {};
    }

    auto path = graph_.aStar(fromNode, toNode, exclude);

    if (path.empty()) {
        return {};
    }

    std::vector<geom::Point> result;
    // Include original 'from' point at start
    result.push_back(from);
    for (auto* n : path) {
        result.push_back(node2pt[n]);
    }
    // Include original 'to' point at end
    result.push_back(to);

    return result;
}

} // namespace building
} // namespace town_generator
