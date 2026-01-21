#include "NPCBehavior.h"
#include "physics/PhysicsSystem.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

glm::vec3 NPCBehavior::update(NPC& npc, float deltaTime, const glm::vec3& playerPosition,
                               const PhysicsWorld* physics) {
    if (!npc.isAlive()) {
        npc.velocity = glm::vec3(0.0f);
        return npc.velocity;
    }

    // Update timers
    npc.stateTimer += deltaTime;
    npc.attackCooldownTimer = std::max(0.0f, npc.attackCooldownTimer - deltaTime);

    // Update perception
    bool playerDetected = npc.perception.update(deltaTime, npc.transform.position,
                                                  npc.transform.forward(), playerPosition,
                                                  npc.config, physics);

    // Update hostility timer for decay
    if (npc.hostility != npc.baseHostility) {
        npc.hostilityTimer += deltaTime;
        if (npc.hostilityTimer >= npc.config.hostilityDecayTime) {
            setHostility(npc, npc.baseHostility, HostilityTrigger::Timeout);
        }
    }

    // Evaluate state transitions based on perception and hostility
    evaluateStateTransition(npc, playerPosition);

    // Update current state and calculate movement
    glm::vec3 desiredVelocity(0.0f);

    switch (npc.behaviorState) {
        case BehaviorState::Idle:
            updateIdle(npc, deltaTime, playerPosition);
            break;

        case BehaviorState::Patrol:
            updatePatrol(npc, deltaTime);
            desiredVelocity = calculatePatrolMovement(npc, deltaTime);
            break;

        case BehaviorState::Chase:
            updateChase(npc, deltaTime, playerPosition);
            desiredVelocity = moveTowards(npc, playerPosition, deltaTime);
            break;

        case BehaviorState::Attack:
            updateAttack(npc, deltaTime, playerPosition);
            // Stay in place during attack, but face player
            npc.transform.smoothLookAt(playerPosition, deltaTime);
            break;

        case BehaviorState::Flee:
            updateFlee(npc, deltaTime, playerPosition);
            desiredVelocity = moveAwayFrom(npc, playerPosition, deltaTime);
            break;

        case BehaviorState::Return:
            updateReturn(npc, deltaTime);
            desiredVelocity = moveTowards(npc, npc.spawnPosition, deltaTime);
            break;
    }

    // Apply speed multiplier
    float speedMult = npc.getSpeedMultiplier();
    npc.currentSpeed = npc.baseSpeed * speedMult;
    npc.velocity = desiredVelocity * npc.currentSpeed;

    // Update alert level for visual feedback (smooth transition)
    float targetAlert = 0.0f;
    if (npc.behaviorState == BehaviorState::Attack) {
        targetAlert = 1.0f;
    } else if (npc.behaviorState == BehaviorState::Chase || npc.behaviorState == BehaviorState::Flee) {
        targetAlert = 0.7f;
    } else if (npc.perception.awareness > npc.config.detectionThreshold) {
        targetAlert = npc.perception.awareness * 0.5f;
    }
    npc.alertLevel += (targetAlert - npc.alertLevel) * (1.0f - exp(-5.0f * deltaTime));

    return npc.velocity;
}

void NPCBehavior::setHostility(NPC& npc, HostilityLevel level, HostilityTrigger trigger) {
    if (npc.hostility != level) {
        npc.hostility = level;
        npc.lastTrigger = trigger;
        npc.hostilityTimer = 0.0f;

        SDL_Log("NPC %s hostility changed to %d (trigger: %d)",
                npc.name.c_str(), static_cast<int>(level), static_cast<int>(trigger));
    }
}

void NPCBehavior::applyDamage(NPC& npc, float damage, const glm::vec3& attackerPosition) {
    npc.health = std::max(0.0f, npc.health - damage);

    if (npc.health <= 0.0f) {
        SDL_Log("NPC %s died", npc.name.c_str());
        return;
    }

    // Become hostile when attacked (unless afraid)
    if (npc.hostility != HostilityLevel::Afraid) {
        setHostility(npc, HostilityLevel::Hostile, HostilityTrigger::PlayerAttack);
    }

    // Update perception with attacker position
    npc.perception.lastKnownPosition = attackerPosition;
    npc.perception.hasLastKnownPosition = true;
    npc.perception.awareness = 1.0f;  // Full awareness when attacked

    SDL_Log("NPC %s took %.1f damage (%.1f remaining)", npc.name.c_str(), damage, npc.health);
}

void NPCBehavior::updateIdle(NPC& npc, float deltaTime, const glm::vec3& playerPosition) {
    npc.idleTimer += deltaTime;

    // Occasional look around behavior
    if (npc.idleTimer > 3.0f) {
        npc.idleTimer = 0.0f;
        // Could add random head turn animation trigger here
    }

    // If we have a patrol path, transition to patrol after idling
    if (!npc.patrolPath.empty() && npc.stateTimer > 2.0f) {
        transitionTo(npc, BehaviorState::Patrol);
    }
}

void NPCBehavior::updatePatrol(NPC& npc, float deltaTime) {
    if (npc.patrolPath.empty()) {
        transitionTo(npc, BehaviorState::Idle);
        return;
    }

    const auto& waypoint = npc.patrolPath[npc.currentWaypointIndex];
    float distToWaypoint = glm::length(waypoint.position - npc.transform.position);

    // Check if reached waypoint
    if (distToWaypoint < 0.5f) {
        npc.waypointWaitTimer += deltaTime;

        if (npc.waypointWaitTimer >= waypoint.waitTime) {
            npc.waypointWaitTimer = 0.0f;

            // Move to next waypoint
            if (npc.patrolForward) {
                npc.currentWaypointIndex++;
                if (npc.currentWaypointIndex >= npc.patrolPath.size()) {
                    npc.currentWaypointIndex = npc.patrolPath.size() - 2;
                    npc.patrolForward = false;
                    if (npc.patrolPath.size() == 1) {
                        npc.currentWaypointIndex = 0;
                    }
                }
            } else {
                if (npc.currentWaypointIndex == 0) {
                    npc.patrolForward = true;
                    if (npc.patrolPath.size() > 1) {
                        npc.currentWaypointIndex = 1;
                    }
                } else {
                    npc.currentWaypointIndex--;
                }
            }
        }
    }
}

void NPCBehavior::updateChase(NPC& npc, float deltaTime, const glm::vec3& playerPosition) {
    // Face the player while chasing
    if (npc.perception.canSeePlayer) {
        npc.transform.smoothLookAt(playerPosition, deltaTime, 8.0f);
    } else if (npc.perception.hasLastKnownPosition) {
        npc.transform.smoothLookAt(npc.perception.lastKnownPosition, deltaTime, 5.0f);
    }
}

void NPCBehavior::updateAttack(NPC& npc, float deltaTime, const glm::vec3& playerPosition) {
    npc.transform.smoothLookAt(playerPosition, deltaTime, 10.0f);

    // Check if we can attack
    if (npc.canAttack() && npc.perception.distanceToPlayer <= npc.config.attackRange) {
        npc.isAttacking = true;
        npc.attackCooldownTimer = npc.config.attackCooldown;
        SDL_Log("NPC %s attacks!", npc.name.c_str());
        // Attack damage would be applied here through a callback or event
    } else {
        npc.isAttacking = false;
    }
}

void NPCBehavior::updateFlee(NPC& npc, float deltaTime, const glm::vec3& playerPosition) {
    // Face away from player while fleeing
    glm::vec3 awayDir = npc.transform.position - playerPosition;
    if (glm::length(awayDir) > 0.001f) {
        glm::vec3 fleeTarget = npc.transform.position + glm::normalize(awayDir) * 10.0f;
        npc.transform.smoothLookAt(fleeTarget, deltaTime, 8.0f);
    }
}

void NPCBehavior::updateReturn(NPC& npc, float deltaTime) {
    float distToSpawn = glm::length(npc.spawnPosition - npc.transform.position);

    // Face spawn position
    npc.transform.smoothLookAt(npc.spawnPosition, deltaTime, 5.0f);

    // Check if returned to spawn
    if (distToSpawn < 1.0f) {
        transitionTo(npc, BehaviorState::Idle);
        npc.perception.reset();
    }
}

void NPCBehavior::evaluateStateTransition(NPC& npc, const glm::vec3& playerPosition) {
    float distToPlayer = npc.perception.distanceToPlayer;
    float awareness = npc.perception.awareness;
    bool canSee = npc.perception.canSeePlayer;

    switch (npc.hostility) {
        case HostilityLevel::Friendly:
            // Friendly NPCs stay idle or patrol, never attack
            if (npc.behaviorState == BehaviorState::Chase ||
                npc.behaviorState == BehaviorState::Attack) {
                transitionTo(npc, BehaviorState::Return);
            }
            break;

        case HostilityLevel::Neutral:
            // Neutral NPCs become hostile if player gets too close
            if (distToPlayer < npc.config.personalSpace && canSee) {
                setHostility(npc, HostilityLevel::Hostile, HostilityTrigger::PlayerProximity);
            }
            // Otherwise patrol or idle
            if (npc.behaviorState == BehaviorState::Chase ||
                npc.behaviorState == BehaviorState::Attack) {
                transitionTo(npc, BehaviorState::Return);
            }
            break;

        case HostilityLevel::Hostile:
            // Hostile behavior based on awareness and distance
            if (canSee && awareness >= npc.config.attackThreshold &&
                distToPlayer <= npc.config.attackRange) {
                if (npc.behaviorState != BehaviorState::Attack) {
                    transitionTo(npc, BehaviorState::Attack);
                }
            } else if (awareness >= npc.config.chaseThreshold &&
                       (canSee || npc.perception.hasLastKnownPosition)) {
                if (distToPlayer > npc.config.chaseRange) {
                    // Player too far, give up chase
                    transitionTo(npc, BehaviorState::Return);
                } else if (npc.behaviorState != BehaviorState::Chase &&
                           npc.behaviorState != BehaviorState::Attack) {
                    transitionTo(npc, BehaviorState::Chase);
                }
            } else if (awareness < npc.config.detectionThreshold) {
                // Lost track of player
                if (npc.behaviorState == BehaviorState::Chase ||
                    npc.behaviorState == BehaviorState::Attack) {
                    transitionTo(npc, BehaviorState::Return);
                }
            }
            break;

        case HostilityLevel::Afraid:
            // Afraid NPCs flee from visible players
            if (canSee && distToPlayer < npc.config.sightRange) {
                if (npc.behaviorState != BehaviorState::Flee) {
                    transitionTo(npc, BehaviorState::Flee);
                }
            } else if (distToPlayer >= npc.config.fleeDistance) {
                // Far enough, stop fleeing
                if (npc.behaviorState == BehaviorState::Flee) {
                    transitionTo(npc, BehaviorState::Return);
                }
            }
            break;
    }
}

glm::vec3 NPCBehavior::moveTowards(NPC& npc, const glm::vec3& target, float deltaTime) {
    glm::vec3 direction = target - npc.transform.position;
    direction.y = 0.0f;  // Keep on horizontal plane

    float distance = glm::length(direction);
    if (distance < 0.1f) {
        return glm::vec3(0.0f);
    }

    direction = glm::normalize(direction);
    npc.transform.smoothLookAt(target, deltaTime);

    return direction;
}

glm::vec3 NPCBehavior::moveAwayFrom(NPC& npc, const glm::vec3& threat, float deltaTime) {
    glm::vec3 direction = npc.transform.position - threat;
    direction.y = 0.0f;  // Keep on horizontal plane

    float distance = glm::length(direction);
    if (distance < 0.001f) {
        // If too close, pick a random direction
        direction = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        direction = glm::normalize(direction);
    }

    return direction;
}

glm::vec3 NPCBehavior::calculatePatrolMovement(NPC& npc, float deltaTime) {
    if (npc.patrolPath.empty()) {
        return glm::vec3(0.0f);
    }

    const auto& waypoint = npc.patrolPath[npc.currentWaypointIndex];
    return moveTowards(npc, waypoint.position, deltaTime);
}

void NPCBehavior::transitionTo(NPC& npc, BehaviorState newState) {
    if (npc.behaviorState == newState) {
        return;
    }

    npc.previousState = npc.behaviorState;
    npc.behaviorState = newState;
    npc.stateTimer = 0.0f;
    npc.isAttacking = false;

    SDL_Log("NPC %s: %d -> %d", npc.name.c_str(),
            static_cast<int>(npc.previousState), static_cast<int>(newState));
}
