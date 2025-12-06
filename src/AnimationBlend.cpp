#include "AnimationBlend.h"
#include <cmath>

glm::mat4 BonePose::toMatrix() const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * R * S;
}

glm::mat4 BonePose::toMatrix(const glm::quat& preRotation) const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 Rpre = glm::mat4_cast(preRotation);
    glm::mat4 R = glm::mat4_cast(rotation);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
    return T * Rpre * R * S;
}

BonePose BonePose::fromMatrix(const glm::mat4& matrix) {
    BonePose pose;

    // Extract translation from column 3
    pose.translation = glm::vec3(matrix[3]);

    // Extract scale from column lengths
    pose.scale.x = glm::length(glm::vec3(matrix[0]));
    pose.scale.y = glm::length(glm::vec3(matrix[1]));
    pose.scale.z = glm::length(glm::vec3(matrix[2]));

    // Handle zero or near-zero scale
    const float epsilon = 1e-6f;
    if (pose.scale.x < epsilon) pose.scale.x = 1.0f;
    if (pose.scale.y < epsilon) pose.scale.y = 1.0f;
    if (pose.scale.z < epsilon) pose.scale.z = 1.0f;

    // Extract rotation by normalizing the rotation matrix columns
    glm::mat3 rotMat(
        glm::vec3(matrix[0]) / pose.scale.x,
        glm::vec3(matrix[1]) / pose.scale.y,
        glm::vec3(matrix[2]) / pose.scale.z
    );
    pose.rotation = glm::quat_cast(rotMat);

    return pose;
}

BonePose BonePose::fromMatrix(const glm::mat4& matrix, const glm::quat& preRotation) {
    BonePose pose = fromMatrix(matrix);

    // The extracted rotation is Rpre * R (preRotation combined with animated rotation)
    // Extract animated rotation: R = inverse(Rpre) * combinedR
    glm::quat preRotInv = glm::inverse(preRotation);
    pose.rotation = preRotInv * pose.rotation;

    return pose;
}

namespace AnimationBlend {

BonePose blend(const BonePose& a, const BonePose& b, float t) {
    BonePose result;
    result.translation = lerp(a.translation, b.translation, t);
    result.rotation = slerp(a.rotation, b.rotation, t);
    result.scale = lerp(a.scale, b.scale, t);
    return result;
}

void blend(const SkeletonPose& a, const SkeletonPose& b, float t, SkeletonPose& out) {
    size_t count = std::min(a.size(), b.size());
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        out[i] = blend(a[i], b[i], t);
    }
}

void blendMasked(const SkeletonPose& a, const SkeletonPose& b,
                 const std::vector<float>& boneWeights, SkeletonPose& out) {
    size_t count = std::min({a.size(), b.size(), boneWeights.size()});
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        float weight = boneWeights[i];
        out[i] = blend(a[i], b[i], weight);
    }
}

BonePose additive(const BonePose& base, const BonePose& additiveDelta, float weight) {
    if (weight <= 0.0f) {
        return base;
    }

    BonePose result;

    // Additive translation: base + delta * weight
    result.translation = base.translation + additiveDelta.translation * weight;

    // Additive rotation: base * slerp(identity, delta, weight)
    // This effectively adds a fraction of the delta rotation
    glm::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat weightedDelta = slerp(identity, additiveDelta.rotation, weight);
    result.rotation = glm::normalize(base.rotation * weightedDelta);

    // Additive scale: base * lerp(1, delta, weight)
    // Where delta is stored as the actual scale (not relative)
    // For additive, we interpret delta.scale as multiplicative offset from 1
    glm::vec3 scaleOffset = (additiveDelta.scale - glm::vec3(1.0f)) * weight;
    result.scale = base.scale * (glm::vec3(1.0f) + scaleOffset);

    return result;
}

void additiveMasked(const SkeletonPose& base, const SkeletonPose& additiveDelta,
                    const std::vector<float>& boneWeights, SkeletonPose& out) {
    size_t count = std::min({base.size(), additiveDelta.size(), boneWeights.size()});
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        out[i] = additive(base[i], additiveDelta[i], boneWeights[i]);
    }
}

BonePose computeAdditiveDelta(const BonePose& reference, const BonePose& animation) {
    BonePose delta;

    // Translation delta: animation - reference
    delta.translation = animation.translation - reference.translation;

    // Rotation delta: inverse(reference) * animation
    // This gives us the rotation needed to go from reference to animation
    delta.rotation = glm::normalize(glm::inverse(reference.rotation) * animation.rotation);

    // Scale delta: stored as the animation scale (will be interpreted as offset from 1 when applied)
    // For proper additive: animation.scale / reference.scale
    // But we store it as the raw scale for the additive() function to handle
    const float epsilon = 1e-6f;
    delta.scale.x = reference.scale.x > epsilon ? animation.scale.x / reference.scale.x : 1.0f;
    delta.scale.y = reference.scale.y > epsilon ? animation.scale.y / reference.scale.y : 1.0f;
    delta.scale.z = reference.scale.z > epsilon ? animation.scale.z / reference.scale.z : 1.0f;

    return delta;
}

void computeAdditiveDelta(const SkeletonPose& reference, const SkeletonPose& animation,
                          SkeletonPose& outDelta) {
    size_t count = std::min(reference.size(), animation.size());
    outDelta.resize(count);

    for (size_t i = 0; i < count; ++i) {
        outDelta[i] = computeAdditiveDelta(reference[i], animation[i]);
    }
}

}  // namespace AnimationBlend
