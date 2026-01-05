#include "town_generator/wards/CraftsmenWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void CraftsmenWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Working class housing - variable density (faithful to Haxe)
    // minSq: 10 + 80 * random^2 = 10-90
    // gridChaos: 0.5 + random * 0.2 = 0.5-0.7
    double minSq = 10 + 80 * utils::Random::floatVal() * utils::Random::floatVal();
    double gridChaos = 0.5 + utils::Random::floatVal() * 0.2;
    createAlleys(block, minSq, gridChaos, 0.6, 0.0, 1.0);  // split=true initially
}

} // namespace wards
} // namespace town_generator
