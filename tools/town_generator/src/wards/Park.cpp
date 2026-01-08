#include "town_generator/wards/Park.h"
#include "town_generator/building/City.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace wards {

void Park::createGeometry() {
    if (!patch) return;

    auto cityBlock = getCityBlock();
    if (cityBlock.empty()) return;

    // Shrink the shape by the city block insets
    geom::Polygon available = patch->shape.shrink(cityBlock);
    if (available.length() < 3) return;

    // Create wavy boundary for organic look (faithful to mfcg.js Hf.render)
    greenArea = createWavyBoundary(available);

    // Add internal paths
    createPaths();

    // Add features (pavilion, fountain, benches)
    addFeatures();

    // Add features to geometry
    for (const auto& feature : features) {
        geometry.push_back(feature);
    }

    // Spawn trees
    trees = spawnTrees();

    SDL_Log("Park: Created green area with %zu paths and %zu features, %zu trees",
            paths.size(), features.size(), trees.size());
}

geom::Polygon Park::createWavyBoundary(const geom::Polygon& shape) {
    // Faithful to mfcg.js Park.createGeometry (lines 687-694)
    // For each vertex, add the vertex and a midpoint to the next vertex
    // Then apply Chaikin smoothing (3 iterations, closed polygon)

    if (shape.length() < 3) return shape;

    std::vector<geom::Point> pointsWithMidpoints;
    size_t len = shape.length();

    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % len];

        // Add vertex
        pointsWithMidpoints.push_back(v0);

        // Add midpoint to next vertex (lerp at 0.5)
        pointsWithMidpoints.emplace_back(
            (v0.x + v1.x) / 2.0,
            (v0.y + v1.y) / 2.0
        );
    }

    // Apply Chaikin smoothing (closed=true, iterations=3)
    geom::Polygon expanded(pointsWithMidpoints);
    return geom::Polygon::chaikin(expanded, true, 3);
}

void Park::createPaths() {
    paths.clear();
    if (greenArea.length() < 3) return;

    geom::Point center = greenArea.centroid();
    double area = std::abs(greenArea.square());
    double radius = std::sqrt(area / M_PI);

    // Create 1-3 paths from edges toward center
    int numPaths = 1 + static_cast<int>(utils::Random::floatVal() * 2);
    size_t len = greenArea.length();

    for (int p = 0; p < numPaths && p < static_cast<int>(len); ++p) {
        // Pick a random edge midpoint
        size_t edgeIdx = static_cast<size_t>(utils::Random::floatVal() * len) % len;
        const geom::Point& v0 = greenArea[edgeIdx];
        const geom::Point& v1 = greenArea[(edgeIdx + 1) % len];

        geom::Point edgeMid = geom::GeomUtils::interpolate(v0, v1, 0.3 + 0.4 * utils::Random::floatVal());

        // Path from edge toward center (not all the way)
        double pathLen = 0.4 + 0.3 * utils::Random::floatVal();
        geom::Point pathEnd = geom::GeomUtils::interpolate(edgeMid, center, pathLen);

        std::vector<geom::Point> path;
        path.push_back(edgeMid);

        // Add a few intermediate points with slight curves
        int numPathPoints = 2 + static_cast<int>(utils::Random::floatVal() * 2);
        for (int j = 1; j <= numPathPoints; ++j) {
            double t = static_cast<double>(j) / (numPathPoints + 1);
            geom::Point basePoint = geom::GeomUtils::interpolate(edgeMid, pathEnd, t);

            // Slight curve
            geom::Point toCenter = center.subtract(edgeMid);
            geom::Point perpDir(-toCenter.y, toCenter.x);
            if (toCenter.length() > 0.01) {
                perpDir = perpDir.scale(1.0 / toCenter.length());
            }

            double curveOffset = std::sin(t * M_PI) * radius * 0.1 * (utils::Random::floatVal() - 0.5);
            path.push_back(basePoint.add(perpDir.scale(curveOffset)));
        }

        path.push_back(pathEnd);
        paths.push_back(path);
    }
}

void Park::addFeatures() {
    features.clear();
    if (greenArea.length() < 3) return;

    geom::Point center = greenArea.centroid();
    double area = std::abs(greenArea.square());
    double baseSize = std::sqrt(area) * 0.05;

    // Maybe add a central feature (pavilion or fountain)
    if (utils::Random::boolVal(0.5)) {
        double featureSize = baseSize * (0.8 + 0.4 * utils::Random::floatVal());
        int numSides = utils::Random::boolVal(0.5) ? 6 : 8;  // Hexagon or octagon

        geom::Polygon feature = geom::Polygon::regular(numSides, featureSize);
        feature.offset(center);
        features.push_back(feature);
    }

    // Maybe add benches near paths
    if (utils::Random::boolVal(0.3) && !paths.empty()) {
        for (size_t i = 0; i < paths.size() && features.size() < 5; ++i) {
            if (paths[i].size() < 2) continue;

            // Pick a point along the path
            size_t pathIdx = paths[i].size() / 2;
            geom::Point benchPos = paths[i][pathIdx];

            // Small rectangular bench
            double benchLen = 0.8 + utils::Random::floatVal() * 0.4;
            double benchWidth = 0.3;

            // Direction along path
            geom::Point pathDir;
            if (pathIdx + 1 < paths[i].size()) {
                pathDir = paths[i][pathIdx + 1].subtract(benchPos);
            } else if (pathIdx > 0) {
                pathDir = benchPos.subtract(paths[i][pathIdx - 1]);
            } else {
                pathDir = geom::Point(1, 0);
            }
            double pathLen = pathDir.length();
            if (pathLen > 0.01) {
                pathDir = pathDir.scale(1.0 / pathLen);
            }
            geom::Point perpDir(-pathDir.y, pathDir.x);

            // Offset bench slightly from path
            geom::Point offsetBenchPos = benchPos.add(perpDir.scale(1.0));

            std::vector<geom::Point> benchVerts;
            benchVerts.push_back(offsetBenchPos.add(pathDir.scale(-benchLen/2)).add(perpDir.scale(-benchWidth/2)));
            benchVerts.push_back(offsetBenchPos.add(pathDir.scale(benchLen/2)).add(perpDir.scale(-benchWidth/2)));
            benchVerts.push_back(offsetBenchPos.add(pathDir.scale(benchLen/2)).add(perpDir.scale(benchWidth/2)));
            benchVerts.push_back(offsetBenchPos.add(pathDir.scale(-benchLen/2)).add(perpDir.scale(benchWidth/2)));

            features.push_back(geom::Polygon(benchVerts));
        }
    }
}

std::vector<geom::Point> Park::spawnTrees() const {
    // Faithful to mfcg.js spawnTrees - spawn trees within the green area
    std::vector<geom::Point> result;

    if (greenArea.length() < 3) return result;

    double area = std::abs(greenArea.square());

    // Greenery factor: higher = more trees
    // Parks use greenery^1 not greenery^2 (from mfcg.js District.createParams)
    double greeneryFactor = (utils::Random::floatVal() + utils::Random::floatVal() +
                             utils::Random::floatVal()) / 3.0;

    // Tree density based on area and greenery
    int numTrees = static_cast<int>(area * greeneryFactor / 20.0);
    if (numTrees < 3) numTrees = 3;
    if (numTrees > 50) numTrees = 50;

    auto bounds = greenArea.getBounds();

    for (int i = 0; i < numTrees * 3 && static_cast<int>(result.size()) < numTrees; ++i) {
        // Random point within bounds
        double x = bounds.left + utils::Random::floatVal() * (bounds.right - bounds.left);
        double y = bounds.top + utils::Random::floatVal() * (bounds.bottom - bounds.top);
        geom::Point p(x, y);

        // Check if point is within green area
        if (greenArea.contains(p)) {
            // Check if not too close to features
            bool tooClose = false;
            for (const auto& feature : features) {
                if (geom::Point::distance(p, feature.centroid()) < 2.0) {
                    tooClose = true;
                    break;
                }
            }

            // Check if not too close to paths
            for (const auto& path : paths) {
                for (const auto& pathPoint : path) {
                    if (geom::Point::distance(p, pathPoint) < 1.5) {
                        tooClose = true;
                        break;
                    }
                }
                if (tooClose) break;
            }

            if (!tooClose) {
                result.push_back(p);
            }
        }
    }

    return result;
}

} // namespace wards
} // namespace town_generator
