#pragma once

#include "Animation.h"
#include "AnimationBlend.h"
#include "GLTFLoader.h"
#include <vector>
#include <string>
#include <algorithm>

// BlendSpace1D blends between animations based on a single parameter
// Common uses: walk/run blending based on speed, lean left/right based on turn rate
//
// Example:
//   BlendSpace1D locomotion;
//   locomotion.addSample(0.0f, idleClip);     // Parameter 0 = idle
//   locomotion.addSample(1.5f, walkClip);     // Parameter 1.5 = walk
//   locomotion.addSample(4.0f, runClip);      // Parameter 4.0 = run
//   locomotion.setParameter(speed);            // Set current speed
//   locomotion.sample(skeleton, pose);         // Get blended pose
//
class BlendSpace1D {
public:
    struct Sample {
        float position;           // Parameter value for this sample
        const AnimationClip* clip;
        float time = 0.0f;        // Current playback time
        float playbackSpeed = 1.0f;
    };

    BlendSpace1D() = default;

    // Add an animation sample at a parameter position
    void addSample(float position, const AnimationClip* clip);

    // Set the current parameter value
    void setParameter(float value) { parameter = value; }
    float getParameter() const { return parameter; }

    // Update playback times (call each frame)
    void update(float deltaTime);

    // Sample the blended pose at current parameter
    void samplePose(const Skeleton& bindPose, SkeletonPose& outPose) const;

    // Synchronize animation times based on normalized time
    // This prevents foot sliding when blending locomotion animations
    void enableTimeSync(bool enable) { syncTime = enable; }
    bool isTimeSyncEnabled() const { return syncTime; }

    // Get the number of samples
    size_t getSampleCount() const { return samples.size(); }

    // Clear all samples
    void clear() { samples.clear(); }

    // Get range of parameter
    float getMinParameter() const;
    float getMaxParameter() const;

private:
    std::vector<Sample> samples;
    float parameter = 0.0f;
    bool syncTime = true;  // Sync animation times for smooth blending

    // Find the two samples to blend between
    void findBlendSamples(size_t& outLower, size_t& outUpper, float& outBlend) const;

    // Sample a single clip into a pose
    void sampleClipToPose(const AnimationClip* clip, float time,
                          const Skeleton& bindPose, SkeletonPose& outPose) const;
};

// BlendSpace2D blends between animations based on two parameters
// Common uses: directional movement (forward/strafe), aiming (pitch/yaw)
//
// Example:
//   BlendSpace2D movement;
//   movement.addSample(0, 0, idleClip);       // Center
//   movement.addSample(0, 1, forwardClip);    // Forward
//   movement.addSample(0, -1, backwardClip);  // Backward
//   movement.addSample(-1, 0, strafeLeftClip);
//   movement.addSample(1, 0, strafeRightClip);
//   movement.setParameters(strafeDir, forwardDir);
//   movement.sample(skeleton, pose);
//
class BlendSpace2D {
public:
    struct Sample {
        glm::vec2 position;       // 2D parameter position
        const AnimationClip* clip;
        float time = 0.0f;
        float playbackSpeed = 1.0f;
    };

    BlendSpace2D() = default;

    // Add an animation sample at a 2D position
    void addSample(float x, float y, const AnimationClip* clip);
    void addSample(const glm::vec2& position, const AnimationClip* clip);

    // Set the current parameters
    void setParameters(float x, float y) { parameters = glm::vec2(x, y); }
    void setParameters(const glm::vec2& params) { parameters = params; }
    glm::vec2 getParameters() const { return parameters; }

    // Update playback times
    void update(float deltaTime);

    // Sample the blended pose
    void samplePose(const Skeleton& bindPose, SkeletonPose& outPose) const;

    // Sync settings
    void enableTimeSync(bool enable) { syncTime = enable; }
    bool isTimeSyncEnabled() const { return syncTime; }

    // Sample management
    size_t getSampleCount() const { return samples.size(); }
    void clear() { samples.clear(); }

private:
    std::vector<Sample> samples;
    glm::vec2 parameters{0.0f};
    bool syncTime = true;

    // Compute blend weights for all samples using inverse distance weighting
    void computeBlendWeights(std::vector<float>& outWeights) const;

    // Sample a single clip into a pose
    void sampleClipToPose(const AnimationClip* clip, float time,
                          const Skeleton& bindPose, SkeletonPose& outPose) const;
};
