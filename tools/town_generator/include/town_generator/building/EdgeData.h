#pragma once

namespace town_generator {
namespace building {

/**
 * EdgeData - Edge type classification from mfcg.js (Tc enum)
 *
 * Used to track what type of feature each edge of a patch borders.
 * This enables proper inset calculations in getAvailable() and correct
 * rendering of different edge types.
 *
 * Reference: mfcg.js lines 11136-11150
 */
enum class EdgeType {
    NONE = 0,       // No special data (interior edge between cells)
    COAST = 1,      // Edge borders water (sea, lake)
    ROAD = 2,       // Edge borders a road/artery
    WALL = 3,       // Edge borders a city wall
    CANAL = 4,      // Edge borders a canal/river
    HORIZON = 5     // Edge is on the map boundary
};

/**
 * Get inset distance for an edge type (faithful to mfcg.js getAvailable)
 *
 * Reference: mfcg.js lines 12359-12379
 */
inline double getEdgeInset(EdgeType type, bool isLanding = false, double canalWidth = 0.0) {
    switch (type) {
        case EdgeType::COAST:
            return isLanding ? 2.0 : 1.2;  // Harbor landing vs regular coast
        case EdgeType::ROAD:
            return 1.0;  // pc.THICKNESS / 2
        case EdgeType::WALL:
            return 1.5 / 2.0 + 1.2;  // pc.THICKNESS / 2 + ALLEY
        case EdgeType::CANAL:
            return canalWidth / 2.0 + 1.2;  // canal width / 2 + ALLEY
        case EdgeType::HORIZON:
            return 0.0;  // No inset for horizon
        case EdgeType::NONE:
        default:
            return 1.2 / 2.0;  // Default is ALLEY / 2
    }
}

/**
 * Get inset distance for farms (different rules than urban wards)
 *
 * Reference: mfcg.js Farm.getAvailable lines 12606-12637
 */
inline double getFarmEdgeInset(EdgeType type, bool neighborIsFarm = false, double canalWidth = 0.0) {
    switch (type) {
        case EdgeType::ROAD:
            return 3.0;  // Larger setback from roads for farms
        case EdgeType::WALL:
            return 2.0 * 1.5;  // 2 * pc.THICKNESS
        case EdgeType::CANAL:
            return canalWidth / 2.0 + 1.2;
        case EdgeType::NONE:
            return neighborIsFarm ? 1.0 : 0.0;  // Buffer between farms, no buffer to other wards
        default:
            return 2.0;  // Default farm inset
    }
}

} // namespace building
} // namespace town_generator
