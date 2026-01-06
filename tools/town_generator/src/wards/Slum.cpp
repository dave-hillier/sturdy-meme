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

    // Cramped housing (faithful to MFCG)
    AlleyParams params = AlleyParams::createUrban();
    params.emptyProb = 0.03;  // 3% empty lots (densely packed)

    createAlleysFaithful(block, params);
}

} // namespace wards
} // namespace town_generator
