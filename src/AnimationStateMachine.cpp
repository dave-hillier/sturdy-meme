#include "AnimationStateMachine.h"
#include "PhysicsSystem.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

void AnimationStateMachine::addState(const std::string& name, const AnimationClip* clip, bool looping) {
    State state;
    state.name = name;
    state.clip = clip;
    state.looping = looping;
    state.time = 0.0f;
    state.speed = 1.0f;
    state.rootMotionSpeed = clip ? clip->getRootMotionSpeed() : 0.0f;
    states.push_back(state);

    if (state.rootMotionSpeed > 0.0f) {
        SDL_Log("AnimationStateMachine: State '%s' has root motion speed %.2f m/s",
                name.c_str(), state.rootMotionSpeed);
    }

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

    // Fire state exit event for previous state
    if (eventDispatcher.hasListeners()) {
        State* prevState = findState(currentState);
        if (prevState && prevState->clip) {
            AnimationEvent exitEvent;
            exitEvent.name = AnimationEvents::STATE_EXIT;
            exitEvent.data = currentState;
            eventDispatcher.dispatch(exitEvent, buildContext(currentState, prevState->clip, prevState->time));
        }
    }

    // Start blending from current state to new state
    previousState = currentState;
    currentState = name;
    newState->time = 0.0f;  // Reset new animation to start
    lastEventTime = 0.0f;   // Reset event tracking for new state
    blendDuration = duration;
    blendTime = 0.0f;
    blendFactor = 0.0f;
    blending = true;

    // Fire blend start and state enter events
    if (eventDispatcher.hasListeners()) {
        AnimationEvent blendStartEvent;
        blendStartEvent.name = AnimationEvents::BLEND_START;
        blendStartEvent.data = name;
        eventDispatcher.dispatch(blendStartEvent, buildContext(name, newState->clip, 0.0f));

        AnimationEvent enterEvent;
        enterEvent.name = AnimationEvents::STATE_ENTER;
        enterEvent.data = name;
        eventDispatcher.dispatch(enterEvent, buildContext(name, newState->clip, 0.0f));
    }
}

void AnimationStateMachine::update(float deltaTime, float movementSpeed, bool isGrounded, bool isJumping) {
    // Update blend factor if blending
    bool blendJustEnded = false;
    if (blending) {
        blendTime += deltaTime;
        blendFactor = blendTime / blendDuration;
        if (blendFactor >= 1.0f) {
            blendFactor = 1.0f;
            blending = false;
            blendJustEnded = true;
        }
    }

    // Fire blend end event
    if (blendJustEnded && eventDispatcher.hasListeners()) {
        State* current = findState(currentState);
        if (current && current->clip) {
            AnimationEvent blendEndEvent;
            blendEndEvent.name = AnimationEvents::BLEND_END;
            blendEndEvent.data = currentState;
            eventDispatcher.dispatch(blendEndEvent, buildContext(currentState, current->clip, current->time));
        }
    }

    // Update animation times for current (and previous if blending) states
    State* current = findState(currentState);

    // Calculate animation speed scaling to match character movement speed
    // This prevents foot sliding by making the animation play faster/slower
    // to match how fast the character is actually moving
    auto calculateSpeedScale = [movementSpeed](const State* state) -> float {
        if (!state || state->rootMotionSpeed <= 0.0f) {
            return 1.0f;  // No root motion data, play at normal speed
        }
        // Scale animation speed to match character movement
        float scale = movementSpeed / state->rootMotionSpeed;
        // Clamp to reasonable range to avoid extreme distortion
        return std::clamp(scale, 0.5f, 2.0f);
    };

    // Special handling for jump animation - sync to trajectory
    if (currentState == jumpStateName && jumpTrajectory.active && current && current->clip) {
        float prevTime = current->time;
        jumpTrajectory.elapsedTime += deltaTime;

        if (jumpTrajectory.predictedDuration > 0.0f && jumpTrajectory.animationDuration > 0.0f) {
            // Map elapsed time to animation time based on predicted arc
            float jumpProgress = std::clamp(jumpTrajectory.elapsedTime / jumpTrajectory.predictedDuration, 0.0f, 1.0f);
            current->time = jumpProgress * jumpTrajectory.animationDuration;
        } else {
            // Fallback to normal time progression
            current->time += deltaTime * current->speed;
        }

        // Fire clip events for jump animation (no looping)
        if (eventDispatcher.hasListeners() && current->time > prevTime) {
            fireClipEvents(current->clip, prevTime, current->time, false, currentState);
        }
    } else if (current && current->clip) {
        // For locomotion animations, scale playback speed to match movement
        float prevTime = current->time;
        float speedScale = 1.0f;
        if (currentState == walkStateName || currentState == runStateName) {
            speedScale = calculateSpeedScale(current);
        }
        current->time += deltaTime * current->speed * speedScale;
        bool looped = false;
        if (current->looping && current->clip->duration > 0.0f) {
            if (current->time >= current->clip->duration) {
                looped = true;
                current->time = std::fmod(current->time, current->clip->duration);
            }
        }

        // Fire clip events for current animation
        if (eventDispatcher.hasListeners()) {
            fireClipEvents(current->clip, prevTime, current->time, looped, currentState);
        }
    }

    if (blending) {
        State* previous = findState(previousState);
        if (previous && previous->clip) {
            // Also scale the previous animation during blending
            float speedScale = 1.0f;
            if (previousState == walkStateName || previousState == runStateName) {
                speedScale = calculateSpeedScale(previous);
            }
            previous->time += deltaTime * previous->speed * speedScale;
            if (previous->looping && previous->clip->duration > 0.0f) {
                previous->time = std::fmod(previous->time, previous->clip->duration);
            }
        }
    }

    // Update blend space if enabled
    if (useBlendSpace && isLocomotionState(currentState)) {
        locomotionBlendSpace.setParameter(movementSpeed);
        locomotionBlendSpace.update(deltaTime);
    }

    // Automatic state transitions based on movement
    if (currentState == jumpStateName) {
        // Check for landing - either expected or early
        if (isGrounded) {
            // Landed - deactivate trajectory and transition based on movement
            jumpTrajectory.active = false;

            // Quick blend to landing animation if we landed early
            float landingBlendDuration = 0.15f;
            if (jumpTrajectory.elapsedTime < jumpTrajectory.predictedDuration * 0.8f) {
                // Early landing - faster blend
                landingBlendDuration = 0.1f;
            }

            if (movementSpeed > runThreshold) {
                transitionTo(runStateName, landingBlendDuration);
            } else if (movementSpeed > walkThreshold) {
                transitionTo(walkStateName, landingBlendDuration);
            } else {
                transitionTo(idleStateName, landingBlendDuration + 0.05f);
            }
        }
    } else if (isJumping) {
        // Started jumping (isJumping is already gated by isGrounded in Application.cpp)
        // Note: startJump() should be called before update() to set up trajectory
        transitionTo(jumpStateName, 0.1f);
    } else if (!useBlendSpace) {
        // Discrete state transitions (only when not using blend space)
        if (movementSpeed > runThreshold) {
            if (currentState != runStateName) {
                transitionTo(runStateName, 0.2f);
            }
        } else if (movementSpeed > walkThreshold) {
            if (currentState != walkStateName) {
                transitionTo(walkStateName, 0.2f);
            }
        } else {
            if (currentState != idleStateName) {
                transitionTo(idleStateName, 0.25f);
            }
        }
    } else {
        // Blend space mode: stay in a generic locomotion state
        // The actual blending is done in applyToSkeleton
        if (!isLocomotionState(currentState)) {
            // Coming from a non-locomotion state (like jump), transition to idle
            transitionTo(idleStateName, 0.2f);
        }
    }
}

void AnimationStateMachine::applyToSkeleton(Skeleton& skeleton) const {
    // Use blend space for locomotion if enabled
    if (useBlendSpace && isLocomotionState(currentState) && !blending) {
        // Sample blend space directly
        if (locomotionBlendSpace.getSampleCount() > 0) {
            // We need a mutable pose to sample into
            SkeletonPose pose;
            pose.resize(skeleton.joints.size());
            locomotionBlendSpace.samplePose(skeleton, pose);

            // Apply pose to skeleton
            for (size_t i = 0; i < skeleton.joints.size(); ++i) {
                glm::mat4 T = glm::translate(glm::mat4(1.0f), pose[i].translation);
                glm::mat4 Rpre = glm::mat4_cast(skeleton.joints[i].preRotation);
                glm::mat4 R = glm::mat4_cast(pose[i].rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), pose[i].scale);
                skeleton.joints[i].localTransform = T * Rpre * R * S;
            }
            return;
        }
    }

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
                // Note: localTransform = T * Rpre * R * S (see Animation.cpp)
                // We need to extract the animated rotation R (without preRotation),
                // interpolate that, then reapply preRotation
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

                // The decomposed rotation is Rpre * R (preRotation combined with animated rotation)
                glm::quat prevCombinedR = glm::quat_cast(prevRotMat);
                glm::quat currCombinedR = glm::quat_cast(currRotMat);

                // Extract the animated rotation by removing preRotation:
                // combinedR = Rpre * R, so R = inverse(Rpre) * combinedR
                const glm::quat& preRot = skeleton.joints[i].preRotation;
                glm::quat preRotInv = glm::inverse(preRot);
                glm::quat prevAnimR = preRotInv * prevCombinedR;
                glm::quat currAnimR = preRotInv * currCombinedR;

                // Interpolate the animated rotations (not the preRotation)
                glm::vec3 blendT = glm::mix(prevT, currT, blendFactor);
                glm::quat blendAnimR = glm::slerp(prevAnimR, currAnimR, blendFactor);
                glm::vec3 blendS = glm::mix(prevS, currS, blendFactor);

                // Rebuild transform: T * Rpre * R * S
                glm::mat4 T = glm::translate(glm::mat4(1.0f), blendT);
                glm::mat4 Rpre = glm::mat4_cast(preRot);
                glm::mat4 R = glm::mat4_cast(blendAnimR);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), blendS);
                skeleton.joints[i].localTransform = T * Rpre * R * S;
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

void AnimationStateMachine::startJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) {
    jumpTrajectory.active = true;
    jumpTrajectory.startPosition = startPos;
    jumpTrajectory.startVelocity = velocity;
    jumpTrajectory.gravity = gravity;
    jumpTrajectory.elapsedTime = 0.0f;

    // Get the jump animation duration
    const State* jumpState = findState(jumpStateName);
    if (jumpState && jumpState->clip) {
        jumpTrajectory.animationDuration = jumpState->clip->duration;
    } else {
        jumpTrajectory.animationDuration = 1.0f;  // Fallback
    }

    // Predict landing time
    jumpTrajectory.predictedDuration = predictLandingTime(startPos, velocity, gravity, physics);

    SDL_Log("Jump started: predicted duration=%.2fs, anim duration=%.2fs",
            jumpTrajectory.predictedDuration, jumpTrajectory.animationDuration);
}

float AnimationStateMachine::predictLandingTime(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) const {
    // Simple parabola calculation as baseline:
    // y(t) = y0 + vy*t - 0.5*g*t^2
    // Landing when y(t) = y0 (or hits ground)
    // 0 = vy*t - 0.5*g*t^2
    // t = 2*vy/g (time to return to starting height)
    float simpleFlightTime = 2.0f * velocity.y / gravity;

    if (!physics) {
        return simpleFlightTime;
    }

    // Trace the parabolic arc to find actual landing point
    // Sample points along the trajectory and raycast downward
    constexpr int NUM_SAMPLES = 16;
    constexpr float MAX_FLIGHT_TIME = 3.0f;  // Cap prediction to reasonable time
    float searchTime = std::min(simpleFlightTime * 1.5f, MAX_FLIGHT_TIME);
    float dt = searchTime / NUM_SAMPLES;

    glm::vec3 prevPos = startPos;

    for (int i = 1; i <= NUM_SAMPLES; ++i) {
        float t = dt * i;

        // Position at time t: p(t) = p0 + v*t + 0.5*a*t^2
        glm::vec3 pos;
        pos.x = startPos.x + velocity.x * t;
        pos.y = startPos.y + velocity.y * t - 0.5f * gravity * t * t;
        pos.z = startPos.z + velocity.z * t;

        // Raycast from previous position to current position
        auto hits = physics->castRayAllHits(prevPos, pos);

        for (const auto& hit : hits) {
            if (hit.hit) {
                // Found collision - interpolate time
                // hit.distance is fraction along the ray
                float segmentTime = dt * hit.distance;
                float landingTime = dt * (i - 1) + segmentTime;

                // Ensure minimum flight time (don't land immediately)
                return std::max(landingTime, 0.2f);
            }
        }

        // Also check if we've gone below starting height without hitting anything
        // (for flat ground case where we might miss the surface)
        if (pos.y < startPos.y - 0.1f) {
            // Raycast straight down from current position
            glm::vec3 downTarget = pos - glm::vec3(0.0f, 2.0f, 0.0f);
            auto downHits = physics->castRayAllHits(pos, downTarget);

            for (const auto& hit : downHits) {
                if (hit.hit && hit.distance < 1.0f) {
                    // Ground is close below - estimate landing time
                    return std::max(t + hit.distance * 0.1f, 0.2f);
                }
            }
        }

        prevPos = pos;
    }

    // No collision found - use simple parabola time
    return std::max(simpleFlightTime, 0.3f);
}

void AnimationStateMachine::fireClipEvents(const AnimationClip* clip, float prevTime, float newTime, bool looped, const std::string& stateName) {
    if (!clip) {
        return;
    }

    AnimationEventContext context = buildContext(stateName, clip, newTime);

    if (looped) {
        // Animation looped - fire loop event and events across the boundary
        AnimationEvent loopEvent;
        loopEvent.name = AnimationEvents::LOOP;
        loopEvent.time = clip->duration;
        eventDispatcher.dispatch(loopEvent, context);

        // Events from prevTime to duration
        auto eventsToEnd = clip->getEventsInRange(prevTime, clip->duration);
        for (const auto* event : eventsToEnd) {
            eventDispatcher.dispatch(*event, context);
        }

        // Events from 0 to newTime
        auto eventsFromStart = clip->getEventsInRange(-0.001f, newTime);
        for (const auto* event : eventsFromStart) {
            eventDispatcher.dispatch(*event, context);
        }
    } else {
        // Normal playback - fire events in range
        auto events = clip->getEventsInRange(prevTime, newTime);
        for (const auto* event : events) {
            eventDispatcher.dispatch(*event, context);
        }
    }
}

AnimationEventContext AnimationStateMachine::buildContext(const std::string& stateName, const AnimationClip* clip, float time) const {
    AnimationEventContext context;
    context.stateName = stateName;
    if (clip) {
        context.animationName = clip->name;
        context.duration = clip->duration;
        context.normalizedTime = clip->duration > 0.0f ? time / clip->duration : 0.0f;
    }
    context.currentTime = time;
    context.userData = userData;
    return context;
}

bool AnimationStateMachine::isLocomotionState(const std::string& stateName) const {
    return stateName == idleStateName ||
           stateName == walkStateName ||
           stateName == runStateName;
}

void AnimationStateMachine::setupLocomotionBlendSpace() {
    locomotionBlendSpace.clear();

    // Find locomotion states and add to blend space based on root motion speed
    const State* idle = findState(idleStateName);
    const State* walk = findState(walkStateName);
    const State* run = findState(runStateName);

    if (idle && idle->clip) {
        locomotionBlendSpace.addSample(0.0f, idle->clip);
        SDL_Log("AnimationStateMachine: Added idle to blend space at speed 0");
    }

    if (walk && walk->clip) {
        float walkSpeed = walk->rootMotionSpeed;
        if (walkSpeed <= 0.0f) walkSpeed = 1.5f;  // Default walk speed
        locomotionBlendSpace.addSample(walkSpeed, walk->clip);
        SDL_Log("AnimationStateMachine: Added walk to blend space at speed %.2f", walkSpeed);
    }

    if (run && run->clip) {
        float runSpeed = run->rootMotionSpeed;
        if (runSpeed <= 0.0f) runSpeed = 4.0f;  // Default run speed
        locomotionBlendSpace.addSample(runSpeed, run->clip);
        SDL_Log("AnimationStateMachine: Added run to blend space at speed %.2f", runSpeed);
    }

    // Enable time synchronization for smooth foot blending
    locomotionBlendSpace.enableTimeSync(true);

    if (locomotionBlendSpace.getSampleCount() > 0) {
        SDL_Log("AnimationStateMachine: Locomotion blend space ready with %zu samples",
                locomotionBlendSpace.getSampleCount());
    }
}
