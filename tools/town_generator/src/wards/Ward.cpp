#include "town_generator/wards/Ward.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Cutter.h"
#include "town_generator/geom/GeomUtils.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace town_generator {
namespace wards {

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

        // Check if edge is on main artery (arteries now use PointPtr)
        for (const auto& artery : model->arteries) {
            if (artery.size() < 2) continue;  // Need at least 2 points to form an edge
            bool found = false;
            for (size_t j = 0; j + 1 < artery.size(); ++j) {
                // Compare dereferenced PointPtrs with Point values
                if ((*artery[j] == v0 && *artery[j + 1] == v1) ||
                    (*artery[j] == v1 && *artery[j + 1] == v0)) {
                    insetDistances[i] = MAIN_STREET / 2;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
    }

    return insetDistances;
}

void Ward::createGeometry() {
    // Base Ward creates no buildings (faithful to Haxe)
    // Subclasses override to create appropriate geometry
}

void Ward::filterOutskirts() {
    if (!patch || !model) return;

    // Simplified filterOutskirts - filter buildings based on distance from patch edges
    // that are on roads or border other city patches.

    // Structure to hold populated edge info
    struct PopulatedEdge {
        double x, y, dx, dy, d;
    };
    std::vector<PopulatedEdge> populatedEdges;

    // Helper to add an edge with a distance factor
    auto addEdge = [&](const geom::Point& v1, const geom::Point& v2, double factor) {
        double dx = v2.x - v1.x;
        double dy = v2.y - v1.y;

        // Find max distance from any vertex to this edge line
        double maxDist = 0;
        for (size_t i = 0; i < patch->shape.length(); ++i) {
            const geom::Point& v = patch->shape[i];
            if (v == v1 || v == v2) continue;
            double dist = geom::GeomUtils::distance2line(v1.x, v1.y, dx, dy, v.x, v.y);
            maxDist = std::max(maxDist, dist * factor);
        }

        if (maxDist > 0) {
            populatedEdges.push_back({v1.x, v1.y, dx, dy, maxDist});
        }
    };

    // Check each edge of the patch
    patch->shape.forEdge([&](const geom::Point& v1, const geom::Point& v2) {
        // Check if edge is on a road/artery
        bool onRoad = false;
        for (const auto& artery : model->arteries) {
            if (artery.size() < 2) continue;
            for (size_t j = 0; j + 1 < artery.size(); ++j) {
                if ((*artery[j] == v1 && *artery[j + 1] == v2) ||
                    (*artery[j] == v2 && *artery[j + 1] == v1)) {
                    onRoad = true;
                    break;
                }
            }
            if (onRoad) break;
        }

        if (onRoad) {
            addEdge(v1, v2, 1.0);
        } else {
            // For non-road edges, add with reduced factor if patch is within city
            if (patch->withinCity) {
                addEdge(v1, v2, 0.4);
            }
        }
    });

    // If no populated edges found, don't filter
    if (populatedEdges.empty()) return;

    // Calculate density for each vertex based on whether all adjacent patches are in city
    std::vector<double> density;
    for (size_t i = 0; i < patch->shape.length(); ++i) {
        const geom::Point& v = patch->shape[i];

        // Check if this is a gate vertex
        bool isGate = false;
        if (model->wall) {
            for (const auto& gate : model->wall->gates) {
                if (*gate == v) {
                    isGate = true;
                    break;
                }
            }
        }

        if (isGate) {
            density.push_back(1.0);
        } else {
            // Check if all patches at this vertex are within city
            auto patches = model->patchByVertex(v);
            bool allWithinCity = true;
            for (auto* p : patches) {
                if (!p->withinCity) {
                    allWithinCity = false;
                    break;
                }
            }
            density.push_back(allWithinCity ? 2.0 * utils::Random::floatVal() : 0.0);
        }
    }

    // Filter buildings
    auto it = geometry.begin();
    while (it != geometry.end()) {
        double minDist = 1.0;

        // Find minimum distance ratio to any populated edge
        for (const auto& edge : populatedEdges) {
            for (size_t i = 0; i < it->length(); ++i) {
                const geom::Point& v = (*it)[i];
                double d = geom::GeomUtils::distance2line(edge.x, edge.y, edge.dx, edge.dy, v.x, v.y);
                double dist = edge.d > 0 ? d / edge.d : 1.0;
                minDist = std::min(minDist, dist);
            }
        }

        // Interpolate density at building center
        geom::Point c = it->center();
        auto interp = patch->shape.interpolate(c);
        double p = 0.0;
        for (size_t j = 0; j < interp.size() && j < density.size(); ++j) {
            p += density[j] * interp[j];
        }
        if (p > 0.001) {
            minDist /= p;
        }

        // Filter based on random threshold
        if (utils::Random::fuzzy(1.0) > minDist) {
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

void Ward::createAlleys(
    const geom::Polygon& p,
    double minSq,
    double gridChaos,
    double sizeChaos,
    double emptyProb,
    double split
) {
    size_t pLen = p.length();

    if (pLen < 3) {
        if (!utils::Random::boolVal(emptyProb)) {
            geometry.push_back(p);
        }
        return;
    }

    // Use OBB (Oriented Bounding Box) to find best cut direction (faithful to mfcg.js Bisector)
    auto obb = p.orientedBoundingBox();
    if (obb.size() < 4) {
        // Fall back to longest edge if OBB fails
        size_t longestEdge = 0;
        double longestLen = 0;
        for (size_t i = 0; i < pLen; ++i) {
            geom::Point v = p.vectori(static_cast<int>(i));
            double len = v.length();
            if (len > longestLen) {
                longestLen = len;
                longestEdge = i;
            }
        }

        double spread = 0.8 * gridChaos;
        double ratio = (1.0 - spread) / 2.0 + utils::Random::floatVal() * spread;
        double sq = p.square();
        double angleSpread = M_PI / 6.0 * gridChaos * (sq < minSq * 4 ? 0.0 : 1.0);
        double angle = (utils::Random::floatVal() - 0.5) * angleSpread;
        double gap = (split > 0.5) ? ALLEY : 0.0;

        auto halves = building::Cutter::bisect(p, p[longestEdge], ratio, angle, gap);

        if (halves.size() == 1) {
            if (!utils::Random::boolVal(emptyProb)) {
                addBuildingLot(p, minSq);
            }
            return;
        }

        for (const auto& half : halves) {
            double halfSq = half.square();
            double threshold = minSq * std::pow(2.0, 4.0 * sizeChaos * (utils::Random::floatVal() - 0.5));

            if (halfSq < threshold) {
                if (!utils::Random::boolVal(emptyProb)) {
                    addBuildingLot(half, minSq);
                }
            } else {
                double r1 = utils::Random::floatVal();
                double r2 = utils::Random::floatVal();
                double divisor = r1 * r2;
                bool shouldSplit = divisor > 0.0001 && halfSq > minSq / divisor;
                createAlleys(half, minSq, gridChaos, sizeChaos, emptyProb, shouldSplit ? 1.0 : 0.0);
            }
        }
        return;
    }

    // Calculate OBB dimensions
    geom::Point edge01 = obb[1].subtract(obb[0]);
    geom::Point edge12 = obb[2].subtract(obb[1]);
    double len01 = edge01.length();
    double len12 = edge12.length();

    // Determine long axis (we cut perpendicular to it)
    geom::Point longAxis = (len01 > len12) ? edge01 : edge12;
    geom::Point shortAxis = (len01 > len12) ? edge12 : edge01;
    double longLen = std::max(len01, len12);

    // Cut position: project centroid onto long axis, add randomness
    geom::Point centroid = p.centroid();
    geom::Point obbCorner = obb[0];

    // Project centroid onto long axis
    double projT = 0.0;
    if (longLen > 0.001) {
        geom::Point toCentroid = centroid.subtract(obbCorner);
        projT = (toCentroid.x * longAxis.x + toCentroid.y * longAxis.y) / (longLen * longLen);
    }

    // Faithful to mfcg.js: cutRatio = (proj + random) / 2, for variation around center
    double normal3 = (utils::Random::floatVal() + utils::Random::floatVal() + utils::Random::floatVal()) / 3.0;
    double cutRatio = (projT + normal3) / 2.0;
    cutRatio = std::max(0.2, std::min(0.8, cutRatio));  // Keep within reasonable bounds

    // Calculate cut point along the long axis
    geom::Point cutPoint(obbCorner.x + longAxis.x * cutRatio, obbCorner.y + longAxis.y * cutRatio);

    // Cut direction is along the short axis (perpendicular to long axis)
    geom::Point cutDir = shortAxis.norm(1.0);
    geom::Point cutEnd = cutPoint.add(cutDir.scale(longLen * 2));  // Extend far enough

    double gap = (split > 0.5) ? ALLEY : 0.0;
    auto halves = p.cut(cutPoint, cutEnd, gap);

    // If cut failed, try cutting from the other direction
    if (halves.size() < 2) {
        cutEnd = cutPoint.subtract(cutDir.scale(longLen * 2));
        halves = p.cut(cutPoint, cutEnd, gap);
    }

    // If still failed, fall back to longest edge bisect
    if (halves.size() < 2) {
        size_t longestEdge = 0;
        double longestLen = 0;
        for (size_t i = 0; i < pLen; ++i) {
            geom::Point v = p.vectori(static_cast<int>(i));
            double len = v.length();
            if (len > longestLen) {
                longestLen = len;
                longestEdge = i;
            }
        }

        double spread = 0.8 * gridChaos;
        double ratio = (1.0 - spread) / 2.0 + utils::Random::floatVal() * spread;
        halves = building::Cutter::bisect(p, p[longestEdge], ratio, 0.0, gap);
    }

    if (halves.size() < 2) {
        if (!utils::Random::boolVal(emptyProb)) {
            addBuildingLot(p, minSq);
        }
        return;
    }

    for (const auto& half : halves) {
        double halfSq = std::abs(half.square());
        double threshold = minSq * std::pow(2.0, 4.0 * sizeChaos * (utils::Random::floatVal() - 0.5));

        if (halfSq < threshold) {
            if (!utils::Random::boolVal(emptyProb)) {
                addBuildingLot(half, minSq);
            }
        } else {
            double r1 = utils::Random::floatVal();
            double r2 = utils::Random::floatVal();
            double divisor = r1 * r2;
            bool shouldSplit = divisor > 0.0001 && halfSq > minSq / divisor;
            createAlleys(half, minSq, gridChaos, sizeChaos, emptyProb, shouldSplit ? 1.0 : 0.0);
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

void Ward::createAlleysWithParams(
    const geom::Polygon& p,
    const AlleyParams& params,
    bool isInitialCall
) {
    if (p.length() < 3) return;

    double area = std::abs(p.square());

    // Initial call uses minSq * blockSize as threshold (faithful to mfcg.js line 13067-13068)
    double threshold = isInitialCall
        ? params.minSq * params.blockSize * std::pow(2.0, params.sizeChaos * (2.0 * utils::Random::floatVal() - 1.0))
        : params.minSq * std::pow(2.0, params.sizeChaos * (2.0 * utils::Random::floatVal() - 1.0));

    // If area is small enough, create a block
    if (area < threshold) {
        if (!utils::Random::boolVal(params.emptyProb)) {
            geometry.push_back(p);
        }
        return;
    }

    // Find longest edge
    size_t longestEdge = 0;
    double longestLen = 0;
    for (size_t i = 0; i < p.length(); ++i) {
        geom::Point v = p.vectori(static_cast<int>(i));
        double len = v.length();
        if (len > longestLen) {
            longestLen = len;
            longestEdge = i;
        }
    }

    // Calculate cut ratio based on gridChaos
    double spread = 0.8 * params.gridChaos;
    double ratio = (1.0 - spread) / 2.0 + utils::Random::floatVal() * spread;

    // Angle spread for larger blocks
    double angleSpread = M_PI / 6.0 * params.gridChaos * (area < params.minSq * 4 ? 0.0 : 1.0);
    double angle = (utils::Random::floatVal() - 0.5) * angleSpread;

    // Gap for alleys
    double gap = ALLEY;

    auto halves = building::Cutter::bisect(p, p[longestEdge], ratio, angle, gap);

    if (halves.size() < 2) {
        // Failed to bisect, treat as leaf
        if (!utils::Random::boolVal(params.emptyProb)) {
            geometry.push_back(p);
        }
        return;
    }

    // Store the alley cut line for rendering
    if (halves.size() >= 2) {
        // The cut is between the two halves - find shared edge
        // For now, store centroid to centroid as approximation
        std::vector<geom::Point> cutLine;
        cutLine.push_back(halves[0].center());
        cutLine.push_back(halves[1].center());
        alleys.push_back(cutLine);
    }

    // Process each half
    for (const auto& half : halves) {
        double halfArea = std::abs(half.square());

        // Check if this could be a church (medium-sized block, first one)
        double churchThreshold = params.minSq * 4.0;
        if (church.empty() && halfArea <= churchThreshold && halfArea >= params.minSq) {
            createChurch(half);
            continue;
        }

        // Recursive subdivision with non-initial threshold
        createAlleysWithParams(half, params, false);
    }
}

std::vector<geom::Point> Ward::semiSmooth(
    const geom::Point& p0,
    const geom::Point& p1,
    const geom::Point& p2,
    double minFront
) {
    // Faithful to mfcg.js semiSmooth function
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

    // Create arc approximation
    std::vector<geom::Point> result;
    result.push_back(p0);

    // Add arc points
    if (len01 < len12) {
        // Shorter segment is p0-p1
        double t = len01 / len12;
        geom::Point arcPoint(p1.x + v12.x * t, p1.y + v12.y * t);
        result.push_back(arcPoint);
    } else {
        // Shorter segment is p1-p2
        double t = -len12 / len01;
        geom::Point arcPoint(p1.x + v01.x * t, p1.y + v01.y * t);
        result.push_back(arcPoint);
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

} // namespace wards
} // namespace town_generator
