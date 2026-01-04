#include "town_generator/wards/Market.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/Cutter.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Market::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Market has mostly open space with scattered stalls around the edge
    auto ring = building::Cutter::ring(block, std::sqrt(std::abs(block.square())) * 0.15);

    for (const auto& segment : ring) {
        // Create small market stalls
        createAlleys(segment, 15, 0.3, 0.5, 0.3);
    }

    // Maybe add a central market building
    if (utils::Random::boolVal(0.5)) {
        geom::Point center = block.centroid();
        double size = std::sqrt(std::abs(block.square())) * 0.15;
        geom::Polygon marketHall = geom::Polygon::rect(size * 1.5, size);
        marketHall.offset(center);
        marketHall.rotate(utils::Random::floatVal() * M_PI);
        geometry.push_back(marketHall);
    }
}

} // namespace wards
} // namespace town_generator
