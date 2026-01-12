#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/Polygon.h"
#include "town_generator/wards/Ward.h"
#include <vector>

namespace town_generator {
namespace building {

class WardGroup;

/**
 * Block - A city block within a WardGroup
 *
 * Simplified implementation based on mfcg-clean Block.js.
 * A Block contains:
 * - shape: the block perimeter polygon
 * - lots: individual building lots from frontage-based subdivision
 * - rects: rectangular approximations of lots (LIRA algorithm)
 * - buildings: complex building shapes
 * - courtyard: inner lots filtered out during building creation
 */
class Block {
public:
    geom::Polygon shape;
    WardGroup* group = nullptr;

    std::vector<geom::Polygon> lots;
    std::vector<geom::Polygon> rects;
    std::vector<geom::Polygon> buildings;
    std::vector<geom::Polygon> courtyard;

    // Center point of the block (cached)
    geom::Point center;
    bool centerComputed = false;

    Block() = default;
    explicit Block(const geom::Polygon& shape, WardGroup* group = nullptr);

    // Create lots by subdividing along longest edge (frontage)
    void createLots();

    // Convert lots to rectangles using LIRA (OBB shrunk 10%)
    void createRects();

    // Create building shapes from rectangles
    void createBuildings();

    // Filter out inner lots that don't touch the block perimeter
    std::vector<geom::Polygon> filterInner();

    // Indent lots toward block center for setback variation
    void indentFronts();

    // Spawn trees in courtyard areas
    std::vector<geom::Point> spawnTrees();

    // Utility functions
    double getArea(const geom::Polygon& poly);
    std::vector<geom::Point> getOBB(const geom::Polygon& poly);
    bool isRectangle(const geom::Polygon& poly);
    geom::Point getCenter();

private:
    bool edgeConvergesWithBlock(const geom::Point& v0, const geom::Point& v1) const;
};

/**
 * TwistedBlock - Legacy API for lot creation
 * Now just delegates to Block::createLots()
 */
class TwistedBlock {
public:
    static std::vector<geom::Polygon> createLots(
        Block* block,
        const wards::AlleyParams& params
    );
};

} // namespace building
} // namespace town_generator
