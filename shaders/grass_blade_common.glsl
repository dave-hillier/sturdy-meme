/*
 * grass_blade_common.glsl - Common grass blade geometry and deformation
 *
 * Provides shared functions for grass blade rendering and shadow passes.
 * Both passes must use identical deformation to ensure shadow consistency.
 */

#ifndef GRASS_BLADE_COMMON_GLSL
#define GRASS_BLADE_COMMON_GLSL

#include "grass_constants.glsl"

// Perlin noise implementation for wind variation
// Uses fixed permutation table for consistency
const int grassPerm[512] = int[512](
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    // Repeat for wrap-around
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
);

float grassFade(float t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

float grassGrad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

float grassPerlinNoise(float x, float y) {
    int X = int(floor(x)) & 255;
    int Y = int(floor(y)) & 255;

    x -= floor(x);
    y -= floor(y);

    float u = grassFade(x);
    float v = grassFade(y);

    int A = grassPerm[X] + Y;
    int AA = grassPerm[A];
    int AB = grassPerm[A + 1];
    int B = grassPerm[X + 1] + Y;
    int BA = grassPerm[B];
    int BB = grassPerm[B + 1];

    float res = mix(
        mix(grassGrad(grassPerm[AA], x, y),
            grassGrad(grassPerm[BA], x - 1.0, y), u),
        mix(grassGrad(grassPerm[AB], x, y - 1.0),
            grassGrad(grassPerm[BB], x - 1.0, y - 1.0), u),
        v
    );

    return (res + 1.0) * 0.5;
}

// Sample wind creating rolling wave effect
// Uses sinusoidal waves as the primary pattern for clear visible wave motion
// windDir: xy = normalized direction
// windStrength: wind strength
// windSpeed: wind speed (controls wave travel speed)
// windTime: time for animation
// gustFreq: gust frequency
// gustAmp: gust amplitude
float grassSampleWind(vec2 worldPos, vec2 windDir, float windStrength, float windSpeed,
                      float windTime, float gustFreq, float gustAmp) {
    // Project position onto wind direction - this is the key to rolling waves
    // Waves travel along this axis, creating bands perpendicular to wind
    float alongWind = dot(worldPos, windDir);

    // Primary rolling wave - large scale waves traveling in wind direction
    // Wavelength ~8m (0.8 frequency), travels at windSpeed
    float primaryWave = sin(alongWind * 0.8 - windTime * windSpeed * 0.5);

    // Secondary wave - adds complexity, different wavelength ~5m
    float secondaryWave = sin(alongWind * 1.3 - windTime * windSpeed * 0.7) * 0.4;

    // Combine waves (primary dominant)
    float wavePattern = (primaryWave + secondaryWave) * 0.5 + 0.5; // Normalize to 0-1

    // Add subtle noise variation along wave fronts (perpendicular to wind)
    // This prevents waves from looking too uniform/artificial
    vec2 perpDir = vec2(-windDir.y, windDir.x);
    float acrossWind = dot(worldPos, perpDir);
    float variation = grassPerlinNoise(acrossWind * 0.15, alongWind * 0.05) * 0.3;

    // Time-varying gust affects overall intensity
    float gust = (sin(windTime * gustFreq * GRASS_TWO_PI) * 0.5 + 0.5) * gustAmp;

    return (wavePattern + variation + gust) * windStrength;
}

// Quadratic Bezier evaluation
vec3 grassBezier(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return u * u * p0 + 2.0 * u * t * p1 + t * t * p2;
}

// Quadratic Bezier derivative
vec3 grassBezierDerivative(vec3 p0, vec3 p1, vec3 p2, float t) {
    float u = 1.0 - t;
    return 2.0 * u * (p1 - p0) + 2.0 * t * (p2 - p1);
}

// Build orthonormal basis from terrain normal
// Returns tangent (T) and bitangent (B) vectors
void grassBuildTerrainBasis(vec3 N, float facing, out vec3 T, out vec3 B) {
    // Start with world up, but handle edge case where N is nearly vertical
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = vec3(1.0, 0.0, 0.0);

    // Choose a reference vector that's not parallel to N
    vec3 ref = abs(N.y) < 0.99 ? up : right;

    // First tangent perpendicular to N
    T = normalize(cross(N, ref));
    // Second tangent perpendicular to both
    B = normalize(cross(N, T));

    // Rotate tangent basis by facing angle around N
    float cs = cos(facing);
    float sn = sin(facing);
    vec3 T_rotated = T * cs + B * sn;
    vec3 B_rotated = -T * sn + B * cs;

    T = T_rotated;
    B = B_rotated;
}

// Grass blade deformation result - Bezier control points
struct GrassBladeControlPoints {
    vec3 p0;  // Base
    vec3 p1;  // Mid control
    vec3 p2;  // Tip
};

// Calculate grass blade deformation including fold/droop for short grass
// This function MUST be used by both render and shadow passes for consistency
GrassBladeControlPoints grassCalculateBladeDeformation(
    float height,
    float bladeHash,
    float tilt,
    float windOffset
) {
    GrassBladeControlPoints result;

    // Blade folding for short grass using unified constants
    // Shorter blades fold over more (like real lawn grass bending under weight)
    // Height range is GRASS_HEIGHT_MIN to GRASS_HEIGHT_MAX, normalize to 0-1 for folding calculation
    float normalizedHeight = clamp((height - GRASS_HEIGHT_MIN) / GRASS_HEIGHT_RANGE, 0.0, 1.0);

    // Fold amount: short grass (0) folds a lot, tall grass (1) stays upright
    // foldAmount ranges from GRASS_FOLD_MAX (short) to GRASS_FOLD_MIN (tall)
    float foldAmount = mix(GRASS_FOLD_MAX, GRASS_FOLD_MIN, normalizedHeight);

    // Add per-blade variation to fold direction using hash
    float foldDirection = (bladeHash - 0.5) * 2.0;  // -1 to 1
    float foldX = foldDirection * foldAmount * height;

    // Short grass also droops more - tip ends up lower relative to height
    float droopFactor = mix(GRASS_DROOP_MAX, 0.0, normalizedHeight);  // GRASS_DROOP_MAX for shortest
    float effectiveHeight = height * (1.0 - droopFactor);

    // Bezier control points (in local blade space)
    result.p0 = vec3(0.0, 0.0, 0.0);  // Base
    result.p1 = vec3(windOffset * 0.3 + tilt * 0.5 + foldX * 0.5, height * 0.6, 0.0);  // Mid control - higher for fold
    result.p2 = vec3(windOffset + tilt + foldX, effectiveHeight, 0.0);  // Tip - with fold and droop

    return result;
}

// Blade geometry constants are defined in grass_constants.glsl:
// GRASS_NUM_SEGMENTS, GRASS_BASE_WIDTH, GRASS_WIDTH_TAPER, GRASS_VERTICES_PER_BLADE, GRASS_TIP_VERTEX_INDEX

// Calculate blade vertex position given vertex index
// Returns: localPos in blade space, t (position along blade 0-1), widthAtT
void grassCalculateBladeVertex(
    uint vertexIndex,
    GrassBladeControlPoints cp,
    out vec3 localPos,
    out float t,
    out float widthAtT
) {
    // Triangle strip blade geometry: 15 vertices = 7 segments (8 height levels)
    // Even vertices (0,2,4,6,8,10,12,14) are left side
    // Odd vertices (1,3,5,7,9,11,13) are right side
    // Vertex 14 is the tip point (width = 0)

    // Calculate which height level this vertex is at
    uint segmentIndex = vertexIndex / 2;  // 0 to GRASS_NUM_SEGMENTS
    bool isRightSide = (vertexIndex % 2) == 1;

    // Calculate t (position along blade, 0 = base, 1 = tip)
    t = float(segmentIndex) / float(GRASS_NUM_SEGMENTS);

    // Width tapers from base to tip using unified constant
    widthAtT = GRASS_BASE_WIDTH * (1.0 - t * GRASS_WIDTH_TAPER);

    // For the last vertex (tip), width is 0
    if (vertexIndex == GRASS_TIP_VERTEX_INDEX) {
        widthAtT = 0.0;
        t = 1.0;
    }

    // Offset left or right from center
    float xOffset = isRightSide ? widthAtT : -widthAtT;

    // Get position on bezier curve and offset by width
    vec3 curvePos = grassBezier(cp.p0, cp.p1, cp.p2, t);
    localPos = curvePos + vec3(xOffset, 0.0, 0.0);
}

#endif // GRASS_BLADE_COMMON_GLSL
