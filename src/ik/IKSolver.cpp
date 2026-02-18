#include "IKSolver.h"
#include <SDL3/SDL_log.h>

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

// Look-At Methods

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

// Foot Placement Methods

bool IKSystem::addFootPlacement(
    const std::string& name,
    const Skeleton& skeleton,
    const std::string& hipBoneName,
    const std::string& kneeBoneName,
    const std::string& footBoneName,
    const std::string& toeBoneName,
    bool isLeftFoot
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

    // Compute bind pose global transforms for skeleton-derived parameters
    std::vector<glm::mat4> bindPoseGlobalTransforms;
    Skeleton& mutableSkeleton = const_cast<Skeleton&>(skeleton);
    mutableSkeleton.computeGlobalTransforms(bindPoseGlobalTransforms);

    NamedFootPlacement nfp;
    nfp.name = name;
    nfp.isLeftFoot = isLeftFoot;
    nfp.foot.hipBoneIndex = hipIdx;
    nfp.foot.kneeBoneIndex = kneeIdx;
    nfp.foot.footBoneIndex = footIdx;
    nfp.foot.toeBoneIndex = toeIdx;
    nfp.foot.enabled = true;  // Enabled by default

    // Compute ankle height from skeleton bind pose
    nfp.foot.ankleHeightAboveGround = FootPlacementIKSolver::computeAnkleHeight(
        skeleton, footIdx, toeIdx, bindPoseGlobalTransforms);

    // Detect foot orientation from skeleton bind pose
    FootPlacementIKSolver::detectFootOrientation(
        skeleton, footIdx, toeIdx, bindPoseGlobalTransforms,
        nfp.foot.footUpVector, nfp.foot.footForwardVector);

    // Compute leg length for reach checking
    nfp.foot.legLength = FootPlacementIKSolver::computeLegLength(
        bindPoseGlobalTransforms, hipIdx, kneeIdx, footIdx);

    footPlacements.push_back(nfp);

    SDL_Log("IKSystem: Added foot placement '%s' (hip=%d, knee=%d, foot=%d, toe=%d, ankleHeight=%.3f, legLength=%.3f)",
            name.c_str(), hipIdx, kneeIdx, footIdx, toeIdx,
            nfp.foot.ankleHeightAboveGround, nfp.foot.legLength);

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

void IKSystem::resetFootLocks() {
    for (auto& nfp : footPlacements) {
        nfp.foot.isLocked = false;
        nfp.foot.lockBlend = 0.0f;
        nfp.foot.lockedWorldPosition = glm::vec3(0.0f);
        nfp.foot.currentFootTarget = glm::vec3(0.0f);
    }
}

// Straddle Methods

bool IKSystem::setupStraddle(
    const Skeleton& skeleton,
    const std::string& pelvisBoneName,
    const std::string& spineBaseBoneName
) {
    int32_t pelvisIdx = skeleton.findJointIndex(pelvisBoneName);
    if (pelvisIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Pelvis bone '%s' not found for straddle", pelvisBoneName.c_str());
        return false;
    }

    straddle.pelvisBoneIndex = pelvisIdx;
    straddle.spineBaseBoneIndex = spineBaseBoneName.empty() ? -1 : skeleton.findJointIndex(spineBaseBoneName);
    straddleEnabled = true;

    SDL_Log("IKSystem: Setup straddle (pelvis=%d, spine=%d)",
            straddle.pelvisBoneIndex, straddle.spineBaseBoneIndex);
    return true;
}

void IKSystem::setStraddleEnabled(bool enabled) {
    straddle.enabled = enabled;
}

void IKSystem::setStraddleWeight(float weight) {
    straddle.weight = glm::clamp(weight, 0.0f, 1.0f);
}

// Climbing Methods

bool IKSystem::setupClimbing(
    const Skeleton& skeleton,
    const std::string& pelvisBoneName,
    const std::string& spineBaseBoneName,
    const std::string& spineMidBoneName,
    const std::string& chestBoneName
) {
    int32_t pelvisIdx = skeleton.findJointIndex(pelvisBoneName);
    if (pelvisIdx < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IKSystem: Pelvis bone '%s' not found for climbing", pelvisBoneName.c_str());
        return false;
    }

    climbing.pelvisBoneIndex = pelvisIdx;
    climbing.spineBaseBoneIndex = skeleton.findJointIndex(spineBaseBoneName);
    climbing.spineMidBoneIndex = spineMidBoneName.empty() ? -1 : skeleton.findJointIndex(spineMidBoneName);
    climbing.chestBoneIndex = chestBoneName.empty() ? -1 : skeleton.findJointIndex(chestBoneName);
    climbingEnabled = true;

    SDL_Log("IKSystem: Setup climbing (pelvis=%d, spineBase=%d, spineMid=%d, chest=%d)",
            climbing.pelvisBoneIndex, climbing.spineBaseBoneIndex,
            climbing.spineMidBoneIndex, climbing.chestBoneIndex);
    return true;
}

void IKSystem::setClimbingArmChains(const std::string& leftArm, const std::string& rightArm) {
    leftArmChainName = leftArm;
    rightArmChainName = rightArm;

    // Find chain indices and store root bone indices for reach calculations
    for (size_t i = 0; i < chains.size(); i++) {
        if (chains[i].name == leftArm) {
            climbing.leftArmChainIndex = static_cast<int32_t>(i);
            climbing.leftShoulderBoneIndex = chains[i].chain.rootBoneIndex;
        }
        if (chains[i].name == rightArm) {
            climbing.rightArmChainIndex = static_cast<int32_t>(i);
            climbing.rightShoulderBoneIndex = chains[i].chain.rootBoneIndex;
        }
    }
}

void IKSystem::setClimbingLegChains(const std::string& leftLeg, const std::string& rightLeg) {
    leftLegChainName = leftLeg;
    rightLegChainName = rightLeg;

    // Find chain indices and store root bone indices for reach calculations
    for (size_t i = 0; i < chains.size(); i++) {
        if (chains[i].name == leftLeg) {
            climbing.leftLegChainIndex = static_cast<int32_t>(i);
            climbing.leftHipBoneIndex = chains[i].chain.rootBoneIndex;
        }
        if (chains[i].name == rightLeg) {
            climbing.rightLegChainIndex = static_cast<int32_t>(i);
            climbing.rightHipBoneIndex = chains[i].chain.rootBoneIndex;
        }
    }
}

void IKSystem::setClimbingHandBones(
    const Skeleton& skeleton,
    const std::string& leftHandBoneName,
    const std::string& rightHandBoneName
) {
    climbing.leftHandBoneIndex = skeleton.findJointIndex(leftHandBoneName);
    climbing.rightHandBoneIndex = skeleton.findJointIndex(rightHandBoneName);
}

void IKSystem::setClimbingHandHold(bool isLeft, const glm::vec3& position, const glm::vec3& normal, const glm::vec3& gripDir) {
    ClimbingIKSolver::setHandHold(climbing, isLeft, position, normal, gripDir);
}

void IKSystem::setClimbingFootHold(bool isLeft, const glm::vec3& position, const glm::vec3& normal) {
    ClimbingIKSolver::setFootHold(climbing, isLeft, position, normal);
}

void IKSystem::clearClimbingHandHold(bool isLeft) {
    ClimbingIKSolver::clearHandHold(climbing, isLeft);
}

void IKSystem::clearClimbingFootHold(bool isLeft) {
    ClimbingIKSolver::clearFootHold(climbing, isLeft);
}

void IKSystem::setClimbingEnabled(bool enabled) {
    climbing.enabled = enabled;
}

void IKSystem::setClimbingWeight(float weight) {
    climbing.weight = glm::clamp(weight, 0.0f, 1.0f);
}

void IKSystem::setClimbingWallNormal(const glm::vec3& normal) {
    climbing.wallNormal = glm::normalize(normal);
}

// Solve Methods

void IKSystem::solve(Skeleton& skeleton, float deltaTime) {
    solve(skeleton, glm::mat4(1.0f), deltaTime);
}

void IKSystem::solve(Skeleton& skeleton, const glm::mat4& characterTransform, float deltaTime) {
    if (!hasEnabledChains()) return;

    // Helper to find left/right foot placements using explicit isLeftFoot flag
    auto findLeftRight = [this](FootPlacementIK*& leftFoot, FootPlacementIK*& rightFoot) {
        leftFoot = nullptr;
        rightFoot = nullptr;
        for (auto& nfp : footPlacements) {
            if (nfp.isLeftFoot) {
                leftFoot = &nfp.foot;
            } else {
                rightFoot = &nfp.foot;
            }
        }
    };

    // 1. Compute global transforms once
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // 2. Query ground for all feet without modifying skeleton (populates
    //    animationFootPosition, currentGroundHeight, isGrounded)
    if (groundQuery) {
        for (auto& nfp : footPlacements) {
            if (nfp.foot.enabled && nfp.foot.weight > 0.0f) {
                FootPlacementIKSolver::queryGround(nfp.foot, cachedGlobalTransforms,
                                                    groundQuery, characterTransform);
                // Multi-point ground query: fit a plane from heel/ball/toe contacts
                if (nfp.foot.useMultiPointGround && (nfp.foot.heelBoneIndex >= 0 ||
                    nfp.foot.ballBoneIndex >= 0 || nfp.foot.toeBoneIndex >= 0)) {
                    nfp.foot.groundPlaneNormal = FootPlacementIKSolver::queryMultiPointGround(
                        nfp.foot, cachedGlobalTransforms, groundQuery, characterTransform);
                }
            }
        }
    }

    // 3. Calculate and apply pelvis offset using current-frame foot data
    if (pelvisAdjustment.enabled && !footPlacements.empty()) {
        FootPlacementIK* leftFoot = nullptr;
        FootPlacementIK* rightFoot = nullptr;
        findLeftRight(leftFoot, rightFoot);

        if (leftFoot && rightFoot) {
            float offset = FootPlacementIKSolver::calculatePelvisOffset(*leftFoot, *rightFoot, 0.0f);
            FootPlacementIKSolver::applyPelvisAdjustment(skeleton, pelvisAdjustment, offset, deltaTime);
        }
    }

    // 3b. Slope compensation: shift pelvis forward/back and lean into slopes
    if (pelvisAdjustment.enabled && groundQuery) {
        glm::vec3 charForward = glm::vec3(characterTransform[2]); // Z column = forward
        FootPlacementIKSolver::applySlopeCompensation(
            skeleton, pelvisAdjustment, groundQuery, characterTransform, charForward, deltaTime);
    }

    // 4. Recompute globals after pelvis adjustment
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // 5. Solve foot placement IK (both feet)
    for (auto& nfp : footPlacements) {
        if (nfp.foot.enabled && nfp.foot.weight > 0.0f && groundQuery) {
            FootPlacementIKSolver::solve(skeleton, nfp.foot, cachedGlobalTransforms,
                                         groundQuery, characterTransform, deltaTime);
        }
    }

    // 5b. Recompute globals, then apply foot roll and toe IK
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);
    for (auto& nfp : footPlacements) {
        if (nfp.foot.enabled && nfp.foot.weight > 0.0f) {
            // Foot roll: heel strike → flat → heel off → toe off
            FootPlacementIKSolver::applyFootRoll(skeleton, nfp.foot,
                                                  cachedGlobalTransforms, characterTransform);
            // Toe IK: bend toes to match ground
            if (groundQuery) {
                FootPlacementIKSolver::solveToeIK(skeleton, nfp.foot, cachedGlobalTransforms,
                                                   groundQuery, characterTransform, deltaTime);
            }
        }
    }

    // 6. Recompute globals after foot IK + roll + toe
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // 7. Solve straddling IK (hip tilt for different foot heights)
    if (straddle.enabled && straddle.weight > 0.0f) {
        FootPlacementIK* leftFoot = nullptr;
        FootPlacementIK* rightFoot = nullptr;
        findLeftRight(leftFoot, rightFoot);
        StraddleIKSolver::solve(skeleton, straddle, leftFoot, rightFoot, cachedGlobalTransforms, deltaTime);
    }

    // 8. Solve two-bone IK chains (arms, etc.)
    bool chainsModified = false;
    for (auto& nc : chains) {
        if (nc.chain.enabled && nc.chain.weight > 0.0f) {
            TwoBoneIKSolver::solveBlended(skeleton, nc.chain, cachedGlobalTransforms, nc.chain.weight);
            chainsModified = true;
        }
    }

    // 9. Recompute if needed for climbing or look-at
    bool needsRecompute = chainsModified || (straddle.enabled && straddle.weight > 0.0f);
    bool hasClimbing = climbing.enabled && climbing.weight > 0.0f && climbing.currentTransition > 0.0f;
    bool hasLookAt = lookAt.enabled && lookAt.weight > 0.0f;

    if (needsRecompute && (hasClimbing || hasLookAt)) {
        skeleton.computeGlobalTransforms(cachedGlobalTransforms);
    }

    // 10. Solve climbing IK
    if (hasClimbing) {
        std::vector<TwoBoneIKChain> armChains;
        std::vector<TwoBoneIKChain> legChains;

        if (auto* leftArm = getChain(leftArmChainName)) armChains.push_back(*leftArm);
        if (auto* rightArm = getChain(rightArmChainName)) armChains.push_back(*rightArm);
        if (auto* leftLeg = getChain(leftLegChainName)) legChains.push_back(*leftLeg);
        if (auto* rightLeg = getChain(rightLegChainName)) legChains.push_back(*rightLeg);

        ClimbingIKSolver::solve(skeleton, climbing, armChains, legChains,
                                cachedGlobalTransforms, characterTransform, deltaTime);
        if (hasLookAt) {
            skeleton.computeGlobalTransforms(cachedGlobalTransforms);
        }
    }

    // 11. Solve look-at IK last (head movement shouldn't affect body)
    if (hasLookAt) {
        LookAtIKSolver::solve(skeleton, lookAt, cachedGlobalTransforms, deltaTime);
    }
}
