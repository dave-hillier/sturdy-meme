#pragma once

#include "JointLimitPresets.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Jolt forward declarations
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>

struct Skeleton;

namespace physics {

// Per-bone shape override for the ragdoll builder.
struct BoneShapeOverride {
    float radius = -1.0f;       // -1 = auto-estimate from bone length
    float lengthScale = 1.0f;   // Scale the auto-computed bone length
    float massScale = 1.0f;     // Relative mass adjustment
};

// Configuration for building a Jolt ragdoll from an engine Skeleton.
struct RagdollConfig {
    float radiusFraction = 0.15f;       // Bone radius = length * fraction
    float minRadius = 0.02f;            // Minimum capsule radius (meters)
    float maxRadius = 0.15f;            // Maximum capsule radius (meters)
    float totalMass = 70.0f;            // Total character mass (kg)
    float linearDamping = 0.1f;
    float angularDamping = 0.3f;

    // Motor spring settings (maps to PD controller gains)
    float motorFrequency = 8.0f;        // Hz — motor responsiveness
    float motorDamping = 0.8f;          // Critical damping ratio
    float maxMotorTorque = 200.0f;      // N·m per joint

    // Per-bone overrides keyed by bone name
    std::unordered_map<std::string, BoneShapeOverride> boneOverrides;
};

// Builds Jolt RagdollSettings from the engine's Skeleton and bind pose.
//
// The returned settings are shared per-archetype (ref-counted). Each NPC
// creates a Ragdoll instance from these settings via CreateRagdoll().
//
// Build process:
//   1. Create JPH::Skeleton mirroring the engine joint hierarchy
//   2. Compute capsule shapes from parent-to-child bone directions
//   3. Distribute mass proportional to bone volume
//   4. Create SwingTwistConstraint settings for each parent-child pair
//   5. Configure motors with spring settings for CALM-driven pose tracking
//   6. Stabilize and disable parent-child collisions
class RagdollBuilder {
public:
    // Build ragdoll settings from the engine skeleton.
    // globalBindPose: world-space transform per joint (from skeleton.computeGlobalTransforms)
    static JPH::Ref<JPH::RagdollSettings> build(
        const Skeleton& skeleton,
        const std::vector<glm::mat4>& globalBindPose,
        const RagdollConfig& config = {});

private:
    // Compute bone length between a joint and its parent
    static float computeBoneLength(const glm::mat4& parentGlobal,
                                    const glm::mat4& childGlobal);

    // Estimate capsule radius for a bone
    static float estimateRadius(float boneLength,
                                 const std::string& boneName,
                                 const RagdollConfig& config);

    // Compute capsule volume for mass distribution
    static float capsuleVolume(float halfHeight, float radius);
};

} // namespace physics
