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
    // MFCG default: processCut = detectStraight (bound to this)
    // Set in constructor so it can access minTurnOffset
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
    // Faithful port of MFCG makeCut (lines 19513-19618)
    if (attempt > 10) {
        return {shape};
    }

    size_t n = shape.length();
    if (n < 3) {
        return {shape};
    }

    // Get OBB (or rotated AABB for subsequent attempts)
    // MFCG lines 19517-19522
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

    // Find long and short axes (MFCG lines 19523-19530)
    geom::Point corner = obb[0];
    geom::Point axis1 = obb[1].subtract(corner);
    geom::Point axis2 = obb[3].subtract(corner);

    geom::Point h, k;  // h = long axis, k = short axis
    if (axis1.length() > axis2.length()) {
        h = axis1;
        k = axis2;
    } else {
        h = axis2;
        k = axis1;
    }

    // Project centroid onto long axis (MFCG lines 19531-19533)
    geom::Point centroid = shape.centroid();
    geom::Point toCentroid = centroid.subtract(corner);
    double hLen = h.length();
    double proj = 0.0;
    if (hLen > 0.001) {
        proj = (toCentroid.x * h.x + toCentroid.y * h.y) / (hLen * hLen);
    }

    // Cut ratio: (projection + normal3) / 2 (MFCG line 19533)
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                      utils::Random::floatVal()) / 3.0;
    double cutRatio = (proj + normal3) / 2.0;

    // Cut point along long axis (MFCG line 19534: p)
    geom::Point p(corner.x + h.x * cutRatio, corner.y + h.y * cutRatio);

    // Find first intersection with best alignment to long axis (MFCG lines 19536-19549)
    int d = -1;  // Edge index for first intersection
    geom::Point f;  // First intersection point
    geom::Point g;  // Normalized edge direction at first intersection
    double bestDot = 0.0;

    geom::Point hNorm = h.norm(1.0);

    for (size_t i = 0; i < n; ++i) {
        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % n];

        geom::Point x = v1.subtract(v0);
        double xLen = x.length();
        if (xLen < 1e-10) continue;

        auto t = geom::GeomUtils::intersectLines(
            p.x, p.y, k.x, k.y,
            v0.x, v0.y, x.x, x.y
        );

        if (t.has_value() && t->y > 0.0 && t->y < 1.0) {
            geom::Point xNorm = x.scale(1.0 / xLen);
            double dotLong = std::abs(h.x * xNorm.x + h.y * xNorm.y);

            if (dotLong > bestDot) {
                bestDot = dotLong;
                d = static_cast<int>(i);
                double ratio = t->y;
                f = geom::Point(v0.x + x.x * ratio, v0.y + x.y * ratio);
                g = xNorm;
            }
        }
    }

    if (d == -1) {
        return makeCut(shape, attempt + 1);
    }

    // Turn g perpendicular (MFCG line 19551)
    g = geom::Point(-g.y, g.x);

    // Find second intersection along perpendicular direction (MFCG lines 19552-19556)
    double hDist = std::numeric_limits<double>::infinity();
    geom::Point pEdge;  // Edge direction at second intersection
    int kEdge = -1;     // Edge index for second intersection

    for (size_t i = 0; i < n; ++i) {
        if (static_cast<int>(i) == d) continue;

        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % n];

        geom::Point x = v1.subtract(v0);
        if (x.length() < 1e-10) continue;

        auto t = geom::GeomUtils::intersectLines(
            f.x, f.y, g.x, g.y,
            v0.x, v0.y, x.x, x.y
        );

        if (t.has_value() && t->x > 0 && t->x < hDist && t->y > 0 && t->y < 1) {
            hDist = t->x;
            pEdge = x;
            kEdge = static_cast<int>(i);
        }
    }

    if (kEdge == -1) {
        return makeCut(shape, attempt + 1);
    }

    // Check perpendicularity (MFCG lines 19558-19559)
    // D = (cross^2) / (|g|^2 * |p|^2)
    double cross = g.x * pEdge.y - g.y * pEdge.x;
    double gLenSq = g.x * g.x + g.y * g.y;
    double pLenSq = pEdge.x * pEdge.x + pEdge.y * pEdge.y;
    double D = (cross * cross) / (gLenSq * pLenSq);

    // Perpendicular case (MFCG lines 19560-19568)
    if (D > 0.99) {
        geom::Point m(f.x + g.x * hDist, f.y + g.y * hDist);
        std::vector<geom::Point> cutLine = {f, m};

        auto halves = split(shape, d, kEdge, cutLine);

        if (halves.size() == 2) {
            double area1 = std::abs(halves[0].square());
            double area2 = std::abs(halves[1].square());
            double ratio = std::max(area1 / area2, area2 / area1);

            if (ratio < 2.0 * variance) {
                cuts.push_back(cutLine);
                return applyGap(halves, cutLine);
            }
        }
        // Fall through to try angled case or retry
    }

    // Angled case with minOffset (MFCG lines 19570-19609)
    double m = minOffset / hDist;
    if (m > 0.5) {
        m = 0.5;
    } else {
        double rand3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                        utils::Random::floatVal()) / 3.0;
        m = m + (1.0 - 2.0 * m) * rand3;
    }

    double nDist = hDist * m;
    geom::Point pMid(f.x + g.x * nDist, f.y + g.y * nDist);

    // Search for third point q using perpendicular to each candidate edge
    // (MFCG lines 19578-19589)
    int kThird = -1;
    geom::Point q;
    double bestW = -std::numeric_limits<double>::infinity();

    for (size_t i = 0; i < n; ++i) {
        if (static_cast<int>(i) == d) continue;

        const geom::Point& v0 = shape[i];
        const geom::Point& v1 = shape[(i + 1) % n];

        geom::Point x = v1.subtract(v0);
        double xLen = x.length();
        if (xLen < 1e-10) continue;

        // Use perpendicular to edge direction
        geom::Point perp(x.y, -x.x);

        auto t = geom::GeomUtils::intersectLines(
            pMid.x, pMid.y, perp.x, perp.y,
            v0.x, v0.y, x.x, x.y
        );

        if (t.has_value() && t->x > 0 && t->y > 0 && t->y < 1) {
            // Calculate cross product alignment (MFCG line 19580)
            double w = (g.x * x.y - g.y * x.x) / xLen;

            if (w > bestW) {
                // Verify the ray doesn't intersect other edges (MFCG lines 19581-19588)
                bool valid = true;
                for (size_t j = 0; j < n; ++j) {
                    if (j == i || static_cast<int>(j) == d) continue;

                    const geom::Point& ev0 = shape[j];
                    const geom::Point& ev1 = shape[(j + 1) % n];
                    geom::Point ey = ev1.subtract(ev0);
                    if (ey.length() < 1e-10) continue;

                    auto check = geom::GeomUtils::intersectLines(
                        pMid.x, pMid.y, perp.x, perp.y,
                        ev0.x, ev0.y, ey.x, ey.y
                    );

                    if (check.has_value() &&
                        check->x >= 0 && check->x <= t->x &&
                        check->y >= 0 && check->y <= 1) {
                        valid = false;
                        break;
                    }
                }

                if (valid) {
                    bestW = w;
                    kThird = static_cast<int>(i);
                    double ratio = t->y;
                    q = geom::Point(v0.x + x.x * ratio, v0.y + x.y * ratio);
                }
            }
        }
    }

    if (kThird != -1) {
        // Create 3-point cut line and process it
        std::vector<geom::Point> cutLine = {f, pMid, q};

        // Apply processCut (detectStraight by default)
        std::vector<geom::Point> processedCut;
        if (processCut) {
            processedCut = processCut(cutLine);
        } else {
            processedCut = detectStraight(cutLine);
        }

        // Validate middle points are inside polygon (MFCG lines 19594-19597)
        bool valid = true;
        for (size_t i = 1; i + 1 < processedCut.size(); ++i) {
            if (!shape.contains(processedCut[i])) {
                processedCut = cutLine;  // Revert to original
                break;
            }
        }

        auto halves = split(shape, d, kThird, processedCut);

        if (halves.size() == 2) {
            double area1 = std::abs(halves[0].square());
            double area2 = std::abs(halves[1].square());
            double ratio = std::max(area1 / area2, area2 / area1);

            if (ratio <= 2.0 * variance) {
                cuts.push_back(processedCut);
                return applyGap(halves, processedCut);
            }
        }
    }

    // Failed to make a cut, retry with different angle
    return makeCut(shape, attempt + 1);
}

std::vector<geom::Polygon> Bisector::applyGap(
    const std::vector<geom::Polygon>& halves,
    const std::vector<geom::Point>& cutLine
) {
    // Faithful to MFCG: uses PolyCreate.stripe + PolyBool.and(half, revert(stripe))
    // Creates a stripe polygon along the cut line, then subtracts it from each half

    if (!getGap || halves.size() < 2 || cutLine.size() < 2) {
        return halves;
    }

    double gap = getGap(cutLine);
    if (gap <= 0) {
        return halves;
    }

    // Create stripe polygon along the cut line (MFCG: PolyCreate.stripe)
    std::vector<geom::Point> stripePoly = geom::GeomUtils::stripe(cutLine, gap, 1.0);

    if (stripePoly.size() < 3) {
        return halves;
    }

    // Reverse the stripe for subtraction (MFCG: revert(stripe))
    std::vector<geom::Point> stripeReversed = geom::GeomUtils::reverse(stripePoly);

    std::vector<geom::Polygon> result;

    for (const auto& half : halves) {
        // Get polygon vertices
        std::vector<geom::Point> halfPts = half.vertexValues();

        if (halfPts.size() < 3) {
            result.push_back(half);
            continue;
        }

        // Boolean AND with reversed stripe (MFCG: PolyBool.and(half, revert(stripe)))
        // This effectively subtracts the stripe from the polygon
        std::vector<geom::Point> clipped = geom::GeomUtils::polygonIntersection(
            halfPts, stripeReversed, true  // subtract = true
        );

        if (clipped.size() >= 3) {
            result.push_back(geom::Polygon(clipped));
        } else {
            // Fallback: if boolean operation fails, try simple shrink approach
            // This can happen with complex polygon shapes
            result.push_back(half);
        }
    }

    return result;
}

std::vector<geom::Polygon> Bisector::split(
    const geom::Polygon& shape,
    int edge1,
    int edge2,
    const std::vector<geom::Point>& cutLine
) {
    // Faithful port of MFCG split (lines 19620-19646)
    // Algorithm: insert cut points into vertex array, then slice

    if (cutLine.size() < 2 || edge1 < 0 || edge2 < 0) {
        return {shape};
    }

    size_t n = shape.length();
    if (n < 3) {
        return {shape};
    }

    // Create mutable copy of vertices (like MFCG's a.slice())
    std::vector<geom::Point> a;
    for (size_t i = 0; i < n; ++i) {
        a.push_back(shape[i]);
    }

    int b = edge1;  // First intersection edge index
    int c = edge2;  // Second intersection edge index

    // Save vertex at c before modifications (MFCG line 19621: var f = a[c])
    geom::Point savedC = a[c];

    geom::Point h = cutLine.front();  // First cut point

    // Insert first cut point after edge b if not already equal (MFCG lines 19622-19623)
    // MFCG: a[b] != h && (b < c && ++c, a.splice(++b, 0, h))
    if (!(a[b].x == h.x && a[b].y == h.y)) {
        if (b < c) ++c;  // Adjust c if inserting before it
        ++b;
        a.insert(a.begin() + b, h);
    }

    h = cutLine.back();  // Last cut point

    // Insert last cut point after edge c if not already equal (MFCG lines 19624-19625)
    // MFCG: f != h && (c < b && ++b, a.splice(++c, 0, h))
    if (!(savedC.x == h.x && savedC.y == h.y)) {
        if (c < b) ++b;  // Adjust b if inserting before it
        ++c;
        a.insert(a.begin() + c, h);
    }

    std::vector<geom::Point> half1Pts;
    std::vector<geom::Point> half2Pts;

    if (b < c) {
        // MFCG lines 19626-19636
        // Half 1: a.slice(b + 1, c) + reversed cut line
        for (int i = b + 1; i < c; ++i) {
            half1Pts.push_back(a[i]);
        }
        // Add reversed cut line
        for (int i = static_cast<int>(cutLine.size()) - 1; i >= 0; --i) {
            half1Pts.push_back(cutLine[i]);
        }

        // Half 2: a.slice(c + 1) + a.slice(0, b)
        for (size_t i = c + 1; i < a.size(); ++i) {
            half2Pts.push_back(a[i]);
        }
        for (int i = 0; i < b; ++i) {
            half2Pts.push_back(a[i]);
        }
    } else {
        // MFCG lines 19637-19643
        // Half 1: a.slice(b + 1) + a.slice(0, c) + reversed cut line
        for (size_t i = b + 1; i < a.size(); ++i) {
            half1Pts.push_back(a[i]);
        }
        for (int i = 0; i < c; ++i) {
            half1Pts.push_back(a[i]);
        }
        // Add reversed cut line
        for (int i = static_cast<int>(cutLine.size()) - 1; i >= 0; --i) {
            half1Pts.push_back(cutLine[i]);
        }

        // Half 2: a.slice(c + 1, b)
        for (int i = c + 1; i < b; ++i) {
            half2Pts.push_back(a[i]);
        }
    }

    // Add cut line to half 2 (MFCG line 19645)
    for (const auto& pt : cutLine) {
        half2Pts.push_back(pt);
    }

    // Validate halves have enough vertices
    if (half1Pts.size() < 3 || half2Pts.size() < 3) {
        return {shape};
    }

    geom::Polygon half1(half1Pts);
    geom::Polygon half2(half2Pts);

    return {half1, half2};
}

std::vector<geom::Point> Bisector::detectStraight(const std::vector<geom::Point>& pts) {
    // Faithful to MFCG detectStraight (lines 19648-19655)
    // If minTurnOffset > 0 and we have 3 points, check if middle point is close to the line
    if (minTurnOffset > 0 && pts.size() >= 3) {
        const geom::Point& p0 = pts[0];
        const geom::Point& p2 = pts[2];

        // Calculate triangle area using the 3 points
        double area = std::abs(geom::GeomUtils::triangleArea(pts[0], pts[1], pts[2]));
        double dist = geom::Point::distance(p0, p2);

        // If area/distance < minTurnOffset, simplify to 2 points
        if (dist > 0.001 && area / dist < minTurnOffset) {
            return {p0, p2};
        }
    }

    return pts;
}

} // namespace building
} // namespace town_generator
