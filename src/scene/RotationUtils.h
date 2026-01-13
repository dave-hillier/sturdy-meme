#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace RotationUtils {

// Create a quaternion that rotates from defaultDir to the target direction
// Handles edge cases where directions are parallel or anti-parallel
inline glm::quat rotationFromDirection(const glm::vec3& direction,
                                        const glm::vec3& defaultDir = glm::vec3(0.0f, -1.0f, 0.0f)) {
    glm::vec3 dir = glm::normalize(direction);
    float dot = glm::dot(defaultDir, dir);

    if (dot > 0.9999f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Already aligned
    }
    if (dot < -0.9999f) {
        // Opposite direction - rotate 180 degrees around perpendicular axis
        glm::vec3 axis = glm::abs(defaultDir.x) < 0.9f
            ? glm::normalize(glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), defaultDir))
            : glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), defaultDir));
        return glm::angleAxis(glm::pi<float>(), axis);
    }

    glm::vec3 axis = glm::normalize(glm::cross(defaultDir, dir));
    float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
    return glm::angleAxis(angle, axis);
}

// Get direction vector from a quaternion rotation (rotates defaultDir by the quaternion)
inline glm::vec3 directionFromRotation(const glm::quat& rotation,
                                        const glm::vec3& defaultDir = glm::vec3(0.0f, -1.0f, 0.0f)) {
    return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(defaultDir, 0.0f));
}

}  // namespace RotationUtils
