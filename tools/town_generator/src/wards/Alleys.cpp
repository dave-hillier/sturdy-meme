#include "town_generator/wards/Alleys.h"
#include "town_generator/building/Cell.h"
#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Block.h"

namespace town_generator {
namespace wards {

void Alleys::createGeometry() {
    // Faithful to mfcg.js Alleys.createGeometry (lines 99-102)
    // The geometry is created by WardGroup, not by individual cells.
    // Only the core cell triggers geometry creation and stores the result.
    if (!patch) return;

    if (!patch->group) {
        // No group - fall back to per-cell geometry using base Ward method
        // This shouldn't happen for properly grouped Alleys wards
        Ward::createGeometry();
        return;
    }

    // Only core cell creates and stores geometry (non-core wards have empty geometry)
    // SVG writer should only render from core wards to avoid duplication
    if (patch->group->core == patch) {
        patch->group->createGeometry();

        // Copy block geometry to this ward for SVG rendering
        geometry.clear();
        alleys.clear();

        for (const auto& block : patch->group->blocks) {
            for (const auto& building : block->buildings) {
                geometry.push_back(building);
            }
        }
    }

    trees_.clear();
}

std::vector<geom::Point> Alleys::spawnTrees() {
    // Faithful to mfcg.js Alleys.spawnTrees (lines 103-118)
    // Trees are spawned by the group's blocks
    if (!patch || !patch->group) return {};

    if (patch->group->core == patch && trees_.empty()) {
        trees_ = patch->group->spawnTrees();
    }

    return trees_;
}

} // namespace wards
} // namespace town_generator
