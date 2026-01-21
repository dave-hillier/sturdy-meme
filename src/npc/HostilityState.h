#pragma once

#include <cstdint>

// Hostility level determines how the NPC reacts to the player
enum class HostilityLevel : uint8_t {
    Friendly,   // Will not attack, may help the player
    Neutral,    // Ignores the player unless provoked
    Hostile,    // Attacks the player on sight
    Afraid      // Flees from the player
};

// Behavior state determines what the NPC is currently doing
enum class BehaviorState : uint8_t {
    Idle,       // Standing still, looking around
    Patrol,     // Walking along a patrol path
    Chase,      // Following/pursuing the player
    Attack,     // Actively attacking the player
    Flee,       // Running away from the player
    Return      // Returning to original position
};

// What triggered a hostility change
enum class HostilityTrigger : uint8_t {
    None,           // No trigger
    PlayerAttack,   // Player attacked the NPC
    PlayerProximity,// Player got too close
    AllyAttacked,   // An ally was attacked
    Timeout,        // Hostility decay over time
    PlayerFled      // Player moved far away
};

// Configuration for hostility behavior thresholds
struct HostilityConfig {
    // Distance thresholds (in meters)
    float sightRange = 20.0f;           // How far the NPC can see the player
    float attackRange = 2.0f;           // Distance to start attacking
    float chaseRange = 30.0f;           // Max distance to chase before giving up
    float personalSpace = 3.0f;         // Distance that triggers hostility for neutral NPCs
    float fleeDistance = 15.0f;         // Distance to flee before stopping (for Afraid NPCs)

    // Time thresholds (in seconds)
    float hostilityDecayTime = 30.0f;   // Time until hostility decays
    float memoryDuration = 60.0f;       // How long NPC remembers player position
    float attackCooldown = 1.5f;        // Time between attacks

    // Awareness thresholds (0-1)
    float detectionThreshold = 0.3f;    // Awareness needed to detect player
    float chaseThreshold = 0.6f;        // Awareness needed to start chasing
    float attackThreshold = 0.9f;       // Awareness needed to attack

    // Speed modifiers
    float patrolSpeedMultiplier = 0.5f; // Speed when patrolling (relative to base speed)
    float chaseSpeedMultiplier = 1.2f;  // Speed when chasing
    float fleeSpeedMultiplier = 1.5f;   // Speed when fleeing
};
