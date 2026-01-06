#include "town_generator/wards/GateWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void GateWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Mixed use near gate (faithful to MFCG)
    AlleyParams params = AlleyParams::createUrban();
    params.emptyProb = 0.04;  // 4% empty lots

    createAlleysFaithful(block, params);

    // Apply density-based filtering near walls/gates (faithful to mfcg.js Ward.filter)
    filterOutskirts();
}

} // namespace wards
} // namespace town_generator
