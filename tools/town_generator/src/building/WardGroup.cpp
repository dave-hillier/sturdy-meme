#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Block.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/Cutter.h"
#include "town_generator/utils/Random.h"
#include "town_generator/wards/Ward.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace building {

// Forward declaration of helper function
static void subdivideIntoBlocksHelper(
    const geom::Polygon& area,
    const wards::AlleyParams& params,
    std::vector<geom::Polygon>& result
);

WardGroup::WardGroup(Model* model) : model(model) {}

void WardGroup::addPatch(Patch* patch) {
    if (!patch) return;

    patches.push_back(patch);
    patch->group = this;

    if (!core) {
        core = patch;
        urban = patch->withinWalls;
    }
}

void WardGroup::buildBorder() {
    if (patches.empty()) return;

    if (patches.size() == 1) {
        border = patches[0]->shape;
    } else {
        // Use Model::findCircumference to get combined border
        border = Model::findCircumference(patches);
    }

    // Compute inner vertices after building border (faithful to mfcg.js)
    computeInnerVertices();
}

void WardGroup::createParams() {
    // Faithful to mfcg.js District.createParams()
    // Uses normal3 and normal4 distributions for natural variation

    // minSq: 15 + 40 * abs(normal4 - 1) where normal4 is sum of 4 randoms / 2
    double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    alleys.minSq = 15.0 + 40.0 * std::abs(normal4);

    // gridChaos: 0.2 + normal3 * 0.8
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                     utils::Random::floatVal()) / 3.0;
    alleys.gridChaos = 0.2 + normal3 * 0.8;

    // sizeChaos: 0.4 + normal3 * 0.6
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.sizeChaos = 0.4 + normal3 * 0.6;

    // shapeFactor: 0.25 + normal3 * 2
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.shapeFactor = 0.25 + normal3 * 2.0;

    // inset: 0.6 * (1 - abs(normal4))
    normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
    alleys.inset = 0.6 * (1.0 - std::abs(normal4));

    // blockSize: 4 + 10 * normal3
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    alleys.blockSize = 4.0 + 10.0 * normal3;

    // Compute derived values
    alleys.computeDerived();

    // greenery: normal3^2 (or ^1 for parks)
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    std::string typeName = getTypeName();
    greenery = (typeName == "Park") ? normal3 : normal3 * normal3;

    // Adjust for sprawl (outer areas)
    if (!urban) {
        alleys.gridChaos *= 0.5;
        alleys.blockSize *= 2.0;
        greenery = (1.0 + greenery) / 2.0;
    }
}

void WardGroup::createGeometry() {
    if (border.length() < 3) {
        buildBorder();
    }

    if (border.length() < 3) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WardGroup: Cannot create geometry with invalid border");
        return;
    }

    createParams();

    // Get available area after street/wall insets
    // For now, use simplified inset based on alleys.inset
    std::vector<double> insets(border.length(), alleys.inset);
    geom::Polygon available = border.shrink(insets);

    if (available.length() < 3) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WardGroup: Available area too small after inset");
        return;
    }

    // Create blocks by recursive subdivision
    // Faithful to mfcg.js createAlleys
    std::vector<geom::Polygon> blockShapes;
    subdivideIntoBlocksHelper(available, alleys, blockShapes);

    // Create Block objects from shapes
    blocks.clear();
    for (const auto& shape : blockShapes) {
        auto block = std::make_unique<Block>(shape, this);
        block->createLots();
        block->createRects();
        block->createBuildings();
        blocks.push_back(std::move(block));
    }

    SDL_Log("WardGroup: Created %zu blocks from %zu patches", blocks.size(), patches.size());
}

std::vector<geom::Point> WardGroup::spawnTrees() {
    std::vector<geom::Point> trees;

    for (const auto& block : blocks) {
        auto blockTrees = block->spawnTrees();
        trees.insert(trees.end(), blockTrees.begin(), blockTrees.end());
    }

    return trees;
}

bool WardGroup::canAddPatch(Patch* patch) const {
    if (!patch || !patch->ward) return false;
    if (patches.empty()) return true;

    // Must be same ward type
    std::string typeName = getTypeName();
    if (patch->ward->getName() != typeName) return false;

    // Must be adjacent to at least one patch in the group
    for (Patch* existing : patches) {
        for (Patch* neighbor : existing->neighbors) {
            if (neighbor == patch) return true;
        }
    }

    return false;
}

std::string WardGroup::getTypeName() const {
    if (patches.empty() || !patches[0]->ward) return "";
    return patches[0]->ward->getName();
}

bool WardGroup::isInnerVertex(const geom::Point& v) const {
    // Faithful to mfcg.js District.isInnerVertex (lines 979-984)
    // A vertex is "inner" if ALL adjacent patches are withinCity OR waterbody
    if (!model) return false;

    auto adjacentPatches = model->patchByVertex(v);
    for (auto* p : adjacentPatches) {
        if (!p->withinCity && !p->waterbody) {
            return false;
        }
    }
    return true;
}

void WardGroup::computeInnerVertices() {
    // Faithful to mfcg.js District constructor (lines 719-724)
    // For each border vertex, check if withinWalls OR isInnerVertex
    inner.clear();

    if (border.length() < 3) return;

    for (size_t i = 0; i < border.length(); ++i) {
        const geom::Point& v = border[i];

        // Check if any patch at this vertex is withinWalls
        bool withinWalls = false;
        if (model) {
            auto adjacentPatches = model->patchByVertex(v);
            for (auto* p : adjacentPatches) {
                if (p->withinWalls) {
                    withinWalls = true;
                    break;
                }
            }
        }

        // A vertex is "inner" if withinWalls OR isInnerVertex
        if (withinWalls || isInnerVertex(v)) {
            inner.push_back(v);
        }
    }

    // District is "urban" if all border vertices are inner
    urban = (inner.size() == border.length());
}

// Helper function to subdivide area into blocks
static void subdivideIntoBlocksHelper(
    const geom::Polygon& area,
    const wards::AlleyParams& params,
    std::vector<geom::Polygon>& result
) {
    double areaSize = std::abs(area.square());
    double threshold = params.minSq * params.blockSize;

    // Apply size chaos to threshold
    double chaosMultiplier = std::pow(2.0, params.sizeChaos * (2.0 * utils::Random::floatVal() - 1.0));
    threshold *= chaosMultiplier;

    // If area is small enough, it's a block
    if (areaSize < threshold || area.length() < 3) {
        if (areaSize > params.minSq / 4.0) {
            result.push_back(area);
        }
        return;
    }

    // Find longest edge for bisection
    size_t longestEdge = 0;
    double longestLen = 0;
    for (size_t i = 0; i < area.length(); ++i) {
        geom::Point v = area.vectori(static_cast<int>(i));
        double len = v.length();
        if (len > longestLen) {
            longestLen = len;
            longestEdge = i;
        }
    }

    // Calculate cut ratio with grid chaos
    double spread = 0.8 * params.gridChaos;
    double ratio = (1.0 - spread) / 2.0 + utils::Random::floatVal() * spread;

    // Angle spread for larger blocks (faithful to mfcg.js)
    double angleSpread = M_PI / 6.0 * params.gridChaos * (areaSize < params.minSq * 4 ? 0.0 : 1.0);
    double angle = (utils::Random::floatVal() - 0.5) * angleSpread;

    // Gap for alleys
    double gap = wards::Ward::ALLEY;

    // Bisect the area
    auto halves = Cutter::bisect(area, area[longestEdge], ratio, angle, gap);

    if (halves.size() < 2) {
        // Bisection failed, treat as a block
        if (areaSize > params.minSq / 4.0) {
            result.push_back(area);
        }
        return;
    }

    // Recursively subdivide each half
    for (const auto& half : halves) {
        subdivideIntoBlocksHelper(half, params, result);
    }
}

// WardGroupBuilder implementation

std::vector<std::unique_ptr<WardGroup>> WardGroupBuilder::build() {
    std::vector<std::unique_ptr<WardGroup>> groups;

    // Get all city patches with wards that support grouping
    std::vector<Patch*> unassigned;
    for (auto* patch : model_->patches) {
        if (patch->withinCity && !patch->waterbody && patch->ward) {
            // Only group "Alleys" type wards (CommonWard, MerchantWard, CraftsmenWard, etc.)
            std::string wardName = patch->ward->getName();
            if (wardName == "CommonWard" || wardName == "MerchantWard" ||
                wardName == "CraftsmenWard" || wardName == "PatriciateWard" ||
                wardName == "Slum" || wardName == "AdministrationWard" ||
                wardName == "MilitaryWard") {
                unassigned.push_back(patch);
            }
        }
    }

    // Group patches into WardGroups
    while (!unassigned.empty()) {
        Patch* seed = unassigned.front();
        auto group = std::make_unique<WardGroup>(model_);
        group->addPatch(seed);

        // Remove seed from unassigned
        unassigned.erase(unassigned.begin());

        // Grow the group by adding adjacent patches of the same type
        growGroup(group.get(), unassigned);

        // Build the group border
        group->buildBorder();

        groups.push_back(std::move(group));
    }

    return groups;
}

void WardGroupBuilder::growGroup(WardGroup* group, std::vector<Patch*>& unassigned) {
    if (!group || unassigned.empty()) return;

    std::string typeName = group->getTypeName();
    bool keepGrowing = true;

    while (keepGrowing && !unassigned.empty()) {
        // Find candidates: neighbors of current group patches that are in unassigned
        std::vector<Patch*> candidates;
        for (Patch* patch : group->patches) {
            for (Patch* neighbor : patch->neighbors) {
                // Check if neighbor is in unassigned
                auto it = std::find(unassigned.begin(), unassigned.end(), neighbor);
                if (it != unassigned.end()) {
                    // Check if same ward type
                    if (neighbor->ward && neighbor->ward->getName() == typeName) {
                        // Not already a candidate
                        if (std::find(candidates.begin(), candidates.end(), neighbor) == candidates.end()) {
                            candidates.push_back(neighbor);
                        }
                    }
                }
            }
        }

        if (candidates.empty()) {
            break;
        }

        // Probability to stop growing increases with size (faithful to mfcg.js)
        double stopProb = static_cast<double>(group->patches.size() - 3) / group->patches.size();
        if (stopProb < 0) stopProb = 0;
        if (group->patches.size() > 1 && unassigned.size() > 1 && utils::Random::floatVal() < stopProb) {
            break;
        }

        // Add a random candidate
        size_t idx = static_cast<size_t>(utils::Random::floatVal() * candidates.size());
        if (idx >= candidates.size()) idx = candidates.size() - 1;

        Patch* chosen = candidates[idx];
        group->addPatch(chosen);

        // Remove from unassigned
        auto it = std::find(unassigned.begin(), unassigned.end(), chosen);
        if (it != unassigned.end()) {
            unassigned.erase(it);
        }
    }
}

} // namespace building
} // namespace town_generator
