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

// Maximum draw distance (meters)
inline constexpr float MAX_DRAW_DISTANCE = 50.0f;

// LOD transition zone
inline constexpr float LOD_TRANSITION_START = 30.0f;
inline constexpr float LOD_TRANSITION_END = 50.0f;

// Maximum blade drop rate at far LOD (50%)
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

// Tile dimensions in world units (64m x 64m per tile)
inline constexpr float TILE_SIZE = 64.0f;

// Grid dimensions per tile (320x320 = 102,400 potential blades per tile)
// With 0.2m spacing: maintains 25 blades per square meter density
inline constexpr uint32_t TILE_GRID_SIZE = 320;

// Derived: Dispatch size per tile
// ceil(TILE_GRID_SIZE / WORKGROUP_SIZE) = ceil(320/16) = 20
inline constexpr uint32_t TILE_DISPATCH_SIZE = (TILE_GRID_SIZE + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;

// Number of tiles around camera in each direction (3x3 = 9 active tiles)
inline constexpr uint32_t TILES_PER_AXIS = 3;

// Total active tiles = TILES_PER_AXIS^2 = 9
inline constexpr uint32_t MAX_ACTIVE_TILES = TILES_PER_AXIS * TILES_PER_AXIS;

// Maximum rendered instances per tile after culling (~12k target)
inline constexpr uint32_t MAX_INSTANCES_PER_TILE = 12000;

// Derived: Total coverage with tiled system
// TILES_PER_AXIS * TILE_SIZE = 192m
inline constexpr float TILED_COVERAGE = static_cast<float>(TILES_PER_AXIS) * TILE_SIZE;

// Tile load/unload margins (hysteresis to prevent thrashing)
inline constexpr float TILE_LOAD_MARGIN = 10.0f;
inline constexpr float TILE_UNLOAD_MARGIN = 20.0f;

} // namespace GrassConstants
