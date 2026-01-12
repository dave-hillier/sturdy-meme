#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include "Components.h"

// Forward declarations
class AnimatedCharacter;
class Skeleton;

// Animation-ECS Integration System
// Bridges ECS animation components with the AnimatedCharacter system
namespace AnimationECS {

// ============================================================================
// Animation Registry - Maps handles to animation resources
// ============================================================================

class AnimationRegistry {
public:
    // Animation clip info
    struct AnimationInfo {
        std::string name;
        float duration{0.0f};
        bool looping{true};
    };

    // Register an animation clip
    AnimationHandle registerAnimation(const std::string& name, float duration, bool looping = true) {
        AnimationHandle handle = static_cast<AnimationHandle>(animations_.size());
        AnimationInfo info{name, duration, looping};
        animations_.push_back(info);
        nameToHandle_[name] = handle;
        return handle;
    }

    // Find animation by name
    AnimationHandle findAnimation(const std::string& name) const {
        auto it = nameToHandle_.find(name);
        if (it != nameToHandle_.end()) {
            return it->second;
        }
        return InvalidAnimation;
    }

    // Get animation info
    const AnimationInfo* getAnimation(AnimationHandle handle) const {
        if (handle == InvalidAnimation || handle >= animations_.size()) {
            return nullptr;
        }
        return &animations_[handle];
    }

    // Get animation name
    std::string getAnimationName(AnimationHandle handle) const {
        auto* info = getAnimation(handle);
        return info ? info->name : "";
    }

    size_t getAnimationCount() const { return animations_.size(); }

private:
    std::vector<AnimationInfo> animations_;
    std::unordered_map<std::string, AnimationHandle> nameToHandle_;
};

// ============================================================================
// Animator State Machine Updates
// ============================================================================

// Update animator state based on movement and ground state
inline void updateAnimatorState(Animator& animator, float movementSpeed, bool grounded, bool jumping) {
    animator.previousState = animator.currentState;
    animator.movementSpeed = movementSpeed;
    animator.grounded = grounded;
    animator.jumping = jumping;

    // State transition logic
    if (jumping && grounded) {
        animator.currentState = Animator::State::Jump;
        animator.stateTime = 0.0f;
    }
    else if (!grounded && animator.currentState != Animator::State::Jump) {
        animator.currentState = Animator::State::Fall;
    }
    else if (grounded) {
        // Landing detection
        if (animator.currentState == Animator::State::Fall ||
            animator.currentState == Animator::State::Jump) {
            animator.currentState = Animator::State::Land;
            animator.stateTime = 0.0f;
        }
        // Land state timeout
        else if (animator.currentState == Animator::State::Land && animator.stateTime > 0.2f) {
            animator.currentState = (movementSpeed > 0.1f) ? Animator::State::Walk : Animator::State::Idle;
        }
        // Locomotion states
        else if (animator.currentState != Animator::State::Land) {
            if (movementSpeed < 0.1f) {
                animator.currentState = Animator::State::Idle;
            } else if (movementSpeed < 3.0f) {
                animator.currentState = Animator::State::Walk;
            } else {
                animator.currentState = Animator::State::Run;
            }
        }
    }
}

// ============================================================================
// Animation Playback Updates
// ============================================================================

// Update animation state timing
inline void updateAnimationState(AnimationState& state, float deltaTime, const AnimationRegistry& registry) {
    if (!state.playing) return;

    const auto* animInfo = registry.getAnimation(state.currentAnimation);
    if (!animInfo) return;

    // Update playback time
    state.time += deltaTime * state.speed;

    // Handle looping
    if (state.time >= animInfo->duration) {
        if (state.looping) {
            state.time = fmod(state.time, animInfo->duration);
        } else {
            state.time = animInfo->duration;
            state.playing = false;
        }
    }

    // Handle crossfade
    if (state.nextAnimation != InvalidAnimation) {
        state.blendWeight += deltaTime / state.blendDuration;
        if (state.blendWeight >= 1.0f) {
            state.currentAnimation = state.nextAnimation;
            state.nextAnimation = InvalidAnimation;
            state.blendWeight = 0.0f;
            state.time = 0.0f;
        }
    }
}

// Trigger animation crossfade
inline void crossfadeTo(AnimationState& state, AnimationHandle animation, float blendDuration = 0.2f) {
    if (state.currentAnimation == animation) return;

    state.nextAnimation = animation;
    state.blendDuration = blendDuration;
    state.blendWeight = 0.0f;
}

// ============================================================================
// ECS System Updates
// ============================================================================

// Update all animator components in registry
inline void updateAnimators(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Animator>();
    for (auto entity : view) {
        auto& animator = view.get<Animator>(entity);
        animator.stateTime += deltaTime;

        // Update from physics state if available
        bool grounded = registry.all_of<Grounded>(entity);
        float speed = 0.0f;
        if (registry.all_of<Velocity>(entity)) {
            auto& vel = registry.get<Velocity>(entity);
            speed = glm::length(glm::vec2(vel.linear.x, vel.linear.z));
        }

        updateAnimatorState(animator, speed, grounded, animator.jumping);
    }
}

// Update all animation states in registry
inline void updateAnimationStates(entt::registry& registry, float deltaTime, const AnimationRegistry& animRegistry) {
    auto view = registry.view<AnimationState>();
    for (auto entity : view) {
        auto& state = view.get<AnimationState>(entity);
        updateAnimationState(state, deltaTime, animRegistry);
    }
}

// ============================================================================
// Foot IK Updates
// ============================================================================

// Type for ground height query function
using GroundQueryFunc = std::function<float(float x, float z)>;

// Update foot IK targets from ground queries
inline void updateFootIK(
    entt::registry& registry,
    const GroundQueryFunc& groundQuery,
    float footHeight = 0.1f)
{
    auto view = registry.view<Transform, FootIK>();
    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& footIK = view.get<FootIK>(entity);

        if (!footIK.enabled) continue;

        // Estimate foot positions (simplified - would need skeleton data for accuracy)
        float footSpread = 0.15f;  // Half the hip width
        glm::vec3 leftFootPos = transform.position + glm::vec3(-footSpread, 0, 0);
        glm::vec3 rightFootPos = transform.position + glm::vec3(footSpread, 0, 0);

        // Query ground height at foot positions
        float leftGround = groundQuery(leftFootPos.x, leftFootPos.z);
        float rightGround = groundQuery(rightFootPos.x, rightFootPos.z);

        // Set IK targets
        footIK.leftFoot.position = glm::vec3(leftFootPos.x, leftGround + footHeight, leftFootPos.z);
        footIK.leftFoot.active = true;

        footIK.rightFoot.position = glm::vec3(rightFootPos.x, rightGround + footHeight, rightFootPos.z);
        footIK.rightFoot.active = true;

        // Calculate pelvis offset (keep hips level-ish)
        float avgGround = (leftGround + rightGround) * 0.5f;
        float currentY = transform.position.y;
        footIK.pelvisOffset = avgGround - currentY;
    }
}

// ============================================================================
// Look-at IK Updates
// ============================================================================

// Update look-at IK targets
inline void updateLookAtIK(entt::registry& registry) {
    auto view = registry.view<Transform, LookAtIK>();
    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& lookAt = view.get<LookAtIK>(entity);

        if (!lookAt.enabled) continue;

        // If looking at another entity, get its position
        if (lookAt.target != entt::null && registry.valid(lookAt.target)) {
            if (registry.all_of<Transform>(lookAt.target)) {
                lookAt.targetPosition = registry.get<Transform>(lookAt.target).position;
            }
        }

        // Calculate look direction and clamp
        glm::vec3 toTarget = lookAt.targetPosition - transform.position;
        if (glm::length(toTarget) > 0.001f) {
            toTarget = glm::normalize(toTarget);

            // Calculate yaw/pitch to target
            float targetYaw = glm::degrees(atan2(toTarget.x, toTarget.z));
            float targetPitch = glm::degrees(asin(toTarget.y));

            // Clamp to limits
            float relativeYaw = targetYaw - transform.yaw;
            while (relativeYaw > 180.0f) relativeYaw -= 360.0f;
            while (relativeYaw < -180.0f) relativeYaw += 360.0f;

            relativeYaw = glm::clamp(relativeYaw, -lookAt.maxYaw, lookAt.maxYaw);
            targetPitch = glm::clamp(targetPitch, -lookAt.maxPitch, lookAt.maxPitch);

            // Store clamped target for IK solver
            float clampedYaw = transform.yaw + relativeYaw;
            lookAt.targetPosition = transform.position +
                glm::vec3(sin(glm::radians(clampedYaw)) * cos(glm::radians(targetPitch)),
                          sin(glm::radians(targetPitch)),
                          cos(glm::radians(clampedYaw)) * cos(glm::radians(targetPitch)));
        }
    }
}

// ============================================================================
// Entity Factory Functions
// ============================================================================

// Create an animated character entity
inline entt::entity createAnimatedEntity(
    entt::registry& registry,
    const glm::vec3& position,
    const std::string& name = "AnimatedEntity")
{
    auto entity = registry.create();

    // Transform
    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<ModelMatrix>(entity);
    registry.emplace<WorldTransform>(entity);

    // Animation
    registry.emplace<SkinnedMeshRenderer>(entity);
    registry.emplace<AnimationState>(entity);
    registry.emplace<Animator>(entity);

    // IK
    registry.emplace<FootIK>(entity);
    registry.emplace<LookAtIK>(entity);

    // Scene graph
    EntityInfo info;
    info.name = name;
    info.icon = "A";  // Animated icon
    registry.emplace<EntityInfo>(entity, info);

    // Render layer
    MeshRenderer meshComp;
    meshComp.layer = RenderLayer::Character;
    registry.emplace<MeshRenderer>(entity, meshComp);

    return entity;
}

// Create a player entity with all required components
inline entt::entity createPlayerEntity(
    entt::registry& registry,
    const glm::vec3& position)
{
    auto entity = createAnimatedEntity(registry, position, "Player");

    // Player-specific components
    registry.emplace<PlayerTag>(entity);
    registry.emplace<PlayerMovement>(entity);
    registry.emplace<Velocity>(entity);

    return entity;
}

// Create an NPC entity with animation
inline entt::entity createNPCEntity(
    entt::registry& registry,
    const glm::vec3& position,
    const std::string& name = "NPC")
{
    auto entity = createAnimatedEntity(registry, position, name);

    // NPC-specific components
    registry.emplace<NPCTag>(entity);
    registry.emplace<AIState>(entity);
    registry.emplace<MovementSettings>(entity);
    registry.emplace<Health>(entity);

    return entity;
}

// ============================================================================
// Animation Queries
// ============================================================================

// Get all animated entities
inline std::vector<entt::entity> getAnimatedEntities(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<SkinnedMeshRenderer>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get entities in specific animation state
inline std::vector<entt::entity> getEntitiesInState(entt::registry& registry, Animator::State state) {
    std::vector<entt::entity> result;
    auto view = registry.view<Animator>();
    for (auto entity : view) {
        if (view.get<Animator>(entity).currentState == state) {
            result.push_back(entity);
        }
    }
    return result;
}

// ============================================================================
// Debug Utilities
// ============================================================================

// Get animator state name
inline const char* getStateName(Animator::State state) {
    switch (state) {
        case Animator::State::Idle: return "Idle";
        case Animator::State::Walk: return "Walk";
        case Animator::State::Run: return "Run";
        case Animator::State::Jump: return "Jump";
        case Animator::State::Fall: return "Fall";
        case Animator::State::Land: return "Land";
        case Animator::State::Custom: return "Custom";
        default: return "Unknown";
    }
}

// Animation stats
struct AnimationStats {
    int animatedEntities;
    int playingAnimations;
    int ikEnabled;
};

inline AnimationStats getAnimationStats(entt::registry& registry) {
    AnimationStats stats{0, 0, 0};

    auto animView = registry.view<SkinnedMeshRenderer>();
    stats.animatedEntities = static_cast<int>(animView.size_hint());

    auto stateView = registry.view<AnimationState>();
    for (auto entity : stateView) {
        if (stateView.get<AnimationState>(entity).playing) {
            stats.playingAnimations++;
        }
    }

    auto footIKView = registry.view<FootIK>();
    for (auto entity : footIKView) {
        if (footIKView.get<FootIK>(entity).enabled) {
            stats.ikEnabled++;
        }
    }

    return stats;
}

}  // namespace AnimationECS
