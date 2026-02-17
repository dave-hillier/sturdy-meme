#pragma once

#include "CALMLowLevelController.h"
#include "CALMLatentSpace.h"
#include "CALMObservation.h"
#include "CALMActionApplier.h"
#include "AnimationBlend.h"
#include <random>

struct Skeleton;
class CharacterController;

namespace ml {

struct CALMControllerConfig {
    int latentStepsMin = 10;       // Min steps before latent resample
    int latentStepsMax = 150;      // Max steps before latent resample
    bool autoResample = false;     // Auto-resample latent on step expiry
};

// Per-character CALM controller that ties together the full inference pipeline:
//   observation extraction → latent management → LLC policy → action application
//
// Each frame:
//   1. Extract observation from skeleton + physics
//   2. Manage latent code (resample, interpolate)
//   3. Run LLC: policy(z, obs) → actions
//   4. Apply actions to produce a SkeletonPose
//
// External control via setLatent() / transitionToLatent() / transitionToBehavior()
// allows high-level controllers and FSMs to direct the character.
class CALMController {
public:
    using Config = CALMControllerConfig;

    CALMController() = default;

    // Initialize with all components
    void init(const CALMCharacterConfig& charConfig,
              CALMLowLevelController llc,
              CALMLatentSpace latentSpace,
              Config config = {});

    // Per-frame update: extract obs, run policy, produce pose.
    // Returns the CALM-generated skeleton pose.
    void update(float deltaTime,
                Skeleton& skeleton,
                const CharacterController& physics,
                SkeletonPose& outPose);

    // Blended update: produces a pose blended with a base animation pose.
    void updateBlended(float deltaTime,
                       Skeleton& skeleton,
                       const CharacterController& physics,
                       const SkeletonPose& basePose,
                       float blendWeight,
                       SkeletonPose& outPose);

    // --- Latent control ---

    // Set latent immediately (no interpolation)
    void setLatent(const Tensor& z);

    // Transition to a new latent over the given number of steps
    void transitionToLatent(const Tensor& z, int steps);

    // Transition to a random behavior with a given tag
    void transitionToBehavior(const std::string& tag, int steps);

    // Get the current (potentially interpolated) latent
    const Tensor& currentLatent() const { return currentLatent_; }

    // --- State queries ---

    bool isInitialized() const { return initialized_; }
    bool isTransitioning() const { return interpolationStepsRemaining_ > 0; }
    int stepsUntilResample() const { return stepsUntilResample_; }

    // Access sub-components
    const CALMLowLevelController& llc() const { return llc_; }
    const CALMLatentSpace& latentSpace() const { return latentSpace_; }
    const CALMObservationExtractor& obsExtractor() const { return obsExtractor_; }
    const CALMActionApplier& actionApplier() const { return actionApplier_; }

    // Reset state (call on teleport/respawn)
    void reset();

private:
    CALMLowLevelController llc_;
    CALMLatentSpace latentSpace_;
    CALMObservationExtractor obsExtractor_;
    CALMActionApplier actionApplier_;
    CALMCharacterConfig charConfig_;
    Config config_;

    // Latent state
    Tensor currentLatent_;
    Tensor targetLatent_;
    int interpolationStepsRemaining_ = 0;
    int interpolationStepsTotal_ = 0;
    int stepsUntilResample_ = 0;

    // RNG for latent resampling
    std::mt19937 rng_{42};

    bool initialized_ = false;

    void stepLatent();
    void resampleLatent();
};

} // namespace ml
