#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// Represents the transform of a single bone in local space
// Uses T/R/S decomposition for clean blending
struct BonePose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    // Convert to matrix (T * R * S)
    glm::mat4 toMatrix() const;

    // Convert to matrix with pre-rotation (T * Rpre * R * S)
    glm::mat4 toMatrix(const glm::quat& preRotation) const;

    // Create from matrix (assumes T * R * S decomposition)
    static BonePose fromMatrix(const glm::mat4& matrix);

    // Create from matrix, extracting the animated rotation (removing preRotation)
    // Matrix format: T * Rpre * R * S
    static BonePose fromMatrix(const glm::mat4& matrix, const glm::quat& preRotation);

    // Identity pose
    static BonePose identity() {
        return BonePose{glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)};
    }
};

// Full skeleton pose (all bones)
struct SkeletonPose {
    std::vector<BonePose> bonePoses;

    void resize(size_t count) { bonePoses.resize(count); }
    size_t size() const { return bonePoses.size(); }

    BonePose& operator[](size_t i) { return bonePoses[i]; }
    const BonePose& operator[](size_t i) const { return bonePoses[i]; }
};

// Blend modes for animation layers
enum class BlendMode {
    Override,   // Replace underlying animation (weighted)
    Additive    // Add delta on top of underlying animation
};

namespace AnimationBlend {

    // Linear interpolation for vectors
    inline glm::vec3 lerp(const glm::vec3& a, const glm::vec3& b, float t) {
        return glm::mix(a, b, t);
    }

    // Spherical linear interpolation for quaternions
    inline glm::quat slerp(const glm::quat& a, const glm::quat& b, float t) {
        return glm::slerp(a, b, t);
    }

    // Blend two bone poses with weight t (0 = a, 1 = b)
    BonePose blend(const BonePose& a, const BonePose& b, float t);

    // Blend two skeleton poses with weight t
    void blend(const SkeletonPose& a, const SkeletonPose& b, float t, SkeletonPose& out);

    // Blend two skeleton poses with per-bone weights
    void blendMasked(const SkeletonPose& a, const SkeletonPose& b,
                     const std::vector<float>& boneWeights, SkeletonPose& out);

    // Add additive pose on top of base pose
    // additivePose is the delta from a reference pose (typically bind pose)
    BonePose additive(const BonePose& base, const BonePose& additiveDelta, float weight = 1.0f);

    // Add additive skeleton pose on top of base with per-bone weights
    void additiveMasked(const SkeletonPose& base, const SkeletonPose& additiveDelta,
                        const std::vector<float>& boneWeights, SkeletonPose& out);

    // Compute additive delta between a reference pose and an animation pose
    // Result: animation - reference (can be applied additively later)
    BonePose computeAdditiveDelta(const BonePose& reference, const BonePose& animation);

    // Compute additive delta for entire skeleton
    void computeAdditiveDelta(const SkeletonPose& reference, const SkeletonPose& animation,
                              SkeletonPose& outDelta);

}  // namespace AnimationBlend
