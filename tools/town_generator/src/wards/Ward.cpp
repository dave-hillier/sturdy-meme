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

        // Check if edge borders wall - use MAIN_STREET/2 like Haxe
        if (model->wall && model->wall->bordersBy(patch, v0, v1)) {
            insetDistances[i] = MAIN_STREET / 2;
            continue;
        }

        // Citadel borders also use MAIN_STREET/2
        if (model->citadel && model->citadel->bordersBy(patch, v0, v1)) {
            insetDistances[i] = MAIN_STREET / 2;
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

void Ward::filterOutskirts() {
    if (!patch || !model) return;

    // Simplified filterOutskirts - filter buildings based on distance from patch edges
    // that are on roads or border other city patches.

    // Structure to hold populated edge info
    struct PopulatedEdge {
        double x, y, dx, dy, d;
    };
    std::vector<PopulatedEdge> populatedEdges;

    // Helper to add an edge with a distance factor
    auto addEdge = [&](const geom::Point& v1, const geom::Point& v2, double factor) {
        double dx = v2.x - v1.x;
        double dy = v2.y - v1.y;

        // Find max distance from any vertex to this edge line
        double maxDist = 0;
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            const geom::Point& v = patch->shape[i];
            if (v == v1 || v == v2) continue;
            double dist = geom::GeomUtils::distance2line(v1.x, v1.y, dx, dy, v.x, v.y);
            maxDist = std::max(maxDist, dist * factor);
        }

        if (maxDist > 0) {
            populatedEdges.push_back({v1.x, v1.y, dx, dy, maxDist});
        }
    };

    // Check each edge of the patch
    patch->shape.forEdge([&](const geom::Point& v1, const geom::Point& v2) {
        // Check if edge is on a road/artery
        bool onRoad = false;
        for (const auto& artery : model->arteries) {
            if (artery.size() < 2) continue;
            for (size_t j = 0; j + 1 < artery.size(); ++j) {
                if ((*artery[j] == v1 && *artery[j + 1] == v2) ||
                    (*artery[j] == v2 && *artery[j + 1] == v1)) {
                    onRoad = true;
                    break;
                }
            }
            if (onRoad) break;
        }

        if (onRoad) {
            addEdge(v1, v2, 1.0);
        } else {
            // For non-road edges, add with reduced factor if patch is within city
            if (patch->withinCity) {
                addEdge(v1, v2, 0.4);
            }
        }
    });

    // If no populated edges found, don't filter
    if (populatedEdges.empty()) return;

    // Calculate density for each vertex based on whether all adjacent patches are in city
    std::vector<double> density;
    for (size_t i = 0; i < patch->shape.length(); ++i) {
        const geom::Point& v = patch->shape[i];

        // Check if this is a gate vertex
        bool isGate = false;
        if (model->wall) {
            for (const auto& gate : model->wall->gates) {
                if (*gate == v) {
                    isGate = true;
                    break;
                }
            }
        }

        if (isGate) {
            density.push_back(1.0);
        } else {
            // Check if all patches at this vertex are within city
            auto patches = model->patchByVertex(v);
            bool allWithinCity = true;
            for (auto* p : patches) {
                if (!p->withinCity) {
                    allWithinCity = false;
                    break;
                }
            }
            density.push_back(allWithinCity ? 2.0 * utils::Random::floatVal() : 0.0);
        }
    }

    // Filter buildings
    auto it = geometry.begin();
    while (it != geometry.end()) {
        double minDist = 1.0;

        // Find minimum distance ratio to any populated edge
        for (const auto& edge : populatedEdges) {
            for (size_t i = 0; i < it->length(); ++i) {
                const geom::Point& v = (*it)[i];
                double d = geom::GeomUtils::distance2line(edge.x, edge.y, edge.dx, edge.dy, v.x, v.y);
                double dist = edge.d > 0 ? d / edge.d : 1.0;
                minDist = std::min(minDist, dist);
            }
        }

        // Interpolate density at building center
        geom::Point c = it->center();
        auto interp = patch->shape.interpolate(c);
        double p = 0.0;
        for (size_t j = 0; j < interp.size() && j < density.size(); ++j) {
            p += density[j] * interp[j];
        }
        if (p > 0.001) {
            minDist /= p;
        }

        // Filter based on random threshold
        if (utils::Random::fuzzy(1.0) > minDist) {
            ++it;
        } else {
            it = geometry.erase(it);
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
    // Find longest edge (faithful to Haxe)
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

    // Angle spread: 0 for small blocks, scaled by gridChaos for larger (faithful to Haxe)
    double sq = p.square();  // Signed area like Haxe
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
        // Use signed area like Haxe (no abs) - CCW polygons have positive area
        double halfSq = half.square();
        // Exponential threshold variation (faithful to Haxe)
        double threshold = minSq * std::pow(2.0, 4.0 * sizeChaos * (utils::Random::floatVal() - 0.5));

        if (halfSq < threshold) {
            // Small enough - add as building
            if (!utils::Random::boolVal(emptyProb)) {
                geometry.push_back(half);
            }
        } else {
            // Determine if we should create alleys in sub-blocks (faithful to Haxe)
            double r1 = utils::Random::floatVal();
            double r2 = utils::Random::floatVal();
            // Haxe: half.square > minSq / (Random.float() * Random.float())
            // When r1*r2 is very small, threshold becomes huge so shouldSplit=false
            double divisor = r1 * r2;
            bool shouldSplit = divisor > 0.0001 && halfSq > minSq / divisor;
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
