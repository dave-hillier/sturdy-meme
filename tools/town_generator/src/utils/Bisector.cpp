#include "town_generator/utils/Bisector.h"
#include "town_generator/utils/Random.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/geom/Polygon.h"
#include <cmath>
#include <algorithm>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace utils {

// Helper: compute polygon area (signed)
static double polyArea(const std::vector<geom::Point>& poly) {
    double area = 0;
    size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        area += poly[i].x * poly[j].y;
        area -= poly[j].x * poly[i].y;
    }
    return area / 2.0;
}

// Helper: compute centroid
static geom::Point polyCentroid(const std::vector<geom::Point>& poly) {
    double x = 0, y = 0, a = 0;
    size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& v0 = poly[i];
        const auto& v1 = poly[(i + 1) % n];
        double f = v0.x * v1.y - v1.x * v0.y;
        a += f;
        x += (v0.x + v1.x) * f;
        y += (v0.y + v1.y) * f;
    }
    double s6 = 1.0 / (3.0 * a);
    return geom::Point(s6 * x, s6 * y);
}

// Helper: get OBB (oriented bounding box)
static std::vector<geom::Point> getOBB(const std::vector<geom::Point>& poly) {
    geom::Polygon p(poly);
    return p.orientedBoundingBox();
}

// Helper: rotate polygon around origin
static std::vector<geom::Point> rotateYX(const std::vector<geom::Point>& poly, double sinA, double cosA) {
    std::vector<geom::Point> result;
    result.reserve(poly.size());
    for (const auto& p : poly) {
        result.emplace_back(
            p.x * cosA - p.y * sinA,
            p.x * sinA + p.y * cosA
        );
    }
    return result;
}

// Helper: get AABB (axis-aligned bounding box)
static std::vector<geom::Point> getAABB(const std::vector<geom::Point>& poly) {
    if (poly.empty()) return {};

    double minX = poly[0].x, maxX = poly[0].x;
    double minY = poly[0].y, maxY = poly[0].y;

    for (const auto& p : poly) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }

    return {
        geom::Point(minX, minY),
        geom::Point(maxX, minY),
        geom::Point(maxX, maxY),
        geom::Point(minX, maxY)
    };
}

// Helper: project point onto vector (returns scalar projection normalized by vector length squared)
static double project(const geom::Point& vec, const geom::Point& point) {
    double lenSq = vec.x * vec.x + vec.y * vec.y;
    if (lenSq < 1e-10) return 0;
    return (vec.x * point.x + vec.y * point.y) / lenSq;
}

// Helper: reverse a polygon
static std::vector<geom::Point> revert(const std::vector<geom::Point>& poly) {
    std::vector<geom::Point> result(poly.rbegin(), poly.rend());
    return result;
}

// Helper: normal3 random (sum of 3 randoms / 3)
static double normal3() {
    return (Random::floatVal() + Random::floatVal() + Random::floatVal()) / 3.0;
}

// Helper: normal4 random (sum of 4 randoms / 2 - 1)
static double normal4() {
    return (Random::floatVal() + Random::floatVal() +
            Random::floatVal() + Random::floatVal()) / 2.0 - 1.0;
}

Bisector::Bisector(const std::vector<geom::Point>& poly, double minArea, double variance)
    : poly(poly)
    , minArea(minArea)
    , variance(variance)
    , minOffset(std::sqrt(minArea))
{
    // Default callbacks
    processCut = [this](const std::vector<geom::Point>& cut) {
        return this->detectStraight(cut);
    };

    isAtomic = [this](const std::vector<geom::Point>& p) {
        return this->isSmallEnough(p);
    };
}

std::vector<std::vector<geom::Point>> Bisector::partition() {
    return subdivide(poly);
}

std::vector<std::vector<geom::Point>> Bisector::subdivide(const std::vector<geom::Point>& poly) {
    if (isAtomic(poly)) {
        return {poly};
    }

    auto parts = makeCut(poly);
    if (parts.size() == 1) {
        return {poly};
    }

    std::vector<std::vector<geom::Point>> result;
    for (const auto& part : parts) {
        auto subparts = subdivide(part);
        for (const auto& sp : subparts) {
            result.push_back(sp);
        }
    }
    return result;
}

bool Bisector::isSmallEnough(const std::vector<geom::Point>& poly) {
    // Faithful to mfcg.js: minArea * pow(variance, |normal4|)
    double threshold = minArea * std::pow(variance, std::abs(normal4()));
    return std::abs(polyArea(poly)) < threshold;
}

std::vector<std::vector<geom::Point>> Bisector::makeCut(
    const std::vector<geom::Point>& a, int attempt
) {
    if (attempt > 10) {
        return {a};
    }

    size_t c = a.size();
    if (c < 3) {
        return {a};
    }

    // Get bounding box (OBB or rotated AABB for retry attempts)
    std::vector<geom::Point> f;
    if (attempt > 0) {
        // Rotate polygon and get AABB, then rotate back
        double angle = static_cast<double>(attempt) / 10.0 * M_PI * 2.0;
        double sinA = std::sin(angle);
        double cosA = std::cos(angle);
        auto rotated = rotateYX(a, sinA, cosA);
        auto aabb = getAABB(rotated);
        f = rotateYX(aabb, -sinA, cosA);
    } else {
        f = getOBB(a);
    }

    if (f.size() < 4) {
        return {a};
    }

    // Get OBB corners and edge vectors
    geom::Point d = f[0];
    geom::Point h = f[1].subtract(d);  // First edge
    geom::Point k = f[3].subtract(d);  // Adjacent edge

    // h should be the longer edge (perpendicular to cut direction)
    if (h.length() < k.length()) {
        std::swap(h, k);
    }

    // Find cut position along longer edge using centroid projection + randomness
    geom::Point centroid = polyCentroid(a);
    double projF = project(h, centroid.subtract(d));
    projF = (projF + normal3()) / 2.0;  // Mix centroid position with random

    // Point along the longer edge where we want to cut
    geom::Point p(d.x + h.x * projF, d.y + h.y * projF);

    // Find the edge most aligned with the longer axis (where cut will start)
    int edge1 = -1;
    geom::Point cutStart;
    geom::Point edgeDir;
    double bestAlign = 0;

    for (size_t r = 0; r < c; ++r) {
        const geom::Point& l = a[r];
        const geom::Point& n = a[(r + 1) % c];
        geom::Point x = n.subtract(l);

        if (x.length() < 1e-10) continue;

        // Intersect ray from p in direction k with edge
        auto t = geom::GeomUtils::intersectLines(p.x, p.y, k.x, k.y, l.x, l.y, x.x, x.y);
        if (!t.has_value() || t->y <= 0 || t->y >= 1) continue;

        // Check alignment with longer axis
        geom::Point xNorm = x.scale(1.0 / x.length());
        double align = std::abs(h.x * xNorm.x + h.y * xNorm.y);

        if (align > bestAlign) {
            bestAlign = align;
            edge1 = static_cast<int>(r);
            cutStart = geom::Point(l.x + x.x * t->y, l.y + x.y * t->y);
            edgeDir = xNorm;
        }
    }

    if (edge1 < 0) {
        return makeCut(a, attempt + 1);
    }

    // Perpendicular direction for cutting
    geom::Point g(-edgeDir.y, edgeDir.x);

    // Find opposite edge to cut to
    double minDist = std::numeric_limits<double>::infinity();
    geom::Point cutEnd;
    int edge2 = -1;

    for (size_t r = 0; r < c; ++r) {
        if (static_cast<int>(r) == edge1) continue;

        const geom::Point& l = a[r];
        const geom::Point& n = a[(r + 1) % c];
        geom::Point x = n.subtract(l);

        if (x.length() < 1e-10) continue;

        auto t = geom::GeomUtils::intersectLines(
            cutStart.x, cutStart.y, g.x, g.y,
            l.x, l.y, x.x, x.y
        );

        if (!t.has_value()) continue;
        if (t->x <= 0 || t->x >= minDist) continue;
        if (t->y <= 0 || t->y >= 1) continue;

        minDist = t->x;
        edge2 = static_cast<int>(r);
    }

    if (edge2 < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Bisector: Failed to find opposite edge (attempt %d)", attempt);
        return makeCut(a, attempt + 1);
    }

    // Check if cut is perpendicular enough (cross product check)
    const geom::Point& l2 = a[edge2];
    const geom::Point& n2 = a[(edge2 + 1) % c];
    geom::Point edge2Dir = n2.subtract(l2);

    double cross = g.x * edge2Dir.y - g.y * edge2Dir.x;
    double crossNormSq = (cross * cross) /
        ((g.x * g.x + g.y * g.y) * (edge2Dir.x * edge2Dir.x + edge2Dir.y * edge2Dir.y));

    // If nearly perpendicular (crossNormSq > 0.99), try straight cut first
    if (crossNormSq > 0.99) {
        geom::Point straightEnd(cutStart.x + g.x * minDist, cutStart.y + g.y * minDist);
        std::vector<geom::Point> cutLine = {cutStart, straightEnd};

        auto result = split(a, edge1, edge2, cutLine);

        double area0 = std::abs(polyArea(result[0]));
        double area1 = std::abs(polyArea(result[1]));
        double ratio = std::max(area0 / area1, area1 / area0);

        if (ratio < 2.0 * variance) {
            cuts.push_back(cutLine);
            // Gap application via stripe subtraction requires proper polygon boolean ops
            // For now, rely on post-subdivision shrinking in WardGroup.cpp (BLOCK_INSET)
            // TODO: Implement proper PolyBool.and for faithful mfcg.js gap application
            return result;
        }
    }

    // Non-perpendicular case: find best turning point
    double offsetRatio = minOffset / minDist;
    if (offsetRatio > 0.5) offsetRatio = 0.5;
    offsetRatio = offsetRatio + (1.0 - 2.0 * offsetRatio) * normal3();

    double turnDist = minDist * offsetRatio;
    geom::Point turnPoint(cutStart.x + g.x * turnDist, cutStart.y + g.y * turnDist);

    // Find edge for second leg of cut
    int edge3 = -1;
    geom::Point cutEnd3;
    double bestCross = -std::numeric_limits<double>::infinity();

    for (size_t r = 0; r < c; ++r) {
        if (static_cast<int>(r) == edge1) continue;

        const geom::Point& l = a[r];
        const geom::Point& n = a[(r + 1) % c];
        geom::Point x = n.subtract(l);
        double xLen = x.length();

        if (xLen < 1e-10) continue;

        // Cast ray perpendicular to edge
        auto t = geom::GeomUtils::intersectLines(
            turnPoint.x, turnPoint.y, x.y, -x.x,
            l.x, l.y, x.x, x.y
        );

        if (!t.has_value() || t->x <= 0 || t->y <= 0 || t->y >= 1) continue;

        double crossVal = (g.x * x.y - g.y * x.x) / xLen;
        if (crossVal <= bestCross) continue;

        // Check that ray doesn't cross other edges
        bool valid = true;
        for (size_t y = 0; y < c && valid; ++y) {
            if (y == r || static_cast<int>(y) == edge1) continue;

            const geom::Point& yl = a[y];
            geom::Point yDir = a[(y + 1) % c].subtract(yl);
            if (yDir.length() < 1e-10) continue;

            auto check = geom::GeomUtils::intersectLines(
                turnPoint.x, turnPoint.y, x.y, -x.x,
                yl.x, yl.y, yDir.x, yDir.y
            );

            if (check.has_value() &&
                check->x >= 0 && check->x <= 1 &&
                check->y >= 0 && check->y <= 1) {
                valid = false;
            }
        }

        if (valid) {
            bestCross = crossVal;
            edge3 = static_cast<int>(r);
            cutEnd3 = geom::Point(l.x + x.x * t->y, l.y + x.y * t->y);
        }
    }

    if (edge3 >= 0) {
        std::vector<geom::Point> cutLine = {cutStart, turnPoint, cutEnd3};

        // Process cut (smooth or simplify)
        auto processedCut = processCut(cutLine);

        // Validate that interior points are inside polygon
        bool valid = true;
        for (size_t i = 1; i < processedCut.size() - 1 && valid; ++i) {
            if (!geom::GeomUtils::containsPoint(a, processedCut[i])) {
                valid = false;
            }
        }
        if (!valid) {
            processedCut = cutLine;
        }

        auto result = split(a, edge1, edge3, processedCut);

        double area0 = std::abs(polyArea(result[0]));
        double area1 = std::abs(polyArea(result[1]));
        double ratio = std::max(area0 / area1, area1 / area0);

        if (ratio > 2.0 * variance) {
            return makeCut(a, attempt + 1);
        }

        cuts.push_back(processedCut);
        // Gap application via stripe subtraction requires proper polygon boolean ops
        // For now, rely on post-subdivision shrinking in WardGroup.cpp (BLOCK_INSET)
        // TODO: Implement proper PolyBool.and for faithful mfcg.js gap application
        return result;
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Bisector: Failed to make cut (attempt %d)", attempt);
    return makeCut(a, attempt + 1);
}

std::vector<std::vector<geom::Point>> Bisector::split(
    std::vector<geom::Point> a,
    int edge1,
    int edge2,
    const std::vector<geom::Point>& cutLine
) {
    // Faithful port of mfcg.js Bisector.split
    // Insert cut endpoints into polygon at the appropriate edges

    geom::Point h1 = cutLine.front();
    geom::Point h2 = cutLine.back();

    // Insert first cut point if not already a vertex
    if (!(a[edge1] == h1)) {
        if (edge1 < edge2) ++edge2;
        a.insert(a.begin() + edge1 + 1, h1);
        ++edge1;
    }

    // Insert second cut point if not already a vertex
    if (!(a[edge2] == h2)) {
        if (edge2 < edge1) ++edge1;
        a.insert(a.begin() + edge2 + 1, h2);
        ++edge2;
    }

    std::vector<geom::Point> poly1, poly2;
    auto cutRev = revert(cutLine);

    if (edge1 < edge2) {
        // First polygon: edge1+1 to edge2, then reversed cut
        for (int i = edge1 + 1; i <= edge2; ++i) {
            poly1.push_back(a[i]);
        }
        for (const auto& p : cutRev) {
            poly1.push_back(p);
        }

        // Second polygon: edge2+1 to end, 0 to edge1, then cut
        for (size_t i = edge2 + 1; i < a.size(); ++i) {
            poly2.push_back(a[i]);
        }
        for (int i = 0; i <= edge1; ++i) {
            poly2.push_back(a[i]);
        }
        for (const auto& p : cutLine) {
            poly2.push_back(p);
        }
    } else {
        // First polygon: edge1+1 to end, 0 to edge2, then reversed cut
        for (size_t i = edge1 + 1; i < a.size(); ++i) {
            poly1.push_back(a[i]);
        }
        for (int i = 0; i <= edge2; ++i) {
            poly1.push_back(a[i]);
        }
        for (const auto& p : cutRev) {
            poly1.push_back(p);
        }

        // Second polygon: edge2+1 to edge1, then cut
        for (int i = edge2 + 1; i <= edge1; ++i) {
            poly2.push_back(a[i]);
        }
        for (const auto& p : cutLine) {
            poly2.push_back(p);
        }
    }

    return {poly1, poly2};
}

std::vector<geom::Point> Bisector::detectStraight(const std::vector<geom::Point>& cut) {
    if (minTurnOffset > 0 && cut.size() >= 3) {
        const geom::Point& b = cut[0];
        const geom::Point& c = cut[2];

        // If area of triangle is small relative to distance, simplify to straight line
        double triArea = std::abs(polyArea(cut));
        double dist = geom::Point::distance(b, c);

        if (triArea / dist < minTurnOffset) {
            return {b, c};
        }
    }
    return cut;
}

std::vector<std::vector<geom::Point>> Bisector::applyGap(
    const std::vector<std::vector<geom::Point>>& halves,
    const std::vector<geom::Point>& cutLine
) {
    // Faithful to mfcg.js Bisector gap application (lines 19562-19567, 19603-19608)
    // Uses PolyCreate.stripe + PolyBool.and(half, revert(stripe))

    if (!getGap || halves.size() < 2 || cutLine.size() < 2) {
        return halves;
    }

    double gap = getGap(cutLine);
    if (gap <= 0) {
        return halves;
    }

    // Create stripe polygon along the cut line (mfcg.js: PolyCreate.stripe)
    std::vector<geom::Point> stripePoly = geom::GeomUtils::stripe(cutLine, gap, 1.0);

    if (stripePoly.size() < 3) {
        return halves;
    }

    // Reverse the stripe for subtraction (mfcg.js: Z.revert(a))
    std::vector<geom::Point> stripeReversed = geom::GeomUtils::reverse(stripePoly);

    std::vector<std::vector<geom::Point>> result;

    for (const auto& halfPts : halves) {
        if (halfPts.size() < 3) {
            result.push_back(halfPts);
            continue;
        }

        // Boolean AND with reversed stripe (mfcg.js: PolyBool.and(b, Z.revert(a), !0))
        // This effectively subtracts the stripe from the polygon
        std::vector<geom::Point> clipped = geom::GeomUtils::polygonIntersection(
            halfPts, stripeReversed, true  // subtract = true
        );

        if (clipped.size() >= 3) {
            result.push_back(clipped);
        } else {
            // Fallback: if boolean operation fails, return original
            result.push_back(halfPts);
        }
    }

    return result;
}

} // namespace utils
} // namespace town_generator
