#include "town_generator/geom/PolyBool.h"
#include "town_generator/geom/GeomUtils.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace geom {

bool PolyBool::pointsEqual(const Point& a, const Point& b, double epsilon) {
    return std::abs(a.x - b.x) < epsilon && std::abs(a.y - b.y) < epsilon;
}

int PolyBool::findPointIndex(const std::vector<Point>& poly, const Point& point) {
    for (size_t i = 0; i < poly.size(); ++i) {
        if (pointsEqual(poly[i], point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool PolyBool::containsPoint(const std::vector<Point>& poly, const Point& p, bool excludeBoundary) {
    // Ray casting algorithm for point-in-polygon test
    if (poly.size() < 3) return false;

    int crossings = 0;
    size_t n = poly.size();

    for (size_t i = 0; i < n; ++i) {
        const Point& p1 = poly[i];
        const Point& p2 = poly[(i + 1) % n];

        // Check if point is on boundary
        if (!excludeBoundary) {
            double dx = p2.x - p1.x;
            double dy = p2.y - p1.y;
            double lenSq = dx * dx + dy * dy;
            if (lenSq > 1e-9) {
                double t = ((p.x - p1.x) * dx + (p.y - p1.y) * dy) / lenSq;
                if (t >= 0.0 && t <= 1.0) {
                    Point proj(p1.x + t * dx, p1.y + t * dy);
                    double distSq = (p.x - proj.x) * (p.x - proj.x) + (p.y - proj.y) * (p.y - proj.y);
                    if (distSq < 1e-9) return true;  // On boundary
                }
            }
        }

        // Ray casting
        if ((p1.y <= p.y && p2.y > p.y) || (p2.y <= p.y && p1.y > p.y)) {
            double vt = (p.y - p1.y) / (p2.y - p1.y);
            if (p.x < p1.x + vt * (p2.x - p1.x)) {
                ++crossings;
            }
        }
    }

    return (crossings % 2) == 1;
}

std::pair<std::vector<Point>, std::vector<Point>> PolyBool::augmentPolygons(
    const std::vector<Point>& polyA,
    const std::vector<Point>& polyB
) {
    // Faithful port of mfcg.js PolyBool.augmentPolygons
    // Find all edge intersection points and insert into both polygons

    size_t lenA = polyA.size();
    size_t lenB = polyB.size();

    // Arrays to store intersection points for each edge
    // Each element is a vector of {parameter_along_edge, intersection_point}
    struct Intersection {
        double paramA;  // Parameter along edge A (0-1)
        double paramB;  // Parameter along edge B (0-1)
        Point p;        // Intersection point
    };

    std::vector<std::vector<Intersection>> intersectionsA(lenA);
    std::vector<std::vector<Intersection>> intersectionsB(lenB);

    // Find all intersections between edges
    for (size_t i = 0; i < lenA; ++i) {
        const Point& a1 = polyA[i];
        const Point& a2 = polyA[(i + 1) % lenA];
        double ax = a1.x;
        double ay = a1.y;
        double adx = a2.x - ax;
        double ady = a2.y - ay;

        for (size_t j = 0; j < lenB; ++j) {
            const Point& b1 = polyB[j];
            const Point& b2 = polyB[(j + 1) % lenB];
            double bx = b1.x;
            double by = b1.y;
            double bdx = b2.x - bx;
            double bdy = b2.y - by;

            auto result = GeomUtils::intersectLines(
                ax, ay, adx, ady,
                bx, by, bdx, bdy
            );

            if (result.has_value()) {
                double ta = result->x;  // Parameter along edge A
                double tb = result->y;  // Parameter along edge B

                // Check if intersection is strictly within both edges
                // Use small epsilon to avoid duplicating endpoints
                const double eps = 1e-6;
                if (ta > eps && ta < (1.0 - eps) &&
                    tb > eps && tb < (1.0 - eps)) {
                    // Found valid intersection
                    Point point = GeomUtils::lerp(a1, a2, ta);
                    Intersection inter{ta, tb, point};
                    intersectionsA[i].push_back(inter);
                    intersectionsB[j].push_back(inter);
                }
            }
        }
    }

    // Build augmented polygon A
    std::vector<Point> augmentedA;
    for (size_t i = 0; i < lenA; ++i) {
        augmentedA.push_back(polyA[i]);
        auto& edgeIntersections = intersectionsA[i];
        if (!edgeIntersections.empty()) {
            // Sort by parameter along edge
            std::sort(edgeIntersections.begin(), edgeIntersections.end(),
                [](const Intersection& x, const Intersection& y) {
                    return x.paramA < y.paramA;
                });
            for (const auto& inter : edgeIntersections) {
                augmentedA.push_back(inter.p);
            }
        }
    }

    // Build augmented polygon B
    std::vector<Point> augmentedB;
    for (size_t i = 0; i < lenB; ++i) {
        augmentedB.push_back(polyB[i]);
        auto& edgeIntersections = intersectionsB[i];
        if (!edgeIntersections.empty()) {
            // Sort by parameter along edge
            std::sort(edgeIntersections.begin(), edgeIntersections.end(),
                [](const Intersection& x, const Intersection& y) {
                    return x.paramB < y.paramB;
                });
            for (const auto& inter : edgeIntersections) {
                augmentedB.push_back(inter.p);
            }
        }
    }

    return {augmentedA, augmentedB};
}

std::vector<Point> PolyBool::polygonAnd(
    const std::vector<Point>& polyA,
    const std::vector<Point>& polyB,
    bool returnA
) {
    // Faithful port of mfcg.js PolyBool.and
    // Computes intersection of two polygons using boundary tracing

    if (polyA.size() < 3 || polyB.size() < 3) {
        return returnA ? polyA : std::vector<Point>{};
    }

    auto [augA, augB] = augmentPolygons(polyA, polyB);

    // If no new points were added, check containment
    if (augA.size() == polyA.size()) {
        // No intersections - check if one contains the other
        if (containsPoint(polyA, polyB[0])) {
            return returnA ? polyA : polyB;
        }
        if (containsPoint(polyB, polyA[0], returnA)) {
            return returnA ? std::vector<Point>{} : polyA;
        }
        return returnA ? polyA : std::vector<Point>{};
    }

    // Find first intersection point (a point added during augmentation)
    int startIdx = -1;
    Point startPoint;

    for (size_t i = 0; i < augA.size(); ++i) {
        // Check if this point is an intersection (not in original polyA)
        bool isOriginal = false;
        for (const auto& p : polyA) {
            if (pointsEqual(augA[i], p)) {
                isOriginal = true;
                break;
            }
        }
        if (!isOriginal) {
            startIdx = static_cast<int>(i);
            startPoint = augA[i];
            break;
        }
    }

    if (startIdx == -1) {
        return returnA ? polyA : std::vector<Point>{};
    }

    // Determine which polygon to trace first
    // Test midpoint of first edge from intersection
    std::vector<Point>* currentPoly = &augA;
    std::vector<Point>* otherPoly = &augB;

    size_t nextIdxTest = (static_cast<size_t>(startIdx) + 1) % augA.size();
    Point testPoint = GeomUtils::lerp(startPoint, augA[nextIdxTest], 0.5);

    if (!containsPoint(polyB, testPoint, returnA)) {
        currentPoly = &augB;
        otherPoly = &augA;
        startIdx = findPointIndex(augB, startPoint);
        if (startIdx == -1) {
            return returnA ? polyA : std::vector<Point>{};
        }
    }

    // Trace the intersection boundary
    std::vector<Point> result;
    int idx = startIdx;
    size_t maxIterations = augA.size() + augB.size() + 10;

    while (result.size() < maxIterations) {
        result.push_back((*currentPoly)[idx]);

        size_t nextIdx = (static_cast<size_t>(idx) + 1) % currentPoly->size();
        const Point& nextPoint = (*currentPoly)[nextIdx];

        // Check if we've completed the loop
        if (!result.empty() && (pointsEqual(nextPoint, result[0]))) {
            return result;
        }

        // Check if next point is in other polygon (switch polygons at intersection)
        int otherIdx = findPointIndex(*otherPoly, nextPoint);
        if (otherIdx != -1) {
            // Switch polygons
            idx = otherIdx;
            std::swap(currentPoly, otherPoly);
        } else {
            idx = static_cast<int>(nextIdx);
        }
    }

    // Safety: if we get here, something went wrong
    return result.size() >= 3 ? result : (returnA ? polyA : std::vector<Point>{});
}

std::vector<Point> PolyBool::polygonSubtract(
    const std::vector<Point>& polyA,
    const std::vector<Point>& polyB
) {
    // Subtraction: A - B
    // This is computed as intersection of A with the complement of B
    // For stripe subtraction in gap application, we reverse B and intersect

    if (polyA.size() < 3 || polyB.size() < 3) {
        return polyA;
    }

    // Reverse polyB to represent its complement for the intersection trace
    std::vector<Point> reversedB = GeomUtils::reverse(polyB);

    // Now trace the boundary of A - B
    auto [augA, augB] = augmentPolygons(polyA, reversedB);

    // If no intersections, check containment
    if (augA.size() == polyA.size()) {
        // Check if B is entirely inside A (result is A minus a hole - return A for simplicity)
        // Check if A is entirely inside B (result is empty)
        if (containsPoint(polyB, polyA[0])) {
            return {};  // A is inside B, subtraction is empty
        }
        // B doesn't overlap A significantly
        return polyA;
    }

    // Find first intersection point
    int startIdx = -1;
    Point startPoint;

    for (size_t i = 0; i < augA.size(); ++i) {
        bool isOriginal = false;
        for (const auto& p : polyA) {
            if (pointsEqual(augA[i], p)) {
                isOriginal = true;
                break;
            }
        }
        if (!isOriginal) {
            startIdx = static_cast<int>(i);
            startPoint = augA[i];
            break;
        }
    }

    if (startIdx == -1) {
        return polyA;
    }

    // For subtraction, we trace along A when outside B, and along reversed B when inside
    std::vector<Point>* currentPoly = &augA;
    std::vector<Point>* otherPoly = &augB;

    // Test which direction to start
    size_t nextIdxTest = (static_cast<size_t>(startIdx) + 1) % augA.size();
    Point testPoint = GeomUtils::lerp(startPoint, augA[nextIdxTest], 0.5);

    // For subtraction, we want to trace parts of A that are OUTSIDE B
    if (containsPoint(polyB, testPoint)) {
        // This direction goes INTO B, so switch to B (reversed)
        currentPoly = &augB;
        otherPoly = &augA;
        startIdx = findPointIndex(augB, startPoint);
        if (startIdx == -1) {
            return polyA;
        }
    }

    // Trace the subtraction boundary
    std::vector<Point> result;
    int idx = startIdx;
    size_t maxIterations = augA.size() + augB.size() + 10;

    while (result.size() < maxIterations) {
        result.push_back((*currentPoly)[idx]);

        size_t nextIdx = (static_cast<size_t>(idx) + 1) % currentPoly->size();
        const Point& nextPoint = (*currentPoly)[nextIdx];

        // Check if we've completed the loop
        if (!result.empty() && pointsEqual(nextPoint, result[0])) {
            return result;
        }

        // Check if next point is in other polygon (switch at intersection)
        int otherIdx = findPointIndex(*otherPoly, nextPoint);
        if (otherIdx != -1) {
            idx = otherIdx;
            std::swap(currentPoly, otherPoly);
        } else {
            idx = static_cast<int>(nextIdx);
        }
    }

    return result.size() >= 3 ? result : polyA;
}

} // namespace geom
} // namespace town_generator
