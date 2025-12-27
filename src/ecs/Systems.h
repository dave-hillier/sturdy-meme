#pragma once

#include <entt/entt.hpp>
#include "Components.h"

namespace ecs {

// Movement system - applies player input to velocity/transform
inline void movementSystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Transform, PlayerController>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& controller = view.get<PlayerController>(entity);

        // Apply movement relative to facing direction
        glm::vec3 movement{0.0f};
        movement += transform.getForward() * controller.moveForward;
        movement += transform.getRight() * controller.moveRight;

        // Movement is applied as position delta (kinematic)
        transform.position += movement;

        // Apply rotation
        transform.yaw += controller.yawDelta;
        transform.normalizeYaw();

        // Clear frame input (yawDelta and movement are set each frame)
        controller.yawDelta = 0.0f;
    }
}

// Gravity system - applies gravity to velocity
inline void gravitySystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Velocity, Gravity>();

    for (auto entity : view) {
        auto& velocity = view.get<Velocity>(entity);
        auto& gravity = view.get<Gravity>(entity);

        // Apply gravity acceleration
        velocity.linear.y -= gravity.acceleration * deltaTime;
    }
}

// Ground collision system - handles ground detection and collision response
inline void groundCollisionSystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Transform, Velocity, Gravity>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& velocity = view.get<Velocity>(entity);
        auto& gravity = view.get<Gravity>(entity);

        // Apply vertical velocity
        transform.position.y += velocity.linear.y * deltaTime;

        // Ground collision (simple flat ground for now)
        float groundLevel = gravity.groundLevel;

        if (transform.position.y <= groundLevel) {
            transform.position.y = groundLevel;
            velocity.linear.y = 0.0f;

            // Add Grounded component if not present
            if (!registry.all_of<Grounded>(entity)) {
                registry.emplace<Grounded>(entity);
            }
        } else {
            // Remove Grounded component if present
            if (registry.all_of<Grounded>(entity)) {
                registry.remove<Grounded>(entity);
            }
        }
    }
}

// Jump system - handles jump requests
inline void jumpSystem(entt::registry& registry) {
    auto view = registry.view<Velocity, PlayerController, Grounded>();

    for (auto entity : view) {
        auto& velocity = view.get<Velocity>(entity);
        auto& controller = view.get<PlayerController>(entity);

        if (controller.jumpRequested) {
            velocity.linear.y = controller.jumpVelocity;
            registry.remove<Grounded>(entity);
            controller.jumpRequested = false;
        }
    }
}

// Model matrix update system - updates cached model matrices
inline void modelMatrixSystem(entt::registry& registry) {
    auto view = registry.view<Transform, CapsuleCollider, ModelMatrix>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& capsule = view.get<CapsuleCollider>(entity);
        auto& modelMatrix = view.get<ModelMatrix>(entity);

        bool orientationLocked = false;
        float lockedYaw = 0.0f;

        if (registry.all_of<PlayerController>(entity)) {
            auto& controller = registry.get<PlayerController>(entity);
            orientationLocked = controller.orientationLocked;
            lockedYaw = controller.lockedYaw;
        }

        modelMatrix.update(transform, capsule, orientationLocked, lockedYaw);
    }
}

// Orientation lock system - handles locking player orientation
inline void setOrientationLock(entt::registry& registry, entt::entity entity, bool locked) {
    if (!registry.all_of<Transform, PlayerController>(entity)) return;

    auto& transform = registry.get<Transform>(entity);
    auto& controller = registry.get<PlayerController>(entity);

    controller.orientationLocked = locked;
    if (locked) {
        controller.lockedYaw = transform.yaw;
    }
}

inline void toggleOrientationLock(entt::registry& registry, entt::entity entity) {
    if (!registry.all_of<PlayerController>(entity)) return;
    auto& controller = registry.get<PlayerController>(entity);
    setOrientationLock(registry, entity, !controller.orientationLocked);
}

} // namespace ecs
