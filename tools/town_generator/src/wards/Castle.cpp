#include "town_generator/wards/Castle.h"
#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
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

// Helper: create a building using Dwellings-style cellular growth
// Faithful to mfcg.js Jd.create (lines 9789-9808)
static geom::Polygon createDwellingsBuilding(const geom::Polygon& outline, double minBlockSq) {
    // Get OBB of the outline
    auto obb = outline.orientedBoundingBox();
    if (obb.size() < 4) return outline;

    double sideLen = std::sqrt(minBlockSq);
    double width = geom::Point::distance(obb[0], obb[1]);
    double height = geom::Point::distance(obb[1], obb[2]);

    int gridW = static_cast<int>(std::ceil(std::min(width, geom::Point::distance(obb[2], obb[3])) / sideLen));
    int gridH = static_cast<int>(std::ceil(std::min(height, geom::Point::distance(obb[3], obb[0])) / sideLen));

    if (gridW <= 1 || gridH <= 1) return outline;

    // Generate a cellular plan (Jd.getPlan style)
    // Start with a random cell and grow outward
    std::vector<bool> plan(gridW * gridH, false);

    int startX = utils::Random::intVal(0, gridW - 1);
    int startY = utils::Random::intVal(0, gridH - 1);
    plan[startX + startY * gridW] = true;

    int minX = startX, maxX = startX;
    int minY = startY, maxY = startY;

    // Grow the building
    int iterations = gridW * gridH * 2;
    while (iterations-- > 0) {
        int x = utils::Random::intVal(0, gridW - 1);
        int y = utils::Random::intVal(0, gridH - 1);
        int idx = x + y * gridW;

        if (!plan[idx]) {
            // Check if adjacent to existing cell
            bool hasNeighbor = false;
            if (x > 0 && plan[idx - 1]) hasNeighbor = true;
            if (x < gridW - 1 && plan[idx + 1]) hasNeighbor = true;
            if (y > 0 && plan[idx - gridW]) hasNeighbor = true;
            if (y < gridH - 1 && plan[idx + gridW]) hasNeighbor = true;

            if (hasNeighbor) {
                plan[idx] = true;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }

        // Stop if we've filled enough or expanded to edges
        if (minX == 0 && maxX == gridW - 1 && minY == 0 && maxY == gridH - 1) {
            if (utils::Random::boolVal(0.5)) break;
        }
    }

    // Count filled cells
    int filledCount = 0;
    for (bool b : plan) if (b) filledCount++;

    // If nearly full, just return the outline
    if (filledCount >= gridW * gridH) return outline;

    // Build individual cell rectangles and merge into circumference
    // For simplicity, we'll create an L-shaped or irregular building
    // by shrinking the outline and adding some cutouts

    // Calculate cell dimensions
    geom::Point v0 = obb[1].subtract(obb[0]);
    geom::Point v1 = obb[3].subtract(obb[0]);
    v0 = v0.scale(1.0 / gridW);
    v1 = v1.scale(1.0 / gridH);

    // For now, create a simplified L-shape if the plan is irregular
    // This approximates the Dwellings engine output
    std::vector<geom::Point> buildingPoints;

    // Find the bounding box of filled cells
    int cellMinX = gridW, cellMaxX = -1, cellMinY = gridH, cellMaxY = -1;
    for (int y = 0; y < gridH; ++y) {
        for (int x = 0; x < gridW; ++x) {
            if (plan[x + y * gridW]) {
                cellMinX = std::min(cellMinX, x);
                cellMaxX = std::max(cellMaxX, x);
                cellMinY = std::min(cellMinY, y);
                cellMaxY = std::max(cellMaxY, y);
            }
        }
    }

    // Create a simple polygon covering the filled cells
    // Start at top-left corner and trace clockwise
    geom::Point origin = obb[0];

    auto gridToWorld = [&](int x, int y) {
        return origin.add(v0.scale(x)).add(v1.scale(y));
    };

    // Simple rectangular building covering filled area
    buildingPoints.push_back(gridToWorld(cellMinX, cellMinY));
    buildingPoints.push_back(gridToWorld(cellMaxX + 1, cellMinY));
    buildingPoints.push_back(gridToWorld(cellMaxX + 1, cellMaxY + 1));
    buildingPoints.push_back(gridToWorld(cellMinX, cellMaxY + 1));

    // Add some L-shape variation if there are empty corners
    bool topRightEmpty = !plan[cellMaxX + cellMinY * gridW];
    bool bottomLeftEmpty = !plan[cellMinX + cellMaxY * gridW];

    if (topRightEmpty && cellMaxX > cellMinX && cellMaxY > cellMinY) {
        // Cut corner
        buildingPoints.clear();
        buildingPoints.push_back(gridToWorld(cellMinX, cellMinY));
        buildingPoints.push_back(gridToWorld(cellMaxX, cellMinY));
        buildingPoints.push_back(gridToWorld(cellMaxX, cellMinY + 1));
        buildingPoints.push_back(gridToWorld(cellMaxX + 1, cellMinY + 1));
        buildingPoints.push_back(gridToWorld(cellMaxX + 1, cellMaxY + 1));
        buildingPoints.push_back(gridToWorld(cellMinX, cellMaxY + 1));
    } else if (bottomLeftEmpty && cellMaxX > cellMinX && cellMaxY > cellMinY) {
        // Cut corner
        buildingPoints.clear();
        buildingPoints.push_back(gridToWorld(cellMinX, cellMinY));
        buildingPoints.push_back(gridToWorld(cellMaxX + 1, cellMinY));
        buildingPoints.push_back(gridToWorld(cellMaxX + 1, cellMaxY + 1));
        buildingPoints.push_back(gridToWorld(cellMinX + 1, cellMaxY + 1));
        buildingPoints.push_back(gridToWorld(cellMinX + 1, cellMaxY));
        buildingPoints.push_back(gridToWorld(cellMinX, cellMaxY));
    }

    return geom::Polygon(buildingPoints);
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
        return {std::sqrt(minR), std::sqrt(maxR)};
    };

    auto [minRadius, maxRadius] = calcRadii();

    // Bloat if minimum dimension < 10 (mfcg.js line 12491)
    while (minRadius < 10.0) {
        SDL_Log("Bloating the citadel... (minRadius=%.2f)", minRadius);

        double bloatRadius = 2.0 * std::max(15.0, maxRadius);

        // Move all vertices away from center (power law)
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            geom::Point& v = patch->shape[i];
            double dist = geom::Point::distance(v, center);
            if (dist < bloatRadius) {
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
    if (!patch || !model) return;

    // Adjust shape for better citadel appearance
    // Note: In mfcg.js this is done in constructor, but we do it here
    // since the curtain wall needs to be built first
    // adjustShape();  // Disabled for now - requires model vertex mutation

    // Create keep building by shrinking the shape
    // mfcg.js: gd.shrinkEq(this.patch.shape, pc.THICKNESS + 2)
    double shrinkAmount = building::CurtainWall::THICKNESS + 2.0;
    geom::Polygon keepOutline = patch->shape.shrinkEq(shrinkAmount);

    if (keepOutline.length() < 3) {
        // Fallback: create simple rectangular keep
        geom::Point center = patch->shape.centroid();
        double radius = std::sqrt(std::abs(patch->shape.square()) / M_PI) * 0.4;
        keepOutline = geom::Polygon::rect(radius * 1.5, radius * 1.2);
        keepOutline.offset(center);
    }

    // Make outline convex (lira operation in mfcg.js)
    // For now we skip this and just use the shrunk shape

    // Create building using Dwellings engine
    // mfcg.js: Jd.create(a, Sa.area(this.patch.shape) / 25, null, null, .4)
    double minBlockSq = std::abs(patch->shape.square()) / 25.0;
    building = createDwellingsBuilding(keepOutline, minBlockSq);

    // If building creation failed, use outline directly
    if (building.length() < 3) {
        building = keepOutline;
    }

    // Add to geometry for rendering
    geometry.clear();
    geometry.push_back(building);
}

} // namespace wards
} // namespace town_generator
