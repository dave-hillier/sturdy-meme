#pragma once

#include "HierarchicalPose.h"
#include "LODLayerController.h"
#include <functional>
#include <memory>

// Type-erased animated hierarchy that works with any hierarchical structure.
// Uses composition rather than inheritance - wraps pose generation callbacks
// rather than requiring derived classes.
//
// This enables uniform treatment of:
// - Skeletal characters with bone animations
// - Trees with wind/LOD animation
// - Any hierarchical pose system
class AnimatedHierarchy {
public:
    // Callback types for pose generation
    using PoseCallback = std::function<HierarchyPose()>;
    using UpdateCallback = std::function<void(float deltaTime)>;
    using NodeCountCallback = std::function<size_t()>;

    AnimatedHierarchy() = default;

    // Create with callbacks for pose generation
    AnimatedHierarchy(
        NodeCountCallback nodeCount,
        PoseCallback restPose,
        PoseCallback currentPose,
        UpdateCallback update = nullptr)
        : getNodeCount_(std::move(nodeCount))
        , getRestPose_(std::move(restPose))
        , getCurrentPose_(std::move(currentPose))
        , onUpdate_(std::move(update)) {}

    // Query node count
    size_t nodeCount() const {
        return getNodeCount_ ? getNodeCount_() : 0;
    }

    // Get the rest/bind pose
    HierarchyPose restPose() const {
        return getRestPose_ ? getRestPose_() : HierarchyPose{};
    }

    // Get the current animated pose
    HierarchyPose currentPose() const {
        return getCurrentPose_ ? getCurrentPose_() : HierarchyPose{};
    }

    // Update animation state
    void update(float deltaTime) {
        if (onUpdate_) {
            onUpdate_(deltaTime);
        }
    }

    // Check if valid
    bool isValid() const {
        return getNodeCount_ != nullptr && getRestPose_ != nullptr && getCurrentPose_ != nullptr;
    }

    // LOD layer controller for blending multiple animation layers
    LODLayerController& layers() { return layers_; }
    const LODLayerController& layers() const { return layers_; }

    // Compute final pose with LOD layer blending applied
    HierarchyPose computeFinalPose() const {
        HierarchyPose base = currentPose();
        if (layers_.getLayers().empty()) {
            return base;
        }
        return layers_.computeFinalPose(base);
    }

private:
    NodeCountCallback getNodeCount_;
    PoseCallback getRestPose_;
    PoseCallback getCurrentPose_;
    UpdateCallback onUpdate_;
    LODLayerController layers_;
};

// Factory functions to create AnimatedHierarchy from common types

namespace AnimatedHierarchyFactory {

// Create from a static pose (useful for testing or static meshes)
inline AnimatedHierarchy fromStaticPose(const HierarchyPose& pose) {
    auto poseCopy = std::make_shared<HierarchyPose>(pose);
    return AnimatedHierarchy(
        [poseCopy]() { return poseCopy->size(); },
        [poseCopy]() { return *poseCopy; },
        [poseCopy]() { return *poseCopy; }
    );
}

// Create from rest pose and mutable current pose
inline AnimatedHierarchy fromPoses(
    std::shared_ptr<HierarchyPose> restPose,
    std::shared_ptr<HierarchyPose> currentPose) {
    return AnimatedHierarchy(
        [restPose]() { return restPose->size(); },
        [restPose]() { return *restPose; },
        [currentPose]() { return *currentPose; }
    );
}

} // namespace AnimatedHierarchyFactory

