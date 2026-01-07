#pragma once

#include "town_generator/wards/Ward.h"

namespace town_generator {

namespace building {
class WardGroup;
}

namespace wards {

/**
 * Alleys - Standard urban ward with buildings along streets
 *
 * Faithful to MFCG: Alleys is the main urban ward type.
 * The actual geometry (blocks, lots, buildings) is created by WardGroup.
 * Alleys.createGeometry() simply delegates to the group.
 */
class Alleys : public Ward {
public:
    Alleys() = default;

    std::string getName() const override { return "Alleys"; }

    void createGeometry() override;

    // Trees spawned by WardGroup (not virtual in base Ward class)
    std::vector<geom::Point> spawnTrees();

    bool operator==(const Alleys& other) const { return Ward::operator==(other); }
    bool operator!=(const Alleys& other) const { return !(*this == other); }

private:
    std::vector<geom::Point> trees_;
};

} // namespace wards
} // namespace town_generator
