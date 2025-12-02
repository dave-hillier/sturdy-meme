#include "Camera.h"
#include <algorithm>
#include <cmath>

Camera::Camera()
    : position(0.0f, 1.0f, 5.0f)
    , worldUp(0.0f, 1.0f, 0.0f)
    , yaw(-90.0f)
    , pitch(0.0f)
    , fov(45.0f)
    , aspectRatio(16.0f / 9.0f)
    , nearPlane(0.1f)
    , farPlane(1000.0f)
    , thirdPersonTarget(0.0f, 1.5f, 0.0f)
    , thirdPersonDistance(5.0f)
    , thirdPersonMinDistance(2.0f)
    , thirdPersonMaxDistance(15.0f)
    , smoothedTarget(0.0f, 1.5f, 0.0f)
    , smoothedYaw(-90.0f)
    , smoothedPitch(0.0f)
    , smoothedDistance(5.0f)
    , targetYaw(-90.0f)
    , targetPitch(0.0f)
    , targetDistance(5.0f)
    , baseFov(45.0f)
    , currentFov(45.0f)
    , targetFov(45.0f)
{
    updateVectors();
}

void Camera::setAspectRatio(float aspect) {
    aspectRatio = aspect;
}

void Camera::moveForward(float delta) {
    position += front * delta;
}

void Camera::moveRight(float delta) {
    position += right * delta;
}

void Camera::moveUp(float delta) {
    position += worldUp * delta;
}

void Camera::rotatePitch(float delta) {
    pitch += delta;
    pitch = std::clamp(pitch, -89.0f, 89.0f);
    updateVectors();
}

void Camera::rotateYaw(float delta) {
    yaw += delta;
    updateVectors();
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::getProjectionMatrix() const {
    glm::mat4 proj = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    proj[1][1] *= -1;
    return proj;
}

void Camera::updateVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    newFront.y = sin(glm::radians(pitch));
    newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(newFront);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

void Camera::setThirdPersonTarget(const glm::vec3& target) {
    thirdPersonTarget = target;
}

void Camera::orbitYaw(float delta) {
    targetYaw += delta;
}

void Camera::orbitPitch(float delta) {
    targetPitch += delta;
    // Clamp pitch to avoid flipping (more restricted for third-person)
    targetPitch = std::clamp(targetPitch, -60.0f, 60.0f);
}

void Camera::adjustDistance(float delta) {
    targetDistance = std::clamp(targetDistance + delta, thirdPersonMinDistance, thirdPersonMaxDistance);
}

void Camera::setDistance(float dist) {
    targetDistance = std::clamp(dist, thirdPersonMinDistance, thirdPersonMaxDistance);
}

void Camera::updateThirdPerson(float deltaTime) {
    // Exponential smoothing formula: smoothed += (target - smoothed) * (1 - exp(-speed * deltaTime))
    float positionFactor = 1.0f - std::exp(-positionSmoothSpeed * deltaTime);
    float rotationFactor = 1.0f - std::exp(-rotationSmoothSpeed * deltaTime);
    float distanceFactor = 1.0f - std::exp(-distanceSmoothSpeed * deltaTime);
    float fovFactor = 1.0f - std::exp(-fovSmoothSpeed * deltaTime);

    // Interpolate smoothed values toward targets
    smoothedTarget += (thirdPersonTarget - smoothedTarget) * positionFactor;
    smoothedDistance += (targetDistance - smoothedDistance) * distanceFactor;

    // Handle yaw wrapping for smooth interpolation
    float yawDiff = targetYaw - smoothedYaw;
    while (yawDiff > 180.0f) yawDiff -= 360.0f;
    while (yawDiff < -180.0f) yawDiff += 360.0f;
    smoothedYaw += yawDiff * rotationFactor;

    smoothedPitch += (targetPitch - smoothedPitch) * rotationFactor;

    // Update FOV
    currentFov += (targetFov - currentFov) * fovFactor;
    fov = currentFov;

    // Update the actual yaw/pitch for getYaw() to work correctly
    yaw = smoothedYaw;
    pitch = smoothedPitch;
    thirdPersonDistance = smoothedDistance;

    // Calculate camera position based on smoothed spherical coordinates around target
    float horizontalDist = smoothedDistance * cos(glm::radians(smoothedPitch));
    float verticalOffset = smoothedDistance * sin(glm::radians(smoothedPitch));

    // Position camera behind the target based on smoothed yaw
    position.x = smoothedTarget.x - horizontalDist * cos(glm::radians(smoothedYaw));
    position.y = smoothedTarget.y + verticalOffset;
    position.z = smoothedTarget.z - horizontalDist * sin(glm::radians(smoothedYaw));

    // Update front vector to look at target
    front = glm::normalize(smoothedTarget - position);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

void Camera::resetSmoothing() {
    // Snap smoothed values to current targets
    smoothedTarget = thirdPersonTarget;
    smoothedYaw = targetYaw;
    smoothedPitch = targetPitch;
    smoothedDistance = targetDistance;
    currentFov = targetFov;
}

void Camera::setTargetFov(float newFov) {
    targetFov = newFov;
}
