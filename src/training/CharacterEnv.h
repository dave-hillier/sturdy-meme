#pragma once

#include "MotionFrame.h"
#include "RewardComputer.h"
#include "ObservationExtractor.h"
#include "ActionApplier.h"
#include "CharacterConfig.h"
#include "Tensor.h"

#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>

struct Skeleton;

namespace JPH {
    class PhysicsSystem;
}

namespace physics {
    class RagdollInstance;
}

namespace training {

struct EnvConfig {
    float simTimestep = 1.0f / 60.0f;
    int simSubsteps = 2;
    float earlyTerminationHeight = 0.3f;
    int maxEpisodeSteps = 300;
};

struct StepResult {
    float taskReward = 0.0f;
    bool done = false;
    bool timeout = false;
};

// Single-character training environment.
//
// Manages one ragdoll in a shared physics world. Provides the reset/step
// interface expected by RL training loops:
//   - reset()          : snap ragdoll to a reference motion frame
//   - applyActions()   : convert policy outputs to motor targets
//   - extractObservations() : read physics state into observation tensors
//   - computeStepResult()   : evaluate task reward and termination
//
// The physics world is NOT owned by this class -- multiple CharacterEnvs
// share a single PhysicsWorld, and the caller is responsible for stepping it.
class CharacterEnv {
public:
    CharacterEnv(const EnvConfig& config,
                 const ml::CharacterConfig& charConfig,
                 const Skeleton& skeleton,
                 JPH::Ref<JPH::RagdollSettings> ragdollSettings,
                 JPH::PhysicsSystem* physicsSystem);

    ~CharacterEnv();

    // Non-copyable (owns RagdollInstance), but movable
    CharacterEnv(const CharacterEnv&) = delete;
    CharacterEnv& operator=(const CharacterEnv&) = delete;
    CharacterEnv(CharacterEnv&&) noexcept;
    CharacterEnv& operator=(CharacterEnv&&) noexcept;

    // Reset the character to a reference motion frame.
    // Deactivates the ragdoll, snaps to the pose, then reactivates.
    void reset(const MotionFrame& frame);

    // Single-env step convenience: apply actions, extract observations,
    // and compute reward using the currently set task goal.
    // NOTE: The caller must step the shared physics world between applyActions
    // and extractObservations when using the batched VecEnv path. This method
    // is a convenience for single-env use where the caller steps physics externally.
    StepResult step(const float* actions, int actionDim);

    // Set the current task goal for reward computation.
    void setTask(TaskType task, const glm::vec3& target);

    // Single-env step with explicit task goal.
    StepResult step(const float* actions, int actionDim, const TaskGoal& goal);

    // Compute step result (reward, done, timeout) for the current state.
    StepResult computeStepResult(const TaskGoal& goal);

    // Pre-step: apply policy actions to ragdoll motors.
    void applyActions(const ml::Tensor& actions);

    // Post-step: extract observations after physics has been stepped.
    void extractObservations();

    // Access the most recent AMP observation (single-frame, for discriminator).
    const ml::Tensor& ampObs() const { return currentAmpObs_; }

    // Access the most recent policy observation (temporally stacked).
    const ml::Tensor& policyObs() const { return currentPolicyObs_; }

    // Whether this environment's episode is finished.
    bool isDone() const { return done_; }

    // Current episode step count.
    int stepCount() const { return stepCount_; }

    // Observation dimensions (delegated to config/extractor).
    int ampObsDim() const;
    int policyObsDim() const;
    int actionDim() const { return charConfig_.actionDim; }

private:
    EnvConfig config_;
    ml::CharacterConfig charConfig_;
    ml::ObservationExtractor obsExtractor_;
    ml::ActionApplier actionApplier_;
    RewardComputer rewardComputer_;

    std::unique_ptr<physics::RagdollInstance> ragdoll_;
    const Skeleton* skeleton_ = nullptr;

    TaskGoal currentGoal_;
    int stepCount_ = 0;
    bool done_ = false;

    ml::Tensor currentAmpObs_;
    ml::Tensor currentPolicyObs_;

    // Build a SkeletonPose from a MotionFrame for ragdoll initialization.
    void buildPoseFromFrame(const MotionFrame& frame, SkeletonPose& outPose) const;

    // Check if the character has fallen (root below threshold).
    bool hasFallen() const;
};

} // namespace training
