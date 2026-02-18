#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <vector>
#include <string>

// =============================================================================
// Combat State
// =============================================================================

enum class CombatPhase : uint8_t {
    Idle = 0,        // Not in combat
    WindUp = 1,      // Attack wind-up (can cancel)
    Active = 2,      // Attack active frames (hitbox active)
    Recovery = 3,    // Attack recovery (vulnerable)
    Blocking = 4,    // Actively blocking
    Parrying = 5,    // Parry window (brief, deflects attacks)
    HitStagger = 6,  // Reacting to hit (stagger)
    Knockdown = 7,   // Knocked down (ragdoll transition)
    GettingUp = 8,   // Recovering from knockdown
    Dodging = 9      // Dodge/roll i-frames
};

// Attack type for different swing directions
enum class AttackType : uint8_t {
    LightHorizontal = 0,
    LightVertical = 1,
    HeavyHorizontal = 2,
    HeavyVertical = 3,
    Thrust = 4
};

struct CombatState {
    CombatPhase phase = CombatPhase::Idle;
    AttackType currentAttack = AttackType::LightHorizontal;
    float phaseTimer = 0.0f;        // Time in current phase
    float phaseDuration = 0.0f;     // Duration of current phase
    int32_t comboCount = 0;         // Current combo count
    float comboTimer = 0.0f;        // Time since last hit (resets combo)
    bool canCombo = false;          // Whether input is accepted for next combo
    float parryWindow = 0.15f;      // Duration of parry timing window

    CombatState() = default;

    [[nodiscard]] bool isAttacking() const {
        return phase == CombatPhase::WindUp ||
               phase == CombatPhase::Active ||
               phase == CombatPhase::Recovery;
    }

    [[nodiscard]] bool isVulnerable() const {
        return phase == CombatPhase::Recovery ||
               phase == CombatPhase::HitStagger;
    }

    [[nodiscard]] bool canStartAttack() const {
        return phase == CombatPhase::Idle ||
               (phase == CombatPhase::Recovery && canCombo) ||
               phase == CombatPhase::Blocking;
    }

    [[nodiscard]] bool canBlock() const {
        return phase == CombatPhase::Idle ||
               phase == CombatPhase::Recovery;
    }

    [[nodiscard]] float phaseProgress() const {
        return phaseDuration > 0.0f ? phaseTimer / phaseDuration : 1.0f;
    }
};

// =============================================================================
// Attack Definition
// =============================================================================

struct AttackDefinition {
    AttackType type = AttackType::LightHorizontal;
    float windUpDuration = 0.2f;     // Seconds before active frames
    float activeDuration = 0.15f;    // Seconds of active hitbox
    float recoveryDuration = 0.3f;   // Seconds of recovery
    float damage = 10.0f;
    float knockbackForce = 50.0f;    // Impulse applied to ragdoll on hit
    float staggerDuration = 0.5f;    // How long the target staggers
    std::string animationName;       // Animation clip to play

    // Weapon sweep arc (for hit detection)
    float sweepAngle = 120.0f;       // Degrees of horizontal sweep
    float sweepRadius = 1.5f;        // Reach in meters

    [[nodiscard]] float totalDuration() const {
        return windUpDuration + activeDuration + recoveryDuration;
    }

    // Preset attacks
    static AttackDefinition lightSlash() {
        AttackDefinition atk;
        atk.type = AttackType::LightHorizontal;
        atk.windUpDuration = 0.15f;
        atk.activeDuration = 0.12f;
        atk.recoveryDuration = 0.25f;
        atk.damage = 10.0f;
        atk.knockbackForce = 40.0f;
        atk.staggerDuration = 0.4f;
        atk.sweepAngle = 100.0f;
        atk.sweepRadius = 1.5f;
        return atk;
    }

    static AttackDefinition heavySlash() {
        AttackDefinition atk;
        atk.type = AttackType::HeavyHorizontal;
        atk.windUpDuration = 0.35f;
        atk.activeDuration = 0.15f;
        atk.recoveryDuration = 0.45f;
        atk.damage = 25.0f;
        atk.knockbackForce = 100.0f;
        atk.staggerDuration = 0.8f;
        atk.sweepAngle = 140.0f;
        atk.sweepRadius = 1.8f;
        return atk;
    }

    static AttackDefinition thrust() {
        AttackDefinition atk;
        atk.type = AttackType::Thrust;
        atk.windUpDuration = 0.2f;
        atk.activeDuration = 0.1f;
        atk.recoveryDuration = 0.3f;
        atk.damage = 15.0f;
        atk.knockbackForce = 60.0f;
        atk.staggerDuration = 0.5f;
        atk.sweepAngle = 30.0f;  // Narrow
        atk.sweepRadius = 2.0f;  // Longer reach
        return atk;
    }
};

// =============================================================================
// Health Component
// =============================================================================

struct Health {
    float current = 100.0f;
    float maximum = 100.0f;
    float armor = 0.0f;              // Damage reduction (0-1)
    float lastDamageTime = -10.0f;   // Time since last damage (for regen)
    float regenRate = 0.0f;          // HP per second regen
    bool isDead = false;

    Health() = default;
    Health(float max) : current(max), maximum(max) {}

    void takeDamage(float amount) {
        float reduced = amount * (1.0f - armor);
        current = std::max(0.0f, current - reduced);
        lastDamageTime = 0.0f;
        if (current <= 0.0f) isDead = true;
    }

    void heal(float amount) {
        current = std::min(maximum, current + amount);
        if (current > 0.0f) isDead = false;
    }

    [[nodiscard]] float percentage() const {
        return maximum > 0.0f ? current / maximum : 0.0f;
    }
};

// =============================================================================
// Weapon Hit Box
// =============================================================================

// Describes the hitbox attached to a weapon bone
struct WeaponHitBox {
    int32_t tipBoneIndex = -1;       // Bone at weapon tip
    int32_t baseBoneIndex = -1;      // Bone at weapon base (hand)
    float radius = 0.1f;            // Capsule radius around weapon line

    // Previous frame positions for sweep detection
    glm::vec3 prevTipPos = glm::vec3(0.0f);
    glm::vec3 prevBasePos = glm::vec3(0.0f);

    // Bodies already hit this attack (avoid double-hit)
    std::vector<uint32_t> hitBodiesThisAttack;

    void clearHits() { hitBodiesThisAttack.clear(); }

    [[nodiscard]] bool hasAlreadyHit(uint32_t bodyId) const {
        for (auto id : hitBodiesThisAttack) {
            if (id == bodyId) return true;
        }
        return false;
    }
};

// =============================================================================
// Combat Hit Result
// =============================================================================

struct CombatHitResult {
    uint32_t targetEntity = UINT32_MAX;  // ECS entity that was hit
    int32_t hitBoneIndex = -1;           // Which bone was hit
    glm::vec3 hitPoint = glm::vec3(0.0f);
    glm::vec3 hitNormal = glm::vec3(0.0f);
    float damage = 0.0f;
    glm::vec3 knockbackDirection = glm::vec3(0.0f);
    float knockbackForce = 0.0f;
    bool wasBlocked = false;
    bool wasParried = false;
};

// =============================================================================
// Combat Input
// =============================================================================

struct CombatInput {
    bool attackLight = false;     // Light attack pressed this frame
    bool attackHeavy = false;     // Heavy attack pressed this frame
    bool block = false;           // Block held
    bool dodge = false;           // Dodge pressed this frame
    glm::vec3 aimDirection = glm::vec3(0.0f, 0.0f, 1.0f); // Where character is facing

    CombatInput() = default;
};
