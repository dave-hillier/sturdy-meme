#include "town_generator/building/District.h"
#include "town_generator/building/City.h"
#include "town_generator/wards/CommonWard.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace building {

District::District(Cell* startPatch, City* model)
    : model(model)
{
    if (startPatch && startPatch->ward) {
        type = startPatch->ward->getName();
        ward = startPatch->ward;
        urban = startPatch->withinWalls;
    }
}

void District::build() {
    if (cells.empty()) return;

    // Create the combined border from all cells
    border = City::findCircumference(cells);

    // Create shared parameters
    createParams();
}

void District::createParams() {
    // Faithful to mfcg.js District.createParams()

    // minSq: 15 + 40 * abs(normal4 - 1)
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

    // minFront derived from minSq
    alleys.minFront = std::sqrt(alleys.minSq);

    // greenery: normal3^2 (or ^1 for parks)
    normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
              utils::Random::floatVal()) / 3.0;
    greenery = (type == "Park") ? normal3 : normal3 * normal3;

    // Adjust for sprawl (outer areas)
    if (!urban) {
        alleys.gridChaos *= 0.5;
        alleys.blockSize *= 2.0;
        greenery = (1.0 + greenery) / 2.0;
    }
}

geom::Polygon District::getShape() const {
    return border;
}

void District::createGeometry() {
    // For now, delegate to each patch's ward
    // In a more complete implementation, this would create unified geometry
    // across the district boundary
    for (auto* patch : cells) {
        if (patch->ward) {
            patch->ward->createGeometry();
        }
    }
}

// DistrictBuilder implementation

std::vector<std::unique_ptr<District>> DistrictBuilder::build() {
    std::vector<std::unique_ptr<District>> districts;

    // Get all city cells
    std::vector<Cell*> unassigned;
    for (auto* patch : model_->cells) {
        if (patch->withinCity && !patch->waterbody && patch->ward) {
            unassigned.push_back(patch);
        }
    }

    // Group cells into districts
    while (!unassigned.empty()) {
        Cell* seed = unassigned.front();

        auto district = std::make_unique<District>(seed, model_);
        district->cells = growDistrict(seed, unassigned);
        district->build();

        districts.push_back(std::move(district));
    }

    return districts;
}

std::vector<Cell*> DistrictBuilder::growDistrict(Cell* seed, std::vector<Cell*>& unassigned) {
    std::vector<Cell*> result;
    result.push_back(seed);

    // Remove seed from unassigned
    auto it = std::find(unassigned.begin(), unassigned.end(), seed);
    if (it != unassigned.end()) {
        unassigned.erase(it);
    }

    std::string seedType = seed->ward ? seed->ward->getName() : "";

    // Grow by adding neighbors of same type
    // Use probability to control district size (faithful to mfcg.js pickFaces)
    bool keepGrowing = true;
    while (keepGrowing && !unassigned.empty()) {
        // Find neighbors of current district that match type
        std::vector<Cell*> candidates;
        for (auto* patch : result) {
            for (auto* neighbor : patch->neighbors) {
                // Check if neighbor is in unassigned and same type
                auto neighborIt = std::find(unassigned.begin(), unassigned.end(), neighbor);
                if (neighborIt != unassigned.end()) {
                    std::string neighborType = neighbor->ward ? neighbor->ward->getName() : "";
                    if (neighborType == seedType) {
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
        double stopProb = static_cast<double>(result.size() - 3) / result.size();
        if (stopProb < 0) stopProb = 0;
        if (result.size() > 1 && unassigned.size() > 1 && utils::Random::floatVal() < stopProb) {
            break;
        }

        // Add a random candidate
        size_t idx = static_cast<size_t>(utils::Random::floatVal() * candidates.size());
        if (idx >= candidates.size()) idx = candidates.size() - 1;

        Cell* chosen = candidates[idx];
        result.push_back(chosen);

        // Remove from unassigned
        it = std::find(unassigned.begin(), unassigned.end(), chosen);
        if (it != unassigned.end()) {
            unassigned.erase(it);
        }
    }

    return result;
}

} // namespace building
} // namespace town_generator
