/**
 * Ported from: Source/com/watabou/towngenerator/wards/Market.hx
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
#include "../geom/Point.hpp"
#include "../geom/Polygon.hpp"
#include "../utils/Random.hpp"
#include "../geom/GeomUtils.hpp"

namespace town {

// Forward declaration
class Model;

class Market : public Ward {
public:
    Market(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : Ward(model, patch) {}

    void createGeometry() override {
        // fountain or statue
        bool statue = Random::getBool(0.6f);
        // we always offset a statue and sometimes a fountain
        bool offset = statue || Random::getBool(0.3f);

        Point v0, v1;
        bool hasEdge = false;

        if (statue || offset) {
            // we need an edge both for rotating a statue and offsetting
            float length = -1.0f;
            patch->shape.forEdge([&](const Point& p0, const Point& p1) {
                float len = Point::distance(p0, p1);
                if (len > length) {
                    length = len;
                    v0 = p0;
                    v1 = p1;
                    hasEdge = true;
                }
            });
        }

        Polygon object;
        if (statue) {
            object = Polygon::rect(1.0f + Random::getFloat(), 1.0f + Random::getFloat());
            object.rotate(std::atan2(v1.y - v0.y, v1.x - v0.x));
        } else {
            object = Polygon::circle(1.0f + Random::getFloat());
        }

        if (offset && hasEdge) {
            Point gravity = GeomUtils::interpolate(v0, v1);
            Point target = GeomUtils::interpolate(
                patch->shape.centroid(),
                gravity,
                0.2f + Random::getFloat() * 0.4f
            );
            object.offset(target);
        } else {
            object.offset(patch->shape.centroid());
        }

        geometry = { object };
    }

    static float rateLocation(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    std::string getLabel() const override {
        return "Market";
    }
};

// Implementation note: rateLocation needs access to Model internals
// One market should not touch another
// Market shouldn't be much larger than the plaza

} // namespace town
