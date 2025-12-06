#include "AnimationEventLoader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <SDL3/SDL_log.h>

using json = nlohmann::json;

namespace {
    // Parse events array from JSON into a clip
    void parseEventsArray(const json& eventsArray, AnimationClip& clip) {
        for (const auto& eventJson : eventsArray) {
            AnimationEvent event;

            if (!eventJson.contains("name")) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "AnimationEventLoader: Event missing 'name' field, skipping");
                continue;
            }

            event.name = eventJson["name"].get<std::string>();

            // Get time - either absolute or normalized
            if (eventJson.contains("time")) {
                event.time = eventJson["time"].get<float>();
            } else if (eventJson.contains("normalizedTime")) {
                float normalizedTime = eventJson["normalizedTime"].get<float>();
                event.time = normalizedTime * clip.duration;
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "AnimationEventLoader: Event '%s' missing time, defaulting to 0",
                    event.name.c_str());
                event.time = 0.0f;
            }

            // Optional data fields
            if (eventJson.contains("data")) {
                event.data = eventJson["data"].get<std::string>();
            }
            if (eventJson.contains("intData")) {
                event.intData = eventJson["intData"].get<int32_t>();
            }

            clip.events.push_back(event);
        }

        // Sort events by time
        std::sort(clip.events.begin(), clip.events.end());
    }

    // Convert events to JSON array
    json eventsToJson(const AnimationClip& clip) {
        json eventsArray = json::array();

        for (const auto& event : clip.events) {
            json eventJson;
            eventJson["name"] = event.name;
            eventJson["time"] = event.time;

            // Also include normalized time for convenience
            if (clip.duration > 0.0f) {
                eventJson["normalizedTime"] = event.time / clip.duration;
            }

            if (!event.data.empty()) {
                eventJson["data"] = event.data;
            }
            if (event.intData != 0) {
                eventJson["intData"] = event.intData;
            }

            eventsArray.push_back(eventJson);
        }

        return eventsArray;
    }
}

bool AnimationEventLoader::loadEventsFromFile(const std::string& jsonPath, AnimationClip& clip) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: Failed to open file '%s'", jsonPath.c_str());
        return false;
    }

    try {
        json root = json::parse(file);

        // Check for events array at root level
        if (root.contains("events") && root["events"].is_array()) {
            parseEventsArray(root["events"], clip);
            SDL_Log("AnimationEventLoader: Loaded %zu events for '%s' from '%s'",
                clip.events.size(), clip.name.c_str(), jsonPath.c_str());
            return true;
        }

        // Check if root contains clip name as key (multi-animation format)
        if (root.contains(clip.name) && root[clip.name].contains("events")) {
            parseEventsArray(root[clip.name]["events"], clip);
            SDL_Log("AnimationEventLoader: Loaded %zu events for '%s' from '%s'",
                clip.events.size(), clip.name.c_str(), jsonPath.c_str());
            return true;
        }

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: No events found in '%s' for clip '%s'",
            jsonPath.c_str(), clip.name.c_str());
        return false;

    } catch (const json::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: JSON parse error in '%s': %s",
            jsonPath.c_str(), e.what());
        return false;
    }
}

int AnimationEventLoader::loadEventsFromFile(const std::string& jsonPath, std::vector<AnimationClip>& clips) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: Failed to open file '%s'", jsonPath.c_str());
        return 0;
    }

    try {
        json root = json::parse(file);
        int loadedCount = 0;

        for (auto& clip : clips) {
            // Check if root contains clip name as key
            if (root.contains(clip.name) && root[clip.name].contains("events")) {
                parseEventsArray(root[clip.name]["events"], clip);
                SDL_Log("AnimationEventLoader: Loaded %zu events for '%s'",
                    clip.events.size(), clip.name.c_str());
                loadedCount++;
            }
        }

        return loadedCount;

    } catch (const json::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: JSON parse error in '%s': %s",
            jsonPath.c_str(), e.what());
        return 0;
    }
}

bool AnimationEventLoader::loadEventsFromString(const std::string& jsonString, AnimationClip& clip) {
    try {
        json root = json::parse(jsonString);

        if (root.contains("events") && root["events"].is_array()) {
            parseEventsArray(root["events"], clip);
            return true;
        }

        if (root.contains(clip.name) && root[clip.name].contains("events")) {
            parseEventsArray(root[clip.name]["events"], clip);
            return true;
        }

        return false;

    } catch (const json::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: JSON parse error: %s", e.what());
        return false;
    }
}

bool AnimationEventLoader::saveEventsToFile(const std::string& jsonPath, const AnimationClip& clip) {
    json root;
    root["animation"] = clip.name;
    root["duration"] = clip.duration;
    root["events"] = eventsToJson(clip);

    std::ofstream file(jsonPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AnimationEventLoader: Failed to create file '%s'", jsonPath.c_str());
        return false;
    }

    file << root.dump(2);  // Pretty print with 2-space indent
    SDL_Log("AnimationEventLoader: Saved %zu events to '%s'",
        clip.events.size(), jsonPath.c_str());
    return true;
}

bool AnimationEventLoader::loadSidecarEvents(const std::string& animationPath, AnimationClip& clip) {
    // Generate sidecar path: animation.fbx -> animation.events.json
    std::filesystem::path path(animationPath);
    std::filesystem::path sidecarPath = path.parent_path() / (path.stem().string() + ".events.json");

    if (!std::filesystem::exists(sidecarPath)) {
        // Try with just the clip name
        sidecarPath = path.parent_path() / (clip.name + ".events.json");
        if (!std::filesystem::exists(sidecarPath)) {
            return false;  // No sidecar file found (this is normal)
        }
    }

    return loadEventsFromFile(sidecarPath.string(), clip);
}

int AnimationEventLoader::loadSidecarEvents(const std::string& animationPath, std::vector<AnimationClip>& clips) {
    // Generate sidecar path: animation.fbx -> animation.events.json
    std::filesystem::path path(animationPath);
    std::filesystem::path sidecarPath = path.parent_path() / (path.stem().string() + ".events.json");

    if (!std::filesystem::exists(sidecarPath)) {
        return 0;  // No sidecar file found (this is normal)
    }

    return loadEventsFromFile(sidecarPath.string(), clips);
}
