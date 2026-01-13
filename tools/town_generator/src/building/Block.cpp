#include "town_generator/building/Block.h"
#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Building.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include "town_generator/utils/Bisector.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace building {

Block::Block(const geom::Polygon& shape, WardGroup* group)
    : shape(shape), group(group) {}

geom::Point Block::getCenter() {
    if (!centerComputed) {
        center = shape.centroid();
        centerComputed = true;
    }
    return center;
}

void Block::createLots() {
    // Frontage-based subdivision (faithful to mfcg-clean Block.js subdivideLots)
    // Divides block along its longest edge (frontage) into rectangular lots

    lots.clear();
    courtyard.clear();

    if (shape.length() < 3) {
        return;
    }

    double area = std::abs(shape.square());
    double minSq = group ? group->alleys.minSq : 100.0;
    double minFront = group ? group->alleys.minFront : 3.0;

    // If block is too small, treat as single lot
    if (area < minSq) {
        lots.push_back(shape);
        return;
    }

    // Find longest edge for frontage
    size_t frontIdx = 0;
    double maxLen = 0;
    size_t n = shape.length();

    for (size_t i = 0; i < n; ++i) {
        double len = geom::Point::distance(shape[i], shape[(i + 1) % n]);
        if (len > maxLen) {
            maxLen = len;
            frontIdx = i;
        }
    }

    const geom::Point& frontP1 = shape[frontIdx];
    const geom::Point& frontP2 = shape[(frontIdx + 1) % n];
    double frontLen = maxLen;

    // If frontage too short for multiple lots, use whole block
    if (frontLen < minFront * 2) {
        lots.push_back(shape);
        return;
    }

    // Calculate number of lots along frontage
    int numLots = std::max(2, static_cast<int>(frontLen / minFront));

    // Faithful to mfcg-clean Block.js subdivideLots - always use front/back interpolation
    // The reference uses the same approach regardless of vertex count.
    // For non-quadrilaterals, this approximates by picking edges at (frontIdx + 2) % n.

    // Find back edge (opposite to front edge)
    size_t backIdx = (frontIdx + 2) % n;
    const geom::Point& backP1 = shape[backIdx];
    const geom::Point& backP2 = shape[(backIdx + 1) % n];

    // Create lots by interpolating along front and back edges
    for (int i = 0; i < numLots; ++i) {
        double t1 = static_cast<double>(i) / numLots;
        double t2 = static_cast<double>(i + 1) / numLots;

        geom::Point p1 = geom::GeomUtils::interpolate(frontP1, frontP2, t1);
        geom::Point p2 = geom::GeomUtils::interpolate(frontP1, frontP2, t2);
        // Back edge goes opposite direction
        geom::Point p3 = geom::GeomUtils::interpolate(backP2, backP1, t2);
        geom::Point p4 = geom::GeomUtils::interpolate(backP2, backP1, t1);

        lots.push_back(geom::Polygon({p1, p2, p3, p4}));
    }

    // If no lots created, use the whole block as one lot
    if (lots.empty()) {
        lots.push_back(shape);
    }
}

void Block::createRects() {
    // Simplified LIRA - faithful to mfcg-clean PolygonUtils.lira
    // Algorithm: Get OBB, shrink 10% toward center

    if (lots.empty()) {
        createLots();
    }

    rects.clear();

    for (const auto& lot : lots) {
        if (lot.length() < 3) {
            rects.push_back(lot);
            continue;
        }

        // Get oriented bounding box
        std::vector<geom::Point> obb = lot.orientedBoundingBox();

        if (obb.size() != 4) {
            // OBB failed, use lot directly
            rects.push_back(lot);
            continue;
        }

        // Calculate center of OBB
        geom::Point obbCenter(0, 0);
        for (const auto& p : obb) {
            obbCenter.x += p.x;
            obbCenter.y += p.y;
        }
        obbCenter.x /= 4.0;
        obbCenter.y /= 4.0;

        // Shrink OBB toward center by 10% (lerp factor 0.1)
        std::vector<geom::Point> liraPoints;
        liraPoints.reserve(4);
        for (const auto& p : obb) {
            liraPoints.emplace_back(
                p.x * 0.9 + obbCenter.x * 0.1,
                p.y * 0.9 + obbCenter.y * 0.1
            );
        }

        // Verify the LIRA rectangle has positive area
        geom::Polygon liraRect(liraPoints);
        if (std::abs(liraRect.square()) < 0.5) {
            // Rectangle too small, use lot directly
            rects.push_back(lot);
            continue;
        }

        rects.push_back(liraRect);
    }
}

void Block::createBuildings() {
    // Faithful to mfcg.js Block.createBuildings (lines 4031-4053)
    if (rects.empty()) {
        createRects();
    }

    buildings.clear();

    // Use minSq/4 * shapeFactor as threshold
    // Faithful to: var c = b.minSq / 4 * b.shapeFactor;
    double minSq = group ? group->alleys.minSq : 100.0;
    double shapeFactor = group ? group->alleys.shapeFactor : 1.0;
    double threshold = minSq / 4.0 * shapeFactor;

    for (const auto& rect : rects) {
        if (rect.length() < 3) {
            continue;
        }

        // Try to create complex building using Building.create
        // Faithful to: Building.create(d, c, !0, null, .6)
        // hasFront=true, symmetric=null(false), gap=0.6
        geom::Polygon building = Building::create(rect, threshold, true, false, 0.6);

        if (building.length() >= 3) {
            buildings.push_back(building);
        } else {
            // Building creation failed, use rectangle directly
            buildings.push_back(rect);
        }
    }
}

std::vector<geom::Polygon> Block::filterInner() {
    // Filter out lots that don't touch the block perimeter
    // Returns the removed (courtyard) lots

    courtyard.clear();
    std::vector<geom::Polygon> filtered;

    size_t blockLen = shape.length();

    for (const auto& lot : lots) {
        bool touchesPerimeter = false;

        // Check each vertex of the lot
        for (size_t vi = 0; vi < lot.length() && !touchesPerimeter; ++vi) {
            const geom::Point& v = lot[vi];

            // Check if vertex lies on any edge of the block perimeter
            geom::Point prevPoint = shape[blockLen - 1];
            for (size_t ei = 0; ei < blockLen && !touchesPerimeter; ++ei) {
                const geom::Point& currPoint = shape[ei];

                double edgeDx = currPoint.x - prevPoint.x;
                double edgeDy = currPoint.y - prevPoint.y;
                double edgeLenSq = edgeDx * edgeDx + edgeDy * edgeDy;

                if (edgeLenSq > 1e-9) {
                    double t = ((v.x - prevPoint.x) * edgeDx + (v.y - prevPoint.y) * edgeDy) / edgeLenSq;

                    if (t >= 0.0 && t <= 1.0) {
                        geom::Point projected(
                            prevPoint.x + t * edgeDx,
                            prevPoint.y + t * edgeDy
                        );
                        double distSq = (v.x - projected.x) * (v.x - projected.x) +
                                       (v.y - projected.y) * (v.y - projected.y);

                        if (distSq < 1e-6) {
                            touchesPerimeter = true;
                        }
                    }
                }

                prevPoint = currPoint;
            }
        }

        if (touchesPerimeter) {
            filtered.push_back(lot);
        } else {
            courtyard.push_back(lot);
        }
    }

    lots = filtered;
    return courtyard;
}

void Block::indentFronts() {
    geom::Point blockCenter = getCenter();

    for (auto& lot : lots) {
        double area = std::abs(lot.square());
        double indent = std::min(std::sqrt(area) / 3.0, 1.2) * utils::Random::floatVal();

        if (indent < 0.5) continue;

        geom::Point lotCenter = lot.centroid();
        geom::Point dir = blockCenter.subtract(lotCenter);
        double dirLen = dir.length();

        if (dirLen < 0.001) continue;

        dir = dir.scale(indent / dirLen);

        // Offset lot vertices toward block center
        std::vector<geom::Point> offsetPts;
        for (size_t i = 0; i < lot.length(); ++i) {
            offsetPts.push_back(lot[i].add(dir));
        }

        lot = geom::Polygon(offsetPts);
    }
}

std::vector<geom::Point> Block::spawnTrees() {
    std::vector<geom::Point> trees;

    if (courtyard.empty() || !group) return trees;

    double greenery = group->greenery;
    bool isUrban = group->urban;

    if (!isUrban) {
        greenery *= 0.1;
    }

    for (const auto& yard : courtyard) {
        std::vector<geom::Point> pts;
        for (size_t i = 0; i < yard.length(); ++i) {
            pts.push_back(yard[i]);
        }

        auto treePts = geom::GeomUtils::fillArea(pts, greenery, 3.0);
        for (const auto& p : treePts) {
            trees.push_back(p);
        }
    }

    return trees;
}

// Kept for API compatibility but simplified
double Block::getArea(const geom::Polygon& poly) {
    return std::abs(poly.square());
}

std::vector<geom::Point> Block::getOBB(const geom::Polygon& poly) {
    return poly.orientedBoundingBox();
}

bool Block::isRectangle(const geom::Polygon& poly) {
    if (poly.length() != 4) return false;

    double area = std::abs(poly.square());
    auto obb = poly.orientedBoundingBox();

    if (obb.size() < 4) return false;

    geom::Point edge01 = obb[1].subtract(obb[0]);
    geom::Point edge12 = obb[2].subtract(obb[1]);
    double obbArea = edge01.length() * edge12.length();

    if (obbArea < 0.001) return false;

    return (area / obbArea) > 0.75;
}

// No longer used but kept for API compatibility
bool Block::edgeConvergesWithBlock(const geom::Point& v0, const geom::Point& v1) const {
    return false;
}

// TwistedBlock - simplified to just use Block's own createLots
std::vector<geom::Polygon> TwistedBlock::createLots(
    Block* block,
    const wards::AlleyParams& params
) {
    if (!block || block->shape.length() < 3) {
        return {};
    }

    // Just delegate to Block's simplified createLots
    block->createLots();
    return block->lots;
}

} // namespace building
} // namespace town_generator
