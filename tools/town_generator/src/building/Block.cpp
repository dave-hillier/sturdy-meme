#include "town_generator/building/Block.h"
#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Bisector.h"
#include "town_generator/building/Building.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>
#include <SDL3/SDL_log.h>

namespace town_generator {
namespace building {

Block::Block(const geom::Polygon& shape, WardGroup* group)
    : shape(shape), group(group) {}

double Block::getArea(const geom::Polygon& poly) {
    auto it = cacheArea.find(&poly);
    if (it != cacheArea.end()) {
        return it->second;
    }
    double area = std::abs(poly.square());
    cacheArea[&poly] = area;
    return area;
}

std::vector<geom::Point> Block::getOBB(const geom::Polygon& poly) {
    auto it = cacheOBB.find(&poly);
    if (it != cacheOBB.end()) {
        return it->second;
    }
    std::vector<geom::Point> obb = poly.orientedBoundingBox();
    cacheOBB[&poly] = obb;
    return obb;
}

geom::Point Block::getCenter() {
    if (!centerComputed) {
        center = shape.centroid();
        centerComputed = true;
    }
    return center;
}

bool Block::isRectangle(const geom::Polygon& poly) {
    // Faithful to mfcg.js Block.isRectangle (lines 12213-12219)
    if (poly.length() != 4) return false;

    double area = getArea(poly);
    auto obb = getOBB(poly);

    if (obb.size() < 4) return false;

    // OBB area
    geom::Point edge01 = obb[1].subtract(obb[0]);
    geom::Point edge12 = obb[2].subtract(obb[1]);
    double obbArea = edge01.length() * edge12.length();

    if (obbArea < 0.001) return false;

    return (area / obbArea) > 0.75;
}

bool Block::edgeConvergesWithBlock(const geom::Point& v0, const geom::Point& v1) const {
    // Check if lot edge (v0, v1) converges with any block perimeter edge
    // Faithful to mfcg.js qa.converge
    size_t blockLen = shape.length();
    for (size_t i = 0; i < blockLen; ++i) {
        const geom::Point& b0 = shape[i];
        const geom::Point& b1 = shape[(i + 1) % blockLen];

        // Check if edges are on the same line and overlap
        // Using geometric check similar to qa.converge
        geom::Point lotDir = v1.subtract(v0);
        geom::Point blockDir = b1.subtract(b0);

        double lotLen = lotDir.length();
        double blockLen = blockDir.length();

        if (lotLen < 0.001 || blockLen < 0.001) continue;

        // Normalize
        lotDir = lotDir.scale(1.0 / lotLen);
        blockDir = blockDir.scale(1.0 / blockLen);

        // Check if parallel (dot product close to +/-1)
        double dot = lotDir.x * blockDir.x + lotDir.y * blockDir.y;
        if (std::abs(dot) < 0.99) continue;

        // Check if on same line (distance from v0 to block edge line)
        double dist = geom::GeomUtils::distance2line(b0.x, b0.y, blockDir.x, blockDir.y, v0.x, v0.y);
        if (dist > 0.1) continue;

        // Check if segments overlap
        // Project v0 and v1 onto block edge
        double t0 = ((v0.x - b0.x) * blockDir.x + (v0.y - b0.y) * blockDir.y);
        double t1 = ((v1.x - b0.x) * blockDir.x + (v1.y - b0.y) * blockDir.y);

        if (t0 > t1) std::swap(t0, t1);

        // Block edge spans [0, blockLen]
        // Need some overlap
        double overlapStart = std::max(t0, 0.0);
        double overlapEnd = std::min(t1, blockLen);

        if (overlapEnd - overlapStart > 0.1) {
            return true;
        }
    }
    return false;
}

void Block::createLots() {
    // Use TwistedBlock to create lots
    if (!group) {
        // No group, can't get parameters
        lots.clear();
        lots.push_back(shape);
        return;
    }

    // TwistedBlock::createLots handles filterInner internally
    // (faithful to MFCG order: partition -> filterInner -> valid-lot-filter)
    lots = TwistedBlock::createLots(this, group->alleys);
}

void Block::createRects() {
    // Faithful to mfcg.js Block.createRects (lines 12177-12220)
    if (lots.empty()) {
        createLots();
    }

    rects.clear();
    double inset = group ? group->alleys.inset : 0.3;
    size_t blockLen = shape.length();

    for (const auto& lot : lots) {
        // Check if lot is already rectangular
        if (isRectangle(lot)) {
            rects.push_back(lot);
            continue;
        }

        // Find if lot has an edge on the block perimeter
        int perimeterEdge = -1;
        size_t lotLen = lot.length();

        for (size_t i = 0; i < lotLen; ++i) {
            const geom::Point& v0 = lot[i];
            const geom::Point& v1 = lot[(i + 1) % lotLen];

            if (edgeConvergesWithBlock(v0, v1)) {
                if (perimeterEdge == -1) {
                    perimeterEdge = static_cast<int>(i);
                } else {
                    // Multiple perimeter edges - treat as rectangular
                    rects.push_back(lot);
                    perimeterEdge = -2;
                    break;
                }
            }
        }

        if (perimeterEdge == -2) continue;  // Already added

        // If no perimeter edge or multiple, use LIRA
        double area = getArea(lot);
        double minDim = std::max(1.2, std::sqrt(area) / 2.0);

        std::vector<geom::Point> pts;
        for (size_t i = 0; i < lot.length(); ++i) {
            pts.push_back(lot[i]);
        }

        std::vector<geom::Point> rect;
        if (perimeterEdge >= 0) {
            // Use LIR aligned with perimeter edge
            rect = geom::GeomUtils::lir(pts, perimeterEdge);
        } else {
            // Use LIRA (largest inscribed rectangle, any alignment)
            rect = geom::GeomUtils::lira(pts);
        }

        // Validate rectangle dimensions
        if (rect.size() >= 4) {
            double rectLen01 = geom::Point::distance(rect[0], rect[1]);
            double rectLen12 = geom::Point::distance(rect[1], rect[2]);

            if (rectLen01 >= minDim && rectLen12 >= minDim) {
                rects.push_back(geom::Polygon(rect));
            } else {
                // Rectangle too small, use original lot
                rects.push_back(lot);
            }
        } else {
            rects.push_back(lot);
        }
    }

    // Apply shrink processing (faithful to mfcg.js "Shrink" processing mode)
    // This adds variation to building setbacks
    if (group && group->processingMode == "Shrink") {
        for (auto& rect : rects) {
            // Generate random shrink amount (normal distribution approximation)
            double normal4 = (utils::Random::floatVal() + utils::Random::floatVal() +
                             utils::Random::floatVal() + utils::Random::floatVal()) / 2.0;
            double shrinkAmount = inset * (1.0 - std::abs(normal4 - 1.0));

            if (shrinkAmount > 0.3) {
                // Calculate shrink amounts for each edge
                // Edges on block perimeter get 0 shrink, others get shrinkAmount
                std::vector<geom::Point> pts;
                for (size_t i = 0; i < rect.length(); ++i) {
                    pts.push_back(rect[i]);
                }

                std::vector<double> amounts;
                for (size_t i = 0; i < pts.size(); ++i) {
                    const geom::Point& v0 = pts[i];
                    const geom::Point& v1 = pts[(i + 1) % pts.size()];

                    // Check if edge converges with block perimeter
                    if (edgeConvergesWithBlock(v0, v1)) {
                        amounts.push_back(0.0);  // No shrink on perimeter edges
                    } else {
                        amounts.push_back(shrinkAmount);
                    }
                }

                auto shrunk = geom::GeomUtils::shrink(pts, amounts);
                if (shrunk.size() >= 3) {
                    rect = geom::Polygon(shrunk);
                }
            }
        }
    }
}

void Block::createBuildings() {
    // Faithful to mfcg.js Block.createBuildings (lines 12221-12252)
    if (rects.empty()) {
        createRects();
    }

    buildings.clear();
    double minSq = group ? group->alleys.minSq : 15.0;
    double minBlockSq = minSq / 4.0;
    double shapeFactor = group ? group->alleys.shapeFactor : 1.0;
    double threshold = minBlockSq * shapeFactor;

    for (const auto& rect : rects) {
        geom::Polygon simplified = rect;

        // If more than 4 vertices, simplify to 4 using PolyCore.simplifyClosed algorithm
        // Faithful to MFCG: removes vertex with smallest "wedge" area
        if (simplified.length() > 4) {
            simplified.simplify(4);
        }

        // Try to create complex L-shaped building using Building.create
        // Faithful to mfcg.js: Building.create(d, c, true, null, 0.6)
        // where c = minSq/4 * shapeFactor
        if (simplified.length() == 4) {
            geom::Polygon lshaped = Building::create(simplified, threshold, true, false, 0.6);
            if (lshaped.length() >= 3) {
                buildings.push_back(lshaped);
            } else {
                // L-shape creation failed, use original rectangle
                buildings.push_back(simplified);
            }
        } else if (simplified.length() >= 3) {
            // Not a quad but valid polygon, use as-is
            // Faithful to MFCG: `4 == f[0].length ? h(f[0]) : this.buildings.push(f[0])`
            buildings.push_back(simplified);
        }
        // Skip degenerate polygons (< 3 vertices)
    }
}

std::vector<geom::Polygon> Block::filterInner() {
    // Faithful to mfcg.js Block.filterInner (lines 12253-12286)
    // Remove lots that don't touch the block perimeter
    // Return the removed (courtyard) lots

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

                // Calculate distance from vertex to edge
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

                        // MFCG uses 1e-9 tolerance, we use 1e-6 for floating point robustness
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
    // Faithful to mfcg.js Block.indentFronts (lines 12287-12302)
    // Push lots slightly toward block center for setback variation

    geom::Point blockCenter = getCenter();

    for (auto& lot : lots) {
        double area = getArea(lot);
        double indent = std::min(std::sqrt(area) / 3.0, 1.2) * utils::Random::floatVal();

        if (indent < 0.5) continue;

        geom::Point lotCenter = lot.centroid();
        geom::Point dir = blockCenter.subtract(lotCenter);
        double dirLen = dir.length();

        if (dirLen < 0.001) continue;

        dir = dir.scale(indent / dirLen);

        // Translate the block shape by the offset
        std::vector<geom::Point> blockPts;
        for (size_t i = 0; i < shape.length(); ++i) {
            blockPts.push_back(shape[i]);
        }
        std::vector<geom::Point> translatedBlock = geom::GeomUtils::translate(blockPts, dir.x, dir.y);

        // Get lot points
        std::vector<geom::Point> lotPts;
        for (size_t i = 0; i < lot.length(); ++i) {
            lotPts.push_back(lot[i]);
        }

        // Intersect lot with translated block shape (ye.and)
        // This ensures the indented lot stays within bounds
        std::vector<geom::Point> clipped = geom::GeomUtils::polygonIntersection(lotPts, translatedBlock);

        if (clipped.size() >= 3) {
            lot = geom::Polygon(clipped);
        }
        // If intersection fails, keep original lot
    }
}

std::vector<geom::Point> Block::spawnTrees() {
    // Faithful to mfcg.js Block.spawnTrees (lines 12303-12325)
    // Uses Ae.fillArea for natural tree distribution
    std::vector<geom::Point> trees;

    if (courtyard.empty() || !group) return trees;

    double greenery = group->greenery;
    bool isUrban = group->urban;

    // Reduce greenery for non-urban areas (mfcg.js multiplies by 0.1)
    if (!isUrban) {
        greenery *= 0.1;
    }

    // Spawn trees in courtyard areas using fillArea (faithful to MFCG)
    for (const auto& yard : courtyard) {
        // Convert polygon to point vector
        std::vector<geom::Point> pts;
        for (size_t i = 0; i < yard.length(); ++i) {
            pts.push_back(yard[i]);
        }

        // Fill with trees using hexagonal grid pattern
        auto treePts = geom::GeomUtils::fillArea(pts, greenery, 3.0);
        for (const auto& p : treePts) {
            trees.push_back(p);
        }
    }

    return trees;
}

// TwistedBlock implementation
// Faithful port of mfcg.js TwistedBlock.createLots (lines 158-187 of 06-blocks.js)

std::vector<geom::Polygon> TwistedBlock::createLots(
    Block* block,
    const wards::AlleyParams& params
) {
    std::vector<geom::Polygon> result;

    if (!block || block->shape.length() < 3) {
        return result;
    }

    // MFCG: new Bisector(a.shape, b.minSq, Math.max(4 * b.sizeChaos, 1.2))
    double variance = std::max(4.0 * params.sizeChaos, 1.2);
    Bisector bisector(block->shape, params.minSq, variance);
    bisector.minTurnOffset = 0.5;  // MFCG: c.minTurnOffset = .5
    // Note: NO getGap for lot subdivision (lots don't have gaps between them)

    // Partition the block shape
    std::vector<geom::Polygon> partitioned = bisector.partition();

    // MFCG order: partition -> filterInner -> valid-lot-filter
    // d = a.filterInner(d) is called BEFORE the valid-lot check
    // Temporarily store in block->lots so filterInner can work on them
    block->lots = partitioned;
    block->filterInner();
    partitioned = block->lots;  // Get filtered results

    // Filter out lots that are too small or have bad shapes
    // MFCG: b = b.minSq / 4 (minArea threshold)
    double minArea = params.minSq / 4.0;

    for (const auto& lot : partitioned) {
        // MFCG: if (4 > h.length || k < b) k = !1
        if (lot.length() < 4) continue;

        double area = std::abs(lot.square());
        if (area < minArea) continue;

        // Get OBB for shape validation
        auto obb = lot.orientedBoundingBox();
        if (obb.size() < 4) {
            // Can't validate, include anyway
            result.push_back(lot);
            continue;
        }

        geom::Point edge01 = obb[1].subtract(obb[0]);
        geom::Point edge12 = obb[2].subtract(obb[1]);
        double len01 = edge01.length();
        double len12 = edge12.length();

        // MFCG: k = 1.2 <= n && 1.2 <= p && .5 < k / (n * p)
        if (len01 < 1.2 || len12 < 1.2) continue;

        double obbArea = len01 * len12;
        if (obbArea > 0.001 && (area / obbArea) < 0.5) continue;

        result.push_back(lot);
    }

    return result;
}

} // namespace building
} // namespace town_generator
