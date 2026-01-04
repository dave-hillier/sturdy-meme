#include "town_generator/wards/MilitaryWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void MilitaryWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Regular, orderly layout - barracks and training grounds
    geom::Point center = block.centroid();
    double area = std::abs(block.square());
    double size = std::sqrt(area) * 0.25;

    // Create row of barracks
    int numBarracks = utils::Random::intVal(2, 4);
    double spacing = size * 1.5;
    double startX = center.x - (numBarracks - 1) * spacing / 2;

    for (int i = 0; i < numBarracks; ++i) {
        geom::Point pos(startX + i * spacing, center.y);
        geom::Polygon barrack = geom::Polygon::rect(size * 0.8, size * 1.5);
        barrack.offset(pos);
        geometry.push_back(barrack);
    }

    // Training ground remains empty (no geometry added)
}

} // namespace wards
} // namespace town_generator
