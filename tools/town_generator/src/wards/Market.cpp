#include "town_generator/wards/Market.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Market::createGeometry() {
    if (!patch) return;

    // Market/plaza - open space with fountain or statue (faithful to Haxe)
    bool statue = utils::Random::boolVal(0.6);
    bool offset = statue || utils::Random::boolVal(0.3);

    geom::PointPtr v0 = nullptr;
    geom::PointPtr v1 = nullptr;

    if (statue || offset) {
        // Find longest edge for rotation/offset reference
        double maxLen = -1.0;
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::PointPtr p0 = patch->shape.ptr(i);
            geom::PointPtr p1 = patch->shape.ptr((i + 1) % patch->shape.length());
            double len = geom::Point::distance(*p0, *p1);
            if (len > maxLen) {
                maxLen = len;
                v0 = p0;
                v1 = p1;
            }
        }
    }

    geom::Polygon object;
    if (statue) {
        // Rectangular statue/monument
        object = geom::Polygon::rect(1 + utils::Random::floatVal(), 1 + utils::Random::floatVal());
        if (v0 && v1) {
            double angle = std::atan2(v1->y - v0->y, v1->x - v0->x);
            object.rotate(angle);
        }
    } else {
        // Circular fountain
        object = geom::Polygon::circle(1 + utils::Random::floatVal());
    }

    if (offset && v0 && v1) {
        // Offset toward the longest edge
        geom::Point gravity((v0->x + v1->x) / 2, (v0->y + v1->y) / 2);
        geom::Point centroid = patch->shape.centroid();
        double t = 0.2 + utils::Random::floatVal() * 0.4;
        geom::Point pos(
            centroid.x + (gravity.x - centroid.x) * t,
            centroid.y + (gravity.y - centroid.y) * t
        );
        object.offset(pos);
    } else {
        object.offset(patch->shape.centroid());
    }

    geometry.push_back(object);
}

} // namespace wards
} // namespace town_generator
