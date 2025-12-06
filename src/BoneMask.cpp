#include "BoneMask.h"
#include <algorithm>
#include <cctype>

// Bone name patterns for common skeleton naming conventions
// These cover Mixamo, Unity Humanoid, and common game engine conventions
namespace BoneMaskPatterns {

const std::vector<std::string> UPPER_BODY_ROOTS = {
    "spine", "spine1", "spine_01", "chest", "torso", "upperchest"
};

const std::vector<std::string> LEFT_ARM_ROOTS = {
    "leftshoulder", "left_shoulder", "l_shoulder", "shoulder_l", "shoulder.l",
    "leftarm", "left_arm", "l_arm", "arm_l", "arm.l",
    "leftupperarm", "left_upperarm", "l_upperarm"
};

const std::vector<std::string> RIGHT_ARM_ROOTS = {
    "rightshoulder", "right_shoulder", "r_shoulder", "shoulder_r", "shoulder.r",
    "rightarm", "right_arm", "r_arm", "arm_r", "arm.r",
    "rightupperarm", "right_upperarm", "r_upperarm"
};

const std::vector<std::string> HEAD_ROOTS = {
    "neck", "head", "neck_01"
};

const std::vector<std::string> SPINE_ROOTS = {
    "spine", "spine1", "spine2", "spine3", "spine_01", "spine_02", "spine_03",
    "chest", "upperchest", "torso"
};

const std::vector<std::string> LOWER_BODY_ROOTS = {
    "hips", "pelvis", "root"
};

const std::vector<std::string> LEFT_LEG_ROOTS = {
    "leftupleg", "left_upleg", "l_upleg", "upleg_l", "upleg.l",
    "leftthigh", "left_thigh", "l_thigh", "thigh_l", "thigh.l",
    "lefthip", "left_hip", "l_hip", "hip_l", "hip.l",
    "leftleg", "left_leg", "l_leg", "leg_l", "leg.l"
};

const std::vector<std::string> RIGHT_LEG_ROOTS = {
    "rightupleg", "right_upleg", "r_upleg", "upleg_r", "upleg.r",
    "rightthigh", "right_thigh", "r_thigh", "thigh_r", "thigh.r",
    "righthip", "right_hip", "r_hip", "hip_r", "hip.r",
    "rightleg", "right_leg", "r_leg", "leg_r", "leg.r"
};

}  // namespace BoneMaskPatterns

// Helper for case-insensitive string comparison
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

static bool containsPattern(const std::string& name, const std::string& pattern) {
    std::string lowerName = toLower(name);
    std::string lowerPattern = toLower(pattern);
    return lowerName.find(lowerPattern) != std::string::npos;
}

BoneMask::BoneMask(size_t boneCount, float defaultWeight)
    : weights(boneCount, defaultWeight) {
}

void BoneMask::resize(size_t count, float defaultWeight) {
    weights.resize(count, defaultWeight);
}

float BoneMask::getWeight(size_t boneIndex) const {
    if (boneIndex < weights.size()) {
        return weights[boneIndex];
    }
    return 0.0f;
}

void BoneMask::setWeight(size_t boneIndex, float weight) {
    if (boneIndex < weights.size()) {
        weights[boneIndex] = std::clamp(weight, 0.0f, 1.0f);
    }
}

void BoneMask::collectBonesByPattern(const Skeleton& skeleton,
                                     const std::vector<std::string>& patterns,
                                     std::unordered_set<int32_t>& outBoneIndices) {
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const std::string& boneName = skeleton.joints[i].name;
        for (const auto& pattern : patterns) {
            if (containsPattern(boneName, pattern)) {
                outBoneIndices.insert(static_cast<int32_t>(i));
                break;
            }
        }
    }
}

void BoneMask::addChildBones(const Skeleton& skeleton,
                             std::unordered_set<int32_t>& boneIndices) {
    // Keep adding children until no new bones are found
    bool foundNew = true;
    while (foundNew) {
        foundNew = false;
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            int32_t parentIdx = skeleton.joints[i].parentIndex;
            if (parentIdx >= 0 && boneIndices.count(parentIdx) > 0) {
                if (boneIndices.insert(static_cast<int32_t>(i)).second) {
                    foundNew = true;
                }
            }
        }
    }
}

void BoneMask::setWeightByName(const Skeleton& skeleton, const std::string& boneName,
                               float weight, bool includeChildren) {
    int32_t boneIndex = skeleton.findJointIndex(boneName);
    if (boneIndex < 0) {
        return;
    }

    setWeight(static_cast<size_t>(boneIndex), weight);

    if (includeChildren) {
        std::unordered_set<int32_t> boneSet;
        boneSet.insert(boneIndex);
        addChildBones(skeleton, boneSet);

        for (int32_t idx : boneSet) {
            setWeight(static_cast<size_t>(idx), weight);
        }
    }
}

BoneMask BoneMask::fromBoneNames(const Skeleton& skeleton,
                                  const std::vector<std::string>& boneNames,
                                  bool includeChildren) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> boneIndices;
    for (const auto& name : boneNames) {
        int32_t idx = skeleton.findJointIndex(name);
        if (idx >= 0) {
            boneIndices.insert(idx);
        }
    }

    if (includeChildren) {
        addChildBones(skeleton, boneIndices);
    }

    for (int32_t idx : boneIndices) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::upperBody(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> upperBodyBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::UPPER_BODY_ROOTS, upperBodyBones);
    collectBonesByPattern(skeleton, BoneMaskPatterns::HEAD_ROOTS, upperBodyBones);
    collectBonesByPattern(skeleton, BoneMaskPatterns::LEFT_ARM_ROOTS, upperBodyBones);
    collectBonesByPattern(skeleton, BoneMaskPatterns::RIGHT_ARM_ROOTS, upperBodyBones);

    addChildBones(skeleton, upperBodyBones);

    for (int32_t idx : upperBodyBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::lowerBody(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> lowerBodyBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::LOWER_BODY_ROOTS, lowerBodyBones);
    collectBonesByPattern(skeleton, BoneMaskPatterns::LEFT_LEG_ROOTS, lowerBodyBones);
    collectBonesByPattern(skeleton, BoneMaskPatterns::RIGHT_LEG_ROOTS, lowerBodyBones);

    addChildBones(skeleton, lowerBodyBones);

    // Remove upper body bones that might have been included via hips
    std::unordered_set<int32_t> upperBodyBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::UPPER_BODY_ROOTS, upperBodyBones);
    addChildBones(skeleton, upperBodyBones);

    for (int32_t idx : upperBodyBones) {
        lowerBodyBones.erase(idx);
    }

    for (int32_t idx : lowerBodyBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::leftArm(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> armBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::LEFT_ARM_ROOTS, armBones);
    addChildBones(skeleton, armBones);

    for (int32_t idx : armBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::rightArm(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> armBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::RIGHT_ARM_ROOTS, armBones);
    addChildBones(skeleton, armBones);

    for (int32_t idx : armBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::leftLeg(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> legBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::LEFT_LEG_ROOTS, legBones);
    addChildBones(skeleton, legBones);

    for (int32_t idx : legBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::rightLeg(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> legBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::RIGHT_LEG_ROOTS, legBones);
    addChildBones(skeleton, legBones);

    for (int32_t idx : legBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::spine(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> spineBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::SPINE_ROOTS, spineBones);
    // Don't include children for spine - just the spine bones themselves

    for (int32_t idx : spineBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::head(const Skeleton& skeleton) {
    BoneMask mask(skeleton.joints.size(), 0.0f);

    std::unordered_set<int32_t> headBones;
    collectBonesByPattern(skeleton, BoneMaskPatterns::HEAD_ROOTS, headBones);
    addChildBones(skeleton, headBones);

    for (int32_t idx : headBones) {
        mask.setWeight(static_cast<size_t>(idx), 1.0f);
    }

    return mask;
}

BoneMask BoneMask::inverted() const {
    BoneMask result(weights.size());
    for (size_t i = 0; i < weights.size(); ++i) {
        result.weights[i] = 1.0f - weights[i];
    }
    return result;
}

void BoneMask::scale(float factor) {
    for (float& w : weights) {
        w = std::clamp(w * factor, 0.0f, 1.0f);
    }
}

BoneMask BoneMask::operator*(const BoneMask& other) const {
    size_t count = std::min(weights.size(), other.weights.size());
    BoneMask result(count);
    for (size_t i = 0; i < count; ++i) {
        result.weights[i] = weights[i] * other.weights[i];
    }
    return result;
}

BoneMask BoneMask::operator+(const BoneMask& other) const {
    size_t count = std::min(weights.size(), other.weights.size());
    BoneMask result(count);
    for (size_t i = 0; i < count; ++i) {
        result.weights[i] = std::clamp(weights[i] + other.weights[i], 0.0f, 1.0f);
    }
    return result;
}
