#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Player {
public:
    // Capsule dimensions for a ~1.8m tall player
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;

    Player();

    // Movement relative to player's facing direction
    void moveForward(float delta);
    void moveRight(float delta);

    // Rotation
    void rotate(float yawDelta);

    // Getters
    glm::vec3 getPosition() const { return position; }
    float getYaw() const { return yaw; }

    // Get the center point for camera focus (eye level)
    glm::vec3 getFocusPoint() const;

    // Get the model matrix for rendering
    glm::mat4 getModelMatrix() const;

    // Set position directly
    void setPosition(const glm::vec3& pos) { position = pos; }

private:
    glm::vec3 position;
    float yaw;  // Horizontal rotation in degrees

    // Get forward direction based on yaw
    glm::vec3 getForward() const;
    glm::vec3 getRight() const;
};
