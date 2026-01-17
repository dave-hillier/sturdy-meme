#include "PoseBlend.h"
#include <algorithm>

namespace PoseBlend {

NodePose blend(const NodePose& a, const NodePose& b, float t) {
    NodePose result;
    result.translation = lerp(a.translation, b.translation, t);
    result.rotation = slerp(a.rotation, b.rotation, t);
    result.scale = lerp(a.scale, b.scale, t);
    return result;
}

void blend(const HierarchyPose& a, const HierarchyPose& b, float t, HierarchyPose& out) {
    size_t count = std::min(a.size(), b.size());
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        out[i] = blend(a[i], b[i], t);
    }
}

void blendMasked(const HierarchyPose& a, const HierarchyPose& b,
                 const std::vector<float>& nodeWeights, HierarchyPose& out) {
    size_t count = std::min({a.size(), b.size(), nodeWeights.size()});
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        float weight = nodeWeights[i];
        out[i] = blend(a[i], b[i], weight);
    }
}

NodePose additive(const NodePose& base, const NodePose& additiveDelta, float weight) {
    if (weight <= 0.0f) {
        return base;
    }

    NodePose result;

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

void additiveMasked(const HierarchyPose& base, const HierarchyPose& additiveDelta,
                    const std::vector<float>& nodeWeights, HierarchyPose& out) {
    size_t count = std::min({base.size(), additiveDelta.size(), nodeWeights.size()});
    out.resize(count);

    for (size_t i = 0; i < count; ++i) {
        out[i] = additive(base[i], additiveDelta[i], nodeWeights[i]);
    }
}

NodePose computeAdditiveDelta(const NodePose& reference, const NodePose& animation) {
    NodePose delta;

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

void computeAdditiveDelta(const HierarchyPose& reference, const HierarchyPose& animation,
                          HierarchyPose& outDelta) {
    size_t count = std::min(reference.size(), animation.size());
    outDelta.resize(count);

    for (size_t i = 0; i < count; ++i) {
        outDelta[i] = computeAdditiveDelta(reference[i], animation[i]);
    }
}

}  // namespace PoseBlend
