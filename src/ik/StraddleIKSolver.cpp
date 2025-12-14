#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

void StraddleIKSolver::solve(
    Skeleton& skeleton,
    StraddleIK& straddle,
    const FootPlacementIK* leftFoot,
    const FootPlacementIK* rightFoot,
    const std::vector<glm::mat4>& globalTransforms,
    float deltaTime
) {
    if (!straddle.enabled || straddle.weight <= 0.0f) return;
    if (straddle.pelvisBoneIndex < 0) return;

    // Get foot heights
    float leftHeight = 0.0f;
    float rightHeight = 0.0f;

    if (leftFoot && leftFoot->isGrounded) {
        leftHeight = leftFoot->currentGroundHeight;
    }
    if (rightFoot && rightFoot->isGrounded) {
        rightHeight = rightFoot->currentGroundHeight;
    }

    // Store for debug/queries
    straddle.leftFootHeight = leftHeight;
    straddle.rightFootHeight = rightHeight;

    float heightDiff = rightHeight - leftHeight;
    float absHeightDiff = std::abs(heightDiff);

    // Only apply straddle if height difference is significant
    if (absHeightDiff < straddle.minHeightDiff) {
        // Smoothly return to neutral
        if (deltaTime > 0.0f) {
            float t = glm::clamp(straddle.tiltSmoothSpeed * deltaTime, 0.0f, 1.0f);
            straddle.currentHipTilt = glm::mix(straddle.currentHipTilt, 0.0f, t);
            straddle.currentHipShift = glm::mix(straddle.currentHipShift, 0.0f, t);
        }
    } else {
        // Calculate target hip tilt
        float targetTilt = calculateHipTilt(leftHeight, rightHeight,
                                            straddle.maxHipTilt, straddle.maxHeightDiff);

        // Calculate lateral shift (shift hips toward higher foot)
        float shiftDir = heightDiff > 0 ? 1.0f : -1.0f;  // Positive = shift right
        float shiftAmount = (absHeightDiff / straddle.maxHeightDiff) * straddle.maxHipShift;
        shiftAmount = glm::clamp(shiftAmount, 0.0f, straddle.maxHipShift) * shiftDir;

        // Smooth interpolation
        if (deltaTime > 0.0f) {
            float t = glm::clamp(straddle.tiltSmoothSpeed * deltaTime, 0.0f, 1.0f);
            straddle.currentHipTilt = glm::mix(straddle.currentHipTilt, targetTilt, t);
            straddle.currentHipShift = glm::mix(straddle.currentHipShift, shiftAmount, t);
        } else {
            straddle.currentHipTilt = targetTilt;
            straddle.currentHipShift = shiftAmount;
        }
    }

    // Apply hip tilt
    Joint& pelvisJoint = skeleton.joints[straddle.pelvisBoneIndex];
    glm::mat4 parentGlobal = pelvisJoint.parentIndex >= 0
        ? globalTransforms[pelvisJoint.parentIndex]
        : glm::mat4(1.0f);

    applyHipTilt(pelvisJoint, straddle.currentHipTilt * straddle.weight,
                 straddle.currentHipShift * straddle.weight, parentGlobal);

    // Apply spine counter-rotation to keep upper body upright
    if (straddle.spineBaseBoneIndex >= 0) {
        Joint& spineJoint = skeleton.joints[straddle.spineBaseBoneIndex];
        glm::mat4 spineParentGlobal = spineJoint.parentIndex >= 0
            ? globalTransforms[spineJoint.parentIndex]
            : glm::mat4(1.0f);

        // Counter-rotate spine to compensate for hip tilt
        float compensation = -straddle.currentHipTilt * 0.7f * straddle.weight;
        applySpineCompensation(spineJoint, compensation, spineParentGlobal);
    }

    // Update weight balance
    straddle.targetWeightBalance = calculateWeightBalance(leftHeight, rightHeight, 0.0f);
    if (deltaTime > 0.0f) {
        float t = glm::clamp(straddle.tiltSmoothSpeed * deltaTime, 0.0f, 1.0f);
        straddle.weightBalance = glm::mix(straddle.weightBalance, straddle.targetWeightBalance, t);
    }
}

float StraddleIKSolver::calculateHipTilt(
    float leftFootHeight,
    float rightFootHeight,
    float maxTilt,
    float maxHeightDiff
) {
    float heightDiff = rightFootHeight - leftFootHeight;
    float normalizedDiff = glm::clamp(heightDiff / maxHeightDiff, -1.0f, 1.0f);

    // Tilt hip down toward lower foot (roll rotation)
    // Positive height diff (right higher) = negative tilt (roll left)
    return -normalizedDiff * maxTilt;
}

float StraddleIKSolver::calculateWeightBalance(
    float leftFootHeight,
    float rightFootHeight,
    float characterVelocityX
) {
    // Weight shifts toward the lower foot (more stable)
    float heightDiff = rightFootHeight - leftFootHeight;
    float baseBalance = 0.5f;

    // Shift weight toward lower foot
    if (std::abs(heightDiff) > 0.01f) {
        float shift = glm::clamp(heightDiff * 2.0f, -0.3f, 0.3f);
        baseBalance -= shift;  // Negative diff = more weight on right
    }

    // Velocity affects weight distribution
    baseBalance += characterVelocityX * 0.1f;

    return glm::clamp(baseBalance, 0.0f, 1.0f);
}

void StraddleIKSolver::applyHipTilt(
    Joint& pelvisJoint,
    float tiltAngle,
    float lateralShift,
    const glm::mat4& parentGlobalTransform
) {
    glm::vec3 translation, scale;
    glm::quat rotation;
    IKUtils::decomposeTransform(pelvisJoint.localTransform, translation, rotation, scale);

    // Apply roll rotation (around forward axis, Z in local space)
    glm::quat tiltRotation = glm::angleAxis(tiltAngle, glm::vec3(0, 0, 1));
    rotation = tiltRotation * rotation;

    // Apply lateral shift
    translation.x += lateralShift;

    pelvisJoint.localTransform = IKUtils::composeTransform(translation, rotation, scale);
}

void StraddleIKSolver::applySpineCompensation(
    Joint& spineJoint,
    float compensationAngle,
    const glm::mat4& parentGlobalTransform
) {
    glm::vec3 translation, scale;
    glm::quat rotation;
    IKUtils::decomposeTransform(spineJoint.localTransform, translation, rotation, scale);

    // Counter-rotate to keep upper body upright
    glm::quat compensation = glm::angleAxis(compensationAngle, glm::vec3(0, 0, 1));
    rotation = compensation * rotation;

    spineJoint.localTransform = IKUtils::composeTransform(translation, rotation, scale);
}
