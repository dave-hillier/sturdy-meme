#include "town_generator/wards/Slum.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"

namespace town_generator {
namespace wards {

void Slum::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // MFCG SPRAWL type parameters (line 11321):
    // - gridChaos *= 0.5 (more regular layout)
    // - blockSize *= 2 (larger lots)
    // - greenery = (1 + greenery) / 2 (more green space)
    AlleyParams params = AlleyParams::createUrban();
    params.gridChaos *= 0.5;      // More regular than urban
    params.blockSize *= 2.0;      // Larger lots
    params.emptyProb = 0.15;      // More empty lots (greenery)

    createAlleys(block, params);
}

} // namespace wards
} // namespace town_generator
