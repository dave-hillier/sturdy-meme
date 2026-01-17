#pragma once

#include "HierarchicalPose.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

// Blend modes for animation/LOD layers
enum class BlendMode {
    Override,   // Replace underlying pose (weighted)
    Additive    // Add delta on top of underlying pose
};

// Generic pose blending functions for hierarchical structures.
// Used by both skeletal animation and tree animation systems.
namespace PoseBlend {

    // Linear interpolation for vectors
    inline glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
        return glm::mix(a, b, t);
    }

    // Spherical linear interpolation for quaternions
    inline glm::quat slerp(const glm::quat& a, const glm::quat& b, float t) {
        return glm::slerp(a, b, t);
    }

    // Blend two node poses with weight t (0 = a, 1 = b)
    NodePose blend(const NodePose& a, const NodePose& b, float t);

    // Blend two hierarchy poses with weight t
    void blend(const HierarchyPose& a, const HierarchyPose& b, float t, HierarchyPose& out);

    // Blend two hierarchy poses with per-node weights
    void blendMasked(const HierarchyPose& a, const HierarchyPose& b,
                     const std::vector<float>& nodeWeights, HierarchyPose& out);

    // Add additive pose on top of base pose
    // additivePose is the delta from a reference pose (typically rest/bind pose)
    NodePose additive(const NodePose& base, const NodePose& additiveDelta, float weight = 1.0f);

    // Add additive hierarchy pose on top of base with per-node weights
    void additiveMasked(const HierarchyPose& base, const HierarchyPose& additiveDelta,
                        const std::vector<float>& nodeWeights, HierarchyPose& out);

    // Compute additive delta between a reference pose and an animation pose
    // Result: animation - reference (can be applied additively later)
    NodePose computeAdditiveDelta(const NodePose& reference, const NodePose& animation);

    // Compute additive delta for entire hierarchy
    void computeAdditiveDelta(const HierarchyPose& reference, const HierarchyPose& animation,
                              HierarchyPose& outDelta);

}  // namespace PoseBlend
