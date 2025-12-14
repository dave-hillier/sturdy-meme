#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <algorithm>

#include "GLTFLoader.h"
#include "AnimationEvent.h"

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
    int32_t rootBoneIndex = -1;  // Index of root bone for root motion extraction
    glm::vec3 rootMotionPerCycle = glm::vec3(0.0f);  // Total root displacement over one cycle
    std::vector<AnimationEvent> events;  // Events to fire during playback

    // Sample all channels at a given time and apply to skeleton
    // If stripRootMotion is true, horizontal (XZ) translation is removed from the root bone
    void sample(float time, Skeleton& skeleton, bool stripRootMotion = true) const;

    // Get the channel for a specific joint (or nullptr if none)
    const AnimationChannel* getChannelForJoint(int32_t jointIndex) const;

    // Calculate root motion speed (units per second)
    float getRootMotionSpeed() const {
        if (duration <= 0.0f) return 0.0f;
        return glm::length(glm::vec2(rootMotionPerCycle.x, rootMotionPerCycle.z)) / duration;
    }

    // Add an event at a specific time (in seconds)
    void addEvent(const std::string& eventName, float time, const std::string& data = "", int32_t intData = 0) {
        AnimationEvent event;
        event.name = eventName;
        event.time = time;
        event.data = data;
        event.intData = intData;
        events.push_back(event);
        // Keep events sorted by time for efficient firing
        std::sort(events.begin(), events.end());
    }

    // Add an event at a normalized time (0.0 to 1.0)
    void addEventNormalized(const std::string& eventName, float normalizedTime, const std::string& data = "", int32_t intData = 0) {
        addEvent(eventName, normalizedTime * duration, data, intData);
    }

    // Get events in a time range (exclusive start, inclusive end)
    // Used to find events that should fire between two time points
    std::vector<const AnimationEvent*> getEventsInRange(float startTime, float endTime) const {
        std::vector<const AnimationEvent*> result;
        for (const auto& event : events) {
            if (event.time > startTime && event.time <= endTime) {
                result.push_back(&event);
            }
        }
        return result;
    }
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
    float getNormalizedTime() const { return currentClip && currentClip->duration > 0.0f ? currentTime / currentClip->duration : 0.0f; }
    bool isPlaying() const { return playing; }
    void play() { playing = true; }
    void pause() { playing = false; }
    void reset() { currentTime = 0.0f; lastEventTime = 0.0f; }

    // Event handling
    AnimationEventDispatcher& getEventDispatcher() { return eventDispatcher; }
    const AnimationEventDispatcher& getEventDispatcher() const { return eventDispatcher; }

    // Set optional user data that will be passed to event callbacks
    void setUserData(void* data) { userData = data; }
    void* getUserData() const { return userData; }

private:
    const AnimationClip* currentClip = nullptr;
    float currentTime = 0.0f;
    float lastEventTime = 0.0f;  // Used to track which events have been fired
    float playbackSpeed = 1.0f;
    bool looping = true;
    bool playing = true;

    AnimationEventDispatcher eventDispatcher;
    void* userData = nullptr;

    // Fire events that occurred between lastEventTime and currentTime
    void fireEvents(float prevTime, float newTime, bool looped);
    // Build context for event firing
    AnimationEventContext buildContext() const;
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

    // Find the two keyframes to interpolate between.
    // Note: This linear search is O(n) but binary search optimization is not necessary
    // because animations typically have few keyframes per channel (< 100) and the
    // constant factor of std::lower_bound would negate benefits at small sizes.
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

    // Linear search - see comment in primary template for rationale
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
