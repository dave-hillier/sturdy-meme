#pragma once

#include "TrainingMLP.h"
#include "TrainingEnv.h"
#include "RolloutBuffer.h"
#include "MotionClip.h"

#include <memory>
#include <string>
#include <vector>

struct TrainerConfig {
    // Environment
    size_t numEnvs = 32;
    TrainingEnv::Config envConfig;

    // PPO hyperparameters
    float gamma = 0.99f;
    float lambda = 0.95f;
    float clipEpsilon = 0.2f;
    float policyLR = 3e-4f;
    float valueLR = 1e-3f;
    size_t rolloutSteps = 64;    // steps per env before update
    size_t ppoEpochs = 5;
    size_t minibatchSize = 256;
    float entropyCoeff = 0.01f;
    float valueCoeff = 0.5f;
    float maxGradNorm = 0.5f;

    // Training loop
    size_t totalIterations = 1000;
    size_t logInterval = 10;
    size_t saveInterval = 50;
    std::string outputDir = "generated/unicon";
    std::string motionDir = "assets/motions";
};

class Trainer {
public:
    explicit Trainer(const TrainerConfig& config);

    // Run the full training loop.
    void train();

    // Save current policy weights.
    void saveCheckpoint(const std::string& path) const;

private:
    void collectRollouts();
    void ppoUpdate();
    void logStats(size_t iteration) const;

    TrainerConfig config_;

    // Networks
    std::unique_ptr<GaussianPolicy> policy_;
    std::unique_ptr<TrainingMLP> valueNet_;

    // Environments
    std::vector<std::unique_ptr<TrainingEnv>> envs_;
    MotionLibrary motions_;

    // Rollout storage
    std::unique_ptr<RolloutBuffer> buffer_;

    // Per-env state
    std::vector<std::vector<float>> envObs_;

    // Training stats
    struct Stats {
        float meanReward = 0.0f;
        float meanEpisodeLen = 0.0f;
        float policyLoss = 0.0f;
        float valueLoss = 0.0f;
        float entropy = 0.0f;
        size_t episodesCompleted = 0;
    };
    Stats stats_;

    std::mt19937 rng_{42};
};
