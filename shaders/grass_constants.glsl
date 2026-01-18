/*
 * grass_constants.glsl - Unified grass system constants (GLSL wrapper)
 *
 * This file includes the shared constants and adds GLSL-specific derived values.
 * The actual constants are defined in grass_constants_shared.glsl which is
 * the single source of truth for both C++ and GLSL.
 */

#ifndef GRASS_CONSTANTS_GLSL
#define GRASS_CONSTANTS_GLSL

// Include shared constants (single source of truth)
#include "grass_constants_shared.glsl"

// =============================================================================
// GLSL-SPECIFIC DERIVED VALUES
// =============================================================================

// Coverage size derived from grid
const float GRASS_COVERAGE_SIZE = float(GRASS_GRID_SIZE) * GRASS_SPACING;
const float GRASS_DENSITY = 1.0 / (GRASS_SPACING * GRASS_SPACING);

// Blade geometry derived
const uint GRASS_TIP_VERTEX_INDEX = GRASS_VERTICES_PER_BLADE - 1;
const float GRASS_WIDTH_TAPER = 0.9;
const float GRASS_FOLD_MIN = 0.1;
const float GRASS_FOLD_MAX = 0.6;
const float GRASS_DROOP_MAX = 0.3;

// Displacement derived
const float GRASS_DISPLACEMENT_TEXEL_SIZE = GRASS_DISPLACEMENT_REGION_SIZE / float(GRASS_DISPLACEMENT_TEXTURE_SIZE);
const uint GRASS_DISPLACEMENT_DISPATCH_SIZE = GRASS_DISPLACEMENT_TEXTURE_SIZE / GRASS_WORKGROUP_SIZE;
const uint GRASS_MAX_DISPLACEMENT_SOURCES = 16;

// Wind parameters
const float GRASS_WIND_EFFECT_MULTIPLIER = 0.25;
const float GRASS_WIND_PHASE_MULTIPLIER = 0.25;
const float GRASS_WIND_BASE_FREQ = 0.1;
const float GRASS_WIND_OCTAVE2_MULT = 2.0;
const float GRASS_WIND_OCTAVE3_MULT = 4.0;
const float GRASS_WIND_OCTAVE1_WEIGHT = 0.7;
const float GRASS_WIND_OCTAVE2_WEIGHT = 0.2;
const float GRASS_WIND_OCTAVE3_WEIGHT = 0.1;

// Colors
const vec3 GRASS_COLOR_BASE = vec3(0.08, 0.22, 0.04);
const vec3 GRASS_COLOR_TIP = vec3(0.35, 0.65, 0.18);
const float GRASS_CLUMP_COLOR_INFLUENCE = 0.15;
const vec3 GRASS_COLOR_SHIFT_WARM = vec3(0.05, 0.03, -0.02);
const vec3 GRASS_COLOR_SHIFT_COOL = vec3(-0.02, 0.02, 0.03);
const float GRASS_BRIGHTNESS_MIN = 0.9;
const float GRASS_BRIGHTNESS_MAX = 1.1;

// Material
const float GRASS_ROUGHNESS = 0.7;
const float GRASS_SSS_STRENGTH = 0.35;
const float GRASS_SPECULAR_STRENGTH = 0.15;
const float GRASS_BACKLIT_DIFFUSE = 0.6;

// Blade normal curvature (makes blades appear rounded, not flat)
// 0.0 = flat normals, 1.0 = strongly curved outward at edges
const float GRASS_BLADE_CURVATURE = 0.6;

// Rendering enhancements
const float GRASS_EDGE_THICKEN_MAX = 3.0;
const float GRASS_EDGE_THICKEN_FACTOR = 2.0;
const float GRASS_RIM_FRESNEL_POWER = 4.0;
const float GRASS_RIM_INTENSITY = 0.15;
const float GRASS_AO_BASE = 0.4;
const float GRASS_AO_TIP = 1.0;
const float GRASS_SHADOW_TIP_THRESHOLD = 0.95;
const float GRASS_SHADOW_DEPTH_BIAS_CONSTANT = 0.25;
const float GRASS_SHADOW_DEPTH_BIAS_SLOPE = 0.75;

#endif // GRASS_CONSTANTS_GLSL
