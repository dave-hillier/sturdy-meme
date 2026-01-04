#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/building/Patch.h"
#include "town_generator/utils/Random.h"
#include <vector>
#include <string>
#include <memory>
#include <cmath>

namespace town_generator {

namespace building {
    class Model;
    class CurtainWall;
}

namespace wards {

/**
 * Ward - Base class for city districts, faithful port from Haxe TownGeneratorOS
 */
class Ward {
public:
    // Street width constants
    static constexpr double MAIN_STREET = 2.0;
    static constexpr double REGULAR_STREET = 1.0;
    static constexpr double ALLEY = 0.6;

    building::Patch* patch = nullptr;
    building::Model* model = nullptr;

    std::vector<geom::Polygon> geometry;

    Ward() = default;
    virtual ~Ward() = default;

    // Get ward name for display/SVG output
    virtual std::string getName() const { return "Ward"; }

    // Get the city block after accounting for streets
    std::vector<double> getCityBlock();

    // Create geometry (buildings)
    virtual void createGeometry();

    // Filter buildings near outskirts
    void filterOutskirts(std::vector<geom::Polygon>& buildings, double minDistance);

    // Create alleys recursively
    void createAlleys(
        const geom::Polygon& p,
        double minArea,
        double gridChaos,
        double sizeChaos,
        double emptyProbability = 0.0,
        double split = 0.0
    );

    // Create orthogonal building
    geom::Polygon createOrthoBuilding(
        const geom::Polygon& poly,
        double fill,
        double ratio = 1.0
    );

    // Equality
    bool operator==(const Ward& other) const {
        return patch == other.patch && model == other.model;
    }

    bool operator!=(const Ward& other) const {
        return !(*this == other);
    }

protected:
    // Helper to get patch's inset shape
    geom::Polygon getInsetShape(double inset);
};

// Forward declarations of all ward types
class Castle;
class Cathedral;
class Market;
class CraftsmenWard;
class MerchantWard;
class PatriciateWard;
class CommonWard;
class AdministrationWard;
class MilitaryWard;
class GateWard;
class Slum;
class Farm;
class Park;

} // namespace wards
} // namespace town_generator
