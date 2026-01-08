#include "town_generator/wards/Cathedral.h"
#include "town_generator/building/City.h"
#include "town_generator/building/Building.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Cathedral::createGeometry() {
    // Faithful to mfcg.js Cathedral.createGeometry (lines 278-286)
    // Uses getAvailable() -> LIRA -> Building.create(minSq=20, hasFront=false, symmetric=true, gap=0.2)
    if (!patch) return;

    utils::Random::reset(patch->seed);

    geometry.clear();

    // Get available area after street/wall insets
    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    geom::Polygon available = patch->shape.shrink(cityBlock);
    if (available.length() < 3) return;

    // Convert to vector for GeomUtils functions
    std::vector<geom::Point> availablePts;
    for (size_t i = 0; i < available.length(); ++i) {
        availablePts.push_back(available[i]);
    }

    // Find largest inscribed rectangle aligned to any edge (LIRA)
    // Faithful to mfcg.js: a = PolyBounds.lira(a)
    std::vector<geom::Point> liraRect = geom::GeomUtils::lira(availablePts);

    if (liraRect.size() < 4) {
        // LIRA failed, use available shape directly
        geometry.push_back(available);
        return;
    }

    geom::Polygon rectPoly(liraRect);

    // Create building using Dwellings-style cellular growth
    // Faithful to mfcg.js: Building.create(a, 20, !1, !0, .2)
    // Parameters: minSq=20, hasFront=false, symmetric=true, gap=0.2
    geom::Polygon buildingPoly = building::Building::create(
        rectPoly,
        20.0,    // minSq
        false,   // hasFront
        true,    // symmetric
        0.2      // gap
    );

    // If Building.create returns a valid shape, use it; otherwise use LIRA rect
    if (buildingPoly.length() >= 3) {
        geometry.push_back(buildingPoly);
    } else {
        geometry.push_back(rectPoly);
    }
}

} // namespace wards
} // namespace town_generator
