#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

void FootPlacementIKSolver::solve(
    Skeleton& skeleton,
    FootPlacementIK& foot,
    const std::vector<glm::mat4>& globalTransforms,
    const GroundQueryFunc& groundQuery,
    const glm::mat4& characterTransform,
    float deltaTime
) {
    if (!foot.enabled || foot.weight <= 0.0f) return;
    if (foot.footBoneIndex < 0 || foot.hipBoneIndex < 0 || foot.kneeBoneIndex < 0) return;
    if (!groundQuery) return;

    // Get current foot position in skeleton space (from animation)
    glm::vec3 animFootPos = IKUtils::getWorldPosition(globalTransforms[foot.footBoneIndex]);

    // Transform to world space using character transform
    glm::vec3 worldFootPos = glm::vec3(characterTransform * glm::vec4(animFootPos, 1.0f));

    // Extract scale from character transform (for converting world offsets to skeleton space)
    float scaleY = glm::length(glm::vec3(characterTransform[1]));
    if (scaleY < 0.0001f) scaleY = 1.0f;

    // Foot locking logic: when lockBlend > 0, we lock the foot's XZ world position
    // to prevent sliding during idle, while still allowing Y adjustment for ground contact
    glm::vec3 queryWorldPos = worldFootPos;

    if (foot.lockBlend > 0.0f) {
        if (!foot.isLocked) {
            // First time locking - store the current world position
            foot.lockedWorldPosition = worldFootPos;
            foot.isLocked = true;
        }

        // Blend XZ position toward locked position (keep foot planted)
        // Y still follows the animation for natural weight shifting
        queryWorldPos.x = glm::mix(worldFootPos.x, foot.lockedWorldPosition.x, foot.lockBlend);
        queryWorldPos.z = glm::mix(worldFootPos.z, foot.lockedWorldPosition.z, foot.lockBlend);
    }

    // Query ground height at the (potentially locked) foot position
    glm::vec3 rayOrigin = queryWorldPos + glm::vec3(0.0f, foot.raycastHeight, 0.0f);
    GroundQueryResult groundResult = groundQuery(rayOrigin, foot.raycastHeight + foot.raycastDistance);

    if (!groundResult.hit) {
        foot.isGrounded = false;
        return;
    }

    foot.isGrounded = true;
    foot.currentGroundHeight = groundResult.position.y;

    // Calculate target foot position in world space
    // The foot bone (ankle) should be at ground height + ankle height offset
    // Note: Skeleton data is now in meters (converted during FBX post-import processing)
    float ankleHeightAboveGround = 0.08f;  // ~8cm ankle height in meters
    float targetWorldFootY = groundResult.position.y + ankleHeightAboveGround;

    // Calculate how much the foot needs to move
    // Since skeleton is now in meters (same as world space), no scale conversion needed
    float heightOffset = targetWorldFootY - queryWorldPos.y;  // Positive = need to move up

    // Clamp the offset to reasonable bounds (in meters)
    // Positive: foot moves up (leg bends more) - max ~20cm = 0.20m
    // Negative: foot moves down (leg straightens) - max ~15cm = 0.15m
    const float maxLiftOffset = 0.20f;
    const float maxDropOffset = -0.15f;
    heightOffset = glm::clamp(heightOffset, maxDropOffset, maxLiftOffset);

    // Small threshold to avoid jitter (in meters)
    const float threshold = 0.02f;  // 2cm
    // Allow processing when foot is locked even with small offsets
    if (std::abs(heightOffset) < threshold && foot.lockBlend < 0.5f) {
        foot.isGrounded = true;
        foot.currentFootTarget = animFootPos;
        return;
    }

    // Calculate the target position in skeleton space
    // When locked, we need to convert the locked world XZ back to skeleton space
    glm::vec3 targetLocalPos;
    if (foot.lockBlend > 0.0f) {
        // Convert locked world position back to skeleton space
        glm::mat4 invCharTransform = glm::inverse(characterTransform);
        glm::vec3 lockedSkeletonPos = glm::vec3(invCharTransform * glm::vec4(queryWorldPos, 1.0f));
        // Use locked XZ, adjust Y for ground
        targetLocalPos = lockedSkeletonPos;
        targetLocalPos.y = animFootPos.y + heightOffset;
    } else {
        // Normal case: animation position adjusted for ground
        targetLocalPos = animFootPos;
        targetLocalPos.y += heightOffset;
    }

    // Initialize currentFootTarget if it's at origin (first frame)
    if (glm::length2(foot.currentFootTarget) < 0.001f) {
        foot.currentFootTarget = targetLocalPos;
    }

    // Smooth the target position
    // Use slower smoothing when locked to reduce jitter
    if (deltaTime > 0.0f) {
        float smoothSpeed = foot.lockBlend > 0.5f ? 8.0f : 20.0f;  // Slower when locked
        float t = glm::clamp(smoothSpeed * deltaTime, 0.0f, 1.0f);
        foot.currentFootTarget = glm::mix(foot.currentFootTarget, targetLocalPos, t);
    } else {
        foot.currentFootTarget = targetLocalPos;
    }

    // Create a temporary two-bone chain for leg IK
    TwoBoneIKChain legChain;
    legChain.rootBoneIndex = foot.hipBoneIndex;
    legChain.midBoneIndex = foot.kneeBoneIndex;
    legChain.endBoneIndex = foot.footBoneIndex;
    legChain.targetPosition = foot.currentFootTarget;
    legChain.poleVector = foot.poleVector;
    legChain.weight = foot.weight;
    legChain.enabled = true;

    // Solve two-bone IK for the leg
    TwoBoneIKSolver::solveBlended(skeleton, legChain, globalTransforms, foot.weight);

    // Align foot to ground slope if enabled
    if (foot.alignToGround && foot.footBoneIndex >= 0) {
        // Recompute global transforms after leg IK to get updated foot/parent orientation
        std::vector<glm::mat4> updatedGlobalTransforms;
        skeleton.computeGlobalTransforms(updatedGlobalTransforms);

        // Ground normal is in world space, transform to skeleton space
        glm::mat3 charRotInv = glm::inverse(glm::mat3(characterTransform));
        glm::vec3 targetUp = glm::normalize(charRotInv * groundResult.normal);

        // Get the foot joint's current local rotation (from animation + leg IK)
        Joint& footJoint = skeleton.joints[foot.footBoneIndex];
        glm::vec3 t, s;
        glm::quat currentLocalRot;
        IKUtils::decomposeTransform(footJoint.localTransform, t, currentLocalRot, s);

        // Get parent world rotation to convert between local and world space
        glm::mat4 footParentGlobal = skeleton.getParentGlobalTransform(foot.footBoneIndex, updatedGlobalTransforms);
        if (skeleton.joints[foot.footBoneIndex].parentIndex >= 0) {
            glm::quat parentWorldRot = glm::quat_cast(glm::mat3(footParentGlobal));

            // Compute the foot's world rotation from its current local rotation
            glm::quat footWorldRot = parentWorldRot * currentLocalRot;

            // The foot's current "up" direction in skeleton space
            // For Mixamo feet, +Z points up from the foot (perpendicular to sole)
            glm::vec3 footCurrentUp = footWorldRot * glm::vec3(0.0f, 0.0f, 1.0f);

            // Calculate rotation needed to align foot up with ground normal
            float dot = glm::dot(footCurrentUp, targetUp);

            // Compute alignment rotation
            glm::quat alignDelta = glm::quat(1, 0, 0, 0);  // Identity
            if (dot < 0.9999f && dot > -0.9999f) {
                glm::vec3 axis = glm::cross(footCurrentUp, targetUp);
                if (glm::length2(axis) > 0.0001f) {
                    float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
                    // Clamp to max foot rotation angle
                    angle = glm::clamp(angle, -foot.maxFootAngle, foot.maxFootAngle);
                    alignDelta = glm::angleAxis(angle, glm::normalize(axis));
                }
            }

            // Apply alignment in world space, then convert back to local
            glm::quat alignedWorldRot = alignDelta * footWorldRot;
            glm::quat alignedLocalRot = glm::inverse(parentWorldRot) * alignedWorldRot;

            // Smooth blend to the aligned rotation
            glm::quat targetLocalRot = alignedLocalRot;
            if (deltaTime > 0.0f) {
                float blendT = glm::clamp(8.0f * deltaTime, 0.0f, 1.0f);
                glm::quat blendedRot = glm::slerp(currentLocalRot, targetLocalRot, blendT);
                footJoint.localTransform = IKUtils::composeTransform(t, blendedRot, s);
            } else {
                footJoint.localTransform = IKUtils::composeTransform(t, targetLocalRot, s);
            }
        }
    }
}

float FootPlacementIKSolver::calculatePelvisOffset(
    const FootPlacementIK& leftFoot,
    const FootPlacementIK& rightFoot,
    float currentPelvisHeight
) {
    // Calculate how much to lower pelvis so both feet can reach ground
    float leftOffset = 0.0f;
    float rightOffset = 0.0f;

    if (leftFoot.enabled && leftFoot.isGrounded) {
        leftOffset = leftFoot.currentFootTarget.y - leftFoot.currentGroundHeight;
    }
    if (rightFoot.enabled && rightFoot.isGrounded) {
        rightOffset = rightFoot.currentFootTarget.y - rightFoot.currentGroundHeight;
    }

    // Use the larger offset (lower the pelvis by the amount needed for the lower foot)
    return glm::min(leftOffset, rightOffset);
}

void FootPlacementIKSolver::applyPelvisAdjustment(
    Skeleton& skeleton,
    PelvisAdjustment& pelvis,
    float targetOffset,
    float deltaTime
) {
    if (!pelvis.enabled || pelvis.pelvisBoneIndex < 0) return;

    // Clamp target offset
    targetOffset = glm::clamp(targetOffset, pelvis.minOffset, pelvis.maxOffset);

    // Smooth interpolation
    if (deltaTime > 0.0f && pelvis.smoothSpeed > 0.0f) {
        float t = glm::clamp(pelvis.smoothSpeed * deltaTime, 0.0f, 1.0f);
        pelvis.currentOffset = glm::mix(pelvis.currentOffset, targetOffset, t);
    } else {
        pelvis.currentOffset = targetOffset;
    }

    // Apply offset to pelvis bone
    Joint& pelvisJoint = skeleton.joints[pelvis.pelvisBoneIndex];
    glm::vec3 t, s;
    glm::quat r;
    IKUtils::decomposeTransform(pelvisJoint.localTransform, t, r, s);

    t.y += pelvis.currentOffset;

    pelvisJoint.localTransform = IKUtils::composeTransform(t, r, s);
}

glm::quat FootPlacementIKSolver::alignFootToGround(
    const glm::vec3& groundNormal,
    const glm::quat& currentRotation,
    float maxAngle
) {
    // Calculate rotation to align foot up vector with ground normal
    glm::vec3 footUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 targetUp = glm::normalize(groundNormal);

    float dot = glm::dot(footUp, targetUp);
    if (dot > 0.9999f) {
        return glm::quat(1, 0, 0, 0);  // Already aligned
    }

    glm::vec3 axis = glm::cross(footUp, targetUp);
    if (glm::length2(axis) < 0.0001f) {
        return glm::quat(1, 0, 0, 0);
    }

    float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));

    // Clamp angle
    angle = glm::clamp(angle, -maxAngle, maxAngle);

    return glm::angleAxis(angle, glm::normalize(axis));
}
