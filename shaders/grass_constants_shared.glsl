/*
 * grass_constants_shared.glsl - Cross-compiled grass constants
 *
 * This file is the SINGLE SOURCE OF TRUTH for grass system constants.
 * It can be included in both GLSL shaders and C++ code.
 *
 * Usage in GLSL:
 *   #include "grass_constants_shared.glsl"
 *
 * Usage in C++:
 *   #define GLSL_TO_CPP
 *   #include "grass_constants_shared.glsl"
 */

#ifndef GRASS_CONSTANTS_SHARED_GLSL
#define GRASS_CONSTANTS_SHARED_GLSL

// =============================================================================
// Cross-compilation macros
// =============================================================================
#ifdef GLSL_TO_CPP
    // C++ mode
    #define CONST_UINT(name, value) inline constexpr uint32_t name = value
    #define CONST_FLOAT(name, value) inline constexpr float name = value##f
    #define CONST_UINT_DERIVED(name, expr) inline constexpr uint32_t name = expr
    #define CONST_FLOAT_DERIVED(name, expr) inline constexpr float name = expr
#else
    // GLSL mode
    #define CONST_UINT(name, value) const uint name = value
    #define CONST_FLOAT(name, value) const float name = value
    #define CONST_UINT_DERIVED(name, expr) const uint name = expr
    #define CONST_FLOAT_DERIVED(name, expr) const float name = expr
#endif

// =============================================================================
// INSTANCE BUDGET (Primary constraint - everything else derives from this)
// =============================================================================

// Total instance buffer size (~100k target like Ghost of Tsushima)
CONST_UINT(GRASS_MAX_INSTANCES, 100000);

// Fraction of budget allocated to LOD 0 tiles (rest for LOD 1/2)
CONST_FLOAT(GRASS_LOD0_BUDGET_FRACTION, 0.8);

// =============================================================================
// BLADE GEOMETRY
// =============================================================================

CONST_FLOAT(GRASS_SPACING, 0.2);              // Base blade spacing (meters)
CONST_FLOAT(GRASS_JITTER_FACTOR, 0.8);        // Position jitter as fraction of spacing
CONST_UINT(GRASS_WORKGROUP_SIZE, 16);         // Compute workgroup size (16x16)
CONST_UINT(GRASS_NUM_SEGMENTS, 7);            // Blade segments
CONST_FLOAT(GRASS_BASE_WIDTH, 0.02);          // Blade base width (2cm)
CONST_FLOAT(GRASS_HEIGHT_MIN, 0.3);           // Min height (normalized)
CONST_FLOAT(GRASS_HEIGHT_MAX, 0.7);           // Max height (normalized)

// Derived
CONST_UINT_DERIVED(GRASS_VERTICES_PER_BLADE, GRASS_NUM_SEGMENTS * 2 + 1);
CONST_FLOAT_DERIVED(GRASS_HEIGHT_RANGE, GRASS_HEIGHT_MAX - GRASS_HEIGHT_MIN);

// =============================================================================
// TILE-BASED GRASS DISPATCH CONFIGURATION
// =============================================================================

// Tile grid size - each tile covers a square area of the world
// 144 x 144 grid at 0.2m spacing = 28.8m x 28.8m per tile
// Multiple tiles (e.g., 3x3) are dispatched around the camera for coverage
CONST_UINT(GRASS_TILE_GRID_SIZE, 144);

// Dispatch size in workgroups per tile
CONST_UINT_DERIVED(GRASS_TILE_DISPATCH_SIZE, (GRASS_TILE_GRID_SIZE + GRASS_WORKGROUP_SIZE - 1) / GRASS_WORKGROUP_SIZE);

// Tile size in world units
#ifdef GLSL_TO_CPP
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE, static_cast<float>(GRASS_TILE_GRID_SIZE) * GRASS_SPACING);
#else
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE, float(GRASS_TILE_GRID_SIZE) * GRASS_SPACING);
#endif

// Legacy aliases for compatibility
CONST_UINT_DERIVED(GRASS_GRID_SIZE, GRASS_TILE_GRID_SIZE);
CONST_UINT_DERIVED(GRASS_DISPATCH_SIZE, GRASS_TILE_DISPATCH_SIZE);
CONST_FLOAT_DERIVED(GRASS_COVERAGE_HALF_EXTENT, GRASS_TILE_SIZE * 1.5);

// =============================================================================
// CONTINUOUS STOCHASTIC CULLING
// =============================================================================

// Distance-based density falloff for smooth LOD transitions
// Full density up to CULL_START_DISTANCE, then linear falloff to zero at CULL_END_DISTANCE
CONST_FLOAT(GRASS_CULL_START_DISTANCE, 20.0);   // Full density within this range
CONST_FLOAT(GRASS_CULL_END_DISTANCE, 100.0);    // Zero density beyond this range
CONST_FLOAT(GRASS_CULL_POWER, 2.0);             // Power curve for falloff (2.0 = quadratic)

// Legacy compatibility aliases (for code that still references tile system)
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE_LOD0, GRASS_TILE_SIZE);
CONST_FLOAT(GRASS_SPACING_MULT_LOD0, 1.0);
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE, GRASS_TILE_SIZE * 3.0);

// =============================================================================
// CULLING
// =============================================================================

CONST_FLOAT(GRASS_FRUSTUM_MARGIN, 15.0);

// Max draw distance - derived from continuous culling end distance
CONST_FLOAT_DERIVED(GRASS_MAX_DRAW_DISTANCE, GRASS_CULL_END_DISTANCE);

// Legacy aliases for compatibility
CONST_FLOAT_DERIVED(GRASS_LOD0_DISTANCE_END, GRASS_CULL_START_DISTANCE);
CONST_FLOAT_DERIVED(GRASS_LOD1_DISTANCE_END, GRASS_CULL_END_DISTANCE * 0.6);
CONST_FLOAT(GRASS_LOD_TRANSITION_ZONE, 10.0);
CONST_FLOAT(GRASS_LOD_TRANSITION_DROP_RATE, 0.75);
CONST_FLOAT(GRASS_LOD_HYSTERESIS, 0.1);

// =============================================================================
// CLUMPING
// =============================================================================

CONST_FLOAT(GRASS_CLUMP_SCALE, 2.0);
CONST_FLOAT(GRASS_CLUMP_HEIGHT_INFLUENCE, 0.4);
CONST_FLOAT(GRASS_CLUMP_FACING_INFLUENCE, 0.3);
CONST_FLOAT(GRASS_CLUMP_TILT_MODIFIER, 0.15);
CONST_FLOAT(GRASS_TILT_RANGE, 0.3);

// =============================================================================
// DISPLACEMENT
// =============================================================================

CONST_UINT(GRASS_DISPLACEMENT_TEXTURE_SIZE, 512);
CONST_FLOAT(GRASS_DISPLACEMENT_REGION_SIZE, 50.0);
CONST_FLOAT(GRASS_DISPLACEMENT_BLEND_MAX, 0.9);
CONST_FLOAT(GRASS_DISPLACEMENT_THRESHOLD, 0.01);

// =============================================================================
// LEGACY TILE STREAMING (kept for compatibility)
// =============================================================================

CONST_FLOAT(GRASS_TILE_LOAD_MARGIN, 10.0);
CONST_FLOAT(GRASS_TILE_UNLOAD_MARGIN, 20.0);
CONST_FLOAT(GRASS_TILE_FADE_IN_DURATION, 0.75);

// =============================================================================
// RENDERING
// =============================================================================

CONST_FLOAT(GRASS_TWO_PI, 6.28318530718);

// Legacy aliases
CONST_UINT(GRASS_TILES_PER_AXIS, 1);
CONST_UINT_DERIVED(GRASS_MAX_INSTANCES_PER_TILE, GRASS_MAX_INSTANCES);
CONST_UINT(GRASS_NUM_LOD_LEVELS, 1);
CONST_UINT(GRASS_MAX_ACTIVE_TILES, 1);

// Cleanup macros for GLSL (C++ doesn't need this)
#ifndef GLSL_TO_CPP
#undef CONST_UINT
#undef CONST_FLOAT
#undef CONST_UINT_DERIVED
#undef CONST_FLOAT_DERIVED
#endif

#endif // GRASS_CONSTANTS_SHARED_GLSL
