#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

float TwoBoneIKSolver::angleBetween(const glm::vec3& a, const glm::vec3& b) {
    float dot = glm::dot(glm::normalize(a), glm::normalize(b));
    return std::acos(glm::clamp(dot, -1.0f, 1.0f));
}

glm::quat TwoBoneIKSolver::applyJointLimits(const glm::quat& rotation, const JointLimits& limits) {
    if (!limits.enabled) {
        return rotation;
    }

    // Use swing-twist decomposition to avoid gimbal lock issues
    // This is more robust than euler angles for joint limits
    //
    // Decompose rotation into twist (around primary axis, typically Y for elbows/knees)
    // and swing (rotation perpendicular to twist axis)
    glm::vec3 twistAxis = glm::vec3(0.0f, 1.0f, 0.0f);  // Primary rotation axis

    // Project rotation axis onto twist axis to get twist component
    glm::vec3 rotAxis = glm::axis(rotation);
    float rotAngle = glm::angle(rotation);

    // Handle identity quaternion
    if (rotAngle < 0.0001f) {
        return rotation;
    }

    // Twist component: rotation around twist axis
    float twistAmount = glm::dot(rotAxis, twistAxis) * rotAngle;
    glm::quat twist = glm::angleAxis(twistAmount, twistAxis);

    // Swing component: remaining rotation
    glm::quat swing = rotation * glm::inverse(twist);

    // Clamp twist angle (Y rotation - typically the main bend axis)
    float clampedTwist = glm::clamp(twistAmount, limits.minAngles.y, limits.maxAngles.y);
    glm::quat clampedTwistQuat = glm::angleAxis(clampedTwist, twistAxis);

    // Clamp swing using cone limit (combined X and Z limits)
    // Extract swing angle and axis
    float swingAngle = glm::angle(swing);
    if (swingAngle > 0.0001f) {
        glm::vec3 swingAxis = glm::axis(swing);

        // Calculate limit based on swing direction
        // Use elliptical cone: limit varies based on swing direction
        float xComponent = std::abs(swingAxis.x);
        float zComponent = std::abs(swingAxis.z);
        float maxSwingX = std::max(std::abs(limits.minAngles.x), std::abs(limits.maxAngles.x));
        float maxSwingZ = std::max(std::abs(limits.minAngles.z), std::abs(limits.maxAngles.z));

        // Elliptical interpolation of limit
        float maxSwing = maxSwingX;
        if (xComponent + zComponent > 0.0001f) {
            float t = zComponent / (xComponent + zComponent);
            maxSwing = glm::mix(maxSwingX, maxSwingZ, t);
        }

        // Clamp swing angle
        if (swingAngle > maxSwing) {
            swing = glm::angleAxis(maxSwing, swingAxis);
        }
    }

    // Recombine: swing * twist
    return glm::normalize(swing * clampedTwistQuat);
}

bool TwoBoneIKSolver::solve(
    Skeleton& skeleton,
    const TwoBoneIKChain& chain,
    const std::vector<glm::mat4>& globalTransforms
) {
    if (!chain.enabled) return false;
    if (chain.rootBoneIndex < 0 || chain.midBoneIndex < 0 || chain.endBoneIndex < 0) {
        return false;
    }
    if (static_cast<size_t>(chain.rootBoneIndex) >= skeleton.joints.size() ||
        static_cast<size_t>(chain.midBoneIndex) >= skeleton.joints.size() ||
        static_cast<size_t>(chain.endBoneIndex) >= skeleton.joints.size()) {
        return false;
    }

    // Get current world positions
    glm::vec3 rootPos = IKUtils::getWorldPosition(globalTransforms[chain.rootBoneIndex]);
    glm::vec3 midPos = IKUtils::getWorldPosition(globalTransforms[chain.midBoneIndex]);
    glm::vec3 endPos = IKUtils::getWorldPosition(globalTransforms[chain.endBoneIndex]);
    glm::vec3 targetPos = chain.targetPosition;

    // Calculate bone lengths
    float upperLen = glm::length(midPos - rootPos);
    float lowerLen = glm::length(endPos - midPos);
    float totalLen = upperLen + lowerLen;

    if (upperLen < 0.0001f || lowerLen < 0.0001f) {
        return false;  // Degenerate bones
    }

    // Vector from root to target
    glm::vec3 toTarget = targetPos - rootPos;
    float targetDist = glm::length(toTarget);

    // Clamp target distance to reachable range
    const float minReach = std::abs(upperLen - lowerLen) + 0.001f;
    const float maxReach = totalLen - 0.001f;

    bool reachable = true;
    if (targetDist > maxReach) {
        targetDist = maxReach;
        targetPos = rootPos + glm::normalize(toTarget) * targetDist;
        reachable = false;
    } else if (targetDist < minReach) {
        targetDist = minReach;
        targetPos = rootPos + glm::normalize(toTarget) * targetDist;
        reachable = false;
    }

    toTarget = targetPos - rootPos;
    glm::vec3 targetDir = glm::normalize(toTarget);

    // Use law of cosines to find the angle at the mid joint (elbow/knee)
    // c^2 = a^2 + b^2 - 2ab*cos(C)
    // cos(C) = (a^2 + b^2 - c^2) / (2ab)
    float cosAngle = (upperLen * upperLen + lowerLen * lowerLen - targetDist * targetDist)
                     / (2.0f * upperLen * lowerLen);
    cosAngle = glm::clamp(cosAngle, -1.0f, 1.0f);
    float midAngle = std::acos(cosAngle);  // Angle at elbow/knee

    // Angle at root joint (shoulder/hip)
    // Using law of cosines again
    float cosRootAngle = (upperLen * upperLen + targetDist * targetDist - lowerLen * lowerLen)
                         / (2.0f * upperLen * targetDist);
    cosRootAngle = glm::clamp(cosRootAngle, -1.0f, 1.0f);
    float rootAngle = std::acos(cosRootAngle);

    // Get parent transforms for converting world rotations to local
    Joint& rootJoint = skeleton.joints[chain.rootBoneIndex];
    Joint& midJoint = skeleton.joints[chain.midBoneIndex];

    // Get current local rotations
    // Note: localTransform = T * Rpre * R * S (pre-rotation is baked in)
    // We need to extract just R for IK, then recompose with Rpre
    glm::vec3 rootTranslation, midTranslation, rootScale, midScale;
    glm::quat rootLocalRotWithPre, midLocalRotWithPre;
    IKUtils::decomposeTransform(rootJoint.localTransform, rootTranslation, rootLocalRotWithPre, rootScale);
    IKUtils::decomposeTransform(midJoint.localTransform, midTranslation, midLocalRotWithPre, midScale);

    // Extract the animated rotation by removing pre-rotation
    // localRotWithPre = Rpre * R, so R = inverse(Rpre) * localRotWithPre
    glm::quat rootAnimRot = glm::inverse(rootJoint.preRotation) * rootLocalRotWithPre;
    glm::quat midAnimRot = glm::inverse(midJoint.preRotation) * midLocalRotWithPre;

    // Calculate bend direction from pole vector
    // The pole vector indicates which way the elbow/knee should point
    // We need to project it onto the plane perpendicular to the target direction
    glm::vec3 poleDir = glm::normalize(chain.poleVector);

    // Project pole vector onto plane perpendicular to target direction
    // bendDir = poleDir - (poleDir . targetDir) * targetDir
    float poleDotTarget = glm::dot(poleDir, targetDir);
    glm::vec3 bendDir = poleDir - poleDotTarget * targetDir;

    if (glm::length2(bendDir) < 0.0001f) {
        // Pole vector is aligned with target direction, use a default perpendicular
        bendDir = glm::vec3(0, 0, 1);  // Default forward
        if (std::abs(glm::dot(targetDir, bendDir)) > 0.99f) {
            bendDir = glm::vec3(0, 1, 0);
        }
        bendDir = bendDir - glm::dot(bendDir, targetDir) * targetDir;
    }
    bendDir = glm::normalize(bendDir);

    // Calculate new mid position
    // The mid joint lies at distance upperLen from root, at angle rootAngle from target direction
    glm::vec3 newMidPos = rootPos
                          + targetDir * (upperLen * std::cos(rootAngle))
                          + bendDir * (upperLen * std::sin(rootAngle));

    // Calculate rotations for root bone
    // Current bone direction (from animation)
    glm::vec3 currentRootDir = glm::normalize(midPos - rootPos);
    glm::vec3 newRootDir = glm::normalize(newMidPos - rootPos);

    // Get parent's world rotation to convert to local space
    glm::mat4 parentGlobal = skeleton.getParentGlobalTransform(chain.rootBoneIndex, globalTransforms);
    glm::quat parentWorldRot = glm::quat_cast(glm::mat3(parentGlobal));
    glm::quat parentWorldRotInv = glm::inverse(parentWorldRot);

    // The key insight: we need to rotate the parent bone such that when the child's
    // local transform is applied, the child ends up at the desired position.
    //
    // Child world pos = Parent world transform * Child local translation
    // We want to find a new parent rotation such that:
    // newMidPos = parentPos + parentRot * midLocalTranslation
    //
    // So we need: parentRot * midLocalTranslation = (newMidPos - parentPos)
    // Which means: parentRot = rotation that takes midLocalTranslation to (newMidPos - parentPos)

    // Get the mid bone's local translation (offset from parent in parent's local space)
    glm::vec3 midLocalTranslation = glm::vec3(midJoint.localTransform[3]);

    // Current: hip rotation transforms midLocalTranslation to (midPos - rootPos)
    // We want: hip rotation transforms midLocalTranslation to (newMidPos - rootPos)
    glm::vec3 currentChildOffset = midPos - rootPos;
    glm::vec3 desiredChildOffset = newMidPos - rootPos;

    // Normalize to get directions (the lengths should be equal since we're not changing bone length)
    glm::vec3 currentOffsetDir = glm::normalize(currentChildOffset);
    glm::vec3 desiredOffsetDir = glm::normalize(desiredChildOffset);

    // Calculate the rotation that takes currentOffsetDir to desiredOffsetDir
    glm::quat rootRotDelta = IKUtils::aimAt(currentOffsetDir, desiredOffsetDir, chain.poleVector);

    // Apply this rotation to the current world rotation
    glm::quat currentWorldRot = glm::quat_cast(glm::mat3(globalTransforms[chain.rootBoneIndex]));
    glm::quat newWorldRot = rootRotDelta * currentWorldRot;

    // Convert to local space (this gives us Rpre * newR)
    glm::quat newLocalRotWithPre = parentWorldRotInv * newWorldRot;

    // Extract just the animated rotation: newR = inverse(Rpre) * newLocalRotWithPre
    glm::quat newRootAnimRot = glm::inverse(rootJoint.preRotation) * newLocalRotWithPre;

    // Recompose: localTransform = T * Rpre * R * S
    glm::quat finalRootLocalRot = rootJoint.preRotation * newRootAnimRot;

    // Calculate rotations for mid bone using the same approach
    glm::vec3 currentEndOffset = endPos - midPos;
    glm::vec3 desiredEndOffset = targetPos - newMidPos;
    glm::vec3 currentEndDir = glm::normalize(currentEndOffset);
    glm::vec3 desiredEndDir = glm::normalize(desiredEndOffset);

    // Mid bone's parent is root bone (after IK), so we need the new root global transform
    // Use finalRootLocalRot which includes pre-rotation
    glm::mat4 newRootGlobal;
    if (rootJoint.parentIndex >= 0) {
        newRootGlobal = globalTransforms[rootJoint.parentIndex] *
                        IKUtils::composeTransform(rootTranslation, finalRootLocalRot, rootScale);
    } else {
        newRootGlobal = IKUtils::composeTransform(rootTranslation, finalRootLocalRot, rootScale);
    }

    glm::quat midParentWorldRot = glm::quat_cast(glm::mat3(newRootGlobal));
    glm::quat midParentWorldRotInv = glm::inverse(midParentWorldRot);

    glm::quat midRotDelta = IKUtils::aimAt(currentEndDir, desiredEndDir, chain.poleVector);
    glm::quat currentMidWorldRot = glm::quat_cast(glm::mat3(globalTransforms[chain.midBoneIndex]));
    glm::quat newMidWorldRot = midRotDelta * currentMidWorldRot;

    // Convert to local space (includes pre-rotation)
    glm::quat newMidLocalRotWithPre = midParentWorldRotInv * newMidWorldRot;

    // Extract animated rotation and recompose
    glm::quat newMidAnimRot = glm::inverse(midJoint.preRotation) * newMidLocalRotWithPre;

    // Apply joint limits to the animated rotation
    newMidAnimRot = applyJointLimits(newMidAnimRot, chain.midBoneLimits);

    // Recompose with pre-rotation
    glm::quat finalMidLocalRot = midJoint.preRotation * newMidAnimRot;

    // Update skeleton local transforms
    rootJoint.localTransform = IKUtils::composeTransform(rootTranslation, finalRootLocalRot, rootScale);
    midJoint.localTransform = IKUtils::composeTransform(midTranslation, finalMidLocalRot, midScale);

    return reachable;
}

bool TwoBoneIKSolver::solveBlended(
    Skeleton& skeleton,
    const TwoBoneIKChain& chain,
    const std::vector<glm::mat4>& globalTransforms,
    float weight
) {
    if (weight <= 0.0f) return true;
    if (weight >= 1.0f) return solve(skeleton, chain, globalTransforms);

    // Store original transforms
    glm::mat4 origRootTransform = skeleton.joints[chain.rootBoneIndex].localTransform;
    glm::mat4 origMidTransform = skeleton.joints[chain.midBoneIndex].localTransform;

    // Solve IK
    bool result = solve(skeleton, chain, globalTransforms);

    // Blend between original and IK result
    glm::vec3 origRootT, origMidT, origRootS, origMidS;
    glm::quat origRootR, origMidR;
    IKUtils::decomposeTransform(origRootTransform, origRootT, origRootR, origRootS);
    IKUtils::decomposeTransform(origMidTransform, origMidT, origMidR, origMidS);

    glm::vec3 ikRootT, ikMidT, ikRootS, ikMidS;
    glm::quat ikRootR, ikMidR;
    IKUtils::decomposeTransform(skeleton.joints[chain.rootBoneIndex].localTransform, ikRootT, ikRootR, ikRootS);
    IKUtils::decomposeTransform(skeleton.joints[chain.midBoneIndex].localTransform, ikMidT, ikMidR, ikMidS);

    // Interpolate
    glm::quat blendedRootR = glm::slerp(origRootR, ikRootR, weight);
    glm::quat blendedMidR = glm::slerp(origMidR, ikMidR, weight);

    skeleton.joints[chain.rootBoneIndex].localTransform =
        IKUtils::composeTransform(origRootT, blendedRootR, origRootS);
    skeleton.joints[chain.midBoneIndex].localTransform =
        IKUtils::composeTransform(origMidT, blendedMidR, origMidS);

    return result;
}
