#include "IKSolver.h"
#include <SDL3/SDL_log.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
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

// TwoBoneIKSolver implementation

float TwoBoneIKSolver::angleBetween(const glm::vec3& a, const glm::vec3& b) {
    float dot = glm::dot(glm::normalize(a), glm::normalize(b));
    return std::acos(glm::clamp(dot, -1.0f, 1.0f));
}

glm::quat TwoBoneIKSolver::applyJointLimits(const glm::quat& rotation, const JointLimits& limits) {
    if (!limits.enabled) {
        return rotation;
    }

    // Convert to euler angles
    glm::vec3 euler = glm::eulerAngles(rotation);

    // Clamp each axis
    euler.x = glm::clamp(euler.x, limits.minAngles.x, limits.maxAngles.x);
    euler.y = glm::clamp(euler.y, limits.minAngles.y, limits.maxAngles.y);
    euler.z = glm::clamp(euler.z, limits.minAngles.z, limits.maxAngles.z);

    return glm::quat(euler);
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
    glm::vec3 rootTranslation, midTranslation, rootScale, midScale;
    glm::quat rootLocalRot, midLocalRot;
    IKUtils::decomposeTransform(rootJoint.localTransform, rootTranslation, rootLocalRot, rootScale);
    IKUtils::decomposeTransform(midJoint.localTransform, midTranslation, midLocalRot, midScale);

    // Calculate the plane normal using pole vector
    // The pole vector defines which way the elbow/knee should point
    glm::vec3 poleDir = glm::normalize(chain.poleVector - rootPos);
    glm::vec3 planeNormal = glm::normalize(glm::cross(targetDir, poleDir));
    if (glm::length2(planeNormal) < 0.0001f) {
        // Pole vector is aligned with target direction, use a default
        planeNormal = glm::vec3(0, 1, 0);
        if (std::abs(glm::dot(targetDir, planeNormal)) > 0.99f) {
            planeNormal = glm::vec3(1, 0, 0);
        }
        planeNormal = glm::normalize(glm::cross(targetDir, planeNormal));
    }

    // Calculate the bend direction (perpendicular to target direction, in the pole plane)
    glm::vec3 bendDir = glm::normalize(glm::cross(planeNormal, targetDir));

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
    glm::mat4 parentGlobal = glm::mat4(1.0f);
    if (rootJoint.parentIndex >= 0) {
        parentGlobal = globalTransforms[rootJoint.parentIndex];
    }
    glm::quat parentWorldRot = glm::quat_cast(glm::mat3(parentGlobal));
    glm::quat parentWorldRotInv = glm::inverse(parentWorldRot);

    // Calculate world rotation delta and convert to local
    glm::quat rootRotDelta = IKUtils::aimAt(currentRootDir, newRootDir, chain.poleVector);
    glm::quat currentWorldRot = glm::quat_cast(glm::mat3(globalTransforms[chain.rootBoneIndex]));
    glm::quat newWorldRot = rootRotDelta * currentWorldRot;
    glm::quat newRootLocalRot = parentWorldRotInv * newWorldRot;

    // Apply pre-rotation if present
    if (glm::length2(rootJoint.preRotation - glm::quat(1, 0, 0, 0)) > 0.0001f) {
        newRootLocalRot = glm::inverse(rootJoint.preRotation) * newRootLocalRot;
    }

    // Calculate rotations for mid bone
    glm::vec3 currentMidDir = glm::normalize(endPos - midPos);
    glm::vec3 newEndPos = targetPos;  // End should reach target
    glm::vec3 newMidDir = glm::normalize(newEndPos - newMidPos);

    // Mid bone's parent is root bone (after IK), so we need the new root global transform
    glm::mat4 newRootGlobal;
    if (rootJoint.parentIndex >= 0) {
        newRootGlobal = globalTransforms[rootJoint.parentIndex] *
                        IKUtils::composeTransform(rootTranslation, newRootLocalRot, rootScale);
    } else {
        newRootGlobal = IKUtils::composeTransform(rootTranslation, newRootLocalRot, rootScale);
    }
    // Apply pre-rotation to get actual global
    if (glm::length2(rootJoint.preRotation - glm::quat(1, 0, 0, 0)) > 0.0001f) {
        glm::mat4 preRotMat = glm::mat4_cast(rootJoint.preRotation);
        newRootGlobal = newRootGlobal * preRotMat;
    }

    glm::quat midParentWorldRot = glm::quat_cast(glm::mat3(newRootGlobal));
    glm::quat midParentWorldRotInv = glm::inverse(midParentWorldRot);

    glm::quat midRotDelta = IKUtils::aimAt(currentMidDir, newMidDir, chain.poleVector);
    glm::quat currentMidWorldRot = glm::quat_cast(glm::mat3(globalTransforms[chain.midBoneIndex]));
    glm::quat newMidWorldRot = midRotDelta * currentMidWorldRot;
    glm::quat newMidLocalRot = midParentWorldRotInv * newMidWorldRot;

    // Apply pre-rotation for mid joint
    if (glm::length2(midJoint.preRotation - glm::quat(1, 0, 0, 0)) > 0.0001f) {
        newMidLocalRot = glm::inverse(midJoint.preRotation) * newMidLocalRot;
    }

    // Apply joint limits to mid bone
    newMidLocalRot = applyJointLimits(newMidLocalRot, chain.midBoneLimits);

    // Update skeleton local transforms
    rootJoint.localTransform = IKUtils::composeTransform(rootTranslation, newRootLocalRot, rootScale);
    midJoint.localTransform = IKUtils::composeTransform(midTranslation, newMidLocalRot, midScale);

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

// IKSystem implementation

bool IKSystem::addTwoBoneChain(
    const std::string& name,
    const Skeleton& skeleton,
    const std::string& rootBoneName,
    const std::string& midBoneName,
    const std::string& endBoneName
) {
    int32_t rootIdx = skeleton.findJointIndex(rootBoneName);
    int32_t midIdx = skeleton.findJointIndex(midBoneName);
    int32_t endIdx = skeleton.findJointIndex(endBoneName);

    if (rootIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Root bone '%s' not found", rootBoneName.c_str());
        return false;
    }
    if (midIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Mid bone '%s' not found", midBoneName.c_str());
        return false;
    }
    if (endIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: End bone '%s' not found", endBoneName.c_str());
        return false;
    }

    NamedChain nc;
    nc.name = name;
    nc.chain.rootBoneIndex = rootIdx;
    nc.chain.midBoneIndex = midIdx;
    nc.chain.endBoneIndex = endIdx;
    nc.chain.enabled = false;

    chains.push_back(nc);

    SDL_Log("IKSystem: Added two-bone chain '%s' (root=%d, mid=%d, end=%d)",
            name.c_str(), rootIdx, midIdx, endIdx);

    return true;
}

TwoBoneIKChain* IKSystem::getChain(const std::string& name) {
    for (auto& nc : chains) {
        if (nc.name == name) {
            return &nc.chain;
        }
    }
    return nullptr;
}

const TwoBoneIKChain* IKSystem::getChain(const std::string& name) const {
    for (const auto& nc : chains) {
        if (nc.name == name) {
            return &nc.chain;
        }
    }
    return nullptr;
}

void IKSystem::setTarget(const std::string& chainName, const glm::vec3& target) {
    if (auto* chain = getChain(chainName)) {
        chain->targetPosition = target;
    }
}

void IKSystem::setPoleVector(const std::string& chainName, const glm::vec3& pole) {
    if (auto* chain = getChain(chainName)) {
        chain->poleVector = pole;
    }
}

void IKSystem::setWeight(const std::string& chainName, float weight) {
    if (auto* chain = getChain(chainName)) {
        chain->weight = glm::clamp(weight, 0.0f, 1.0f);
    }
}

void IKSystem::setEnabled(const std::string& chainName, bool enabled) {
    if (auto* chain = getChain(chainName)) {
        chain->enabled = enabled;
    }
}

IKDebugData IKSystem::getDebugData(const Skeleton& skeleton) const {
    IKDebugData data;

    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    for (const auto& nc : chains) {
        IKDebugData::Chain chainData;

        if (nc.chain.rootBoneIndex >= 0 && static_cast<size_t>(nc.chain.rootBoneIndex) < globalTransforms.size()) {
            chainData.rootPos = IKUtils::getWorldPosition(globalTransforms[nc.chain.rootBoneIndex]);
        }
        if (nc.chain.midBoneIndex >= 0 && static_cast<size_t>(nc.chain.midBoneIndex) < globalTransforms.size()) {
            chainData.midPos = IKUtils::getWorldPosition(globalTransforms[nc.chain.midBoneIndex]);
        }
        if (nc.chain.endBoneIndex >= 0 && static_cast<size_t>(nc.chain.endBoneIndex) < globalTransforms.size()) {
            chainData.endPos = IKUtils::getWorldPosition(globalTransforms[nc.chain.endBoneIndex]);
        }

        chainData.targetPos = nc.chain.targetPosition;
        chainData.polePos = nc.chain.poleVector;
        chainData.active = nc.chain.enabled;

        data.chains.push_back(chainData);
    }

    // Add look-at debug data
    if (lookAtEnabled && lookAt.headBoneIndex >= 0) {
        IKDebugData::LookAt lookAtData;
        if (static_cast<size_t>(lookAt.headBoneIndex) < globalTransforms.size()) {
            lookAtData.headPos = IKUtils::getWorldPosition(globalTransforms[lookAt.headBoneIndex]);
        }
        lookAtData.targetPos = lookAt.targetPosition;
        lookAtData.forward = glm::vec3(0, 0, 1);  // Default forward
        if (static_cast<size_t>(lookAt.headBoneIndex) < globalTransforms.size()) {
            lookAtData.forward = glm::normalize(glm::vec3(globalTransforms[lookAt.headBoneIndex][2]));
        }
        lookAtData.active = lookAt.enabled;
        data.lookAtTargets.push_back(lookAtData);
    }

    // Add foot placement debug data
    for (const auto& nfp : footPlacements) {
        IKDebugData::FootPlacement footData;
        if (nfp.foot.footBoneIndex >= 0 && static_cast<size_t>(nfp.foot.footBoneIndex) < globalTransforms.size()) {
            footData.footPos = IKUtils::getWorldPosition(globalTransforms[nfp.foot.footBoneIndex]);
        }
        footData.groundPos = glm::vec3(footData.footPos.x, nfp.foot.currentGroundHeight, footData.footPos.z);
        footData.normal = glm::vec3(0, 1, 0);  // Would need to store this in foot state
        footData.active = nfp.foot.enabled;
        data.footPlacements.push_back(footData);
    }

    return data;
}

void IKSystem::clear() {
    chains.clear();
    footPlacements.clear();
    lookAt = LookAtIK();
    lookAtEnabled = false;
    pelvisAdjustment = PelvisAdjustment();
    groundQuery = nullptr;
    cachedGlobalTransforms.clear();
}

bool IKSystem::hasEnabledChains() const {
    for (const auto& nc : chains) {
        if (nc.chain.enabled) return true;
    }
    if (lookAt.enabled) return true;
    for (const auto& fp : footPlacements) {
        if (fp.foot.enabled) return true;
    }
    return false;
}

// ============================================================================
// Look-At IK Solver Implementation
// ============================================================================

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
        glm::mat4 parentGlobal = lookAt.spineBoneIndex >= 0 && spineJoint.parentIndex >= 0
            ? globalTransforms[spineJoint.parentIndex]
            : glm::mat4(1.0f);
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
        glm::mat4 parentGlobal = neckJoint.parentIndex >= 0
            ? globalTransforms[neckJoint.parentIndex]
            : glm::mat4(1.0f);
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
        glm::mat4 parentGlobal = headJoint.parentIndex >= 0
            ? globalTransforms[headJoint.parentIndex]
            : glm::mat4(1.0f);
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
    // Convert to euler angles
    glm::vec3 euler = glm::eulerAngles(rotation);

    // Clamp yaw (y-axis rotation) and pitch (x-axis rotation)
    euler.y = glm::clamp(euler.y, -maxYaw, maxYaw);
    euler.x = glm::clamp(euler.x, -maxPitch, maxPitch);

    return glm::quat(euler);
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

// ============================================================================
// Foot Placement IK Solver Implementation
// ============================================================================

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

    // Get current foot world position from animation
    glm::vec3 animFootPos = IKUtils::getWorldPosition(globalTransforms[foot.footBoneIndex]);

    // Transform to world space using character transform
    glm::vec3 worldFootPos = glm::vec3(characterTransform * glm::vec4(animFootPos, 1.0f));

    // Query ground height at foot position
    glm::vec3 rayOrigin = worldFootPos + glm::vec3(0.0f, foot.raycastHeight, 0.0f);
    GroundQueryResult groundResult = groundQuery(rayOrigin, foot.raycastHeight + foot.raycastDistance);

    if (!groundResult.hit) {
        foot.isGrounded = false;
        return;
    }

    foot.isGrounded = true;
    foot.currentGroundHeight = groundResult.position.y;

    // Calculate target foot position (on ground)
    glm::vec3 targetWorldPos = groundResult.position - foot.footOffset;

    // Transform target back to character local space
    glm::mat4 invCharTransform = glm::inverse(characterTransform);
    glm::vec3 targetLocalPos = glm::vec3(invCharTransform * glm::vec4(targetWorldPos, 1.0f));

    // Smooth the target position
    if (deltaTime > 0.0f) {
        float t = glm::clamp(10.0f * deltaTime, 0.0f, 1.0f);
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
        // Transform ground normal to character local space
        glm::vec3 localNormal = glm::normalize(
            glm::vec3(glm::inverse(glm::mat3(characterTransform)) * groundResult.normal)
        );

        glm::quat footAlign = alignFootToGround(localNormal, foot.currentFootRotation, foot.maxFootAngle);

        // Apply foot rotation
        Joint& footJoint = skeleton.joints[foot.footBoneIndex];
        glm::vec3 t, s;
        glm::quat r;
        IKUtils::decomposeTransform(footJoint.localTransform, t, r, s);

        // Blend alignment
        glm::quat targetRot = footAlign * r;
        if (deltaTime > 0.0f) {
            float blendT = glm::clamp(8.0f * deltaTime, 0.0f, 1.0f);
            foot.currentFootRotation = glm::slerp(foot.currentFootRotation, footAlign, blendT);
        }

        glm::quat finalRot = glm::slerp(r, foot.currentFootRotation * r, foot.weight);
        footJoint.localTransform = IKUtils::composeTransform(t, finalRot, s);
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

// ============================================================================
// IKSystem - Look-At Methods
// ============================================================================

bool IKSystem::setupLookAt(
    const Skeleton& skeleton,
    const std::string& headBoneName,
    const std::string& neckBoneName,
    const std::string& spineBoneName
) {
    int32_t headIdx = skeleton.findJointIndex(headBoneName);
    if (headIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Head bone '%s' not found", headBoneName.c_str());
        return false;
    }

    lookAt.headBoneIndex = headIdx;
    lookAt.neckBoneIndex = neckBoneName.empty() ? -1 : skeleton.findJointIndex(neckBoneName);
    lookAt.spineBoneIndex = spineBoneName.empty() ? -1 : skeleton.findJointIndex(spineBoneName);
    lookAtEnabled = true;

    SDL_Log("IKSystem: Setup look-at (head=%d, neck=%d, spine=%d)",
            lookAt.headBoneIndex, lookAt.neckBoneIndex, lookAt.spineBoneIndex);

    return true;
}

void IKSystem::setLookAtTarget(const glm::vec3& target) {
    lookAt.targetPosition = target;
}

void IKSystem::setLookAtWeight(float weight) {
    lookAt.weight = glm::clamp(weight, 0.0f, 1.0f);
}

void IKSystem::setLookAtEnabled(bool enabled) {
    lookAt.enabled = enabled;
}

// ============================================================================
// IKSystem - Foot Placement Methods
// ============================================================================

bool IKSystem::addFootPlacement(
    const std::string& name,
    const Skeleton& skeleton,
    const std::string& hipBoneName,
    const std::string& kneeBoneName,
    const std::string& footBoneName,
    const std::string& toeBoneName
) {
    int32_t hipIdx = skeleton.findJointIndex(hipBoneName);
    int32_t kneeIdx = skeleton.findJointIndex(kneeBoneName);
    int32_t footIdx = skeleton.findJointIndex(footBoneName);
    int32_t toeIdx = toeBoneName.empty() ? -1 : skeleton.findJointIndex(toeBoneName);

    if (hipIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Hip bone '%s' not found", hipBoneName.c_str());
        return false;
    }
    if (kneeIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Knee bone '%s' not found", kneeBoneName.c_str());
        return false;
    }
    if (footIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Foot bone '%s' not found", footBoneName.c_str());
        return false;
    }

    NamedFootPlacement nfp;
    nfp.name = name;
    nfp.foot.hipBoneIndex = hipIdx;
    nfp.foot.kneeBoneIndex = kneeIdx;
    nfp.foot.footBoneIndex = footIdx;
    nfp.foot.toeBoneIndex = toeIdx;
    nfp.foot.enabled = false;

    footPlacements.push_back(nfp);

    SDL_Log("IKSystem: Added foot placement '%s' (hip=%d, knee=%d, foot=%d, toe=%d)",
            name.c_str(), hipIdx, kneeIdx, footIdx, toeIdx);

    return true;
}

bool IKSystem::setupPelvisAdjustment(
    const Skeleton& skeleton,
    const std::string& pelvisBoneName
) {
    int32_t pelvisIdx = skeleton.findJointIndex(pelvisBoneName);
    if (pelvisIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Pelvis bone '%s' not found", pelvisBoneName.c_str());
        return false;
    }

    pelvisAdjustment.pelvisBoneIndex = pelvisIdx;
    pelvisAdjustment.enabled = false;

    SDL_Log("IKSystem: Setup pelvis adjustment (bone=%d)", pelvisIdx);
    return true;
}

FootPlacementIK* IKSystem::getFootPlacement(const std::string& name) {
    for (auto& nfp : footPlacements) {
        if (nfp.name == name) {
            return &nfp.foot;
        }
    }
    return nullptr;
}

const FootPlacementIK* IKSystem::getFootPlacement(const std::string& name) const {
    for (const auto& nfp : footPlacements) {
        if (nfp.name == name) {
            return &nfp.foot;
        }
    }
    return nullptr;
}

void IKSystem::setFootPlacementEnabled(const std::string& name, bool enabled) {
    if (auto* foot = getFootPlacement(name)) {
        foot->enabled = enabled;
    }
}

void IKSystem::setFootPlacementWeight(const std::string& name, float weight) {
    if (auto* foot = getFootPlacement(name)) {
        foot->weight = glm::clamp(weight, 0.0f, 1.0f);
    }
}

// ============================================================================
// IKSystem - Updated Solve Methods
// ============================================================================

void IKSystem::solve(Skeleton& skeleton, float deltaTime) {
    solve(skeleton, glm::mat4(1.0f), deltaTime);
}

void IKSystem::solve(Skeleton& skeleton, const glm::mat4& characterTransform, float deltaTime) {
    if (!hasEnabledChains()) return;

    // Compute global transforms once
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // Solve pelvis adjustment first (affects leg IK)
    if (pelvisAdjustment.enabled && !footPlacements.empty()) {
        // Find left and right foot placements
        FootPlacementIK* leftFoot = nullptr;
        FootPlacementIK* rightFoot = nullptr;
        for (auto& nfp : footPlacements) {
            if (nfp.name.find("left") != std::string::npos ||
                nfp.name.find("Left") != std::string::npos ||
                nfp.name.find("L_") != std::string::npos) {
                leftFoot = &nfp.foot;
            } else {
                rightFoot = &nfp.foot;
            }
        }

        if (leftFoot && rightFoot) {
            float offset = FootPlacementIKSolver::calculatePelvisOffset(*leftFoot, *rightFoot, 0.0f);
            FootPlacementIKSolver::applyPelvisAdjustment(skeleton, pelvisAdjustment, offset, deltaTime);
            skeleton.computeGlobalTransforms(cachedGlobalTransforms);
        }
    }

    // Solve foot placement IK
    for (auto& nfp : footPlacements) {
        if (nfp.foot.enabled && nfp.foot.weight > 0.0f && groundQuery) {
            FootPlacementIKSolver::solve(skeleton, nfp.foot, cachedGlobalTransforms,
                                         groundQuery, characterTransform, deltaTime);
            skeleton.computeGlobalTransforms(cachedGlobalTransforms);
        }
    }

    // Solve two-bone IK chains
    for (auto& nc : chains) {
        if (nc.chain.enabled && nc.chain.weight > 0.0f) {
            TwoBoneIKSolver::solveBlended(skeleton, nc.chain, cachedGlobalTransforms, nc.chain.weight);
            skeleton.computeGlobalTransforms(cachedGlobalTransforms);
        }
    }

    // Solve look-at IK last (head movement shouldn't affect body)
    if (lookAt.enabled && lookAt.weight > 0.0f) {
        LookAtIKSolver::solve(skeleton, lookAt, cachedGlobalTransforms, deltaTime);
    }
}
