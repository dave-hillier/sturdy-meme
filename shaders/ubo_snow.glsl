// Snow UBO definition - include this in shaders that need snow rendering
// Separated from main UBO to reduce size for shaders that don't need snow

#ifndef UBO_SNOW_GLSL
#define UBO_SNOW_GLSL

#include "bindings.glsl"

#ifndef SNOW_UBO_BINDING
#define SNOW_UBO_BINDING BINDING_SNOW_UBO
#endif

layout(binding = SNOW_UBO_BINDING) uniform SnowUBO {
    float snowAmount;            // Global snow intensity (0-1)
    float snowRoughness;         // Snow surface roughness
    float snowTexScale;          // World-space snow texture scale
    float useVolumetricSnow;     // 1.0 = use cascades, 0.0 = use legacy mask
    vec4 snowColor;              // rgb = snow color, a = unused
    vec4 snowMaskParams;         // xy = mask origin, z = mask size, w = unused
    // Volumetric snow cascade parameters
    vec4 snowCascade0Params;     // xy = origin, z = size, w = texel size
    vec4 snowCascade1Params;     // xy = origin, z = size, w = texel size
    vec4 snowCascade2Params;     // xy = origin, z = size, w = texel size
    float snowMaxHeight;         // Maximum snow height in meters
    float debugSnowDepth;        // 1.0 = show depth visualization
    vec2 snowPadding;
} snow;

#endif // UBO_SNOW_GLSL
