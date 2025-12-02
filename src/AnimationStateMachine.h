#pragma once

#include "Animation.h"
#include <string>
#include <vector>
#include <functional>

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

    // Apply the current animation state to a skeleton
    void applyToSkeleton(Skeleton& skeleton) const;

    // Get current state info
    const std::string& getCurrentStateName() const { return currentState; }
    float getBlendFactor() const { return blendFactor; }
    bool isBlending() const { return blending; }

private:
    struct State {
        std::string name;
        const AnimationClip* clip;
        bool looping;
        float time;
        float speed;
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

    // Thresholds for state transitions
    static constexpr float WALK_THRESHOLD = 0.1f;
    static constexpr float RUN_THRESHOLD = 3.0f;
};
