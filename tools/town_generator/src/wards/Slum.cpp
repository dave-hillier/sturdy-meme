#include "town_generator/wards/Slum.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void Slum::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Cramped housing - high density, chaotic (faithful to Haxe)
    // minSq: 10 + 30 * random^2 = 10-40, scaled up 4x for testing
    // gridChaos: 0.6 + random * 0.4 = 0.6-1.0
    double minSq = 4 * (10 + 30 * utils::Random::floatVal() * utils::Random::floatVal());
    double gridChaos = 0.6 + utils::Random::floatVal() * 0.4;
    createAlleys(block, minSq, gridChaos, 0.8, 0.03, 1.0);  // split=true initially
}

} // namespace wards
} // namespace town_generator
