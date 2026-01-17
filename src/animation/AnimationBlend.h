#pragma once

#include "../core/HierarchicalPose.h"
#include "../core/PoseBlend.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// BonePose is a type alias for the generic NodePose.
// This maintains API compatibility while sharing the underlying implementation.
using BonePose = NodePose;

// SkeletonPose is a type alias for the generic HierarchyPose.
// This maintains API compatibility while sharing the underlying implementation.
using SkeletonPose = HierarchyPose;

// Re-export BlendMode from PoseBlend for backwards compatibility
using ::BlendMode;

// Animation-specific blending functions.
// These delegate to the generic PoseBlend namespace but provide
// animation-domain naming for clarity.
namespace AnimationBlend {

    // Linear interpolation for vectors
    inline glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
        return PoseBlend::lerp(a, b, t);
    }

    // Spherical linear interpolation for quaternions
    inline glm::quat slerp(const glm::quat& a, const glm::quat& b, float t) {
        return PoseBlend::slerp(a, b, t);
    }

    // Blend two bone poses with weight t (0 = a, 1 = b)
    inline BonePose blend(const BonePose& a, const BonePose& b, float t) {
        return PoseBlend::blend(a, b, t);
    }

    // Blend two skeleton poses with weight t
    inline void blend(const SkeletonPose& a, const SkeletonPose& b, float t, SkeletonPose& out) {
        PoseBlend::blend(a, b, t, out);
    }

    // Blend two skeleton poses with per-bone weights
    inline void blendMasked(const SkeletonPose& a, const SkeletonPose& b,
                            const std::vector<float>& boneWeights, SkeletonPose& out) {
        PoseBlend::blendMasked(a, b, boneWeights, out);
    }

    // Add additive pose on top of base pose
    // additivePose is the delta from a reference pose (typically bind pose)
    inline BonePose additive(const BonePose& base, const BonePose& additiveDelta, float weight = 1.0f) {
        return PoseBlend::additive(base, additiveDelta, weight);
    }

    // Add additive skeleton pose on top of base with per-bone weights
    inline void additiveMasked(const SkeletonPose& base, const SkeletonPose& additiveDelta,
                               const std::vector<float>& boneWeights, SkeletonPose& out) {
        PoseBlend::additiveMasked(base, additiveDelta, boneWeights, out);
    }

    // Compute additive delta between a reference pose and an animation pose
    // Result: animation - reference (can be applied additively later)
    inline BonePose computeAdditiveDelta(const BonePose& reference, const BonePose& animation) {
        return PoseBlend::computeAdditiveDelta(reference, animation);
    }

    // Compute additive delta for entire skeleton
    inline void computeAdditiveDelta(const SkeletonPose& reference, const SkeletonPose& animation,
                                     SkeletonPose& outDelta) {
        PoseBlend::computeAdditiveDelta(reference, animation, outDelta);
    }

}  // namespace AnimationBlend
