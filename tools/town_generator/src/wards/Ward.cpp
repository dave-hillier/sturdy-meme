#include "town_generator/wards/Ward.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Cutter.h"
#include "town_generator/geom/GeomUtils.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace wards {

std::vector<double> Ward::getCityBlock() {
    if (!patch || !model) return {};

    std::vector<double> insetDistances;
    size_t len = patch->shape.length();
    insetDistances.resize(len, REGULAR_STREET / 2);

    // Check each edge for special conditions
    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Check if edge borders wall
        if (model->wall && model->wall->bordersBy(patch, v0, v1)) {
            insetDistances[i] = 0;
            continue;
        }

        if (model->citadel && model->citadel->bordersBy(patch, v0, v1)) {
            insetDistances[i] = 0;
            continue;
        }

        // Check if edge is on main artery (arteries now use PointPtr)
        for (const auto& artery : model->arteries) {
            if (artery.size() < 2) continue;  // Need at least 2 points to form an edge
            bool found = false;
            for (size_t j = 0; j + 1 < artery.size(); ++j) {
                // Compare dereferenced PointPtrs with Point values
                if ((*artery[j] == v0 && *artery[j + 1] == v1) ||
                    (*artery[j] == v1 && *artery[j + 1] == v0)) {
                    insetDistances[i] = MAIN_STREET / 2;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }

    return insetDistances;
}

void Ward::createGeometry() {
    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    auto block = patch->shape.shrink(cityBlock);
    if (block.empty()) return;

    // Default implementation: create simple buildings
    createAlleys(block, 30, 0.6, 0.8, 0.1);
}

void Ward::filterOutskirts(std::vector<geom::Polygon>& buildings, double minDistance) {
    auto it = buildings.begin();
    while (it != buildings.end()) {
        double minDist = std::numeric_limits<double>::infinity();
        geom::Point center = it->center();

        for (const auto& vPtr : patch->shape) {
            minDist = std::min(minDist, geom::Point::distance(center, *vPtr));
        }

        if (minDist < minDistance) {
            it = buildings.erase(it);
        } else {
            ++it;
        }
    }
}

void Ward::createAlleys(
    const geom::Polygon& p,
    double minArea,
    double gridChaos,
    double sizeChaos,
    double emptyProbability,
    double split
) {
    double area = std::abs(p.square());

    if (area < minArea) {
        if (!utils::Random::boolVal(emptyProbability)) {
            geometry.push_back(p);
        }
        return;
    }

    // Find longest edge
    size_t longestEdge = 0;
    double longestLen = 0;
    size_t pLen = p.length();

    if (pLen == 0) {
        return;
    }

    // Polygons with less than 3 vertices can't be bisected
    if (pLen < 3) {
        if (!utils::Random::boolVal(emptyProbability)) {
            geometry.push_back(p);
        }
        return;
    }

    for (size_t i = 0; i < pLen; ++i) {
        geom::Point v = p.vectori(static_cast<int>(i));
        double len = v.length();
        if (len > longestLen) {
            longestLen = len;
            longestEdge = i;
        }
    }

    // Bisect at longest edge
    double ratio = 0.35 + utils::Random::floatVal() * 0.3;
    double angle = (utils::Random::floatVal() - 0.5) * gridChaos;
    double gap = ALLEY;

    auto halves = building::Cutter::bisect(p, p[longestEdge], ratio, angle, gap);

    // If bisect returns only 1 polygon (failed to cut), treat as leaf
    if (halves.size() == 1) {
        if (!utils::Random::boolVal(emptyProbability)) {
            geometry.push_back(p);
        }
        return;
    }

    for (const auto& half : halves) {
        createAlleys(half, minArea * (1.0 - sizeChaos * 0.5 + utils::Random::floatVal() * sizeChaos),
                     gridChaos, sizeChaos, emptyProbability);
    }
}

geom::Polygon Ward::createOrthoBuilding(const geom::Polygon& poly, double fill, double ratio) {
    if (poly.length() < 3) return poly;

    // Find two longest edges
    std::vector<std::pair<size_t, double>> edges;
    for (size_t i = 0; i < poly.length(); ++i) {
        edges.push_back({i, poly.vectori(static_cast<int>(i)).length()});
    }

    std::sort(edges.begin(), edges.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    // Cut from shorter sides
    geom::Polygon result = poly;
    double shrinkAmount = (1.0 - fill) * edges[0].second / 2;

    if (shrinkAmount > 0.1) {
        for (size_t i = 2; i < edges.size(); ++i) {
            result = result.peel(result[edges[i].first], shrinkAmount);
        }
    }

    return result;
}

geom::Polygon Ward::getInsetShape(double inset) {
    std::vector<double> distances(patch->shape.length(), inset);
    return patch->shape.shrink(distances);
}

} // namespace wards
} // namespace town_generator
