#include "town_generator/wards/MerchantWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void MerchantWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Prosperous shops - medium-large buildings (faithful to Haxe)
    // minSq: 50 + 60 * random^2 = 50-110, scaled up 4x for testing
    // gridChaos: 0.5 + random * 0.3 = 0.5-0.8
    double minSq = 4 * (50 + 60 * utils::Random::floatVal() * utils::Random::floatVal());
    double gridChaos = 0.5 + utils::Random::floatVal() * 0.3;
    createAlleys(block, minSq, gridChaos, 0.7, 0.15, 1.0);  // split=true initially
}

} // namespace wards
} // namespace town_generator
