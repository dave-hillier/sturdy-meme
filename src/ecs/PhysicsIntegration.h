#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include "Components.h"
#include "../physics/PhysicsSystem.h"

// Physics-ECS Integration System
// Synchronizes physics simulation with ECS entity transforms
namespace PhysicsECS {

// ============================================================================
// Transform Sync - Physics to ECS
// ============================================================================

// Sync physics body transforms to ECS entities
// Call this after PhysicsWorld::update() each frame
inline void syncPhysicsToECS(entt::registry& registry, PhysicsWorld& physics) {
    auto view = registry.view<PhysicsBody, PhysicsDriven>();

    for (auto entity : view) {
        auto& body = view.get<PhysicsBody>(entity);
        if (body.id == INVALID_BODY_ID) continue;

        PhysicsBodyInfo info = physics.getBodyInfo(body.id);

        // Update Transform component
        if (registry.all_of<Transform>(entity)) {
            auto& transform = registry.get<Transform>(entity);
            transform.position = info.position;

            // Extract yaw from quaternion (Y-axis rotation)
            // Simplified - assumes mostly Y-up rotation
            glm::vec3 forward = info.rotation * glm::vec3(0, 0, 1);
            transform.yaw = glm::degrees(atan2(forward.x, forward.z));
        }

        // Update Velocity component
        if (registry.all_of<Velocity>(entity)) {
            auto& vel = registry.get<Velocity>(entity);
            vel.linear = info.linearVelocity;
        }

        // Update ModelMatrix from physics transform
        if (registry.all_of<ModelMatrix>(entity)) {
            auto& modelMatrix = registry.get<ModelMatrix>(entity);
            modelMatrix.matrix = physics.getBodyTransform(body.id);

            // Preserve scale if stored elsewhere
            if (registry.all_of<Hierarchy>(entity)) {
                auto& hierarchy = registry.get<Hierarchy>(entity);
                modelMatrix.matrix = glm::scale(modelMatrix.matrix, hierarchy.localScale);
            }
        }

        // Update WorldTransform for hierarchy system
        if (registry.all_of<WorldTransform>(entity)) {
            auto& world = registry.get<WorldTransform>(entity);
            world.position = info.position;
            world.matrix = physics.getBodyTransform(body.id);
            world.dirty = false;
        }
    }
}

// ============================================================================
// Transform Sync - ECS to Physics (for kinematic bodies)
// ============================================================================

// Sync ECS transforms to physics (for kinematic bodies)
// Use for animated objects that should affect physics but not be driven by it
inline void syncECSToPhysics(entt::registry& registry, PhysicsWorld& physics) {
    auto view = registry.view<PhysicsBody, PhysicsKinematic, Transform>();

    for (auto entity : view) {
        auto& body = view.get<PhysicsBody>(entity);
        auto& transform = view.get<Transform>(entity);

        if (body.id == INVALID_BODY_ID) continue;

        physics.setBodyPosition(body.id, transform.position);
    }
}

// ============================================================================
// Entity Factory Functions
// ============================================================================

// Create a physics-enabled box entity
inline entt::entity createPhysicsBox(
    entt::registry& registry,
    PhysicsWorld& physics,
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    float mass = 1.0f,
    const std::string& name = "PhysicsBox")
{
    auto entity = registry.create();

    // Create physics body
    PhysicsBodyID bodyId = physics.createBox(position, halfExtents, mass);

    // Add components
    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<PhysicsBody>(entity, PhysicsBody{bodyId});
    registry.emplace<PhysicsDriven>(entity);
    registry.emplace<Velocity>(entity);
    registry.emplace<ModelMatrix>(entity);
    registry.emplace<DynamicObject>(entity);

    // Add bounding info
    AABBBounds bounds;
    bounds.min = -halfExtents;
    bounds.max = halfExtents;
    registry.emplace<AABBBounds>(entity, bounds);

    // Add scene graph info
    EntityInfo info;
    info.name = name;
    info.icon = "B";  // Box icon
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a physics-enabled sphere entity
inline entt::entity createPhysicsSphere(
    entt::registry& registry,
    PhysicsWorld& physics,
    const glm::vec3& position,
    float radius,
    float mass = 1.0f,
    const std::string& name = "PhysicsSphere")
{
    auto entity = registry.create();

    // Create physics body
    PhysicsBodyID bodyId = physics.createSphere(position, radius, mass);

    // Add components
    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<PhysicsBody>(entity, PhysicsBody{bodyId});
    registry.emplace<PhysicsDriven>(entity);
    registry.emplace<Velocity>(entity);
    registry.emplace<ModelMatrix>(entity);
    registry.emplace<DynamicObject>(entity);

    // Add bounding info
    BoundingSphere sphere;
    sphere.radius = radius;
    registry.emplace<BoundingSphere>(entity, sphere);

    // Add scene graph info
    EntityInfo info;
    info.name = name;
    info.icon = "O";  // Sphere icon
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a static physics box (for obstacles)
inline entt::entity createStaticBox(
    entt::registry& registry,
    PhysicsWorld& physics,
    const glm::vec3& position,
    const glm::vec3& halfExtents,
    const std::string& name = "StaticBox")
{
    auto entity = registry.create();

    // Create physics body
    PhysicsBodyID bodyId = physics.createStaticBox(position, halfExtents);

    // Add components
    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<PhysicsBody>(entity, PhysicsBody{bodyId});
    registry.emplace<StaticObject>(entity);
    registry.emplace<ModelMatrix>(entity, ModelMatrix{glm::translate(glm::mat4(1.0f), position)});

    // Add bounding info
    AABBBounds bounds;
    bounds.min = -halfExtents;
    bounds.max = halfExtents;
    registry.emplace<AABBBounds>(entity, bounds);

    // Add scene graph info
    EntityInfo info;
    info.name = name;
    info.icon = "S";  // Static icon
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// ============================================================================
// Physics Body Management
// ============================================================================

// Attach physics body to existing entity
inline void attachPhysicsBody(
    entt::registry& registry,
    entt::entity entity,
    PhysicsWorld& physics,
    PhysicsBodyID bodyId,
    bool dynamic = true)
{
    registry.emplace_or_replace<PhysicsBody>(entity, PhysicsBody{bodyId});

    if (dynamic) {
        registry.emplace_or_replace<PhysicsDriven>(entity);
        registry.emplace_or_replace<Velocity>(entity);
        registry.emplace_or_replace<DynamicObject>(entity);
    } else {
        registry.emplace_or_replace<StaticObject>(entity);
    }

    // Ensure ModelMatrix exists for transform sync
    if (!registry.all_of<ModelMatrix>(entity)) {
        registry.emplace<ModelMatrix>(entity);
    }
}

// Remove physics body from entity (doesn't destroy the physics body)
inline void detachPhysicsBody(entt::registry& registry, entt::entity entity) {
    registry.remove<PhysicsBody>(entity);
    registry.remove<PhysicsDriven>(entity);
    registry.remove<DynamicObject>(entity);
}

// Destroy entity and its physics body
inline void destroyPhysicsEntity(
    entt::registry& registry,
    PhysicsWorld& physics,
    entt::entity entity)
{
    if (registry.all_of<PhysicsBody>(entity)) {
        auto& body = registry.get<PhysicsBody>(entity);
        if (body.id != INVALID_BODY_ID) {
            physics.removeBody(body.id);
        }
    }
    registry.destroy(entity);
}

// ============================================================================
// Physics Queries
// ============================================================================

// Get all dynamic physics entities
inline std::vector<entt::entity> getDynamicEntities(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<PhysicsBody, PhysicsDriven>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all static physics entities
inline std::vector<entt::entity> getStaticPhysicsEntities(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<PhysicsBody, StaticObject>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Find entity by physics body ID
inline entt::entity findByBodyId(entt::registry& registry, PhysicsBodyID bodyId) {
    auto view = registry.view<PhysicsBody>();
    for (auto entity : view) {
        if (view.get<PhysicsBody>(entity).id == bodyId) {
            return entity;
        }
    }
    return entt::null;
}

// ============================================================================
// Physics State Helpers
// ============================================================================

// Apply impulse to entity's physics body
inline void applyImpulse(
    entt::registry& registry,
    PhysicsWorld& physics,
    entt::entity entity,
    const glm::vec3& impulse)
{
    if (!registry.all_of<PhysicsBody>(entity)) return;

    auto& body = registry.get<PhysicsBody>(entity);
    if (body.id != INVALID_BODY_ID) {
        physics.applyImpulse(body.id, impulse);
    }
}

// Set entity velocity
inline void setVelocity(
    entt::registry& registry,
    PhysicsWorld& physics,
    entt::entity entity,
    const glm::vec3& velocity)
{
    if (!registry.all_of<PhysicsBody>(entity)) return;

    auto& body = registry.get<PhysicsBody>(entity);
    if (body.id != INVALID_BODY_ID) {
        physics.setBodyVelocity(body.id, velocity);
    }

    // Also update ECS velocity component
    if (registry.all_of<Velocity>(entity)) {
        registry.get<Velocity>(entity).linear = velocity;
    }
}

// Teleport entity to position (updates both physics and ECS)
inline void teleport(
    entt::registry& registry,
    PhysicsWorld& physics,
    entt::entity entity,
    const glm::vec3& position)
{
    // Update physics
    if (registry.all_of<PhysicsBody>(entity)) {
        auto& body = registry.get<PhysicsBody>(entity);
        if (body.id != INVALID_BODY_ID) {
            physics.setBodyPosition(body.id, position);
        }
    }

    // Update ECS transform
    if (registry.all_of<Transform>(entity)) {
        registry.get<Transform>(entity).position = position;
    }

    // Update model matrix
    if (registry.all_of<ModelMatrix>(entity)) {
        auto& mm = registry.get<ModelMatrix>(entity);
        mm.matrix[3] = glm::vec4(position, 1.0f);
    }
}

// ============================================================================
// Debug Utilities
// ============================================================================

// Count active physics bodies in ECS
inline int countPhysicsBodies(entt::registry& registry) {
    int count = 0;
    auto view = registry.view<PhysicsBody>();
    for (auto entity : view) {
        if (view.get<PhysicsBody>(entity).id != INVALID_BODY_ID) {
            count++;
        }
    }
    return count;
}

// Get physics stats
struct PhysicsStats {
    int dynamicBodies;
    int staticBodies;
    int kinematicBodies;
    int totalECSBodies;
};

inline PhysicsStats getPhysicsStats(entt::registry& registry) {
    PhysicsStats stats{0, 0, 0, 0};

    auto dynamicView = registry.view<PhysicsBody, PhysicsDriven>();
    for (auto entity : dynamicView) {
        if (dynamicView.get<PhysicsBody>(entity).id != INVALID_BODY_ID) {
            stats.dynamicBodies++;
        }
    }

    auto staticView = registry.view<PhysicsBody, StaticObject>();
    for (auto entity : staticView) {
        if (staticView.get<PhysicsBody>(entity).id != INVALID_BODY_ID) {
            stats.staticBodies++;
        }
    }

    auto kinematicView = registry.view<PhysicsBody, PhysicsKinematic>();
    for (auto entity : kinematicView) {
        if (kinematicView.get<PhysicsBody>(entity).id != INVALID_BODY_ID) {
            stats.kinematicBodies++;
        }
    }

    stats.totalECSBodies = stats.dynamicBodies + stats.staticBodies + stats.kinematicBodies;
    return stats;
}

}  // namespace PhysicsECS
