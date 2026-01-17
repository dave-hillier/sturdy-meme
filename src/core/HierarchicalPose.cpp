#include "HierarchicalPose.h"

glm::mat4 NodePose::toMatrix() const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * R * S;
}

glm::mat4 NodePose::toMatrix(const glm::quat& preRotation) const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 Rpre = glm::mat4_cast(preRotation);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * Rpre * R * S;
}

NodePose NodePose::fromMatrix(const glm::mat4& matrix) {
    NodePose pose;

    // Extract translation from column 3
    pose.translation = glm::vec3(matrix[3]);

    // Extract scale from column lengths
    pose.scale.x = glm::length(glm::vec3(matrix[0]));
    pose.scale.y = glm::length(glm::vec3(matrix[1]));
    pose.scale.z = glm::length(glm::vec3(matrix[2]));

    // Handle zero or near-zero scale
    const float epsilon = 1e-6f;
    if (pose.scale.x < epsilon) pose.scale.x = 1.0f;
    if (pose.scale.y < epsilon) pose.scale.y = 1.0f;
    if (pose.scale.z < epsilon) pose.scale.z = 1.0f;

    // Extract rotation by normalizing the rotation matrix columns
    glm::mat3 rotMat(
        glm::vec3(matrix[0]) / pose.scale.x,
        glm::vec3(matrix[1]) / pose.scale.y,
        glm::vec3(matrix[2]) / pose.scale.z
    );
    pose.rotation = glm::quat_cast(rotMat);

    return pose;
}

NodePose NodePose::fromMatrix(const glm::mat4& matrix, const glm::quat& preRotation) {
    NodePose pose = fromMatrix(matrix);

    // The extracted rotation is Rpre * R (preRotation combined with animated rotation)
    // Extract animated rotation: R = inverse(Rpre) * combinedR
    glm::quat preRotInv = glm::inverse(preRotation);
    pose.rotation = preRotInv * pose.rotation;

    return pose;
}
