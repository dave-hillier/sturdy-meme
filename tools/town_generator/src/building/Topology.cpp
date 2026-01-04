#include "town_generator/building/Topology.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include <algorithm>
#include <limits>

namespace town_generator {
namespace building {

Topology::Topology(Model* model) : model_(model) {
    // Building a list of all blocked points (shore + walls excluding gates)
    // Using PointPtr for reference semantics - blocked vertices are identified
    // by pointer identity, not coordinate values
    blocked_.clear();

    if (model_->citadel) {
        for (const auto& pPtr : model_->citadel->shape) {
            blocked_.push_back(pPtr);
        }
    }

    if (model_->wall) {
        for (const auto& pPtr : model_->wall->shape) {
            blocked_.push_back(pPtr);
        }
    }

    // Remove gates from blocked list (by pointer identity)
    for (const auto& gatePtr : model_->gates) {
        auto it = std::find(blocked_.begin(), blocked_.end(), gatePtr);
        if (it != blocked_.end()) {
            blocked_.erase(it);
        }
    }

    const auto& border = model_->border.shape;

    for (auto* patch : model_->patches) {
        bool withinCity = patch->withinCity;

        geom::PointPtr v1Ptr = patch->shape.lastPtr();
        geom::Node* n1 = processPoint(v1Ptr);

        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr v0Ptr = v1Ptr;
            v1Ptr = patch->shape.ptr(i);
            geom::Node* n0 = n1;
            n1 = processPoint(v1Ptr);

            if (n0 != nullptr && !border.containsPtr(v0Ptr)) {
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

            if (n1 != nullptr && !border.containsPtr(v1Ptr)) {
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
                n0->link(n1, geom::Point::distance(*v0Ptr, *v1Ptr));
            }
        }
    }
}

geom::Node* Topology::processPoint(const geom::PointPtr& vPtr) {
    geom::Node* n = nullptr;

    // Look up by pointer identity
    auto it = pt2node.find(vPtr);
    if (it != pt2node.end()) {
        n = it->second;
    } else {
        n = graph_.add();
        pt2node[vPtr] = n;
        node2pt[n] = vPtr;
    }

    // Return nullptr if point is blocked (by pointer identity)
    for (const auto& blockedPtr : blocked_) {
        if (blockedPtr == vPtr) {
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
    // Find nodes - since pt2node now uses PointPtr keys, we need to search
    // by coordinate matching among all known points
    geom::Node* fromNode = nullptr;
    geom::Node* toNode = nullptr;

    // Find node matching 'from' by coordinates
    double minFromDist = std::numeric_limits<double>::infinity();
    for (const auto& [ptPtr, node] : pt2node) {
        double d = geom::Point::distance(from, *ptPtr);
        if (d < 0.001) {  // Exact match
            fromNode = node;
            break;
        }
        if (d < minFromDist) {
            minFromDist = d;
            fromNode = node;
        }
    }

    // Find node matching 'to' by coordinates
    double minToDist = std::numeric_limits<double>::infinity();
    for (const auto& [ptPtr, node] : pt2node) {
        double d = geom::Point::distance(to, *ptPtr);
        if (d < 0.001) {  // Exact match
            toNode = node;
            break;
        }
        if (d < minToDist) {
            minToDist = d;
            toNode = node;
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
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            result.push_back(*it->second);  // Dereference PointPtr
        }
    }
    // Include original 'to' point at end
    result.push_back(to);

    return result;
}

} // namespace building
} // namespace town_generator
