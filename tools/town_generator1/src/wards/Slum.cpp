#include "town_generator/wards/Slum.h"
#include "town_generator/building/Model.h"

namespace town_generator {
namespace wards {

void Slum::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // High density, chaotic layout - cramped housing
    createAlleys(block, 12, 0.8, 0.9, 0.02);
}

} // namespace wards
} // namespace town_generator
