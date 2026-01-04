#include "town_generator/wards/GateWard.h"
#include "town_generator/building/Model.h"

namespace town_generator {
namespace wards {

void GateWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Mixed use near gate - inns, stables, workshops
    createAlleys(block, 25, 0.5, 0.7, 0.15);
}

} // namespace wards
} // namespace town_generator
