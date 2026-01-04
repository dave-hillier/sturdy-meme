#include "town_generator/wards/Farm.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Farm::createGeometry() {
    if (!patch) return;

    // Farms are mostly empty land with a single farmhouse
    geom::Point center = patch->shape.centroid();
    double area = std::abs(patch->shape.square());
    double size = std::sqrt(area) * 0.1;

    // Main farmhouse
    geom::Polygon farmhouse = geom::Polygon::rect(size * 1.2, size);
    farmhouse.offset(center);
    farmhouse.rotate(utils::Random::floatVal() * M_PI);
    geometry.push_back(farmhouse);

    // Maybe a barn
    if (utils::Random::boolVal(0.7)) {
        double angle = utils::Random::floatVal() * M_PI * 2;
        double dist = size * 2;
        geom::Point barnPos(
            center.x + std::cos(angle) * dist,
            center.y + std::sin(angle) * dist
        );

        geom::Polygon barn = geom::Polygon::rect(size * 1.5, size * 0.8);
        barn.offset(barnPos);
        barn.rotate(angle);
        geometry.push_back(barn);
    }
}

} // namespace wards
} // namespace town_generator
