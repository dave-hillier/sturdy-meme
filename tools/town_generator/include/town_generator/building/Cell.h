#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/geom/Voronoi.h"
#include "town_generator/building/EdgeData.h"
#include <vector>
#include <map>

namespace town_generator {

// Forward declarations
namespace wards {
    class Ward;
}

namespace building {

class WardGroup;

/**
 * Cell - City district, faithful port from Haxe TownGeneratorOS
 *
 * Each patch represents a Voronoi cell and contains:
 * - shape: the polygon boundary
 * - ward: the ward type (Castle, Market, etc.)
 * - neighbors: adjacent cells that share an edge
 * - edgeData: per-edge type classification (COAST, ROAD, WALL, CANAL)
 */
class Cell {
public:
    geom::Polygon shape;
    wards::Ward* ward = nullptr;
    std::vector<Cell*> neighbors;  // Adjacent cells (share an edge)

    // Per-edge data: maps edge index -> EdgeType
    // Edge i is from shape[i] to shape[(i+1) % length]
    std::map<size_t, EdgeType> edgeData;

    // Group of adjacent cells with same ward type for unified geometry generation
    WardGroup* group = nullptr;

    bool withinWalls = false;
    bool withinCity = false;
    bool waterbody = false;    // True if this patch is water (sea, lake, river)
    bool landing = false;      // True if this patch is a harbour landing

    // Seed for reproducible random in this patch (faithful to mfcg.js patch.seed)
    int seed = 0;

    Cell() = default;

    explicit Cell(const std::vector<geom::Point>& vertices)
        : shape(vertices), withinWalls(false), withinCity(false) {}

    explicit Cell(const geom::Polygon& poly)
        : shape(poly), withinWalls(false), withinCity(false) {}

    // Create from Voronoi region
    static Cell fromRegion(geom::Region* r) {
        std::vector<geom::Point> vertices;
        for (auto* tr : r->vertices) {
            vertices.push_back(tr->c);
        }
        return Cell(vertices);
    }

    // Get edge type for edge at index i (from shape[i] to shape[(i+1) % length])
    EdgeType getEdgeType(size_t edgeIndex) const {
        auto it = edgeData.find(edgeIndex);
        return (it != edgeData.end()) ? it->second : EdgeType::NONE;
    }

    // Set edge type for edge at index i
    void setEdgeType(size_t edgeIndex, EdgeType type) {
        edgeData[edgeIndex] = type;
    }

    // Find edge index for an edge defined by two vertices (in either direction)
    // Returns -1 if not found
    int findEdgeIndex(const geom::Point& v0, const geom::Point& v1) const {
        size_t len = shape.length();
        for (size_t i = 0; i < len; ++i) {
            const geom::Point& a = shape[i];
            const geom::Point& b = shape[(i + 1) % len];
            if ((a == v0 && b == v1) || (a == v1 && b == v0)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Set edge type by vertex coordinates
    void setEdgeTypeByVertices(const geom::Point& v0, const geom::Point& v1, EdgeType type) {
        int idx = findEdgeIndex(v0, v1);
        if (idx >= 0) {
            setEdgeType(static_cast<size_t>(idx), type);
        }
    }

    // Get inset amount for an edge (uses edge data and patch properties)
    double getEdgeInsetAmount(size_t edgeIndex, double canalWidth = 0.0) const {
        EdgeType type = getEdgeType(edgeIndex);
        return getEdgeInset(type, landing, canalWidth);
    }

    // Equality
    bool operator==(const Cell& other) const {
        return shape == other.shape &&
               withinWalls == other.withinWalls &&
               withinCity == other.withinCity;
    }

    bool operator!=(const Cell& other) const {
        return !(*this == other);
    }
};

} // namespace building
} // namespace town_generator
