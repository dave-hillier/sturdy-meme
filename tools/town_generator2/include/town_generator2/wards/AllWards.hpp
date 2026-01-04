#pragma once

#include "town_generator2/wards/Ward.hpp"
#include "town_generator2/building/Model.hpp"
#include "town_generator2/building/CurtainWall.hpp"
#include "town_generator2/geom/GeomUtils.hpp"
#include <cmath>
#include <typeinfo>

namespace town_generator2 {
namespace wards {

// Forward declarations for rateLocation methods that need type info
class Park;
class Slum;

/**
 * CraftsmenWard - Small to large buildings, moderately regular
 */
class CraftsmenWard : public CommonWard {
public:
    CraftsmenWard(building::Model* model_, building::Patch* patch_)
        : CommonWard(model_, patch_,
            10 + 80 * utils::Random::getFloat() * utils::Random::getFloat(),
            0.5 + utils::Random::getFloat() * 0.2,
            0.6) {}

    std::string getLabel() const override { return "Craftsmen"; }
};

/**
 * MerchantWard - Medium to large buildings, prefers center
 */
class MerchantWard : public CommonWard {
public:
    MerchantWard(building::Model* model_, building::Patch* patch_)
        : CommonWard(model_, patch_,
            50 + 60 * utils::Random::getFloat() * utils::Random::getFloat(),
            0.5 + utils::Random::getFloat() * 0.3,
            0.7,
            0.15) {}

    std::string getLabel() const override { return "Merchant"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        geom::Point target = model->plaza
            ? model->plaza->shape.center()
            : *model->center;
        return patch->shape.distance(target);
    }
};

/**
 * Slum - Small to medium buildings, chaotic, prefers city edge
 * (Defined early so PatriciateWard can use it)
 */
class Slum : public CommonWard {
public:
    Slum(building::Model* model_, building::Patch* patch_)
        : CommonWard(model_, patch_,
            10 + 30 * utils::Random::getFloat() * utils::Random::getFloat(),
            0.6 + utils::Random::getFloat() * 0.4,
            0.8,
            0.03) {}

    std::string getLabel() const override { return "Slum"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        geom::Point target = model->plaza
            ? model->plaza->shape.center()
            : *model->center;
        return -patch->shape.distance(target);
    }
};

/**
 * Park - Green space with radial paths
 * (Defined early so PatriciateWard can use it)
 */
class Park : public Ward {
public:
    using Ward::Ward;

    void createGeometry() override {
        geom::Polygon block = getCityBlock();
        geometry = block.compactness() >= 0.7
            ? building::Cutter::radial(block, nullptr, Ward::ALLEY)
            : building::Cutter::semiRadial(block, nullptr, Ward::ALLEY);
    }

    std::string getLabel() const override { return "Park"; }
};

/**
 * PatriciateWard - Large buildings, prefers parks, avoids slums
 */
class PatriciateWard : public CommonWard {
public:
    PatriciateWard(building::Model* model_, building::Patch* patch_)
        : CommonWard(model_, patch_,
            80 + 30 * utils::Random::getFloat() * utils::Random::getFloat(),
            0.5 + utils::Random::getFloat() * 0.3,
            0.8,
            0.2) {}

    std::string getLabel() const override { return "Patriciate"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        int rate = 0;
        for (auto* p : model->patches) {
            if (p->ward && p->shape.borders(patch->shape)) {
                if (dynamic_cast<Park*>(p->ward)) rate--;
                else if (dynamic_cast<Slum*>(p->ward)) rate++;
            }
        }
        return rate;
    }
};

/**
 * Market - Central plaza with fountain or statue
 */
class Market : public Ward {
public:
    using Ward::Ward;

    void createGeometry() override {
        geometry.clear();

        bool statue = utils::Random::getBool(0.6);
        bool offset = statue || utils::Random::getBool(0.3);

        geom::PointPtr v0, v1;
        if (statue || offset) {
            double length = -1.0;
            patch->shape.forEdgePtr([&](const geom::PointPtr& p0, const geom::PointPtr& p1) {
                double len = geom::Point::distance(*p0, *p1);
                if (len > length) {
                    length = len;
                    v0 = p0;
                    v1 = p1;
                }
            });
        }

        geom::Polygon object;
        if (statue) {
            object = geom::Polygon::rect(1 + utils::Random::getFloat(), 1 + utils::Random::getFloat());
            object.rotate(std::atan2(v1->y - v0->y, v1->x - v0->x));
        } else {
            object = geom::Polygon::circle(1 + utils::Random::getFloat());
        }

        if (offset) {
            geom::Point gravity = geom::GeomUtils::interpolate(*v0, *v1);
            geom::Point pos = geom::GeomUtils::interpolate(patch->shape.centroid(), gravity,
                0.2 + utils::Random::getFloat() * 0.4);
            object.offset(pos);
        } else {
            object.offset(patch->shape.centroid());
        }

        geometry.push_back(object);
    }

    std::string getLabel() const override { return "Market"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        for (auto* p : model->inner) {
            if (dynamic_cast<Market*>(p->ward) && p->shape.borders(patch->shape)) {
                return std::numeric_limits<double>::infinity();
            }
        }

        if (model->plaza) {
            return patch->shape.square() / model->plaza->shape.square();
        }
        return patch->shape.distance(*model->center);
    }
};

/**
 * GateWard - Ward near city gates
 */
class GateWard : public CommonWard {
public:
    GateWard(building::Model* model_, building::Patch* patch_)
        : CommonWard(model_, patch_,
            10 + 50 * utils::Random::getFloat() * utils::Random::getFloat(),
            0.5 + utils::Random::getFloat() * 0.3,
            0.7) {}

    std::string getLabel() const override { return "Gate"; }
};

/**
 * Cathedral - Religious building with ring or orthogonal layout
 */
class Cathedral : public Ward {
public:
    using Ward::Ward;

    void createGeometry() override {
        geom::Polygon block = getCityBlock();
        if (utils::Random::getBool(0.4)) {
            geometry = building::Cutter::ring(block, 2 + utils::Random::getFloat() * 4);
        } else {
            geometry = Ward::createOrthoBuilding(block, 50, 0.8);
        }
    }

    std::string getLabel() const override { return "Temple"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        if (model->plaza && patch->shape.borders(model->plaza->shape)) {
            return -1.0 / patch->shape.square();
        }

        geom::Point target = model->plaza
            ? model->plaza->shape.center()
            : *model->center;
        return patch->shape.distance(target) * patch->shape.square();
    }
};

/**
 * Castle - Citadel with inner wall
 */
class Castle : public Ward {
public:
    std::unique_ptr<building::CurtainWall> wall;

    Castle(building::Model* model_, building::Patch* patch_);

    void createGeometry() override {
        geom::Polygon block = patch->shape.shrinkEq(Ward::MAIN_STREET * 2);
        double side = std::sqrt(block.square()) * 4;
        geometry = Ward::createOrthoBuilding(block, side, 0.6);
    }

    std::string getLabel() const override { return "Castle"; }
};

/**
 * MilitaryWard - Regular barracks layout
 */
class MilitaryWard : public Ward {
public:
    using Ward::Ward;

    void createGeometry() override {
        geom::Polygon block = getCityBlock();
        double side = std::sqrt(block.square()) * (1 + utils::Random::getFloat());
        geometry = Ward::createAlleys(block, side,
            0.1 + utils::Random::getFloat() * 0.3,
            0.3,
            0.25);
    }

    std::string getLabel() const override { return "Military"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        if (model->citadel && model->citadel->shape.borders(patch->shape)) {
            return 0;
        }
        if (model->wall && model->wall->borders(patch)) {
            return 1;
        }
        return (model->citadel == nullptr && model->wall == nullptr)
            ? 0 : std::numeric_limits<double>::infinity();
    }
};

/**
 * Farm - Rural area with farmhouse
 */
class Farm : public Ward {
public:
    using Ward::Ward;

    void createGeometry() override {
        geom::Polygon housing = geom::Polygon::rect(4, 4);

        size_t randomIdx = utils::Random::getInt(0, patch->shape.length());
        const geom::Point& randomVert = patch->shape[randomIdx];

        geom::Point pos = geom::GeomUtils::interpolate(randomVert, patch->shape.centroid(),
            0.3 + utils::Random::getFloat() * 0.4);
        housing.rotate(utils::Random::getFloat() * M_PI);
        housing.offset(pos);

        geometry = Ward::createOrthoBuilding(housing, 8, 0.5);
    }

    std::string getLabel() const override { return "Farm"; }
};

/**
 * AdministrationWard - Large regular buildings, prefers plaza
 */
class AdministrationWard : public CommonWard {
public:
    AdministrationWard(building::Model* model_, building::Patch* patch_)
        : CommonWard(model_, patch_,
            80 + 30 * utils::Random::getFloat() * utils::Random::getFloat(),
            0.1 + utils::Random::getFloat() * 0.3,
            0.3) {}

    std::string getLabel() const override { return "Administration"; }

    static double rateLocation(building::Model* model, building::Patch* patch) {
        if (model->plaza) {
            if (patch->shape.borders(model->plaza->shape)) {
                return 0;
            }
            return patch->shape.distance(model->plaza->shape.center());
        }
        return patch->shape.distance(*model->center);
    }
};

} // namespace wards
} // namespace town_generator2
