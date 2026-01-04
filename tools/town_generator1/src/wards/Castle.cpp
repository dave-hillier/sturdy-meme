#include "town_generator/wards/Castle.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Castle::createGeometry() {
    if (!patch) return;

    // Create a keep (main tower) in the center
    geom::Point center = patch->shape.centroid();
    double radius = std::sqrt(std::abs(patch->shape.square()) / M_PI) * 0.3;

    // Simple rectangular keep
    geom::Polygon keep = geom::Polygon::rect(radius * 1.5, radius * 1.2);
    keep.offset(center);

    // Add some randomness to rotation
    keep.rotate(utils::Random::floatVal() * M_PI / 4);

    geometry.push_back(keep);

    // Add smaller towers/buildings around the keep
    int numBuildings = utils::Random::intVal(2, 5);
    for (int i = 0; i < numBuildings; ++i) {
        double angle = static_cast<double>(i) / numBuildings * M_PI * 2;
        double dist = radius * (1.5 + utils::Random::floatVal() * 0.5);

        geom::Point pos(
            center.x + std::cos(angle) * dist,
            center.y + std::sin(angle) * dist
        );

        double size = radius * (0.3 + utils::Random::floatVal() * 0.3);
        geom::Polygon building = geom::Polygon::rect(size, size * 0.8);
        building.offset(pos);
        building.rotate(angle + utils::Random::floatVal() * 0.5);

        geometry.push_back(building);
    }
}

} // namespace wards
} // namespace town_generator
