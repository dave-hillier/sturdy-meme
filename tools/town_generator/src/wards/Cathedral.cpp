#include "town_generator/wards/Cathedral.h"
#include "town_generator/building/City.h"
#include "town_generator/utils/Random.h"
#include <cmath>

namespace town_generator {
namespace wards {

void Cathedral::createGeometry() {
    if (!patch) return;

    geom::Point center = patch->shape.centroid();
    double area = std::abs(patch->shape.square());
    double baseSize = std::sqrt(area) * 0.4;

    // Main cathedral body (cross shape approximated as rectangles)
    double mainLength = baseSize * 1.5;
    double mainWidth = baseSize * 0.6;
    double transeptLength = baseSize * 0.8;
    double transeptWidth = baseSize * 0.4;

    // Main nave
    geom::Polygon nave = geom::Polygon::rect(mainWidth, mainLength);
    nave.offset(center);
    geometry.push_back(nave);

    // Transept (cross arm)
    geom::Point transeptCenter(center.x, center.y + mainLength * 0.2);
    geom::Polygon transept = geom::Polygon::rect(transeptLength, transeptWidth);
    transept.offset(transeptCenter);
    geometry.push_back(transept);

    // Apse (semicircular end, approximated as rectangle)
    geom::Point apseCenter(center.x, center.y + mainLength * 0.5 + transeptWidth * 0.3);
    geom::Polygon apse = geom::Polygon::rect(mainWidth * 0.8, transeptWidth * 0.6);
    apse.offset(apseCenter);
    geometry.push_back(apse);

    // Tower
    geom::Point towerCenter(center.x, center.y - mainLength * 0.4);
    double towerSize = mainWidth * 0.5;
    geom::Polygon tower = geom::Polygon::rect(towerSize, towerSize);
    tower.offset(towerCenter);
    geometry.push_back(tower);
}

} // namespace wards
} // namespace town_generator
