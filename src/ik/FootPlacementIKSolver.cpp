#include "IKSolver.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>

// Calculate how extended the leg would be to reach a target
static float calculateExtensionRatio(
    const std::vector<glm::mat4>& globalTransforms,
    int32_t hipBoneIndex,
    int32_t kneeBoneIndex,
    int32_t footBoneIndex,
    const glm::vec3& targetPosition,
    float legLength
) {
    if (legLength <= 0.0f) return 0.0f;

    glm::vec3 hipPos = IKUtils::getWorldPosition(globalTransforms[hipBoneIndex]);
    float distanceToTarget = glm::length(targetPosition - hipPos);

    return glm::clamp(distanceToTarget / legLength, 0.0f, 1.5f);
}

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

    // Store animation foot position for pelvis offset calculation
    foot.animationFootPosition = animFootPos;

    // Transform to world space using character transform
    glm::vec3 worldFootPos = glm::vec3(characterTransform * glm::vec4(animFootPos, 1.0f));

    // Clear lock state when lockBlend reaches zero
    if (foot.lockBlend <= 0.0f) {
        foot.isLocked = false;
        foot.lockedWorldPosition = glm::vec3(0.0f);
        foot.lockedGroundNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // During swing phase, reduce IK influence to let animation play
    float phaseWeight = 1.0f;
    if (foot.currentPhase == FootPhase::Swing) {
        // Blend in IK toward end of swing (preparing for contact)
        phaseWeight = foot.phaseProgress > 0.7f ? (foot.phaseProgress - 0.7f) / 0.3f : 0.0f;
    } else if (foot.currentPhase == FootPhase::PushOff) {
        // Blend out IK during push-off
        phaseWeight = 1.0f - foot.phaseProgress;
    }

    // Foot locking logic: when lockBlend > 0, lock the foot's world position
    glm::vec3 queryWorldPos = worldFootPos;

    if (foot.lockBlend > 0.0f) {
        if (!foot.isLocked) {
            // First time locking - store the current world position
            foot.lockedWorldPosition = worldFootPos;
            foot.isLocked = true;
        }

        // Check if locked position is too far from animation position (would break silhouette)
        // If so, release the lock and let foot slide (per Simon Clavet's GDC recommendation)
        float distanceFromAnim = glm::length(glm::vec2(foot.lockedWorldPosition.x - worldFootPos.x,
                                                        foot.lockedWorldPosition.z - worldFootPos.z));
        const float maxLockDistance = 0.15f;  // 15cm max deviation from animation

        if (distanceFromAnim > maxLockDistance) {
            // Too far - release lock
            foot.isLocked = false;
            foot.lockedWorldPosition = worldFootPos;
        } else {
            // Blend full position toward locked position (including Y for slope handling)
            queryWorldPos = glm::mix(worldFootPos, foot.lockedWorldPosition, foot.lockBlend);
        }
    }

    // Query ground height at the (potentially locked) foot position
    glm::vec3 rayOrigin = queryWorldPos + glm::vec3(0.0f, foot.raycastHeight, 0.0f);
    GroundQueryResult groundResult = groundQuery(rayOrigin, foot.raycastHeight + foot.raycastDistance);

    if (!groundResult.hit) {
        foot.isGrounded = false;
        foot.targetUnreachable = true;
        return;
    }

    foot.isGrounded = true;
    foot.currentGroundHeight = groundResult.position.y;

    // When locking, also store the ground normal for consistent foot orientation
    if (foot.lockBlend > 0.0f && foot.isLocked) {
        if (glm::length2(foot.lockedGroundNormal) < 0.5f) {
            foot.lockedGroundNormal = groundResult.normal;
        }
        // Update locked Y position to match ground at locked XZ
        foot.lockedWorldPosition.y = groundResult.position.y + foot.ankleHeightAboveGround;
    }

    // Calculate target foot position in world space
    // Use the skeleton-derived ankle height
    float targetWorldFootY = groundResult.position.y + foot.ankleHeightAboveGround;

    // Calculate how much the foot needs to move
    float heightOffset = targetWorldFootY - queryWorldPos.y;

    // Clamp the offset to reasonable bounds (in meters)
    const float maxLiftOffset = 0.20f;
    const float maxDropOffset = -0.15f;
    heightOffset = glm::clamp(heightOffset, maxDropOffset, maxLiftOffset);

    // Small threshold to avoid jitter (in meters)
    const float threshold = 0.02f;
    if (std::abs(heightOffset) < threshold && foot.lockBlend < 0.5f && phaseWeight < 0.5f) {
        foot.isGrounded = true;
        foot.currentFootTarget = animFootPos;
        foot.targetUnreachable = false;
        return;
    }

    // Calculate the target position in skeleton space
    glm::vec3 targetLocalPos;
    if (foot.lockBlend > 0.0f && foot.isLocked) {
        // Convert locked world position back to skeleton space
        glm::mat4 invCharTransform = glm::inverse(characterTransform);
        glm::vec3 lockedSkeletonPos = glm::vec3(invCharTransform * glm::vec4(queryWorldPos, 1.0f));
        targetLocalPos = lockedSkeletonPos;
        targetLocalPos.y = animFootPos.y + heightOffset;
    } else {
        // Normal case: animation position adjusted for ground
        targetLocalPos = animFootPos;
        targetLocalPos.y += heightOffset;
    }

    // Check if target is reachable (leg extension)
    if (foot.legLength > 0.0f) {
        foot.currentExtensionRatio = calculateExtensionRatio(
            globalTransforms, foot.hipBoneIndex, foot.kneeBoneIndex, foot.footBoneIndex,
            targetLocalPos, foot.legLength);

        // If over 95% extended, target is unreachable - blend down IK weight
        if (foot.currentExtensionRatio > 0.95f) {
            foot.targetUnreachable = true;
            // Smoothly reduce weight as we approach full extension
            float reachWeight = glm::clamp(1.0f - (foot.currentExtensionRatio - 0.9f) * 10.0f, 0.0f, 1.0f);
            phaseWeight *= reachWeight;
        } else {
            foot.targetUnreachable = false;
        }
    }

    // Initialize currentFootTarget if it's at origin (first frame)
    if (glm::length2(foot.currentFootTarget) < 0.001f) {
        foot.currentFootTarget = targetLocalPos;
    }

    // Smooth the target position
    if (deltaTime > 0.0f) {
        float smoothSpeed = foot.lockBlend > 0.5f ? 8.0f : 20.0f;
        float t = glm::clamp(smoothSpeed * deltaTime, 0.0f, 1.0f);
        foot.currentFootTarget = glm::mix(foot.currentFootTarget, targetLocalPos, t);
    } else {
        foot.currentFootTarget = targetLocalPos;
    }

    // Calculate effective IK weight based on phase and reachability
    float effectiveWeight = foot.weight * phaseWeight;
    if (effectiveWeight <= 0.0f) return;

    // Create a temporary two-bone chain for leg IK
    TwoBoneIKChain legChain;
    legChain.rootBoneIndex = foot.hipBoneIndex;
    legChain.midBoneIndex = foot.kneeBoneIndex;
    legChain.endBoneIndex = foot.footBoneIndex;
    legChain.targetPosition = foot.currentFootTarget;
    legChain.poleVector = foot.poleVector;
    legChain.weight = effectiveWeight;
    legChain.enabled = true;

    // Solve two-bone IK for the leg
    TwoBoneIKSolver::solveBlended(skeleton, legChain, globalTransforms, effectiveWeight);

    // Align foot to ground slope if enabled
    if (foot.alignToGround && foot.footBoneIndex >= 0 && effectiveWeight > 0.1f) {
        // Recompute global transforms after leg IK to get updated foot/parent orientation
        std::vector<glm::mat4> updatedGlobalTransforms;
        skeleton.computeGlobalTransforms(updatedGlobalTransforms);

        // Use locked ground normal during stance for stability, otherwise use current
        glm::vec3 groundNormal = (foot.lockBlend > 0.5f && foot.isLocked)
            ? foot.lockedGroundNormal
            : groundResult.normal;

        // Ground normal is in world space, transform to skeleton space
        glm::mat3 charRotInv = glm::inverse(glm::mat3(characterTransform));
        glm::vec3 targetUp = glm::normalize(charRotInv * groundNormal);

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

            // Use the skeleton-derived foot up vector instead of hardcoded axis
            glm::vec3 footCurrentUp = footWorldRot * foot.footUpVector;

            // Calculate rotation needed to align foot up with ground normal
            float dot = glm::dot(footCurrentUp, targetUp);

            // Compute alignment rotation
            glm::quat alignDelta = glm::quat(1, 0, 0, 0);
            if (dot < 0.9999f && dot > -0.9999f) {
                glm::vec3 axis = glm::cross(footCurrentUp, targetUp);
                if (glm::length2(axis) > 0.0001f) {
                    float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
                    angle = glm::clamp(angle, -foot.maxFootAngle, foot.maxFootAngle);
                    alignDelta = glm::angleAxis(angle, glm::normalize(axis));
                }
            }

            // Apply alignment in world space, then convert back to local
            glm::quat alignedWorldRot = alignDelta * footWorldRot;
            glm::quat alignedLocalRot = glm::inverse(parentWorldRot) * alignedWorldRot;

            // Smooth blend to the aligned rotation
            if (deltaTime > 0.0f) {
                float blendT = glm::clamp(8.0f * deltaTime * effectiveWeight, 0.0f, 1.0f);
                glm::quat blendedRot = glm::slerp(currentLocalRot, alignedLocalRot, blendT);
                footJoint.localTransform = IKUtils::composeTransform(t, blendedRot, s);
            } else {
                glm::quat blendedRot = glm::slerp(currentLocalRot, alignedLocalRot, effectiveWeight);
                footJoint.localTransform = IKUtils::composeTransform(t, blendedRot, s);
            }
        }
    }
}

float FootPlacementIKSolver::calculatePelvisOffset(
    const FootPlacementIK& leftFoot,
    const FootPlacementIK& rightFoot,
    float currentPelvisHeight
) {
    // Following ozz-animation approach: calculate how much lower ground is than animation position
    // Pelvis drops by the amount needed for the lowest foot to reach its target
    float leftDrop = 0.0f;
    float rightDrop = 0.0f;

    if (leftFoot.enabled && leftFoot.isGrounded && !leftFoot.targetUnreachable) {
        // Ground target Y = groundHeight + ankleHeight
        // Animation foot Y = animationFootPosition.y (in skeleton space)
        // Drop needed = groundTargetY - animFootY (negative if ground is lower)
        float groundTargetY = leftFoot.currentGroundHeight + leftFoot.ankleHeightAboveGround;
        leftDrop = groundTargetY - leftFoot.animationFootPosition.y;
    }

    if (rightFoot.enabled && rightFoot.isGrounded && !rightFoot.targetUnreachable) {
        float groundTargetY = rightFoot.currentGroundHeight + rightFoot.ankleHeightAboveGround;
        rightDrop = groundTargetY - rightFoot.animationFootPosition.y;
    }

    // Return the most negative drop (lowest foot needs pelvis to drop most)
    // If both are positive (ground higher than animation), return the smaller positive
    // This ensures both feet can reach their targets
    return glm::min(leftDrop, rightDrop);
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
    // Use configurable foot up vector (defaults to Y-up for compatibility)
    glm::vec3 footUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 targetUp = glm::normalize(groundNormal);

    float dot = glm::dot(footUp, targetUp);
    if (dot > 0.9999f) {
        return glm::quat(1, 0, 0, 0);
    }

    glm::vec3 axis = glm::cross(footUp, targetUp);
    if (glm::length2(axis) < 0.0001f) {
        return glm::quat(1, 0, 0, 0);
    }

    float angle = std::acos(glm::clamp(dot, -1.0f, 1.0f));
    angle = glm::clamp(angle, -maxAngle, maxAngle);

    return glm::angleAxis(angle, glm::normalize(axis));
}

// Helper to compute ankle height from skeleton bind pose
float FootPlacementIKSolver::computeAnkleHeight(
    const Skeleton& skeleton,
    int32_t footBoneIndex,
    int32_t toeBoneIndex,
    const std::vector<glm::mat4>& bindPoseGlobalTransforms
) {
    if (footBoneIndex < 0 || footBoneIndex >= static_cast<int32_t>(bindPoseGlobalTransforms.size())) {
        return 0.08f;  // Default fallback
    }

    glm::vec3 footPos = IKUtils::getWorldPosition(bindPoseGlobalTransforms[footBoneIndex]);

    if (toeBoneIndex >= 0 && toeBoneIndex < static_cast<int32_t>(bindPoseGlobalTransforms.size())) {
        // If toe bone exists, use its Y position as ground reference
        glm::vec3 toePos = IKUtils::getWorldPosition(bindPoseGlobalTransforms[toeBoneIndex]);
        // Ankle height is foot Y minus toe Y (assuming toe is near ground in bind pose)
        return footPos.y - toePos.y;
    }

    // No toe bone - estimate based on foot position
    // In most bind poses, the foot is slightly above ground
    // Use the foot's Y position as an approximation (assumes bind pose is standing)
    return footPos.y > 0.0f ? footPos.y : 0.08f;
}

// Helper to detect foot orientation from skeleton bind pose
void FootPlacementIKSolver::detectFootOrientation(
    const Skeleton& skeleton,
    int32_t footBoneIndex,
    int32_t toeBoneIndex,
    const std::vector<glm::mat4>& bindPoseGlobalTransforms,
    glm::vec3& outUpVector,
    glm::vec3& outForwardVector
) {
    // Default orientation (Y-up, Z-forward)
    outUpVector = glm::vec3(0.0f, 1.0f, 0.0f);
    outForwardVector = glm::vec3(0.0f, 0.0f, 1.0f);

    if (footBoneIndex < 0 || footBoneIndex >= static_cast<int32_t>(bindPoseGlobalTransforms.size())) {
        return;
    }

    glm::mat4 footGlobal = bindPoseGlobalTransforms[footBoneIndex];

    if (toeBoneIndex >= 0 && toeBoneIndex < static_cast<int32_t>(bindPoseGlobalTransforms.size())) {
        // Use foot-to-toe direction as forward
        glm::vec3 footPos = IKUtils::getWorldPosition(footGlobal);
        glm::vec3 toePos = IKUtils::getWorldPosition(bindPoseGlobalTransforms[toeBoneIndex]);
        glm::vec3 footToToe = toePos - footPos;

        if (glm::length2(footToToe) > 0.0001f) {
            outForwardVector = glm::normalize(footToToe);
            // Up is perpendicular to forward and the world up
            glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::cross(outForwardVector, worldUp);
            if (glm::length2(right) > 0.0001f) {
                right = glm::normalize(right);
                outUpVector = glm::normalize(glm::cross(right, outForwardVector));
            }
        }
    } else {
        // No toe bone - extract orientation from foot bone's rotation
        glm::mat3 footRot = glm::mat3(footGlobal);
        // Assume foot's local +Y is up and +Z is forward in most rigs
        outUpVector = glm::normalize(footRot * glm::vec3(0.0f, 1.0f, 0.0f));
        outForwardVector = glm::normalize(footRot * glm::vec3(0.0f, 0.0f, 1.0f));
    }
}

// Helper to compute total leg length
float FootPlacementIKSolver::computeLegLength(
    const std::vector<glm::mat4>& bindPoseGlobalTransforms,
    int32_t hipBoneIndex,
    int32_t kneeBoneIndex,
    int32_t footBoneIndex
) {
    if (hipBoneIndex < 0 || kneeBoneIndex < 0 || footBoneIndex < 0) {
        return 0.0f;
    }

    glm::vec3 hipPos = IKUtils::getWorldPosition(bindPoseGlobalTransforms[hipBoneIndex]);
    glm::vec3 kneePos = IKUtils::getWorldPosition(bindPoseGlobalTransforms[kneeBoneIndex]);
    glm::vec3 footPos = IKUtils::getWorldPosition(bindPoseGlobalTransforms[footBoneIndex]);

    float upperLeg = glm::length(kneePos - hipPos);
    float lowerLeg = glm::length(footPos - kneePos);

    return upperLeg + lowerLeg;
}
