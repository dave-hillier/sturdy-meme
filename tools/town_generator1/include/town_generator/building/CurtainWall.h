#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Patch.h"
#include <vector>
#include <stdexcept>

namespace town_generator {
namespace building {

class Model;

/**
 * CurtainWall - City walls with gates and towers
 * Faithful port from Haxe TownGeneratorOS
 *
 * Gates are stored as PointPtr to share vertices with the wall shape.
 * This ensures mutations to gate positions propagate correctly.
 */
class CurtainWall {
public:
    geom::Polygon shape;
    std::vector<bool> segments;
    std::vector<geom::PointPtr> gates;  // Shared with shape vertices
    std::vector<geom::Point> towers;

private:
    bool real_;
    std::vector<Patch*> patches_;

public:
    CurtainWall(
        bool real,
        Model* model,
        const std::vector<Patch*>& patches,
        const std::vector<geom::PointPtr>& reserved
    );

    void buildTowers();

    double getRadius() const;

    bool bordersBy(Patch* p, const geom::Point& v0, const geom::Point& v1) const;
    bool borders(Patch* p) const;

    // Equality
    bool operator==(const CurtainWall& other) const {
        return shape == other.shape && gates.size() == other.gates.size();
    }

    bool operator!=(const CurtainWall& other) const {
        return !(*this == other);
    }

private:
    void buildGates(bool real, Model* model, const std::vector<geom::PointPtr>& reserved);
};

} // namespace building
} // namespace town_generator
