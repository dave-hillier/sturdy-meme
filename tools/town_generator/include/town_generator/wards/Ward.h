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
    class Block;
}

namespace wards {

/**
 * AlleyParams - Parameters for alley/building generation (from mfcg.js District.createParams)
 * These control the recursive subdivision algorithm
 */
struct AlleyParams {
    double minSq = 15.0;       // Minimum block area
    double gridChaos = 0.5;    // How chaotic the grid is (0=regular, 1=chaotic)
    double sizeChaos = 0.6;    // Variation in building sizes
    double blockSize = 8.0;    // Multiplier for initial subdivision threshold
    double emptyProb = 0.04;   // Probability of empty lots
    double minFront = 4.0;     // Minimum frontage (sqrt(minSq))
    double shapeFactor = 1.0;  // Shape factor for buildings
    double inset = 0.3;        // Inset factor for building edges

    // Compute minFront from minSq
    void computeDerived() {
        minFront = std::sqrt(minSq);
    }

    // Create default urban parameters (faithful to mfcg.js District.createParams)
    static AlleyParams createUrban() {
        AlleyParams p;
        // minSq: 15 + 40 * abs(normal4 - 1)  where normal4 is avg of 4 randoms
        double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                         utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
        p.minSq = 15.0 + 40.0 * std::abs(normal4);

        // gridChaos: 0.2 + normal3 * 0.8
        double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                         utils::Random::floatVal()) / 3.0;
        p.gridChaos = 0.2 + normal3 * 0.8;

        // sizeChaos: 0.4 + normal3 * 0.6
        normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                  utils::Random::floatVal()) / 3.0;
        p.sizeChaos = 0.4 + normal3 * 0.6;

        // shapeFactor: 0.25 + normal3 * 2
        normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                  utils::Random::floatVal()) / 3.0;
        p.shapeFactor = 0.25 + normal3 * 2.0;

        // inset: 0.6 * (1 - abs(normal4))
        normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                  utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0;
        p.inset = 0.6 * (1.0 - std::abs(normal4));

        // blockSize: 4 + 10 * normal3
        normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                  utils::Random::floatVal()) / 3.0;
        p.blockSize = 4.0 + 10.0 * normal3;

        p.computeDerived();
        return p;
    }
};

/**
 * Ward - Base class for city districts, faithful port from Haxe TownGeneratorOS
 */
class Ward {
public:
    // Street width constants (scaled up to match 4x minSq scaling)
    static constexpr double MAIN_STREET = 4.0;
    static constexpr double REGULAR_STREET = 2.0;
    static constexpr double ALLEY = 1.2;

    building::Patch* patch = nullptr;
    building::Model* model = nullptr;

    std::vector<geom::Polygon> geometry;
    std::vector<std::vector<geom::Point>> alleys;  // Alley cut lines for rendering
    std::vector<building::Block*> blocks;  // City blocks (MFCG-style)
    geom::Polygon church;  // Church building if created
    bool urban = true;  // Whether this is an urban ward (affects lot generation)

    Ward() = default;
    virtual ~Ward() = default;

    // Get ward name for display/SVG output
    virtual std::string getName() const { return "Ward"; }

    // Check if ward has a church building
    bool hasChurch() const { return church.length() > 0; }

    // Get the church polygon (for special rendering)
    const geom::Polygon& getChurch() const { return church; }

    // Check if this ward type should be rendered as special/solid (churches, cathedrals, castles)
    virtual bool isSpecialWard() const { return false; }

    // Get the city block after accounting for streets
    std::vector<double> getCityBlock();

    // Create geometry (buildings)
    virtual void createGeometry();

    // Filter buildings near outskirts (faithful to Haxe filterOutskirts)
    void filterOutskirts();

    // Filter inner buildings that don't touch block perimeter
    // Returns the inner (courtyard) lots, removes them from geometry
    // Based on mfcg.js filterInner - buildings that touch perimeter are kept
    void filterInner(const geom::Polygon& blockShape);

    // Create alleys recursively (legacy interface)
    void createAlleys(
        const geom::Polygon& p,
        double minArea,
        double gridChaos,
        double sizeChaos,
        double emptyProbability = 0.0,
        double split = 0.0
    );

    // Create alleys with AlleyParams (faithful to mfcg.js createAlleys)
    // Uses minSq * blockSize for initial subdivision threshold
    void createAlleysWithParams(
        const geom::Polygon& p,
        const AlleyParams& params,
        bool isInitialCall = true
    );

    // Semi-smooth alley corners into arcs (faithful to mfcg.js semiSmooth)
    static std::vector<geom::Point> semiSmooth(
        const geom::Point& p0,
        const geom::Point& p1,
        const geom::Point& p2,
        double minFront
    );

    // Create a church in a medium-sized block (faithful to mfcg.js createChurch)
    void createChurch(const geom::Polygon& block);

    // Create a Block (faithful to mfcg.js createBlock)
    // isSmall: if true, the whole shape becomes a single lot
    void createBlock(const geom::Polygon& shape, bool isSmall);

    // Create alleys using Bisector (faithful to mfcg.js createAlleys)
    void createAlleysFaithful(const geom::Polygon& shape, const AlleyParams& params);

    // Check if block is at blockSize threshold (for non-urban wards)
    bool isBlockSized(const geom::Polygon& shape, const AlleyParams& params);

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

    // Helper to add a building lot with filtering and rectangularization
    // Faithful to mfcg.js createLots filtering and createRects
    void addBuildingLot(const geom::Polygon& lot, double minSq);

    // Check if a polygon is approximately rectangular
    bool isRectangle(const geom::Polygon& poly) const;
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
class Harbour;

} // namespace wards
} // namespace town_generator
