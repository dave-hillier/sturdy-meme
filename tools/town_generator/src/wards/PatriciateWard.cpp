#include "town_generator/wards/PatriciateWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void PatriciateWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Wealthy mansions - large buildings (faithful to Haxe)
    // minSq: 80 + 30 * random^2 = 80-110, scaled up 4x for testing
    // gridChaos: 0.5 + random * 0.3 = 0.5-0.8
    double minSq = 4 * (80 + 30 * utils::Random::floatVal() * utils::Random::floatVal());
    double gridChaos = 0.5 + utils::Random::floatVal() * 0.3;
    createAlleys(block, minSq, gridChaos, 0.8, 0.2, 1.0);  // emptyProb=0.2
}

} // namespace wards
} // namespace town_generator
