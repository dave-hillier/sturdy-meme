#pragma once

#include <glm/glm.hpp>

// Snow UBO struct - matches GLSL layout in shaders/ubo_snow.glsl (binding 10)
// This is a bootstrap definition; the authoritative version is auto-generated
// in generated/UBOs.h from SPIR-V reflection
struct SnowUBO {
    float snowAmount;
    float snowRoughness;
    float snowTexScale;
    float useVolumetricSnow;
    glm::vec4 snowColor;
    glm::vec4 snowMaskParams;
    glm::vec4 snowCascade0Params;
    glm::vec4 snowCascade1Params;
    glm::vec4 snowCascade2Params;
    float snowMaxHeight;
    float debugSnowDepth;
    glm::vec2 snowPadding;
};
