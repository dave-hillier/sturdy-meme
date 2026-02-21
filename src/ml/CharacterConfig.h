#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

struct Skeleton;

namespace ml {

// Maps between a policy's DOF ordering and the engine's Skeleton joint indices.
// Observations/actions use a flat array of joint angles; this config defines
// which engine joints correspond to which DOF slots.
struct CharacterConfig {
    // Observation layout
    int observationDim = 0;            // Total per-frame observation size
    int numPolicyObsSteps = 2;         // Temporal stacking for policy
    int numEncoderObsSteps = 10;       // Temporal stacking for encoder

    // Action layout
    int actionDim = 0;                 // Number of controllable DOFs

    // Joint DOF mapping:
    // Each entry maps a DOF index to a skeleton joint.
    // A joint may contribute 1-3 DOFs depending on which axes are controllable.
    struct DOFMapping {
        int32_t jointIndex;            // Index into Skeleton::joints
        int axis;                      // 0=X, 1=Y, 2=Z rotation axis
        float rangeMin = -3.14159f;    // Joint limit (radians)
        float rangeMax = 3.14159f;
    };
    std::vector<DOFMapping> dofMappings;

    // Key body joints used for position features in the observation.
    // Tracks world-space positions of key bodies (hands, feet, head)
    // relative to the root, projected into heading frame.
    struct KeyBody {
        int32_t jointIndex;
        std::string name;              // For debugging
    };
    std::vector<KeyBody> keyBodies;

    // Root joint index in the skeleton
    int32_t rootJointIndex = 0;

    // PD controller gains for physics-based action application
    float pdKp = 40.0f;
    float pdKd = 5.0f;

    // Latent space
    int latentDim = 64;

    // Build a default config by scanning a skeleton for standard humanoid bones.
    // Searches for common bone names (Hips, Spine, LeftUpLeg, etc.)
    // and builds DOF mappings + key body list automatically.
    static CharacterConfig buildFromSkeleton(const Skeleton& skeleton);

    // Build from an explicit joint name map (for custom skeletons).
    // nameMap: maps canonical names -> engine joint names
    static CharacterConfig buildFromNameMap(
        const Skeleton& skeleton,
        const std::unordered_map<std::string, std::string>& nameMap);
};

} // namespace ml
