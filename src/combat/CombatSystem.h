#pragma once

#include "CombatComponents.h"
#include "../physics/ActiveRagdoll.h"
#include "../animation/AnimatedCharacter.h"
#include "../animation/AnimationEvent.h"
#include "../ecs/World.h"
#include "../ecs/Components.h"

#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>

class PhysicsWorld;

// Callback for when a hit lands
using CombatHitCallback = std::function<void(const CombatHitResult& hit)>;

// CombatSystem manages combat state, hit detection, and ragdoll reactions
// for all combat-capable entities (player and NPCs).
//
// Key concepts:
// - Each combat entity has a CombatState component tracking its phase
// - Attacks follow a WindUp -> Active -> Recovery lifecycle
// - During Active phase, weapon sweep volumes detect hits
// - Hits apply physics impulses to the target's ActiveRagdoll
// - Hit reactions blend physics forces with animation via motor strength
class CombatSystem {
public:
    CombatSystem() = default;

    // Register a combatant entity with its ragdoll and animation
    struct CombatantInfo {
        ecs::Entity entity;
        ActiveRagdoll* ragdoll = nullptr;           // Physics ragdoll
        AnimatedCharacter* character = nullptr;      // Animation source
        int32_t rightHandBoneIndex = -1;             // For weapon attachment
        int32_t leftHandBoneIndex = -1;
    };

    void registerCombatant(const CombatantInfo& info);
    void unregisterCombatant(ecs::Entity entity);

    // Process combat for all registered entities
    // Should be called each frame after animation update but before rendering
    void update(float deltaTime, PhysicsWorld& physics, ecs::World& world);

    // Process combat input for a specific entity
    void processInput(ecs::Entity entity, const CombatInput& input, ecs::World& world);

    // Start an attack
    void startAttack(ecs::Entity entity, const AttackDefinition& attack, ecs::World& world);

    // Start blocking
    void startBlock(ecs::Entity entity, ecs::World& world);
    void stopBlock(ecs::Entity entity, ecs::World& world);

    // Register hit callback
    void setHitCallback(CombatHitCallback callback) { hitCallback_ = std::move(callback); }

    // Get attack definitions
    const AttackDefinition& getLightAttack() const { return lightAttack_; }
    const AttackDefinition& getHeavyAttack() const { return heavyAttack_; }
    const AttackDefinition& getThrustAttack() const { return thrustAttack_; }

    // Get hit results from last frame (for effects/UI)
    const std::vector<CombatHitResult>& getLastFrameHits() const { return lastFrameHits_; }

    // Recovery: gradually restore motor strength after hit reaction
    void setRecoveryRate(float rate) { motorRecoveryRate_ = rate; }

private:
    struct RegisteredCombatant {
        ecs::Entity entity;
        ActiveRagdoll* ragdoll = nullptr;
        AnimatedCharacter* character = nullptr;
        int32_t rightHandBoneIndex = -1;
        int32_t leftHandBoneIndex = -1;
    };

    std::vector<RegisteredCombatant> combatants_;

    // Attack definitions
    AttackDefinition lightAttack_ = AttackDefinition::lightSlash();
    AttackDefinition heavyAttack_ = AttackDefinition::heavySlash();
    AttackDefinition thrustAttack_ = AttackDefinition::thrust();

    // Hit detection
    std::vector<CombatHitResult> lastFrameHits_;
    CombatHitCallback hitCallback_;

    // Motor recovery rate (per second)
    float motorRecoveryRate_ = 2.0f;

    // Update combat state machine for a single entity
    void updateCombatState(RegisteredCombatant& combatant, CombatState& state, float deltaTime);

    // Perform weapon sweep hit detection during active frames
    void performHitDetection(
        RegisteredCombatant& attacker,
        CombatState& attackerState,
        PhysicsWorld& physics,
        ecs::World& world
    );

    // Apply hit reaction to a target
    void applyHitReaction(
        RegisteredCombatant& target,
        CombatState& targetState,
        const CombatHitResult& hit
    );

    // Recover motor strengths gradually
    void recoverMotorStrengths(float deltaTime);

    // Find combatant by entity
    RegisteredCombatant* findCombatant(ecs::Entity entity);
};
