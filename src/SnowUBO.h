#pragma once

#include <glm/glm.hpp>

/**
 * SnowUBO - Snow rendering uniform buffer object
 *
 * This struct matches the GLSL layout in shaders/ubo_snow.glsl (binding 14).
 * Uses std140 layout for uniform buffer compatibility.
 */
struct SnowUBO {
    float snowAmount;            // Global snow intensity (0-1)
    float snowRoughness;         // Snow surface roughness
    float snowTexScale;          // World-space snow texture scale
    float useVolumetricSnow;     // 1.0 = use cascades, 0.0 = use legacy mask
    glm::vec4 snowColor;         // rgb = snow color, a = unused
    glm::vec4 snowMaskParams;    // xy = mask origin, z = mask size, w = unused
    glm::vec4 snowCascade0Params;// xy = origin, z = size, w = texel size
    glm::vec4 snowCascade1Params;// xy = origin, z = size, w = texel size
    glm::vec4 snowCascade2Params;// xy = origin, z = size, w = texel size
    float snowMaxHeight;         // Maximum snow height in meters
    float debugSnowDepth;        // 1.0 = show depth visualization
    glm::vec2 snowPadding;
};

static_assert(sizeof(SnowUBO) == 112, "SnowUBO size mismatch with GLSL std140 layout");
