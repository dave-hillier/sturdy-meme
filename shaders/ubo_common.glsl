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
    float cameraNear;          // Camera near plane for linearizing depth
    float cameraFar;           // Camera far plane for linearizing depth
    float uboPad3;

    // Atmosphere parameters (from UI controls) - used by atmosphere_common.glsl
    vec4 atmosRayleighScattering;  // xyz = rayleigh scattering base, w = scale height
    vec4 atmosMieParams;           // x = mie scattering, y = mie absorption, z = scale height, w = anisotropy
    vec4 atmosOzoneAbsorption;     // xyz = ozone absorption, w = layer center
    float atmosOzoneWidth;         // Ozone layer width
    float atmosPad1, atmosPad2, atmosPad3;  // Padding for alignment

    // Height fog parameters (from UI controls) - used by applyHeightFog/applyAerialPerspective
    vec4 heightFogParams;          // x = baseHeight, y = scaleHeight, z = density, w = unused
    vec4 heightFogLayerParams;     // x = layerThickness, y = layerDensity, z = unused, w = unused
} ubo;

#endif // UBO_COMMON_GLSL
