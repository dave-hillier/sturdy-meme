#pragma once

#include "HostilityState.h"
#include <glm/glm.hpp>
#include <cstdint>

class PhysicsWorld;

// Perception system for detecting and tracking targets
struct NPCPerception {
    // Current awareness of the player (0 = unaware, 1 = fully aware)
    float awareness = 0.0f;

    // Last known player position (valid if hasLastKnownPosition is true)
    glm::vec3 lastKnownPosition{0.0f};
    bool hasLastKnownPosition = false;

    // Time since last saw the player
    float timeSinceLastSeen = 0.0f;

    // Is the player currently visible (line of sight check passed)
    bool canSeePlayer = false;

    // Distance to player (if visible)
    float distanceToPlayer = 0.0f;

    // Direction to player (normalized, if visible)
    glm::vec3 directionToPlayer{0.0f};

    // Update perception based on NPC and player positions
    // Returns true if player is detected (awareness >= detection threshold)
    bool update(float deltaTime, const glm::vec3& npcPosition, const glm::vec3& npcForward,
                const glm::vec3& playerPosition, const HostilityConfig& config,
                const PhysicsWorld* physics);

    // Reset perception (forget about player)
    void reset();

private:
    // Check if there's line of sight to the player
    bool checkLineOfSight(const glm::vec3& from, const glm::vec3& to,
                          const PhysicsWorld* physics) const;

    // Calculate awareness gain/loss rate based on conditions
    float calculateAwarenessRate(float distance, float dotProduct,
                                 bool hasLineOfSight, const HostilityConfig& config) const;
};

// Helper to compute field of view check
inline float computeFOVDot(const glm::vec3& forward, const glm::vec3& toTarget) {
    return glm::dot(glm::normalize(forward), glm::normalize(toTarget));
}

// Standard FOV values (dot product thresholds)
constexpr float FOV_NARROW = 0.7f;   // ~90 degrees total
constexpr float FOV_NORMAL = 0.5f;   // ~120 degrees total
constexpr float FOV_WIDE = 0.0f;     // ~180 degrees total
constexpr float FOV_FULL = -1.0f;    // 360 degrees (always visible if in range)
