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
    // Base Ward creates no buildings (faithful to Haxe)
    // Subclasses override to create appropriate geometry
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
    double minSq,
    double gridChaos,
    double sizeChaos,
    double emptyProb,
    double split
) {
    // Find longest edge (faithful to town_generator2)
    size_t longestEdge = 0;
    double longestLen = 0;
    size_t pLen = p.length();

    if (pLen < 3) {
        if (!utils::Random::boolVal(emptyProb)) {
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

    // Ratio based on gridChaos (faithful to town_generator2)
    double spread = 0.8 * gridChaos;
    double ratio = (1.0 - spread) / 2.0 + utils::Random::floatVal() * spread;

    // Angle spread: 0 for small blocks, scaled by gridChaos for larger (faithful to town_generator2)
    double sq = std::abs(p.square());
    double angleSpread = M_PI / 6.0 * gridChaos * (sq < minSq * 4 ? 0.0 : 1.0);
    double angle = (utils::Random::floatVal() - 0.5) * angleSpread;

    // Conditional alley gap based on split parameter (faithful to town_generator2)
    double gap = (split > 0.5) ? ALLEY : 0.0;

    auto halves = building::Cutter::bisect(p, p[longestEdge], ratio, angle, gap);

    // If bisect returns only 1 polygon (failed to cut), treat as leaf
    if (halves.size() == 1) {
        if (!utils::Random::boolVal(emptyProb)) {
            geometry.push_back(p);
        }
        return;
    }

    for (const auto& half : halves) {
        double halfSq = std::abs(half.square());
        // Exponential threshold variation (faithful to town_generator2)
        double threshold = minSq * std::pow(2.0, 4.0 * sizeChaos * (utils::Random::floatVal() - 0.5));

        if (halfSq < threshold) {
            // Small enough - add as building
            if (!utils::Random::boolVal(emptyProb)) {
                geometry.push_back(half);
            }
        } else {
            // Determine if we should create alleys in sub-blocks (faithful to town_generator2)
            double r1 = utils::Random::floatVal();
            double r2 = utils::Random::floatVal();
            bool shouldSplit = halfSq > minSq / (r1 * r2 + 0.001);  // +0.001 to avoid div by zero
            createAlleys(half, minSq, gridChaos, sizeChaos, emptyProb, shouldSplit ? 1.0 : 0.0);
        }
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
