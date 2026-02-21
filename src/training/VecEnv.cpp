#include "VecEnv.h"
#include "GLTFLoader.h"
#include "RagdollBuilder.h"

#include <SDL3/SDL_log.h>
#include <cmath>
#include <cassert>

namespace training {

// Grid spacing between characters (meters).
// 3m is enough for humanoid characters to avoid inter-collision.
static constexpr float GRID_SPACING = 3.0f;

glm::vec3 VecEnv::envGridPosition(int envIndex) {
    // Lay characters out on a square grid in the XZ plane
    int gridSize = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(envIndex + 1))));
    int row = envIndex / gridSize;
    int col = envIndex % gridSize;
    return glm::vec3(
        static_cast<float>(col) * GRID_SPACING,
        0.0f,
        static_cast<float>(row) * GRID_SPACING
    );
}

VecEnv::VecEnv(int numEnvs,
               const EnvConfig& config,
               const Skeleton& skeleton)
    : VecEnv(numEnvs, config,
             ml::CharacterConfig::buildFromSkeleton(skeleton),
             skeleton,
             [&]() {
                 std::vector<glm::mat4> globalBindPose;
                 skeleton.computeGlobalTransforms(globalBindPose);
                 return physics::RagdollBuilder::build(skeleton, globalBindPose);
             }())
{
}

VecEnv::VecEnv(int numEnvs,
               const EnvConfig& config,
               const ml::CharacterConfig& charConfig,
               const Skeleton& skeleton,
               JPH::Ref<JPH::RagdollSettings> ragdollSettings)
    : numEnvs_(numEnvs)
    , config_(config)
    , physicsWorld_(PhysicsWorld::create().value())
    , ownedSkeleton_(std::make_unique<Skeleton>(skeleton))
{
    SDL_Log("VecEnv: creating %d environments", numEnvs);

    // Create a ground plane for the training arena
    float arenaRadius = GRID_SPACING * static_cast<float>(numEnvs) + 10.0f;
    physicsWorld_.createTerrainDisc(arenaRadius, 0.0f);

    JPH::PhysicsSystem* physSystem = physicsWorld_.getPhysicsSystem();

    // Create per-environment CharacterEnvs
    envs_.reserve(numEnvs);
    for (int i = 0; i < numEnvs; ++i) {
        envs_.emplace_back(config, charConfig, skeleton, ragdollSettings, physSystem);
    }

    // Allocate contiguous output buffers
    int policyDim = envs_.empty() ? 0 : envs_[0].policyObsDim();
    int ampDim = envs_.empty() ? 0 : envs_[0].ampObsDim();

    observations_.resize(static_cast<size_t>(numEnvs) * policyDim, 0.0f);
    ampObservations_.resize(static_cast<size_t>(numEnvs) * ampDim, 0.0f);
    rewards_.resize(numEnvs, 0.0f);
    dones_.resize(numEnvs, 0);

    // Reset all envs to a default standing pose spread across the grid
    reset();

    SDL_Log("VecEnv: ready (policyObsDim=%d, ampObsDim=%d, actionDim=%d)",
            policyDim, ampDim, actionDim());
}

VecEnv::~VecEnv() {
    // CharacterEnvs (and their ragdolls) must be destroyed before PhysicsWorld.
    // std::vector destruction order is element-by-element in order, which is fine,
    // but we explicitly clear to make the ordering unambiguous.
    envs_.clear();
}

void VecEnv::reset() {
    for (int i = 0; i < numEnvs_; ++i) {
        MotionFrame defaultFrame;
        glm::vec3 gridPos = envGridPosition(i);
        defaultFrame.rootPosition = glm::vec3(gridPos.x, 1.0f, gridPos.z);
        defaultFrame.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        envs_[i].reset(defaultFrame);
        dones_[i] = 0;
    }

    copyObsToBuffers();
}

void VecEnv::resetDone(const std::vector<MotionFrame>& frames) {
    int frameIdx = 0;
    for (int i = 0; i < numEnvs_; ++i) {
        if (envs_[i].isDone()) {
            if (frameIdx >= static_cast<int>(frames.size())) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "VecEnv::resetDone: not enough frames (%zu) for done envs",
                            frames.size());
                break;
            }

            // Offset the motion frame root position to this env's grid slot
            MotionFrame adjusted = frames[frameIdx];
            glm::vec3 gridPos = envGridPosition(i);
            adjusted.rootPosition.x += gridPos.x;
            adjusted.rootPosition.z += gridPos.z;

            envs_[i].reset(adjusted);
            dones_[i] = 0;
            ++frameIdx;
        }
    }
}

void VecEnv::resetEnv(int envIndex, const MotionFrame& frame) {
    assert(envIndex >= 0 && envIndex < numEnvs_);

    // Offset to grid position
    MotionFrame adjusted = frame;
    glm::vec3 gridPos = envGridPosition(envIndex);
    adjusted.rootPosition.x += gridPos.x;
    adjusted.rootPosition.z += gridPos.z;

    envs_[envIndex].reset(adjusted);
    dones_[envIndex] = 0;
}

void VecEnv::setTask(TaskType task, const glm::vec3& target) {
    currentGoal_.type = task;
    currentGoal_.targetPosition = target;
    if (task == TaskType::Heading) {
        currentGoal_.targetHeading = std::atan2(target.x, -target.z);
    }
}

void VecEnv::step(const float* actions) {
    // Build a per-env goals vector from the shared current goal
    std::vector<TaskGoal> goals(numEnvs_, currentGoal_);
    step(actions, goals);
}

void VecEnv::step(const float* actions, const std::vector<TaskGoal>& goals) {
    assert(static_cast<int>(goals.size()) == numEnvs_);

    int actDim = actionDim();

    // 1. Apply actions to all environments
    for (int i = 0; i < numEnvs_; ++i) {
        if (!envs_[i].isDone()) {
            ml::Tensor actionTensor(static_cast<size_t>(actDim));
            actionTensor.copyFrom(actions + i * actDim, static_cast<size_t>(actDim));
            envs_[i].applyActions(actionTensor);
        }
    }

    // 2. Step the shared physics world once
    physicsWorld_.update(config_.simTimestep);

    // 3. Extract observations from all environments
    for (int i = 0; i < numEnvs_; ++i) {
        if (!envs_[i].isDone()) {
            envs_[i].extractObservations();
        }
    }

    // 4. Compute rewards and check termination
    for (int i = 0; i < numEnvs_; ++i) {
        if (!envs_[i].isDone()) {
            StepResult result = envs_[i].computeStepResult(goals[i]);
            rewards_[i] = result.taskReward;
            dones_[i] = result.done ? 1 : 0;
        } else {
            rewards_[i] = 0.0f;
            dones_[i] = 1;
        }
    }

    // 5. Copy observations to contiguous buffers
    copyObsToBuffers();
}

void VecEnv::copyObsToBuffers() {
    int pDim = policyObsDim();
    int aDim = ampObsDim();

    for (int i = 0; i < numEnvs_; ++i) {
        const ml::Tensor& pObs = envs_[i].policyObs();
        const ml::Tensor& aObs = envs_[i].ampObs();

        // Copy policy observations
        if (!pObs.empty()) {
            size_t copySize = std::min(pObs.size(), static_cast<size_t>(pDim));
            std::copy(pObs.data(), pObs.data() + copySize,
                      observations_.data() + i * pDim);
        }

        // Copy AMP observations
        if (!aObs.empty()) {
            size_t copySize = std::min(aObs.size(), static_cast<size_t>(aDim));
            std::copy(aObs.data(), aObs.data() + copySize,
                      ampObservations_.data() + i * aDim);
        }
    }
}

int VecEnv::policyObsDim() const {
    return envs_.empty() ? 0 : envs_[0].policyObsDim();
}

int VecEnv::ampObsDim() const {
    return envs_.empty() ? 0 : envs_[0].ampObsDim();
}

int VecEnv::actionDim() const {
    return envs_.empty() ? 0 : envs_[0].actionDim();
}

int VecEnv::loadMotions(const std::string& directory) {
    if (!ownedSkeleton_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VecEnv::loadMotions: no skeleton available");
        return 0;
    }
    return motionLibrary_.loadFromDirectory(directory, *ownedSkeleton_);
}

int VecEnv::loadMotionFile(const std::string& path) {
    if (!ownedSkeleton_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "VecEnv::loadMotionFile: no skeleton available");
        return 0;
    }
    return motionLibrary_.loadFile(path, *ownedSkeleton_);
}

void VecEnv::resetDoneWithMotions() {
    if (motionLibrary_.empty() || !ownedSkeleton_) {
        // No motions loaded — use default standing pose
        for (int i = 0; i < numEnvs_; ++i) {
            if (envs_[i].isDone()) {
                MotionFrame defaultFrame;
                glm::vec3 gridPos = envGridPosition(i);
                defaultFrame.rootPosition = glm::vec3(gridPos.x, 1.0f, gridPos.z);
                defaultFrame.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                envs_[i].reset(defaultFrame);
                dones_[i] = 0;
            }
        }
        return;
    }

    for (int i = 0; i < numEnvs_; ++i) {
        if (envs_[i].isDone()) {
            MotionFrame frame = motionLibrary_.sampleRandomFrame(rng_, *ownedSkeleton_);

            // Offset to this env's grid position
            glm::vec3 gridPos = envGridPosition(i);
            frame.rootPosition.x += gridPos.x;
            frame.rootPosition.z += gridPos.z;

            envs_[i].reset(frame);
            dones_[i] = 0;
        }
    }
}

} // namespace training
