// UBO struct definitions
// Must match layout in shaders/ubo_common.glsl (std140 layout rules)

#pragma once

#include <glm/glm.hpp>

#ifndef NUM_SHADOW_CASCADES
#define NUM_SHADOW_CASCADES 4
#endif

// Matches layout(binding = 0) uniform UniformBufferObject in ubo_common.glsl
// Uses std140 layout rules for proper alignment
struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 cascadeViewProj[NUM_SHADOW_CASCADES];  // Per-cascade light matrices
    glm::vec4 cascadeSplits;                          // View-space split depths
    glm::vec4 sunDirection;                           // xyz = direction, w = intensity
    glm::vec4 moonDirection;                          // xyz = direction, w = intensity
    glm::vec4 sunColor;                               // rgb = color, a unused
    glm::vec4 moonColor;                              // rgb = color, a = moon phase
    glm::vec4 ambientColor;
    glm::vec4 cameraPosition;
    glm::vec4 pointLightPosition;                     // xyz = position, w = intensity
    glm::vec4 pointLightColor;                        // rgb = color, a = radius
    glm::vec4 windDirectionAndSpeed;                  // xy = direction, z = speed, w = time
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;                              // 1.0 = show cascade colors
    float julianDay;                                  // Julian day for sidereal rotation
    float cloudStyle;
    float cameraNear;                                 // Camera near plane for linearizing depth
    float cameraFar;                                  // Camera far plane for linearizing depth
    float eclipseAmount;                              // Eclipse amount (0 = none, 1 = total solar eclipse)

    // Atmosphere parameters (from UI controls)
    glm::vec4 atmosRayleighScattering;                // xyz = rayleigh scattering base, w = scale height
    glm::vec4 atmosMieParams;                         // x = mie scattering, y = mie absorption, z = scale height, w = anisotropy
    glm::vec4 atmosOzoneAbsorption;                   // xyz = ozone absorption, w = layer center
    float atmosOzoneWidth;                            // Ozone layer width
    float atmosPad1, atmosPad2, atmosPad3;            // Padding for alignment

    // Height fog parameters (from UI controls)
    glm::vec4 heightFogParams;                        // x = baseHeight, y = scaleHeight, z = density, w = unused
    glm::vec4 heightFogLayerParams;                   // x = layerThickness, y = layerDensity, z = unused, w = unused

    // Cloud parameters (from UI controls)
    float cloudCoverage;                              // 0-1 cloud coverage amount
    float cloudDensity;                               // Base density multiplier
    float cloudPad1, cloudPad2;                       // Padding for alignment
};
