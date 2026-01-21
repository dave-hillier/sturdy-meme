#include "NPCPerception.h"
#include "physics/PhysicsSystem.h"
#include <algorithm>
#include <cmath>

bool NPCPerception::update(float deltaTime, const glm::vec3& npcPosition, const glm::vec3& npcForward,
                           const glm::vec3& playerPosition, const HostilityConfig& config,
                           const PhysicsWorld* physics) {
    // Calculate direction and distance to player
    glm::vec3 toPlayer = playerPosition - npcPosition;
    distanceToPlayer = glm::length(toPlayer);

    if (distanceToPlayer > 0.001f) {
        directionToPlayer = toPlayer / distanceToPlayer;
    } else {
        directionToPlayer = glm::vec3(0.0f);
    }

    // Check if player is within sight range
    bool inRange = distanceToPlayer <= config.sightRange;

    // Check field of view (dot product with forward direction)
    float dotProduct = computeFOVDot(npcForward, directionToPlayer);
    bool inFOV = dotProduct >= FOV_NORMAL;  // ~120 degree FOV

    // Check line of sight if in range and FOV
    bool hasLOS = false;
    if (inRange && inFOV) {
        // Raycast from NPC eye level to player
        glm::vec3 eyePos = npcPosition + glm::vec3(0.0f, 1.6f, 0.0f);  // Eye height
        glm::vec3 targetPos = playerPosition + glm::vec3(0.0f, 1.0f, 0.0f);  // Player center
        hasLOS = checkLineOfSight(eyePos, targetPos, physics);
    }

    canSeePlayer = inRange && inFOV && hasLOS;

    // Update awareness based on visibility
    float awarenessRate = calculateAwarenessRate(distanceToPlayer, dotProduct, canSeePlayer, config);

    if (canSeePlayer) {
        // Increase awareness when player is visible
        awareness = std::min(1.0f, awareness + awarenessRate * deltaTime);
        lastKnownPosition = playerPosition;
        hasLastKnownPosition = true;
        timeSinceLastSeen = 0.0f;
    } else {
        // Decay awareness when player is not visible
        awareness = std::max(0.0f, awareness - awarenessRate * 0.3f * deltaTime);
        timeSinceLastSeen += deltaTime;

        // Forget position after memory duration
        if (timeSinceLastSeen > config.memoryDuration) {
            hasLastKnownPosition = false;
        }
    }

    // Return true if player is detected (above threshold)
    return awareness >= config.detectionThreshold;
}

void NPCPerception::reset() {
    awareness = 0.0f;
    lastKnownPosition = glm::vec3(0.0f);
    hasLastKnownPosition = false;
    timeSinceLastSeen = 0.0f;
    canSeePlayer = false;
    distanceToPlayer = 0.0f;
    directionToPlayer = glm::vec3(0.0f);
}

bool NPCPerception::checkLineOfSight(const glm::vec3& from, const glm::vec3& to,
                                      const PhysicsWorld* physics) const {
    if (!physics) {
        return true;  // No physics = assume clear LOS
    }

    // Cast ray from NPC to player
    auto hits = physics->castRayAllHits(from, to);

    // If no hits, we have clear line of sight
    if (hits.empty()) {
        return true;
    }

    // Check if the first hit is close to the target (player)
    // Allow some tolerance for the player's collision body
    float distanceToTarget = glm::length(to - from);
    if (!hits.empty() && hits[0].distance >= distanceToTarget - 0.5f) {
        return true;  // Hit is at or beyond the target
    }

    return false;  // Something is blocking the view
}

float NPCPerception::calculateAwarenessRate(float distance, float dotProduct,
                                             bool hasLineOfSight, const HostilityConfig& config) const {
    if (!hasLineOfSight) {
        return 0.5f;  // Base decay rate
    }

    // Base awareness rate
    float rate = 1.0f;

    // Distance factor: closer = faster awareness gain
    float distanceFactor = 1.0f - (distance / config.sightRange);
    distanceFactor = std::max(0.1f, distanceFactor);
    rate *= distanceFactor;

    // FOV factor: more centered = faster awareness gain
    float fovFactor = (dotProduct + 1.0f) * 0.5f;  // Map [-1, 1] to [0, 1]
    rate *= fovFactor;

    // Scale by 2 so max rate is 2.0 (full awareness in 0.5 seconds when close and centered)
    return rate * 2.0f;
}
