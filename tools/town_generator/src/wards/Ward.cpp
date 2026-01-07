#include "town_generator/wards/Ward.h"
#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Cutter.h"
#include "town_generator/building/Block.h"
#include "town_generator/building/Bisector.h"
#include "town_generator/geom/GeomUtils.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace town_generator {
namespace wards {

// Helper function to check if a patch edge overlaps with a road segment
// Uses geometric proximity rather than exact vertex matching
static bool edgeOverlapsRoadSegment(
    const geom::Point& e0, const geom::Point& e1,  // Cell edge
    const geom::Point& r0, const geom::Point& r1,  // Road segment
    double tolerance = 0.5  // Distance tolerance for overlap
) {
    // Calculate edge and road vectors
    geom::Point edgeVec(e1.x - e0.x, e1.y - e0.y);
    geom::Point roadVec(r1.x - r0.x, r1.y - r0.y);

    double edgeLen = std::sqrt(edgeVec.x * edgeVec.x + edgeVec.y * edgeVec.y);
    double roadLen = std::sqrt(roadVec.x * roadVec.x + roadVec.y * roadVec.y);

    if (edgeLen < 0.01 || roadLen < 0.01) return false;

    // Normalize
    edgeVec.x /= edgeLen;
    edgeVec.y /= edgeLen;
    roadVec.x /= roadLen;
    roadVec.y /= roadLen;

    // Check if edges are roughly parallel (dot product close to +/-1)
    double dot = edgeVec.x * roadVec.x + edgeVec.y * roadVec.y;
    if (std::abs(dot) < 0.9) return false;  // Not parallel enough

    // Check distance from edge midpoint to road line
    geom::Point edgeMid((e0.x + e1.x) / 2, (e0.y + e1.y) / 2);
    double dist = geom::GeomUtils::distance2line(r0.x, r0.y, roadVec.x, roadVec.y, edgeMid.x, edgeMid.y);
    if (dist > tolerance) return false;  // Too far from road

    // Check if projections overlap
    // Project edge endpoints onto road line
    auto projectOntoRoad = [&](const geom::Point& p) -> double {
        return (p.x - r0.x) * roadVec.x + (p.y - r0.y) * roadVec.y;
    };

    double proj0 = projectOntoRoad(e0);
    double proj1 = projectOntoRoad(e1);
    if (proj0 > proj1) std::swap(proj0, proj1);

    // Road segment spans [0, roadLen]
    // Check for overlap
    double overlapStart = std::max(proj0, 0.0);
    double overlapEnd = std::min(proj1, roadLen);

    // Need significant overlap (at least 20% of edge length or 1 unit)
    double minOverlap = std::min(edgeLen * 0.2, 1.0);
    return (overlapEnd - overlapStart) > minOverlap;
}

// Check if a patch edge is on any road in the given street list
static bool isEdgeOnRoad(
    const geom::Point& v0, const geom::Point& v1,
    const std::vector<std::vector<geom::PointPtr>>& roads
) {
    for (const auto& road : roads) {
        if (road.size() < 2) continue;
        for (size_t j = 0; j + 1 < road.size(); ++j) {
            // First check exact match (fast path)
            if ((*road[j] == v0 && *road[j + 1] == v1) ||
                (*road[j] == v1 && *road[j + 1] == v0)) {
                return true;
            }
            // Then check geometric overlap (for edges that don't exactly match)
            if (edgeOverlapsRoadSegment(v0, v1, *road[j], *road[j + 1])) {
                return true;
            }
        }
    }
    return false;
}

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

        // Check if edge is on a canal - use canal width / 2 + ALLEY for gap
        // (faithful to mfcg.js getAvailable() which handles CANAL edge type)
        for (const auto& canal : model->canals) {
            if (canal->containsEdge(v0, v1)) {
                double canalInset = canal->width / 2.0 + ALLEY;
                insetDistances[i] = std::max(insetDistances[i], canalInset);
                break;
            }
        }

        // Check if edge is on main artery using improved geometric detection
        if (isEdgeOnRoad(v0, v1, model->arteries)) {
            insetDistances[i] = MAIN_STREET / 2;
            continue;
        }

        // Also check streets and roads (secondary roads get smaller inset)
        if (isEdgeOnRoad(v0, v1, model->streets) || isEdgeOnRoad(v0, v1, model->roads)) {
            insetDistances[i] = std::max(insetDistances[i], REGULAR_STREET / 2);
        }
    }

    // Per-vertex exclusion zones for canals and walls
    // (faithful to mfcg.js getAvailable lines 12326-12381)
    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v = patch->shape[i];
        double maxExclusion = 0.0;

        // Check tower radius from all walls (mfcg.js lines 12334-12337)
        if (model->wall) {
            double towerRadius = model->wall->getTowerRadius(v);
            if (towerRadius > 0) {
                maxExclusion = std::max(maxExclusion, towerRadius + ALLEY);
            }
        }
        if (model->citadel) {
            double towerRadius = model->citadel->getTowerRadius(v);
            if (towerRadius > 0) {
                maxExclusion = std::max(maxExclusion, towerRadius + ALLEY);
            }
        }

        // Check canal width at vertex (mfcg.js lines 12339-12344, 12353-12358)
        for (const auto& canal : model->canals) {
            double canalWidth = canal->getWidthAtVertex(v);
            if (canalWidth > 0) {
                // For vertices on the canal, use canal width / 2 + ALLEY
                double canalInset = canalWidth / 2.0 + ALLEY;
                maxExclusion = std::max(maxExclusion, canalInset);
            }
        }

        // Apply exclusion to both adjacent edges
        if (maxExclusion > 0) {
            insetDistances[i] = std::max(insetDistances[i], maxExclusion);
            size_t prevIdx = (i + len - 1) % len;
            insetDistances[prevIdx] = std::max(insetDistances[prevIdx], maxExclusion);
        }
    }

    // Cross-ward boundary check: if neighboring patch has different ward type,
    // apply additional buffer to prevent building overlaps
    for (size_t i = 0; i < len; ++i) {
        const geom::Point& v0 = patch->shape[i];
        const geom::Point& v1 = patch->shape[(i + 1) % len];

        // Find neighbor that shares this edge
        for (const auto* neighbor : patch->neighbors) {
            if (!neighbor || !neighbor->ward) continue;

            // Check if neighbor shares this edge
            bool sharesEdge = false;
            for (size_t j = 0; j < neighbor->shape.length(); ++j) {
                const geom::Point& n0 = neighbor->shape[j];
                const geom::Point& n1 = neighbor->shape[(j + 1) % neighbor->shape.length()];
                if ((n0 == v0 && n1 == v1) || (n0 == v1 && n1 == v0)) {
                    sharesEdge = true;
                    break;
                }
            }

            if (sharesEdge) {
                // If different ward type, apply additional buffer
                if (neighbor->ward->getName() != getName()) {
                    // Both wards will apply this buffer independently, creating proper separation
                    insetDistances[i] = std::max(insetDistances[i], REGULAR_STREET / 2);
                }
                break;  // Found the neighbor for this edge
            }
        }
    }

    return insetDistances;
}

void Ward::createGeometry() {
    // Base Ward creates no buildings (faithful to Haxe)
    // Subclasses override to create appropriate geometry
}

void Ward::filterOutskirts() {
    // Faithful to mfcg.js Ward.filter (lines 889-958)
    // Uses edge-type-based density at vertices, interpolated to building centers
    if (!patch || !model) return;

    size_t numVerts = patch->shape.length();
    if (numVerts < 3) return;

    // Calculate density for each vertex based on:
    // 1. If "inner" vertex (all adjacent cells are withinCity or waterbody), density = 1.0
    // 2. Otherwise, max density from adjacent edges:
    //    - ROAD edges: 0.3
    //    - WALL edges: 0.5
    //    - CANAL edges: 0.1
    //    - Other edges: 0.0
    std::vector<double> vertexDensity(numVerts, 0.0);

    for (size_t i = 0; i < numVerts; ++i) {
        const geom::Point& v = patch->shape[i];

        // Check if this is an "inner" vertex (all adjacent cells are withinCity or waterbody)
        auto adjacentPatches = model->cellsByVertex(v);
        bool isInner = true;
        for (auto* p : adjacentPatches) {
            if (!p->withinCity && !p->waterbody) {
                isInner = false;
                break;
            }
        }

        if (isInner) {
            vertexDensity[i] = 1.0;
            continue;
        }

        // Not inner - calculate density from adjacent edges
        // Previous edge (i-1 to i) and current edge (i to i+1)
        size_t prevIdx = (i + numVerts - 1) % numVerts;
        const geom::Point& vPrev = patch->shape[prevIdx];
        const geom::Point& vNext = patch->shape[(i + 1) % numVerts];

        double maxDensity = 0.0;

        // Check previous edge type
        double prevDensity = 0.0;
        if (model->wall && model->wall->bordersBy(patch, vPrev, v)) {
            prevDensity = 0.5;  // WALL
        } else if (isEdgeOnRoad(vPrev, v, model->arteries) ||
                   isEdgeOnRoad(vPrev, v, model->streets) ||
                   isEdgeOnRoad(vPrev, v, model->roads)) {
            prevDensity = 0.3;  // ROAD
        }
        for (const auto& canal : model->canals) {
            if (canal->containsEdge(vPrev, v)) {
                prevDensity = std::max(prevDensity, 0.1);  // CANAL
                break;
            }
        }
        maxDensity = std::max(maxDensity, prevDensity);

        // Check current edge type
        double currDensity = 0.0;
        if (model->wall && model->wall->bordersBy(patch, v, vNext)) {
            currDensity = 0.5;  // WALL
        } else if (isEdgeOnRoad(v, vNext, model->arteries) ||
                   isEdgeOnRoad(v, vNext, model->streets) ||
                   isEdgeOnRoad(v, vNext, model->roads)) {
            currDensity = 0.3;  // ROAD
        }
        for (const auto& canal : model->canals) {
            if (canal->containsEdge(v, vNext)) {
                currDensity = std::max(currDensity, 0.1);  // CANAL
                break;
            }
        }
        maxDensity = std::max(maxDensity, currDensity);

        vertexDensity[i] = maxDensity;
    }

    // MFCG threshold calculation: density * sqrt(numFaces) - (0.5 * sqrt(numFaces) - 0.5)
    // Where numFaces is the number of buildings
    double sqrtFaces = std::sqrt(static_cast<double>(geometry.size()));
    double offset = 0.5 * sqrtFaces - 0.5;

    // Filter buildings based on interpolated density at center
    auto it = geometry.begin();
    while (it != geometry.end()) {
        // Get building center
        geom::Point center = it->center();

        // Interpolate density at center using barycentric coordinates
        auto weights = patch->shape.interpolate(center);
        double interpolatedDensity = 0.0;
        for (size_t j = 0; j < weights.size() && j < vertexDensity.size(); ++j) {
            interpolatedDensity += vertexDensity[j] * weights[j];
        }

        // Calculate threshold: interpolatedDensity * sqrtFaces - offset
        double threshold = interpolatedDensity * sqrtFaces - offset;

        // Keep building if random < threshold (higher density = more likely to keep)
        if (utils::Random::floatVal() < threshold) {
            ++it;
        } else {
            it = geometry.erase(it);
        }
    }
}

void Ward::filterInner(const geom::Polygon& blockShape) {
    // Based on mfcg.js filterInner:
    // A building is considered "inner" (courtyard) if NONE of its vertices
    // lie on any edge of the block shape.
    // Only keep buildings that have at least one vertex touching the perimeter.

    if (blockShape.length() < 3) return;

    size_t blockLen = blockShape.length();

    auto it = geometry.begin();
    while (it != geometry.end()) {
        bool touchesPerimeter = false;

        // Check each vertex of the building
        for (size_t vi = 0; vi < it->length() && !touchesPerimeter; ++vi) {
            const geom::Point& v = (*it)[vi];

            // Check against each edge of the block shape
            geom::Point prevPoint = blockShape[blockLen - 1];
            for (size_t ei = 0; ei < blockLen && !touchesPerimeter; ++ei) {
                const geom::Point& currPoint = blockShape[ei];

                // Check if vertex lies on this edge
                double edgeDx = currPoint.x - prevPoint.x;
                double edgeDy = currPoint.y - prevPoint.y;
                double edgeLenSq = edgeDx * edgeDx + edgeDy * edgeDy;

                if (edgeLenSq > 1e-9) {
                    // Project point onto edge line
                    double t = ((v.x - prevPoint.x) * edgeDx + (v.y - prevPoint.y) * edgeDy) / edgeLenSq;

                    // Check if projection is within edge segment
                    if (t >= 0.0 && t <= 1.0) {
                        // Calculate distance from point to edge
                        geom::Point projected(
                            prevPoint.x + t * edgeDx,
                            prevPoint.y + t * edgeDy
                        );
                        double distSq = (v.x - projected.x) * (v.x - projected.x) +
                                       (v.y - projected.y) * (v.y - projected.y);

                        // If very close to edge, consider it touching
                        // Use 0.01 (squared distance) = ~0.1 unit tolerance
                        // This accounts for floating point drift in recursive bisection
                        if (distSq < 0.01) {
                            touchesPerimeter = true;
                        }
                    }
                }

                prevPoint = currPoint;
            }
        }

        // Keep only buildings that touch the perimeter
        if (touchesPerimeter) {
            ++it;
        } else {
            it = geometry.erase(it);
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

bool Ward::isRectangle(const geom::Polygon& poly) const {
    // Faithful to mfcg.js isRectangle: 4 vertices and area/obbArea > 0.75
    if (poly.length() != 4) return false;

    double area = std::abs(poly.square());
    auto obb = poly.orientedBoundingBox();
    if (obb.size() < 4) return false;

    // OBB area
    geom::Point edge01 = obb[1].subtract(obb[0]);
    geom::Point edge12 = obb[2].subtract(obb[1]);
    double obbArea = edge01.length() * edge12.length();

    if (obbArea < 0.001) return false;

    return (area / obbArea) > 0.75;
}

void Ward::addBuildingLot(const geom::Polygon& lot, double minSq) {
    // Faithful to mfcg.js createLots filtering and createRects
    // Filter conditions from createLots (lines 12289-12296):
    // - Must have >= 4 vertices
    // - Area must be >= minSq/4
    // - OBB dimensions must both be >= 1.2
    // - Area/OBBArea ratio must be > 0.5

    size_t numVerts = lot.length();

    // Must have at least 3 vertices to form a shape
    if (numVerts < 3) return;

    double area = std::abs(lot.square());
    double minArea = minSq / 4.0;

    // Area check
    if (area < minArea) return;

    // For triangles (< 4 verts), check proportions and potentially convert
    if (numVerts < 4) {
        // Triangles are filtered in mfcg.js
        return;
    }

    // Get OBB for dimension checks
    auto obb = lot.orientedBoundingBox();
    if (obb.size() < 4) {
        // Can't compute OBB, reject
        return;
    }

    geom::Point edge01 = obb[1].subtract(obb[0]);
    geom::Point edge12 = obb[2].subtract(obb[1]);
    double len01 = edge01.length();
    double len12 = edge12.length();
    double obbArea = len01 * len12;

    // Minimum dimension check (1.2 in mfcg.js)
    if (len01 < 1.2 || len12 < 1.2) return;

    // Shape ratio check
    if (obbArea > 0.001 && (area / obbArea) < 0.5) return;

    // Now convert to rectangle if needed (createRects logic)
    geom::Polygon building;

    if (isRectangle(lot)) {
        // Already rectangular enough
        building = lot;
    } else {
        // Use LIR/LIRA to find largest inscribed rectangle
        std::vector<geom::Point> pts;
        for (size_t i = 0; i < lot.length(); ++i) {
            pts.push_back(lot[i]);
        }

        // Try LIRA (finds best edge to align rectangle with)
        std::vector<geom::Point> rect = geom::GeomUtils::lira(pts);

        // Validate the rectangle has reasonable dimensions
        if (rect.size() >= 4) {
            double minDim = std::sqrt(area) / 2.0;
            minDim = std::max(1.2, minDim);

            double rectLen01 = geom::Point::distance(rect[0], rect[1]);
            double rectLen12 = geom::Point::distance(rect[1], rect[2]);

            if (rectLen01 >= minDim && rectLen12 >= minDim) {
                building = geom::Polygon(rect);
            } else {
                // Rectangle too small, use original lot
                building = lot;
            }
        } else {
            building = lot;
        }
    }

    // Simplify polygons with more than 4 vertices (mfcg.js lines 12194-12197)
    if (building.length() > 4) {
        // Simplify to 4 vertices by removing shortest edges
        std::vector<geom::Point> pts;
        for (size_t i = 0; i < building.length(); ++i) {
            pts.push_back(building[i]);
        }

        while (pts.size() > 4) {
            // Find shortest edge
            size_t shortestIdx = 0;
            double shortestLen = std::numeric_limits<double>::max();
            for (size_t i = 0; i < pts.size(); ++i) {
                size_t next = (i + 1) % pts.size();
                double len = geom::Point::distance(pts[i], pts[next]);
                if (len < shortestLen) {
                    shortestLen = len;
                    shortestIdx = i;
                }
            }

            // Remove the vertex that creates the shortest edge by merging
            size_t prev = (shortestIdx + pts.size() - 1) % pts.size();
            size_t next = (shortestIdx + 1) % pts.size();

            // Keep the vertex that maintains more area
            pts.erase(pts.begin() + shortestIdx);
        }

        building = geom::Polygon(pts);
    }

    geometry.push_back(building);
}


std::vector<geom::Point> Ward::semiSmooth(
    const geom::Point& p0,
    const geom::Point& p1,
    const geom::Point& p2,
    double minFront
) {
    // Faithful to mfcg.js semiSmooth function (using Qe.getCircle, Qe.getArc)
    // Smooths a corner (p0, p1, p2) into an arc if appropriate

    double dist02 = geom::Point::distance(p0, p2);
    double triArea = std::abs(geom::GeomUtils::triangleArea(p0, p1, p2));

    // Skip if too thin
    if (triArea / dist02 < 1.0 || triArea / (dist02 * dist02) < 0.01) {
        return {p0, p2};
    }

    geom::Point v01 = p1.subtract(p0);
    geom::Point v12 = p2.subtract(p1);
    double len01 = v01.length();
    double len12 = v12.length();
    double minLen = std::min(len01, len12);

    // Calculate angle-based probability
    double dot = (v01.x * v12.x + v01.y * v12.y) / (len01 * len12);
    double angleProb = (1.0 - dot) / 2.0;

    // Random decision whether to smooth
    if (utils::Random::floatVal() < angleProb) {
        return {p0, p1, p2};  // Keep original corner
    }

    // Distance-based probability
    double distProb = minFront / minLen;
    if (utils::Random::floatVal() < distProb) {
        return {p0, p1, p2};  // Keep original corner
    }

    // Create arc using getCircle and getArc (faithful to mfcg.js)
    std::vector<geom::Point> result;
    result.push_back(p0);

    // Determine arc endpoints based on shorter segment
    geom::Point arcStart, arcEnd;
    geom::Point dir1, dir2;

    if (len01 < len12) {
        // Shorter segment is p0-p1: arc from p0 to point on p1-p2
        double t = len01 / len12;
        arcStart = p0;
        arcEnd = geom::Point(p1.x + v12.x * t, p1.y + v12.y * t);
        dir1 = v01.norm(1.0);
        dir2 = v12.norm(1.0);
    } else {
        // Shorter segment is p1-p2: arc from point on p0-p1 to p2
        double t = len12 / len01;
        arcStart = geom::Point(p1.x - v01.x * t, p1.y - v01.y * t);
        arcEnd = p2;
        dir1 = v01.norm(1.0);
        dir2 = v12.norm(1.0);
    }

    // Get circle passing through arc endpoints
    auto circle = geom::GeomUtils::getCircle(arcStart, dir1, arcEnd, dir2);

    if (circle.r > 0.001) {
        // Calculate angles
        geom::Point toStart = arcStart.subtract(circle.c);
        geom::Point toEnd = arcEnd.subtract(circle.c);
        double startAngle = std::atan2(toStart.y, toStart.x);
        double endAngle = std::atan2(toEnd.y, toEnd.x);

        // Get arc points (4 segments for smooth curve)
        auto arcPoints = geom::GeomUtils::getArc(circle, startAngle, endAngle, 4);

        if (!arcPoints.empty()) {
            // Add arc points (skip first as it's arcStart which is close to p0)
            for (size_t i = 1; i < arcPoints.size(); ++i) {
                result.push_back(arcPoints[i]);
            }
        } else {
            // Arc failed, use simple midpoint
            result.push_back(geom::GeomUtils::lerp(arcStart, arcEnd));
        }
    } else {
        // Circle failed, use simple approach
        result.push_back(geom::GeomUtils::lerp(arcStart, arcEnd));
    }

    result.push_back(p2);
    return result;
}

void Ward::createChurch(const geom::Polygon& block) {
    // Faithful to mfcg.js createChurch
    // Creates a church building in a medium-sized block

    if (block.length() < 3) return;

    // Find oriented bounding box
    auto obb = block.orientedBoundingBox();
    if (obb.size() < 4) {
        church = block;
        return;
    }

    // Find longer axis
    geom::Point v01 = obb[1].subtract(obb[0]);
    geom::Point v12 = obb[2].subtract(obb[1]);
    geom::Point axis = (v01.length() > v12.length()) ? v01 : v12;

    // Calculate cut position
    double axisLen = axis.length();
    double cutRatio = 0.5;
    if (axisLen > 0.01) {
        double minRatio = patch ? (std::sqrt(15.0) / axisLen) : 0.3;  // minFront approximation
        if (minRatio > 0.5) {
            minRatio = 0.5;
        }
        // Average of 3 randoms for normal distribution approximation
        double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() +
                         utils::Random::floatVal()) / 3.0;
        cutRatio = minRatio + (1.0 - 2.0 * minRatio) * normal3;
    }

    // Cut the block
    geom::Point cutStart(obb[1].x + axis.x * cutRatio, obb[1].y + axis.y * cutRatio);
    geom::Point cutDir(-axis.y, axis.x);  // Perpendicular
    geom::Point cutEnd = cutStart.add(cutDir);

    auto halves = block.cut(cutStart, cutEnd);

    // Pick the more compact half as the church
    if (halves.empty()) {
        church = block;
    } else {
        double maxCompact = -1;
        for (const auto& half : halves) {
            double compact = half.compactness();
            if (compact > maxCompact) {
                maxCompact = compact;
                church = half;
            }
        }
    }
}

void Ward::createBlock(const geom::Polygon& shape, bool isSmall) {
    // Faithful to mfcg.js createBlock (lines 13167-13171)
    // Creates a Block object and stores it

    // Create WardGroup if needed (for Block to get parameters)
    // For now, create Block directly
    building::Block* block = new building::Block(shape, nullptr);

    if (isSmall) {
        // Small block: whole shape is a single lot
        block->lots = {shape};
    } else {
        // Normal block: use TwistedBlock to create lots
        AlleyParams params = AlleyParams::createUrban();
        block->lots = building::TwistedBlock::createLots(block, params);
    }

    // Filter inner lots
    block->filterInner();

    // Create rectangles from lots
    block->createRects();

    // Add block's rects to geometry
    for (const auto& rect : block->rects) {
        if (rect.length() >= 3) {
            geometry.push_back(rect);
        }
    }

    blocks.push_back(block);
}

void Ward::createAlleys(const geom::Polygon& shape, const AlleyParams& params) {
    // Faithful to mfcg.js createAlleys (lines 13124-13145)
    // Uses Bisector for proper partitioning

    // Create bisector with minArea = minSq * blockSize, variance = 16 * gridChaos
    double minArea = params.minSq * params.blockSize;
    double variance = 16.0 * params.gridChaos;

    building::Bisector bisector(shape, minArea, variance);

    // Set gap callback (returns ALLEY)
    bisector.getGap = [](const std::vector<geom::Point>&) { return ALLEY; };

    // Set processCut to semiSmooth (optional, can be nullptr)
    double minFront = params.minFront;
    bisector.processCut = [minFront](const std::vector<geom::Point>& pts) {
        if (pts.size() >= 3) {
            return Ward::semiSmooth(pts[0], pts[1], pts[2], minFront);
        }
        return pts;
    };

    // For non-urban wards, use isBlockSized check
    if (!urban) {
        bisector.isAtomic = [this, &params](const geom::Polygon& p) {
            return isBlockSized(p, params);
        };
    }

    // Partition
    auto partitions = bisector.partition();

    // Store alley cuts for rendering
    for (const auto& cut : bisector.cuts) {
        alleys.push_back(cut);
    }

    // Process each partition
    for (const auto& partition : partitions) {
        double area = std::abs(partition.square());

        // Calculate threshold with random variation (faithful to mfcg.js line 13138-13139)
        double threshold = params.minSq * std::pow(2.0,
            params.sizeChaos * (2.0 * utils::Random::floatVal() - 1.0));
        double churchThreshold = 4.0 * threshold;

        if (area < threshold) {
            // Small block - create Block with isSmall=true
            createBlock(partition, true);
        } else if (church.empty() && area <= churchThreshold) {
            // Church-sized block - create church
            createChurch(partition);
        } else {
            // Regular block - create Block with isSmall=false
            createBlock(partition, false);
        }
    }
}

bool Ward::isBlockSized(const geom::Polygon& shape, const AlleyParams& params) {
    // Faithful to mfcg.js isBlockSized (line 13131)
    // For non-urban wards, checks if block is at blockSize threshold
    double area = std::abs(shape.square());
    double threshold = params.minSq * params.blockSize;

    // Random variation
    double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                      utils::Random::floatVal() + utils::Random::floatVal()) / 2.0;
    threshold *= std::pow(16.0 * params.gridChaos, std::abs(normal4 - 1.0));

    return area < threshold;
}

} // namespace wards
} // namespace town_generator
