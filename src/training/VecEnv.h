#pragma once

#include "CharacterEnv.h"
#include "MotionFrame.h"
#include "MotionLibrary.h"
#include "RewardComputer.h"
#include "CharacterConfig.h"
#include "PhysicsSystem.h"

#include <vector>
#include <memory>
#include <cstdint>
#include <random>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>

struct Skeleton;

namespace training {

// Vectorised environment managing N parallel character training environments.
//
// All ragdolls live in a single shared PhysicsWorld. Characters are placed on
// a grid with sufficient spacing to avoid inter-character collisions.
// One call to step() applies actions to all characters, advances physics once,
// and extracts observations + rewards for all characters.
//
// Output buffers are contiguous float arrays suitable for zero-copy transfer
// to Python/NumPy via pybind11 (to be added in a later phase).
class VecEnv {
public:
    // Full constructor with explicit config and ragdoll settings.
    VecEnv(int numEnvs,
           const EnvConfig& config,
           const ml::CharacterConfig& charConfig,
           const Skeleton& skeleton,
           JPH::Ref<JPH::RagdollSettings> ragdollSettings);

    // Convenience constructor that builds CharacterConfig and RagdollSettings
    // from the skeleton automatically.
    VecEnv(int numEnvs,
           const EnvConfig& config,
           const Skeleton& skeleton);

    ~VecEnv();

    // Non-copyable, non-movable (owns PhysicsWorld + ragdolls)
    VecEnv(const VecEnv&) = delete;
    VecEnv& operator=(const VecEnv&) = delete;
    VecEnv(VecEnv&&) = delete;
    VecEnv& operator=(VecEnv&&) = delete;

    // Reset ALL environments to a default standing pose on the grid.
    void reset();

    // Reset environments whose episodes have ended, using provided motion frames.
    // frames must have at least as many entries as there are done environments.
    void resetDone(const std::vector<MotionFrame>& frames);

    // Reset a specific environment to the given motion frame.
    void resetEnv(int envIndex, const MotionFrame& frame);

    // Step all environments using the currently set task goal:
    //   1. Apply batched actions (contiguous float array: [numEnvs x actionDim])
    //   2. Step the shared physics world
    //   3. Extract observations from all ragdolls
    //   4. Compute rewards for all environments
    void step(const float* actions);

    // Step all environments with per-environment task goals.
    void step(const float* actions, const std::vector<TaskGoal>& goals);

    // Set a single task goal applied to all environments.
    void setTask(TaskType task, const glm::vec3& target);

    // --- Motion Library ---

    // Load FBX animations from a directory for episode resets.
    int loadMotions(const std::string& directory);

    // Load a single FBX animation file.
    int loadMotionFile(const std::string& path);

    // Reset done environments using random frames from the motion library.
    // Falls back to default standing pose if no motions are loaded.
    void resetDoneWithMotions();

    // Access the motion library directly.
    const MotionLibrary& motionLibrary() const { return motionLibrary_; }

    // --- Batched output buffers (contiguous, for zero-copy to Python) ---

    const float* observations() const { return observations_.data(); }
    const float* ampObservations() const { return ampObservations_.data(); }
    const float* rewards() const { return rewards_.data(); }
    const bool* dones() const { return reinterpret_cast<const bool*>(dones_.data()); }

    // --- Dimension queries ---

    int numEnvs() const { return numEnvs_; }
    int policyObsDim() const;
    int obsDim() const { return policyObsDim(); }
    int ampObsDim() const;
    int actionDim() const;

private:
    int numEnvs_;
    EnvConfig config_;

    // Shared physics world for all characters
    PhysicsWorld physicsWorld_;

    // Per-environment state
    std::vector<CharacterEnv> envs_;

    // Current task goal (applied to all envs when using the single-goal step)
    TaskGoal currentGoal_;

    // Motion library for episode resets
    MotionLibrary motionLibrary_;
    std::mt19937 rng_{42};
    std::unique_ptr<Skeleton> ownedSkeleton_;  // Owned copy for MotionLibrary sampling

    // Contiguous output buffers
    // dones_ uses uint8_t because std::vector<bool> is bit-packed and does
    // not provide a contiguous bool* suitable for zero-copy pybind11 access.
    std::vector<float> observations_;
    std::vector<float> ampObservations_;
    std::vector<float> rewards_;
    std::vector<uint8_t> dones_;

    // Copy per-env observations into the contiguous output buffers.
    void copyObsToBuffers();

    // Compute the world-space offset for the i-th environment on the grid.
    static glm::vec3 envGridPosition(int envIndex);
};

} // namespace training
