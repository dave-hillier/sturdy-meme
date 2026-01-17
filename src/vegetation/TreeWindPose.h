#pragma once

#include "../core/HierarchicalPose.h"
#include "../core/NodeMask.h"
#include "TreeSkeleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// CPU-side wind parameters matching shader WindParams struct
struct TreeWindParams {
    glm::vec2 direction{1.0f, 0.0f};  // Normalized wind direction in XZ plane
    float strength{0.5f};              // Wind strength [0, 1+]
    float speed{1.0f};                 // Wind animation speed
    float gustFrequency{1.0f};         // Gust oscillation frequency
    float gustAmplitude{0.5f};         // Gust amplitude
    float time{0.0f};                  // Animation time (seconds)

    // Create from typical wind system values
    static TreeWindParams fromWindSystem(
        const glm::vec2& dir, float strength, float gustFreq, float time);
};

// Result of per-tree oscillation calculation (mirrors shader TreeWindOscillation)
struct TreeOscillation {
    float mainBend{0.0f};      // Primary bend in wind direction
    float perpBend{0.0f};      // Secondary perpendicular sway (figure-8 motion)
    glm::vec3 windDir3D;       // Wind direction in 3D (XZ plane)
    glm::vec3 windPerp3D;      // Perpendicular to wind direction
    float treePhase{0.0f};     // Per-tree phase offset
};

// Calculates wind-driven poses for tree skeletons.
// Mirrors the GPU wind animation from wind_animation_common.glsl but produces
// CPU-side poses that can be used for:
// - Hybrid CPU+GPU animation
// - Branch-level animation control
// - Wind response layers in the pose system
class TreeWindPose {
public:
    // Calculate wind oscillation for a tree at a given world position
    // Matches windCalculateTreeOscillation from shader
    static TreeOscillation calculateOscillation(
        const glm::vec3& treeWorldPosition,
        const TreeWindParams& wind);

    // Calculate wind pose deltas for a tree skeleton
    // Returns additive pose deltas (can be applied with PoseBlend::additive)
    // The pose represents rotation deltas to apply to each branch
    static HierarchyPose calculateWindPose(
        const TreeSkeleton& skeleton,
        const TreeOscillation& oscillation,
        const TreeWindParams& wind);

    // Calculate wind pose with custom flexibility mask
    // Allows per-branch control over wind response
    static HierarchyPose calculateWindPoseMasked(
        const TreeSkeleton& skeleton,
        const TreeOscillation& oscillation,
        const TreeWindParams& wind,
        const NodeMask& flexibilityMask);

    // Get default flexibility mask based on branch levels
    // Higher level branches (outer) are more flexible
    static NodeMask getDefaultFlexibilityMask(const TreeSkeleton& skeleton);

    // Calculate branch flexibility factor for a given branch level
    // Matches windCalculateBranchFlexibility from shader
    static float calculateBranchFlexibility(int branchLevel);

    // Calculate how much a branch should respond based on its orientation
    // relative to wind direction. Matches windCalculateDirectionScale.
    static float calculateDirectionScale(
        const glm::vec3& branchDirection,
        const glm::vec3& windDir3D);

private:
    // Simple noise function for phase offset (matches shader simplex3 behavior)
    static float noise3(const glm::vec3& p);
};
