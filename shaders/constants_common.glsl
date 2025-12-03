// Common constants shared across all shaders
// Prevent multiple inclusion
#ifndef CONSTANTS_COMMON_GLSL
#define CONSTANTS_COMMON_GLSL

const float PI = 3.14159265359;
const int NUM_CASCADES = 4;

// Atmospheric parameters (Phase 4 - scaled to kilometers for scene scale)
const float PLANET_RADIUS = 6371.0;
const float ATMOSPHERE_RADIUS = 6471.0;

const vec3 RAYLEIGH_SCATTERING_BASE = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;

const float MIE_SCATTERING_BASE = 3.996e-3;
const float MIE_ABSORPTION_BASE = 4.4e-3;
const float MIE_SCALE_HEIGHT = 1.2;
const float MIE_ANISOTROPY = 0.8;

const vec3 OZONE_ABSORPTION = vec3(0.65e-3, 1.881e-3, 0.085e-3);
const float OZONE_LAYER_CENTER = 25.0;
const float OZONE_LAYER_WIDTH = 15.0;

// Height fog parameters (Phase 4.3 - Volumetric Haze, scaled for large world)
const float FOG_BASE_HEIGHT = 0.0;        // Ground level
const float FOG_SCALE_HEIGHT = 300.0;     // Exponential falloff height in scene units (large world)
const float FOG_DENSITY = 0.003;          // Base fog density (reduced for large world visibility)
const float FOG_LAYER_THICKNESS = 30.0;   // Low-lying fog layer thickness
const float FOG_LAYER_DENSITY = 0.008;    // Low-lying fog density (reduced for large world)

// Light types
const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_SPOT = 1;

// Maximum lights (must match CPU side)
const uint MAX_LIGHTS = 16;

#endif // CONSTANTS_COMMON_GLSL
