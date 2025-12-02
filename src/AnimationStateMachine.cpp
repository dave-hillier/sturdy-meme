#include "AnimationStateMachine.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

void AnimationStateMachine::addState(const std::string& name, const AnimationClip* clip, bool looping) {
    State state;
    state.name = name;
    state.clip = clip;
    state.looping = looping;
    state.time = 0.0f;
    state.speed = 1.0f;
    states.push_back(state);

    // Set as current state if this is the first one
    if (states.size() == 1) {
        currentState = name;
    }
}

void AnimationStateMachine::setState(const std::string& name) {
    State* state = findState(name);
    if (!state) {
        SDL_Log("AnimationStateMachine: State '%s' not found", name.c_str());
        return;
    }

    currentState = name;
    state->time = 0.0f;
    blending = false;
    blendFactor = 1.0f;
}

void AnimationStateMachine::transitionTo(const std::string& name, float duration) {
    if (name == currentState) {
        return;  // Already in this state
    }

    State* newState = findState(name);
    if (!newState) {
        SDL_Log("AnimationStateMachine: State '%s' not found for transition", name.c_str());
        return;
    }

    // Start blending from current state to new state
    previousState = currentState;
    currentState = name;
    newState->time = 0.0f;  // Reset new animation to start
    blendDuration = duration;
    blendTime = 0.0f;
    blendFactor = 0.0f;
    blending = true;
}

void AnimationStateMachine::update(float deltaTime, float movementSpeed, bool isGrounded, bool isJumping, float turnAngle) {
    // Update blend factor if blending
    if (blending) {
        blendTime += deltaTime;
        blendFactor = blendTime / blendDuration;
        if (blendFactor >= 1.0f) {
            blendFactor = 1.0f;
            blending = false;
        }
    }

    // Update animation times for current (and previous if blending) states
    State* current = findState(currentState);
    if (current && current->clip) {
        current->time += deltaTime * current->speed;
        if (current->looping && current->clip->duration > 0.0f) {
            current->time = std::fmod(current->time, current->clip->duration);
        }
    }

    if (blending) {
        State* previous = findState(previousState);
        if (previous && previous->clip) {
            previous->time += deltaTime * previous->speed;
            if (previous->looping && previous->clip->duration > 0.0f) {
                previous->time = std::fmod(previous->time, previous->clip->duration);
            }
        }
    }

    // Automatic state transitions based on movement
    if (currentState == "jump") {
        // Wait for jump animation to finish or land
        if (isGrounded && current && current->time > 0.3f) {
            // Landed - transition based on movement
            if (movementSpeed > RUN_THRESHOLD) {
                transitionTo("run", 0.15f);
            } else if (movementSpeed > WALK_THRESHOLD) {
                transitionTo("walk", 0.15f);
            } else {
                transitionTo("idle", 0.2f);
            }
        }
    } else if (currentState == "turn_left" || currentState == "turn_right" || currentState == "turn_180") {
        // Check if turn animation is complete
        if (current && current->clip && current->time >= current->clip->duration * 0.8f) {
            isTurning = false;
            // Transition back to locomotion based on movement
            if (movementSpeed > RUN_THRESHOLD) {
                transitionTo("run", 0.15f);
            } else if (movementSpeed > WALK_THRESHOLD) {
                transitionTo("walk", 0.15f);
            } else {
                transitionTo("idle", 0.2f);
            }
        }
    } else if (isJumping) {
        // Started jumping (isJumping is already gated by isGrounded in Application.cpp)
        isTurning = false;
        transitionTo("jump", 0.1f);
    } else if (movementSpeed > WALK_THRESHOLD && !isTurning) {
        // Check for turn while moving (only trigger if not already turning)
        float absTurnAngle = std::abs(turnAngle);
        if (absTurnAngle > TURN_180_THRESHOLD && findState("turn_180")) {
            isTurning = true;
            transitionTo("turn_180", 0.1f);
        } else if (absTurnAngle > TURN_THRESHOLD) {
            if (turnAngle > 0 && findState("turn_right")) {
                isTurning = true;
                transitionTo("turn_right", 0.1f);
            } else if (turnAngle < 0 && findState("turn_left")) {
                isTurning = true;
                transitionTo("turn_left", 0.1f);
            }
        } else {
            // Normal locomotion transition
            if (movementSpeed > RUN_THRESHOLD) {
                if (currentState != "run") {
                    transitionTo("run", 0.2f);
                }
            } else {
                if (currentState != "walk") {
                    transitionTo("walk", 0.2f);
                }
            }
        }
    } else if (movementSpeed > RUN_THRESHOLD) {
        if (currentState != "run") {
            transitionTo("run", 0.2f);
        }
    } else if (movementSpeed > WALK_THRESHOLD) {
        if (currentState != "walk") {
            transitionTo("walk", 0.2f);
        }
    } else {
        if (currentState != "idle") {
            isTurning = false;
            transitionTo("idle", 0.25f);
        }
    }
}

void AnimationStateMachine::applyToSkeleton(Skeleton& skeleton) const {
    const State* current = findState(currentState);
    if (!current || !current->clip) {
        return;
    }

    if (blending && blendFactor < 1.0f) {
        const State* previous = findState(previousState);
        if (previous && previous->clip) {
            // Sample both animations and blend
            // First, apply previous animation
            previous->clip->sample(previous->time, skeleton);

            // Store the previous transforms
            std::vector<glm::mat4> prevTransforms(skeleton.joints.size());
            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                prevTransforms[i] = skeleton.joints[i].localTransform;
            }

            // Apply current animation
            current->clip->sample(current->time, skeleton);

            // Blend between previous and current
            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                // Decompose both transforms
                glm::vec3 prevT = glm::vec3(prevTransforms[i][3]);
                glm::vec3 currT = glm::vec3(skeleton.joints[i].localTransform[3]);

                glm::vec3 prevS, currS;
                prevS.x = glm::length(glm::vec3(prevTransforms[i][0]));
                prevS.y = glm::length(glm::vec3(prevTransforms[i][1]));
                prevS.z = glm::length(glm::vec3(prevTransforms[i][2]));
                currS.x = glm::length(glm::vec3(skeleton.joints[i].localTransform[0]));
                currS.y = glm::length(glm::vec3(skeleton.joints[i].localTransform[1]));
                currS.z = glm::length(glm::vec3(skeleton.joints[i].localTransform[2]));

                glm::mat3 prevRotMat(
                    glm::vec3(prevTransforms[i][0]) / prevS.x,
                    glm::vec3(prevTransforms[i][1]) / prevS.y,
                    glm::vec3(prevTransforms[i][2]) / prevS.z
                );
                glm::mat3 currRotMat(
                    glm::vec3(skeleton.joints[i].localTransform[0]) / currS.x,
                    glm::vec3(skeleton.joints[i].localTransform[1]) / currS.y,
                    glm::vec3(skeleton.joints[i].localTransform[2]) / currS.z
                );
                glm::quat prevR = glm::quat_cast(prevRotMat);
                glm::quat currR = glm::quat_cast(currRotMat);

                // Interpolate
                glm::vec3 blendT = glm::mix(prevT, currT, blendFactor);
                glm::quat blendR = glm::slerp(prevR, currR, blendFactor);
                glm::vec3 blendS = glm::mix(prevS, currS, blendFactor);

                // Rebuild transform
                glm::mat4 T = glm::translate(glm::mat4(1.0f), blendT);
                glm::mat4 R = glm::mat4_cast(blendR);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), blendS);
                skeleton.joints[i].localTransform = T * R * S;
            }
            return;
        }
    }

    // No blending, just apply current animation
    current->clip->sample(current->time, skeleton);
}

AnimationStateMachine::State* AnimationStateMachine::findState(const std::string& name) {
    for (auto& state : states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}

const AnimationStateMachine::State* AnimationStateMachine::findState(const std::string& name) const {
    for (const auto& state : states) {
        if (state.name == name) {
            return &state;
        }
    }
    return nullptr;
}
