#pragma once

#include "Animation.h"
#include "AnimationBlend.h"
#include "BoneMask.h"
#include "GLTFLoader.h"
#include <string>
#include <memory>
#include <functional>

// Animation layer represents a single animation source that can be blended
// with other layers. Supports override and additive blending modes.
class AnimationLayer {
public:
    AnimationLayer() = default;
    explicit AnimationLayer(const std::string& name);

    // Layer configuration
    void setName(const std::string& name) { layerName = name; }
    const std::string& getName() const { return layerName; }

    void setBlendMode(BlendMode mode) { blendMode = mode; }
    BlendMode getBlendMode() const { return blendMode; }

    void setWeight(float w) { weight = std::clamp(w, 0.0f, 1.0f); }
    float getWeight() const { return weight; }

    void setMask(const BoneMask& mask) { boneMask = mask; }
    const BoneMask& getMask() const { return boneMask; }
    BoneMask& getMask() { return boneMask; }

    // Enable/disable the layer
    void setEnabled(bool enabled) { isEnabled = enabled; }
    bool getEnabled() const { return isEnabled; }

    // Animation playback
    void setAnimation(const AnimationClip* clip, bool looping = true);
    const AnimationClip* getAnimation() const { return currentClip; }

    void setPlaybackSpeed(float speed) { playbackSpeed = speed; }
    float getPlaybackSpeed() const { return playbackSpeed; }

    void setLooping(bool loop) { looping = loop; }
    bool isLooping() const { return looping; }

    void play() { playing = true; }
    void pause() { playing = false; }
    void stop() { playing = false; currentTime = 0.0f; }
    void reset() { currentTime = 0.0f; }
    bool isPlaying() const { return playing; }

    float getCurrentTime() const { return currentTime; }
    void setCurrentTime(float time) { currentTime = time; }
    float getDuration() const { return currentClip ? currentClip->duration : 0.0f; }
    float getNormalizedTime() const;

    // Crossfade to a new animation
    void crossfadeTo(const AnimationClip* newClip, float duration = 0.2f, bool looping = true);
    bool isCrossfading() const { return crossfading; }
    float getCrossfadeProgress() const { return crossfadeBlend; }

    // Update layer (call each frame)
    void update(float deltaTime);

    // Sample the layer's current pose
    // Applies the animation to create a pose, handling crossfades internally
    void samplePose(const Skeleton& bindPose, SkeletonPose& outPose) const;

    // For additive layers: set the reference pose used to compute deltas
    void setReferencePose(const SkeletonPose& refPose) { referencePose = refPose; hasReferencePose = true; }
    bool hasReference() const { return hasReferencePose; }

private:
    std::string layerName;
    BlendMode blendMode = BlendMode::Override;
    float weight = 1.0f;
    BoneMask boneMask;
    bool isEnabled = true;

    // Current animation
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool looping = true;
    bool playing = true;

    // Crossfade state
    bool crossfading = false;
    const AnimationClip* previousClip = nullptr;
    float previousTime = 0.0f;
    float crossfadeBlend = 1.0f;  // 0 = previous, 1 = current
    float crossfadeDuration = 0.2f;
    float crossfadeElapsed = 0.0f;

    // Reference pose for additive blending
    SkeletonPose referencePose;
    bool hasReferencePose = false;

    // Helper to sample a single clip into a pose
    void sampleClipToPose(const AnimationClip* clip, float time,
                          const Skeleton& bindPose, SkeletonPose& outPose) const;
};
