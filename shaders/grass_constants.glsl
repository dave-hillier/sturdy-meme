/*
 * grass_constants.glsl - Unified grass system constants
 *
 * Central location for all grass-related constants to avoid duplication
 * and ensure derived values stay consistent with their base values.
 *
 * Include this file in all grass shaders that need these constants.
 */

#ifndef GRASS_CONSTANTS_GLSL
#define GRASS_CONSTANTS_GLSL

// =============================================================================
// GRID AND INSTANCE CONFIGURATION
// =============================================================================

// Grid dimensions (1000x1000 = 1,000,000 potential blades)
const uint GRASS_GRID_SIZE = 1000;

// Spacing between blades in world units (meters)
// With 0.2m spacing: 200m x 200m coverage, 25 blades per square meter
const float GRASS_SPACING = 0.2;

// Derived: Total coverage area in each dimension
// GRASS_GRID_SIZE * GRASS_SPACING = 200m
const float GRASS_COVERAGE_SIZE = float(GRASS_GRID_SIZE) * GRASS_SPACING;

// Derived: Blades per square meter
// 1.0 / (GRASS_SPACING * GRASS_SPACING) = 25
const float GRASS_DENSITY = 1.0 / (GRASS_SPACING * GRASS_SPACING);

// Position jitter as fraction of spacing (80%)
const float GRASS_JITTER_FACTOR = 0.8;

// Workgroup size for compute shaders (16x16 = 256 threads)
const uint GRASS_WORKGROUP_SIZE = 16;

// Derived: Number of workgroups needed to cover grid
// ceil(GRASS_GRID_SIZE / GRASS_WORKGROUP_SIZE) = ceil(1000/16) = 63
const uint GRASS_DISPATCH_SIZE = (GRASS_GRID_SIZE + GRASS_WORKGROUP_SIZE - 1) / GRASS_WORKGROUP_SIZE;

// Maximum instances rendered after culling (~100k target like Ghost of Tsushima)
const uint GRASS_MAX_INSTANCES = 100000;

// =============================================================================
// BLADE GEOMETRY
// =============================================================================

// Number of segments per blade (determines detail level)
const uint GRASS_NUM_SEGMENTS = 7;

// Derived: Vertices per blade (2 per segment for strip + 1 tip vertex)
// (GRASS_NUM_SEGMENTS * 2) + 1 = 15
const uint GRASS_VERTICES_PER_BLADE = GRASS_NUM_SEGMENTS * 2 + 1;

// Derived: Tip vertex index (for width=0 check)
const uint GRASS_TIP_VERTEX_INDEX = GRASS_VERTICES_PER_BLADE - 1;

// Base width of grass blade at ground level (2cm)
const float GRASS_BASE_WIDTH = 0.02;

// Width taper from base to tip (90% taper)
const float GRASS_WIDTH_TAPER = 0.9;

// =============================================================================
// BLADE HEIGHT PROPERTIES
// =============================================================================

// Height range for blades (normalized 0-1 scale, actual height = h * heightScale)
const float GRASS_HEIGHT_MIN = 0.3;
const float GRASS_HEIGHT_MAX = 0.7;

// Derived: Height variation range
const float GRASS_HEIGHT_RANGE = GRASS_HEIGHT_MAX - GRASS_HEIGHT_MIN;

// Blade fold/droop parameters (short grass folds more)
const float GRASS_FOLD_MIN = 0.1;    // Tall grass fold amount
const float GRASS_FOLD_MAX = 0.6;    // Short grass fold amount
const float GRASS_DROOP_MAX = 0.3;   // Maximum droop factor for shortest grass

// Tilt range (random tilt in radians)
const float GRASS_TILT_RANGE = 0.3;

// =============================================================================
// DISPLACEMENT SYSTEM
// =============================================================================

// Displacement texture resolution (512x512 texels)
const uint GRASS_DISPLACEMENT_TEXTURE_SIZE = 512;

// Displacement region coverage in world units (50m x 50m)
const float GRASS_DISPLACEMENT_REGION_SIZE = 50.0;

// Derived: Texel size in world units
// GRASS_DISPLACEMENT_REGION_SIZE / GRASS_DISPLACEMENT_TEXTURE_SIZE = ~0.0977m
const float GRASS_DISPLACEMENT_TEXEL_SIZE = GRASS_DISPLACEMENT_REGION_SIZE / float(GRASS_DISPLACEMENT_TEXTURE_SIZE);

// Derived: Dispatch size for displacement compute shader
// GRASS_DISPLACEMENT_TEXTURE_SIZE / GRASS_WORKGROUP_SIZE = 32
const uint GRASS_DISPLACEMENT_DISPATCH_SIZE = GRASS_DISPLACEMENT_TEXTURE_SIZE / GRASS_WORKGROUP_SIZE;

// Maximum displacement sources per frame (player, NPCs, etc.)
const uint GRASS_MAX_DISPLACEMENT_SOURCES = 16;

// Displacement blend factor (how much displacement affects facing)
const float GRASS_DISPLACEMENT_BLEND_MAX = 0.9;

// Displacement magnitude threshold
const float GRASS_DISPLACEMENT_THRESHOLD = 0.01;

// =============================================================================
// CULLING AND LOD
// =============================================================================

// Maximum draw distance (meters)
const float GRASS_MAX_DRAW_DISTANCE = 50.0;

// LOD transition zone
const float GRASS_LOD_TRANSITION_START = 30.0;
const float GRASS_LOD_TRANSITION_END = 50.0;

// Maximum blade drop rate at far LOD (50%)
const float GRASS_MAX_LOD_DROP_RATE = 0.5;

// Frustum culling margin (accounts for double-buffer lag and shadow casting)
const float GRASS_FRUSTUM_MARGIN = 15.0;

// =============================================================================
// CLUMPING PARAMETERS
// =============================================================================

// Size of clumps in world units
const float GRASS_CLUMP_SCALE = 2.0;

// How much clump affects blade height (0-1)
const float GRASS_CLUMP_HEIGHT_INFLUENCE = 0.4;

// How much blades face toward/away from clump center (0-1)
const float GRASS_CLUMP_FACING_INFLUENCE = 0.3;

// Tilt modifier from clump distance
const float GRASS_CLUMP_TILT_MODIFIER = 0.15;

// =============================================================================
// WIND PARAMETERS
// =============================================================================

// Wind effect multiplier
const float GRASS_WIND_EFFECT_MULTIPLIER = 0.25;

// Wind phase offset multiplier
const float GRASS_WIND_PHASE_MULTIPLIER = 0.25;

// Wind base frequency (10m wavelength)
const float GRASS_WIND_BASE_FREQ = 0.1;

// Multi-octave frequency multipliers
const float GRASS_WIND_OCTAVE2_MULT = 2.0;
const float GRASS_WIND_OCTAVE3_MULT = 4.0;

// Multi-octave weights
const float GRASS_WIND_OCTAVE1_WEIGHT = 0.7;
const float GRASS_WIND_OCTAVE2_WEIGHT = 0.2;
const float GRASS_WIND_OCTAVE3_WEIGHT = 0.1;

// =============================================================================
// BLADE COLORS
// =============================================================================

// Base color (dark green at blade base)
const vec3 GRASS_COLOR_BASE = vec3(0.08, 0.22, 0.04);

// Tip color (lighter green at blade tip)
const vec3 GRASS_COLOR_TIP = vec3(0.35, 0.65, 0.18);

// Clump color variation influence (subtle)
const float GRASS_CLUMP_COLOR_INFLUENCE = 0.15;

// Warm/cool color shifts for clump variation
const vec3 GRASS_COLOR_SHIFT_WARM = vec3(0.05, 0.03, -0.02);
const vec3 GRASS_COLOR_SHIFT_COOL = vec3(-0.02, 0.02, 0.03);

// Brightness variation range per clump
const float GRASS_BRIGHTNESS_MIN = 0.9;
const float GRASS_BRIGHTNESS_MAX = 1.1;

// =============================================================================
// MATERIAL PROPERTIES
// =============================================================================

// Surface roughness (fairly matte)
const float GRASS_ROUGHNESS = 0.7;

// Subsurface scattering intensity
const float GRASS_SSS_STRENGTH = 0.35;

// Specular highlight intensity (subtle)
const float GRASS_SPECULAR_STRENGTH = 0.15;

// Two-sided diffuse multiplier (for backlit blades)
const float GRASS_BACKLIT_DIFFUSE = 0.6;

// =============================================================================
// RENDERING ENHANCEMENTS
// =============================================================================

// Edge-on view thickening multiplier
const float GRASS_EDGE_THICKEN_MAX = 3.0;
const float GRASS_EDGE_THICKEN_FACTOR = 2.0;

// Rim lighting parameters
const float GRASS_RIM_FRESNEL_POWER = 4.0;
const float GRASS_RIM_INTENSITY = 0.15;

// Ambient occlusion range (darker at base)
const float GRASS_AO_BASE = 0.4;
const float GRASS_AO_TIP = 1.0;

// Shadow tip discard threshold (top 5%)
const float GRASS_SHADOW_TIP_THRESHOLD = 0.95;

// Depth bias for shadow pipeline
const float GRASS_SHADOW_DEPTH_BIAS_CONSTANT = 0.25;
const float GRASS_SHADOW_DEPTH_BIAS_SLOPE = 0.75;

// =============================================================================
// TILED GRASS SYSTEM
// =============================================================================

// Number of LOD levels for variable tile sizes
// LOD 0: High detail (near camera) - smaller tiles, full density
// LOD 1: Medium detail - 2x tile size, 2x spacing (1/4 blade density)
// LOD 2: Low detail (far) - 4x tile size, 4x spacing (1/16 blade density)
const uint GRASS_NUM_LOD_LEVELS = 3;

// Tile dimensions per LOD level in world units
const float GRASS_TILE_SIZE_LOD0 = 64.0;   // High detail: 64m tiles
const float GRASS_TILE_SIZE_LOD1 = 128.0;  // Medium: 128m tiles (2x)
const float GRASS_TILE_SIZE_LOD2 = 256.0;  // Low detail: 256m tiles (4x)

// Legacy constant for compatibility
const float GRASS_TILE_SIZE = GRASS_TILE_SIZE_LOD0;

// Spacing multipliers per LOD (blades spread out further at lower LOD)
const float GRASS_SPACING_MULT_LOD0 = 1.0;  // Full density
const float GRASS_SPACING_MULT_LOD1 = 2.0;  // 2x spacing -> 1/4 density
const float GRASS_SPACING_MULT_LOD2 = 4.0;  // 4x spacing -> 1/16 density

// Grid dimensions per tile (same for all LODs - blades just spread out)
// 320x320 = 102,400 potential blades per tile
const uint GRASS_TILE_GRID_SIZE = 320;

// Derived: Dispatch size per tile
// ceil(GRASS_TILE_GRID_SIZE / GRASS_WORKGROUP_SIZE) = ceil(320/16) = 20
const uint GRASS_TILE_DISPATCH_SIZE = (GRASS_TILE_GRID_SIZE + GRASS_WORKGROUP_SIZE - 1) / GRASS_WORKGROUP_SIZE;

// LOD distance thresholds (meters from camera to tile center)
const float GRASS_LOD0_DISTANCE_END = 80.0;   // LOD 0 within 80m
const float GRASS_LOD1_DISTANCE_END = 200.0;  // LOD 1 within 200m, LOD 2 beyond

// LOD transition zones for smooth blade dropping
// When approaching LOD boundary, progressively drop 3/4 of blades
// (Ghost of Tsushima: "high LOD tiles drop 3 out of every 4 grass blades
//  before they transition to the low LOD tiles")
const float GRASS_LOD_TRANSITION_ZONE = 20.0;     // 20m transition zone
const float GRASS_LOD_TRANSITION_DROP_RATE = 0.75; // Drop 75% at boundary

// Number of tiles around camera per LOD level
const uint GRASS_TILES_PER_AXIS_LOD0 = 3;  // 3x3 = 9 high-detail tiles
const uint GRASS_TILES_PER_AXIS_LOD1 = 3;  // 3x3 = 9 medium tiles
const uint GRASS_TILES_PER_AXIS_LOD2 = 3;  // 3x3 = 9 low-detail tiles

// Legacy constant for compatibility
const uint GRASS_TILES_PER_AXIS = GRASS_TILES_PER_AXIS_LOD0;

// Total active tiles = sum of all LOD levels
const uint GRASS_MAX_ACTIVE_TILES_LOD0 = GRASS_TILES_PER_AXIS_LOD0 * GRASS_TILES_PER_AXIS_LOD0;
const uint GRASS_MAX_ACTIVE_TILES_LOD1 = GRASS_TILES_PER_AXIS_LOD1 * GRASS_TILES_PER_AXIS_LOD1;
const uint GRASS_MAX_ACTIVE_TILES_LOD2 = GRASS_TILES_PER_AXIS_LOD2 * GRASS_TILES_PER_AXIS_LOD2;
const uint GRASS_MAX_ACTIVE_TILES = GRASS_MAX_ACTIVE_TILES_LOD0 + GRASS_MAX_ACTIVE_TILES_LOD1 + GRASS_MAX_ACTIVE_TILES_LOD2;

// Maximum rendered instances per tile after culling (~12k target)
const uint GRASS_MAX_INSTANCES_PER_TILE = 12000;

// Derived: Total coverage with multi-LOD tiled system
const float GRASS_TILED_COVERAGE_LOD0 = float(GRASS_TILES_PER_AXIS_LOD0) * GRASS_TILE_SIZE_LOD0;
const float GRASS_TILED_COVERAGE_LOD1 = float(GRASS_TILES_PER_AXIS_LOD1) * GRASS_TILE_SIZE_LOD1;
const float GRASS_TILED_COVERAGE_LOD2 = float(GRASS_TILES_PER_AXIS_LOD2) * GRASS_TILE_SIZE_LOD2;
const float GRASS_TILED_COVERAGE = GRASS_TILED_COVERAGE_LOD2;  // Max coverage is LOD2 extent

// Tile load/unload margins (hysteresis to prevent thrashing)
const float GRASS_TILE_LOAD_MARGIN = 10.0;
const float GRASS_TILE_UNLOAD_MARGIN = 20.0;

// =============================================================================
// MATHEMATICAL CONSTANTS
// =============================================================================

// 2*PI for full rotation
const float GRASS_TWO_PI = 6.28318530718;

#endif // GRASS_CONSTANTS_GLSL
