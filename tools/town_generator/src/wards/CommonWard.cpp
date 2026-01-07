#include "town_generator/wards/CommonWard.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void CommonWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Standard residential area (faithful to MFCG)
    AlleyParams params = AlleyParams::createUrban();
    // Override some params for CommonWard characteristics
    params.emptyProb = 0.08;  // 8% empty lots

    // Use the faithful implementation with Bisector and Block classes
    createAlleys(block, params);
}

} // namespace wards
} // namespace town_generator
