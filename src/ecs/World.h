#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include "Components.h"
#include "Systems.h"

namespace ecs {

// World class - manages the ECS registry and provides convenience methods
class World {
public:
    World() = default;
    ~World() = default;

    // Non-copyable, movable
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = default;
    World& operator=(World&&) = default;

    // Access to underlying registry
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    // Create a basic entity
    entt::entity createEntity() {
        return registry_.create();
    }

    // Destroy an entity
    void destroyEntity(entt::entity entity) {
        registry_.destroy(entity);
    }

    // Create a player entity with all necessary components
    entt::entity createPlayer(const glm::vec3& position = glm::vec3{0.0f},
                              float yaw = 0.0f) {
        auto entity = registry_.create();

        // Core components
        registry_.emplace<Transform>(entity, Transform{position, yaw});
        registry_.emplace<Velocity>(entity);
        registry_.emplace<Gravity>(entity);
        registry_.emplace<CapsuleCollider>(entity);
        registry_.emplace<PlayerController>(entity);
        registry_.emplace<CameraTarget>(entity);
        registry_.emplace<ModelMatrix>(entity);
        registry_.emplace<PlayerTag>(entity);
        registry_.emplace<Grounded>(entity);  // Start on ground

        return entity;
    }

    // Find the player entity (assumes single player)
    entt::entity findPlayer() const {
        auto view = registry_.view<PlayerTag>();
        if (view.begin() != view.end()) {
            return *view.begin();
        }
        return entt::null;
    }

    // Check if entity is valid
    bool valid(entt::entity entity) const {
        return registry_.valid(entity);
    }

    // Update all systems (call once per frame)
    void update(float deltaTime) {
        // Process jump requests first (while grounded info is valid)
        jumpSystem(registry_);

        // Apply player movement input
        movementSystem(registry_, deltaTime);

        // Apply gravity
        gravitySystem(registry_, deltaTime);

        // Handle ground collision
        groundCollisionSystem(registry_, deltaTime);

        // Update model matrices for rendering
        modelMatrixSystem(registry_);
    }

    // Player-specific convenience methods
    void setPlayerPosition(entt::entity player, const glm::vec3& pos) {
        if (registry_.valid(player) && registry_.all_of<Transform>(player)) {
            registry_.get<Transform>(player).position = pos;
        }
    }

    glm::vec3 getPlayerPosition(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<Transform>(player)) {
            return registry_.get<Transform>(player).position;
        }
        return glm::vec3{0.0f};
    }

    float getPlayerYaw(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<Transform>(player)) {
            return registry_.get<Transform>(player).yaw;
        }
        return 0.0f;
    }

    bool isPlayerOnGround(entt::entity player) const {
        return registry_.valid(player) && registry_.all_of<Grounded>(player);
    }

    glm::vec3 getPlayerFocusPoint(entt::entity player) const {
        if (registry_.valid(player) &&
            registry_.all_of<Transform, CapsuleCollider, CameraTarget>(player)) {
            auto& transform = registry_.get<Transform>(player);
            auto& capsule = registry_.get<CapsuleCollider>(player);
            auto& camera = registry_.get<CameraTarget>(player);
            return camera.getFocusPoint(transform, capsule.height);
        }
        return glm::vec3{0.0f, 1.5f, 0.0f};
    }

    glm::mat4 getPlayerModelMatrix(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<ModelMatrix>(player)) {
            return registry_.get<ModelMatrix>(player).matrix;
        }
        return glm::mat4{1.0f};
    }

    // Input handling for player
    void setPlayerMovement(entt::entity player, float forward, float right) {
        if (registry_.valid(player) && registry_.all_of<PlayerController>(player)) {
            auto& controller = registry_.get<PlayerController>(player);
            controller.moveForward = forward;
            controller.moveRight = right;
        }
    }

    void setPlayerRotation(entt::entity player, float yawDelta) {
        if (registry_.valid(player) && registry_.all_of<PlayerController>(player)) {
            registry_.get<PlayerController>(player).yawDelta = yawDelta;
        }
    }

    void requestPlayerJump(entt::entity player) {
        if (registry_.valid(player) && registry_.all_of<PlayerController>(player)) {
            registry_.get<PlayerController>(player).jumpRequested = true;
        }
    }

    void togglePlayerOrientationLock(entt::entity player) {
        ecs::toggleOrientationLock(registry_, player);
    }

    bool isPlayerOrientationLocked(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<PlayerController>(player)) {
            return registry_.get<PlayerController>(player).orientationLocked;
        }
        return false;
    }

    float getPlayerLockedYaw(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<PlayerController>(player)) {
            return registry_.get<PlayerController>(player).lockedYaw;
        }
        return 0.0f;
    }

    // Get capsule dimensions
    float getPlayerCapsuleHeight(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<CapsuleCollider>(player)) {
            return registry_.get<CapsuleCollider>(player).height;
        }
        return 1.8f;
    }

    float getPlayerCapsuleRadius(entt::entity player) const {
        if (registry_.valid(player) && registry_.all_of<CapsuleCollider>(player)) {
            return registry_.get<CapsuleCollider>(player).radius;
        }
        return 0.3f;
    }

private:
    entt::registry registry_;
};

} // namespace ecs
