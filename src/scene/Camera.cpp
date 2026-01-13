#include "Camera.h"
#include <algorithm>
#include <cmath>

Camera::Camera()
    : node_("Camera")
    , position_(0.0f, 1.5f, 5.0f)
    , worldUp_(0.0f, 1.0f, 0.0f)
    , yaw_(-90.0f)
    , pitch_(0.0f)
    , fov_(45.0f)
    , aspectRatio_(16.0f / 9.0f)
    , nearPlane_(0.1f)
    , farPlane_(50000.0f)
    , thirdPersonTarget_(0.0f, 1.5f, 0.0f)
    , thirdPersonDistance_(3.0f)
    , thirdPersonMinDistance_(1.0f)
    , thirdPersonMaxDistance_(10.0f)
    , smoothedTarget_(0.0f, 1.5f, 0.0f)
    , smoothedYaw_(-90.0f)
    , smoothedPitch_(0.0f)
    , smoothedDistance_(3.0f)
    , targetYaw_(-90.0f)
    , targetPitch_(0.0f)
    , targetDistance_(3.0f)
    , baseFov_(45.0f)
    , currentFov_(45.0f)
    , targetFov_(45.0f)
{
    updateVectors();
    syncNodeTransform();
}

void Camera::setAspectRatio(float aspect) {
    aspectRatio_ = aspect;
}

void Camera::setPosition(const glm::vec3& pos) {
    position_ = pos;
    syncNodeTransform();
}

void Camera::setYaw(float newYaw) {
    yaw_ = newYaw;
    targetYaw_ = newYaw;
    smoothedYaw_ = newYaw;
    updateVectors();
    syncNodeTransform();
}

void Camera::setPitch(float newPitch) {
    pitch_ = std::clamp(newPitch, -89.0f, 89.0f);
    targetPitch_ = pitch_;
    smoothedPitch_ = pitch_;
    updateVectors();
    syncNodeTransform();
}

void Camera::setRotation(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -89.0f, 89.0f);
    targetYaw_ = yaw_;
    targetPitch_ = pitch_;
    smoothedYaw_ = yaw_;
    smoothedPitch_ = pitch_;
    updateVectors();
    syncNodeTransform();
}

void Camera::moveForward(float delta) {
    position_ += front_ * delta;
    syncNodeTransform();
}

void Camera::moveRight(float delta) {
    position_ += right_ * delta;
    syncNodeTransform();
}

void Camera::moveUp(float delta) {
    position_ += worldUp_ * delta;
    syncNodeTransform();
}

void Camera::rotatePitch(float delta) {
    pitch_ += delta;
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);
    updateVectors();
    syncNodeTransform();
}

void Camera::rotateYaw(float delta) {
    yaw_ += delta;
    updateVectors();
    syncNodeTransform();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position_, position_ + front_, up_);
}

glm::mat4 Camera::getProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(glm::radians(fov_), aspectRatio_, nearPlane_, farPlane_);
    proj[1][1] *= -1;
    return proj;
}

glm::quat Camera::getRotation() const {
    // Convert yaw/pitch to quaternion
    glm::quat qYaw = glm::angleAxis(glm::radians(yaw_), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat qPitch = glm::angleAxis(glm::radians(pitch_), glm::vec3(1.0f, 0.0f, 0.0f));
    return qYaw * qPitch;
}

void Camera::updateVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    newFront.y = sin(glm::radians(pitch_));
    newFront.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
    front_ = glm::normalize(newFront);
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));
}

void Camera::syncNodeTransform() {
    node_.setPosition(position_);
    node_.setRotation(getRotation());
}

void Camera::setThirdPersonTarget(const glm::vec3& target) {
    thirdPersonTarget_ = target;
}

void Camera::setThirdPersonTargetNode(SceneNode* targetNode) {
    thirdPersonTargetNode_ = targetNode;
}

void Camera::orbitYaw(float delta) {
    targetYaw_ += delta;
}

void Camera::orbitPitch(float delta) {
    targetPitch_ += delta;
    // Clamp pitch to avoid flipping (more restricted for third-person)
    targetPitch_ = std::clamp(targetPitch_, -60.0f, 60.0f);
}

void Camera::adjustDistance(float delta) {
    targetDistance_ = std::clamp(targetDistance_ + delta, thirdPersonMinDistance_, thirdPersonMaxDistance_);
}

void Camera::setDistance(float dist) {
    targetDistance_ = std::clamp(dist, thirdPersonMinDistance_, thirdPersonMaxDistance_);
}

void Camera::updateThirdPerson(float deltaTime) {
    // Reset collision adjustment - will be set by applyCollisionDistance if needed
    collisionAdjustedDistance_ = -1.0f;

    // If following a scene node, get its world position
    if (thirdPersonTargetNode_) {
        thirdPersonTarget_ = thirdPersonTargetNode_->getWorldPosition();
    }

    // Exponential smoothing formula: smoothed += (target - smoothed) * (1 - exp(-speed * deltaTime))
    float positionFactor = 1.0f - std::exp(-kPositionSmoothSpeed * deltaTime);
    float rotationFactor = 1.0f - std::exp(-kRotationSmoothSpeed * deltaTime);
    float distanceFactor = 1.0f - std::exp(-kDistanceSmoothSpeed * deltaTime);
    float fovFactor = 1.0f - std::exp(-kFovSmoothSpeed * deltaTime);

    // Interpolate smoothed values toward targets
    smoothedTarget_ += (thirdPersonTarget_ - smoothedTarget_) * positionFactor;
    smoothedDistance_ += (targetDistance_ - smoothedDistance_) * distanceFactor;

    // Handle yaw wrapping for smooth interpolation
    float yawDiff = targetYaw_ - smoothedYaw_;
    while (yawDiff > 180.0f) yawDiff -= 360.0f;
    while (yawDiff < -180.0f) yawDiff += 360.0f;
    smoothedYaw_ += yawDiff * rotationFactor;

    smoothedPitch_ += (targetPitch_ - smoothedPitch_) * rotationFactor;

    // Update FOV
    currentFov_ += (targetFov_ - currentFov_) * fovFactor;
    fov_ = currentFov_;

    // Update the actual yaw/pitch for getYaw() to work correctly
    yaw_ = smoothedYaw_;
    pitch_ = smoothedPitch_;
    thirdPersonDistance_ = smoothedDistance_;

    // Use collision-adjusted distance if set, otherwise use smoothed distance
    float effectiveDistance = smoothedDistance_;

    // Calculate camera position based on smoothed spherical coordinates around target
    float horizontalDist = effectiveDistance * cos(glm::radians(smoothedPitch_));
    float verticalOffset = effectiveDistance * sin(glm::radians(smoothedPitch_));

    // Position camera behind the target based on smoothed yaw
    position_.x = smoothedTarget_.x - horizontalDist * cos(glm::radians(smoothedYaw_));
    position_.y = smoothedTarget_.y + verticalOffset;
    position_.z = smoothedTarget_.z - horizontalDist * sin(glm::radians(smoothedYaw_));

    // Update front vector to look at target
    front_ = glm::normalize(smoothedTarget_ - position_);
    right_ = glm::normalize(glm::cross(front_, worldUp_));
    up_ = glm::normalize(glm::cross(right_, front_));

    syncNodeTransform();
}

void Camera::applyCollisionDistance(float distance) {
    // Apply collision adjustment - pull camera closer to avoid clipping
    if (distance > 0.0f && distance < smoothedDistance_) {
        collisionAdjustedDistance_ = distance;

        // Recalculate position with adjusted distance
        float effectiveDistance = std::max(thirdPersonMinDistance_, distance - 0.2f);  // Small offset to avoid clipping
        float horizontalDist = effectiveDistance * cos(glm::radians(smoothedPitch_));
        float verticalOffset = effectiveDistance * sin(glm::radians(smoothedPitch_));

        position_.x = smoothedTarget_.x - horizontalDist * cos(glm::radians(smoothedYaw_));
        position_.y = smoothedTarget_.y + verticalOffset;
        position_.z = smoothedTarget_.z - horizontalDist * sin(glm::radians(smoothedYaw_));

        // Update front vector
        front_ = glm::normalize(smoothedTarget_ - position_);
        right_ = glm::normalize(glm::cross(front_, worldUp_));
        up_ = glm::normalize(glm::cross(right_, front_));

        syncNodeTransform();
    }
}

void Camera::resetSmoothing() {
    // Snap smoothed values to current targets
    smoothedTarget_ = thirdPersonTarget_;
    smoothedYaw_ = targetYaw_;
    smoothedPitch_ = targetPitch_;
    smoothedDistance_ = targetDistance_;
    currentFov_ = targetFov_;
}

void Camera::setTargetFov(float newFov) {
    targetFov_ = newFov;
}

void Camera::initializeThirdPersonFromCurrentPosition(const glm::vec3& target) {
    // Set the target position
    thirdPersonTarget_ = target;
    smoothedTarget_ = target;

    // Calculate camera offset from target
    glm::vec3 offset = position_ - target;
    float distance = glm::length(offset);

    // Clamp distance to valid range
    distance = std::clamp(distance, thirdPersonMinDistance_, thirdPersonMaxDistance_);

    // Calculate yaw from horizontal offset (atan2 of x,z gives angle)
    // Camera is positioned at target - horizontalDist * (cos(yaw), 0, sin(yaw))
    // So offset = -horizontalDist * (cos(yaw), 0, sin(yaw)) + (0, verticalOffset, 0)
    // Therefore: yaw = atan2(-offset.z, -offset.x)
    float calculatedYaw = glm::degrees(atan2(-offset.z, -offset.x));

    // Calculate pitch from vertical offset
    // verticalOffset = distance * sin(pitch)
    // horizontalDist = distance * cos(pitch)
    float horizontalDist = glm::length(glm::vec2(offset.x, offset.z));
    float calculatedPitch = glm::degrees(atan2(offset.y, horizontalDist));

    // Clamp pitch to valid range
    calculatedPitch = std::clamp(calculatedPitch, -60.0f, 60.0f);

    // Set both target and smoothed values to calculated values for smooth transition
    targetYaw_ = calculatedYaw;
    smoothedYaw_ = calculatedYaw;
    targetPitch_ = calculatedPitch;
    smoothedPitch_ = calculatedPitch;
    targetDistance_ = distance;
    smoothedDistance_ = distance;

    // Also update the base yaw/pitch so getYaw() returns correct value
    yaw_ = calculatedYaw;
    pitch_ = calculatedPitch;
    thirdPersonDistance_ = distance;

    syncNodeTransform();
}
