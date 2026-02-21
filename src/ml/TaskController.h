#pragma once

#include "MLPNetwork.h"
#include "Tensor.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace ml {

// High-Level Task Controller.
// Task-specific policies that output latent codes to command a low-level controller.
// Each task controller takes a task observation (target direction, position, etc.)
// and produces a latent code.
class TaskController {
public:
    TaskController() = default;

    // Set the task policy network
    void setNetwork(MLPNetwork network);

    // Evaluate: task observation -> latent code (L2-normalized)
    // taskObs: task-specific observation (target direction, distance, etc.)
    // outLatent: L2-normalized latent code
    void evaluate(const Tensor& taskObs, Tensor& outLatent) const;

    // Check if weights are loaded
    bool isLoaded() const { return network_.numLayers() > 0; }

    // Get the expected task observation dimension
    int taskObsDim() const;

    // Get the output latent dimension
    int latentDim() const;

    // Access network for weight loading
    MLPNetwork& network() { return network_; }

private:
    MLPNetwork network_;
    mutable Tensor output_;
};

// HeadingController — move in a direction at a target speed.
// Task obs: [local_target_dir_x(1), local_target_dir_z(1), target_speed(1)]
class HeadingController {
public:
    HeadingController() = default;

    // Set the underlying task network
    void setHLC(TaskController hlc) { hlc_ = std::move(hlc); }

    // Set the desired heading direction and speed
    // direction: world-space 2D direction (xz plane, will be rotated to local frame)
    // speed: target movement speed (m/s)
    void setTarget(glm::vec2 direction, float speed);

    // Evaluate given the character's current heading (yaw angle in radians)
    // Returns the latent code to feed to the LLC
    void evaluate(float characterHeading, Tensor& outLatent) const;

    bool isLoaded() const { return hlc_.isLoaded(); }
    TaskController& hlc() { return hlc_; }

private:
    TaskController hlc_;
    glm::vec2 targetDirection_{0.0f, 1.0f};
    float targetSpeed_ = 0.0f;
    mutable Tensor taskObs_;
};

// LocationController — navigate to a world position.
// Task obs: [local_offset_x(1), local_offset_y(1), local_offset_z(1)]
class LocationController {
public:
    LocationController() = default;

    void setHLC(TaskController hlc) { hlc_ = std::move(hlc); }

    // Set the target world position
    void setTarget(glm::vec3 worldPosition);

    // Evaluate given the character's current position and heading
    void evaluate(glm::vec3 characterPosition, float characterHeading,
                  Tensor& outLatent) const;

    // Check if the character has reached the target (within threshold)
    bool hasReached(glm::vec3 characterPosition, float threshold = 0.5f) const;

    bool isLoaded() const { return hlc_.isLoaded(); }
    TaskController& hlc() { return hlc_; }

private:
    TaskController hlc_;
    glm::vec3 targetPosition_{0.0f};
    mutable Tensor taskObs_;
};

// StrikeController — attack a target position.
// Task obs: [local_target_x(1), local_target_y(1), local_target_z(1), distance(1)]
class StrikeController {
public:
    StrikeController() = default;

    void setHLC(TaskController hlc) { hlc_ = std::move(hlc); }

    // Set the target to strike
    void setTarget(glm::vec3 targetPosition);

    // Evaluate given the character's current position and heading
    void evaluate(glm::vec3 characterPosition, float characterHeading,
                  Tensor& outLatent) const;

    // Get distance to target from a position
    float distanceToTarget(glm::vec3 characterPosition) const;

    bool isLoaded() const { return hlc_.isLoaded(); }
    TaskController& hlc() { return hlc_; }

private:
    TaskController hlc_;
    glm::vec3 targetPosition_{0.0f};
    mutable Tensor taskObs_;
};

} // namespace ml
