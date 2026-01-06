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

    // Prosperous shops (faithful to MFCG)
    AlleyParams params = AlleyParams::createUrban();
    params.emptyProb = 0.15;  // 15% empty lots for market squares

    createAlleysFaithful(block, params);
}

} // namespace wards
} // namespace town_generator
