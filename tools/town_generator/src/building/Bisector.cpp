#include "town_generator/building/Bisector.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace town_generator {
namespace building {

Bisector::Bisector(const geom::Polygon& poly, double minArea, double variance)
    : poly(poly), minArea(minArea), variance(variance) {
    minOffset = std::sqrt(minArea);
}

std::vector<geom::Polygon> Bisector::partition() {
    return subdivide(poly);
}

std::vector<geom::Polygon> Bisector::subdivide(const geom::Polygon& shape) {
    // Check if atomic (can't be further subdivided)
    bool atomic = isAtomic ? isAtomic(shape) : isSmallEnough(shape);

    if (atomic) {
        return {shape};
    }

    auto halves = makeCut(shape);

    if (halves.size() == 1) {
        // Cut failed, return as-is
        return {shape};
    }

    // Recursively subdivide each half
    std::vector<geom::Polygon> result;
    for (const auto& half : halves) {
        auto subparts = subdivide(half);
        for (const auto& part : subparts) {
            result.push_back(part);
        }
    }

    return result;
}

bool Bisector::isSmallEnough(const geom::Polygon& shape) {
    // MFCG: threshold = minArea * pow(variance, |normal4 - 1|)
    // where normal4 is sum of 4 randoms / 2
    double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                      utils::Random::floatVal() + utils::Random::floatVal()) / 2.0;
    double threshold = minArea * std::pow(variance, std::abs(normal4 - 1.0));

    return std::abs(shape.square()) < threshold;
}

std::vector<geom::Polygon> Bisector::makeCut(const geom::Polygon& shape, int attempt) {
    // MFCG makeCut (lines 19513-19640)
    if (attempt > 10) {
        return {shape};
    }

    size_t n = shape.length();
    if (n < 3) {
        return {shape};
    }

    // Get OBB (try rotated AABB for subsequent attempts)
    std::vector<geom::Point> obb;
    if (attempt > 0) {
        // Rotate polygon for different cut angles
        double angle = attempt / 10.0 * M_PI * 2.0;
        double cosA = std::cos(angle);
        double sinA = std::sin(angle);

        std::vector<geom::Point> rotated;
        for (size_t i = 0; i < n; ++i) {
            const geom::Point& p = shape[i];
            rotated.push_back(geom::Point(
                p.x * cosA - p.y * sinA,
                p.x * sinA + p.y * cosA
            ));
        }

        // Get AABB of rotated polygon
        double minX = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double minY = std::numeric_limits<double>::max();
        double maxY = std::numeric_limits<double>::lowest();
        for (const auto& p : rotated) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        // Create AABB corners and rotate back
        std::vector<geom::Point> aabb = {
            geom::Point(minX, minY),
            geom::Point(maxX, minY),
            geom::Point(maxX, maxY),
            geom::Point(minX, maxY)
        };

        for (auto& p : aabb) {
            double x = p.x * cosA + p.y * sinA;
            double y = -p.x * sinA + p.y * cosA;
            p = geom::Point(x, y);
        }
        obb = aabb;
    } else {
        obb = shape.orientedBoundingBox();
    }

    if (obb.size() < 4) {
        return {shape};
    }

    // Find long and short axes
    geom::Point corner = obb[0];
    geom::Point axis1 = obb[1].subtract(corner);
    geom::Point axis2 = obb[3].subtract(corner);

    geom::Point longAxis, shortAxis;
    if (axis1.length() > axis2.length()) {
        longAxis = axis1;
        shortAxis = axis2;
    } else {
        longAxis = axis2;
        shortAxis = axis1;
    }

    // Project centroid onto long axis
    geom::Point centroid = shape.centroid();
    geom::Point toCentroid = centroid.subtract(corner);
    double longLen = longAxis.length();
    double proj = 0.0;
    if (longLen > 0.001) {
        proj = (toCentroid.x * longAxis.x + toCentroid.y * longAxis.y) / (longLen * longLen);
    }

    // Cut ratio: (projection + random) / 2
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                      utils::Random::floatVal()) / 3.0;
    double cutRatio = (proj + normal3) / 2.0;
    cutRatio = std::max(0.2, std::min(0.8, cutRatio));

    // Cut point along long axis
    geom::Point cutPoint(corner.x + longAxis.x * cutRatio, corner.y + longAxis.y * cutRatio);

    // Find intersections with polygon edges along short axis direction
    geom::Point cutDir = shortAxis;
    if (cutDir.length() < 0.001) {
        return {shape};
    }
    cutDir = cutDir.norm(1.0);

    // Find the two edges that the cut line intersects
    int edge1 = -1, edge2 = -1;
    geom::Point intersect1, intersect2;
    double bestDot1 = -std::numeric_limits<double>::max();
    double bestDot2 = -std::numeric_limits<double>::max();

    geom::Point longDir = longAxis.norm(1.0);

    for (size_t i = 0; i < n; ++i) {
        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % n];

        geom::Point edgeDir = v1.subtract(v0);
        double edgeLen = edgeDir.length();
        if (edgeLen < 1e-9) continue;

        auto t = geom::GeomUtils::intersectLines(
            cutPoint.x, cutPoint.y, cutDir.x, cutDir.y,
            v0.x, v0.y, edgeDir.x, edgeDir.y
        );

        if (t.has_value() && t->y > 0.01 && t->y < 0.99) {
            geom::Point p(v0.x + edgeDir.x * t->y, v0.y + edgeDir.y * t->y);

            // Check alignment with long axis
            geom::Point normEdge = edgeDir.scale(1.0 / edgeLen);
            double dotLong = std::abs(normEdge.x * longDir.x + normEdge.y * longDir.y);

            if (dotLong > bestDot1) {
                // Shift previous best to second
                edge2 = edge1;
                intersect2 = intersect1;
                bestDot2 = bestDot1;
                // New best
                edge1 = static_cast<int>(i);
                intersect1 = p;
                bestDot1 = dotLong;
            } else if (dotLong > bestDot2 && static_cast<int>(i) != edge1) {
                edge2 = static_cast<int>(i);
                intersect2 = p;
                bestDot2 = dotLong;
            }
        }
    }

    if (edge1 == -1 || edge2 == -1) {
        // Try different angle
        return makeCut(shape, attempt + 1);
    }

    // Create cut line
    std::vector<geom::Point> cutLine = {intersect1, intersect2};

    // Process cut (e.g., smooth corners)
    if (processCut) {
        cutLine = processCut(cutLine);
    }

    // Store the cut
    cuts.push_back(cutLine);

    // Split the polygon
    auto halves = split(shape, edge1, edge2, cutLine);

    // Check area ratio
    if (halves.size() == 2) {
        double area1 = std::abs(halves[0].square());
        double area2 = std::abs(halves[1].square());
        double ratio = std::max(area1 / area2, area2 / area1);

        if (ratio > 2 * variance) {
            // Bad split, try again
            cuts.pop_back();
            return makeCut(shape, attempt + 1);
        }
    }

    // Apply gap
    if (getGap && halves.size() >= 2) {
        double gap = getGap(cutLine);
        if (gap > 0) {
            std::vector<geom::Polygon> gappedHalves;
            for (const auto& half : halves) {
                // Shrink the half away from the cut line
                geom::Polygon gapped = half.peel(cutLine[0], gap / 2);
                gappedHalves.push_back(gapped);
            }
            return gappedHalves;
        }
    }

    return halves;
}

std::vector<geom::Polygon> Bisector::split(
    const geom::Polygon& shape,
    int edge1,
    int edge2,
    const std::vector<geom::Point>& cutLine
) {
    if (cutLine.size() < 2) {
        return {shape};
    }

    // Use polygon.cut with the cut line endpoints
    auto halves = shape.cut(cutLine[0], cutLine.back(), 0);

    return halves;
}

std::vector<geom::Point> Bisector::detectStraight(const std::vector<geom::Point>& pts) {
    // Default: just return the line as-is
    return pts;
}

} // namespace building
} // namespace town_generator
