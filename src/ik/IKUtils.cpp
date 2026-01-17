#include "IKSolver.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/norm.hpp>
#include <cmath>

namespace IKUtils {

void decomposeTransform(
    const glm::mat4& transform,
    glm::vec3& translation,
    glm::quat& rotation,
    glm::vec3& scale
) {
    // Extract translation
    translation = glm::vec3(transform[3]);

    // Extract scale (length of each basis vector)
    scale.x = glm::length(glm::vec3(transform[0]));
    scale.y = glm::length(glm::vec3(transform[1]));
    scale.z = glm::length(glm::vec3(transform[2]));

    // Remove scale from rotation matrix
    glm::mat3 rotMat(
        glm::vec3(transform[0]) / scale.x,
        glm::vec3(transform[1]) / scale.y,
        glm::vec3(transform[2]) / scale.z
    );

    rotation = glm::quat_cast(rotMat);
}

glm::mat4 composeTransform(
    const glm::vec3& translation,
    const glm::quat& rotation,
    const glm::vec3& scale
) {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * R * S;
}

glm::vec3 getWorldPosition(const glm::mat4& globalTransform) {
    return glm::vec3(globalTransform[3]);
}

float getBoneLength(
    const std::vector<glm::mat4>& globalTransforms,
    int32_t boneIndex,
    int32_t childBoneIndex
) {
    if (boneIndex < 0 || childBoneIndex < 0) return 0.0f;
    if (static_cast<size_t>(boneIndex) >= globalTransforms.size()) return 0.0f;
    if (static_cast<size_t>(childBoneIndex) >= globalTransforms.size()) return 0.0f;

    glm::vec3 bonePos = getWorldPosition(globalTransforms[boneIndex]);
    glm::vec3 childPos = getWorldPosition(globalTransforms[childBoneIndex]);
    return glm::length(childPos - bonePos);
}

glm::quat aimAt(
    const glm::vec3& currentDir,
    const glm::vec3& targetDir,
    const glm::vec3& upHint
) {
    glm::vec3 from = glm::normalize(currentDir);
    glm::vec3 to = glm::normalize(targetDir);

    float dot = glm::dot(from, to);

    // Already aligned
    if (dot > 0.9999f) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    // Opposite directions
    if (dot < -0.9999f) {
        // Find orthogonal axis
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), from);
        if (glm::length2(axis) < 0.0001f) {
            axis = glm::cross(glm::vec3(0, 1, 0), from);
        }
        axis = glm::normalize(axis);
        return glm::angleAxis(glm::pi<float>(), axis);
    }

    glm::vec3 axis = glm::cross(from, to);
    float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
    return glm::angleAxis(angle, glm::normalize(axis));
}

} // namespace IKUtils
