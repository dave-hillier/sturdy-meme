#pragma once

#include "CombatComponents.h"
#include "../animation/AnimatedCharacter.h"
#include "../animation/AnimationLayerController.h"
#include "../animation/BoneMask.h"
#include "../physics/ActiveRagdoll.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

// CombatAnimationController manages animation-to-ragdoll coordination.
//
// It uses the existing AnimationLayerController to blend:
// - Base layer: locomotion animations (idle/walk/run)
// - Combat layer: attack/block/dodge animations (upper body override)
// - Additive layer: hit reactions (physics-driven additive)
//
// The ragdoll reads animation targets from the final blended pose,
// and physics influences feed back into the pose via ragdoll readback.
class CombatAnimationController {
public:
    CombatAnimationController() = default;

    // Initialize with a character's animation and skeleton data
    void initialize(AnimatedCharacter& character, ActiveRagdoll* ragdoll);

    // Update each frame
    // 1. Samples animation state machine for current combat phase
    // 2. Feeds animation targets to ragdoll motors
    // 3. Reads back physics-influenced transforms
    // 4. Blends for final pose
    void update(
        float deltaTime,
        const CombatState& combatState,
        const glm::mat4& characterTransform,
        float movementSpeed,
        bool isGrounded
    );

    // Set combat animation clips by name
    // These are looked up from the character's loaded animations
    void setCombatAnimations(
        const std::string& lightAttack,
        const std::string& heavyAttack,
        const std::string& block,
        const std::string& hitReactFront,
        const std::string& hitReactBack,
        const std::string& dodge
    );

    // Get the final bone matrices (physics + animation blended)
    // These should be used for rendering instead of the raw animation matrices
    const std::vector<glm::mat4>& getFinalBoneMatrices() const { return finalBoneMatrices_; }
    bool hasFinalBoneMatrices() const { return !finalBoneMatrices_.empty(); }

    // Access ragdoll
    ActiveRagdoll* getRagdoll() { return ragdoll_; }
    const ActiveRagdoll* getRagdoll() const { return ragdoll_; }

    // Motor strength controls
    void setBaseMotorStrength(float strength) { baseMotorStrength_ = strength; }
    float getBaseMotorStrength() const { return baseMotorStrength_; }

    // Whether to use the combat animation controller output
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

private:
    AnimatedCharacter* character_ = nullptr;
    ActiveRagdoll* ragdoll_ = nullptr;
    bool active_ = false;

    // Animation clip indices for combat
    std::unordered_map<std::string, size_t> combatClipIndices_;
    std::string lightAttackAnim_;
    std::string heavyAttackAnim_;
    std::string blockAnim_;
    std::string hitReactFrontAnim_;
    std::string hitReactBackAnim_;
    std::string dodgeAnim_;

    // Final blended bone matrices
    std::vector<glm::mat4> finalBoneMatrices_;
    std::vector<glm::mat4> animationBoneMatrices_;

    // Motor strength
    float baseMotorStrength_ = 0.85f;

    // Bone mask for upper body combat override
    BoneMask upperBodyMask_;

    // Find animation clip index by name (returns -1 if not found)
    int32_t findClipIndex(const std::string& name) const;

    // Apply combat animation state to the layer controller
    void applyCombatAnimation(const CombatState& combatState);
};
