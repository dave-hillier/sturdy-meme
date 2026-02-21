#include "TrainingEnv.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

TrainingEnv::TrainingEnv(const Config& config, const MotionLibrary* motions)
    : config_(config)
    , motions_(motions)
{
    encoder_.configure(config.numJoints, config.tau);
    targetFrames_.resize(config.tau);
    torques_.resize(config.numJoints);

    createPhysicsWorld();
}

TrainingEnv::~TrainingEnv() {
    destroyRagdoll();
}

void TrainingEnv::createPhysicsWorld() {
    auto pw = PhysicsWorld::create();
    if (!pw) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TrainingEnv: failed to create PhysicsWorld");
        return;
    }
    physics_ = std::make_unique<PhysicsWorld>(std::move(*pw));

    // Create flat ground plane
    physics_->createTerrainDisc(50.0f, 0.0f);
}

const std::vector<float>& TrainingEnv::reset() {
    // Destroy existing ragdoll
    destroyRagdoll();

    // Pick a random starting state
    glm::vec3 startPos(0.0f, 1.0f, 0.0f);
    float startClipTime = 0.0f;

    if (motions_ && !motions_->empty()) {
        std::uniform_int_distribution<size_t> clipDist(0, motions_->clips.size() - 1);
        currentClipIdx_ = clipDist(rng_);

        const auto& clip = motions_->clips[currentClipIdx_];
        std::uniform_real_distribution<float> timeDist(0.0f, clip.duration() * 0.8f);
        startClipTime = timeDist(rng_);

        MotionFrame refFrame = clip.sampleAt(startClipTime);
        startPos = refFrame.rootPos;
        startPos.y = std::max(startPos.y, 0.5f); // Don't spawn underground
    }

    currentClipTime_ = startClipTime;
    stepCount_ = 0;

    spawnRagdoll(startPos);

    // Build initial observation
    for (auto& tf : targetFrames_) {
        tf = makeTargetFrame();
    }
    encoder_.encode(*ragdoll_, *physics_, targetFrames_, observation_);

    return observation_;
}

TrainingEnv::StepResult TrainingEnv::step(const float* action) {
    StepResult result;
    result.observation = &observation_;

    if (!ragdoll_ || !ragdoll_->isValid()) {
        result.done = true;
        return result;
    }

    // Convert flat action to per-joint torques
    for (size_t i = 0; i < config_.numJoints; ++i) {
        torques_[i] = glm::vec3(action[i * 3], action[i * 3 + 1], action[i * 3 + 2]);
    }

    // Apply torques and step physics
    ragdoll_->applyTorques(*physics_, torques_);
    physics_->update(config_.fixedTimestep);

    ++stepCount_;
    currentClipTime_ += config_.fixedTimestep;

    // Get current state
    ragdoll_->getState(currentStates_, *physics_);

    // Check for NaN divergence
    if (ragdoll_->hasNaNState(*physics_)) {
        result.done = true;
        result.reward = -1.0f;
        return result;
    }

    // Compute reward
    MotionFrame refFrame;
    if (motions_ && !motions_->empty()) {
        refFrame = motions_->clips[currentClipIdx_].sampleAt(currentClipTime_);
    } else {
        // Standing target
        refFrame.rootPos = glm::vec3(0.0f, 1.0f, 0.0f);
        refFrame.rootRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        refFrame.jointPositions.assign(config_.numJoints, glm::vec3(0.0f));
        refFrame.jointRotations.assign(config_.numJoints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    }

    RewardResult rr = computeReward(currentStates_, refFrame, config_.reward);
    result.reward = rr.total;

    // Episode termination
    if (rr.earlyTermination || stepCount_ >= config_.maxEpisodeSteps) {
        result.done = true;
    }

    // Build next observation
    for (auto& tf : targetFrames_) {
        tf = makeTargetFrame();
    }
    encoder_.encode(*ragdoll_, *physics_, targetFrames_, observation_);

    return result;
}

void TrainingEnv::getBodyStates(std::vector<ArticulatedBody::PartState>& states) const {
    if (ragdoll_ && ragdoll_->isValid() && physics_) {
        ragdoll_->getState(states, *physics_);
    } else {
        states.clear();
    }
}

void TrainingEnv::spawnRagdoll(const glm::vec3& position) {
    ragdoll_ = std::make_unique<ArticulatedBody>();
    auto config = createTrainingHumanoidConfig();
    if (!ragdoll_->create(*physics_, config, position)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TrainingEnv: failed to create ragdoll");
        ragdoll_.reset();
    }
}

void TrainingEnv::destroyRagdoll() {
    if (ragdoll_ && ragdoll_->isValid() && physics_) {
        ragdoll_->destroy(*physics_);
    }
    ragdoll_.reset();
}

TargetFrame TrainingEnv::makeTargetFrame() const {
    TargetFrame tf;

    if (motions_ && !motions_->empty()) {
        float futureTime = currentClipTime_ + config_.fixedTimestep;
        MotionFrame frame = motions_->clips[currentClipIdx_].sampleAt(futureTime);

        tf.rootPosition = frame.rootPos;
        tf.rootRotation = frame.rootRot;
        tf.rootLinearVelocity = glm::vec3(0.0f);
        tf.rootAngularVelocity = glm::vec3(0.0f);
        tf.jointPositions = frame.jointPositions;
        tf.jointRotations = frame.jointRotations;
        tf.jointAngularVelocities.assign(config_.numJoints, glm::vec3(0.0f));
    } else {
        // Standing target
        tf.rootPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        tf.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tf.rootLinearVelocity = glm::vec3(0.0f);
        tf.rootAngularVelocity = glm::vec3(0.0f);
        tf.jointPositions.assign(config_.numJoints, glm::vec3(0.0f));
        tf.jointRotations.assign(config_.numJoints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        tf.jointAngularVelocities.assign(config_.numJoints, glm::vec3(0.0f));
    }

    return tf;
}

ArticulatedBodyConfig TrainingEnv::createTrainingHumanoidConfig() const {
    // Same 20-part humanoid as createHumanoidConfig() but without skeleton mapping.
    // Joint indices are set to -1 since we don't have a rendering skeleton.
    ArticulatedBodyConfig config;
    config.globalScale = 1.0f;

    struct P {
        const char* name;
        int32_t parent;
        float halfHeight, radius, mass;
        glm::vec3 anchorParent, anchorChild;
        glm::vec3 twistAxis, planeAxis;
        float twistMin, twistMax, normalCone, planeCone;
        float effort;
    };

    const P parts[] = {
        {"Pelvis",        -1, 0.08f,0.12f,10.f, {0,0,0},{0,0,0},           {0,1,0},{1,0,0}, -0.3f,0.3f,0.3f,0.3f, 400.f},
        {"LowerSpine",     0, 0.08f,0.10f, 6.f, {0,.08f,0},{0,-.08f,0},    {0,1,0},{1,0,0}, -0.3f,0.3f,0.3f,0.3f, 400.f},
        {"UpperSpine",     1, 0.08f,0.10f, 6.f, {0,.08f,0},{0,-.08f,0},    {0,1,0},{1,0,0}, -0.2f,0.2f,0.2f,0.2f, 400.f},
        {"Chest",          2, 0.10f,0.12f, 8.f, {0,.08f,0},{0,-.10f,0},    {0,1,0},{1,0,0}, -0.2f,0.2f,0.2f,0.2f, 300.f},
        {"Neck",           3, 0.04f,0.04f, 2.f, {0,.10f,0},{0,-.04f,0},    {0,1,0},{1,0,0}, -0.3f,0.3f,0.3f,0.3f, 100.f},
        {"Head",           4, 0.06f,0.09f, 4.f, {0,.04f,0},{0,-.06f,0},    {0,1,0},{1,0,0}, -0.4f,0.4f,0.3f,0.3f, 100.f},
        {"LeftShoulder",   3, 0.06f,0.03f,1.5f, {-.06f,.08f,0},{.06f,0,0}, {-1,0,0},{0,1,0},-0.2f,0.2f,0.2f,0.2f, 100.f},
        {"LeftUpperArm",   6, 0.12f,0.04f,2.5f, {-.06f,0,0},{0,.12f,0},    {0,-1,0},{1,0,0},-1.2f,1.2f,1.2f,0.8f, 150.f},
        {"LeftForearm",    7, 0.11f,0.035f,1.5f,{0,-.12f,0},{0,.11f,0},    {0,-1,0},{1,0,0},-2.0f,0.0f,0.1f,0.1f, 100.f},
        {"LeftHand",       8, 0.04f,0.03f, 0.5f,{0,-.11f,0},{0,.04f,0},    {0,-1,0},{1,0,0},-0.5f,0.5f,0.4f,0.4f,  50.f},
        {"RightShoulder",  3, 0.06f,0.03f,1.5f, {.06f,.08f,0},{-.06f,0,0}, {1,0,0},{0,1,0}, -0.2f,0.2f,0.2f,0.2f, 100.f},
        {"RightUpperArm", 10, 0.12f,0.04f,2.5f, {.06f,0,0},{0,.12f,0},     {0,-1,0},{1,0,0},-1.2f,1.2f,1.2f,0.8f, 150.f},
        {"RightForearm",  11, 0.11f,0.035f,1.5f,{0,-.12f,0},{0,.11f,0},    {0,-1,0},{1,0,0},-2.0f,0.0f,0.1f,0.1f, 100.f},
        {"RightHand",     12, 0.04f,0.03f, 0.5f,{0,-.11f,0},{0,.04f,0},    {0,-1,0},{1,0,0},-0.5f,0.5f,0.4f,0.4f,  50.f},
        {"LeftThigh",      0, 0.18f,0.06f, 6.f, {-.10f,-.08f,0},{0,.18f,0},{0,-1,0},{1,0,0},-0.5f,0.5f,0.8f,0.5f, 600.f},
        {"LeftShin",      14, 0.18f,0.05f, 4.f, {0,-.18f,0},{0,.18f,0},    {0,-1,0},{1,0,0}, 0.0f,2.5f,0.1f,0.1f, 400.f},
        {"LeftFoot",      15, 0.06f,0.035f,1.f, {0,-.18f,0},{0,.035f,.03f},{1,0,0},{0,1,0}, -0.5f,0.5f,0.3f,0.3f, 100.f},
        {"RightThigh",    0,  0.18f,0.06f, 6.f, {.10f,-.08f,0},{0,.18f,0}, {0,-1,0},{1,0,0},-0.5f,0.5f,0.8f,0.5f, 600.f},
        {"RightShin",    17,  0.18f,0.05f, 4.f, {0,-.18f,0},{0,.18f,0},    {0,-1,0},{1,0,0}, 0.0f,2.5f,0.1f,0.1f, 400.f},
        {"RightFoot",    18,  0.06f,0.035f,1.f, {0,-.18f,0},{0,.035f,.03f},{1,0,0},{0,1,0}, -0.5f,0.5f,0.3f,0.3f, 100.f},
    };

    for (const auto& p : parts) {
        BodyPartDef def;
        def.name = p.name;
        def.skeletonJointIndex = -1; // no rendering skeleton in training
        def.parentPartIndex = p.parent;
        def.halfHeight = p.halfHeight;
        def.radius = p.radius;
        def.mass = p.mass;
        def.localAnchorInParent = p.anchorParent;
        def.localAnchorInChild = p.anchorChild;
        def.twistAxis = p.twistAxis;
        def.planeAxis = p.planeAxis;
        def.twistMinAngle = p.twistMin;
        def.twistMaxAngle = p.twistMax;
        def.normalHalfConeAngle = p.normalCone;
        def.planeHalfConeAngle = p.planeCone;
        def.effortFactor = p.effort;
        config.parts.push_back(def);
    }

    return config;
}
