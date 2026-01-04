#include "town_generator2/wards/Ward.hpp"
#include "town_generator2/building/Model.hpp"
#include "town_generator2/building/CurtainWall.hpp"

namespace town_generator2 {
namespace wards {

geom::Polygon Ward::getCityBlock() {
    std::vector<double> insetDist;

    bool innerPatch = model->wall == nullptr || patch->withinWalls;

    patch->shape.forEdgePtr([&](const geom::PointPtr& v0, const geom::PointPtr& v1) {
        if (model->wall && model->wall->bordersBy(patch, v0, v1)) {
            // Not too close to the wall
            insetDist.push_back(MAIN_STREET / 2);
        } else {
            bool onStreet = innerPatch && (model->plaza && model->plaza->shape.findEdge(v1, v0) != -1);

            if (!onStreet) {
                for (const auto& street : model->arteries) {
                    if (street.contains(v0) && street.contains(v1)) {
                        onStreet = true;
                        break;
                    }
                }
            }

            double dist = onStreet ? MAIN_STREET : (innerPatch ? REGULAR_STREET : ALLEY);
            insetDist.push_back(dist / 2);
        }
    });

    return patch->shape.isConvex()
        ? patch->shape.shrink(insetDist)
        : patch->shape.buffer(insetDist);
}

void Ward::filterOutskirts() {
    struct PopEdge {
        double x, y, dx, dy, d;
    };
    std::vector<PopEdge> populatedEdges;

    auto addEdge = [&](const geom::Point& v1, const geom::Point& v2, double factor) {
        double dx = v2.x - v1.x;
        double dy = v2.y - v1.y;

        double maxDist = 0;
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            const geom::Point& v = patch->shape[i];
            if (&v != &v1 && &v != &v2) {
                double d = geom::GeomUtils::distance2line(v1.x, v1.y, dx, dy, v.x, v.y) * factor;
                if (d > maxDist) maxDist = d;
            }
        }

        populatedEdges.push_back({v1.x, v1.y, dx, dy, maxDist});
    };

    patch->shape.forEdge([&](const geom::Point& v1, const geom::Point& v2) {
        bool onRoad = false;
        for (const auto& street : model->arteries) {
            if (street.containsByValue(v1) && street.containsByValue(v2)) {
                onRoad = true;
                break;
            }
        }

        if (onRoad) {
            addEdge(v1, v2, 1.0);
        } else {
            // Find neighbor across this edge
            int idx = patch->shape.indexOfByValue(v1);
            if (idx != -1) {
                auto* n = model->getNeighbour(patch, patch->shape.ptr(idx));
                if (n && n->withinCity) {
                    addEdge(v1, v2, model->isEnclosed(n) ? 1.0 : 0.4);
                }
            }
        }
    });

    // Build density map
    std::vector<double> density;
    for (size_t i = 0; i < patch->shape.length(); ++i) {
        const geom::PointPtr& v = patch->shape.ptr(i);
        bool isGate = false;
        for (const auto& gate : model->gates) {
            if (gate == v) { isGate = true; break; }
        }

        if (isGate) {
            density.push_back(1.0);
        } else {
            bool allWithinCity = true;
            auto patchesAtV = model->patchByVertex(v);
            for (auto* p : patchesAtV) {
                if (!p->withinCity) {
                    allWithinCity = false;
                    break;
                }
            }
            density.push_back(allWithinCity ? 2 * utils::Random::getFloat() : 0);
        }
    }

    // Filter buildings
    std::vector<geom::Polygon> filtered;
    for (const auto& building : geometry) {
        double minDist = 1.0;
        for (const auto& edge : populatedEdges) {
            for (size_t i = 0; i < building.length(); ++i) {
                const geom::Point& v = building[i];
                double d = geom::GeomUtils::distance2line(edge.x, edge.y, edge.dx, edge.dy, v.x, v.y);
                double dist = (edge.d > 0) ? d / edge.d : 1.0;
                if (dist < minDist) minDist = dist;
            }
        }

        geom::Point c = building.center();
        auto weights = patch->shape.interpolate(c);
        double p = 0.0;
        for (size_t j = 0; j < weights.size(); ++j) {
            p += density[j] * weights[j];
        }
        if (p > 0) minDist /= p;

        if (utils::Random::fuzzy(1.0) > minDist) {
            filtered.push_back(building);
        }
    }
    geometry = filtered;
}

geom::PointPtr Ward::findLongestEdge(const geom::Polygon& poly) {
    return poly.min([&poly](const geom::Point& v) {
        int idx = poly.indexOfByValue(v);
        if (idx == -1) return 0.0;
        return -poly.vectori(idx).length();
    });
}

std::vector<geom::Polygon> Ward::createAlleys(
    const geom::Polygon& p,
    double minSq,
    double gridChaos,
    double sizeChaos,
    double emptyProb,
    bool split
) {
    // Find longest edge
    geom::PointPtr v;
    double length = -1.0;
    p.forEdgePtr([&](const geom::PointPtr& p0, const geom::PointPtr& p1) {
        double len = geom::Point::distance(*p0, *p1);
        if (len > length) {
            length = len;
            v = p0;
        }
    });

    if (!v) return {};

    double spread = 0.8 * gridChaos;
    double ratio = (1 - spread) / 2 + utils::Random::getFloat() * spread;

    // Angle spread
    double angleSpread = M_PI / 6 * gridChaos * (p.square() < minSq * 4 ? 0.0 : 1.0);
    double b = (utils::Random::getFloat() - 0.5) * angleSpread;

    auto halves = building::Cutter::bisect(p, v, ratio, b, split ? ALLEY : 0.0);

    std::vector<geom::Polygon> buildings;
    for (auto& half : halves) {
        double sq = half.square();
        double threshold = minSq * std::pow(2, 4 * sizeChaos * (utils::Random::getFloat() - 0.5));

        if (sq < threshold) {
            if (!utils::Random::getBool(emptyProb)) {
                buildings.push_back(half);
            }
        } else {
            bool shouldSplit = sq > minSq / (utils::Random::getFloat() * utils::Random::getFloat());
            auto subBuildings = createAlleys(half, minSq, gridChaos, sizeChaos, emptyProb, shouldSplit);
            buildings.insert(buildings.end(), subBuildings.begin(), subBuildings.end());
        }
    }

    return buildings;
}

std::vector<geom::Polygon> Ward::createOrthoBuilding(
    const geom::Polygon& poly,
    double minBlockSq,
    double fill
) {
    if (poly.square() < minBlockSq) {
        return {poly};
    }

    geom::PointPtr longestEdge = findLongestEdge(poly);
    geom::Point c1 = poly.vector(longestEdge);
    geom::Point c2 = c1.rotate90();

    std::function<std::vector<geom::Polygon>(const geom::Polygon&, const geom::Point&, const geom::Point&)> slice;
    slice = [&](const geom::Polygon& p, const geom::Point& c1, const geom::Point& c2) -> std::vector<geom::Polygon> {
        geom::PointPtr v0 = findLongestEdge(p);
        geom::PointPtr v1 = p.next(v0);
        geom::Point v = v1->subtract(*v0);

        double ratio = 0.4 + utils::Random::getFloat() * 0.2;
        geom::Point p1 = geom::GeomUtils::interpolate(*v0, *v1, ratio);

        geom::Point c = (std::abs(geom::GeomUtils::scalar(v.x, v.y, c1.x, c1.y)) <
                         std::abs(geom::GeomUtils::scalar(v.x, v.y, c2.x, c2.y)))
            ? c1 : c2;

        auto halves = p.cut(p1, p1.add(c));

        std::vector<geom::Polygon> buildings;
        for (auto& half : halves) {
            double threshold = minBlockSq * std::pow(2, utils::Random::normal() * 2 - 1);
            if (half.square() < threshold) {
                if (utils::Random::getBool(fill)) {
                    buildings.push_back(half);
                }
            } else {
                auto subBuildings = slice(half, c1, c2);
                buildings.insert(buildings.end(), subBuildings.begin(), subBuildings.end());
            }
        }
        return buildings;
    };

    // Try to slice - may need multiple attempts
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto blocks = slice(poly, c1, c2);
        if (!blocks.empty()) {
            return blocks;
        }
    }
    return {poly};  // Fallback
}

void CommonWard::createGeometry() {
    geom::Polygon block = getCityBlock();
    geometry = Ward::createAlleys(block, minSq, gridChaos, sizeChaos, emptyProb);

    if (!model->isEnclosed(patch)) {
        filterOutskirts();
    }
}

} // namespace wards
} // namespace town_generator2
