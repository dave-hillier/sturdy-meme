#include "TaskController.h"
#include <cmath>
#include <cassert>

namespace ml {

// --- TaskController ---

void TaskController::setNetwork(MLPNetwork network) {
    network_ = std::move(network);
}

void TaskController::evaluate(const Tensor& taskObs, Tensor& outLatent) const {
    assert(network_.numLayers() > 0);
    network_.forward(taskObs, output_);

    // L2 normalize to place on the unit hypersphere
    outLatent = output_;
    Tensor::l2Normalize(outLatent);
}

int TaskController::taskObsDim() const {
    return network_.inputSize();
}

int TaskController::latentDim() const {
    return network_.outputSize();
}

// --- HeadingController ---

void HeadingController::setTarget(glm::vec2 direction, float speed) {
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len > 1e-6f) {
        targetDirection_ = direction / len;
    }
    targetSpeed_ = speed;
}

void HeadingController::evaluate(float characterHeading, Tensor& outLatent) const {
    // Rotate target direction into the character's local frame
    float cosH = std::cos(-characterHeading);
    float sinH = std::sin(-characterHeading);

    float localX = targetDirection_.x * cosH - targetDirection_.y * sinH;
    float localZ = targetDirection_.x * sinH + targetDirection_.y * cosH;

    // Build task observation: [local_dir_x, local_dir_z, target_speed]
    taskObs_ = Tensor(3);
    taskObs_[0] = localX;
    taskObs_[1] = localZ;
    taskObs_[2] = targetSpeed_;

    hlc_.evaluate(taskObs_, outLatent);
}

// --- LocationController ---

void LocationController::setTarget(glm::vec3 worldPosition) {
    targetPosition_ = worldPosition;
}

void LocationController::evaluate(glm::vec3 characterPosition, float characterHeading,
                                   Tensor& outLatent) const {
    // Compute world-space offset
    glm::vec3 offset = targetPosition_ - characterPosition;

    // Rotate into character's local frame
    float cosH = std::cos(-characterHeading);
    float sinH = std::sin(-characterHeading);

    float localX = offset.x * cosH - offset.z * sinH;
    float localZ = offset.x * sinH + offset.z * cosH;

    // Build task observation: [local_offset_x, local_offset_y, local_offset_z]
    taskObs_ = Tensor(3);
    taskObs_[0] = localX;
    taskObs_[1] = offset.y;
    taskObs_[2] = localZ;

    hlc_.evaluate(taskObs_, outLatent);
}

bool LocationController::hasReached(glm::vec3 characterPosition, float threshold) const {
    glm::vec3 offset = targetPosition_ - characterPosition;
    float dist = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
    return dist < threshold;
}

// --- StrikeController ---

void StrikeController::setTarget(glm::vec3 targetPosition) {
    targetPosition_ = targetPosition;
}

void StrikeController::evaluate(glm::vec3 characterPosition, float characterHeading,
                                 Tensor& outLatent) const {
    glm::vec3 offset = targetPosition_ - characterPosition;
    float dist = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);

    // Rotate into character's local frame
    float cosH = std::cos(-characterHeading);
    float sinH = std::sin(-characterHeading);

    float localX = offset.x * cosH - offset.z * sinH;
    float localZ = offset.x * sinH + offset.z * cosH;

    // Build task observation: [local_target_x, local_target_y, local_target_z, distance]
    taskObs_ = Tensor(4);
    taskObs_[0] = localX;
    taskObs_[1] = offset.y;
    taskObs_[2] = localZ;
    taskObs_[3] = dist;

    hlc_.evaluate(taskObs_, outLatent);
}

float StrikeController::distanceToTarget(glm::vec3 characterPosition) const {
    glm::vec3 offset = targetPosition_ - characterPosition;
    return std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
}

} // namespace ml
