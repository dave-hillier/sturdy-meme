#include "MotionMatchingTrajectory.h"
#include <algorithm>
#include <cmath>

namespace MotionMatching {

// TrajectoryPredictor implementation

void TrajectoryPredictor::update(const glm::vec3& position,
                                  const glm::vec3& facing,
                                  const glm::vec3& inputDirection,
                                  float inputMagnitude,
                                  float deltaTime) {
    currentTime_ += deltaTime;
    currentPosition_ = position;

    // Update facing direction
    if (glm::length(facing) > 0.01f) {
        currentFacing_ = glm::normalize(glm::vec3(facing.x, 0.0f, facing.z));
    }

    // Smooth input direction
    glm::vec3 targetInput = inputDirection * inputMagnitude;
    float smoothFactor = 1.0f - std::exp(-deltaTime / std::max(0.001f, config_.inputSmoothing));
    smoothedInput_ = glm::mix(smoothedInput_, targetInput, smoothFactor);

    // Calculate target velocity
    glm::vec3 targetVelocity = smoothedInput_ * config_.maxSpeed;

    // Accelerate/decelerate towards target
    glm::vec3 velocityDiff = targetVelocity - currentVelocity_;
    float velocityDiffLen = glm::length(velocityDiff);

    if (velocityDiffLen > 0.001f) {
        // Use acceleration if speeding up, deceleration if slowing down
        float currentSpeed = glm::length(currentVelocity_);
        float targetSpeed = glm::length(targetVelocity);
        float rate = targetSpeed > currentSpeed ? config_.acceleration : config_.deceleration;

        float maxChange = rate * deltaTime;
        if (velocityDiffLen <= maxChange) {
            currentVelocity_ = targetVelocity;
        } else {
            currentVelocity_ += (velocityDiff / velocityDiffLen) * maxChange;
        }
    }

    // Store in history
    TrajectoryHistory entry;
    entry.position = currentPosition_;
    entry.velocity = currentVelocity_;
    entry.facing = currentFacing_;
    entry.timestamp = currentTime_;
    history_.push_back(entry);

    // Prune old history
    pruneHistory();
}

Trajectory TrajectoryPredictor::generateTrajectory() const {
    Trajectory trajectory;

    for (float timeOffset : config_.sampleTimes) {
        TrajectorySample sample;

        if (timeOffset <= 0.0f) {
            // Past: use history
            sample = getHistorySample(timeOffset);
        } else {
            // Future: predict
            sample = predictFuture(timeOffset);
        }

        sample.timeOffset = timeOffset;
        trajectory.addSample(sample);
    }

    return trajectory;
}

TrajectorySample TrajectoryPredictor::predictFuture(float timeOffset) const {
    TrajectorySample sample;

    // Simple physics-based prediction
    // Assumes constant acceleration towards smoothed input velocity

    glm::vec3 targetVelocity = smoothedInput_ * config_.maxSpeed;
    glm::vec3 velocityDiff = targetVelocity - currentVelocity_;
    float velocityDiffLen = glm::length(velocityDiff);

    float timeToTarget = 0.0f;
    if (velocityDiffLen > 0.001f) {
        float rate = glm::length(targetVelocity) > glm::length(currentVelocity_) ?
                     config_.acceleration : config_.deceleration;
        timeToTarget = velocityDiffLen / rate;
    }

    glm::vec3 predictedVelocity;
    glm::vec3 predictedPosition = currentPosition_;

    if (timeOffset <= timeToTarget && timeToTarget > 0.0f) {
        // Still accelerating
        float t = timeOffset / timeToTarget;
        predictedVelocity = glm::mix(currentVelocity_, targetVelocity, t);

        // Position uses average velocity
        glm::vec3 avgVelocity = (currentVelocity_ + predictedVelocity) * 0.5f;
        predictedPosition += avgVelocity * timeOffset;
    } else {
        // At target velocity
        predictedVelocity = targetVelocity;

        // Position: accelerate for timeToTarget, then constant velocity
        if (timeToTarget > 0.0f) {
            glm::vec3 avgVelocity = (currentVelocity_ + targetVelocity) * 0.5f;
            predictedPosition += avgVelocity * timeToTarget;
            predictedPosition += targetVelocity * (timeOffset - timeToTarget);
        } else {
            predictedPosition += targetVelocity * timeOffset;
        }
    }

    // Predict facing direction
    glm::vec3 predictedFacing = currentFacing_;
    if (glm::length(smoothedInput_) > 0.1f) {
        // Turn towards movement direction
        glm::vec3 targetFacing = glm::normalize(glm::vec3(smoothedInput_.x, 0.0f, smoothedInput_.z));
        float turnAngle = glm::radians(config_.turnSpeed) * timeOffset;

        // Calculate angle between current and target facing
        float dotProduct = glm::clamp(glm::dot(currentFacing_, targetFacing), -1.0f, 1.0f);
        float angleDiff = std::acos(dotProduct);

        if (angleDiff > 0.01f) {
            float turnProgress = std::min(1.0f, turnAngle / angleDiff);
            // Use SLERP-like interpolation on ground plane
            float cross = currentFacing_.x * targetFacing.z - currentFacing_.z * targetFacing.x;
            float sign = cross >= 0.0f ? 1.0f : -1.0f;
            float actualTurn = sign * angleDiff * turnProgress;

            float cosA = std::cos(actualTurn);
            float sinA = std::sin(actualTurn);
            predictedFacing = glm::vec3(
                currentFacing_.x * cosA - currentFacing_.z * sinA,
                0.0f,
                currentFacing_.x * sinA + currentFacing_.z * cosA
            );
        }
    }

    // Convert to local space (relative to current position/facing)
    sample.position = predictedPosition - currentPosition_;
    sample.velocity = predictedVelocity;
    sample.facing = predictedFacing;

    return sample;
}

TrajectorySample TrajectoryPredictor::getHistorySample(float timeOffset) const {
    TrajectorySample sample;

    if (history_.empty()) {
        sample.position = glm::vec3(0.0f);
        sample.velocity = currentVelocity_;
        sample.facing = currentFacing_;
        return sample;
    }

    float targetTime = currentTime_ + timeOffset; // timeOffset is negative

    // Find bracketing history entries
    const TrajectoryHistory* before = nullptr;
    const TrajectoryHistory* after = nullptr;

    for (size_t i = 0; i < history_.size(); ++i) {
        if (history_[i].timestamp <= targetTime) {
            before = &history_[i];
        }
        if (history_[i].timestamp >= targetTime && after == nullptr) {
            after = &history_[i];
        }
    }

    if (before == nullptr && after != nullptr) {
        // Before start of history
        sample.position = after->position - currentPosition_;
        sample.velocity = after->velocity;
        sample.facing = after->facing;
    } else if (before != nullptr && after == nullptr) {
        // After end of history (shouldn't happen)
        sample.position = before->position - currentPosition_;
        sample.velocity = before->velocity;
        sample.facing = before->facing;
    } else if (before != nullptr && after != nullptr) {
        // Interpolate
        float t = 0.0f;
        float timeDiff = after->timestamp - before->timestamp;
        if (timeDiff > 0.001f) {
            t = (targetTime - before->timestamp) / timeDiff;
        }

        glm::vec3 interpolatedPos = glm::mix(before->position, after->position, t);
        sample.position = interpolatedPos - currentPosition_;
        sample.velocity = glm::mix(before->velocity, after->velocity, t);
        sample.facing = glm::normalize(glm::mix(before->facing, after->facing, t));
    } else {
        // No history
        sample.position = glm::vec3(0.0f);
        sample.velocity = currentVelocity_;
        sample.facing = currentFacing_;
    }

    return sample;
}

void TrajectoryPredictor::pruneHistory() {
    float cutoffTime = currentTime_ - config_.historyDuration;
    while (!history_.empty() && history_.front().timestamp < cutoffTime) {
        history_.pop_front();
    }
}

void TrajectoryPredictor::reset() {
    history_.clear();
    currentVelocity_ = glm::vec3(0.0f);
    smoothedInput_ = glm::vec3(0.0f);
    currentTime_ = 0.0f;
}

// InertialBlender implementation

void InertialBlender::startBlend(const glm::vec3& currentPosition,
                                   const glm::vec3& currentVelocity,
                                   const glm::vec3& targetPosition,
                                   const glm::vec3& targetVelocity) {
    // Calculate initial offset between current and target
    springPosition_ = currentPosition - targetPosition;
    springVelocity_ = currentVelocity - targetVelocity;

    positionOffset_ = springPosition_;
    velocityOffset_ = springVelocity_;
    blendTime_ = 0.0f;
}

void InertialBlender::update(float deltaTime) {
    if (!isBlending()) {
        positionOffset_ = glm::vec3(0.0f);
        velocityOffset_ = glm::vec3(0.0f);
        return;
    }

    blendTime_ += deltaTime;

    // Critically damped spring for smooth decay
    // Using the analytical solution for critically damped spring
    float omega = config_.naturalFrequency;
    float t = blendTime_;

    // For critically damped: x(t) = (A + Bt)e^(-omega*t)
    float decay = std::exp(-omega * t);
    float tDecay = t * decay;

    // Initial conditions determine A and B
    // A = x0, B = v0 + omega*x0

    for (int i = 0; i < 3; ++i) {
        float x0 = springPosition_[i];
        float v0 = springVelocity_[i];
        float A = x0;
        float B = v0 + omega * x0;

        positionOffset_[i] = (A + B * t) * decay;
        velocityOffset_[i] = (B - omega * (A + B * t)) * decay;
    }

    // The critically damped spring naturally decays to zero
    // No additional blend curve needed - it would cause double-attenuation
}

void InertialBlender::reset() {
    positionOffset_ = glm::vec3(0.0f);
    velocityOffset_ = glm::vec3(0.0f);
    springPosition_ = glm::vec3(0.0f);
    springVelocity_ = glm::vec3(0.0f);
    blendTime_ = config_.blendDuration; // Mark as complete
}

// RootMotionExtractor implementation

void RootMotionExtractor::update(const glm::vec3& rootPosition,
                                   const glm::quat& rootRotation,
                                   float deltaTime) {
    if (!hasReference_) {
        setReference(rootPosition, rootRotation);
        return;
    }

    // Calculate delta translation
    if (config_.extractTranslation) {
        glm::vec3 rawDelta = rootPosition - prevRootPosition_;
        // Keep only horizontal translation
        deltaTranslation_ = glm::vec3(rawDelta.x, 0.0f, rawDelta.z) * config_.translationScale;
    } else {
        deltaTranslation_ = glm::vec3(0.0f);
    }

    // Calculate delta rotation (Y axis only)
    if (config_.extractRotation) {
        // Extract Y rotation from quaternion difference
        glm::quat deltaRot = rootRotation * glm::inverse(prevRootRotation_);

        // Convert to axis-angle
        float angle = 2.0f * std::acos(std::clamp(deltaRot.w, -1.0f, 1.0f));
        if (std::abs(angle) > 0.001f && std::abs(deltaRot.y) > 0.001f) {
            // Check if rotation is around Y axis
            float yComponent = deltaRot.y / std::sin(angle * 0.5f);
            deltaRotation_ = angle * yComponent * config_.rotationScale;
        } else {
            deltaRotation_ = 0.0f;
        }
    } else {
        deltaRotation_ = 0.0f;
    }

    // Update reference for next frame
    prevRootPosition_ = rootPosition;
    prevRootRotation_ = rootRotation;
}

void RootMotionExtractor::reset() {
    hasReference_ = false;
    deltaTranslation_ = glm::vec3(0.0f);
    deltaRotation_ = 0.0f;
}

void RootMotionExtractor::setReference(const glm::vec3& rootPosition,
                                         const glm::quat& rootRotation) {
    prevRootPosition_ = rootPosition;
    prevRootRotation_ = rootRotation;
    hasReference_ = true;
    deltaTranslation_ = glm::vec3(0.0f);
    deltaRotation_ = 0.0f;
}

} // namespace MotionMatching
