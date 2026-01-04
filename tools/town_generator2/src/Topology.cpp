#include "town_generator2/building/Topology.hpp"
#include "town_generator2/building/Model.hpp"
#include "town_generator2/building/CurtainWall.hpp"
#include "town_generator2/wards/AllWards.hpp"

namespace town_generator2 {
namespace building {

Topology::Topology(Model& model) : model_(model) {
    // Build list of blocked points (walls excluding gates)
    if (model.citadel) {
        auto* castle = dynamic_cast<wards::Castle*>(model.citadel->ward);
        if (castle && castle->wall) {
            for (const auto& v : castle->wall->shape) {
                blocked_.push_back(v);
            }
        }
    }
    if (model.wall) {
        for (const auto& v : model.wall->shape) {
            blocked_.push_back(v);
        }
    }

    // Remove gates from blocked
    for (const auto& gate : model.gates) {
        blocked_.erase(std::remove(blocked_.begin(), blocked_.end(), gate), blocked_.end());
    }

    const geom::Polygon& border = model.border->shape;

    for (auto* p : model.patches) {
        bool withinCity = p->withinCity;

        geom::PointPtr v1 = p->shape.lastPtr();
        geom::NodePtr n1 = processPoint(v1);

        for (size_t i = 0; i < p->shape.length(); ++i) {
            geom::PointPtr v0 = v1;
            v1 = p->shape.ptr(i);
            geom::NodePtr n0 = n1;
            n1 = processPoint(v1);

            if (n0 && !border.contains(v0)) {
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

            if (n1 && !border.contains(v1)) {
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

            if (n0 && n1) {
                double dist = geom::Point::distance(*v0, *v1);
                n0->links[n1] = dist;
                n1->links[n0] = dist;
            }
        }
    }
}

geom::NodePtr Topology::processPoint(const geom::PointPtr& v) {
    geom::NodePtr n;

    auto it = pt2node.find(v);
    if (it != pt2node.end()) {
        n = it->second;
    } else {
        n = graph_.add();
        pt2node[v] = n;
        node2pt[n] = v;
    }

    // Check if blocked
    for (const auto& b : blocked_) {
        if (b == v) return nullptr;
    }

    return n;
}

std::vector<geom::Point> Topology::buildPath(
    const geom::PointPtr& from,
    const geom::PointPtr& to,
    const std::vector<geom::NodePtr>* exclude
) {
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
    for (const auto& n : path) {
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            result.push_back(*it->second);
        }
    }
    return result;
}

} // namespace building
} // namespace town_generator2
