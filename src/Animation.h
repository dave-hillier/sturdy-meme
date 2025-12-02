#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

#include "GLTFLoader.h"

// Keyframe data for a single transform component
template<typename T>
struct AnimationSampler {
    std::vector<float> times;
    std::vector<T> values;

    // Sample the value at a given time using linear interpolation
    T sample(float time) const;
};

// Animation channel targeting a specific joint's transform
struct AnimationChannel {
    int32_t jointIndex;
    AnimationSampler<glm::vec3> translation;
    AnimationSampler<glm::quat> rotation;
    AnimationSampler<glm::vec3> scale;

    bool hasTranslation() const { return !translation.times.empty(); }
    bool hasRotation() const { return !rotation.times.empty(); }
    bool hasScale() const { return !scale.times.empty(); }
};

// A single animation clip (e.g., "walk", "idle", "jump")
struct AnimationClip {
    std::string name;
    float duration;
    std::vector<AnimationChannel> channels;

    // Sample all channels at a given time and apply to skeleton
    void sample(float time, Skeleton& skeleton) const;

    // Get the channel for a specific joint (or nullptr if none)
    const AnimationChannel* getChannelForJoint(int32_t jointIndex) const;
};

// Simple animation player for a single clip
class AnimationPlayer {
public:
    AnimationPlayer() = default;

    void setAnimation(const AnimationClip* clip);
    void setPlaybackSpeed(float speed) { playbackSpeed = speed; }
    void setLooping(bool loop) { looping = loop; }

    void update(float deltaTime);
    void applyToSkeleton(Skeleton& skeleton) const;

    float getCurrentTime() const { return currentTime; }
    float getDuration() const { return currentClip ? currentClip->duration : 0.0f; }
    bool isPlaying() const { return playing; }
    void play() { playing = true; }
    void pause() { playing = false; }
    void reset() { currentTime = 0.0f; }

private:
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool looping = true;
    bool playing = true;
};

// Template implementations
template<typename T>
T AnimationSampler<T>::sample(float time) const {
    if (times.empty()) {
        return T{};
    }

    // Clamp time to valid range
    if (time <= times.front()) {
        return values.front();
    }
    if (time >= times.back()) {
        return values.back();
    }

    // Find the two keyframes to interpolate between
    size_t nextIndex = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        if (times[i] > time) {
            nextIndex = i;
            break;
        }
    }
    size_t prevIndex = nextIndex - 1;

    // Calculate interpolation factor
    float prevTime = times[prevIndex];
    float nextTime = times[nextIndex];
    float t = (time - prevTime) / (nextTime - prevTime);

    // Linear interpolation (will be specialized for quaternions)
    return glm::mix(values[prevIndex], values[nextIndex], t);
}

// Specialization for quaternion interpolation (SLERP)
template<>
inline glm::quat AnimationSampler<glm::quat>::sample(float time) const {
    if (times.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    if (time <= times.front()) {
        return values.front();
    }
    if (time >= times.back()) {
        return values.back();
    }

    size_t nextIndex = 0;
    for (size_t i = 0; i < times.size(); ++i) {
        if (times[i] > time) {
            nextIndex = i;
            break;
        }
    }
    size_t prevIndex = nextIndex - 1;

    float prevTime = times[prevIndex];
    float nextTime = times[nextIndex];
    float t = (time - prevTime) / (nextTime - prevTime);

    // Use SLERP for quaternion interpolation
    return glm::slerp(values[prevIndex], values[nextIndex], t);
}
