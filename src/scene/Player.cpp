#include "Player.h"

Player::Player() {
    // Create the player entity in the ECS world
    playerEntity_ = world_.createPlayer(glm::vec3{0.0f}, 0.0f);
}

void Player::moveForward(float delta) {
    accumulatedForward_ += delta;
}

void Player::moveRight(float delta) {
    accumulatedRight_ += delta;
}

void Player::rotate(float yawDelta) {
    if (world_.valid(playerEntity_)) {
        auto& registry = world_.registry();
        if (registry.all_of<ecs::Transform>(playerEntity_)) {
            auto& transform = registry.get<ecs::Transform>(playerEntity_);
            transform.yaw += yawDelta;
            transform.normalizeYaw();
        }
    }
}

void Player::update(float deltaTime) {
    // Apply accumulated movement through the controller
    if (world_.valid(playerEntity_)) {
        auto& registry = world_.registry();
        if (registry.all_of<ecs::Transform, ecs::PlayerController>(playerEntity_)) {
            auto& transform = registry.get<ecs::Transform>(playerEntity_);
            auto& controller = registry.get<ecs::PlayerController>(playerEntity_);

            // Apply movement as position delta (kinematic movement)
            glm::vec3 movement{0.0f};
            movement += transform.getForward() * accumulatedForward_;
            movement += transform.getRight() * accumulatedRight_;
            transform.position += movement;

            // Clear accumulated movement
            accumulatedForward_ = 0.0f;
            accumulatedRight_ = 0.0f;
        }
    }

    // Run ECS systems (gravity, ground collision, etc.)
    world_.update(deltaTime);
}

void Player::jump() {
    world_.requestPlayerJump(playerEntity_);
}

glm::vec3 Player::getPosition() const {
    return world_.getPlayerPosition(playerEntity_);
}

float Player::getYaw() const {
    return world_.getPlayerYaw(playerEntity_);
}

bool Player::isOnGround() const {
    return world_.isPlayerOnGround(playerEntity_);
}

glm::vec3 Player::getFocusPoint() const {
    return world_.getPlayerFocusPoint(playerEntity_);
}

glm::mat4 Player::getModelMatrix() const {
    return world_.getPlayerModelMatrix(playerEntity_);
}

void Player::setPosition(const glm::vec3& pos) {
    world_.setPlayerPosition(playerEntity_, pos);
}

bool Player::isOrientationLocked() const {
    return world_.isPlayerOrientationLocked(playerEntity_);
}

void Player::setOrientationLock(bool locked) {
    if (world_.valid(playerEntity_)) {
        ecs::setOrientationLock(world_.registry(), playerEntity_, locked);
    }
}

void Player::toggleOrientationLock() {
    world_.togglePlayerOrientationLock(playerEntity_);
}

void Player::lockToCurrentOrientation() {
    if (world_.valid(playerEntity_)) {
        auto& registry = world_.registry();
        if (registry.all_of<ecs::Transform, ecs::PlayerController>(playerEntity_)) {
            auto& transform = registry.get<ecs::Transform>(playerEntity_);
            auto& controller = registry.get<ecs::PlayerController>(playerEntity_);
            controller.lockedYaw = transform.yaw;
            controller.orientationLocked = true;
        }
    }
}

float Player::getLockedYaw() const {
    return world_.getPlayerLockedYaw(playerEntity_);
}
