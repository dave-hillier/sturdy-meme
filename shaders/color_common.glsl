// Color space conversion utilities

#ifndef COLOR_COMMON_GLSL
#define COLOR_COMMON_GLSL

// Convert RGB to HSV
// H is in [0, 1] representing 0-360 degrees
// S and V are in [0, 1]
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// Convert HSV to RGB
// H is in [0, 1] representing 0-360 degrees
// S and V are in [0, 1]
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Apply autumn color effect to foliage
// autumnFactor: 0 = summer green, 1 = full autumn colors
// Returns color shifted toward orange/yellow/red autumn tones
vec3 applyAutumnHueShift(vec3 color, float autumnFactor) {
    if (autumnFactor <= 0.0) return color;

    vec3 hsv = rgb2hsv(color);

    // Original hue (green is around 0.33 in our 0-1 scale)
    float originalHue = hsv.x;

    // Target autumn hue range: 0.0-0.15 (red to orange-yellow)
    // We want green (0.33) to shift toward orange (0.08-0.12)
    // with some variation for natural look

    // Only shift colors that are in the green-cyan range (0.2 to 0.5)
    // This preserves browns, reds, and other non-green colors
    float greenMask = smoothstep(0.15, 0.25, originalHue) * smoothstep(0.55, 0.45, originalHue);

    // Autumn target hue - varies based on saturation for more natural variation
    // More saturated greens become more orange, less saturated become more yellow
    float autumnHue = mix(0.12, 0.06, hsv.y * 0.5);  // Yellow-orange to red-orange

    // Shift hue toward autumn colors
    float hueShift = mix(originalHue, autumnHue, autumnFactor * greenMask);

    // Slightly increase saturation for vibrant autumn colors
    float saturationBoost = 1.0 + autumnFactor * greenMask * 0.2;

    // Slightly adjust value/brightness - autumn leaves can be slightly darker
    float valueAdjust = 1.0 - autumnFactor * greenMask * 0.1;

    hsv.x = hueShift;
    hsv.y = clamp(hsv.y * saturationBoost, 0.0, 1.0);
    hsv.z = hsv.z * valueAdjust;

    return hsv2rgb(hsv);
}

#endif // COLOR_COMMON_GLSL
