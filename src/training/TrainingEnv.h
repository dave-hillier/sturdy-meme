#pragma once

#include "MotionClip.h"
#include "RewardFunction.h"
#include "../physics/PhysicsSystem.h"
#include "../physics/ArticulatedBody.h"
#include "../unicon/StateEncoder.h"

#include <memory>
#include <random>
#include <vector>

// Jolt-based training environment with gym-like step/reset interface.
// Each instance owns its own PhysicsWorld and ArticulatedBody.
class TrainingEnv {
public:
    struct Config {
        size_t numJoints = 20;
        size_t tau = 1;
        float fixedTimestep = 1.0f / 60.0f;
        int physicsSubsteps = 1;
        int maxEpisodeSteps = 300;     // 5 seconds at 60 fps
        RewardConfig reward;
    };

    explicit TrainingEnv(const Config& config, const MotionLibrary* motions = nullptr);
    ~TrainingEnv();

    TrainingEnv(TrainingEnv&&) = default;
    TrainingEnv& operator=(TrainingEnv&&) = default;
    TrainingEnv(const TrainingEnv&) = delete;
    TrainingEnv& operator=(const TrainingEnv&) = delete;

    // Reset environment to a random initial state. Returns observation.
    const std::vector<float>& reset();

    // Apply action torques, step physics, compute reward.
    struct StepResult {
        const std::vector<float>* observation = nullptr;
        float reward = 0.0f;
        bool done = false;
    };
    StepResult step(const float* action);

    size_t observationDim() const { return encoder_.getObservationDim(); }
    size_t actionDim() const { return config_.numJoints * 3; }

    // Get current state for value function estimation
    const std::vector<float>& currentObservation() const { return observation_; }

    // Get current body part states for visualization
    void getBodyStates(std::vector<ArticulatedBody::PartState>& states) const;

private:
    void createPhysicsWorld();
    void spawnRagdoll(const glm::vec3& position);
    void destroyRagdoll();
    ArticulatedBodyConfig createTrainingHumanoidConfig() const;
    TargetFrame makeTargetFrame() const;

    Config config_;
    const MotionLibrary* motions_;

    std::unique_ptr<PhysicsWorld> physics_;
    std::unique_ptr<ArticulatedBody> ragdoll_;
    StateEncoder encoder_;

    std::vector<float> observation_;
    std::vector<TargetFrame> targetFrames_;
    std::vector<glm::vec3> torques_;
    std::vector<ArticulatedBody::PartState> currentStates_;

    // Episode state
    int stepCount_ = 0;
    size_t currentClipIdx_ = 0;
    float currentClipTime_ = 0.0f;

    std::mt19937 rng_{42};
};
