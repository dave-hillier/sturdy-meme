#include "Animation.h"
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL_log.h>

void AnimationClip::sample(float time, Skeleton& skeleton, bool stripRootMotion) const {
    for (const auto& channel : channels) {
        if (channel.jointIndex < 0 || channel.jointIndex >= static_cast<int32_t>(skeleton.joints.size())) {
            continue;
        }

        Joint& joint = skeleton.joints[channel.jointIndex];

        // Start with current transform (which should be bind pose, reset before sampling)
        // Decompose to get components
        glm::vec3 translation = glm::vec3(joint.localTransform[3]);

        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(joint.localTransform[0]));
        scale.y = glm::length(glm::vec3(joint.localTransform[1]));
        scale.z = glm::length(glm::vec3(joint.localTransform[2]));

        glm::mat3 rotMat(
            glm::vec3(joint.localTransform[0]) / scale.x,
            glm::vec3(joint.localTransform[1]) / scale.y,
            glm::vec3(joint.localTransform[2]) / scale.z
        );
        glm::quat rotation = glm::quat_cast(rotMat);

        // Override with animated values where available
        if (channel.hasTranslation()) {
            translation = channel.translation.sample(time);
        }
        if (channel.hasRotation()) {
            rotation = channel.rotation.sample(time);
        }
        if (channel.hasScale()) {
            scale = channel.scale.sample(time);
        }

        // Strip root motion: zero out horizontal translation for root bone
        // This prevents the animation from moving the character - locomotion handles that
        if (stripRootMotion && channel.jointIndex == rootBoneIndex) {
            translation.x = 0.0f;
            translation.z = 0.0f;
        }

        // Build local transform matrix: T * Rpre * R * S
        // FBX pre-rotation is applied before the animated rotation
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 Rpre = glm::mat4_cast(joint.preRotation);
        glm::mat4 R = glm::mat4_cast(rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

        joint.localTransform = T * Rpre * R * S;
    }
}

const AnimationChannel* AnimationClip::getChannelForJoint(int32_t jointIndex) const {
    for (const auto& channel : channels) {
        if (channel.jointIndex == jointIndex) {
            return &channel;
        }
    }
    return nullptr;
}

void AnimationPlayer::setAnimation(const AnimationClip* clip) {
    currentClip = clip;
    currentTime = 0.0f;
    lastEventTime = 0.0f;
    playing = true;

    // Fire animation start event
    if (clip && eventDispatcher.hasListeners()) {
        AnimationEvent startEvent;
        startEvent.name = AnimationEvents::START;
        startEvent.time = 0.0f;
        eventDispatcher.dispatch(startEvent, buildContext());
    }
}

void AnimationPlayer::update(float deltaTime) {
    if (!currentClip || !playing) {
        return;
    }

    float prevTime = currentTime;
    currentTime += deltaTime * playbackSpeed;
    bool looped = false;

    if (looping) {
        if (currentTime > currentClip->duration) {
            looped = true;
            currentTime = std::fmod(currentTime, currentClip->duration);
        } else if (currentTime < 0.0f) {
            looped = true;
            currentTime = currentClip->duration + std::fmod(currentTime, currentClip->duration);
        }
    } else {
        if (currentTime > currentClip->duration) {
            currentTime = currentClip->duration;
            playing = false;
            // Fire end event for non-looping animations
            if (eventDispatcher.hasListeners()) {
                // Fire any remaining events before the end
                fireEvents(prevTime, currentClip->duration, false);
                AnimationEvent endEvent;
                endEvent.name = AnimationEvents::END;
                endEvent.time = currentClip->duration;
                eventDispatcher.dispatch(endEvent, buildContext());
            }
            lastEventTime = currentTime;
            return;
        } else if (currentTime < 0.0f) {
            currentTime = 0.0f;
            playing = false;
            if (eventDispatcher.hasListeners()) {
                AnimationEvent endEvent;
                endEvent.name = AnimationEvents::END;
                endEvent.time = 0.0f;
                eventDispatcher.dispatch(endEvent, buildContext());
            }
            lastEventTime = currentTime;
            return;
        }
    }

    // Fire events that occurred during this update
    if (eventDispatcher.hasListeners()) {
        fireEvents(prevTime, currentTime, looped);
    }

    lastEventTime = currentTime;
}

void AnimationPlayer::applyToSkeleton(Skeleton& skeleton) const {
    if (!currentClip) {
        return;
    }

    currentClip->sample(currentTime, skeleton);
}

void AnimationPlayer::fireEvents(float prevTime, float newTime, bool looped) {
    if (!currentClip) {
        return;
    }

    AnimationEventContext context = buildContext();

    if (looped) {
        // Animation looped - fire events from prevTime to end, then start to newTime
        // Fire loop event
        AnimationEvent loopEvent;
        loopEvent.name = AnimationEvents::LOOP;
        loopEvent.time = currentClip->duration;
        eventDispatcher.dispatch(loopEvent, context);

        // Events from prevTime to duration
        auto eventsToEnd = currentClip->getEventsInRange(prevTime, currentClip->duration);
        for (const auto* event : eventsToEnd) {
            eventDispatcher.dispatch(*event, context);
        }

        // Events from 0 to newTime
        auto eventsFromStart = currentClip->getEventsInRange(-0.001f, newTime);
        for (const auto* event : eventsFromStart) {
            eventDispatcher.dispatch(*event, context);
        }
    } else {
        // Normal playback - fire events in range
        auto events = currentClip->getEventsInRange(prevTime, newTime);
        for (const auto* event : events) {
            eventDispatcher.dispatch(*event, context);
        }
    }
}

AnimationEventContext AnimationPlayer::buildContext() const {
    AnimationEventContext context;
    if (currentClip) {
        context.animationName = currentClip->name;
        context.duration = currentClip->duration;
    }
    context.currentTime = currentTime;
    context.normalizedTime = getNormalizedTime();
    context.userData = userData;
    return context;
}
