#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Voronoi.h"
#include <vector>

namespace town_generator {

// Forward declarations
namespace wards {
    class Ward;
}

namespace building {

/**
 * Patch - City district, faithful port from Haxe TownGeneratorOS
 */
class Patch {
public:
    geom::Polygon shape;
    wards::Ward* ward = nullptr;
    std::vector<Patch*> neighbors;  // Adjacent patches (share an edge)

    bool withinWalls = false;
    bool withinCity = false;

    Patch() = default;

    explicit Patch(const std::vector<geom::Point>& vertices)
        : shape(vertices), withinWalls(false), withinCity(false) {}

    explicit Patch(const geom::Polygon& poly)
        : shape(poly), withinWalls(false), withinCity(false) {}

    // Create from Voronoi region
    static Patch fromRegion(geom::Region* r) {
        std::vector<geom::Point> vertices;
        for (auto* tr : r->vertices) {
            vertices.push_back(tr->c);
        }
        return Patch(vertices);
    }

    // Equality
    bool operator==(const Patch& other) const {
        return shape == other.shape &&
               withinWalls == other.withinWalls &&
               withinCity == other.withinCity;
    }

    bool operator!=(const Patch& other) const {
        return !(*this == other);
    }
};

} // namespace building
} // namespace town_generator
