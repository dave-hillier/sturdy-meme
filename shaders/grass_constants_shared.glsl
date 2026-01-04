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
// TILED GRASS SYSTEM - All derived from budget
// =============================================================================

CONST_UINT(GRASS_NUM_LOD_LEVELS, 3);

// ----- PRIMARY: Tile layout configuration -----
CONST_UINT(GRASS_TILES_PER_AXIS_LOD0, 3);     // 3x3 = 9 high-detail tiles
CONST_UINT(GRASS_TILES_PER_AXIS_LOD1, 3);     // 3x3 = 9 medium tiles
CONST_UINT(GRASS_TILES_PER_AXIS_LOD2, 3);     // 3x3 = 9 low-detail tiles

// ----- DERIVED: Total tiles per LOD -----
CONST_UINT_DERIVED(GRASS_MAX_ACTIVE_TILES_LOD0, GRASS_TILES_PER_AXIS_LOD0 * GRASS_TILES_PER_AXIS_LOD0);
CONST_UINT_DERIVED(GRASS_MAX_ACTIVE_TILES_LOD1, GRASS_TILES_PER_AXIS_LOD1 * GRASS_TILES_PER_AXIS_LOD1);
CONST_UINT_DERIVED(GRASS_MAX_ACTIVE_TILES_LOD2, GRASS_TILES_PER_AXIS_LOD2 * GRASS_TILES_PER_AXIS_LOD2);
CONST_UINT_DERIVED(GRASS_MAX_ACTIVE_TILES, GRASS_MAX_ACTIVE_TILES_LOD0 + GRASS_MAX_ACTIVE_TILES_LOD1 + GRASS_MAX_ACTIVE_TILES_LOD2);

// ----- DERIVED: Instance budget per tile -----
// LOD 0 gets 80% of budget, split evenly among its tiles
// 100000 * 0.8 / 9 = ~8,888 instances per LOD 0 tile after culling
#ifdef GLSL_TO_CPP
CONST_UINT_DERIVED(GRASS_LOD0_BUDGET, static_cast<uint32_t>(GRASS_MAX_INSTANCES * GRASS_LOD0_BUDGET_FRACTION));
#else
CONST_UINT_DERIVED(GRASS_LOD0_BUDGET, uint(float(GRASS_MAX_INSTANCES) * GRASS_LOD0_BUDGET_FRACTION));
#endif
CONST_UINT_DERIVED(GRASS_INSTANCES_PER_TILE_LOD0, GRASS_LOD0_BUDGET / GRASS_MAX_ACTIVE_TILES_LOD0);

// ----- DERIVED: Grid size from budget -----
// Assuming ~50% visible after culling, need 2x potential. sqrt(8888*2) ≈ 133
// Round to multiple of 16 for workgroup efficiency
CONST_UINT(GRASS_TILE_GRID_SIZE, 144);

// ----- DERIVED: Dispatch size -----
CONST_UINT_DERIVED(GRASS_TILE_DISPATCH_SIZE, (GRASS_TILE_GRID_SIZE + GRASS_WORKGROUP_SIZE - 1) / GRASS_WORKGROUP_SIZE);

// ----- DERIVED: Tile size from grid size -----
// Tile must be exactly GRID_SIZE * SPACING to avoid gaps/overlaps
#ifdef GLSL_TO_CPP
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE_LOD0, static_cast<float>(GRASS_TILE_GRID_SIZE) * GRASS_SPACING);
#else
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE_LOD0, float(GRASS_TILE_GRID_SIZE) * GRASS_SPACING);
#endif

// ----- DERIVED: Tile sizes for higher LODs (2x, 4x) -----
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE_LOD1, GRASS_TILE_SIZE_LOD0 * 2.0);
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE_LOD2, GRASS_TILE_SIZE_LOD0 * 4.0);
CONST_FLOAT_DERIVED(GRASS_TILE_SIZE, GRASS_TILE_SIZE_LOD0);

// ----- DERIVED: Spacing multipliers -----
CONST_FLOAT(GRASS_SPACING_MULT_LOD0, 1.0);
CONST_FLOAT(GRASS_SPACING_MULT_LOD1, 2.0);
CONST_FLOAT(GRASS_SPACING_MULT_LOD2, 4.0);

// ----- DERIVED: Coverage area -----
#ifdef GLSL_TO_CPP
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE_LOD0, static_cast<float>(GRASS_TILES_PER_AXIS_LOD0) * GRASS_TILE_SIZE_LOD0);
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE_LOD1, static_cast<float>(GRASS_TILES_PER_AXIS_LOD1) * GRASS_TILE_SIZE_LOD1);
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE_LOD2, static_cast<float>(GRASS_TILES_PER_AXIS_LOD2) * GRASS_TILE_SIZE_LOD2);
#else
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE_LOD0, float(GRASS_TILES_PER_AXIS_LOD0) * GRASS_TILE_SIZE_LOD0);
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE_LOD1, float(GRASS_TILES_PER_AXIS_LOD1) * GRASS_TILE_SIZE_LOD1);
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE_LOD2, float(GRASS_TILES_PER_AXIS_LOD2) * GRASS_TILE_SIZE_LOD2);
#endif
CONST_FLOAT_DERIVED(GRASS_TILED_COVERAGE, GRASS_TILED_COVERAGE_LOD2);

// =============================================================================
// CULLING AND LOD
// =============================================================================

CONST_FLOAT(GRASS_FRUSTUM_MARGIN, 15.0);

// LOD distance thresholds derived from tile coverage
// Each LOD extends half its coverage from camera center
// LOD 0: 86.4m coverage -> 43.2m from center
// LOD 1: 172.8m coverage -> 86.4m from center
CONST_FLOAT_DERIVED(GRASS_LOD0_DISTANCE_END, GRASS_TILED_COVERAGE_LOD0 * 0.5);
CONST_FLOAT_DERIVED(GRASS_LOD1_DISTANCE_END, GRASS_TILED_COVERAGE_LOD1 * 0.5);
CONST_FLOAT_DERIVED(GRASS_MAX_DRAW_DISTANCE, GRASS_TILED_COVERAGE_LOD2 * 0.5);

// Transition zone as fraction of LOD 0 coverage (~10m for smooth blend)
CONST_FLOAT_DERIVED(GRASS_LOD_TRANSITION_ZONE, GRASS_TILE_SIZE_LOD0 * 0.35);
CONST_FLOAT(GRASS_LOD_TRANSITION_DROP_RATE, 0.75);

// Hysteresis to prevent flickering at LOD boundaries
// Each blade gets a random offset (±half this value) to stagger transitions
// This prevents all blades at the same distance from flickering together
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
// TILE STREAMING
// =============================================================================

CONST_FLOAT(GRASS_TILE_LOAD_MARGIN, 10.0);
CONST_FLOAT(GRASS_TILE_UNLOAD_MARGIN, 20.0);

// Fade-in duration when a new tile loads (seconds)
// Grass blades stochastically appear over this duration to prevent popping
// Longer duration = smoother fade but more visible transition
CONST_FLOAT(GRASS_TILE_FADE_IN_DURATION, 0.75);

// =============================================================================
// RENDERING
// =============================================================================

CONST_FLOAT(GRASS_TWO_PI, 6.28318530718);

// Legacy aliases
CONST_UINT_DERIVED(GRASS_TILES_PER_AXIS, GRASS_TILES_PER_AXIS_LOD0);
CONST_UINT_DERIVED(GRASS_MAX_INSTANCES_PER_TILE, GRASS_INSTANCES_PER_TILE_LOD0);

// Cleanup macros for GLSL (C++ doesn't need this)
#ifndef GLSL_TO_CPP
#undef CONST_UINT
#undef CONST_FLOAT
#undef CONST_UINT_DERIVED
#undef CONST_FLOAT_DERIVED
#endif

#endif // GRASS_CONSTANTS_SHARED_GLSL
