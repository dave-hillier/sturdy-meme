#include "town_generator/wards/AdministrationWard.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void AdministrationWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Large official buildings with open spaces
    geom::Point center = block.centroid();
    double area = std::abs(block.square());
    double size = std::sqrt(area) * 0.4;

    // Main administrative building
    geom::Polygon mainBuilding = geom::Polygon::rect(size * 1.2, size * 0.8);
    mainBuilding.offset(center);
    mainBuilding.rotate(utils::Random::floatVal() * M_PI / 6);
    geometry.push_back(mainBuilding);

    // Secondary buildings
    if (utils::Random::boolVal(0.6)) {
        geom::Point secPos(center.x + size * 0.8, center.y + size * 0.3);
        geom::Polygon secondary = geom::Polygon::rect(size * 0.5, size * 0.4);
        secondary.offset(secPos);
        geometry.push_back(secondary);
    }
}

} // namespace wards
} // namespace town_generator
