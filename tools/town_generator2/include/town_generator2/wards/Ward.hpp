#pragma once

#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/geom/GeomUtils.hpp"
#include "town_generator2/building/Patch.hpp"
#include "town_generator2/building/Cutter.hpp"
#include "town_generator2/utils/Random.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <limits>

namespace town_generator2 {

namespace building { class Model; }

namespace wards {

/**
 * Ward - Base class for city districts
 */
class Ward {
public:
    static constexpr double MAIN_STREET = 2.0;
    static constexpr double REGULAR_STREET = 1.0;
    static constexpr double ALLEY = 0.6;

    building::Model* model;
    building::Patch* patch;
    std::vector<geom::Polygon> geometry;

    Ward(building::Model* model_, building::Patch* patch_)
        : model(model_), patch(patch_) {}

    virtual ~Ward() = default;

    virtual void createGeometry() {
        geometry.clear();
    }

    virtual std::string getLabel() const { return ""; }

    /**
     * Get city block polygon inset from patch edges by street widths
     */
    geom::Polygon getCityBlock();

    /**
     * Rate how suitable this patch is for this ward type
     * Lower values are better
     */
    static double rateLocation(building::Model* model, building::Patch* patch) {
        return 0;
    }

    /**
     * Recursively subdivide polygon into building lots
     */
    static std::vector<geom::Polygon> createAlleys(
        const geom::Polygon& p,
        double minSq,
        double gridChaos,
        double sizeChaos,
        double emptyProb = 0.04,
        bool split = true
    );

    /**
     * Create orthogonal building subdivisions
     */
    static std::vector<geom::Polygon> createOrthoBuilding(
        const geom::Polygon& poly,
        double minBlockSq,
        double fill
    );

protected:
    void filterOutskirts();

private:
    static geom::PointPtr findLongestEdge(const geom::Polygon& poly);
};

/**
 * CommonWard - Parametric ward for generic residential/commercial areas
 */
class CommonWard : public Ward {
public:
    CommonWard(building::Model* model_, building::Patch* patch_,
               double minSq_, double gridChaos_, double sizeChaos_,
               double emptyProb_ = 0.04)
        : Ward(model_, patch_)
        , minSq(minSq_)
        , gridChaos(gridChaos_)
        , sizeChaos(sizeChaos_)
        , emptyProb(emptyProb_) {}

    void createGeometry() override;

protected:
    double minSq;
    double gridChaos;
    double sizeChaos;
    double emptyProb;
};

// Forward declarations for all ward types
class CraftsmenWard;
class MerchantWard;
class PatriciateWard;
class Slum;
class Market;
class Park;
class GateWard;
class Cathedral;
class Castle;
class MilitaryWard;
class Farm;
class AdministrationWard;

} // namespace wards
} // namespace town_generator2
