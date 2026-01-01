#pragma once

/**
 * GrassConstants.h - Unified grass system constants for C++
 *
 * Central location for all grass-related constants to avoid duplication
 * and ensure derived values stay consistent with their base values.
 *
 * NOTE: These values must match shaders/grass_constants.glsl
 */

#include <cstdint>

namespace GrassConstants {

// =============================================================================
// GRID AND INSTANCE CONFIGURATION
// =============================================================================

// Grid dimensions (1000x1000 = 1,000,000 potential blades)
inline constexpr uint32_t GRID_SIZE = 1000;

// Spacing between blades in world units (meters)
// With 0.2m spacing: 200m x 200m coverage, 25 blades per square meter
inline constexpr float SPACING = 0.2f;

// Derived: Total coverage area in each dimension
// GRID_SIZE * SPACING = 200m
inline constexpr float COVERAGE_SIZE = static_cast<float>(GRID_SIZE) * SPACING;

// Derived: Blades per square meter
// 1.0 / (SPACING * SPACING) = 25
inline constexpr float DENSITY = 1.0f / (SPACING * SPACING);

// Workgroup size for compute shaders (16x16 = 256 threads)
inline constexpr uint32_t WORKGROUP_SIZE = 16;

// Derived: Number of workgroups needed to cover grid
// ceil(GRID_SIZE / WORKGROUP_SIZE) = ceil(1000/16) = 63
inline constexpr uint32_t DISPATCH_SIZE = (GRID_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

// Maximum instances rendered after culling (~100k target like Ghost of Tsushima)
inline constexpr uint32_t MAX_INSTANCES = 100000;

// =============================================================================
// BLADE GEOMETRY
// =============================================================================

// Number of segments per blade (determines detail level)
inline constexpr uint32_t NUM_SEGMENTS = 7;

// Derived: Vertices per blade (2 per segment for strip + 1 tip vertex)
// (NUM_SEGMENTS * 2) + 1 = 15
inline constexpr uint32_t VERTICES_PER_BLADE = NUM_SEGMENTS * 2 + 1;

// Base width of grass blade at ground level (2cm)
inline constexpr float BASE_WIDTH = 0.02f;

// =============================================================================
// BLADE HEIGHT PROPERTIES
// =============================================================================

// Height range for blades (normalized 0-1 scale, actual height = h * heightScale)
inline constexpr float HEIGHT_MIN = 0.3f;
inline constexpr float HEIGHT_MAX = 0.7f;

// Derived: Height variation range
inline constexpr float HEIGHT_RANGE = HEIGHT_MAX - HEIGHT_MIN;

// =============================================================================
// DISPLACEMENT SYSTEM
// =============================================================================

// Displacement texture resolution (512x512 texels)
inline constexpr uint32_t DISPLACEMENT_TEXTURE_SIZE = 512;

// Displacement region coverage in world units (50m x 50m)
inline constexpr float DISPLACEMENT_REGION_SIZE = 50.0f;

// Derived: Texel size in world units
// DISPLACEMENT_REGION_SIZE / DISPLACEMENT_TEXTURE_SIZE = ~0.0977m
inline constexpr float DISPLACEMENT_TEXEL_SIZE = DISPLACEMENT_REGION_SIZE / static_cast<float>(DISPLACEMENT_TEXTURE_SIZE);

// Derived: Dispatch size for displacement compute shader
// DISPLACEMENT_TEXTURE_SIZE / WORKGROUP_SIZE = 32
inline constexpr uint32_t DISPLACEMENT_DISPATCH_SIZE = DISPLACEMENT_TEXTURE_SIZE / WORKGROUP_SIZE;

// Maximum displacement sources per frame (player, NPCs, etc.)
inline constexpr uint32_t MAX_DISPLACEMENT_SOURCES = 16;

// =============================================================================
// CULLING AND LOD
// =============================================================================

// Maximum draw distance (meters) - must be >= LOD1_DISTANCE_END to see all LOD levels
// With multi-LOD tiles: LOD0 up to 80m, LOD1 up to 200m, LOD2 beyond
inline constexpr float MAX_DRAW_DISTANCE = 250.0f;

// Legacy LOD transition zone (for additional blade dropping within tiles)
// This supplements the tile-based LOD system with smooth per-blade culling
inline constexpr float LOD_TRANSITION_START = 150.0f;
inline constexpr float LOD_TRANSITION_END = 250.0f;

// Maximum blade drop rate at far distance (50% - on top of tile LOD reduction)
inline constexpr float MAX_LOD_DROP_RATE = 0.5f;

// =============================================================================
// SHADOW PIPELINE
// =============================================================================

// Depth bias for shadow pipeline
inline constexpr float SHADOW_DEPTH_BIAS_CONSTANT = 0.25f;
inline constexpr float SHADOW_DEPTH_BIAS_SLOPE = 0.75f;

// =============================================================================
// TILED GRASS SYSTEM
// =============================================================================

// Number of LOD levels for variable tile sizes
// LOD 0: High detail (near camera) - smaller tiles, full density
// LOD 1: Medium detail - 2x tile size, 2x spacing (1/4 blade density)
// LOD 2: Low detail (far) - 4x tile size, 4x spacing (1/16 blade density)
inline constexpr uint32_t NUM_LOD_LEVELS = 3;

// Base tile dimensions in world units (64m x 64m per tile at LOD 0)
inline constexpr float TILE_SIZE_LOD0 = 64.0f;
inline constexpr float TILE_SIZE_LOD1 = 128.0f;  // 2x size
inline constexpr float TILE_SIZE_LOD2 = 256.0f;  // 4x size

// Legacy constant for compatibility
inline constexpr float TILE_SIZE = TILE_SIZE_LOD0;

// Spacing multipliers per LOD (blades spread out further at lower LOD)
inline constexpr float SPACING_MULT_LOD0 = 1.0f;
inline constexpr float SPACING_MULT_LOD1 = 2.0f;  // 2x spacing -> 1/4 density
inline constexpr float SPACING_MULT_LOD2 = 4.0f;  // 4x spacing -> 1/16 density

// Grid dimensions per tile (same grid size for all LODs - blades just spread out)
// 320x320 = 102,400 potential blades per tile
inline constexpr uint32_t TILE_GRID_SIZE = 320;

// Derived: Dispatch size per tile
// ceil(TILE_GRID_SIZE / WORKGROUP_SIZE) = ceil(320/16) = 20
inline constexpr uint32_t TILE_DISPATCH_SIZE = (TILE_GRID_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

// LOD distance thresholds (meters from camera to tile center)
// Tiles closer than LOD0_END use LOD 0
// Tiles between LOD0_END and LOD1_END use LOD 1
// Tiles beyond LOD1_END use LOD 2
inline constexpr float LOD0_DISTANCE_END = 80.0f;   // LOD 0 within 80m
inline constexpr float LOD1_DISTANCE_END = 200.0f;  // LOD 1 within 200m, LOD 2 beyond

// LOD transition zones for smooth blade dropping
// When approaching LOD boundary, progressively drop 3/4 of blades
// (Ghost of Tsushima: "high LOD tiles drop 3 out of every 4 grass blades
//  before they transition to the low LOD tiles")
inline constexpr float LOD_TRANSITION_ZONE = 20.0f;  // 20m transition zone
inline constexpr float LOD_TRANSITION_DROP_RATE = 0.75f;  // Drop 75% at boundary

// Number of tiles around camera per LOD level
inline constexpr uint32_t TILES_PER_AXIS_LOD0 = 3;  // 3x3 = 9 high-detail tiles
inline constexpr uint32_t TILES_PER_AXIS_LOD1 = 3;  // 3x3 = 9 medium tiles (ring around LOD0)
inline constexpr uint32_t TILES_PER_AXIS_LOD2 = 3;  // 3x3 = 9 low-detail tiles (outer ring)

// Legacy constant for compatibility
inline constexpr uint32_t TILES_PER_AXIS = TILES_PER_AXIS_LOD0;

// Total active tiles = sum of all LOD levels
inline constexpr uint32_t MAX_ACTIVE_TILES_LOD0 = TILES_PER_AXIS_LOD0 * TILES_PER_AXIS_LOD0;
inline constexpr uint32_t MAX_ACTIVE_TILES_LOD1 = TILES_PER_AXIS_LOD1 * TILES_PER_AXIS_LOD1;
inline constexpr uint32_t MAX_ACTIVE_TILES_LOD2 = TILES_PER_AXIS_LOD2 * TILES_PER_AXIS_LOD2;
inline constexpr uint32_t MAX_ACTIVE_TILES = MAX_ACTIVE_TILES_LOD0 + MAX_ACTIVE_TILES_LOD1 + MAX_ACTIVE_TILES_LOD2;

// Maximum rendered instances per tile after culling (~12k target)
inline constexpr uint32_t MAX_INSTANCES_PER_TILE = 12000;

// Derived: Total coverage with multi-LOD tiled system
// LOD2 outer edge: tiles extend much further than single-LOD system
inline constexpr float TILED_COVERAGE_LOD0 = static_cast<float>(TILES_PER_AXIS_LOD0) * TILE_SIZE_LOD0;
inline constexpr float TILED_COVERAGE_LOD1 = static_cast<float>(TILES_PER_AXIS_LOD1) * TILE_SIZE_LOD1;
inline constexpr float TILED_COVERAGE_LOD2 = static_cast<float>(TILES_PER_AXIS_LOD2) * TILE_SIZE_LOD2;
inline constexpr float TILED_COVERAGE = TILED_COVERAGE_LOD2;  // Max coverage is LOD2 extent

// Tile load/unload margins (hysteresis to prevent thrashing)
inline constexpr float TILE_LOAD_MARGIN = 10.0f;
inline constexpr float TILE_UNLOAD_MARGIN = 20.0f;

// Helper to get tile size for a given LOD level
inline constexpr float getTileSizeForLod(uint32_t lod) {
    return (lod == 0) ? TILE_SIZE_LOD0 :
           (lod == 1) ? TILE_SIZE_LOD1 : TILE_SIZE_LOD2;
}

// Helper to get spacing multiplier for a given LOD level
inline constexpr float getSpacingMultForLod(uint32_t lod) {
    return (lod == 0) ? SPACING_MULT_LOD0 :
           (lod == 1) ? SPACING_MULT_LOD1 : SPACING_MULT_LOD2;
}

} // namespace GrassConstants
