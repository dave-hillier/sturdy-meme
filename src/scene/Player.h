#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "World.h"

// Player class - facade that wraps an ECS entity for backward compatibility
// Provides the same interface as before but uses ECS components internally
class Player {
public:
    // Capsule dimensions for a ~1.8m tall player (kept for external reference)
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;
    static constexpr float GROUND_LEVEL = 0.0f;
    static constexpr float GRAVITY = 9.8f;
    static constexpr float JUMP_VELOCITY = 5.0f;

    Player();
    ~Player() = default;

    // Movement relative to player's facing direction
    void moveForward(float delta);
    void moveRight(float delta);

    // Rotation
    void rotate(float yawDelta);

    // Physics update - updates the ECS world
    void update(float deltaTime);

    // Jump
    void jump();

    // Getters
    glm::vec3 getPosition() const;
    float getYaw() const;
    bool isOnGround() const;

    // Get the center point for camera focus (eye level)
    glm::vec3 getFocusPoint() const;

    // Get the model matrix for rendering
    glm::mat4 getModelMatrix() const;

    // Set position directly
    void setPosition(const glm::vec3& pos);

    // Orientation lock (strafe mode)
    bool isOrientationLocked() const;
    void setOrientationLock(bool locked);
    void toggleOrientationLock();
    void lockToCurrentOrientation();
    float getLockedYaw() const;

    // Access to the ECS world (for advanced usage)
    ecs::World& getWorld() { return world_; }
    const ecs::World& getWorld() const { return world_; }
    entt::entity getEntity() const { return playerEntity_; }

private:
    ecs::World world_;
    entt::entity playerEntity_;

    // Accumulated movement for the current frame
    float accumulatedForward_{0.0f};
    float accumulatedRight_{0.0f};
};
