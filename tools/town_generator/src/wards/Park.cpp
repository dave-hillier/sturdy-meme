#include "town_generator/wards/Park.h"
#include "town_generator/building/City.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace wards {

void Park::createGeometry() {
    // Faithful to mfcg.js Park.createGeometry (lines 12988-12997)
    // Only creates smoothed boundary - trees are spawned lazily
    if (!patch) return;

    // Get available area after street/wall insets with tower corner rounding
    geom::Polygon available = getAvailable();
    if (available.length() < 3) return;

    // Create wavy boundary for organic look (doubled vertices + Chaikin 3x)
    greenArea = createWavyBoundary(available);

    // Trees are spawned lazily via spawnTrees() - initialize to null equivalent
    trees.clear();

    SDL_Log("Park: Created green area with %zu vertices", greenArea.length());
}

geom::Polygon Park::createWavyBoundary(const geom::Polygon& shape) {
    // Faithful to mfcg.js Park.createGeometry (lines 12988-12996)
    // 1. Double vertices by adding midpoints
    // 2. Apply Chaikin smoothing 3 times

    if (shape.length() < 3) return shape;

    // Double vertices by adding midpoints between each pair
    std::vector<geom::Point> doubled;
    size_t len = shape.length();

    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % len];

        doubled.push_back(v0);
        doubled.push_back(geom::GeomUtils::lerp(v0, v1));  // midpoint
    }

    // Apply Chaikin smoothing 3 times (closed polygon)
    geom::Polygon result(doubled);
    result = geom::Polygon::chaikin(result, true, 3);

    return result;
}

std::vector<geom::Point> Park::spawnTrees() {
    // Faithful to mfcg.js Park.spawnTrees (lines 12998-13001)
    // Lazy initialization: null == this.trees && (this.trees = Forester.fillArea(...))

    if (!trees.empty()) {
        return trees;  // Already spawned
    }

    if (greenArea.length() < 3) return trees;

    // Use Forester-style fill algorithm
    // Reference uses: Forester.fillArea(this.getAvailable(), this.patch.district.greenery)
    double area = std::abs(greenArea.square());

    // Greenery factor from district (approximation - use random normal)
    double greeneryFactor = (utils::Random::floatVal() + utils::Random::floatVal() +
                             utils::Random::floatVal()) / 3.0;

    // Tree density based on area and greenery
    int numTrees = static_cast<int>(area * greeneryFactor / 20.0);
    if (numTrees < 3) numTrees = 3;
    if (numTrees > 50) numTrees = 50;

    auto bounds = greenArea.getBounds();

    // Rejection sampling within green area
    for (int i = 0; i < numTrees * 3 && static_cast<int>(trees.size()) < numTrees; ++i) {
        double x = bounds.left + utils::Random::floatVal() * (bounds.right - bounds.left);
        double y = bounds.top + utils::Random::floatVal() * (bounds.bottom - bounds.top);
        geom::Point p(x, y);

        if (greenArea.contains(p)) {
            trees.push_back(p);
        }
    }

    return trees;
}

} // namespace wards
} // namespace town_generator
