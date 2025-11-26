#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Player {
public:
    // Capsule dimensions for a ~1.8m tall player
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;
    static constexpr float GROUND_LEVEL = 0.0f;
    static constexpr float GRAVITY = 9.8f;
    static constexpr float JUMP_VELOCITY = 5.0f;

    Player();

    // Movement relative to player's facing direction
    void moveForward(float delta);
    void moveRight(float delta);

    // Rotation
    void rotate(float yawDelta);

    // Physics update
    void update(float deltaTime);

    // Jump
    void jump();

    // Getters
    glm::vec3 getPosition() const { return position; }
    float getYaw() const { return yaw; }
    bool isOnGround() const { return onGround; }

    // Get the center point for camera focus (eye level)
    glm::vec3 getFocusPoint() const;

    // Get the model matrix for rendering
    glm::mat4 getModelMatrix() const;

    // Set position directly
    void setPosition(const glm::vec3& pos) { position = pos; }

private:
    glm::vec3 position;
    float yaw;  // Horizontal rotation in degrees
    float verticalVelocity;
    bool onGround;

    // Get forward direction based on yaw
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
};
