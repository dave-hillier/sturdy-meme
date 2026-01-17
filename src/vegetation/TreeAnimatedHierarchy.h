#pragma once

#include "../core/AnimatedHierarchy.h"
#include "TreeSkeleton.h"
#include "TreeWindPose.h"
#include <glm/glm.hpp>

// Creates AnimatedHierarchy for a tree with wind animation and LOD blending.
// Combines TreeSkeleton, TreeWindPose, and LODLayerController into a unified system.
class TreeAnimatedHierarchy {
public:
    TreeAnimatedHierarchy() = default;

    // Initialize from a tree skeleton and world position
    void initialize(const TreeSkeleton& skeleton, const glm::vec3& worldPosition) {
        skeleton_ = skeleton;
        worldPosition_ = worldPosition;
        restPose_ = skeleton_.getRestPose();
        currentPose_ = restPose_;

        // Configure LOD layers based on tree structure
        std::vector<int> nodeLevels;
        nodeLevels.reserve(skeleton_.size());
        int maxLevel = 0;
        for (const auto& branch : skeleton_.branches) {
            nodeLevels.push_back(branch.level);
            maxLevel = std::max(maxLevel, branch.level);
        }
        layers_.configureTreeLOD(nodeLevels, maxLevel);

        // Initialize wind pose
        windOscillation_ = {};
    }

    // Update wind animation
    void update(float deltaTime) {
        time_ += deltaTime;
        windParams_.time = time_;

        // Calculate tree-specific oscillation
        windOscillation_ = TreeWindPose::calculateOscillation(worldPosition_, windParams_);

        // Calculate wind pose delta
        HierarchyPose windPose = TreeWindPose::calculateWindPose(skeleton_, windOscillation_, windParams_);

        // Apply wind as additive layer if we have one
        LODLayer* windLayer = layers_.getLayer("wind");
        if (windLayer) {
            windLayer->pose = windPose;
        }

        // Update layer weights based on LOD factor
        layers_.updateLayerWeights();

        // Compute final pose with all layers
        layers_.computeFinalPose(restPose_, currentPose_);
    }

    // Set wind parameters
    void setWindParams(const TreeWindParams& params) {
        windParams_ = params;
        windParams_.time = time_;
    }

    // Set LOD blend factor (0 = full detail, 1 = simplified)
    void setLODFactor(float factor) {
        layers_.setLODBlendFactor(factor);
    }

    // Add wind as an animation layer
    void enableWindLayer(float weight = 1.0f) {
        LODLayer* windLayer = layers_.addLayer("wind");
        windLayer->blendMode = BlendMode::Additive;
        windLayer->weight = weight;
        windLayer->nodeMask = skeleton_.flexibilityMask();
    }

    // Access components
    const TreeSkeleton& skeleton() const { return skeleton_; }
    const HierarchyPose& restPose() const { return restPose_; }
    const HierarchyPose& currentPose() const { return currentPose_; }
    LODLayerController& layers() { return layers_; }
    const LODLayerController& layers() const { return layers_; }

    // Convert to generic AnimatedHierarchy for uniform processing
    AnimatedHierarchy toAnimatedHierarchy() {
        return AnimatedHierarchy(
            [this]() { return skeleton_.size(); },
            [this]() { return restPose_; },
            [this]() { return currentPose_; },
            [this](float dt) { update(dt); }
        );
    }

private:
    TreeSkeleton skeleton_;
    glm::vec3 worldPosition_{0.0f};
    HierarchyPose restPose_;
    HierarchyPose currentPose_;
    LODLayerController layers_;

    TreeWindParams windParams_;
    TreeOscillation windOscillation_;
    float time_{0.0f};
};

