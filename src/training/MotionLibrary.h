#pragma once

#include "MotionFrame.h"
#include "../animation/Animation.h"
#include "../loaders/GLTFLoader.h"

#include <vector>
#include <string>
#include <random>

namespace training {

// Loads FBX animation files via FBXLoader and provides random MotionFrame
// sampling for training episode resets and reference motion data.
//
// Usage:
//   MotionLibrary lib;
//   lib.loadFromDirectory("assets/characters/fbx/", skeleton);
//   MotionFrame frame = lib.sampleRandomFrame(rng);
//   env.reset(frame);
class MotionLibrary {
public:
    MotionLibrary() = default;

    // Load all FBX files from a directory (recursively).
    // Returns number of clips loaded.
    int loadFromDirectory(const std::string& directory, const Skeleton& skeleton);

    // Load a single FBX file. Returns number of clips loaded from it.
    int loadFile(const std::string& path, const Skeleton& skeleton);

    // Sample a random MotionFrame from a random clip at a random time.
    // The skeleton is used to compute FK (global joint positions).
    MotionFrame sampleRandomFrame(std::mt19937& rng, const Skeleton& skeleton) const;

    // Sample a MotionFrame from a specific clip at a specific time.
    MotionFrame sampleFrame(int clipIndex, float time, const Skeleton& skeleton) const;

    // Number of loaded clips.
    int numClips() const { return static_cast<int>(clips_.size()); }

    // Total duration of all clips (for weighted sampling).
    float totalDuration() const { return totalDuration_; }

    // Get clip name.
    const std::string& clipName(int index) const { return clips_[index].name; }

    // Get clip duration.
    float clipDuration(int index) const { return clips_[index].duration; }

    // Check if any clips are loaded.
    bool empty() const { return clips_.empty(); }

private:
    std::vector<AnimationClip> clips_;
    float totalDuration_ = 0.0f;

    // Convert a sampled skeleton pose to a MotionFrame.
    static MotionFrame poseToMotionFrame(
        const Skeleton& skeleton,
        const std::vector<glm::mat4>& globalTransforms,
        int rootBoneIndex);
};

} // namespace training
