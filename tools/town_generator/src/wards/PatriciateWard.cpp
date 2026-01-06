#include "town_generator/wards/PatriciateWard.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void PatriciateWard::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Wealthy mansions (faithful to MFCG)
    AlleyParams params = AlleyParams::createUrban();
    params.emptyProb = 0.2;  // 20% empty lots for gardens

    createAlleysFaithful(block, params);
}

} // namespace wards
} // namespace town_generator
