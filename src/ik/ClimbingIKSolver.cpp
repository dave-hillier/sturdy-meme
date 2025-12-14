#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

void ClimbingIKSolver::solve(
    Skeleton& skeleton,
    ClimbingIK& climbing,
    std::vector<TwoBoneIKChain>& armChains,
    std::vector<TwoBoneIKChain>& legChains,
    const std::vector<glm::mat4>& globalTransforms,
    const glm::mat4& characterTransform,
    float deltaTime
) {
    if (!climbing.enabled || climbing.weight <= 0.0f) return;

    // Update transition
    float targetTransition = climbing.enabled ? 1.0f : 0.0f;
    if (deltaTime > 0.0f) {
        float t = glm::clamp(climbing.transitionSpeed * deltaTime, 0.0f, 1.0f);
        climbing.currentTransition = glm::mix(climbing.currentTransition, targetTransition, t);
    }

    if (climbing.currentTransition < 0.01f) return;

    // Calculate and apply body position
    positionBody(skeleton, climbing, globalTransforms, deltaTime);

    // Solve arm IK for hand holds
    if (armChains.size() >= 2) {
        // Left arm
        if (climbing.leftHandHold.isValid) {
            armChains[0].targetPosition = climbing.leftHandHold.position;
            armChains[0].enabled = true;
            armChains[0].weight = climbing.weight * climbing.currentTransition;
            TwoBoneIKSolver::solveBlended(skeleton, armChains[0], globalTransforms, armChains[0].weight);

            // Orient hand to grip
            if (climbing.leftHandBoneIndex >= 0) {
                int32_t parentIdx = skeleton.joints[climbing.leftHandBoneIndex].parentIndex;
                glm::mat4 parentGlobal = (parentIdx >= 0 && static_cast<size_t>(parentIdx) < globalTransforms.size())
                    ? globalTransforms[parentIdx]
                    : glm::mat4(1.0f);
                orientHandToHold(skeleton.joints[climbing.leftHandBoneIndex],
                                climbing.leftHandHold,
                                parentGlobal);
            }
        }

        // Right arm
        if (climbing.rightHandHold.isValid) {
            armChains[1].targetPosition = climbing.rightHandHold.position;
            armChains[1].enabled = true;
            armChains[1].weight = climbing.weight * climbing.currentTransition;
            TwoBoneIKSolver::solveBlended(skeleton, armChains[1], globalTransforms, armChains[1].weight);

            if (climbing.rightHandBoneIndex >= 0) {
                int32_t parentIdx = skeleton.joints[climbing.rightHandBoneIndex].parentIndex;
                glm::mat4 parentGlobal = (parentIdx >= 0 && static_cast<size_t>(parentIdx) < globalTransforms.size())
                    ? globalTransforms[parentIdx]
                    : glm::mat4(1.0f);
                orientHandToHold(skeleton.joints[climbing.rightHandBoneIndex],
                                climbing.rightHandHold,
                                parentGlobal);
            }
        }
    }

    // Solve leg IK for foot holds
    if (legChains.size() >= 2) {
        // Left leg
        if (climbing.leftFootHold.isValid) {
            legChains[0].targetPosition = climbing.leftFootHold.position;
            legChains[0].enabled = true;
            legChains[0].weight = climbing.weight * climbing.currentTransition;
            TwoBoneIKSolver::solveBlended(skeleton, legChains[0], globalTransforms, legChains[0].weight);
        }

        // Right leg
        if (climbing.rightFootHold.isValid) {
            legChains[1].targetPosition = climbing.rightFootHold.position;
            legChains[1].enabled = true;
            legChains[1].weight = climbing.weight * climbing.currentTransition;
            TwoBoneIKSolver::solveBlended(skeleton, legChains[1], globalTransforms, legChains[1].weight);
        }
    }
}

glm::vec3 ClimbingIKSolver::calculateBodyPosition(
    const ClimbingIK& climbing,
    const glm::mat4& characterTransform
) {
    // Calculate center of active holds
    glm::vec3 holdCenter(0.0f);
    int holdCount = 0;

    if (climbing.leftHandHold.isValid) {
        holdCenter += climbing.leftHandHold.position;
        holdCount++;
    }
    if (climbing.rightHandHold.isValid) {
        holdCenter += climbing.rightHandHold.position;
        holdCount++;
    }
    if (climbing.leftFootHold.isValid) {
        holdCenter += climbing.leftFootHold.position;
        holdCount++;
    }
    if (climbing.rightFootHold.isValid) {
        holdCenter += climbing.rightFootHold.position;
        holdCount++;
    }

    if (holdCount > 0) {
        holdCenter /= static_cast<float>(holdCount);
    }

    // Position body at wall distance from surface
    glm::vec3 bodyPos = holdCenter + climbing.wallNormal * climbing.wallDistance;

    // Adjust height to be between hands and feet
    float handHeight = 0.0f;
    float footHeight = 0.0f;
    int handCount = 0;
    int footCount = 0;

    if (climbing.leftHandHold.isValid) { handHeight += climbing.leftHandHold.position.y; handCount++; }
    if (climbing.rightHandHold.isValid) { handHeight += climbing.rightHandHold.position.y; handCount++; }
    if (climbing.leftFootHold.isValid) { footHeight += climbing.leftFootHold.position.y; footCount++; }
    if (climbing.rightFootHold.isValid) { footHeight += climbing.rightFootHold.position.y; footCount++; }

    if (handCount > 0) handHeight /= handCount;
    if (footCount > 0) footHeight /= footCount;

    // Body should be roughly 60% of the way from feet to hands
    if (handCount > 0 && footCount > 0) {
        bodyPos.y = footHeight + (handHeight - footHeight) * 0.6f;
    }

    return bodyPos;
}

glm::quat ClimbingIKSolver::calculateBodyRotation(
    const glm::vec3& wallNormal,
    const glm::vec3& upVector
) {
    // Face toward wall (opposite of normal)
    glm::vec3 forward = -glm::normalize(wallNormal);
    glm::vec3 up = glm::normalize(upVector);
    glm::vec3 right = glm::normalize(glm::cross(up, forward));
    up = glm::cross(forward, right);

    glm::mat3 rotMat(right, up, forward);
    return glm::quat_cast(rotMat);
}

void ClimbingIKSolver::setHandHold(
    ClimbingIK& climbing,
    bool isLeft,
    const glm::vec3& position,
    const glm::vec3& normal,
    const glm::vec3& gripDir
) {
    HandHold& hold = isLeft ? climbing.leftHandHold : climbing.rightHandHold;
    hold.position = position;
    hold.normal = normal;
    hold.gripDirection = gripDir;
    hold.isValid = true;
}

void ClimbingIKSolver::setFootHold(
    ClimbingIK& climbing,
    bool isLeft,
    const glm::vec3& position,
    const glm::vec3& normal
) {
    FootHold& hold = isLeft ? climbing.leftFootHold : climbing.rightFootHold;
    hold.position = position;
    hold.normal = normal;
    hold.isValid = true;
}

void ClimbingIKSolver::clearHandHold(ClimbingIK& climbing, bool isLeft) {
    HandHold& hold = isLeft ? climbing.leftHandHold : climbing.rightHandHold;
    hold.isValid = false;
}

void ClimbingIKSolver::clearFootHold(ClimbingIK& climbing, bool isLeft) {
    FootHold& hold = isLeft ? climbing.leftFootHold : climbing.rightFootHold;
    hold.isValid = false;
}

bool ClimbingIKSolver::canReach(
    const ClimbingIK& climbing,
    const glm::vec3& holdPosition,
    bool isArm,
    bool isLeft,
    const std::vector<glm::mat4>& globalTransforms
) {
    // Get shoulder/hip bone index (the root of the arm/leg chain)
    int32_t rootBoneIndex;
    if (isArm) {
        rootBoneIndex = isLeft ? climbing.leftShoulderBoneIndex : climbing.rightShoulderBoneIndex;
    } else {
        rootBoneIndex = isLeft ? climbing.leftHipBoneIndex : climbing.rightHipBoneIndex;
    }

    if (rootBoneIndex < 0 || static_cast<size_t>(rootBoneIndex) >= globalTransforms.size()) {
        return false;
    }

    glm::vec3 rootPos = IKUtils::getWorldPosition(globalTransforms[rootBoneIndex]);
    float distance = glm::length(holdPosition - rootPos);
    float maxReach = isArm ? climbing.maxArmReach : climbing.maxLegReach;

    return distance <= maxReach;
}

void ClimbingIKSolver::positionBody(
    Skeleton& skeleton,
    ClimbingIK& climbing,
    const std::vector<glm::mat4>& globalTransforms,
    float deltaTime
) {
    if (climbing.pelvisBoneIndex < 0) return;

    // Calculate target body position
    climbing.targetBodyPosition = calculateBodyPosition(climbing, glm::mat4(1.0f));
    climbing.targetBodyRotation = calculateBodyRotation(climbing.wallNormal, glm::vec3(0, 1, 0));

    // Smooth interpolation
    if (deltaTime > 0.0f) {
        float t = glm::clamp(climbing.transitionSpeed * deltaTime, 0.0f, 1.0f);
        climbing.currentBodyPosition = glm::mix(climbing.currentBodyPosition, climbing.targetBodyPosition, t);
        climbing.currentBodyRotation = glm::slerp(climbing.currentBodyRotation, climbing.targetBodyRotation, t);
    }

    // Apply to pelvis (root of body hierarchy)
    Joint& pelvisJoint = skeleton.joints[climbing.pelvisBoneIndex];
    glm::vec3 translation, scale;
    glm::quat rotation;
    IKUtils::decomposeTransform(pelvisJoint.localTransform, translation, rotation, scale);

    // Blend between animation and climbing position
    float blend = climbing.weight * climbing.currentTransition;

    // For climbing, we override the pelvis position to be relative to holds
    // This is a simplification - full implementation would transform to local space
    glm::quat blendedRotation = glm::slerp(rotation, climbing.currentBodyRotation, blend);

    pelvisJoint.localTransform = IKUtils::composeTransform(translation, blendedRotation, scale);
}

void ClimbingIKSolver::orientHandToHold(
    Joint& handJoint,
    const HandHold& hold,
    const glm::mat4& parentGlobalTransform
) {
    glm::vec3 translation, scale;
    glm::quat rotation;
    IKUtils::decomposeTransform(handJoint.localTransform, translation, rotation, scale);

    // Orient hand so palm faces hold surface
    glm::vec3 palmNormal = -hold.normal;  // Palm faces into surface
    glm::vec3 fingerDir = hold.gripDirection;
    glm::vec3 thumbDir = glm::cross(palmNormal, fingerDir);

    glm::mat3 handRotMat(thumbDir, palmNormal, fingerDir);
    glm::quat worldRotation = glm::quat_cast(handRotMat);

    // Convert to local space
    glm::quat parentRotation = glm::quat_cast(glm::mat3(parentGlobalTransform));
    glm::quat localRotation = glm::inverse(parentRotation) * worldRotation;

    handJoint.localTransform = IKUtils::composeTransform(translation, localRotation, scale);
}

void ClimbingIKSolver::orientFootToHold(
    Joint& footJoint,
    const FootHold& hold,
    const glm::mat4& parentGlobalTransform
) {
    glm::vec3 translation, scale;
    glm::quat rotation;
    IKUtils::decomposeTransform(footJoint.localTransform, translation, rotation, scale);

    // Orient foot so sole contacts hold
    glm::vec3 footUp = hold.normal;
    glm::vec3 footForward = glm::vec3(0, 0, 1);  // Default forward

    // Make forward perpendicular to normal
    footForward = glm::normalize(footForward - glm::dot(footForward, footUp) * footUp);
    glm::vec3 footRight = glm::cross(footUp, footForward);

    glm::mat3 footRotMat(footRight, footUp, footForward);
    glm::quat worldRotation = glm::quat_cast(footRotMat);

    glm::quat parentRotation = glm::quat_cast(glm::mat3(parentGlobalTransform));
    glm::quat localRotation = glm::inverse(parentRotation) * worldRotation;

    footJoint.localTransform = IKUtils::composeTransform(translation, localRotation, scale);
}
