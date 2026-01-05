#include "town_generator/wards/GateWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void GateWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Mixed use near gate - inns, stables, workshops (faithful to Haxe)
    // minSq: 10 + 50 * random^2 = 10-60, scaled up 4x for testing
    // gridChaos: 0.5 + random * 0.3 = 0.5-0.8
    double minSq = 4 * (10 + 50 * utils::Random::floatVal() * utils::Random::floatVal());
    double gridChaos = 0.5 + utils::Random::floatVal() * 0.3;
    createAlleys(block, minSq, gridChaos, 0.7, 0.04, 1.0);  // emptyProb=0.04 (Haxe default)
}

} // namespace wards
} // namespace town_generator
