#include "CombatAnimationController.h"
#include <SDL3/SDL_log.h>
#include <algorithm>

void CombatAnimationController::initialize(AnimatedCharacter& character, ActiveRagdoll* ragdoll) {
    character_ = &character;
    ragdoll_ = ragdoll;

    // Build upper body mask for combat animation override
    const auto& skeleton = character.getSkeleton();
    upperBodyMask_ = BoneMask::upperBody(skeleton);

    // Pre-allocate bone matrices
    size_t boneCount = skeleton.joints.size();
    finalBoneMatrices_.resize(boneCount, glm::mat4(1.0f));
    animationBoneMatrices_.resize(boneCount, glm::mat4(1.0f));

    active_ = true;

    SDL_Log("CombatAnimationController initialized with %zu bones", boneCount);
}

void CombatAnimationController::update(
    float deltaTime,
    const CombatState& combatState,
    const glm::mat4& characterTransform,
    float movementSpeed,
    bool isGrounded)
{
    if (!active_ || !character_) return;

    // Step 1: Get animation bone matrices (from whatever mode character uses)
    character_->computeBoneMatrices(animationBoneMatrices_);

    // Step 2: Copy animation as base for final output
    finalBoneMatrices_ = animationBoneMatrices_;

    // Step 3: Drive ragdoll toward animation pose
    if (ragdoll_ && ragdoll_->isEnabled()) {
        // Adjust motor strength based on combat phase
        float motorStrength = baseMotorStrength_;

        switch (combatState.phase) {
            case CombatPhase::Idle:
                motorStrength = baseMotorStrength_;
                break;
            case CombatPhase::WindUp:
                motorStrength = 0.95f; // Strong tracking during wind-up
                break;
            case CombatPhase::Active:
                motorStrength = 0.9f;  // Strong but allow some physics deviation
                break;
            case CombatPhase::Recovery:
                motorStrength = 0.7f;  // More relaxed during recovery
                break;
            case CombatPhase::Blocking:
            case CombatPhase::Parrying:
                motorStrength = 0.95f; // Strong for block pose
                break;
            case CombatPhase::HitStagger:
                motorStrength = 0.3f;  // Weak motors - let physics dominate
                break;
            case CombatPhase::Knockdown:
                motorStrength = 0.0f;  // Pure ragdoll
                break;
            case CombatPhase::GettingUp:
                motorStrength = 0.5f + (combatState.phaseProgress() * 0.4f);
                break;
            case CombatPhase::Dodging:
                motorStrength = 0.85f;
                break;
        }

        // Apply computed motor strength to ragdoll
        ragdoll_->setMotorStrength(motorStrength);

        // Drive ragdoll bodies toward animation targets
        ragdoll_->driveToAnimationPose(animationBoneMatrices_, characterTransform, deltaTime);

        // Read back physics-influenced transforms
        ragdoll_->readPhysicsTransforms(
            finalBoneMatrices_,
            animationBoneMatrices_,
            characterTransform
        );
    }
}

void CombatAnimationController::setCombatAnimations(
    const std::string& lightAttack,
    const std::string& heavyAttack,
    const std::string& block,
    const std::string& hitReactFront,
    const std::string& hitReactBack,
    const std::string& dodge)
{
    lightAttackAnim_ = lightAttack;
    heavyAttackAnim_ = heavyAttack;
    blockAnim_ = block;
    hitReactFrontAnim_ = hitReactFront;
    hitReactBackAnim_ = hitReactBack;
    dodgeAnim_ = dodge;

    // Look up clip indices
    if (character_) {
        auto tryFind = [this](const std::string& name) {
            int32_t idx = findClipIndex(name);
            if (idx >= 0) {
                combatClipIndices_[name] = static_cast<size_t>(idx);
            }
        };

        tryFind(lightAttack);
        tryFind(heavyAttack);
        tryFind(block);
        tryFind(hitReactFront);
        tryFind(hitReactBack);
        tryFind(dodge);

        SDL_Log("Combat animations: found %zu/%d clips",
                combatClipIndices_.size(), 6);
    }
}

int32_t CombatAnimationController::findClipIndex(const std::string& name) const {
    if (!character_ || name.empty()) return -1;

    const auto& animations = character_->getAnimations();
    for (size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void CombatAnimationController::applyCombatAnimation(const CombatState& combatState) {
    if (!character_) return;

    // Look up the appropriate animation for the current combat phase
    std::string targetAnim;

    switch (combatState.phase) {
        case CombatPhase::WindUp:
        case CombatPhase::Active:
        case CombatPhase::Recovery:
            switch (combatState.currentAttack) {
                case AttackType::LightHorizontal:
                case AttackType::LightVertical:
                    targetAnim = lightAttackAnim_;
                    break;
                case AttackType::HeavyHorizontal:
                case AttackType::HeavyVertical:
                    targetAnim = heavyAttackAnim_;
                    break;
                case AttackType::Thrust:
                    targetAnim = lightAttackAnim_; // Fallback
                    break;
            }
            break;

        case CombatPhase::Blocking:
        case CombatPhase::Parrying:
            targetAnim = blockAnim_;
            break;

        case CombatPhase::HitStagger:
            targetAnim = hitReactFrontAnim_;
            break;

        case CombatPhase::Dodging:
            targetAnim = dodgeAnim_;
            break;

        default:
            break;
    }

    // Play the animation if found
    if (!targetAnim.empty()) {
        auto it = combatClipIndices_.find(targetAnim);
        if (it != combatClipIndices_.end()) {
            character_->playAnimation(it->second);
        }
    }
}
