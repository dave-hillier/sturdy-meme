/**
 * Implementation of Topology class.
 */

#include "Topology.hpp"
#include "Model.hpp"

namespace town {

Topology::Topology(std::shared_ptr<Model> model) : model_(model) {
    graph_ = std::make_unique<Graph>();

    inner.clear();
    outer.clear();

    // Building a list of all blocked points (shore + walls excluding gates)
    blocked_.clear();
    if (model->citadel) {
        for (size_t i = 0; i < model->citadel->shape.size(); ++i) {
            blocked_.push_back(model->citadel->shape[i]);
        }
    }
    if (model->wall) {
        for (size_t i = 0; i < model->wall->shape.size(); ++i) {
            blocked_.push_back(model->wall->shape[i]);
        }
    }
    // Remove gates from blocked (by pointer identity)
    for (PointPtr gate : model->gates) {
        auto it = std::find(blocked_.begin(), blocked_.end(), gate);
        if (it != blocked_.end()) {
            blocked_.erase(it);
        }
    }

    const Polygon& border = model->border->shape;

    for (auto& p : model->patches) {
        bool withinCity = p->withinCity;

        PointPtr v1 = p->shape[p->shape.size() - 1];
        auto n1 = processPoint(v1);

        for (size_t i = 0; i < p->shape.size(); ++i) {
            PointPtr v0 = v1;
            v1 = p->shape[i];
            auto n0 = n1;
            n1 = processPoint(v1);

            if (n0 && !border.contains(v0)) {
                if (withinCity)
                    addUnique(inner, n0);
                else
                    addUnique(outer, n0);
            }
            if (n1 && !border.contains(v1)) {
                if (withinCity)
                    addUnique(inner, n1);
                else
                    addUnique(outer, n1);
            }

            if (n0 && n1) {
                n0->link(n1.get(), Point::distance(*v0, *v1));
            }
        }
    }
}

std::shared_ptr<Node> Topology::processPoint(PointPtr v) {
    std::shared_ptr<Node> n;

    auto it = pt2node.find(v);
    if (it != pt2node.end()) {
        n = it->second;
    } else {
        n = graph_->add();
        pt2node[v] = n;
        node2pt[n] = v;
    }

    // Check if blocked (by pointer identity)
    for (PointPtr bp : blocked_) {
        if (bp == v) {
            return nullptr;
        }
    }
    return n;
}

std::unique_ptr<Polygon> Topology::buildPath(const Point& from, const Point& to,
                                              const std::vector<std::shared_ptr<Node>>* exclude) {
    // Find nodes for from and to points (by coordinate values, not pointer identity)
    std::shared_ptr<Node> fromNode = nullptr;
    std::shared_ptr<Node> toNode = nullptr;

    for (const auto& [pt, node] : pt2node) {
        if (Point::valuesEqual(*pt, from)) fromNode = node;
        if (Point::valuesEqual(*pt, to)) toNode = node;
    }

    if (!fromNode || !toNode) {
        return nullptr;
    }

    auto path = graph_->aStar(fromNode, toNode, exclude);
    if (!path) {
        return nullptr;
    }

    // Build a polygon with the path points
    // The polygon references the existing Point objects from node2pt
    std::vector<PointPtr> pathPts;
    for (const auto& n : *path) {
        auto it = node2pt.find(n);
        if (it != node2pt.end()) {
            pathPts.push_back(it->second);
        }
    }
    return std::make_unique<Polygon>(pathPts);
}

} // namespace town
