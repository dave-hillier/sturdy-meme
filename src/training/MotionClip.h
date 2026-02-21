#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

// A single frame of motion capture data for the 20-body humanoid.
struct MotionFrame {
    glm::vec3 rootPos{0.0f};
    glm::quat rootRot{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<glm::vec3> jointPositions;   // [numJoints]
    std::vector<glm::quat> jointRotations;   // [numJoints]
};

// A motion clip: sequence of frames at a fixed FPS.
struct MotionClip {
    float fps = 30.0f;
    std::vector<MotionFrame> frames;
    std::string name;

    float duration() const {
        return frames.empty() ? 0.0f : static_cast<float>(frames.size()) / fps;
    }

    // Get interpolated frame at arbitrary time (clamps to range).
    MotionFrame sampleAt(float time) const;
};

// A collection of motion clips for training.
struct MotionLibrary {
    std::vector<MotionClip> clips;
    size_t numJoints = 20;

    // Load all .json motion files from a directory.
    bool loadDirectory(const std::string& dir);

    // Load a single .json motion file.
    bool loadFile(const std::string& path);

    // Generate a default standing clip for training without motion data.
    void addStandingClip(float durationSec = 5.0f, float fps = 30.0f);

    bool empty() const { return clips.empty(); }
    size_t totalFrames() const;
};
