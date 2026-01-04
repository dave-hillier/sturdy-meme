#pragma once

#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/building/Patch.hpp"
#include "town_generator2/utils/Random.hpp"
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace town_generator2 {
namespace building {

class Model;  // Forward declaration

/**
 * CurtainWall - City wall with gates and towers
 */
class CurtainWall {
public:
    geom::Polygon shape;
    std::vector<bool> segments;        // Which segments are actual wall
    geom::PointList gates;             // Gate locations (shared pointers)
    geom::PointList towers;            // Tower locations

    CurtainWall(bool real, Model& model, std::vector<Patch*>& patches,
                const geom::PointList& reserved);

    void buildTowers();

    double getRadius() const {
        double radius = 0.0;
        for (const auto& v : shape) {
            radius = std::max(radius, v->length());
        }
        return radius;
    }

    /**
     * Check if patch borders this wall at edge v0->v1
     */
    bool bordersBy(Patch* p, const geom::PointPtr& v0, const geom::PointPtr& v1) const {
        bool isInner = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
        int index = isInner
            ? shape.findEdge(v0, v1)
            : shape.findEdge(v1, v0);

        return (index != -1 && segments[index]);
    }

    /**
     * Check if patch borders this wall at any segment
     */
    bool borders(Patch* p) const {
        bool isInner = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
        size_t length = shape.length();

        for (size_t i = 0; i < length; ++i) {
            if (!segments[i]) continue;

            const geom::PointPtr& v0 = shape.ptr(i);
            const geom::PointPtr& v1 = shape.ptr((i + 1) % length);

            int index = isInner
                ? p->shape.findEdge(v0, v1)
                : p->shape.findEdge(v1, v0);

            if (index != -1) return true;
        }
        return false;
    }

private:
    bool real_;
    std::vector<Patch*> patches_;

    void buildGates(bool real, Model& model, const geom::PointList& reserved);
};

} // namespace building
} // namespace town_generator2
