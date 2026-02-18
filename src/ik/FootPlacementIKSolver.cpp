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

void FootPlacementIKSolver::queryGround(
    FootPlacementIK& foot,
    const std::vector<glm::mat4>& globalTransforms,
    const GroundQueryFunc& groundQuery,
    const glm::mat4& characterTransform
) {
    if (!foot.enabled || foot.weight <= 0.0f) return;
    if (foot.footBoneIndex < 0 || foot.hipBoneIndex < 0 || foot.kneeBoneIndex < 0) return;
    if (!groundQuery) return;

    // Get current foot position in skeleton space (from animation)
    glm::vec3 animFootPos = IKUtils::getWorldPosition(globalTransforms[foot.footBoneIndex]);
    foot.animationFootPosition = animFootPos;

    // Transform to world space
    glm::vec3 worldFootPos = glm::vec3(characterTransform * glm::vec4(animFootPos, 1.0f));

    // Query ground at foot position
    glm::vec3 rayOrigin = worldFootPos + glm::vec3(0.0f, foot.raycastHeight, 0.0f);
    GroundQueryResult result = groundQuery(rayOrigin, foot.raycastHeight + foot.raycastDistance);

    foot.isGrounded = result.hit;
    if (result.hit) {
        foot.currentGroundHeight = result.position.y;
    }
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
        foot.lockOriginWorldPosition = glm::vec3(0.0f);
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
            // First time locking - store both the locked position and the origin for drift comparison
            foot.lockedWorldPosition = worldFootPos;
            foot.lockOriginWorldPosition = worldFootPos;
            foot.isLocked = true;
        }

        // Compare the animation's current foot position against where the foot was at lock time.
        // This measures how far the character has moved since locking, not how far the lock
        // has drifted. The lock should release when the character has walked far enough that
        // maintaining the lock would visually break the silhouette.
        float distanceFromLockOrigin = glm::length(glm::vec2(
            worldFootPos.x - foot.lockOriginWorldPosition.x,
            worldFootPos.z - foot.lockOriginWorldPosition.z));
        const float maxLockDistance = 0.15f;  // 15cm max deviation from lock origin

        if (distanceFromLockOrigin > maxLockDistance) {
            // Character has moved too far from where foot was planted - release lock
            foot.isLocked = false;
            foot.lockedWorldPosition = worldFootPos;
            foot.lockOriginWorldPosition = worldFootPos;
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

        // If over 90% extended, smoothly blend down IK weight to avoid pop at full extension
        if (foot.currentExtensionRatio > 0.9f) {
            foot.targetUnreachable = true;
            // Smooth ramp from 1.0 at 0.9 to 0.0 at 1.0 — guard and ramp aligned
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

        // Use locked ground normal during stance for stability, otherwise use current.
        // Prefer multi-point fitted plane normal when available.
        glm::vec3 groundNormal;
        if (foot.lockBlend > 0.5f && foot.isLocked) {
            groundNormal = foot.lockedGroundNormal;
        } else if (foot.useMultiPointGround && glm::length2(foot.groundPlaneNormal) > 0.5f) {
            groundNormal = foot.groundPlaneNormal;
        } else {
            groundNormal = groundResult.normal;
        }

        // Ground normal is in world space, transform to skeleton space
        // Use transpose(inverse(mat3)) for correct normal transformation
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(characterTransform)));
        glm::vec3 targetUp = glm::normalize(normalMatrix * groundNormal);

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

// Multi-point ground query: probe heel, ball, toe and fit a ground plane normal.
glm::vec3 FootPlacementIKSolver::queryMultiPointGround(
    const FootPlacementIK& foot,
    const std::vector<glm::mat4>& globalTransforms,
    const GroundQueryFunc& groundQuery,
    const glm::mat4& characterTransform
) {
    // Collect world-space probe positions from available bones
    auto probeAt = [&](int32_t boneIndex) -> std::pair<glm::vec3, bool> {
        if (boneIndex < 0 || boneIndex >= static_cast<int32_t>(globalTransforms.size()))
            return {{}, false};
        glm::vec3 localPos = IKUtils::getWorldPosition(globalTransforms[boneIndex]);
        glm::vec3 worldPos = glm::vec3(characterTransform * glm::vec4(localPos, 1.0f));
        glm::vec3 rayOrigin = worldPos + glm::vec3(0.0f, foot.raycastHeight, 0.0f);
        GroundQueryResult gq = groundQuery(rayOrigin, foot.raycastHeight + foot.raycastDistance);
        if (gq.hit) return {gq.position, true};
        return {{}, false};
    };

    // Probe heel, ball, toe when bones are available
    auto [heelHit, heelOk] = probeAt(foot.heelBoneIndex);
    auto [ballHit, ballOk] = probeAt(foot.ballBoneIndex);
    auto [toeHit, toeOk] = probeAt(foot.toeBoneIndex);

    // Collect valid contacts
    std::vector<glm::vec3> contacts;
    if (heelOk) contacts.push_back(heelHit);
    if (ballOk) contacts.push_back(ballHit);
    if (toeOk) contacts.push_back(toeHit);

    if (contacts.size() >= 3) {
        // Fit plane: normal = normalize(cross(ball-heel, toe-heel))
        glm::vec3 v1 = contacts[1] - contacts[0];
        glm::vec3 v2 = contacts[2] - contacts[0];
        glm::vec3 normal = glm::cross(v1, v2);
        if (glm::length2(normal) > 0.0001f) {
            normal = glm::normalize(normal);
            if (normal.y < 0.0f) normal = -normal;
            return normal;
        }
    } else if (contacts.size() == 2) {
        // Two contacts: derive normal from the edge and world up
        glm::vec3 edge = contacts[1] - contacts[0];
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::cross(edge, worldUp);
        if (glm::length2(right) > 0.0001f) {
            glm::vec3 normal = glm::normalize(glm::cross(right, edge));
            if (normal.y < 0.0f) normal = -normal;
            return normal;
        }
    }

    // Fallback: flat ground
    return glm::vec3(0.0f, 1.0f, 0.0f);
}

// Toe IK: bend toe bone to match ground angle under the toe.
void FootPlacementIKSolver::solveToeIK(
    Skeleton& skeleton,
    FootPlacementIK& foot,
    const std::vector<glm::mat4>& globalTransforms,
    const GroundQueryFunc& groundQuery,
    const glm::mat4& characterTransform,
    float deltaTime
) {
    if (foot.toeBoneIndex < 0 || foot.footBoneIndex < 0) return;
    if (!foot.enabled || foot.weight <= 0.0f) return;

    // Phase-aware blending
    float phaseBlend = 1.0f;
    if (foot.currentPhase == FootPhase::Swing) {
        phaseBlend = foot.phaseProgress > 0.8f ? (foot.phaseProgress - 0.8f) / 0.2f : 0.0f;
    }
    // PushOff: allow full toe bend (natural toe-off)

    if (phaseBlend <= 0.01f) {
        if (deltaTime > 0.0f) {
            float t = glm::clamp(8.0f * deltaTime, 0.0f, 1.0f);
            foot.currentToeAngle = glm::mix(foot.currentToeAngle, 0.0f, t);
        }
        return;
    }

    // Get toe and foot world positions
    glm::vec3 toeLocalPos = IKUtils::getWorldPosition(globalTransforms[foot.toeBoneIndex]);
    glm::vec3 toeWorldPos = glm::vec3(characterTransform * glm::vec4(toeLocalPos, 1.0f));
    glm::vec3 footLocalPos = IKUtils::getWorldPosition(globalTransforms[foot.footBoneIndex]);
    glm::vec3 footWorldPos = glm::vec3(characterTransform * glm::vec4(footLocalPos, 1.0f));

    // Query ground under toe
    glm::vec3 rayOrigin = toeWorldPos + glm::vec3(0.0f, foot.raycastHeight, 0.0f);
    GroundQueryResult toeGround = groundQuery(rayOrigin, foot.raycastHeight + foot.raycastDistance);
    if (!toeGround.hit) return;

    // Angle from foot-ground height to toe-ground height
    float horizontalDist = glm::length(glm::vec2(toeWorldPos.x - footWorldPos.x,
                                                   toeWorldPos.z - footWorldPos.z));
    if (horizontalDist < 0.01f) return;

    float heightDiff = toeGround.position.y - foot.currentGroundHeight;
    foot.targetToeAngle = std::atan2(heightDiff, horizontalDist);

    // Clamp to natural limits
    constexpr float MAX_DORSIFLEXION = glm::radians(45.0f);
    constexpr float MAX_PLANTARFLEXION = glm::radians(60.0f);
    foot.targetToeAngle = glm::clamp(foot.targetToeAngle, -MAX_DORSIFLEXION, MAX_PLANTARFLEXION);
    foot.targetToeAngle *= phaseBlend;

    // Smooth toward target
    if (deltaTime > 0.0f) {
        float t = glm::clamp(10.0f * deltaTime, 0.0f, 1.0f);
        foot.currentToeAngle = glm::mix(foot.currentToeAngle, foot.targetToeAngle, t);
    } else {
        foot.currentToeAngle = foot.targetToeAngle;
    }

    if (std::abs(foot.currentToeAngle) < 0.005f) return;

    // Apply pitch rotation to toe bone around local X axis
    Joint& toeJoint = skeleton.joints[foot.toeBoneIndex];
    glm::vec3 tt, ts;
    glm::quat tr;
    IKUtils::decomposeTransform(toeJoint.localTransform, tt, tr, ts);
    glm::quat toeBend = glm::angleAxis(foot.currentToeAngle, glm::vec3(1.0f, 0.0f, 0.0f));
    tr = tr * toeBend;
    toeJoint.localTransform = IKUtils::composeTransform(tt, tr, ts);
}

// Foot roll: apply sub-phase rotations to the foot bone.
void FootPlacementIKSolver::applyFootRoll(
    Skeleton& skeleton,
    FootPlacementIK& foot,
    const std::vector<glm::mat4>& globalTransforms,
    const glm::mat4& characterTransform
) {
    if (foot.footBoneIndex < 0 || !foot.enabled || foot.weight <= 0.0f) return;

    float rollAngle = 0.0f;
    if (foot.currentPhase == FootPhase::Contact) {
        // Heel strike → flat: foot starts angled (toe up) and rotates to flat
        float heelStrikeAngle = glm::radians(15.0f);
        rollAngle = heelStrikeAngle * (1.0f - foot.phaseProgress);
    } else if (foot.currentPhase == FootPhase::PushOff) {
        // Heel off → toe off: heel lifts, foot pivots forward
        float pushOffAngle = glm::radians(-25.0f);
        rollAngle = pushOffAngle * foot.phaseProgress;
    }

    if (std::abs(rollAngle) < 0.005f) return;

    Joint& footJoint = skeleton.joints[foot.footBoneIndex];
    glm::vec3 ft, fs;
    glm::quat fr;
    IKUtils::decomposeTransform(footJoint.localTransform, ft, fr, fs);
    glm::quat rollQuat = glm::angleAxis(rollAngle * foot.weight, glm::vec3(1.0f, 0.0f, 0.0f));
    fr = fr * rollQuat;
    footJoint.localTransform = IKUtils::composeTransform(ft, fr, fs);
}

// Slope compensation: shift pelvis forward/back and lean body into slope.
void FootPlacementIKSolver::applySlopeCompensation(
    Skeleton& skeleton,
    PelvisAdjustment& pelvis,
    const GroundQueryFunc& groundQuery,
    const glm::mat4& characterTransform,
    const glm::vec3& characterForward,
    float deltaTime
) {
    if (!pelvis.enabled || pelvis.pelvisBoneIndex < 0 || !groundQuery) return;

    // Sample ground normal at character center
    glm::vec3 charPos = glm::vec3(characterTransform[3]);
    glm::vec3 rayOrigin = charPos + glm::vec3(0.0f, 0.5f, 0.0f);
    GroundQueryResult result = groundQuery(rayOrigin, 2.0f);
    if (!result.hit) return;

    glm::vec3 groundNormal = glm::normalize(result.normal);

    // Compute slope angle along forward direction
    glm::vec3 fwd = glm::normalize(glm::vec3(characterForward.x, 0.0f, characterForward.z));
    float forwardSlope = -glm::dot(groundNormal, fwd);
    float slopeAngle = std::asin(glm::clamp(forwardSlope, -1.0f, 1.0f));

    // Forward/backward shift proportional to slope
    float targetShiftAmount = slopeAngle * (pelvis.maxSlopeShift / glm::radians(30.0f));
    targetShiftAmount = glm::clamp(targetShiftAmount, -pelvis.maxSlopeShift, pelvis.maxSlopeShift);
    glm::vec3 targetShift = fwd * targetShiftAmount;

    // Body lean proportional to slope
    float targetLean = glm::clamp(slopeAngle, -pelvis.maxSlopeLean, pelvis.maxSlopeLean);

    // Smooth
    if (deltaTime > 0.0f) {
        float t = glm::clamp(pelvis.smoothSpeed * deltaTime, 0.0f, 1.0f);
        pelvis.currentSlopeShift = glm::mix(pelvis.currentSlopeShift, targetShift, t);
        pelvis.slopeLeanAngle = glm::mix(pelvis.slopeLeanAngle, targetLean, t);
    } else {
        pelvis.currentSlopeShift = targetShift;
        pelvis.slopeLeanAngle = targetLean;
    }

    // Apply to pelvis
    if (glm::length2(pelvis.currentSlopeShift) > 0.0001f || std::abs(pelvis.slopeLeanAngle) > 0.001f) {
        Joint& pelvisJoint = skeleton.joints[pelvis.pelvisBoneIndex];
        glm::vec3 pt, ps;
        glm::quat pr;
        IKUtils::decomposeTransform(pelvisJoint.localTransform, pt, pr, ps);

        // Convert world-space shift to local space
        glm::mat3 invCharRot = glm::transpose(glm::mat3(characterTransform));
        glm::vec3 localShift = invCharRot * pelvis.currentSlopeShift;
        pt += localShift;

        // Apply lean as pitch around local X
        if (std::abs(pelvis.slopeLeanAngle) > 0.001f) {
            glm::quat lean = glm::angleAxis(pelvis.slopeLeanAngle, glm::vec3(1.0f, 0.0f, 0.0f));
            pr = lean * pr;
        }

        pelvisJoint.localTransform = IKUtils::composeTransform(pt, pr, ps);
    }
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
