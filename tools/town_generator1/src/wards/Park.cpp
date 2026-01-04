#include "town_generator/wards/Park.h"
#include "town_generator/building/Model.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Park::createGeometry() {
    if (!patch) return;

    // Parks are mostly empty - maybe a small pavilion
    if (utils::Random::boolVal(0.3)) {
        geom::Point center = patch->shape.centroid();
        double area = std::abs(patch->shape.square());
        double size = std::sqrt(area) * 0.08;

        geom::Polygon pavilion = geom::Polygon::regular(6, size);
        pavilion.offset(center);
        geometry.push_back(pavilion);
    }
}

} // namespace wards
} // namespace town_generator
