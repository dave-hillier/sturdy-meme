#pragma once

#include "town_generator/wards/Ward.h"
#include "town_generator/geom/Polygon.h"

namespace town_generator {

namespace building {
class CurtainWall;
}

namespace wards {

/**
 * Castle - Castle ward with keep and curtain wall
 *
 * Faithful port from mfcg.js (lines 12456-12570):
 * - Creates a curtain wall around the castle patch
 * - Adjusts shape to be more circular (min radius >= 10, compactness >= 0.75)
 * - Creates a single large keep building inside
 */
class Castle : public Ward {
public:
    Castle() = default;

    std::string getName() const override { return "Castle"; }
    bool isSpecialWard() const override { return true; }

    void createGeometry() override;

    // The castle's curtain wall (created during City::build)
    building::CurtainWall* wall = nullptr;

    // The main keep building polygon
    geom::Polygon building;

    bool operator==(const Castle& other) const { return Ward::operator==(other); }
    bool operator!=(const Castle& other) const { return !(*this == other); }

private:
    // Adjust shape to ensure min radius >= 10 and compactness >= 0.75
    // Faithful to mfcg.js Castle.adjustShape (lines 12472-12528)
    void adjustShape();

    // Make shape more circular using DFT-like averaging
    // Faithful to mfcg.js Castle.equalize (lines 12530-12546)
    void equalize(const geom::Point& center, double factor, const std::vector<geom::Point>& fixed);
};

} // namespace wards
} // namespace town_generator
