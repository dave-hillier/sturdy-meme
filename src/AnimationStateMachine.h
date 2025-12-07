#pragma once

#include "Animation.h"
#include "AnimationEvent.h"
#include "BlendSpace.h"
#include <string>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

class PhysicsWorld;

// Jump trajectory prediction for syncing animation to physics arc
struct JumpTrajectory {
    bool active = false;
    glm::vec3 startPosition{0.0f};
    glm::vec3 startVelocity{0.0f};
    float gravity = 9.81f;
    float predictedDuration = 0.0f;  // Predicted flight time from raycasting
    float elapsedTime = 0.0f;        // Time since jump started
    float animationDuration = 0.0f;  // Duration of the jump animation clip
};

// Animation state machine for blending between animations based on conditions
class AnimationStateMachine {
public:
    AnimationStateMachine() = default;

    // Add an animation state
    void addState(const std::string& name, const AnimationClip* clip, bool looping = true);

    // Set the current state (immediate, no blend)
    void setState(const std::string& name);

    // Transition to a new state with crossfade blending
    void transitionTo(const std::string& name, float blendDuration = 0.2f);

    // Update the state machine (call each frame)
    // movementSpeed: horizontal movement speed of the character
    // isGrounded: whether the character is on the ground
    // isJumping: whether the character just started a jump
    void update(float deltaTime, float movementSpeed, bool isGrounded, bool isJumping);

    // Start a jump with trajectory prediction
    // startPos: character position at jump start
    // velocity: initial velocity (including jump impulse)
    // physics: physics world for raycasting (can be nullptr for simple parabola)
    void startJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics);

    // Apply the current animation state to a skeleton
    void applyToSkeleton(Skeleton& skeleton) const;

    // Get current state info
    const std::string& getCurrentStateName() const { return currentState; }
    float getBlendFactor() const { return blendFactor; }
    bool isBlending() const { return blending; }

    // Event handling
    AnimationEventDispatcher& getEventDispatcher() { return eventDispatcher; }
    const AnimationEventDispatcher& getEventDispatcher() const { return eventDispatcher; }

    // Set optional user data that will be passed to event callbacks
    void setUserData(void* data) { userData = data; }
    void* getUserData() const { return userData; }

    // ========== Blend Space Mode ==========
    // When enabled, locomotion (idle/walk/run) uses smooth blend space interpolation
    // instead of discrete state transitions

    // Enable blend space mode for locomotion blending
    void setUseBlendSpace(bool use) { useBlendSpace = use; }
    bool isUsingBlendSpace() const { return useBlendSpace; }

    // Get the locomotion blend space for configuration
    BlendSpace1D& getLocomotionBlendSpace() { return locomotionBlendSpace; }
    const BlendSpace1D& getLocomotionBlendSpace() const { return locomotionBlendSpace; }

    // Setup locomotion blend space from registered states
    // Call after adding idle, walk, run states
    void setupLocomotionBlendSpace();

    // ========== Configurable Thresholds ==========

    // Set speed thresholds for locomotion state transitions
    void setWalkThreshold(float threshold) { walkThreshold = threshold; }
    void setRunThreshold(float threshold) { runThreshold = threshold; }
    float getWalkThreshold() const { return walkThreshold; }
    float getRunThreshold() const { return runThreshold; }

    // ========== Locomotion State Names ==========
    // Configure which state names are treated as locomotion states
    // This allows custom state naming (e.g., "locomotion_idle" instead of "idle")

    void setIdleStateName(const std::string& name) { idleStateName = name; }
    void setWalkStateName(const std::string& name) { walkStateName = name; }
    void setRunStateName(const std::string& name) { runStateName = name; }
    void setJumpStateName(const std::string& name) { jumpStateName = name; }

    const std::string& getIdleStateName() const { return idleStateName; }
    const std::string& getWalkStateName() const { return walkStateName; }
    const std::string& getRunStateName() const { return runStateName; }
    const std::string& getJumpStateName() const { return jumpStateName; }

private:
    struct State {
        std::string name;
        const AnimationClip* clip;
        bool looping;
        float time;
        float speed;
        float rootMotionSpeed;  // Animation's natural movement speed (m/s)
    };

    State* findState(const std::string& name);
    const State* findState(const std::string& name) const;

    std::vector<State> states;
    std::string currentState;
    std::string previousState;

    float blendFactor = 1.0f;      // 1.0 = fully in current state, 0.0 = fully in previous
    float blendDuration = 0.2f;
    float blendTime = 0.0f;
    bool blending = false;

    // Configurable thresholds for state transitions
    float walkThreshold = 0.1f;
    float runThreshold = 2.5f;  // Between walk (1.44 m/s) and run (3.98 m/s) animation speeds

    // Configurable locomotion state names
    std::string idleStateName = "idle";
    std::string walkStateName = "walk";
    std::string runStateName = "run";
    std::string jumpStateName = "jump";

    // Blend space mode for smooth locomotion blending
    bool useBlendSpace = false;
    BlendSpace1D locomotionBlendSpace;
    SkeletonPose blendSpacePose;  // Cached pose for blend space sampling

    // Jump trajectory tracking
    JumpTrajectory jumpTrajectory;

    // Check if a state is a locomotion state (idle, walk, or run)
    bool isLocomotionState(const std::string& stateName) const;

    // Event handling
    AnimationEventDispatcher eventDispatcher;
    void* userData = nullptr;
    float lastEventTime = 0.0f;  // Track last event firing time for current state

    // Predict landing time by tracing the parabolic arc
    float predictLandingTime(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) const;

    // Fire events that occurred between prevTime and newTime for a clip
    void fireClipEvents(const AnimationClip* clip, float prevTime, float newTime, bool looped, const std::string& stateName);

    // Build context for event firing
    AnimationEventContext buildContext(const std::string& stateName, const AnimationClip* clip, float time) const;
};
