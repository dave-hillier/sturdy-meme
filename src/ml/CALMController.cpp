#include "CALMController.h"
#include "GLTFLoader.h"
#include "CharacterController.h"
#include "RagdollInstance.h"
#include <SDL3/SDL_log.h>

namespace ml {

void CALMController::init(const CALMCharacterConfig& charConfig,
                           CALMLowLevelController llc,
                           CALMLatentSpace latentSpace,
                           Config config) {
    charConfig_ = charConfig;
    llc_ = std::move(llc);
    latentSpace_ = std::move(latentSpace);
    config_ = config;

    obsExtractor_ = CALMObservationExtractor(charConfig_);
    actionApplier_ = CALMActionApplier(charConfig_);

    // Initialize latent to a default
    currentLatent_ = latentSpace_.zeroLatent();
    targetLatent_ = currentLatent_;

    // Set initial resample countdown
    if (config_.latentStepsMax > config_.latentStepsMin) {
        std::uniform_int_distribution<int> dist(config_.latentStepsMin, config_.latentStepsMax);
        stepsUntilResample_ = dist(rng_);
    } else {
        stepsUntilResample_ = config_.latentStepsMin;
    }

    initialized_ = true;
    SDL_Log("CALMController: initialized (actionDim=%d, obsDim=%d, latentDim=%d)",
            charConfig_.actionDim, charConfig_.observationDim, latentSpace_.latentDim());
}

void CALMController::update(float deltaTime,
                             Skeleton& skeleton,
                             const CharacterController& physics,
                             SkeletonPose& outPose) {
    if (!initialized_) return;

    // 1. Extract observation
    obsExtractor_.extractFrame(skeleton, physics, deltaTime);

    // 2. Step latent (interpolation / resample)
    stepLatent();

    // 3. Run LLC policy
    Tensor obs = obsExtractor_.getCurrentObs();
    Tensor actions;
    llc_.evaluate(currentLatent_, obs, actions);

    // 4. Clamp and apply actions
    actionApplier_.clampActions(actions);
    actionApplier_.applyToSkeleton(actions, skeleton, outPose);
}

void CALMController::updateBlended(float deltaTime,
                                    Skeleton& skeleton,
                                    const CharacterController& physics,
                                    const SkeletonPose& basePose,
                                    float blendWeight,
                                    SkeletonPose& outPose) {
    if (!initialized_) return;

    // 1. Extract observation
    obsExtractor_.extractFrame(skeleton, physics, deltaTime);

    // 2. Step latent
    stepLatent();

    // 3. Run LLC policy
    Tensor obs = obsExtractor_.getCurrentObs();
    Tensor actions;
    llc_.evaluate(currentLatent_, obs, actions);

    // 4. Clamp and apply blended
    actionApplier_.clampActions(actions);
    actionApplier_.applyBlended(actions, skeleton, basePose, blendWeight, outPose);
}

void CALMController::updatePhysics(float deltaTime,
                                    Skeleton& skeleton,
                                    physics::RagdollInstance& ragdoll,
                                    SkeletonPose& outPose) {
    if (!initialized_) return;

    // 1. Read current pose from ragdoll for observation
    SkeletonPose ragdollPose;
    ragdoll.readPose(ragdollPose, skeleton);

    // Update skeleton joint transforms from ragdoll pose so key body
    // positions are computed correctly in extractKeyBodyFeatures
    for (size_t j = 0; j < ragdollPose.size() && j < skeleton.joints.size(); ++j) {
        skeleton.joints[j].localTransform = ragdollPose[j].toMatrix(skeleton.joints[j].preRotation);
    }

    // 2. Extract observation from ragdoll state
    obsExtractor_.extractFrameFromRagdoll(skeleton, ragdoll, deltaTime);

    // 3. Step latent
    stepLatent();

    // 4. Run LLC policy
    Tensor obs = obsExtractor_.getCurrentObs();
    Tensor actions;
    llc_.evaluate(currentLatent_, obs, actions);

    // 5. Clamp actions and convert to target pose
    actionApplier_.clampActions(actions);

    SkeletonPose targetPose;
    actionApplier_.actionsToTargetPose(actions, skeleton, targetPose);

    // 6. Drive ragdoll motors toward target pose
    ragdoll.driveToTargetPose(targetPose);

    // 7. Output the current physics-resolved pose for rendering
    outPose = ragdollPose;
}

// --- Latent control ---

void CALMController::setLatent(const Tensor& z) {
    currentLatent_ = z;
    targetLatent_ = z;
    interpolationStepsRemaining_ = 0;
    Tensor::l2Normalize(currentLatent_);
}

void CALMController::transitionToLatent(const Tensor& z, int steps) {
    targetLatent_ = z;
    Tensor::l2Normalize(targetLatent_);
    interpolationStepsTotal_ = std::max(1, steps);
    interpolationStepsRemaining_ = interpolationStepsTotal_;
}

void CALMController::transitionToBehavior(const std::string& tag, int steps) {
    const Tensor& z = latentSpace_.sampleByTag(tag, rng_);
    transitionToLatent(z, steps);
}

void CALMController::reset() {
    obsExtractor_.reset();
    currentLatent_ = latentSpace_.zeroLatent();
    targetLatent_ = currentLatent_;
    interpolationStepsRemaining_ = 0;

    if (config_.latentStepsMax > config_.latentStepsMin) {
        std::uniform_int_distribution<int> dist(config_.latentStepsMin, config_.latentStepsMax);
        stepsUntilResample_ = dist(rng_);
    } else {
        stepsUntilResample_ = config_.latentStepsMin;
    }
}

// --- Private ---

void CALMController::stepLatent() {
    // Handle interpolation
    if (interpolationStepsRemaining_ > 0) {
        --interpolationStepsRemaining_;
        float alpha = 1.0f - static_cast<float>(interpolationStepsRemaining_)
                             / static_cast<float>(interpolationStepsTotal_);
        currentLatent_ = CALMLatentSpace::interpolate(currentLatent_, targetLatent_, alpha);
    }

    // Handle auto-resample
    if (config_.autoResample && latentSpace_.librarySize() > 0) {
        --stepsUntilResample_;
        if (stepsUntilResample_ <= 0) {
            resampleLatent();
        }
    }
}

void CALMController::resampleLatent() {
    const Tensor& newZ = latentSpace_.sampleRandom(rng_);
    currentLatent_ = newZ;
    targetLatent_ = newZ;
    interpolationStepsRemaining_ = 0;

    // Reset countdown
    if (config_.latentStepsMax > config_.latentStepsMin) {
        std::uniform_int_distribution<int> dist(config_.latentStepsMin, config_.latentStepsMax);
        stepsUntilResample_ = dist(rng_);
    } else {
        stepsUntilResample_ = config_.latentStepsMin;
    }
}

} // namespace ml
