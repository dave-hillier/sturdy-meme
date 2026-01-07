#include "town_generator/building/District.h"
#include "town_generator/building/City.h"
#include "town_generator/wards/Ward.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace building {

District::District(Cell* startPatch, City* model)
    : model(model)
{
    if (startPatch && startPatch->ward) {
        type = startPatch->ward->getName();
        ward = startPatch->ward;
        urban = startPatch->withinWalls;
    }
}

void District::build() {
    if (cells.empty()) return;

    // Create the combined border from all cells
    border = City::findCircumference(cells);

    // Create shared parameters
    createParams();
}

void District::createParams() {
    // Faithful to mfcg.js District.createParams()

    // minSq: 15 + 40 * abs(normal4 - 1)
    double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    alleys.minSq = 15.0 + 40.0 * std::abs(normal4);

    // gridChaos: 0.2 + normal3 * 0.8
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal()) / 3.0;
    alleys.gridChaos = 0.2 + normal3 * 0.8;

    // sizeChaos: 0.4 + normal3 * 0.6
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.sizeChaos = 0.4 + normal3 * 0.6;

    // shapeFactor: 0.25 + normal3 * 2
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.shapeFactor = 0.25 + normal3 * 2.0;

    // inset: 0.6 * (1 - abs(normal4))
    normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    alleys.inset = 0.6 * (1.0 - std::abs(normal4));

    // blockSize: 4 + 10 * normal3
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.blockSize = 4.0 + 10.0 * normal3;

    // minFront derived from minSq
    alleys.minFront = std::sqrt(alleys.minSq);

    // greenery: normal3^2 (or ^1 for parks)
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    greenery = (type == "Park") ? normal3 : normal3 * normal3;

    // Adjust for sprawl (outer areas)
    if (!urban) {
        alleys.gridChaos *= 0.5;
        alleys.blockSize *= 2.0;
        greenery = (1.0 + greenery) / 2.0;
    }
}

geom::Polygon District::getShape() const {
    return border;
}

void District::createGeometry() {
    // For now, delegate to each patch's ward
    // In a more complete implementation, this would create unified geometry
    // across the district boundary
    for (auto* patch : cells) {
        if (patch->ward) {
            patch->ward->createGeometry();
        }
    }
}

// NOTE: DistrictBuilder functionality has been moved to WardGroupBuilder in WardGroup.cpp
// The WardGroupBuilder class provides the same grouping and growth logic.

} // namespace building
} // namespace town_generator
