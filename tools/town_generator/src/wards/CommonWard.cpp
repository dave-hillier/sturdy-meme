#include "town_generator/wards/CommonWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void CommonWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Standard residential area (faithful to Haxe)
    // minSq: 20 + 80 * random^2 = 20-100 (general residential), scaled up 4x for testing
    // gridChaos: 0.4 + random * 0.3 = 0.4-0.7
    double minSq = 4 * (20 + 80 * utils::Random::floatVal() * utils::Random::floatVal());
    double gridChaos = 0.4 + utils::Random::floatVal() * 0.3;
    createAlleys(block, minSq, gridChaos, 0.6, 0.08, 1.0);  // emptyProb=0.08

    // Filter buildings near outskirts for patches not fully enclosed
    if (!patch->withinWalls) {
        filterOutskirts();
    }
}

} // namespace wards
} // namespace town_generator
