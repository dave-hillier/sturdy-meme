#include "Camera.h"
#include <algorithm>

Camera::Camera()
    : position(0.0f, 1.0f, 5.0f)
    , worldUp(0.0f, 1.0f, 0.0f)
    , yaw(-90.0f)
    , pitch(0.0f)
    , fov(45.0f)
    , aspectRatio(16.0f / 9.0f)
    , nearPlane(0.1f)
    , farPlane(5000.0f)
    , thirdPersonTarget(0.0f, 1.5f, 0.0f)
    , thirdPersonDistance(5.0f)
    , thirdPersonMinDistance(2.0f)
    , thirdPersonMaxDistance(15.0f)
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
    yaw += delta;
    updateVectors();
}

void Camera::orbitPitch(float delta) {
    pitch += delta;
    // Clamp pitch to avoid flipping (more restricted for third-person)
    pitch = std::clamp(pitch, -60.0f, 60.0f);
    updateVectors();
}

void Camera::adjustDistance(float delta) {
    thirdPersonDistance = std::clamp(thirdPersonDistance + delta, thirdPersonMinDistance, thirdPersonMaxDistance);
}

void Camera::setDistance(float dist) {
    thirdPersonDistance = std::clamp(dist, thirdPersonMinDistance, thirdPersonMaxDistance);
}

void Camera::updateThirdPerson() {
    // Calculate camera position based on spherical coordinates around target
    // Camera is behind and above the target, looking at the target
    float horizontalDist = thirdPersonDistance * cos(glm::radians(pitch));
    float verticalOffset = thirdPersonDistance * sin(glm::radians(pitch));

    // Position camera behind the target based on yaw
    position.x = thirdPersonTarget.x - horizontalDist * cos(glm::radians(yaw));
    position.y = thirdPersonTarget.y + verticalOffset;
    position.z = thirdPersonTarget.z - horizontalDist * sin(glm::radians(yaw));

    // Update front vector to look at target
    front = glm::normalize(thirdPersonTarget - position);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}
