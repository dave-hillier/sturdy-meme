/**
 * Ported from: Source/com/watabou/towngenerator/building/Patch.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 *
 * Note: In Haxe, Point is a reference type. Patches share Point objects
 * (specifically Triangle circumcenters). This C++ version uses PointPtr
 * (shared_ptr) to preserve those reference semantics.
 */

#pragma once

#include <memory>
#include <vector>

#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../geom/Voronoi.hpp"

namespace town {

// Forward declaration
class Ward;

/**
 * Patch class - represents a region/cell in the town layout.
 * Contains a polygon shape and metadata about its position relative to walls.
 */
class Patch {
public:
    Polygon shape;
    std::shared_ptr<Ward> ward;  // Matches Haxe reference semantics

    bool withinWalls = false;
    bool withinCity = false;

    /**
     * Constructs a Patch from a list of PointPtr.
     */
    explicit Patch(const std::vector<PointPtr>& vertices)
        : shape(vertices), withinCity(false), withinWalls(false) {}

    /**
     * Constructs a Patch from a Voronoi region.
     * Uses the circumcenters of the region's triangles as vertices.
     * Note: Circumcenters are owned by their respective Triangles via shared_ptr.
     */
    static std::shared_ptr<Patch> fromRegion(const Region& r) {
        std::vector<PointPtr> vertices;
        for (Triangle* tr : r.vertices) {
            if (tr && tr->c) {
                vertices.push_back(tr->c);
            }
        }
        return std::make_shared<Patch>(vertices);
    }
};

} // namespace town
