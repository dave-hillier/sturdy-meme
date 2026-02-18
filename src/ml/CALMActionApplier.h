#pragma once

#include "CALMCharacterConfig.h"
#include "Tensor.h"
#include "AnimationBlend.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct Skeleton;

namespace ml {

// Converts CALM action outputs (target joint angles per DOF) into skeleton poses.
//
// CALM's policy outputs a vector of target joint angles. This class maps those
// back to the engine's Skeleton joint transforms, producing a SkeletonPose
// that can be blended with clip-based animation via AnimationBlend.
//
// Two modes:
//   Kinematic — directly set joint rotations from action targets (default)
//   Physics   — convert actions to a target SkeletonPose for ragdoll motor driving
class CALMActionApplier {
public:
    CALMActionApplier() = default;
    explicit CALMActionApplier(const CALMCharacterConfig& config);

    // Apply CALM actions to a skeleton pose (kinematic mode).
    // actions: flat tensor of size actionDim (target angles per DOF)
    // outPose: receives the resulting skeleton pose
    // The pose is built from the skeleton's current state with CALM-controlled
    // joints overridden by the action targets.
    void applyToSkeleton(const Tensor& actions,
                         const Skeleton& skeleton,
                         SkeletonPose& outPose) const;

    // Apply CALM actions blended with an existing pose.
    // blendWeight: 0 = keep basePose, 1 = full CALM override
    void applyBlended(const Tensor& actions,
                      const Skeleton& skeleton,
                      const SkeletonPose& basePose,
                      float blendWeight,
                      SkeletonPose& outPose) const;

    // Convert CALM actions to a target SkeletonPose without applying to skeleton.
    // Used for ragdoll motor driving — the returned pose is fed to
    // RagdollInstance::driveToTargetPose().
    void actionsToTargetPose(const Tensor& actions,
                             const Skeleton& skeleton,
                             SkeletonPose& outPose) const;

    // Clamp action values to joint limits.
    void clampActions(Tensor& actions) const;

    // Get config
    const CALMCharacterConfig& config() const { return config_; }

private:
    CALMCharacterConfig config_;

    // Build a rotation quaternion from Euler angles for a single joint,
    // applying only the axes controlled by CALM DOFs.
    glm::quat buildJointRotation(int32_t jointIndex,
                                  const Tensor& actions) const;

    // Cache: for each joint, the range of DOF indices [first, last) that affect it.
    // Built once from config_.dofMappings.
    struct JointDOFRange {
        int32_t jointIndex;
        int firstDOF;    // Index into dofMappings
        int numDOFs;     // How many DOFs this joint has (1-3)
    };
    std::vector<JointDOFRange> jointDOFRanges_;

    void buildJointDOFRanges();
};

} // namespace ml
