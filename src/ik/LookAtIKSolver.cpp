#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

void LookAtIKSolver::solve(
    Skeleton& skeleton,
    LookAtIK& lookAt,
    const std::vector<glm::mat4>& globalTransforms,
    float deltaTime
) {
    if (!lookAt.enabled || lookAt.weight <= 0.0f) return;
    if (lookAt.headBoneIndex < 0) return;

    // Get head world position
    glm::vec3 headPos = IKUtils::getWorldPosition(globalTransforms[lookAt.headBoneIndex]);
    glm::vec3 eyePos = headPos + lookAt.eyeOffset;

    // Calculate direction to target
    glm::vec3 toTarget = lookAt.targetPosition - eyePos;
    float distance = glm::length(toTarget);
    if (distance < 0.001f) return;

    glm::vec3 targetDir = toTarget / distance;

    // Get current forward direction from head bone
    glm::vec3 currentForward = glm::normalize(glm::vec3(globalTransforms[lookAt.headBoneIndex][2]));

    // Calculate required rotation to look at target
    glm::quat fullRotation = IKUtils::aimAt(currentForward, targetDir, glm::vec3(0, 1, 0));

    // Clamp rotation to limits
    fullRotation = clampLookRotation(fullRotation, lookAt.maxYawAngle, lookAt.maxPitchAngle);

    // Distribute rotation across bones based on weights
    float totalWeight = 0.0f;
    if (lookAt.headBoneIndex >= 0) totalWeight += lookAt.headWeight;
    if (lookAt.neckBoneIndex >= 0) totalWeight += lookAt.neckWeight;
    if (lookAt.spineBoneIndex >= 0) totalWeight += lookAt.spineWeight;

    if (totalWeight < 0.001f) totalWeight = 1.0f;

    // Apply spine rotation (if available)
    if (lookAt.spineBoneIndex >= 0 && lookAt.spineWeight > 0.0f) {
        float boneWeight = (lookAt.spineWeight / totalWeight) * lookAt.weight;
        glm::quat targetRot = glm::slerp(glm::quat(1, 0, 0, 0), fullRotation, boneWeight);

        // Smooth interpolation
        if (lookAt.smoothSpeed > 0.0f && deltaTime > 0.0f) {
            float t = glm::clamp(lookAt.smoothSpeed * deltaTime, 0.0f, 1.0f);
            lookAt.currentSpineRotation = glm::slerp(lookAt.currentSpineRotation, targetRot, t);
        } else {
            lookAt.currentSpineRotation = targetRot;
        }

        Joint& spineJoint = skeleton.joints[lookAt.spineBoneIndex];
        glm::mat4 parentGlobal = skeleton.getParentGlobalTransform(lookAt.spineBoneIndex, globalTransforms);
        applyBoneRotation(spineJoint, lookAt.currentSpineRotation, parentGlobal, 1.0f);
    }

    // Apply neck rotation (if available)
    if (lookAt.neckBoneIndex >= 0 && lookAt.neckWeight > 0.0f) {
        float boneWeight = (lookAt.neckWeight / totalWeight) * lookAt.weight;
        glm::quat targetRot = glm::slerp(glm::quat(1, 0, 0, 0), fullRotation, boneWeight);

        if (lookAt.smoothSpeed > 0.0f && deltaTime > 0.0f) {
            float t = glm::clamp(lookAt.smoothSpeed * deltaTime, 0.0f, 1.0f);
            lookAt.currentNeckRotation = glm::slerp(lookAt.currentNeckRotation, targetRot, t);
        } else {
            lookAt.currentNeckRotation = targetRot;
        }

        Joint& neckJoint = skeleton.joints[lookAt.neckBoneIndex];
        glm::mat4 parentGlobal = skeleton.getParentGlobalTransform(lookAt.neckBoneIndex, globalTransforms);
        applyBoneRotation(neckJoint, lookAt.currentNeckRotation, parentGlobal, 1.0f);
    }

    // Apply head rotation
    if (lookAt.headBoneIndex >= 0 && lookAt.headWeight > 0.0f) {
        float boneWeight = (lookAt.headWeight / totalWeight) * lookAt.weight;
        glm::quat targetRot = glm::slerp(glm::quat(1, 0, 0, 0), fullRotation, boneWeight);

        if (lookAt.smoothSpeed > 0.0f && deltaTime > 0.0f) {
            float t = glm::clamp(lookAt.smoothSpeed * deltaTime, 0.0f, 1.0f);
            lookAt.currentHeadRotation = glm::slerp(lookAt.currentHeadRotation, targetRot, t);
        } else {
            lookAt.currentHeadRotation = targetRot;
        }

        Joint& headJoint = skeleton.joints[lookAt.headBoneIndex];
        glm::mat4 parentGlobal = skeleton.getParentGlobalTransform(lookAt.headBoneIndex, globalTransforms);
        applyBoneRotation(headJoint, lookAt.currentHeadRotation, parentGlobal, 1.0f);
    }
}

glm::vec3 LookAtIKSolver::getLookDirection(
    const glm::mat4& boneGlobalTransform,
    const glm::vec3& targetPosition,
    const glm::vec3& eyeOffset
) {
    glm::vec3 bonePos = IKUtils::getWorldPosition(boneGlobalTransform);
    glm::vec3 eyePos = bonePos + eyeOffset;
    return glm::normalize(targetPosition - eyePos);
}

glm::quat LookAtIKSolver::clampLookRotation(
    const glm::quat& rotation,
    float maxYaw,
    float maxPitch
) {
    // Use swing-twist decomposition to avoid gimbal lock
    // Twist = yaw (rotation around Y axis)
    // Swing = pitch/roll (rotation perpendicular to Y)
    glm::vec3 yawAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    float rotAngle = glm::angle(rotation);
    if (rotAngle < 0.0001f) {
        return rotation;
    }

    glm::vec3 rotAxis = glm::axis(rotation);

    // Extract yaw (twist around Y)
    float yawAmount = glm::dot(rotAxis, yawAxis) * rotAngle;
    glm::quat yaw = glm::angleAxis(yawAmount, yawAxis);

    // Extract pitch/roll (swing)
    glm::quat swing = rotation * glm::inverse(yaw);

    // Clamp yaw
    float clampedYaw = glm::clamp(yawAmount, -maxYaw, maxYaw);
    glm::quat clampedYawQuat = glm::angleAxis(clampedYaw, yawAxis);

    // Clamp pitch (swing angle, primarily around X axis for look-at)
    float swingAngle = glm::angle(swing);
    if (swingAngle > maxPitch) {
        glm::vec3 swingAxis = glm::axis(swing);
        swing = glm::angleAxis(maxPitch, swingAxis);
    }

    return glm::normalize(swing * clampedYawQuat);
}

void LookAtIKSolver::applyBoneRotation(
    Joint& joint,
    const glm::quat& additionalRotation,
    const glm::mat4& parentGlobalTransform,
    float weight
) {
    // Decompose current local transform
    glm::vec3 translation, scale;
    glm::quat currentRotation;
    IKUtils::decomposeTransform(joint.localTransform, translation, currentRotation, scale);

    // Apply additional rotation
    glm::quat newRotation = additionalRotation * currentRotation;

    // Blend if weight < 1
    if (weight < 1.0f) {
        newRotation = glm::slerp(currentRotation, newRotation, weight);
    }

    // Recompose transform
    joint.localTransform = IKUtils::composeTransform(translation, newRotation, scale);
}
