#pragma once

#include "town_generator/utils/Random.h"
#include <string>
#include <optional>

namespace town_generator {
namespace building {

/**
 * Blueprint - City generation blueprint/parameters
 *
 * Defines all the parameters that control city generation.
 * Faithful port from mfcg-clean/model/Blueprint.js
 */
class Blueprint {
public:
    // Core parameters
    int size;           // City size (number of patches)
    int seed;           // Random seed

    // City metadata
    std::string name;   // City name (optional)
    int population;     // Population estimate

    // Feature flags
    bool citadel;       // Has citadel/fortress
    bool inner;         // Urban castle (stadtburg) inside walls
    bool plaza;         // Has central plaza/market
    bool temple;        // Has temple/cathedral
    bool walls;         // Has city walls
    bool shanty;        // Has shantytown outside walls
    bool coast;         // Has coastline
    bool river;         // Has river
    bool greens;        // Has green spaces/parks
    bool hub;           // Is a hub city (multiple gates)

    // Additional parameters
    std::optional<double> coastDir;  // Coast direction (angle)
    int gates;          // Number of gates (-1 for auto)
    bool random;        // Whether parameters were randomized

    // Style/export options
    std::string style;  // Rendering style
    std::string exportFormat;  // Export format

    /**
     * Create a blueprint with basic parameters
     * @param size City size (number of patches)
     * @param seed Random seed
     */
    Blueprint(int size, int seed)
        : size(size)
        , seed(seed)
        , name("")
        , population(0)
        , citadel(true)
        , inner(false)
        , plaza(true)
        , temple(true)
        , walls(true)
        , shanty(false)
        , coast(true)
        , river(true)
        , greens(false)
        , hub(false)
        , coastDir(std::nullopt)
        , gates(-1)
        , random(true)
        , style("")
        , exportFormat("")
    {}

    /**
     * Create a randomized blueprint
     * @param size City size
     * @param seed Random seed
     * @return Randomized blueprint
     */
    static Blueprint create(int size, int seed) {
        utils::Random::reset(seed);

        Blueprint bp(size, seed);
        bp.name = "";
        bp.population = 0;
        bp.greens = false;
        bp.random = true;

        // Randomize features based on size
        double wallsChance = (size + 30.0) / 80.0;
        bp.walls = utils::Random::boolVal(wallsChance);

        double shantyChance = size / 80.0;
        bp.shanty = utils::Random::boolVal(shantyChance);

        double citadelChance = 0.5 + size / 100.0;
        bp.citadel = utils::Random::boolVal(citadelChance);

        double innerChance = bp.walls ? size / (size + 30.0) : 0.5;
        bp.inner = utils::Random::boolVal(innerChance);

        bp.plaza = utils::Random::boolVal(0.9);

        double templeChance = size / 18.0;
        bp.temple = utils::Random::boolVal(templeChance);

        bp.river = utils::Random::boolVal(0.667);
        bp.coast = utils::Random::boolVal(0.5);

        return bp;
    }

    /**
     * Create a similar blueprint (same parameters, new seed)
     * @param original Original blueprint
     * @return Similar blueprint with new seed
     */
    static Blueprint similar(const Blueprint& original) {
        Blueprint bp = create(original.size, original.seed);
        bp.name = original.name;
        return bp;
    }

    /**
     * Clone this blueprint
     * @return Copy of blueprint
     */
    Blueprint clone() const {
        Blueprint bp(size, seed);
        bp.name = name;
        bp.population = population;
        bp.citadel = citadel;
        bp.inner = inner;
        bp.plaza = plaza;
        bp.temple = temple;
        bp.walls = walls;
        bp.shanty = shanty;
        bp.coast = coast;
        bp.coastDir = coastDir;
        bp.river = river;
        bp.greens = greens;
        bp.hub = hub;
        bp.gates = gates;
        bp.random = random;
        bp.style = style;
        bp.exportFormat = exportFormat;
        return bp;
    }

    /**
     * Get estimated population based on size
     * @return Population estimate
     */
    int estimatePopulation() const {
        if (population > 0) return population;
        // Rough estimate: ~100 people per patch
        return size * 100;
    }
};

} // namespace building
} // namespace town_generator
