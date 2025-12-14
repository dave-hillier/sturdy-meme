#pragma once

#include "AnimationLayer.h"
#include "AnimationBlend.h"
#include "GLTFLoader.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

// AnimationLayerController manages multiple animation layers
// and blends them together to produce the final skeleton pose.
//
// Layers are processed in order (index 0 first, then 1, etc.)
// Each layer's result is blended onto the accumulated result based on:
// - Layer weight (global blend factor)
// - Bone mask (per-bone blend weights)
// - Blend mode (override replaces, additive adds)
//
// Example usage:
//   controller.addLayer("base");        // Full body locomotion
//   controller.addLayer("upper_body");  // Upper body override (e.g., aiming)
//   controller.addLayer("additive");    // Additive breathing animation
//
//   controller.getLayer("base")->setAnimation(walkClip);
//   controller.getLayer("upper_body")->setAnimation(aimClip);
//   controller.getLayer("upper_body")->setMask(BoneMask::upperBody(skeleton));
//   controller.getLayer("additive")->setAnimation(breatheClip);
//   controller.getLayer("additive")->setBlendMode(BlendMode::Additive);
//
class AnimationLayerController {
public:
    AnimationLayerController() = default;

    // Initialize with a skeleton (stores bind pose for reference)
    void initialize(const Skeleton& skeleton);

    // Layer management
    AnimationLayer* addLayer(const std::string& name);
    void removeLayer(const std::string& name);
    AnimationLayer* getLayer(const std::string& name);
    const AnimationLayer* getLayer(const std::string& name) const;
    AnimationLayer* getLayer(size_t index);
    const AnimationLayer* getLayer(size_t index) const;
    size_t getLayerCount() const { return layers.size(); }

    // Reorder layers (affects blend order)
    void setLayerOrder(size_t layerIndex, size_t newPosition);
    void moveLayerUp(const std::string& name);
    void moveLayerDown(const std::string& name);

    // Update all layers (call each frame)
    void update(float deltaTime);

    // Compute the final blended pose from all layers
    // The result can be applied to a skeleton
    void computeFinalPose(SkeletonPose& outPose) const;

    // Apply the final pose to a skeleton
    void applyToSkeleton(Skeleton& skeleton) const;

    // Quick access to set base layer animation (layer 0)
    void setBaseAnimation(const AnimationClip* clip, bool looping = true);

    // Get layer names (for UI/debugging)
    std::vector<std::string> getLayerNames() const;

    // Get the stored bind pose
    const SkeletonPose& getBindPose() const { return bindPose; }

    // Check if initialized
    bool isInitialized() const { return initialized; }

private:
    std::vector<std::unique_ptr<AnimationLayer>> layers;
    std::unordered_map<std::string, size_t> layerNameToIndex;

    // Cached bind pose (used as base for blending)
    SkeletonPose bindPose;
    std::vector<glm::quat> bindPosePreRotations;  // Store preRotations for matrix reconstruction
    bool initialized = false;

    // Helper to find layer index
    int findLayerIndex(const std::string& name) const;

    // Apply a layer's pose onto the accumulated pose
    void applyLayer(const AnimationLayer& layer, SkeletonPose& accumPose) const;
};
