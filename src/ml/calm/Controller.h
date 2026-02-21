#pragma once

#include "LowLevelController.h"
#include "../LatentSpace.h"
#include "../ObservationExtractor.h"
#include "../ActionApplier.h"
#include "AnimationBlend.h"
#include <random>

struct Skeleton;
class CharacterController;

namespace physics {
    class RagdollInstance;
}

namespace ml::calm {

struct ControllerConfig {
    int latentStepsMin = 10;       // Min steps before latent resample
    int latentStepsMax = 150;      // Max steps before latent resample
    bool autoResample = false;     // Auto-resample latent on step expiry
};

// Per-character CALM controller that ties together the full inference pipeline:
//   observation extraction -> latent management -> LLC policy -> action application
//
// Each frame:
//   1. Extract observation from skeleton + physics
//   2. Manage latent code (resample, interpolate)
//   3. Run LLC: policy(z, obs) -> actions
//   4. Apply actions to produce a SkeletonPose
//
// External control via setLatent() / transitionToLatent() / transitionToBehavior()
// allows high-level controllers and FSMs to direct the character.
class Controller {
public:
    using Config = ControllerConfig;

    Controller() = default;

    // Initialize with all components
    void init(const CharacterConfig& charConfig,
              LowLevelController llc,
              LatentSpace latentSpace,
              Config config = {});

    // Per-frame update: extract obs, run policy, produce pose.
    // Returns the generated skeleton pose.
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

    // Physics-driven update: read ragdoll state -> observe -> infer -> drive motors.
    // Instead of setting joint transforms directly, this converts actions to a
    // target pose and feeds it to the ragdoll's motor system.
    // outPose receives the current physics-resolved pose for rendering.
    void updatePhysics(float deltaTime,
                       Skeleton& skeleton,
                       physics::RagdollInstance& ragdoll,
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
    const LowLevelController& llc() const { return llc_; }
    const LatentSpace& latentSpace() const { return latentSpace_; }
    const ObservationExtractor& obsExtractor() const { return obsExtractor_; }
    const ActionApplier& actionApplier() const { return actionApplier_; }

    // Reset state (call on teleport/respawn)
    void reset();

private:
    LowLevelController llc_;
    LatentSpace latentSpace_;
    ObservationExtractor obsExtractor_;
    ActionApplier actionApplier_;
    CharacterConfig charConfig_;
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

} // namespace ml::calm
