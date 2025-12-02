#include "Animation.h"
#include <glm/gtc/matrix_transform.hpp>

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
    playing = true;
}

void AnimationPlayer::update(float deltaTime) {
    if (!currentClip || !playing) {
        return;
    }

    currentTime += deltaTime * playbackSpeed;

    if (looping) {
        if (currentTime > currentClip->duration) {
            currentTime = std::fmod(currentTime, currentClip->duration);
        } else if (currentTime < 0.0f) {
            currentTime = currentClip->duration + std::fmod(currentTime, currentClip->duration);
        }
    } else {
        if (currentTime > currentClip->duration) {
            currentTime = currentClip->duration;
            playing = false;
        } else if (currentTime < 0.0f) {
            currentTime = 0.0f;
            playing = false;
        }
    }
}

void AnimationPlayer::applyToSkeleton(Skeleton& skeleton) const {
    if (!currentClip) {
        return;
    }

    currentClip->sample(currentTime, skeleton);
}
