#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// Animation event that can be triggered at specific times during playback
struct AnimationEvent {
    std::string name;           // Event identifier (e.g., "footstep_left", "attack_hit")
    float time = 0.0f;          // Time in seconds when the event should fire
    std::string data;           // Optional data (e.g., sound file path, effect name)
    int32_t intData = 0;        // Optional integer data (e.g., damage amount)

    // Comparison for sorting events by time
    bool operator<(const AnimationEvent& other) const {
        return time < other.time;
    }
};

// Built-in event types (passed as name to listeners)
namespace AnimationEvents {
    // Fired when an animation starts playing
    constexpr const char* START = "animation_start";
    // Fired when a non-looping animation completes
    constexpr const char* END = "animation_end";
    // Fired when a looping animation wraps around
    constexpr const char* LOOP = "animation_loop";
    // Fired when state machine transitions to a new state
    constexpr const char* STATE_ENTER = "state_enter";
    // Fired when state machine leaves a state
    constexpr const char* STATE_EXIT = "state_exit";
    // Fired when blend transition starts
    constexpr const char* BLEND_START = "blend_start";
    // Fired when blend transition completes
    constexpr const char* BLEND_END = "blend_end";
}

// Context provided to event listeners
struct AnimationEventContext {
    std::string animationName;  // Name of the animation clip
    std::string stateName;      // Current state name (for state machine)
    float currentTime = 0.0f;   // Current playback time
    float duration = 0.0f;      // Total animation duration
    float normalizedTime = 0.0f; // Time as 0-1 fraction
    void* userData = nullptr;   // Optional user data pointer
};

// Callback signature for animation event listeners
// Parameters: event, context
using AnimationEventCallback = std::function<void(const AnimationEvent&, const AnimationEventContext&)>;

// Interface for receiving animation events
class IAnimationEventListener {
public:
    virtual ~IAnimationEventListener() = default;

    // Called when an animation event is fired
    virtual void onAnimationEvent(const AnimationEvent& event, const AnimationEventContext& context) = 0;
};

// Simple dispatcher that manages multiple listeners
class AnimationEventDispatcher {
public:
    // Add a callback-based listener
    // Returns an ID that can be used to remove the listener
    uint32_t addListener(AnimationEventCallback callback) {
        uint32_t id = nextListenerId++;
        callbacks.push_back({id, std::move(callback)});
        return id;
    }

    // Add an interface-based listener (not owned by dispatcher)
    void addListener(IAnimationEventListener* listener) {
        if (listener) {
            listeners.push_back(listener);
        }
    }

    // Remove a callback listener by ID
    void removeListener(uint32_t id) {
        callbacks.erase(
            std::remove_if(callbacks.begin(), callbacks.end(),
                [id](const CallbackEntry& entry) { return entry.id == id; }),
            callbacks.end()
        );
    }

    // Remove an interface listener
    void removeListener(IAnimationEventListener* listener) {
        listeners.erase(
            std::remove(listeners.begin(), listeners.end(), listener),
            listeners.end()
        );
    }

    // Fire an event to all listeners
    void dispatch(const AnimationEvent& event, const AnimationEventContext& context) {
        for (const auto& entry : callbacks) {
            entry.callback(event, context);
        }
        for (auto* listener : listeners) {
            listener->onAnimationEvent(event, context);
        }
    }

    // Clear all listeners
    void clear() {
        callbacks.clear();
        listeners.clear();
    }

    bool hasListeners() const {
        return !callbacks.empty() || !listeners.empty();
    }

private:
    struct CallbackEntry {
        uint32_t id;
        AnimationEventCallback callback;
    };

    std::vector<CallbackEntry> callbacks;
    std::vector<IAnimationEventListener*> listeners;
    uint32_t nextListenerId = 1;
};
