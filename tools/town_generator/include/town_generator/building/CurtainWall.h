#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Cell.h"
#include <vector>
#include <stdexcept>

namespace town_generator {
namespace building {

class City;

/**
 * CurtainWall - City walls with gates and towers
 * Faithful port from Haxe TownGeneratorOS
 *
 * Gates are stored as PointPtr to share vertices with the wall shape.
 * This ensures mutations to gate positions propagate correctly.
 */
class CurtainWall {
public:
    // Tower radius constants (faithful to mfcg.js pc.TOWER_RADIUS and pc.LTOWER_RADIUS)
    static constexpr double TOWER_RADIUS = 1.9;
    static constexpr double LTOWER_RADIUS = 2.5;  // Large tower radius
    static constexpr double THICKNESS = 1.9;      // Wall thickness (faithful to MFCG)

    geom::Polygon shape;
    std::vector<bool> segments;
    std::vector<geom::PointPtr> gates;  // Shared with shape vertices
    std::vector<geom::Point> towers;

private:
    bool real_;
    std::vector<Cell*> patches_;

public:
    CurtainWall(
        bool real,
        City* model,
        const std::vector<Cell*>& cells,
        const std::vector<geom::PointPtr>& reserved
    );

    void buildTowers();

    double getRadius() const;

    bool bordersBy(Cell* p, const geom::Point& v0, const geom::Point& v1) const;
    bool borders(Cell* p) const;

    // Get tower radius exclusion at a vertex (faithful to mfcg.js getTowerRadius)
    // Returns: LTOWER_RADIUS if vertex is a tower, 1 + 2*TOWER_RADIUS if gate, 0 otherwise
    double getTowerRadius(const geom::Point& vertex) const;

    // Equality
    bool operator==(const CurtainWall& other) const {
        return shape == other.shape && gates.size() == other.gates.size();
    }

    bool operator!=(const CurtainWall& other) const {
        return !(*this == other);
    }

private:
    void buildGates(bool real, City* model, const std::vector<geom::PointPtr>& reserved);
};

} // namespace building
} // namespace town_generator
