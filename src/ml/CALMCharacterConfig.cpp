#include "CALMCharacterConfig.h"
#include "GLTFLoader.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

namespace ml {

// Standard humanoid bone names used by CALM / AMP.
// Listed in the order CALM enumerates DOFs.
// Each entry: {canonical name, candidate engine names to search for}
struct BoneDef {
    const char* canonicalName;
    std::vector<std::string> candidateNames;
    int numDOFs;                // Rotation DOFs this joint contributes
    bool isKeyBody;             // Whether this joint is tracked as a key body position
};

static const std::vector<BoneDef>& getHumanoidBoneDefs() {
    static const std::vector<BoneDef> defs = {
        // Spine chain
        {"pelvis",      {"Hips", "pelvis", "hip", "Pelvis"},                            3, false},
        {"abdomen",     {"Spine", "spine", "Spine1", "abdomen"},                        3, false},
        {"chest",       {"Spine1", "Spine2", "chest", "Chest"},                         3, false},
        {"neck",        {"Neck", "neck"},                                                3, false},
        {"head",        {"Head", "head"},                                                3, true},

        // Right arm
        {"right_upper_arm", {"RightArm", "RightUpperArm", "right_upper_arm", "R_Arm"},  3, false},
        {"right_lower_arm", {"RightForeArm", "RightLowerArm", "right_lower_arm"},       1, false},
        {"right_hand",      {"RightHand", "right_hand", "R_Hand"},                      0, true},

        // Left arm
        {"left_upper_arm",  {"LeftArm", "LeftUpperArm", "left_upper_arm", "L_Arm"},     3, false},
        {"left_lower_arm",  {"LeftForeArm", "LeftLowerArm", "left_lower_arm"},          1, false},
        {"left_hand",       {"LeftHand", "left_hand", "L_Hand"},                        0, true},

        // Right leg
        {"right_thigh",     {"RightUpLeg", "RightThigh", "right_thigh", "R_UpLeg"},    3, false},
        {"right_shin",      {"RightLeg", "RightShin", "right_shin", "R_Leg"},          1, false},
        {"right_foot",      {"RightFoot", "right_foot", "R_Foot"},                     3, true},

        // Left leg
        {"left_thigh",      {"LeftUpLeg", "LeftThigh", "left_thigh", "L_UpLeg"},       3, false},
        {"left_shin",       {"LeftLeg", "LeftShin", "left_shin", "L_Leg"},             1, false},
        {"left_foot",       {"LeftFoot", "left_foot", "L_Foot"},                       3, true},
    };
    return defs;
}

static int32_t findJointByName(const Skeleton& skeleton,
                                const std::vector<std::string>& candidates) {
    for (const auto& name : candidates) {
        int32_t idx = skeleton.findJointIndex(name);
        if (idx >= 0) return idx;
    }
    return -1;
}

// CALM observation per timestep:
//   root_h (1) + root_rot (6) + root_vel (3) + root_ang_vel (3)
//   + dof_pos (N) + dof_vel (N) + key_body_pos (K*3)
static int computeObservationDim(int numDOFs, int numKeyBodies) {
    return 1 + 6 + 3 + 3 + numDOFs + numDOFs + numKeyBodies * 3;
}

CALMCharacterConfig CALMCharacterConfig::buildFromSkeleton(const Skeleton& skeleton) {
    CALMCharacterConfig config;

    const auto& defs = getHumanoidBoneDefs();

    int dofIndex = 0;
    for (const auto& def : defs) {
        int32_t jointIdx = findJointByName(skeleton, def.candidateNames);
        if (jointIdx < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CALMCharacterConfig: bone '%s' not found in skeleton, skipping",
                        def.canonicalName);
            continue;
        }

        // Add DOF mappings for this joint
        for (int axis = 0; axis < def.numDOFs; ++axis) {
            CALMCharacterConfig::DOFMapping mapping;
            mapping.jointIndex = jointIdx;
            mapping.axis = axis;
            config.dofMappings.push_back(mapping);
            ++dofIndex;
        }

        // Add as key body if flagged
        if (def.isKeyBody) {
            config.keyBodies.push_back({jointIdx, def.canonicalName});
        }

        // Track root
        if (std::string(def.canonicalName) == "pelvis") {
            config.rootJointIndex = jointIdx;
        }
    }

    config.actionDim = dofIndex;
    config.observationDim = computeObservationDim(
        dofIndex, static_cast<int>(config.keyBodies.size()));

    SDL_Log("CALMCharacterConfig: built config with %d DOFs, %zu key bodies, obs_dim=%d",
            config.actionDim, config.keyBodies.size(), config.observationDim);

    return config;
}

CALMCharacterConfig CALMCharacterConfig::buildFromNameMap(
    const Skeleton& skeleton,
    const std::unordered_map<std::string, std::string>& nameMap) {
    CALMCharacterConfig config;

    const auto& defs = getHumanoidBoneDefs();

    int dofIndex = 0;
    for (const auto& def : defs) {
        auto it = nameMap.find(def.canonicalName);
        if (it == nameMap.end()) continue;

        int32_t jointIdx = skeleton.findJointIndex(it->second);
        if (jointIdx < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CALMCharacterConfig: mapped bone '%s' -> '%s' not found in skeleton",
                        def.canonicalName, it->second.c_str());
            continue;
        }

        for (int axis = 0; axis < def.numDOFs; ++axis) {
            CALMCharacterConfig::DOFMapping mapping;
            mapping.jointIndex = jointIdx;
            mapping.axis = axis;
            config.dofMappings.push_back(mapping);
            ++dofIndex;
        }

        if (def.isKeyBody) {
            config.keyBodies.push_back({jointIdx, def.canonicalName});
        }

        if (std::string(def.canonicalName) == "pelvis") {
            config.rootJointIndex = jointIdx;
        }
    }

    config.actionDim = dofIndex;
    config.observationDim = computeObservationDim(
        dofIndex, static_cast<int>(config.keyBodies.size()));

    return config;
}

} // namespace ml
