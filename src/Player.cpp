#include "Player.h"
#include <cmath>

Player::Player()
    : position(0.0f, 0.0f, 0.0f)
    , yaw(0.0f)
{
}

void Player::moveForward(float delta) {
    position += getForward() * delta;
}

void Player::moveRight(float delta) {
    position += getRight() * delta;
}

void Player::rotate(float yawDelta) {
    yaw += yawDelta;
    // Keep yaw in reasonable range
    while (yaw > 360.0f) yaw -= 360.0f;
    while (yaw < 0.0f) yaw += 360.0f;
}

glm::vec3 Player::getFocusPoint() const {
    // Camera focus at roughly eye level (top of capsule minus a bit)
    return position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.85f, 0.0f);
}

glm::mat4 Player::getModelMatrix() const {
    glm::mat4 model = glm::mat4(1.0f);
    // Translate to position (capsule center is at ground level, so offset up by half height)
    model = glm::translate(model, position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.5f, 0.0f));
    // Rotate around Y axis based on yaw
    model = glm::rotate(model, glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    return model;
}

glm::vec3 Player::getForward() const {
    float radYaw = glm::radians(yaw);
    return glm::vec3(sin(radYaw), 0.0f, cos(radYaw));
}

glm::vec3 Player::getRight() const {
    float radYaw = glm::radians(yaw + 90.0f);
    return glm::vec3(sin(radYaw), 0.0f, cos(radYaw));
}
