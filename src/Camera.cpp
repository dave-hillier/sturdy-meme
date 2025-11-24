#include "Camera.h"
#include <algorithm>

Camera::Camera()
    : position(0.0f, 0.0f, 5.0f)
    , worldUp(0.0f, 1.0f, 0.0f)
    , yaw(-90.0f)
    , pitch(0.0f)
    , fov(45.0f)
    , aspectRatio(16.0f / 9.0f)
    , nearPlane(0.1f)
    , farPlane(100.0f)
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
