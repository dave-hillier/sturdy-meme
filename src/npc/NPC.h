#pragma once

#include "HostilityState.h"
#include "NPCPerception.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

// Forward declarations
class PhysicsWorld;
class BehaviorTree;

// Unique identifier for NPCs
using NPCID = uint32_t;
constexpr NPCID INVALID_NPC_ID = 0xFFFFFFFF;

// Patrol waypoint
struct PatrolWaypoint {
    glm::vec3 position;
    float waitTime = 2.0f;  // Time to wait at this waypoint
};

// NPC transform (position and orientation)
struct NPCTransform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    // Get forward direction
    glm::vec3 forward() const {
        return rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // Get right direction
    glm::vec3 right() const {
        return rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    }

    // Create rotation to face a direction (Y-up)
    void lookAt(const glm::vec3& target) {
        glm::vec3 dir = glm::normalize(target - position);
        // Only rotate around Y axis
        float yaw = atan2(dir.x, dir.z);
        rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Smoothly rotate towards a target direction
    void smoothLookAt(const glm::vec3& target, float deltaTime, float turnSpeed = 5.0f) {
        glm::vec3 dir = target - position;
        if (glm::length(dir) < 0.001f) return;
        dir = glm::normalize(dir);

        float targetYaw = atan2(dir.x, dir.z);
        glm::quat targetRot = glm::angleAxis(targetYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        rotation = glm::slerp(rotation, targetRot, 1.0f - exp(-turnSpeed * deltaTime));
    }

    // Build a model matrix for rendering
    glm::mat4 toMatrix() const {
        glm::mat4 result = glm::mat4_cast(rotation);
        result[3] = glm::vec4(position, 1.0f);
        return result;
    }
};

// Core NPC data structure
struct NPC {
    // Identification
    NPCID id = INVALID_NPC_ID;
    std::string name;

    // Transform
    NPCTransform transform;
    glm::vec3 spawnPosition{0.0f};  // Original spawn position for returning

    // Movement
    float baseSpeed = 3.0f;         // Base movement speed (m/s)
    float currentSpeed = 0.0f;      // Current actual speed
    glm::vec3 velocity{0.0f};       // Current velocity

    // Hostility
    HostilityLevel hostility = HostilityLevel::Neutral;
    HostilityLevel baseHostility = HostilityLevel::Neutral;  // Default hostility to return to
    HostilityTrigger lastTrigger = HostilityTrigger::None;
    float hostilityTimer = 0.0f;    // Timer for hostility decay

    // Behavior
    BehaviorState behaviorState = BehaviorState::Idle;
    BehaviorState previousState = BehaviorState::Idle;
    float stateTimer = 0.0f;        // Time in current state
    float idleTimer = 0.0f;         // Timer for idle behavior variations

    // Patrol
    std::vector<PatrolWaypoint> patrolPath;
    size_t currentWaypointIndex = 0;
    float waypointWaitTimer = 0.0f;
    bool patrolForward = true;      // Direction along patrol path (for ping-pong)

    // Perception
    NPCPerception perception;

    // Combat
    float attackCooldownTimer = 0.0f;
    float health = 100.0f;
    float maxHealth = 100.0f;

    // Configuration
    HostilityConfig config;

    // Behavior tree (owned by NPC)
    std::unique_ptr<BehaviorTree> behaviorTree;

    // Visual state (for rendering feedback)
    float alertLevel = 0.0f;        // 0 = calm, 1 = fully alert (for visual indicators)
    bool isAttacking = false;       // Currently in attack animation

    // Animation state for skinned rendering
    float animationTime = 0.0f;     // Current animation time
    size_t currentAnimation = 0;    // Current animation clip index
    uint32_t boneSlot = 0;          // Slot index for bone matrices in renderer

    // Check if NPC is alive
    bool isAlive() const { return health > 0.0f; }

    // Check if NPC can attack (cooldown elapsed)
    bool canAttack() const { return attackCooldownTimer <= 0.0f; }

    // Get tint color based on hostility for visual distinction
    glm::vec4 getTintColor() const {
        switch (hostility) {
            case HostilityLevel::Friendly:
                return glm::vec4(0.7f, 1.0f, 0.7f, 1.0f);  // Light green tint
            case HostilityLevel::Neutral:
                return glm::vec4(0.9f, 0.9f, 0.7f, 1.0f);  // Light yellow tint
            case HostilityLevel::Hostile:
                return glm::vec4(1.0f, 0.6f, 0.6f, 1.0f);  // Light red tint
            case HostilityLevel::Afraid:
                return glm::vec4(0.7f, 0.7f, 1.0f, 1.0f);  // Light blue tint
            default:
                return glm::vec4(1.0f);  // No tint
        }
    }

    // Get current speed multiplier based on behavior
    float getSpeedMultiplier() const {
        switch (behaviorState) {
            case BehaviorState::Patrol: return config.patrolSpeedMultiplier;
            case BehaviorState::Chase: return config.chaseSpeedMultiplier;
            case BehaviorState::Flee: return config.fleeSpeedMultiplier;
            default: return 1.0f;
        }
    }
};

// NPC spawn configuration
struct NPCSpawnInfo {
    std::string name = "NPC";
    glm::vec3 position{0.0f};
    HostilityLevel hostility = HostilityLevel::Neutral;
    float baseSpeed = 3.0f;
    float health = 100.0f;
    HostilityConfig config;         // Optional custom config
    std::vector<PatrolWaypoint> patrolPath;  // Optional patrol path
};
