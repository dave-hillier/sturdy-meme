// Common UBO definition - include this in all shaders that need core rendering data
// This ensures the struct stays in sync between vertex and fragment shaders
//
// For snow uniforms, include ubo_snow.glsl (binding 10)
// For cloud shadow uniforms, include ubo_cloud_shadow.glsl (binding 11)

#ifndef UBO_COMMON_GLSL
#define UBO_COMMON_GLSL

#include "bindings.glsl"

#ifndef NUM_CASCADES
#define NUM_CASCADES 4
#endif

#ifndef UBO_BINDING
#define UBO_BINDING BINDING_UBO
#endif

layout(binding = UBO_BINDING) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];  // Per-cascade light matrices
    vec4 cascadeSplits;                   // View-space split depths
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;                       // rgb = moon color
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;  // xyz = position, w = intensity
    vec4 pointLightColor;     // rgb = color, a = radius
    vec4 windDirectionAndSpeed;           // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;       // 1.0 = show cascade colors
    float julianDay;           // Julian day for sidereal rotation
    float cloudStyle;
    vec3 uboPadding;           // Padding for alignment
} ubo;

#endif // UBO_COMMON_GLSL
