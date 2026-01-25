// Celestial constants and helper functions
// Shared between sky.frag, skyview_lut.comp, and other shaders needing celestial calculations
//
// IMPORTANT: Include constants_common.glsl before this file for PI constant

#ifndef CELESTIAL_COMMON_GLSL
#define CELESTIAL_COMMON_GLSL

// ============================================================================
// Twilight Thresholds
// ============================================================================
// These thresholds use sun altitude as direction.y (approximately sin(altitude_angle))
// They define the transitions between day, twilight, and night states.
//
// Consistency note: All shaders should use these constants for twilight calculations
// to ensure synchronized behavior between sky rendering, star visibility, moon
// contribution, and lighting.

// Sun altitude thresholds (as direction.y, ~sin(angle))
const float SUN_ALTITUDE_DAY = 0.17;        // ~10 degrees - full daylight
const float SUN_ALTITUDE_CIVIL_TWILIGHT = -0.1;  // ~-6 degrees - civil twilight ends
const float SUN_ALTITUDE_NIGHT = 0.08;      // ~5 degrees - transition to night visuals

// Moon altitude thresholds (as direction.y)
const float MOON_ALTITUDE_VISIBLE = -0.035;  // ~-2 degrees - moon begins contributing light
const float MOON_ALTITUDE_FULL = 0.17;       // ~10 degrees - full moon contribution

// ============================================================================
// Celestial Body Sizes
// ============================================================================
// Both sun and moon have approximately 0.5 degree angular diameter from Earth

const float CELESTIAL_ANGULAR_RADIUS = 0.0056 / 2.0;   // Reduced for smaller disc appearance
const float CELESTIAL_DISC_SOFTNESS = 0.3;             // Smoothstep falloff factor

// Derived disc size for celestialDisc function (produces visible disc)
// The celestialDisc function uses: smoothstep(1.0 - size, 1.0 - size * softness, dot)
// For angular radius r, we need size such that the disc appears at that radius
const float SUN_DISC_SIZE = CELESTIAL_ANGULAR_RADIUS;
const float MOON_DISC_SIZE = CELESTIAL_ANGULAR_RADIUS;

// Phase mask radius - should match visible disc for consistent rendering
// This is the angular distance used for phase calculations
const float MOON_PHASE_RADIUS = 0.058;  // Angular radius for phase terminator calculation

// ============================================================================
// Julian Day Reference
// ============================================================================
const float J2000_EPOCH = 2451545.0;  // January 1, 2000, 12:00 TT
const float SIDEREAL_RATE = 360.9856; // Degrees per day (sidereal rotation)

// ============================================================================
// Helper Functions
// ============================================================================

// Get twilight blend factor (0 = full day, 1 = full night/twilight)
// Use this for determining when to show stars, increase moon contribution, etc.
float getTwilightFactor(float sunAltitudeY) {
    return smoothstep(SUN_ALTITUDE_DAY, SUN_ALTITUDE_CIVIL_TWILIGHT, sunAltitudeY);
}

// Get night factor for visual effects (stars, night sky color)
// Slightly different curve for visual transitions
float getNightFactor(float sunAltitudeY) {
    return 1.0 - smoothstep(SUN_ALTITUDE_CIVIL_TWILIGHT, SUN_ALTITUDE_NIGHT, sunAltitudeY);
}

// Get moon visibility factor based on its altitude
float getMoonVisibility(float moonAltitudeY) {
    return smoothstep(MOON_ALTITUDE_VISIBLE, MOON_ALTITUDE_FULL, moonAltitudeY);
}

// Combined moon contribution factor (twilight * visibility)
// Moon only contributes light during twilight/night when it's above horizon
float getMoonContribution(float sunAltitudeY, float moonAltitudeY) {
    float twilight = getTwilightFactor(sunAltitudeY);
    float visibility = getMoonVisibility(moonAltitudeY);
    return twilight * visibility;
}

// Render a celestial disc (sun or moon)
// Returns intensity falloff from center (1.0 at center, 0.0 outside)
float celestialDisc(vec3 viewDir, vec3 celestialDir, float angularSize) {
    float d = dot(normalize(viewDir), normalize(celestialDir));
    return smoothstep(1.0 - angularSize, 1.0 - angularSize * CELESTIAL_DISC_SOFTNESS, d);
}

// Rotate a direction vector for sidereal star field rotation
// julianDayOffset should be (julianDay - J2000_EPOCH) for precision
vec3 rotateSidereal(vec3 dir, float julianDayOffset) {
    float rotationAngle = julianDayOffset * SIDEREAL_RATE;
    float angleRad = radians(rotationAngle);

    // Rotation matrix around Y-axis (celestial pole approximation)
    float c = cos(angleRad);
    float s = sin(angleRad);
    mat3 rotation = mat3(
        c,   0.0, s,
        0.0, 1.0, 0.0,
        -s,  0.0, c
    );

    return rotation * dir;
}

#endif // CELESTIAL_COMMON_GLSL
