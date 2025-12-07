#pragma once

#include "Animation.h"
#include <string>
#include <filesystem>

// Utility class for loading animation events from JSON files
//
// JSON format:
// {
//   "events": [
//     {
//       "name": "footstep_left",
//       "time": 0.25,           // Time in seconds (optional if normalizedTime is provided)
//       "normalizedTime": 0.25, // Time as 0-1 fraction (optional if time is provided)
//       "data": "sounds/footstep1.wav",  // Optional string data
//       "intData": 100          // Optional integer data
//     },
//     ...
//   ]
// }
//
// Alternative format for multiple animations:
// {
//   "walk": {
//     "events": [...]
//   },
//   "run": {
//     "events": [...]
//   }
// }
class AnimationEventLoader {
public:
    // Load events from a JSON file into an animation clip
    // Returns true on success, false on failure
    static bool loadEventsFromFile(const std::string& jsonPath, AnimationClip& clip);

    // Load events from a JSON file into multiple animation clips
    // The JSON should have animation names as keys
    // Returns number of clips that had events loaded
    static int loadEventsFromFile(const std::string& jsonPath, std::vector<AnimationClip>& clips);

    // Load events from a JSON string into an animation clip
    static bool loadEventsFromString(const std::string& jsonString, AnimationClip& clip);

    // Save events from an animation clip to a JSON file
    // Useful for tools/editors to export event data
    static bool saveEventsToFile(const std::string& jsonPath, const AnimationClip& clip);

    // Try to find and load events for an animation file
    // Looks for a sidecar file: animation.fbx -> animation.events.json
    // Returns true if events were loaded
    static bool loadSidecarEvents(const std::string& animationPath, AnimationClip& clip);

    // Try to load sidecar events for all clips based on a base path
    // Returns number of clips that had events loaded
    static int loadSidecarEvents(const std::string& animationPath, std::vector<AnimationClip>& clips);
};
