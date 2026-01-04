#include "town_generator/wards/CommonWard.h"
#include "town_generator/building/Model.h"

namespace town_generator {
namespace wards {

void CommonWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Standard residential area
    createAlleys(block, 30, 0.5, 0.6, 0.08);
}

} // namespace wards
} // namespace town_generator
