#include "town_generator/geom/GeomUtils.h"
#include <limits>
#include <algorithm>

namespace town_generator {
namespace geom {

std::vector<Point> GeomUtils::rotatePoints(const std::vector<Point>& pts, double angle) {
    std::vector<Point> result;
    result.reserve(pts.size());
    double cosA = std::cos(angle);
    double sinA = std::sin(angle);
    for (const auto& p : pts) {
        result.push_back(Point(p.x * cosA - p.y * sinA, p.x * sinA + p.y * cosA));
    }
    return result;
}

double GeomUtils::polygonArea(const std::vector<Point>& poly) {
    if (poly.size() < 3) return 0;
    double area = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Point& p1 = poly[i];
        const Point& p2 = poly[(i + 1) % poly.size()];
        area += p1.x * p2.y - p2.x * p1.y;
    }
    return std::abs(area * 0.5);
}

std::vector<Point> GeomUtils::lir(const std::vector<Point>& poly, size_t edgeIdx) {
    // Simplified LIR algorithm - finds largest inscribed rectangle aligned to edge
    // This is a simplified version of mfcg.js Gb.lir

    if (poly.size() < 3 || edgeIdx >= poly.size()) {
        return poly;
    }

    size_t n = poly.size();
    size_t nextIdx = (edgeIdx + 1) % n;

    // Get the edge direction
    Point edge = poly[nextIdx].subtract(poly[edgeIdx]);
    double edgeLen = edge.length();
    if (edgeLen < 0.0001) return poly;

    // Compute rotation angle to align edge with x-axis
    double angle = std::atan2(edge.y, edge.x);

    // Rotate all points so edge is horizontal
    std::vector<Point> rotated = rotatePoints(poly, -angle);

    // Find bounding box of rotated polygon
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

    // The edge should be at y = rotated[edgeIdx].y
    double baseY = rotated[edgeIdx].y;
    double baseX1 = rotated[edgeIdx].x;
    double baseX2 = rotated[nextIdx].x;
    if (baseX1 > baseX2) std::swap(baseX1, baseX2);

    // Find rectangle extending from the edge inward
    // Scan through possible heights
    double bestArea = 0;
    double bestLeft = baseX1, bestRight = baseX2, bestTop = baseY, bestBottom = baseY;

    // Simple approach: use edge as base, find max height while staying inside polygon
    // This is a simplification - the full algorithm is more complex

    // Determine which direction is "inside" the polygon
    Point edgeMid((baseX1 + baseX2) / 2, baseY);
    double testOffset = (maxY - minY) * 0.01;

    // Test both directions to find inside
    double insideY = baseY + testOffset;
    if (insideY > maxY || insideY < minY) {
        insideY = baseY - testOffset;
    }

    // Sample heights to find max inscribed rectangle
    int samples = 10;
    for (int s = 1; s <= samples; ++s) {
        double t = static_cast<double>(s) / samples;
        double testY;
        if (insideY > baseY) {
            testY = baseY + t * (maxY - baseY);
        } else {
            testY = baseY - t * (baseY - minY);
        }

        // Find x-bounds at this y level by intersecting with polygon edges
        double leftBound = minX;
        double rightBound = maxX;

        for (size_t i = 0; i < n; ++i) {
            const Point& p1 = rotated[i];
            const Point& p2 = rotated[(i + 1) % n];

            // Check if this edge crosses the y level
            if ((p1.y <= testY && p2.y > testY) || (p2.y <= testY && p1.y > testY)) {
                // Find x at intersection
                double intersectX = p1.x + (testY - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);

                // Determine if this is left or right bound
                if (intersectX < edgeMid.x) {
                    leftBound = std::max(leftBound, intersectX);
                } else {
                    rightBound = std::min(rightBound, intersectX);
                }
            }
        }

        // Clamp to base edge bounds
        leftBound = std::max(leftBound, baseX1);
        rightBound = std::min(rightBound, baseX2);

        double width = rightBound - leftBound;
        double height = std::abs(testY - baseY);
        double area = width * height;

        if (area > bestArea && width > 0 && height > 0) {
            bestArea = area;
            bestLeft = leftBound;
            bestRight = rightBound;
            if (insideY > baseY) {
                bestBottom = baseY;
                bestTop = testY;
            } else {
                bestTop = baseY;
                bestBottom = testY;
            }
        }
    }

    // Create rectangle corners (in rotated space)
    std::vector<Point> rectRotated = {
        Point(bestLeft, bestBottom),
        Point(bestRight, bestBottom),
        Point(bestRight, bestTop),
        Point(bestLeft, bestTop)
    };

    // Rotate back
    return rotatePoints(rectRotated, angle);
}

std::vector<Point> GeomUtils::lira(const std::vector<Point>& poly) {
    // Try lir for each edge and return the one with largest area
    if (poly.size() < 3) return poly;

    double bestArea = -1;
    std::vector<Point> bestRect;

    for (size_t i = 0; i < poly.size(); ++i) {
        std::vector<Point> rect = lir(poly, i);
        double area = polygonArea(rect);
        if (area > bestArea) {
            bestArea = area;
            bestRect = rect;
        }
    }

    return bestRect.empty() ? poly : bestRect;
}

bool GeomUtils::containsPoint(const std::vector<Point>& poly, const Point& p, bool excludeBoundary) {
    // Ray casting algorithm for point-in-polygon test
    if (poly.size() < 3) return false;

    int crossings = 0;
    size_t n = poly.size();

    for (size_t i = 0; i < n; ++i) {
        const Point& p1 = poly[i];
        const Point& p2 = poly[(i + 1) % n];

        // Check if point is on boundary
        if (!excludeBoundary) {
            // Check if point lies on edge
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

std::vector<Point> GeomUtils::polygonIntersection(
    const std::vector<Point>& polyA,
    const std::vector<Point>& polyB,
    bool subtract
) {
    // Sutherland-Hodgman algorithm for polygon clipping
    // This is a simplified version for convex clipping polygons
    // For complex cases, return empty (MFCG uses more sophisticated algorithm)

    if (polyA.size() < 3 || polyB.size() < 3) return {};

    std::vector<Point> output = polyA;

    // Clip against each edge of polyB
    for (size_t i = 0; i < polyB.size(); ++i) {
        if (output.empty()) break;

        std::vector<Point> input = output;
        output.clear();

        const Point& edgeStart = polyB[i];
        const Point& edgeEnd = polyB[(i + 1) % polyB.size()];

        // Edge normal (pointing inward for intersection, outward for subtract)
        Point edgeDir = edgeEnd.subtract(edgeStart);
        Point normal(-edgeDir.y, edgeDir.x);  // Left normal

        if (subtract) {
            normal = Point(edgeDir.y, -edgeDir.x);  // Right normal
        }

        for (size_t j = 0; j < input.size(); ++j) {
            const Point& current = input[j];
            const Point& previous = input[(j + input.size() - 1) % input.size()];

            // Calculate signed distances
            Point toCurr = current.subtract(edgeStart);
            Point toPrev = previous.subtract(edgeStart);
            double distCurr = normal.x * toCurr.x + normal.y * toCurr.y;
            double distPrev = normal.x * toPrev.x + normal.y * toPrev.y;

            bool currInside = distCurr >= 0;
            bool prevInside = distPrev >= 0;

            if (currInside) {
                if (!prevInside) {
                    // Entering - add intersection point
                    double t = distPrev / (distPrev - distCurr);
                    output.push_back(lerp(previous, current, t));
                }
                output.push_back(current);
            } else if (prevInside) {
                // Leaving - add intersection point
                double t = distPrev / (distPrev - distCurr);
                output.push_back(lerp(previous, current, t));
            }
        }
    }

    return output;
}

std::vector<Point> GeomUtils::stripe(
    const std::vector<Point>& line,
    double width,
    double capExtend
) {
    // Faithful to mfcg.js Qd.stripe
    // Creates a polygon representing a stripe along the polyline

    if (line.size() < 2) return {};

    double halfWidth = width / 2.0;
    std::vector<Point> leftSide;
    std::vector<Point> rightSide;

    size_t n = line.size();

    // First point
    Point p0 = line[0];
    Point p1 = line[1];
    Point dir = p1.subtract(p0);
    dir = dir.norm(1.0);

    // Perpendicular
    Point perp(-dir.y * halfWidth, dir.x * halfWidth);

    if (capExtend > 0) {
        // Extend cap
        Point capOffset(dir.x * halfWidth * capExtend, dir.y * halfWidth * capExtend);
        p0 = p0.subtract(capOffset);
    }

    leftSide.push_back(p0.subtract(perp));
    rightSide.push_back(p0.add(perp));

    // Middle points
    for (size_t i = 1; i < n - 1; ++i) {
        Point prev = line[i - 1];
        Point curr = line[i];
        Point next = line[i + 1];

        Point dir1 = curr.subtract(prev);
        Point dir2 = next.subtract(curr);
        dir1 = dir1.norm(1.0);
        dir2 = dir2.norm(1.0);

        // Average direction
        double dot = dir1.x * dir2.x + dir1.y * dir2.y;
        Point avgDir = dir1.add(dir2);
        avgDir = Point(-avgDir.y, avgDir.x);  // Perpendicular

        // Miter factor
        double miter = halfWidth * std::sqrt(2.0 / (1.0 + dot));
        avgDir = avgDir.norm(miter);

        leftSide.push_back(curr.subtract(avgDir));
        rightSide.push_back(curr.add(avgDir));
    }

    // Last point
    Point pn1 = line[n - 2];
    Point pn = line[n - 1];
    dir = pn.subtract(pn1);
    dir = dir.norm(1.0);
    perp = Point(-dir.y * halfWidth, dir.x * halfWidth);

    if (capExtend > 0) {
        Point capOffset(dir.x * halfWidth * capExtend, dir.y * halfWidth * capExtend);
        pn = pn.add(capOffset);
    }

    leftSide.push_back(pn.subtract(perp));
    rightSide.push_back(pn.add(perp));

    // Combine left and reversed right to form polygon
    std::vector<Point> result = leftSide;
    for (auto it = rightSide.rbegin(); it != rightSide.rend(); ++it) {
        result.push_back(*it);
    }

    return result;
}

GeomUtils::Circle GeomUtils::getCircle(
    const Point& p0,
    const Point& dir0,
    const Point& p1,
    const Point& dir1
) {
    // Find circle passing through p0 with tangent dir0 and through p1
    // Based on mfcg.js Qe.getCircle

    Circle result;
    result.c = Point(0, 0);
    result.r = 0;

    // The center lies on perpendicular to dir0 through p0
    // and perpendicular to dir1 through p1
    Point perp0(-dir0.y, dir0.x);
    Point perp1(-dir1.y, dir1.x);

    // Find intersection of the two perpendicular lines
    auto intersection = intersectLines(
        p0.x, p0.y, perp0.x, perp0.y,
        p1.x, p1.y, perp1.x, perp1.y
    );

    if (!intersection) {
        // Parallel - use midpoint
        result.c = lerp(p0, p1);
        result.r = Point::distance(result.c, p0);
    } else {
        double t = intersection->x;
        result.c = Point(p0.x + perp0.x * t, p0.y + perp0.y * t);
        result.r = Point::distance(result.c, p0);
    }

    return result;
}

std::vector<Point> GeomUtils::getArc(
    const Circle& circle,
    double startAngle,
    double endAngle,
    int numSegments
) {
    // Generate arc points
    // Based on mfcg.js Qe.getArc

    if (numSegments < 1) numSegments = 1;
    if (circle.r < 0.001) return {};

    std::vector<Point> result;

    // Normalize angle difference
    double angleDiff = endAngle - startAngle;
    while (angleDiff > M_PI) angleDiff -= 2 * M_PI;
    while (angleDiff < -M_PI) angleDiff += 2 * M_PI;

    // If angle is too small, return null (MFCG returns null for small angles)
    if (std::abs(angleDiff) < 0.01) {
        return {};
    }

    for (int i = 0; i <= numSegments; ++i) {
        double t = static_cast<double>(i) / numSegments;
        double angle = startAngle + angleDiff * t;
        result.push_back(Point(
            circle.c.x + circle.r * std::cos(angle),
            circle.c.y + circle.r * std::sin(angle)
        ));
    }

    return result;
}

std::vector<Point> GeomUtils::translate(const std::vector<Point>& poly, double dx, double dy) {
    std::vector<Point> result;
    result.reserve(poly.size());
    for (const auto& p : poly) {
        result.push_back(Point(p.x + dx, p.y + dy));
    }
    return result;
}

std::vector<Point> GeomUtils::reverse(const std::vector<Point>& poly) {
    std::vector<Point> result(poly.rbegin(), poly.rend());
    return result;
}

std::vector<Point> GeomUtils::shrink(const std::vector<Point>& poly, const std::vector<double>& amounts) {
    // Faithful to mfcg.js gd.shrink
    // Shrinks each edge inward by the specified amount
    // Uses inward normal vectors and computes new vertices at intersections

    if (poly.size() < 3 || amounts.size() != poly.size()) {
        return poly;
    }

    size_t n = poly.size();
    std::vector<Point> result;

    // For each vertex, compute new position based on adjacent edge shrinks
    for (size_t i = 0; i < n; ++i) {
        size_t prevIdx = (i + n - 1) % n;
        size_t nextIdx = (i + 1) % n;

        const Point& prev = poly[prevIdx];
        const Point& curr = poly[i];
        const Point& next = poly[nextIdx];

        // Previous edge: prev -> curr
        Point prevEdge = curr.subtract(prev);
        double prevLen = prevEdge.length();

        // Current edge: curr -> next
        Point currEdge = next.subtract(curr);
        double currLen = currEdge.length();

        if (prevLen < 1e-9 || currLen < 1e-9) {
            result.push_back(curr);
            continue;
        }

        // Inward normals (assuming CCW winding)
        Point prevNorm(-prevEdge.y / prevLen, prevEdge.x / prevLen);
        Point currNorm(-currEdge.y / currLen, currEdge.x / currLen);

        // Offset edges
        double prevAmount = amounts[prevIdx];
        double currAmount = amounts[i];

        Point prevOffsetStart(prev.x + prevNorm.x * prevAmount, prev.y + prevNorm.y * prevAmount);
        Point prevOffsetEnd(curr.x + prevNorm.x * prevAmount, curr.y + prevNorm.y * prevAmount);

        Point currOffsetStart(curr.x + currNorm.x * currAmount, curr.y + currNorm.y * currAmount);
        Point currOffsetEnd(next.x + currNorm.x * currAmount, next.y + currNorm.y * currAmount);

        // Find intersection of offset edges
        auto intersection = intersectLines(
            prevOffsetStart.x, prevOffsetStart.y,
            prevEdge.x, prevEdge.y,
            currOffsetStart.x, currOffsetStart.y,
            currEdge.x, currEdge.y
        );

        if (intersection) {
            double t = intersection->x;
            result.push_back(Point(
                prevOffsetStart.x + prevEdge.x * t,
                prevOffsetStart.y + prevEdge.y * t
            ));
        } else {
            // Parallel edges - use average offset
            result.push_back(Point(
                curr.x + (prevNorm.x * prevAmount + currNorm.x * currAmount) / 2.0,
                curr.y + (prevNorm.y * prevAmount + currNorm.y * currAmount) / 2.0
            ));
        }
    }

    return result;
}

} // namespace geom
} // namespace town_generator
