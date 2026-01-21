#pragma once

#include "NPC.h"
#include <glm/glm.hpp>

class PhysicsWorld;

// Behavior system that updates NPC state based on perception and hostility
class NPCBehavior {
public:
    // Update the NPC's behavior state and calculate desired movement
    // Returns the desired velocity for the NPC
    static glm::vec3 update(NPC& npc, float deltaTime, const glm::vec3& playerPosition,
                            const PhysicsWorld* physics);

    // Force a hostility change (e.g., from player attack)
    static void setHostility(NPC& npc, HostilityLevel level, HostilityTrigger trigger);

    // Apply damage to NPC (may trigger hostility change)
    static void applyDamage(NPC& npc, float damage, const glm::vec3& attackerPosition);

private:
    // State update functions
    static void updateIdle(NPC& npc, float deltaTime, const glm::vec3& playerPosition);
    static void updatePatrol(NPC& npc, float deltaTime);
    static void updateChase(NPC& npc, float deltaTime, const glm::vec3& playerPosition);
    static void updateAttack(NPC& npc, float deltaTime, const glm::vec3& playerPosition);
    static void updateFlee(NPC& npc, float deltaTime, const glm::vec3& playerPosition);
    static void updateReturn(NPC& npc, float deltaTime);

    // State transition logic
    static void evaluateStateTransition(NPC& npc, const glm::vec3& playerPosition);

    // Movement helpers
    static glm::vec3 moveTowards(NPC& npc, const glm::vec3& target, float deltaTime);
    static glm::vec3 moveAwayFrom(NPC& npc, const glm::vec3& threat, float deltaTime);
    static glm::vec3 calculatePatrolMovement(NPC& npc, float deltaTime);

    // Transition to a new state
    static void transitionTo(NPC& npc, BehaviorState newState);
};
