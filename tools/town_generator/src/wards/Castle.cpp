#include "town_generator/wards/Castle.h"
#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Building.h"
#include "town_generator/utils/Random.h"
#include "town_generator/geom/GeomUtils.h"
#include <cmath>
#include <algorithm>
#include <SDL3/SDL.h>

namespace town_generator {
namespace wards {

// Helper: calculate compactness of a polygon (4 * PI * area / perimeter^2)
// Returns 1.0 for a circle, less for irregular shapes
static double compactness(const geom::Polygon& poly) {
    double area = std::abs(poly.square());
    double perimeter = poly.perimeter();
    if (perimeter < 0.001) return 0.0;
    return 4.0 * M_PI * area / (perimeter * perimeter);
}

void Castle::adjustShape() {
    if (!patch || !model) return;

    geom::Point center = patch->shape.centroid();

    // Calculate min and max radius from center
    auto calcRadii = [&]() -> std::pair<double, double> {
        double minR = std::numeric_limits<double>::infinity();
        double maxR = 0.0;
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            double r = geom::Point::distance(patch->shape[i], center);
            minR = std::min(minR, r);
            maxR = std::max(maxR, r);
        }
        return {minR, maxR};
    };

    auto [minRadius, maxRadius] = calcRadii();

    // Bloat if minimum dimension < 10 (mfcg.js line 12491)
    int bloatIter = 0;
    while (minRadius < 10.0 && bloatIter++ < 1000) {
        SDL_Log("Bloating the citadel... (minRadius=%.2f)", minRadius);

        double bloatRadius = 2.0 * std::max(15.0, maxRadius);

        // Move all vertices away from center (power law)
        // mfcg.js: q = Math.pow(q / k, -.25) — negative exponent expands inward vertices
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::Point& v = patch->shape[i];
            double dist = geom::Point::distance(v, center);
            if (dist > 0.001 && dist < bloatRadius) {
                geom::Point dir = v.subtract(center);
                double factor = std::pow(dist / bloatRadius, -0.25);
                v = geom::Point(center.x + dir.x * factor, center.y + dir.y * factor);
            }
        }

        std::tie(minRadius, maxRadius) = calcRadii();
    }

    // Get gate positions to keep fixed during equalization
    std::vector<geom::Point> fixed;
    if (wall && !wall->gates.empty()) {
        fixed.push_back(*wall->gates[0]);
        // Also fix adjacent vertices if gate has 2 edges
    }

    // Equalize until compactness >= 0.75 (mfcg.js line 12517)
    double comp = compactness(patch->shape);
    while (comp < 0.75) {
        SDL_Log("Equalizing... compactness=%.3f", comp);
        equalize(center, 0.2, fixed);
        double newComp = compactness(patch->shape);
        if (std::abs(newComp - comp) < 0.001) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Bad citadel shape - equalization not converging");
            break;
        }
        comp = newComp;
    }
}

void Castle::equalize(const geom::Point& center, double factor, const std::vector<geom::Point>& fixed) {
    // DFT-like averaging to make shape more circular
    // Faithful to mfcg.js Castle.equalize (lines 12530-12546)

    size_t n = patch->shape.length();
    if (n < 3) return;

    // Compute average direction from center
    geom::Point h = patch->shape[0].subtract(center);

    for (size_t i = 1; i < n; ++i) {
        geom::Point v = patch->shape[i].subtract(center);
        double angle = -2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
        double cosA = std::cos(angle);
        double sinA = std::sin(angle);
        geom::Point rotated(v.x * cosA - v.y * sinA, v.y * cosA + v.x * sinA);
        h = h.add(rotated);
    }

    h = h.scale(1.0 / static_cast<double>(n));

    // Apply the averaged direction back to each vertex
    for (size_t i = 0; i < n; ++i) {
        // Skip fixed points (gates)
        bool isFixed = false;
        for (const auto& f : fixed) {
            if (geom::Point::distance(patch->shape[i], f) < 0.1) {
                isFixed = true;
                break;
            }
        }
        if (isFixed) continue;

        double angle = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
        double cosA = std::cos(angle);
        double sinA = std::sin(angle);
        geom::Point target = center.add(geom::Point(h.x * cosA - h.y * sinA, h.y * cosA + h.x * sinA));

        // Interpolate toward target
        patch->shape[i] = geom::Point(
            patch->shape[i].x + (target.x - patch->shape[i].x) * factor,
            patch->shape[i].y + (target.y - patch->shape[i].y) * factor
        );
    }
}

void Castle::createGeometry() {
    // Faithful to mfcg.js Castle.createGeometry (lines 12548-12556)
    if (!patch || !model) return;

    utils::Random::reset(patch->seed);

    geometry.clear();

    // Adjust shape for better citadel appearance
    // In mfcg.js this is done in constructor after wall creation
    // We do it here since the curtain wall has been built by now
    adjustShape();

    // Create keep building by shrinking the shape
    // mfcg.js: a = PolyCut.shrinkEq(this.patch.shape, CurtainWall.THICKNESS + 2)
    double shrinkAmount = building::CurtainWall::THICKNESS + 2.0;
    geom::Polygon keepOutline = patch->shape.shrinkEq(shrinkAmount);

    if (keepOutline.length() < 3) {
        // Fallback: create simple rectangular keep
        geom::Point center = patch->shape.centroid();
        double radius = std::sqrt(std::abs(patch->shape.square()) / M_PI) * 0.4;
        keepOutline = geom::Polygon::rect(radius * 1.5, radius * 1.2);
        keepOutline.offset(center);
        building = keepOutline;
        geometry.push_back(building);
        return;
    }

    // Find largest inscribed rectangle (LIRA)
    // mfcg.js: a = PolyBounds.lira(a)
    std::vector<geom::Point> outlinePts;
    for (size_t i = 0; i < keepOutline.length(); ++i) {
        outlinePts.push_back(keepOutline[i]);
    }

    std::vector<geom::Point> liraRect = geom::GeomUtils::lira(outlinePts);

    if (liraRect.size() < 4) {
        // LIRA failed, use outline directly
        building = keepOutline;
        geometry.push_back(building);
        return;
    }

    geom::Polygon rectPoly(liraRect);

    // Create building using Dwellings-style cellular growth
    // mfcg.js: Building.create(a, PolyCore.area(this.patch.shape) / 25, null, null, .4)
    // Parameters: minSq=area/25, hasFront=false, symmetric=false, gap=0.4
    double minBlockSq = std::abs(patch->shape.square()) / 25.0;
    building = building::Building::create(
        rectPoly,
        minBlockSq,
        false,   // hasFront
        false,   // symmetric
        0.4      // gap
    );

    // If building creation failed, use LIRA rect directly
    if (building.length() < 3) {
        building = rectPoly;
    }

    geometry.push_back(building);
}

} // namespace wards
} // namespace town_generator
