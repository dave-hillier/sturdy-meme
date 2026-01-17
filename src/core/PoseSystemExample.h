#pragma once

// Example usage of the hierarchical pose system.
// This header demonstrates common patterns for using the shared pose infrastructure.

#include "HierarchicalPose.h"
#include "PoseBlend.h"
#include "NodeMask.h"
#include "LODLayerController.h"
#include "AnimatedHierarchy.h"

#include <glm/gtc/constants.hpp>

namespace PoseSystemExample {

// Example 1: Basic pose blending
inline void basicBlending() {
    // Create two poses
    NodePose poseA = NodePose::identity();
    NodePose poseB;
    poseB.translation = glm::vec3(1.0f, 0.0f, 0.0f);
    poseB.rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0));

    // Blend between them (t=0.5 = halfway)
    NodePose blended = PoseBlend::blend(poseA, poseB, 0.5f);

    // Result: translation = (0.5, 0, 0), rotation = 45 degrees around Y
    (void)blended;
}

// Example 2: Masked blending for partial body animation
inline void maskedBlending() {
    const size_t nodeCount = 10;

    // Create base and overlay poses
    HierarchyPose basePose;
    basePose.resize(nodeCount);
    HierarchyPose overlayPose;
    overlayPose.resize(nodeCount);

    // Create a mask that affects only upper nodes (e.g., upper body)
    NodeMask upperMask(nodeCount, 0.0f);  // Start with all zeros
    for (size_t i = 5; i < nodeCount; ++i) {
        upperMask.setWeight(i, 1.0f);  // Enable upper nodes
    }

    // Blend with mask - only upper nodes are affected
    HierarchyPose result;
    PoseBlend::blendMasked(basePose, overlayPose, upperMask.getWeights(), result);

    (void)result;
}

// Example 3: Additive animation (e.g., breathing on top of walk cycle)
inline void additiveAnimation() {
    NodePose walkPose;
    walkPose.translation = glm::vec3(0.0f, 0.0f, 1.0f);  // Walking forward

    // Breathing is an additive delta (small up/down on chest)
    NodePose breatheDelta;
    breatheDelta.translation = glm::vec3(0.0f, 0.05f, 0.0f);  // Slight lift
    breatheDelta.rotation = glm::quat(1, 0, 0, 0);  // Identity (no rotation change)
    breatheDelta.scale = glm::vec3(1.02f, 1.0f, 1.02f);  // Slight chest expansion

    // Apply additive with 50% weight
    NodePose result = PoseBlend::additive(walkPose, breatheDelta, 0.5f);

    (void)result;
}

// Example 4: LOD layer blending for trees
inline void treeLODBlending() {
    // Simulate a tree with branches at different levels
    const size_t branchCount = 20;
    std::vector<int> branchLevels(branchCount);

    // Level 0: trunk (indices 0-2)
    // Level 1: primary branches (indices 3-7)
    // Level 2+: outer branches (indices 8-19)
    for (size_t i = 0; i < branchCount; ++i) {
        if (i < 3) branchLevels[i] = 0;
        else if (i < 8) branchLevels[i] = 1;
        else branchLevels[i] = 2;
    }

    // Configure LOD controller
    LODLayerController lodController;
    lodController.configureTreeLOD(branchLevels, 2);

    // Create a wind animation pose
    HierarchyPose windPose;
    windPose.resize(branchCount);
    for (size_t i = 0; i < branchCount; ++i) {
        float bendAmount = static_cast<float>(branchLevels[i]) * 0.1f;
        windPose[i].rotation = glm::angleAxis(bendAmount, glm::vec3(1, 0, 0));
    }

    // Set up wind layer
    LODLayer* windLayer = lodController.addLayer("wind");
    windLayer->pose = windPose;
    windLayer->blendMode = BlendMode::Additive;

    // As LOD factor increases, outer branches fade first
    HierarchyPose restPose;
    restPose.resize(branchCount);  // All identity

    // At LOD 0.0 (full detail): all wind animation visible
    lodController.setLODBlendFactor(0.0f);
    HierarchyPose fullDetail = lodController.computeFinalPose(restPose);

    // At LOD 0.5 (mid distance): outer branches partially faded
    lodController.setLODBlendFactor(0.5f);
    HierarchyPose midLOD = lodController.computeFinalPose(restPose);

    // At LOD 1.0 (far): all animation faded
    lodController.setLODBlendFactor(1.0f);
    HierarchyPose farLOD = lodController.computeFinalPose(restPose);

    (void)fullDetail;
    (void)midLOD;
    (void)farLOD;
}

// Example 5: Using AnimatedHierarchy for uniform processing
inline void unifiedInterface() {
    const size_t nodeCount = 5;

    // Shared pose storage
    auto restPose = std::make_shared<HierarchyPose>();
    restPose->resize(nodeCount);
    auto currentPose = std::make_shared<HierarchyPose>();
    currentPose->resize(nodeCount);

    // Create animated hierarchy with callbacks
    AnimatedHierarchy anim(
        [nodeCount]() { return nodeCount; },
        [restPose]() { return *restPose; },
        [currentPose]() { return *currentPose; },
        [currentPose](float dt) {
            // Simple oscillation animation
            float angle = dt * 2.0f;  // Assuming dt accumulates
            for (auto& pose : *currentPose) {
                pose.rotation = glm::angleAxis(
                    std::sin(angle) * 0.1f,
                    glm::vec3(0, 0, 1)
                );
            }
        }
    );

    // Can be processed uniformly regardless of source type
    anim.update(0.016f);  // ~60fps
    HierarchyPose result = anim.computeFinalPose();

    (void)result;
}

// Example 6: Creating masks from hierarchy depth
inline void depthBasedMasks() {
    const size_t nodeCount = 10;

    // Simulate depth levels (0 = root, higher = further from root)
    std::vector<int> nodeDepths = {0, 1, 1, 2, 2, 2, 3, 3, 3, 3};

    // Create mask for extremities (depth >= 3)
    NodeMask extremities = NodeMask::fromDepthRange(nodeCount, nodeDepths, 3, 3);

    // Create mask for mid-level (depth 1-2)
    NodeMask midLevel = NodeMask::fromDepthRange(nodeCount, nodeDepths, 1, 2);

    // Create mask for core (depth 0)
    NodeMask core = NodeMask::fromDepthRange(nodeCount, nodeDepths, 0, 0);

    // Verify masks are mutually exclusive
    for (size_t i = 0; i < nodeCount; ++i) {
        float total = extremities.getWeight(i) + midLevel.getWeight(i) + core.getWeight(i);
        (void)total;  // Should equal 1.0 for each node
    }
}

} // namespace PoseSystemExample

