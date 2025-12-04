#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <optional>

#include "GLTFLoader.h"

// Joint rotation limits (in radians)
struct JointLimits {
    glm::vec3 minAngles = glm::vec3(-glm::pi<float>());
    glm::vec3 maxAngles = glm::vec3(glm::pi<float>());
    bool enabled = false;
};

// Two-bone IK chain definition (for arms/legs)
struct TwoBoneIKChain {
    int32_t rootBoneIndex = -1;     // Upper arm / thigh
    int32_t midBoneIndex = -1;      // Forearm / shin
    int32_t endBoneIndex = -1;      // Hand / foot

    glm::vec3 targetPosition = glm::vec3(0.0f);
    glm::vec3 poleVector = glm::vec3(0.0f, 0.0f, 1.0f);  // Controls elbow/knee direction
    float weight = 1.0f;            // Blend weight (0 = animation only, 1 = full IK)
    bool enabled = false;

    // Joint limits for mid bone (elbow/knee)
    JointLimits midBoneLimits;
};

// Debug visualization data for IK chains
struct IKDebugData {
    struct Chain {
        glm::vec3 rootPos;
        glm::vec3 midPos;
        glm::vec3 endPos;
        glm::vec3 targetPos;
        glm::vec3 polePos;
        bool active;
    };
    std::vector<Chain> chains;
};

// Two-Bone IK Solver
// Uses analytical solution for exactly 2 bones (arm or leg)
// Algorithm: Law of cosines to find joint angles, pole vector for bend direction
class TwoBoneIKSolver {
public:
    // Solve IK for a two-bone chain
    // Modifies skeleton joint transforms in place
    // Returns true if target was reachable
    static bool solve(
        Skeleton& skeleton,
        const TwoBoneIKChain& chain,
        const std::vector<glm::mat4>& globalTransforms
    );

    // Solve with blend weight (interpolates between animation and IK result)
    static bool solveBlended(
        Skeleton& skeleton,
        const TwoBoneIKChain& chain,
        const std::vector<glm::mat4>& globalTransforms,
        float weight
    );

private:
    // Apply joint limits to a rotation
    static glm::quat applyJointLimits(const glm::quat& rotation, const JointLimits& limits);

    // Calculate angle between two vectors
    static float angleBetween(const glm::vec3& a, const glm::vec3& b);
};

// IK System - manages multiple IK chains for a character
class IKSystem {
public:
    IKSystem() = default;

    // Setup chains by bone names
    bool addTwoBoneChain(
        const std::string& name,
        const Skeleton& skeleton,
        const std::string& rootBoneName,
        const std::string& midBoneName,
        const std::string& endBoneName
    );

    // Get chain by name for modification
    TwoBoneIKChain* getChain(const std::string& name);
    const TwoBoneIKChain* getChain(const std::string& name) const;

    // Set target for a chain
    void setTarget(const std::string& chainName, const glm::vec3& target);
    void setPoleVector(const std::string& chainName, const glm::vec3& pole);
    void setWeight(const std::string& chainName, float weight);
    void setEnabled(const std::string& chainName, bool enabled);

    // Solve all enabled IK chains
    // Call this after animation sampling, before computing bone matrices
    void solve(Skeleton& skeleton);

    // Get debug visualization data
    IKDebugData getDebugData(const Skeleton& skeleton) const;

    // Clear all chains
    void clear();

    // Check if any chains are enabled
    bool hasEnabledChains() const;

private:
    struct NamedChain {
        std::string name;
        TwoBoneIKChain chain;
    };
    std::vector<NamedChain> chains;

    // Cached global transforms (computed once per solve)
    mutable std::vector<glm::mat4> cachedGlobalTransforms;
};

// Helper functions for extracting transform components
namespace IKUtils {
    // Decompose a matrix into translation, rotation, scale
    void decomposeTransform(
        const glm::mat4& transform,
        glm::vec3& translation,
        glm::quat& rotation,
        glm::vec3& scale
    );

    // Compose a matrix from components
    glm::mat4 composeTransform(
        const glm::vec3& translation,
        const glm::quat& rotation,
        const glm::vec3& scale
    );

    // Get world position from global transform
    glm::vec3 getWorldPosition(const glm::mat4& globalTransform);

    // Calculate bone length from global transforms
    float getBoneLength(
        const std::vector<glm::mat4>& globalTransforms,
        int32_t boneIndex,
        int32_t childBoneIndex
    );

    // Rotate a bone to point toward a target position
    glm::quat aimAt(
        const glm::vec3& currentDir,
        const glm::vec3& targetDir,
        const glm::vec3& upHint
    );
}
