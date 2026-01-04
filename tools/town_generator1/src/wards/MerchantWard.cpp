#include "town_generator/wards/MerchantWard.h"
#include "town_generator/building/Model.h"

namespace town_generator {
namespace wards {

void MerchantWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Higher density, more regular layout - prosperous shops
    createAlleys(block, 35, 0.3, 0.5, 0.02);
}

} // namespace wards
} // namespace town_generator
