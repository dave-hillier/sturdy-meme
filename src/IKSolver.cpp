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

void IKSystem::solve(Skeleton& skeleton) {
    if (chains.empty()) return;

    // Compute global transforms once
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // Solve each enabled chain
    for (auto& nc : chains) {
        if (nc.chain.enabled && nc.chain.weight > 0.0f) {
            TwoBoneIKSolver::solveBlended(skeleton, nc.chain, cachedGlobalTransforms, nc.chain.weight);

            // Recompute global transforms after each chain solve
            // (subsequent chains may depend on updated transforms)
            skeleton.computeGlobalTransforms(cachedGlobalTransforms);
        }
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

    return data;
}

void IKSystem::clear() {
    chains.clear();
    cachedGlobalTransforms.clear();
}

bool IKSystem::hasEnabledChains() const {
    for (const auto& nc : chains) {
        if (nc.chain.enabled) return true;
    }
    return false;
}
