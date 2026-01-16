#pragma once

#include <entt/entt.hpp>
#include "Components.h"
#include "TransformHierarchy.h"

// ============================================================================
// Light Systems
// ============================================================================

// Update lights with hierarchy - no longer syncs to Transform to preserve local offsets.
// Rendering should use getLightWorldPosition() helper instead.
// This now only handles legacy LightAttachment for backward compatibility.
inline void lightAttachmentSystem(entt::registry& registry) {
    // Lights with Hierarchy: WorldTransform is already computed by hierarchy system.
    // DON'T sync to Transform - that would overwrite the local offset!
    // Rendering should use World::getLightWorldPosition() which checks WorldTransform.

    // Legacy support: Handle old LightAttachment component (deprecated)
    auto legacyView = registry.view<Transform, LightAttachment>(entt::exclude<Hierarchy>);
    for (auto entity : legacyView) {
        auto& transform = legacyView.get<Transform>(entity);
        auto& attachment = legacyView.get<LightAttachment>(entity);

        if (registry.valid(attachment.parent) &&
            registry.all_of<Transform>(attachment.parent)) {
            auto& parentTransform = registry.get<Transform>(attachment.parent);

            // Apply parent's rotation to the offset using quaternion
            glm::vec3 rotatedOffset = glm::vec3(glm::mat4_cast(parentTransform.rotation) * glm::vec4(attachment.offset, 0.0f));

            transform.position = parentTransform.position + rotatedOffset;
            transform.rotation = parentTransform.rotation;
        }
    }
}

// ============================================================================
// AI/NPC Systems
// ============================================================================

// Update AI state timers
inline void aiStateTimerSystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<AIState>();

    for (auto entity : view) {
        auto& state = view.get<AIState>(entity);
        state.stateTimer += deltaTime;
    }
}

// Patrol system - moves NPCs along their patrol paths
inline void patrolSystem(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Transform, PatrolPath, MovementSettings, AIState>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& patrol = view.get<PatrolPath>(entity);
        auto& movement = view.get<MovementSettings>(entity);
        auto& state = view.get<AIState>(entity);

        // Only patrol when in patrol state
        if (state.current != AIState::State::Patrol) continue;
        if (patrol.waypoints.empty()) continue;

        // Get current waypoint
        glm::vec3 target = patrol.waypoints[patrol.currentWaypoint];
        glm::vec3 toTarget = target - transform.position;
        toTarget.y = 0.0f;  // Only move on XZ plane

        float distance = glm::length(toTarget);

        if (distance < patrol.waypointRadius) {
            // Reached waypoint, move to next
            patrol.currentWaypoint++;
            if (patrol.currentWaypoint >= patrol.waypoints.size()) {
                if (patrol.loop) {
                    patrol.currentWaypoint = 0;
                } else {
                    patrol.currentWaypoint = patrol.waypoints.size() - 1;
                    state.current = AIState::State::Idle;
                    continue;
                }
            }
        } else {
            // Move towards waypoint
            glm::vec3 direction = glm::normalize(toTarget);

            // Calculate target yaw
            float targetYaw = glm::degrees(atan2(direction.x, direction.z));

            // Turn towards target
            float currentYaw = transform.getYaw();
            float yawDiff = targetYaw - currentYaw;
            while (yawDiff > 180.0f) yawDiff -= 360.0f;
            while (yawDiff < -180.0f) yawDiff += 360.0f;

            float maxTurn = movement.turnSpeed * deltaTime;
            float newYaw;
            if (fabs(yawDiff) <= maxTurn) {
                newYaw = targetYaw;
            } else {
                newYaw = currentYaw + (yawDiff > 0 ? 1.0f : -1.0f) * maxTurn;
            }
            transform.setYaw(newYaw);

            // Move forward
            transform.position += transform.getForward() * movement.walkSpeed * deltaTime;
        }
    }
}

// ============================================================================
// Health System
// ============================================================================

// Apply damage to an entity
inline void applyDamage(entt::registry& registry, entt::entity entity, float amount) {
    if (!registry.valid(entity) || !registry.all_of<Health>(entity)) return;

    auto& health = registry.get<Health>(entity);
    if (health.invulnerable) return;

    health.current = glm::max(0.0f, health.current - amount);
}

// Heal an entity
inline void applyHealing(entt::registry& registry, entt::entity entity, float amount) {
    if (!registry.valid(entity) || !registry.all_of<Health>(entity)) return;

    auto& health = registry.get<Health>(entity);
    health.current = glm::min(health.maximum, health.current + amount);
}

// Check if entity is dead
inline bool isDead(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Health>(entity)) return false;
    return registry.get<Health>(entity).current <= 0.0f;
}
