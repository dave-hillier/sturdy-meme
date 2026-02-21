#include "Controller.h"
#include "../ModelLoader.h"

#include <SDL3/SDL_log.h>
#include <cmath>
#include <random>

namespace ml::unicon {

void Controller::init(size_t numJoints, size_t tau) {
    numJoints_ = numJoints;
    actionDim_ = numJoints * 3; // 3 torque components per joint
    encoder_.configure(numJoints, tau);
    targetFrames_.resize(tau);

    for (auto& tf : targetFrames_) {
        tf.rootPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        tf.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tf.rootLinearVelocity = glm::vec3(0.0f);
        tf.rootAngularVelocity = glm::vec3(0.0f);
        tf.jointPositions.assign(numJoints, glm::vec3(0.0f));
        tf.jointRotations.assign(numJoints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        tf.jointAngularVelocities.assign(numJoints, glm::vec3(0.0f));
    }

    SDL_Log("UniCon Controller initialized: %zu joints, tau=%zu, obs_dim=%zu",
            numJoints, tau, encoder_.getObservationDim());
}

bool Controller::loadPolicy(const std::string& path) {
    if (!ml::ModelLoader::loadMLP(path, policy_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "UniCon Controller: failed to load policy from '%s'", path.c_str());
        return false;
    }
    policyLoaded_ = true;
    actionDim_ = static_cast<size_t>(policy_.outputSize());
    return true;
}

void Controller::initRandomPolicy() {
    size_t obsDim = encoder_.getObservationDim();

    // Build 3 hidden layers of 1024 with ELU, output layer linear
    policy_ = ml::MLPNetwork();
    policy_.addLayer(static_cast<int>(obsDim), 1024, ml::Activation::ELU);
    policy_.addLayer(1024, 1024, ml::Activation::ELU);
    policy_.addLayer(1024, 1024, ml::Activation::ELU);
    policy_.addLayer(1024, static_cast<int>(actionDim_), ml::Activation::None);

    // Xavier initialization
    std::mt19937 rng(42);
    for (size_t i = 0; i < policy_.numLayers(); ++i) {
        auto& layer = policy_.layer(i);
        float stddev = std::sqrt(2.0f / static_cast<float>(layer.inFeatures + layer.outFeatures));
        std::normal_distribution<float> dist(0.0f, stddev);

        size_t wCount = static_cast<size_t>(layer.outFeatures) * layer.inFeatures;
        std::vector<float> weights(wCount);
        for (auto& w : weights) w = dist(rng);

        std::vector<float> bias(static_cast<size_t>(layer.outFeatures), 0.0f);
        policy_.setLayerWeights(i, std::move(weights), std::move(bias));
    }

    policyLoaded_ = true;
    SDL_Log("UniCon Controller: random policy initialized (obs=%zu, act=%zu)",
            obsDim, actionDim_);
}

void Controller::update(std::vector<ArticulatedBody>& ragdolls,
                         PhysicsWorld& physics) {
    if (!policyLoaded_) return;

    for (auto& ragdoll : ragdolls) {
        if (!ragdoll.isValid()) continue;

        if (!useCustomTarget_) {
            for (auto& tf : targetFrames_) {
                tf = makeStandingTarget(ragdoll, physics);
            }
        }

        // Encode observation
        encoder_.encode(ragdoll, physics, targetFrames_, observation_);

        // Copy into Tensor for MLPNetwork
        size_t obsDim = observation_.size();
        if (obsTensor_.size() != obsDim) {
            obsTensor_ = ml::Tensor(obsDim);
        }
        obsTensor_.copyFrom(observation_.data(), obsDim);

        // Run policy
        policy_.forward(obsTensor_, actionTensor_);

        // Convert flat action tensor to per-joint torques
        size_t partCount = ragdoll.getPartCount();
        torques_.resize(partCount);
        for (size_t i = 0; i < partCount; ++i) {
            size_t base = i * 3;
            if (base + 2 < actionTensor_.size()) {
                torques_[i] = glm::vec3(actionTensor_[base], actionTensor_[base + 1], actionTensor_[base + 2]);
            } else {
                torques_[i] = glm::vec3(0.0f);
            }
        }

        ragdoll.applyTorques(physics, torques_);
    }
}

void Controller::setTargetFrame(const TargetFrame& target) {
    useCustomTarget_ = true;
    for (auto& tf : targetFrames_) {
        tf = target;
    }
}

TargetFrame Controller::makeStandingTarget(const ArticulatedBody& body,
                                            const PhysicsWorld& physics) const {
    TargetFrame tf;
    tf.rootPosition = body.getRootPosition(physics);
    tf.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    tf.rootLinearVelocity = glm::vec3(0.0f);
    tf.rootAngularVelocity = glm::vec3(0.0f);

    size_t n = body.getPartCount();
    tf.jointPositions.assign(n, tf.rootPosition);
    tf.jointRotations.assign(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    tf.jointAngularVelocities.assign(n, glm::vec3(0.0f));

    return tf;
}

} // namespace ml::unicon
