#pragma once

#include <glm/glm.hpp>

/**
 * CloudShadowUBO - Cloud shadow rendering uniform buffer object
 *
 * This struct matches the GLSL layout in shaders/ubo_cloud_shadow.glsl (binding 15).
 * Uses std140 layout for uniform buffer compatibility.
 */
struct CloudShadowUBO {
    glm::mat4 cloudShadowMatrix;   // World XZ to cloud shadow UV transform
    float cloudShadowIntensity;    // How dark cloud shadows are (0-1)
    float cloudShadowEnabled;      // 1.0 = enabled, 0.0 = disabled
    glm::vec2 cloudShadowPadding;
};

static_assert(sizeof(CloudShadowUBO) == 80, "CloudShadowUBO size mismatch with GLSL std140 layout");
