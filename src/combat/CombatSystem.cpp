#include "CombatSystem.h"
#include "../physics/PhysicsSystem.h"

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

void CombatSystem::registerCombatant(const CombatantInfo& info) {
    // Check for duplicate
    for (const auto& c : combatants_) {
        if (c.entity == info.entity) return;
    }

    RegisteredCombatant reg;
    reg.entity = info.entity;
    reg.ragdoll = info.ragdoll;
    reg.character = info.character;
    reg.rightHandBoneIndex = info.rightHandBoneIndex;
    reg.leftHandBoneIndex = info.leftHandBoneIndex;

    combatants_.push_back(reg);
    SDL_Log("Registered combatant entity %u", static_cast<uint32_t>(info.entity));
}

void CombatSystem::unregisterCombatant(ecs::Entity entity) {
    combatants_.erase(
        std::remove_if(combatants_.begin(), combatants_.end(),
            [entity](const RegisteredCombatant& c) { return c.entity == entity; }),
        combatants_.end()
    );
}

void CombatSystem::update(float deltaTime, PhysicsWorld& physics, ecs::World& world) {
    lastFrameHits_.clear();

    for (auto& combatant : combatants_) {
        if (!world.valid(combatant.entity)) continue;

        // Get or create combat state
        if (!world.has<CombatState>(combatant.entity)) {
            world.add<CombatState>(combatant.entity);
        }
        auto& state = world.get<CombatState>(combatant.entity);

        // Update state machine
        updateCombatState(combatant, state, deltaTime);

        // Hit detection during active frames
        if (state.phase == CombatPhase::Active) {
            performHitDetection(combatant, state, physics, world);
        }
    }

    // Recover motor strengths gradually
    recoverMotorStrengths(deltaTime);
}

void CombatSystem::processInput(ecs::Entity entity, const CombatInput& input, ecs::World& world) {
    if (!world.valid(entity)) return;
    if (!world.has<CombatState>(entity)) {
        world.add<CombatState>(entity);
    }

    auto& state = world.get<CombatState>(entity);

    // Light attack
    if (input.attackLight && state.canStartAttack()) {
        startAttack(entity, lightAttack_, world);
        return;
    }

    // Heavy attack
    if (input.attackHeavy && state.canStartAttack()) {
        startAttack(entity, heavyAttack_, world);
        return;
    }

    // Block
    if (input.block && state.canBlock()) {
        startBlock(entity, world);
    } else if (!input.block && state.phase == CombatPhase::Blocking) {
        stopBlock(entity, world);
    }
}

void CombatSystem::startAttack(ecs::Entity entity, const AttackDefinition& attack, ecs::World& world) {
    if (!world.has<CombatState>(entity)) return;
    auto& state = world.get<CombatState>(entity);

    // Start wind-up phase
    state.phase = CombatPhase::WindUp;
    state.currentAttack = attack.type;
    state.phaseTimer = 0.0f;
    state.phaseDuration = attack.windUpDuration;

    if (state.comboTimer < 0.8f && state.comboCount > 0) {
        state.comboCount++;
    } else {
        state.comboCount = 1;
    }
    state.canCombo = false;

    // Set ragdoll to powered mode for responsive combat animation
    auto* combatant = findCombatant(entity);
    if (combatant && combatant->ragdoll) {
        combatant->ragdoll->setMotorStrength(0.9f);
        combatant->ragdoll->setBlendMode(RagdollBlendMode::Powered);
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Attack started: type=%d, combo=%d",
                 static_cast<int>(attack.type), state.comboCount);
}

void CombatSystem::startBlock(ecs::Entity entity, ecs::World& world) {
    if (!world.has<CombatState>(entity)) return;
    auto& state = world.get<CombatState>(entity);

    state.phase = CombatPhase::Blocking;
    state.phaseTimer = 0.0f;
    state.phaseDuration = 0.0f; // Indefinite until released

    // Parry window at the start of block
    // The CombatPhase::Parrying is set briefly
}

void CombatSystem::stopBlock(ecs::Entity entity, ecs::World& world) {
    if (!world.has<CombatState>(entity)) return;
    auto& state = world.get<CombatState>(entity);

    if (state.phase == CombatPhase::Blocking || state.phase == CombatPhase::Parrying) {
        state.phase = CombatPhase::Idle;
        state.phaseTimer = 0.0f;
    }
}

void CombatSystem::updateCombatState(RegisteredCombatant& combatant, CombatState& state, float deltaTime) {
    state.phaseTimer += deltaTime;
    state.comboTimer += deltaTime;

    // Get the attack definition for current attack
    const AttackDefinition* currentAttack = nullptr;
    switch (state.currentAttack) {
        case AttackType::LightHorizontal:
        case AttackType::LightVertical:
            currentAttack = &lightAttack_;
            break;
        case AttackType::HeavyHorizontal:
        case AttackType::HeavyVertical:
            currentAttack = &heavyAttack_;
            break;
        case AttackType::Thrust:
            currentAttack = &thrustAttack_;
            break;
    }

    switch (state.phase) {
        case CombatPhase::Idle:
            break;

        case CombatPhase::WindUp:
            if (currentAttack && state.phaseTimer >= currentAttack->windUpDuration) {
                // Transition to active
                state.phase = CombatPhase::Active;
                state.phaseTimer = 0.0f;
                state.phaseDuration = currentAttack->activeDuration;
            }
            break;

        case CombatPhase::Active:
            if (currentAttack && state.phaseTimer >= currentAttack->activeDuration) {
                // Transition to recovery
                state.phase = CombatPhase::Recovery;
                state.phaseTimer = 0.0f;
                state.phaseDuration = currentAttack->recoveryDuration;
                state.canCombo = true; // Can queue next attack during recovery
            }
            break;

        case CombatPhase::Recovery:
            if (currentAttack && state.phaseTimer >= currentAttack->recoveryDuration) {
                state.phase = CombatPhase::Idle;
                state.phaseTimer = 0.0f;
                state.canCombo = false;
            }
            break;

        case CombatPhase::Blocking:
            // Check if at start - brief parry window
            if (state.phaseTimer < state.parryWindow) {
                // Parry window active (handled in hit detection)
            }
            break;

        case CombatPhase::Parrying:
            // Very brief - auto-transition back to blocking
            if (state.phaseTimer >= state.parryWindow) {
                state.phase = CombatPhase::Blocking;
                state.phaseTimer = 0.0f;
            }
            break;

        case CombatPhase::HitStagger:
            if (state.phaseTimer >= state.phaseDuration) {
                state.phase = CombatPhase::Idle;
                state.phaseTimer = 0.0f;

                // Restore ragdoll to powered mode after stagger
                if (combatant.ragdoll) {
                    combatant.ragdoll->transitionToMode(RagdollBlendMode::Powered, 0.3f);
                }
            }
            break;

        case CombatPhase::Knockdown:
            // Knockdown lasts until getting up is triggered
            if (state.phaseTimer >= 2.0f) {
                state.phase = CombatPhase::GettingUp;
                state.phaseTimer = 0.0f;
                state.phaseDuration = 1.0f;

                if (combatant.ragdoll) {
                    combatant.ragdoll->transitionToMode(RagdollBlendMode::Powered, 1.0f);
                }
            }
            break;

        case CombatPhase::GettingUp:
            if (state.phaseTimer >= state.phaseDuration) {
                state.phase = CombatPhase::Idle;
                state.phaseTimer = 0.0f;
            }
            break;

        case CombatPhase::Dodging:
            if (state.phaseTimer >= 0.5f) {
                state.phase = CombatPhase::Idle;
                state.phaseTimer = 0.0f;
            }
            break;
    }
}

void CombatSystem::performHitDetection(
    RegisteredCombatant& attacker,
    CombatState& attackerState,
    PhysicsWorld& physics,
    ecs::World& world)
{
    if (!attacker.character) return;

    // Get attacker world transform
    if (!world.has<ecs::Transform>(attacker.entity)) return;
    auto& attackerTransform = world.get<ecs::Transform>(attacker.entity);
    glm::vec3 attackerPos = attackerTransform.position();

    // Get the current attack definition
    const AttackDefinition* attack = nullptr;
    switch (attackerState.currentAttack) {
        case AttackType::LightHorizontal:
        case AttackType::LightVertical:
            attack = &lightAttack_;
            break;
        case AttackType::HeavyHorizontal:
        case AttackType::HeavyVertical:
            attack = &heavyAttack_;
            break;
        case AttackType::Thrust:
            attack = &thrustAttack_;
            break;
    }
    if (!attack) return;

    // Get attacker facing direction
    glm::vec3 forward = glm::normalize(glm::vec3(
        attackerTransform.matrix[2][0],
        0.0f,
        attackerTransform.matrix[2][2]
    ));

    // Check all other combatants for hits
    for (auto& target : combatants_) {
        if (target.entity == attacker.entity) continue;
        if (!world.valid(target.entity)) continue;
        if (!world.has<ecs::Transform>(target.entity)) continue;

        auto& targetTransform = world.get<ecs::Transform>(target.entity);
        glm::vec3 targetPos = targetTransform.position();

        // Distance check
        glm::vec3 toTarget = targetPos - attackerPos;
        float dist = glm::length(glm::vec2(toTarget.x, toTarget.z));
        if (dist > attack->sweepRadius) continue;

        // Angle check (is target within sweep arc?)
        glm::vec3 toTargetNorm = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
        float dotProduct = glm::dot(forward, toTargetNorm);
        float angle = glm::degrees(std::acos(std::clamp(dotProduct, -1.0f, 1.0f)));
        if (angle > attack->sweepAngle * 0.5f) continue;

        // Height check (within reasonable vertical range)
        float heightDiff = std::abs(toTarget.y);
        if (heightDiff > 2.0f) continue;

        // Hit detected!
        CombatHitResult hit;
        hit.targetEntity = static_cast<uint32_t>(target.entity);
        hit.hitPoint = targetPos + glm::vec3(0.0f, 1.0f, 0.0f); // Approximate hit point
        hit.hitNormal = -toTargetNorm;
        hit.damage = attack->damage * (1.0f + attackerState.comboCount * 0.1f); // Combo bonus
        hit.knockbackDirection = toTargetNorm;
        hit.knockbackForce = attack->knockbackForce;

        // Check if target is blocking/parrying
        if (world.has<CombatState>(target.entity)) {
            auto& targetState = world.get<CombatState>(target.entity);

            if (targetState.phase == CombatPhase::Blocking) {
                // Check parry timing (within parry window)
                if (targetState.phaseTimer < targetState.parryWindow) {
                    hit.wasParried = true;
                    hit.damage = 0.0f;
                    hit.knockbackForce *= 0.5f;

                    // Parry stuns the attacker briefly
                    attackerState.phase = CombatPhase::HitStagger;
                    attackerState.phaseTimer = 0.0f;
                    attackerState.phaseDuration = 0.5f;

                    if (attacker.ragdoll) {
                        attacker.ragdoll->applyImpulseAtPoint(
                            attackerPos + forward,
                            -forward * 30.0f
                        );
                    }
                } else {
                    hit.wasBlocked = true;
                    hit.damage *= 0.2f; // Chip damage
                    hit.knockbackForce *= 0.3f;
                }
            }
        }

        // Apply hit reaction
        applyHitReaction(target, world.get<CombatState>(target.entity), hit);

        lastFrameHits_.push_back(hit);

        if (hitCallback_) {
            hitCallback_(hit);
        }
    }
}

void CombatSystem::applyHitReaction(
    RegisteredCombatant& target,
    CombatState& targetState,
    const CombatHitResult& hit)
{
    if (hit.wasParried) {
        // Parry - minimal reaction on target
        return;
    }

    // Apply damage
    // Health component would be on the entity - handled externally via callback

    // Determine reaction severity
    bool isKnockdown = hit.knockbackForce > 80.0f && !hit.wasBlocked;

    if (isKnockdown) {
        // Full ragdoll knockdown
        targetState.phase = CombatPhase::Knockdown;
        targetState.phaseTimer = 0.0f;
        targetState.phaseDuration = 2.0f;

        if (target.ragdoll) {
            target.ragdoll->transitionToMode(RagdollBlendMode::FullRagdoll, 0.15f);
            target.ragdoll->applyImpulseAtPoint(
                hit.hitPoint,
                hit.knockbackDirection * hit.knockbackForce
            );
        }
    } else if (!hit.wasBlocked) {
        // Stagger
        targetState.phase = CombatPhase::HitStagger;
        targetState.phaseTimer = 0.0f;
        targetState.phaseDuration = 0.5f;

        if (target.ragdoll) {
            // Reduce motor strength at hit location for physics response
            target.ragdoll->applyImpulseAtPoint(
                hit.hitPoint,
                hit.knockbackDirection * hit.knockbackForce
            );
            // Keep motors partially engaged for stagger animation
            target.ragdoll->setMotorStrength(0.4f);
        }
    } else {
        // Blocked - slight push back
        if (target.ragdoll) {
            target.ragdoll->applyImpulseAtPoint(
                hit.hitPoint,
                hit.knockbackDirection * hit.knockbackForce
            );
        }
    }

    // Reset combo timer on hit
    targetState.comboTimer = 0.0f;
}

void CombatSystem::recoverMotorStrengths(float deltaTime) {
    for (auto& combatant : combatants_) {
        if (!combatant.ragdoll) continue;
        if (!combatant.ragdoll->isEnabled()) continue;

        // Gradually restore per-bone motor strengths
        auto& boneStates = combatant.ragdoll->getBoneStates();
        const auto& def = combatant.ragdoll->getDefinition();

        for (size_t i = 0; i < def.bones.size(); ++i) {
            float current = combatant.ragdoll->getMotorStrength();
            if (current < 0.95f && combatant.ragdoll->getBlendMode() == RagdollBlendMode::Powered) {
                float newStrength = std::min(1.0f, current + motorRecoveryRate_ * deltaTime);
                combatant.ragdoll->setMotorStrength(newStrength);
            }
        }
    }
}

CombatSystem::RegisteredCombatant* CombatSystem::findCombatant(ecs::Entity entity) {
    for (auto& c : combatants_) {
        if (c.entity == entity) return &c;
    }
    return nullptr;
}
