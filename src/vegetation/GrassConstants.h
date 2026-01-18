#pragma once

/**
 * GrassConstants.h - Unified grass system constants for C++
 *
 * This file includes the shared constants from grass_constants_shared.glsl
 * which is the single source of truth for both C++ and GLSL.
 *
 * C++-specific derived values are added below the include.
 */

#include <cstdint>

namespace GrassConstants {

// Include shared constants (single source of truth)
// Define GLSL_TO_CPP to enable C++ syntax in the shared header
#define GLSL_TO_CPP
#include "../../shaders/grass_constants_shared.glsl"
#undef GLSL_TO_CPP

// =============================================================================
// C++-SPECIFIC DERIVED VALUES
// =============================================================================

// Grid configuration (mirrors GRASS_TILE_GRID_SIZE and GRASS_TILE_DISPATCH_SIZE from shared)
inline constexpr uint32_t GRID_SIZE = GRASS_GRID_SIZE;
inline constexpr uint32_t DISPATCH_SIZE = GRASS_DISPATCH_SIZE;
inline constexpr uint32_t TILE_GRID_SIZE = GRASS_TILE_GRID_SIZE;
inline constexpr uint32_t TILE_DISPATCH_SIZE = GRASS_TILE_DISPATCH_SIZE;
inline constexpr float TILE_SIZE = GRASS_TILE_SIZE;
inline constexpr float COVERAGE_SIZE = static_cast<float>(GRID_SIZE) * GRASS_SPACING;
inline constexpr float COVERAGE_HALF_EXTENT = GRASS_COVERAGE_HALF_EXTENT;
inline constexpr float DENSITY = 1.0f / (GRASS_SPACING * GRASS_SPACING);

// Continuous stochastic culling parameters
inline constexpr float CULL_START_DISTANCE = GRASS_CULL_START_DISTANCE;
inline constexpr float CULL_END_DISTANCE = GRASS_CULL_END_DISTANCE;
inline constexpr float CULL_POWER = GRASS_CULL_POWER;

// Blade geometry derived
inline constexpr float WIDTH_TAPER = 0.9f;

// Displacement derived
inline constexpr float DISPLACEMENT_TEXEL_SIZE = GRASS_DISPLACEMENT_REGION_SIZE / static_cast<float>(GRASS_DISPLACEMENT_TEXTURE_SIZE);
inline constexpr uint32_t DISPLACEMENT_DISPATCH_SIZE = GRASS_DISPLACEMENT_TEXTURE_SIZE / GRASS_WORKGROUP_SIZE;
inline constexpr uint32_t MAX_DISPLACEMENT_SOURCES = 16;

// Shadow pipeline
inline constexpr float SHADOW_DEPTH_BIAS_CONSTANT = 0.25f;
inline constexpr float SHADOW_DEPTH_BIAS_SLOPE = 0.75f;

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Get tile size for a given LOD level (legacy - returns same value for all LODs)
 */
inline constexpr float getTileSizeForLod(uint32_t /*lod*/) {
    return GRASS_TILE_SIZE;
}

/**
 * Get spacing multiplier for a given LOD level (legacy - always 1.0)
 */
inline constexpr float getSpacingMultForLod(uint32_t /*lod*/) {
    return GRASS_SPACING_MULT_LOD0;
}

/**
 * Get tiles per axis for a given LOD level (legacy - always 1)
 */
inline constexpr uint32_t getTilesPerAxisForLod(uint32_t /*lod*/) {
    return GRASS_TILES_PER_AXIS;
}

// =============================================================================
// LEGACY ALIASES (without GRASS_ prefix for backward compatibility)
// =============================================================================

inline constexpr uint32_t WORKGROUP_SIZE = GRASS_WORKGROUP_SIZE;
inline constexpr uint32_t MAX_INSTANCES = GRASS_MAX_INSTANCES;
inline constexpr uint32_t NUM_SEGMENTS = GRASS_NUM_SEGMENTS;
inline constexpr uint32_t VERTICES_PER_BLADE = GRASS_VERTICES_PER_BLADE;
inline constexpr float SPACING = GRASS_SPACING;
inline constexpr float BASE_WIDTH = GRASS_BASE_WIDTH;
inline constexpr float HEIGHT_MIN = GRASS_HEIGHT_MIN;
inline constexpr float HEIGHT_MAX = GRASS_HEIGHT_MAX;
inline constexpr float HEIGHT_RANGE = GRASS_HEIGHT_RANGE;
inline constexpr float MAX_DRAW_DISTANCE = GRASS_MAX_DRAW_DISTANCE;
inline constexpr uint32_t DISPLACEMENT_TEXTURE_SIZE = GRASS_DISPLACEMENT_TEXTURE_SIZE;
inline constexpr float DISPLACEMENT_REGION_SIZE = GRASS_DISPLACEMENT_REGION_SIZE;
inline constexpr uint32_t NUM_LOD_LEVELS = GRASS_NUM_LOD_LEVELS;
inline constexpr float TILE_SIZE_LOD0 = GRASS_TILE_SIZE_LOD0;
inline constexpr float TILE_SIZE_LOD1 = GRASS_TILE_SIZE_LOD0;  // Legacy: same as LOD0
inline constexpr float TILE_SIZE_LOD2 = GRASS_TILE_SIZE_LOD0;  // Legacy: same as LOD0
inline constexpr float SPACING_MULT_LOD0 = GRASS_SPACING_MULT_LOD0;
inline constexpr float SPACING_MULT_LOD1 = 1.0f;  // Legacy: no LOD multiplier
inline constexpr float SPACING_MULT_LOD2 = 1.0f;  // Legacy: no LOD multiplier
inline constexpr float LOD0_DISTANCE_END = GRASS_LOD0_DISTANCE_END;
inline constexpr float LOD1_DISTANCE_END = GRASS_LOD1_DISTANCE_END;
inline constexpr float LOD_TRANSITION_ZONE = GRASS_LOD_TRANSITION_ZONE;
inline constexpr float LOD_TRANSITION_DROP_RATE = GRASS_LOD_TRANSITION_DROP_RATE;
inline constexpr uint32_t TILES_PER_AXIS_LOD0 = 3;  // 3x3 grid of tiles
inline constexpr uint32_t TILES_PER_AXIS_LOD1 = 3;
inline constexpr uint32_t TILES_PER_AXIS_LOD2 = 3;
inline constexpr uint32_t TILES_PER_AXIS = GRASS_TILES_PER_AXIS;
inline constexpr uint32_t MAX_ACTIVE_TILES_LOD0 = 9;
inline constexpr uint32_t MAX_ACTIVE_TILES_LOD1 = 9;
inline constexpr uint32_t MAX_ACTIVE_TILES_LOD2 = 9;
inline constexpr uint32_t MAX_ACTIVE_TILES = GRASS_MAX_ACTIVE_TILES;
inline constexpr uint32_t MAX_INSTANCES_PER_TILE = GRASS_MAX_INSTANCES_PER_TILE;
inline constexpr float TILED_COVERAGE_LOD0 = GRASS_TILED_COVERAGE;
inline constexpr float TILED_COVERAGE_LOD1 = GRASS_TILED_COVERAGE;
inline constexpr float TILED_COVERAGE_LOD2 = GRASS_TILED_COVERAGE;
inline constexpr float TILED_COVERAGE = GRASS_TILED_COVERAGE;
inline constexpr float TILE_LOAD_MARGIN = GRASS_TILE_LOAD_MARGIN;
inline constexpr float TILE_UNLOAD_MARGIN = GRASS_TILE_UNLOAD_MARGIN;

} // namespace GrassConstants
