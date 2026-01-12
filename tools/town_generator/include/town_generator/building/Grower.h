#pragma once

#include "town_generator/building/Cell.h"
#include "town_generator/building/District.h"
#include "town_generator/building/EdgeData.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <algorithm>
#include <memory>

namespace town_generator {
namespace building {

// Forward declarations
class District;

/**
 * District type indices (matching original Haxe enum)
 */
enum class DistrictType {
    CENTRAL = 0,
    CASTLE = 1,
    DOCKS = 2,
    BRIDGE = 3,
    GATE = 4,
    PARK = 5,
    SPRAWL = 6,
    SLUM = 7
};

/**
 * Grower - Base class for district growth algorithms
 *
 * Handles expansion of districts by adding neighboring patches.
 * Faithful port from mfcg-clean/model/Grower.js
 */
class Grower {
public:
    District* district;
    double rate;

    /**
     * Create a grower for a district
     * @param district The district to grow
     * @param typeIndex District type index for growth rate
     */
    explicit Grower(District* district, int typeIndex = 0)
        : district(district)
        , rate(1.0)
    {
        // Set growth rate based on district type
        switch (static_cast<DistrictType>(typeIndex)) {
            case DistrictType::CASTLE:
            case DistrictType::BRIDGE:
            case DistrictType::GATE:
                rate = 0.1;
                break;
            case DistrictType::PARK:
                rate = 0.5;
                break;
            default:
                rate = 1.0;
        }
    }

    virtual ~Grower() = default;

    /**
     * Attempt to grow the district by one cell
     * @param availableCells Cells available for expansion (modified in place)
     * @return True if growth occurred or should continue
     */
    virtual bool grow(std::vector<Cell*>& availableCells) {
        if (rate == 0) {
            return false;
        }

        // Random chance to skip based on rate
        double skipChance = 1.0 - rate;
        if (utils::Random::floatVal() < skipChance) {
            return true; // Continue but don't grow this step
        }

        // Find candidate cells adjacent to district
        std::vector<Cell*> candidates;

        for (Cell* cell : district->cells) {
            // Get neighbors of this cell
            for (Cell* neighbor : cell->neighbors) {
                if (!neighbor) continue;

                // Check if neighbor is available
                auto it = std::find(availableCells.begin(), availableCells.end(), neighbor);
                if (it == availableCells.end()) continue;

                // Validate this candidate
                double patchScore = validatePatch(cell, neighbor);
                double edgeScore = validateEdge(cell, neighbor);
                double score = patchScore * edgeScore;

                if (utils::Random::floatVal() < score) {
                    // Check if not already in candidates
                    if (std::find(candidates.begin(), candidates.end(), neighbor) == candidates.end()) {
                        candidates.push_back(neighbor);
                    }
                }
            }
        }

        // Pick a random candidate and add to district
        if (!candidates.empty()) {
            size_t idx = static_cast<size_t>(utils::Random::floatVal() * candidates.size());
            Cell* chosen = candidates[idx];

            district->cells.push_back(chosen);

            // Remove from available list
            auto it = std::find(availableCells.begin(), availableCells.end(), chosen);
            if (it != availableCells.end()) {
                availableCells.erase(it);
            }

            return true;
        }

        return false;
    }

    /**
     * Validate whether a patch can be added to district
     * @param currentCell Cell already in district
     * @param candidateCell Cell being considered
     * @return Score 0-1, higher = more likely to add
     */
    virtual double validatePatch(Cell* currentCell, Cell* candidateCell) {
        // Default: prefer cells with matching landing status
        return (currentCell->landing == candidateCell->landing) ? 1.0 : 0.0;
    }

    /**
     * Validate edge between cells
     * @param currentCell Cell in district
     * @param candidateCell Cell being considered
     * @return Score 0-1
     */
    virtual double validateEdge(Cell* currentCell, Cell* candidateCell) {
        // Find the edge index between these cells by checking neighbors
        size_t neighborIdx = 0;
        for (size_t i = 0; i < currentCell->neighbors.size(); ++i) {
            if (currentCell->neighbors[i] == candidateCell) {
                neighborIdx = i;
                break;
            }
        }

        // Get edge type at that index
        EdgeType edgeType = currentCell->getEdgeType(neighborIdx);

        switch (edgeType) {
            case EdgeType::ROAD:
                return 0.9; // Slightly less likely to cross roads
            case EdgeType::WALL:
            case EdgeType::WATER:
                return 0.0; // Cannot cross walls or water
            default:
                return 1.0;
        }
    }
};

/**
 * DocksGrower - Grower for dock/harbor districts
 * Prefers landing areas with Alleys wards
 */
class DocksGrower : public Grower {
public:
    explicit DocksGrower(District* district)
        : Grower(district, static_cast<int>(DistrictType::DOCKS))
    {}

    double validatePatch(Cell* currentCell, Cell* candidateCell) override {
        // Must be a landing area
        if (candidateCell->landing) {
            // Check if has appropriate ward type (Alleys)
            if (candidateCell->ward && candidateCell->ward->getName() == "Alleys") {
                return 1.0;
            }
        }
        return 0.0;
    }
};

/**
 * ParkGrower - Grower for park districts
 * Only accepts cells with Park wards
 */
class ParkGrower : public Grower {
public:
    explicit ParkGrower(District* district)
        : Grower(district, static_cast<int>(DistrictType::PARK))
    {}

    double validatePatch(Cell* currentCell, Cell* candidateCell) override {
        // Must have a Park ward
        if (candidateCell->ward && candidateCell->ward->getName() == "Park") {
            return 1.0;
        }
        return 0.0;
    }
};

/**
 * Factory function to create appropriate grower for district type
 */
inline std::unique_ptr<Grower> createGrower(District* district, DistrictType type) {
    switch (type) {
        case DistrictType::DOCKS:
            return std::make_unique<DocksGrower>(district);
        case DistrictType::PARK:
            return std::make_unique<ParkGrower>(district);
        default:
            return std::make_unique<Grower>(district, static_cast<int>(type));
    }
}

} // namespace building
} // namespace town_generator
