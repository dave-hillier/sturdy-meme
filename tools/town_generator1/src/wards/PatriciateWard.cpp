#include "town_generator/wards/PatriciateWard.h"
#include "town_generator/building/Model.h"

namespace town_generator {
namespace wards {

void PatriciateWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Larger buildings, regular layout - wealthy mansions
    createAlleys(block, 60, 0.2, 0.4, 0.1);
}

} // namespace wards
} // namespace town_generator
