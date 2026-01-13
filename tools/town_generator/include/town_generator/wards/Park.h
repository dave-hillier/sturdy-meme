#pragma once

#include "town_generator/wards/Ward.h"
#include <vector>

namespace town_generator {
namespace wards {

/**
 * Park - Open green space with trees
 *
 * Faithful port from mfcg.js Park (lines 12985-13003).
 * Parks contain:
 * - Smoothed organic boundary (Chaikin smoothing)
 * - Tree spawn points for rendering (via Forester)
 */
class Park : public Ward {
public:
    // Smoothed green area boundary
    geom::Polygon greenArea;

    // Tree spawn points (lazily initialized)
    std::vector<geom::Point> trees;

    Park() = default;

    std::string getName() const override { return "Park"; }

    void createGeometry() override;

    // Spawn trees for rendering (faithful to mfcg.js spawnTrees)
    // Lazily initialized - returns cached trees or spawns new ones
    std::vector<geom::Point> spawnTrees();

    bool operator==(const Park& other) const { return Ward::operator==(other); }
    bool operator!=(const Park& other) const { return !(*this == other); }

private:
    // Create wavy boundary for organic look (doubled vertices + Chaikin 3x)
    geom::Polygon createWavyBoundary(const geom::Polygon& shape);
};

} // namespace wards
} // namespace town_generator
