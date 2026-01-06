#include "town_generator/building/Topology.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include <algorithm>
#include <limits>
#include <SDL3/SDL_log.h>

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

    // Use the actual wall shape, not borderPatch (which is just a bounding rectangle)
    const auto& border = model_->border->shape;

    // First pass: build the graph from all land patches
    for (auto* patch : model_->patches) {
        // Skip water patches entirely - they don't participate in road graph
        // (faithful to mfcg.js line 10534: d.withinCity || d.waterbody || a.push(d))
        if (patch->waterbody) {
            continue;
        }

        bool withinCity = patch->withinCity;

        // Check if point is a gate - gates should be in BOTH inner and outer
        auto isGate = [this](const geom::PointPtr& ptr) {
            for (const auto& gatePtr : model_->gates) {
                if (gatePtr == ptr) return true;
            }
            return false;
        };

        geom::PointPtr v1Ptr = patch->shape.lastPtr();
        geom::Node* n1 = processPoint(v1Ptr);

        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr v0Ptr = v1Ptr;
            v1Ptr = patch->shape.ptr(i);
            geom::Node* n0 = n1;
            n1 = processPoint(v1Ptr);

            if (n0 != nullptr) {
                bool onBorder = border.containsPtr(v0Ptr);
                bool isGatePoint = isGate(v0Ptr);

                // Gates go in BOTH lists (they connect inside to outside)
                // Non-border points go in inner or outer based on withinCity
                if (isGatePoint) {
                    if (std::find(inner.begin(), inner.end(), n0) == inner.end()) {
                        inner.push_back(n0);
                    }
                    if (std::find(outer.begin(), outer.end(), n0) == outer.end()) {
                        outer.push_back(n0);
                    }
                } else if (!onBorder) {
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
            }

            if (n1 != nullptr) {
                bool onBorder = border.containsPtr(v1Ptr);
                bool isGatePoint = isGate(v1Ptr);

                // Gates go in BOTH lists (they connect inside to outside)
                if (isGatePoint) {
                    if (std::find(inner.begin(), inner.end(), n1) == inner.end()) {
                        inner.push_back(n1);
                    }
                    if (std::find(outer.begin(), outer.end(), n1) == outer.end()) {
                        outer.push_back(n1);
                    }
                } else if (!onBorder) {
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
            }

            if (n0 != nullptr && n1 != nullptr) {
                n0->link(n1, geom::Point::distance(*v0Ptr, *v1Ptr));
            }
        }
    }

    // Second pass: collect shore vertices and unlink them from the graph
    // This matches mfcg.js excludePoints() which calls unlinkAll() on each point
    // (faithful to mfcg.js lines 10538-10544, 12047-12053)
    std::vector<geom::PointPtr> shoreVertices;
    for (auto* patch : model_->patches) {
        if (patch->waterbody) continue;

        // For each vertex, check if it's shared with a water neighbor
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr vPtr = patch->shape.ptr(i);

            // Check if already processed
            if (std::find(shoreVertices.begin(), shoreVertices.end(), vPtr) != shoreVertices.end()) {
                continue;
            }

            // Check if this vertex is shared with any water patch
            for (auto* neighbor : patch->neighbors) {
                if (neighbor->waterbody && neighbor->shape.containsPtr(vPtr)) {
                    shoreVertices.push_back(vPtr);

                    // Unlink this node from the graph (like mfcg.js excludePoints -> unlinkAll)
                    auto nodeIt = pt2node.find(vPtr);
                    if (nodeIt != pt2node.end() && nodeIt->second != nullptr) {
                        nodeIt->second->unlinkAll();

                        // Also remove from inner/outer lists
                        auto innerIt = std::find(inner.begin(), inner.end(), nodeIt->second);
                        if (innerIt != inner.end()) {
                            inner.erase(innerIt);
                        }
                        auto outerIt = std::find(outer.begin(), outer.end(), nodeIt->second);
                        if (outerIt != outer.end()) {
                            outer.erase(outerIt);
                        }
                    }
                    break;
                }
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

    // Find node matching 'from' by coordinates - require exact match
    for (const auto& [ptPtr, node] : pt2node) {
        double d = geom::Point::distance(from, *ptPtr);
        if (d < 0.001) {  // Exact match only
            fromNode = node;
            break;
        }
    }

    // Find node matching 'to' by coordinates - require exact match
    for (const auto& [ptPtr, node] : pt2node) {
        double d = geom::Point::distance(to, *ptPtr);
        if (d < 0.001) {  // Exact match only
            toNode = node;
            break;
        }
    }

    if (!fromNode || !toNode) {
        return {};
    }

    auto path = graph_.aStar(fromNode, toNode, exclude);

    if (path.empty()) {
        return {};
    }

    // Return path points directly (faithful to Haxe)
    std::vector<geom::Point> result;
    for (auto* n : path) {
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            result.push_back(*it->second);  // Dereference PointPtr
        }
    }

    return result;
}

std::vector<geom::Point> Topology::buildPath(
    const geom::PointPtr& from,
    const geom::PointPtr& to,
    const std::vector<geom::Node*>* exclude
) {
    // Use pointer identity for exact node matching
    auto fromIt = pt2node.find(from);
    auto toIt = pt2node.find(to);

    if (fromIt == pt2node.end() || toIt == pt2node.end()) {
        return {};
    }

    auto path = graph_.aStar(fromIt->second, toIt->second, exclude);

    if (path.empty()) {
        return {};
    }

    std::vector<geom::Point> result;
    for (auto* n : path) {
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            result.push_back(*it->second);
        }
    }

    return result;
}

std::vector<geom::PointPtr> Topology::buildPathPtrs(
    const geom::PointPtr& from,
    const geom::PointPtr& to,
    const std::vector<geom::Node*>* exclude
) {
    // Use pointer identity for exact node matching
    auto fromIt = pt2node.find(from);
    auto toIt = pt2node.find(to);

    if (fromIt == pt2node.end() || toIt == pt2node.end()) {
        return {};
    }

    auto path = graph_.aStar(fromIt->second, toIt->second, exclude);

    if (path.empty()) {
        return {};
    }

    // Return PointPtrs for mutable reference semantics
    std::vector<geom::PointPtr> result;
    for (auto* n : path) {
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            result.push_back(it->second);
        }
    }

    return result;
}

} // namespace building
} // namespace town_generator
