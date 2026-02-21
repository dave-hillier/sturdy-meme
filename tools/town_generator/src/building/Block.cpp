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
    // Faithful to mfcg-clean TwistedBlock.createLots (lines 234-270)
    // Uses Bisector WITHOUT getGap to subdivide block into individual lots
    // This means lots within a block share edges (no gaps between buildings)

    lots.clear();
    courtyard.clear();

    if (shape.length() < 3) {
        return;
    }

    double area = std::abs(shape.square());
    double minSq = group ? group->alleys.minSq : 100.0;
    double sizeChaos = group ? group->alleys.sizeChaos : 0.5;

    // If block is too small, treat as single lot
    if (area < minSq) {
        lots.push_back(shape);
        return;
    }

    // Use Bisector to subdivide block into lots
    // Key: NO getGap callback - lots share edges within a block
    // Faithful to TwistedBlock: new Bisector(block.shape, config.minSq, max(4*sizeChaos, 1.2))
    double variance = std::max(4.0 * sizeChaos, 1.2);
    utils::Bisector bisector(shape.vertexValues(), minSq, variance);
    bisector.minTurnOffset = 0.5;

    // NO getGap - lots within a block share edges
    // NO processCut - keep simple cuts

    auto lotShapes = bisector.partition();

    // Convert partitioned shapes to Polygons (faithful to MFCG: filterInner BEFORE valid-lot filter)
    for (const auto& lotShape : lotShapes) {
        if (lotShape.size() >= 3) {
            lots.push_back(geom::Polygon(lotShape));
        }
    }

    // Filter inner lots FIRST (faithful to MFCG TwistedBlock.createLots)
    // Reference: 06-blocks.js - filterInner is called before area/aspect filtering
    filterInner();

    // Now apply valid-lot filters on remaining lots
    double minArea = minSq / 4.0;
    std::vector<geom::Polygon> validLots;

    for (const auto& lotPoly : lots) {
        // Minimum 4 vertices (faithful to MFCG: 4 > h.length rejects < 4)
        if (lotPoly.length() < 4) continue;

        double lotArea = std::abs(lotPoly.square());

        // Skip lots that are too small
        if (lotArea < minArea) continue;

        // Check aspect ratio using OBB (faithful to TwistedBlock lines 258-264)
        auto obb = lotPoly.orientedBoundingBox();
        if (obb.size() == 4) {
            double width = geom::Point::distance(obb[0], obb[1]);
            double height = geom::Point::distance(obb[1], obb[2]);

            // Reject if too thin
            if (width < 1.2 || height < 1.2) continue;

            // Reject if too irregular (area vs OBB area ratio)
            double obbArea = width * height;
            if (obbArea > 0.001 && lotArea / obbArea < 0.5) continue;
        }

        validLots.push_back(lotPoly);
    }

    lots = validLots;

    // If no lots created, use the whole block as one lot
    if (lots.empty()) {
        lots.push_back(shape);
    }
}

void Block::createRects() {
    // Faithful to mfcg.js Block.createRects (lines 12123-12170)
    // Converts lots to rectangles using LIRA (largest inscribed rectangle),
    // then applies "Shrink" processing for gaps between buildings

    if (lots.empty()) {
        createLots();
    }

    rects.clear();

    double inset = group ? group->alleys.inset : 0.3;
    size_t blockLen = shape.length();

    for (const auto& lot : lots) {
        if (lot.length() < 3) {
            rects.push_back(lot);
            continue;
        }

        geom::Polygon rect = lot;

        // Faithful to mfcg.js lines 12131-12148
        // If lot is already a rectangle, skip LIRA calculation
        if (!isRectangle(lot)) {
            // Find which edge of lot touches the block perimeter (for front edge)
            int frontEdge = -1;
            size_t lotLen = lot.length();
            for (size_t li = 0; li < lotLen; ++li) {
                const geom::Point& lv0 = lot[li];
                const geom::Point& lv1 = lot[(li + 1) % lotLen];

                for (size_t bi = 0; bi < blockLen; ++bi) {
                    const geom::Point& bv0 = shape[bi];
                    const geom::Point& bv1 = shape[(bi + 1) % blockLen];

                    // Check if edges converge (are collinear and overlapping)
                    if (geom::GeomUtils::converge(lv0, lv1, bv0, bv1)) {
                        frontEdge = static_cast<int>(li);
                        break;
                    }
                }
                if (frontEdge != -1) break;
            }

            // Get largest inscribed rectangle
            std::vector<geom::Point> lotPts;
            for (size_t i = 0; i < lot.length(); ++i) {
                lotPts.push_back(lot[i]);
            }

            std::vector<geom::Point> lirRect;
            if (frontEdge != -1) {
                // Use LIR aligned to front edge
                lirRect = geom::GeomUtils::lir(lotPts, frontEdge);
            } else {
                // Use LIRA (axis-aligned)
                lirRect = geom::GeomUtils::lira(lotPts);
            }

            if (lirRect.size() == 4) {
                // Check minimum dimensions - mfcg.js line 12148
                double lotArea = std::abs(lot.square());
                double minDim = std::max(1.2, std::sqrt(lotArea) / 2.0);
                double w = geom::Point::distance(lirRect[0], lirRect[1]);
                double h = geom::Point::distance(lirRect[1], lirRect[2]);

                if (w >= minDim && h >= minDim) {
                    rect = geom::Polygon(lirRect);
                }
                // else keep original lot shape
            }
        }

        // Apply "Shrink" processing (faithful to mfcg.js lines 35-52)
        // Shrink edges that DON'T touch the block perimeter
        // This creates gaps between adjacent buildings
        double shrinkAmount = inset * (1.0 - std::abs(
            (utils::Random::floatVal() + utils::Random::floatVal() +
             utils::Random::floatVal() + utils::Random::floatVal()) / 2.0 - 1.0));

        if (shrinkAmount > 0.3) {
            size_t rectLen = rect.length();
            std::vector<double> shrinkAmounts(rectLen, 0.0);

            // For each edge, check if it touches the block perimeter
            for (size_t i = 0; i < rectLen; ++i) {
                const geom::Point& e0 = rect[i];
                const geom::Point& e1 = rect[(i + 1) % rectLen];

                bool touchesBlock = false;
                for (size_t j = 0; j < blockLen && !touchesBlock; ++j) {
                    const geom::Point& b0 = shape[j];
                    const geom::Point& b1 = shape[(j + 1) % blockLen];

                    // Check if edges converge (are parallel and overlapping)
                    // Simplified: check if edge midpoints are close to block edge
                    geom::Point eMid((e0.x + e1.x) / 2, (e0.y + e1.y) / 2);

                    // Project midpoint onto block edge
                    double bLen = geom::Point::distance(b0, b1);
                    if (bLen < 0.001) continue;

                    geom::Point bDir((b1.x - b0.x) / bLen, (b1.y - b0.y) / bLen);
                    double t = (eMid.x - b0.x) * bDir.x + (eMid.y - b0.y) * bDir.y;

                    if (t >= 0 && t <= bLen) {
                        geom::Point proj(b0.x + t * bDir.x, b0.y + t * bDir.y);
                        double dist = geom::Point::distance(eMid, proj);
                        if (dist < 0.5) {
                            touchesBlock = true;
                        }
                    }
                }

                // Edges that don't touch block get shrunk
                shrinkAmounts[i] = touchesBlock ? 0.0 : shrinkAmount;
            }

            // Apply shrink to create the final rect
            rect = rect.shrink(shrinkAmounts);
        }

        if (rect.length() >= 3 && std::abs(rect.square()) > 0.5) {
            rects.push_back(rect);
        } else {
            rects.push_back(lot);
        }
    }
}

void Block::createBuildings() {
    // Faithful to mfcg.js Block.createBuildings (lines 12177-12199)
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

        // Faithful to mfcg.js lines 12194-12198:
        // - If >4 vertices: simplify to 4, then call Building.create
        // - If =4 vertices: call Building.create
        // - If <4 vertices: use rect as-is

        if (rect.length() > 4) {
            // Simplify polygon to quad using simplifyClosed
            // Faithful to: do PolyCore.simplifyClosed(f); while (4 < f.length);
            geom::Polygon simplified = rect.simplifyClosed(4);

            if (simplified.length() == 4) {
                // Try Building.create with simplified quad
                geom::Polygon building = Building::create(simplified, threshold, true, false, 0.6);
                if (building.length() >= 3) {
                    buildings.push_back(building);
                } else {
                    buildings.push_back(simplified);
                }
            } else {
                // Simplification failed, use original
                buildings.push_back(rect);
            }
        } else if (rect.length() == 4) {
            // Quad - try Building.create
            // Faithful to: Building.create(d, c, !0, null, .6)
            // hasFront=true, symmetric=null(false), gap=0.6
            geom::Polygon building = Building::create(rect, threshold, true, false, 0.6);

            if (building.length() >= 3) {
                buildings.push_back(building);
            } else {
                // Building creation failed, use rectangle directly
                buildings.push_back(rect);
            }
        } else {
            // <4 vertices - use rect as-is (no Building.create)
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
