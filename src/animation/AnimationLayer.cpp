#include "AnimationLayer.h"
#include <cmath>
#include <algorithm>

AnimationLayer::AnimationLayer(const std::string& name)
    : layerName(name) {
}

void AnimationLayer::setAnimation(const AnimationClip* clip, bool loop) {
    currentClip = clip;
    looping = loop;
    currentTime = 0.0f;
    crossfading = false;
    playing = true;
}

float AnimationLayer::getNormalizedTime() const {
    if (!currentClip || currentClip->duration <= 0.0f) {
        return 0.0f;
    }
    return currentTime / currentClip->duration;
}

void AnimationLayer::crossfadeTo(const AnimationClip* newClip, float duration, bool loop) {
    if (newClip == currentClip) {
        return;  // Already playing this animation
    }

    if (!currentClip) {
        // No current animation, just set directly
        setAnimation(newClip, loop);
        return;
    }

    // Start crossfade
    previousClip = currentClip;
    previousTime = currentTime;
    currentClip = newClip;
    currentTime = 0.0f;
    looping = loop;

    crossfading = true;
    crossfadeDuration = duration;
    crossfadeElapsed = 0.0f;
    crossfadeBlend = 0.0f;
}

void AnimationLayer::update(float deltaTime) {
    if (!isEnabled || !playing) {
        return;
    }

    // Update crossfade
    if (crossfading) {
        crossfadeElapsed += deltaTime;
        crossfadeBlend = crossfadeElapsed / crossfadeDuration;
        if (crossfadeBlend >= 1.0f) {
            crossfadeBlend = 1.0f;
            crossfading = false;
            previousClip = nullptr;
        }

        // Also update previous animation time during crossfade
        if (previousClip && previousClip->duration > 0.0f) {
            previousTime += deltaTime * playbackSpeed;
            if (previousTime > previousClip->duration) {
                previousTime = std::fmod(previousTime, previousClip->duration);
            }
        }
    }

    // Update current animation time
    if (currentClip && currentClip->duration > 0.0f) {
        currentTime += deltaTime * playbackSpeed;
        if (looping) {
            currentTime = std::fmod(currentTime, currentClip->duration);
        } else {
            if (currentTime >= currentClip->duration) {
                currentTime = currentClip->duration;
                playing = false;  // Stop at end for non-looping
            }
        }
    }
}

void AnimationLayer::sampleClipToPose(const AnimationClip* clip, float time,
                                       const Skeleton& bindPose, SkeletonPose& outPose) const {
    if (!clip) {
        return;
    }

    outPose.resize(bindPose.joints.size());

    // Start with bind pose for all bones
    for (size_t i = 0; i < bindPose.joints.size(); ++i) {
        outPose[i] = BonePose::fromMatrix(bindPose.joints[i].localTransform,
                                          bindPose.joints[i].preRotation);
    }

    // Apply animation channels
    for (const auto& channel : clip->channels) {
        if (channel.jointIndex < 0 || static_cast<size_t>(channel.jointIndex) >= outPose.size()) {
            continue;
        }

        BonePose& pose = outPose[channel.jointIndex];

        // Sample each component that has animation data
        if (channel.hasTranslation()) {
            pose.translation = channel.translation.sample(time);
        }
        if (channel.hasRotation()) {
            pose.rotation = channel.rotation.sample(time);
        }
        if (channel.hasScale()) {
            pose.scale = channel.scale.sample(time);
        }
    }
}

void AnimationLayer::samplePose(const Skeleton& bindPose, SkeletonPose& outPose) const {
    if (!isEnabled || !currentClip) {
        // Return bind pose if layer is disabled or no animation
        outPose.resize(bindPose.joints.size());
        for (size_t i = 0; i < bindPose.joints.size(); ++i) {
            outPose[i] = BonePose::fromMatrix(bindPose.joints[i].localTransform,
                                              bindPose.joints[i].preRotation);
        }
        return;
    }

    if (crossfading && previousClip) {
        // Sample both animations and blend
        SkeletonPose prevPose, currPose;
        sampleClipToPose(previousClip, previousTime, bindPose, prevPose);
        sampleClipToPose(currentClip, currentTime, bindPose, currPose);

        // Blend between previous and current
        AnimationBlend::blend(prevPose, currPose, crossfadeBlend, outPose);
    } else {
        // Just sample current animation
        sampleClipToPose(currentClip, currentTime, bindPose, outPose);
    }

    // For additive mode, compute the delta from reference pose
    if (blendMode == BlendMode::Additive && hasReferencePose) {
        SkeletonPose delta;
        AnimationBlend::computeAdditiveDelta(referencePose, outPose, delta);
        outPose = delta;
    }
}
