#include "Trainer.h"
#include "TrainingVisualizer.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>

namespace fs = std::filesystem;

Trainer::~Trainer() = default;

Trainer::Trainer(const TrainerConfig& config)
    : config_(config)
{
    // Load motion data
    if (!config.motionDir.empty()) {
        motions_.loadDirectory(config.motionDir);
    }
    if (motions_.empty()) {
        SDL_Log("Trainer: no motion data found, using standing target");
        motions_.addStandingClip();
    }

    SDL_Log("Trainer: %zu motion clips, %zu total frames",
            motions_.clips.size(), motions_.totalFrames());

    // Determine dimensions
    size_t numJoints = config.envConfig.numJoints;
    size_t tau = config.envConfig.tau;

    // Create a temporary encoder to get observation dimension
    StateEncoder tmpEncoder;
    tmpEncoder.configure(numJoints, tau);
    size_t obsDim = tmpEncoder.getObservationDim();
    size_t actDim = numJoints * 3;

    SDL_Log("Trainer: obs_dim=%zu, act_dim=%zu", obsDim, actDim);

    // Create policy and value networks
    TrainingMLP::Config policyConfig;
    policyConfig.inputDim = obsDim;
    policyConfig.outputDim = actDim;
    policyConfig.hiddenDim = 1024;
    policyConfig.hiddenLayers = 3;

    policy_ = std::make_unique<GaussianPolicy>(policyConfig, -0.5f);

    TrainingMLP::Config valueConfig;
    valueConfig.inputDim = obsDim;
    valueConfig.outputDim = 1;
    valueConfig.hiddenDim = 512;
    valueConfig.hiddenLayers = 2;

    valueNet_ = std::make_unique<TrainingMLP>(valueConfig);

    SDL_Log("Trainer: policy params=%zu, value params=%zu",
            policy_->network.parameterCount(), valueNet_->parameterCount());

    // Create environments
    envs_.reserve(config.numEnvs);
    for (size_t i = 0; i < config.numEnvs; ++i) {
        envs_.push_back(std::make_unique<TrainingEnv>(config.envConfig, &motions_));
    }

    // Rollout buffer: numEnvs * rolloutSteps transitions
    size_t bufferSize = config.numEnvs * config.rolloutSteps;
    buffer_ = std::make_unique<RolloutBuffer>(bufferSize, obsDim, actDim);

    // Initialize envs
    envObs_.resize(config.numEnvs);
    for (size_t i = 0; i < config.numEnvs; ++i) {
        const auto& obs = envs_[i]->reset();
        envObs_[i] = obs;
    }

    SDL_Log("Trainer: %zu environments initialized, buffer capacity=%zu",
            config.numEnvs, bufferSize);

    // Create visualizer if requested
    if (config_.visualize) {
        TrainingVisualizer::Config vizConfig;
        vizConfig.maxVisible = std::min(config.numEnvs, size_t(16));
        visualizer_ = std::make_unique<TrainingVisualizer>(vizConfig);
        SDL_Log("Trainer: visualization enabled, showing %zu environments",
                vizConfig.maxVisible);
    }
}

void Trainer::train() {
    SDL_Log("Trainer: starting training for %zu iterations", config_.totalIterations);

    // Create output directory
    fs::create_directories(config_.outputDir);

    for (size_t iter = 0; iter < config_.totalIterations; ++iter) {
        // 1. Render current state (before collecting new rollouts)
        if (visualizer_ && visualizer_->isOpen()) {
            if (!visualizer_->pollEvents()) {
                SDL_Log("Trainer: visualization window closed, continuing without viz");
                visualizer_.reset();
            } else {
                renderFrame(iter);
            }
        }

        // 2. Collect rollouts
        collectRollouts();

        // 3. PPO update
        ppoUpdate();

        // 4. Logging
        if ((iter + 1) % config_.logInterval == 0) {
            logStats(iter + 1);
        }

        // 5. Save checkpoint
        if ((iter + 1) % config_.saveInterval == 0) {
            std::string path = config_.outputDir + "/policy_weights.bin";
            saveCheckpoint(path);
        }
    }

    // Final save
    std::string finalPath = config_.outputDir + "/policy_weights.bin";
    saveCheckpoint(finalPath);
    SDL_Log("Trainer: training complete, final weights saved to '%s'", finalPath.c_str());
}

void Trainer::collectRollouts() {
    buffer_->clear();
    stats_ = {};

    float totalReward = 0.0f;
    size_t totalSteps = 0;

    for (size_t step = 0; step < config_.rolloutSteps; ++step) {
        for (size_t e = 0; e < config_.numEnvs; ++e) {
            const float* obs = envObs_[e].data();
            size_t actDim = envs_[e]->actionDim();

            // Get value estimate
            const auto& valueOut = valueNet_->forward(obs);
            float value = valueOut[0];

            // Sample action from policy
            std::vector<float> action(actDim);
            float logProb = policy_->sampleAction(obs, action.data(), rng_);

            // Step environment
            auto result = envs_[e]->step(action.data());

            // Store transition
            Transition t;
            t.observation = envObs_[e];
            t.action = std::move(action);
            t.reward = result.reward;
            t.value = value;
            t.logProb = logProb;
            t.done = result.done;
            buffer_->addTransition(t);

            totalReward += result.reward;
            ++totalSteps;

            if (result.done) {
                // Reset environment
                const auto& newObs = envs_[e]->reset();
                envObs_[e] = newObs;
                ++stats_.episodesCompleted;
            } else {
                envObs_[e].assign(result.observation->begin(), result.observation->end());
            }
        }
    }

    // Bootstrap value for last state
    float lastValueSum = 0.0f;
    for (size_t e = 0; e < config_.numEnvs; ++e) {
        const auto& v = valueNet_->forward(envObs_[e].data());
        lastValueSum += v[0];
    }
    float bootstrapValue = lastValueSum / static_cast<float>(config_.numEnvs);

    buffer_->computeGAE(bootstrapValue, config_.gamma, config_.lambda);

    stats_.meanReward = totalReward / static_cast<float>(totalSteps);
    if (stats_.episodesCompleted > 0) {
        stats_.meanEpisodeLen = static_cast<float>(totalSteps) / static_cast<float>(stats_.episodesCompleted);
    }
}

void Trainer::ppoUpdate() {
    size_t bufSize = buffer_->size();
    if (bufSize == 0) return;

    size_t obsDim = buffer_->obsDim();
    size_t actDim = buffer_->actDim();

    const float* allObs = buffer_->observations().data();
    const float* allActs = buffer_->actions().data();
    const float* allAdvantages = buffer_->advantages().data();
    const float* allReturns = buffer_->returns().data();
    const float* allOldLogProbs = buffer_->oldLogProbs().data();

    float policyLossSum = 0.0f;
    float valueLossSum = 0.0f;
    float entropySum = 0.0f;
    size_t updateCount = 0;

    // Generate shuffled indices
    std::vector<size_t> indices(bufSize);
    std::iota(indices.begin(), indices.end(), 0);

    for (size_t epoch = 0; epoch < config_.ppoEpochs; ++epoch) {
        std::shuffle(indices.begin(), indices.end(), rng_);

        for (size_t batchStart = 0; batchStart < bufSize; batchStart += config_.minibatchSize) {
            size_t batchEnd = std::min(batchStart + config_.minibatchSize, bufSize);
            size_t batchSize = batchEnd - batchStart;

            policy_->zeroGrad();
            valueNet_->zeroGrad();

            float batchPolicyLoss = 0.0f;
            float batchValueLoss = 0.0f;
            float batchEntropy = 0.0f;

            for (size_t b = batchStart; b < batchEnd; ++b) {
                size_t idx = indices[b];
                const float* obs = allObs + idx * obsDim;
                const float* act = allActs + idx * actDim;
                float advantage = allAdvantages[idx];
                float returnVal = allReturns[idx];
                float oldLogProb = allOldLogProbs[idx];

                // Policy loss (clipped surrogate)
                float newLogProb = policy_->logProb(obs, act);
                float ratio = std::exp(newLogProb - oldLogProb);
                float clippedRatio = std::clamp(ratio, 1.0f - config_.clipEpsilon,
                                                        1.0f + config_.clipEpsilon);
                float surr1 = ratio * advantage;
                float surr2 = clippedRatio * advantage;
                float policyLoss = -std::min(surr1, surr2);
                batchPolicyLoss += policyLoss;

                // Entropy bonus (Gaussian entropy = 0.5 * ln(2*pi*e*var))
                float entropy = 0.0f;
                for (size_t i = 0; i < actDim; ++i) {
                    entropy += policy_->logStd[i] + 0.5f * std::log(2.0f * 3.14159265f * 2.71828183f);
                }
                batchEntropy += entropy;

                // Policy backward: d(-min(surr1,surr2) - entropy_coeff * entropy) / d(params)
                float policyGradScale = -advantage; // simplified: ratio * advantage gradient
                if (surr2 < surr1) {
                    // Clipped: no gradient from ratio
                    policyGradScale = 0.0f;
                }
                policyGradScale /= static_cast<float>(batchSize);
                policy_->backward(obs, act, policyGradScale);

                // Value loss (MSE)
                const auto& valueOut = valueNet_->forward(obs);
                float valuePred = valueOut[0];
                float valueDiff = valuePred - returnVal;
                float valueLoss = 0.5f * valueDiff * valueDiff;
                batchValueLoss += valueLoss;

                // Value backward
                float valueGrad = valueDiff / static_cast<float>(batchSize);
                valueNet_->backward(&valueGrad);
            }

            // Optimizer steps
            policy_->adamStep(config_.policyLR);
            valueNet_->adamStep(config_.valueLR);

            policyLossSum += batchPolicyLoss / static_cast<float>(batchSize);
            valueLossSum += batchValueLoss / static_cast<float>(batchSize);
            entropySum += batchEntropy / static_cast<float>(batchSize);
            ++updateCount;
        }
    }

    if (updateCount > 0) {
        stats_.policyLoss = policyLossSum / static_cast<float>(updateCount);
        stats_.valueLoss = valueLossSum / static_cast<float>(updateCount);
        stats_.entropy = entropySum / static_cast<float>(updateCount);
    }
}

void Trainer::saveCheckpoint(const std::string& path) const {
    policy_->saveWeights(path);
}

void Trainer::renderFrame(size_t iteration) {
    if (!visualizer_ || !visualizer_->isOpen()) return;

    visualizer_->beginFrame();
    visualizer_->drawGround();

    // Draw each visible environment's ragdoll
    size_t numVisible = std::min(config_.numEnvs, size_t(16));
    std::vector<ArticulatedBody::PartState> states;

    for (size_t e = 0; e < numVisible; ++e) {
        envs_[e]->getBodyStates(states);
        if (!states.empty()) {
            visualizer_->drawRagdoll(e, states);
        }
    }

    visualizer_->drawStats(iteration, stats_.meanReward, stats_.meanEpisodeLen,
                           stats_.policyLoss, stats_.valueLoss, stats_.episodesCompleted);
    visualizer_->endFrame();
}

void Trainer::logStats(size_t iteration) const {
    SDL_Log("iter=%zu | reward=%.4f | ep_len=%.0f | episodes=%zu | "
            "pi_loss=%.4f | v_loss=%.4f | entropy=%.2f",
            iteration,
            stats_.meanReward,
            stats_.meanEpisodeLen,
            stats_.episodesCompleted,
            stats_.policyLoss,
            stats_.valueLoss,
            stats_.entropy);
}
