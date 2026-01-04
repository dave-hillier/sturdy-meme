#pragma once

#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/Voronoi.hpp"
#include <memory>

namespace town_generator2 {

namespace wards { class Ward; }

namespace building {

/**
 * Patch - A Voronoi region with associated ward type
 */
class Patch {
public:
    geom::Polygon shape;
    wards::Ward* ward = nullptr;  // Non-owning reference

    bool withinWalls = false;
    bool withinCity = false;

    explicit Patch(const std::vector<geom::PointPtr>& vertices)
        : shape(vertices) {}

    explicit Patch(const geom::Polygon& poly)
        : shape(poly) {}

    static std::unique_ptr<Patch> fromRegion(const geom::Region& r) {
        std::vector<geom::PointPtr> pts;
        pts.reserve(r.vertices.size());
        for (const auto& tr : r.vertices) {
            // Use the shared circumcenter pointer so adjacent patches share vertices
            pts.push_back(tr->c);
        }
        return std::make_unique<Patch>(pts);
    }
};

} // namespace building
} // namespace town_generator2
