#pragma once

#include <glm/glm.hpp>

// Cloud Shadow UBO struct - matches GLSL layout in shaders/ubo_cloud_shadow.glsl (binding 11)
// This is a bootstrap definition; the authoritative version is auto-generated
// in generated/UBOs.h from SPIR-V reflection
struct CloudShadowUBO {
    glm::mat4 cloudShadowMatrix;
    float cloudShadowIntensity;
    float cloudShadowEnabled;
    glm::vec2 cloudShadowPadding;
};
