#include "CharacterEnv.h"
#include "RagdollInstance.h"
#include "GLTFLoader.h"
#include "AnimationBlend.h"

#include <SDL3/SDL_log.h>
#include <cmath>

namespace training {

CharacterEnv::CharacterEnv(const EnvConfig& config,
                           const ml::CharacterConfig& charConfig,
                           const Skeleton& skeleton,
                           JPH::Ref<JPH::RagdollSettings> ragdollSettings,
                           JPH::PhysicsSystem* physicsSystem)
    : config_(config)
    , charConfig_(charConfig)
    , obsExtractor_(charConfig)
    , actionApplier_(charConfig)
    , skeleton_(&skeleton)
{
    ragdoll_ = std::make_unique<physics::RagdollInstance>(
        ragdollSettings, skeleton, physicsSystem);
    ragdoll_->activate();

    SDL_Log("CharacterEnv: created with obsDim=%d, actionDim=%d",
            charConfig_.observationDim, charConfig_.actionDim);
}

CharacterEnv::~CharacterEnv() = default;

CharacterEnv::CharacterEnv(CharacterEnv&&) noexcept = default;
CharacterEnv& CharacterEnv::operator=(CharacterEnv&&) noexcept = default;

void CharacterEnv::buildPoseFromFrame(const MotionFrame& frame,
                                       SkeletonPose& outPose) const
{
    const auto& joints = skeleton_->joints;
    outPose.resize(joints.size());

    for (size_t i = 0; i < joints.size(); ++i) {
        BonePose& bp = outPose[i];

        if (static_cast<int32_t>(i) == charConfig_.rootJointIndex) {
            // Root joint: use motion frame root transform
            bp.translation = frame.rootPosition;
            bp.rotation = frame.rootRotation;
            bp.scale = glm::vec3(1.0f);
        } else {
            // Non-root joints: use joint rotation from motion frame if available,
            // otherwise fall back to bind pose identity
            bp.translation = glm::vec3(0.0f);
            bp.scale = glm::vec3(1.0f);

            if (i < frame.jointRotations.size()) {
                bp.rotation = frame.jointRotations[i];
            } else {
                bp.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    }
}

void CharacterEnv::setTask(TaskType task, const glm::vec3& target) {
    currentGoal_.type = task;
    currentGoal_.targetPosition = target;
    // For heading tasks, compute heading angle from target direction vector
    if (task == TaskType::Heading) {
        currentGoal_.targetHeading = std::atan2(target.x, -target.z);
    }
}

StepResult CharacterEnv::step(const float* actions, int actionDim) {
    return step(actions, actionDim, currentGoal_);
}

StepResult CharacterEnv::step(const float* actions, int actDim, const TaskGoal& goal) {
    if (done_) {
        StepResult result;
        result.done = true;
        result.taskReward = 0.0f;
        return result;
    }

    // Apply actions
    ml::Tensor actionTensor(static_cast<size_t>(actDim));
    actionTensor.copyFrom(actions, static_cast<size_t>(actDim));
    applyActions(actionTensor);

    // NOTE: Physics is NOT stepped here. The caller (or VecEnv) must step
    // the shared PhysicsWorld. This method is a convenience for the
    // extract-and-compute part after physics has been advanced.

    // Extract observations
    extractObservations();

    // Compute reward and termination
    return computeStepResult(goal);
}

void CharacterEnv::reset(const MotionFrame& frame) {
    stepCount_ = 0;
    done_ = false;

    // Build a skeleton pose from the motion frame
    SkeletonPose pose;
    buildPoseFromFrame(frame, pose);

    // Snap the ragdoll to this pose immediately (bypasses physics)
    ragdoll_->deactivate();
    ragdoll_->setPoseImmediate(pose, *skeleton_);
    ragdoll_->activate();

    // Reset observation history so stale frames don't leak across episodes
    obsExtractor_.reset();

    // Extract initial observations
    extractObservations();

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "CharacterEnv::reset: root at (%.2f, %.2f, %.2f)",
                 frame.rootPosition.x, frame.rootPosition.y, frame.rootPosition.z);
}

StepResult CharacterEnv::computeStepResult(const TaskGoal& goal) {
    StepResult result;

    ++stepCount_;

    // Check for fall
    if (hasFallen()) {
        done_ = true;
        result.done = true;
        result.timeout = false;
        result.taskReward = 0.0f;
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "CharacterEnv: early termination (fall) at step %d", stepCount_);
        return result;
    }

    // Check for episode timeout
    if (stepCount_ >= config_.maxEpisodeSteps) {
        done_ = true;
        result.done = true;
        result.timeout = true;
    }

    // Gather state for reward computation
    glm::vec3 rootPos = ragdoll_->getRootPosition();
    glm::quat rootRot = ragdoll_->getRootRotation();
    glm::vec3 rootVel = ragdoll_->getRootLinearVelocity();

    // Collect key body positions from ragdoll
    // Key bodies are identified by joint index in the config; we read their
    // world-space positions from the ragdoll body array.
    std::vector<glm::vec3> keyBodyPositions;
    keyBodyPositions.reserve(charConfig_.keyBodies.size());

    SkeletonPose currentPose;
    ragdoll_->readPose(currentPose, *skeleton_);

    // Compute global transforms to get key body world positions
    // Update skeleton joint transforms from ragdoll pose
    // (We work on a local copy to avoid mutating the shared skeleton)
    std::vector<glm::mat4> globalTransforms;
    {
        // Build global transforms from the ragdoll pose
        // Each bone's local transform is reconstructed from BonePose
        std::vector<glm::mat4> localTransforms(skeleton_->joints.size());
        for (size_t j = 0; j < currentPose.size() && j < skeleton_->joints.size(); ++j) {
            localTransforms[j] = currentPose[j].toMatrix(skeleton_->joints[j].preRotation);
        }

        // Compute global transforms by walking the hierarchy
        globalTransforms.resize(skeleton_->joints.size(), glm::mat4(1.0f));
        for (size_t j = 0; j < skeleton_->joints.size(); ++j) {
            int32_t parentIdx = skeleton_->joints[j].parentIndex;
            if (parentIdx >= 0) {
                globalTransforms[j] = globalTransforms[parentIdx] * localTransforms[j];
            } else {
                globalTransforms[j] = localTransforms[j];
            }
        }
    }

    for (const auto& kb : charConfig_.keyBodies) {
        if (kb.jointIndex >= 0 &&
            static_cast<size_t>(kb.jointIndex) < globalTransforms.size()) {
            glm::vec3 pos = glm::vec3(globalTransforms[kb.jointIndex][3]);
            keyBodyPositions.push_back(pos);
        } else {
            keyBodyPositions.push_back(glm::vec3(0.0f));
        }
    }

    result.taskReward = rewardComputer_.computeTaskReward(
        goal, rootPos, rootRot, rootVel, keyBodyPositions);

    return result;
}

void CharacterEnv::applyActions(const ml::Tensor& actions) {
    if (done_) return;

    // Convert actions to a target skeleton pose and drive the ragdoll
    ml::Tensor clampedActions = actions;
    actionApplier_.clampActions(clampedActions);

    SkeletonPose targetPose;
    actionApplier_.actionsToTargetPose(clampedActions, *skeleton_, targetPose);
    ragdoll_->driveToTargetPose(targetPose);
}

void CharacterEnv::extractObservations() {
    // The ObservationExtractor needs skeleton joint transforms to be current.
    // We read the ragdoll pose and update a mutable copy of the skeleton's
    // local transforms. Since the skeleton is shared, we temporarily update it
    // and rely on the fact that each env calls this sequentially.
    //
    // NOTE: In a truly parallel setting this would need per-env skeleton copies.
    // For the current single-threaded VecEnv this is safe.
    Skeleton& mutableSkeleton = const_cast<Skeleton&>(*skeleton_);

    SkeletonPose ragdollPose;
    ragdoll_->readPose(ragdollPose, mutableSkeleton);

    // Sync skeleton local transforms from ragdoll pose
    for (size_t j = 0; j < ragdollPose.size() && j < mutableSkeleton.joints.size(); ++j) {
        mutableSkeleton.joints[j].localTransform =
            ragdollPose[j].toMatrix(mutableSkeleton.joints[j].preRotation);
    }

    obsExtractor_.extractFrameFromRagdoll(mutableSkeleton, *ragdoll_, config_.simTimestep);

    currentAmpObs_ = obsExtractor_.getCurrentObs();
    currentPolicyObs_ = obsExtractor_.getPolicyObs();
}

bool CharacterEnv::hasFallen() const {
    float rootHeight = ragdoll_->getRootPosition().y;
    return rootHeight < config_.earlyTerminationHeight;
}

int CharacterEnv::ampObsDim() const {
    return obsExtractor_.frameDim();
}

int CharacterEnv::policyObsDim() const {
    // Policy obs = numPolicyObsSteps * frameDim
    return charConfig_.numPolicyObsSteps * obsExtractor_.frameDim();
}

} // namespace training
