/**
 * Ported from: Source/com/watabou/towngenerator/wards/Farm.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <cmath>

#include "../wards/Ward.hpp"
#include "../building/Patch.hpp"
#include "../geom/Polygon.hpp"
#include "../utils/Random.hpp"
#include "../geom/GeomUtils.hpp"

namespace town {

// Forward declaration
class Model;

class Farm : public Ward {
public:
    Farm(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : Ward(model, patch) {}

    void createGeometry() override {
        Polygon housing = Polygon::rect(4.0f, 4.0f);
        Point randomVert = *patch->shape.random();
        Point centroid = patch->shape.centroid();  // centroid() returns Point value
        Point pos = GeomUtils::interpolate(
            randomVert,
            centroid,
            0.3f + Random::getFloat() * 0.4f
        );
        housing.rotate(Random::getFloat() * static_cast<float>(M_PI));
        housing.offset(pos);

        geometry = Ward::createOrthoBuilding(housing, 8.0f, 0.5f);
    }

    std::string getLabel() const override {
        return "Farm";
    }
};

} // namespace town
