// Patch: A Voronoi region that will be assigned a ward type
// Ported from watabou's Medieval Fantasy City Generator
//
// Semantic rules:
// - Each patch is one Voronoi region
// - Patches have boolean flags: withinCity, withinWalls
// - Each patch is assigned exactly one Ward
// - Patch shape is used for ward geometry and street routing

#pragma once

#include "Geometry.h"
#include "Voronoi.h"
#include <memory>

namespace city {

// Forward declaration
class Ward;

// Patch represents a city district before ward assignment
class Patch {
public:
    Polygon shape;           // The polygon boundary of this patch
    Vec2 seed;               // Original Voronoi seed point

    bool withinCity = false; // Is this patch inside the city boundary?
    bool withinWalls = false;// Is this patch inside the city walls?

    Ward* ward = nullptr;    // The ward assigned to this patch

    // Neighbors (patches that share an edge)
    std::vector<Patch*> neighbors;

    Patch() = default;

    explicit Patch(const Polygon& poly) : shape(poly) {
        seed = shape.centroid();
    }

    explicit Patch(const Region& region) {
        seed = region.seed;
        shape = region.shape();
    }

    // Get area of the patch
    float area() const { return shape.area(); }

    // Get compactness (how circular)
    float compactness() const { return shape.compactness(); }

    // Check if this patch borders another
    bool borders(const Patch& other) const {
        // Two patches border if they share at least one edge
        for (size_t i = 0; i < shape.size(); i++) {
            size_t j = (i + 1) % shape.size();
            const Vec2& v1 = shape[i];
            const Vec2& v2 = shape[j];

            for (size_t k = 0; k < other.shape.size(); k++) {
                size_t l = (k + 1) % other.shape.size();
                // Check if edge (v1,v2) matches edge (other[k], other[l]) in either direction
                if ((v1 == other.shape[k] && v2 == other.shape[l]) ||
                    (v1 == other.shape[l] && v2 == other.shape[k])) {
                    return true;
                }
            }
        }
        return false;
    }

    // Get the shared edge between this patch and another
    std::pair<Vec2, Vec2> getSharedEdge(const Patch& other) const {
        for (size_t i = 0; i < shape.size(); i++) {
            size_t j = (i + 1) % shape.size();
            const Vec2& v1 = shape[i];
            const Vec2& v2 = shape[j];

            for (size_t k = 0; k < other.shape.size(); k++) {
                size_t l = (k + 1) % other.shape.size();
                if ((v1 == other.shape[k] && v2 == other.shape[l]) ||
                    (v1 == other.shape[l] && v2 == other.shape[k])) {
                    return {v1, v2};
                }
            }
        }
        return {{0, 0}, {0, 0}};
    }

    // Distance from center to a point
    float distanceToCenter(const Vec2& p) const {
        return Vec2::distance(seed, p);
    }
};

} // namespace city
