#include "town_generator/wards/CraftsmenWard.h"
#include "town_generator/building/Model.h"

namespace town_generator {
namespace wards {

void CraftsmenWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Medium density, moderate chaos - working class housing
    createAlleys(block, 25, 0.4, 0.6, 0.05);
}

} // namespace wards
} // namespace town_generator
