#pragma once

#include "PhysicsSystem.h"
#include "../loaders/GLTFLoader.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

// Forward declarations for Jolt types
namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class Constraint;
}

// Describes how a single bone maps to a physics body
struct RagdollBoneMapping {
    int32_t boneIndex = -1;          // Index into skeleton joints
    std::string boneName;
    float capsuleRadius = 0.05f;     // Radius of physics capsule
    float capsuleHalfHeight = 0.1f;  // Half-height of capsule cylinder
    float mass = 1.0f;
    int32_t parentMappingIndex = -1; // Index into RagdollDefinition::bones (-1 for root)

    // Joint constraint limits (radians)
    glm::vec3 swingLimits = glm::vec3(glm::radians(45.0f));  // Max swing in each axis
    float twistMin = glm::radians(-45.0f);
    float twistMax = glm::radians(45.0f);
};

// Definition of a ragdoll - maps skeleton bones to physics bodies
struct RagdollDefinition {
    std::vector<RagdollBoneMapping> bones;

    // Build a default ragdoll definition from a skeleton
    // Automatically maps major bones (hips, spine, arms, legs, head)
    static RagdollDefinition buildFromSkeleton(const Skeleton& skeleton);
};

// Motor settings for controlling how strongly the ragdoll follows animation
struct RagdollMotorSettings {
    float maxForce = 500.0f;          // Maximum motor force (Newtons)
    float maxTorque = 100.0f;         // Maximum motor torque (Nm)
    float springFrequency = 10.0f;    // Spring frequency (Hz) for position targeting
    float springDamping = 1.0f;       // Damping ratio (1.0 = critically damped)
};

// Per-bone runtime state
struct RagdollBoneState {
    PhysicsBodyID bodyId = INVALID_BODY_ID;
    uint32_t constraintIndex = UINT32_MAX; // Index into constraints array
    glm::vec3 localAnchor = glm::vec3(0.0f); // Anchor point in parent body space

    // Motor target (set from animation)
    glm::quat targetRotation = glm::quat(1, 0, 0, 0);
    glm::vec3 targetPosition = glm::vec3(0.0f);
};

// Active ragdoll blend mode
enum class RagdollBlendMode : uint8_t {
    FullyAnimated = 0,   // No physics, pure animation
    Powered = 1,         // Physics bodies follow animation via motors (active ragdoll)
    PartialRagdoll = 2,  // Some bones physics-driven, others animated
    FullRagdoll = 3      // All physics, no animation (death, knockdown)
};

// Active Ragdoll Instance
// Creates and manages Jolt physics bodies that follow skeleton animation
// via motor-driven constraints. When hit, physics forces blend with animation.
class ActiveRagdoll {
public:
    // Factory: creates an active ragdoll for the given skeleton
    // physicsWorld: the physics world to create bodies in
    // definition: bone-to-body mapping
    // characterPosition: initial world position of the character
    static std::unique_ptr<ActiveRagdoll> create(
        PhysicsWorld& physicsWorld,
        const RagdollDefinition& definition,
        const Skeleton& skeleton,
        const glm::vec3& characterPosition
    );

    ~ActiveRagdoll();

    // Non-copyable
    ActiveRagdoll(const ActiveRagdoll&) = delete;
    ActiveRagdoll& operator=(const ActiveRagdoll&) = delete;

    // Drive ragdoll bodies toward animation pose
    // globalBoneTransforms: world-space bone transforms from animation
    // characterTransform: character's world transform
    // deltaTime: frame time
    void driveToAnimationPose(
        const std::vector<glm::mat4>& globalBoneTransforms,
        const glm::mat4& characterTransform,
        float deltaTime
    );

    // Read back physics transforms into skeleton
    // Blends between animation and physics based on blend mode
    // outGlobalTransforms: output world-space transforms (physics-influenced)
    void readPhysicsTransforms(
        std::vector<glm::mat4>& outGlobalTransforms,
        const std::vector<glm::mat4>& animationTransforms,
        const glm::mat4& characterTransform
    ) const;

    // Apply an impulse to a specific bone (e.g., from a sword hit)
    void applyImpulse(int32_t boneIndex, const glm::vec3& impulse, const glm::vec3& point);

    // Apply an impulse to the nearest bone to a world position
    void applyImpulseAtPoint(const glm::vec3& worldPoint, const glm::vec3& impulse);

    // Set blend mode
    void setBlendMode(RagdollBlendMode mode);
    RagdollBlendMode getBlendMode() const { return blendMode_; }

    // Set motor strength (0 = pure physics, 1 = strong animation following)
    void setMotorStrength(float strength);
    float getMotorStrength() const { return motorStrength_; }

    // Set per-bone motor strength override (for partial ragdoll)
    void setBoneMotorStrength(int32_t boneIndex, float strength);

    // Configure motor settings
    void setMotorSettings(const RagdollMotorSettings& settings);
    const RagdollMotorSettings& getMotorSettings() const { return motorSettings_; }

    // Transition smoothly between modes over duration (seconds)
    void transitionToMode(RagdollBlendMode targetMode, float duration = 0.3f);

    // Update transition state
    void updateTransition(float deltaTime);

    // Check if transitioning
    bool isTransitioning() const { return transitionActive_; }

    // Get the physics body for a bone (for hit detection queries)
    PhysicsBodyID getBoneBody(int32_t boneIndex) const;

    // Find which bone a physics body belongs to (-1 if not found)
    int32_t findBoneForBody(PhysicsBodyID bodyId) const;

    // Get the ragdoll definition
    const RagdollDefinition& getDefinition() const { return definition_; }

    // Get bone state
    const std::vector<RagdollBoneState>& getBoneStates() const { return boneStates_; }

    // Enable/disable the ragdoll (removes/adds bodies from simulation)
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }

    // Teleport all ragdoll bodies to match current animation pose
    void teleportToAnimation(
        const std::vector<glm::mat4>& globalBoneTransforms,
        const glm::mat4& characterTransform
    );

private:
    ActiveRagdoll() = default;

    bool createBodies(
        PhysicsWorld& physicsWorld,
        const Skeleton& skeleton,
        const glm::vec3& characterPosition
    );

    // Mapping from skeleton bone index to ragdoll bone index
    int32_t findRagdollBoneIndex(int32_t skeletonBoneIndex) const;

    RagdollDefinition definition_;
    std::vector<RagdollBoneState> boneStates_;

    // Motor control
    RagdollMotorSettings motorSettings_;
    float motorStrength_ = 1.0f;
    std::vector<float> perBoneMotorStrength_; // Per-bone override

    // Blend mode
    RagdollBlendMode blendMode_ = RagdollBlendMode::Powered;

    // Transition
    bool transitionActive_ = false;
    RagdollBlendMode transitionTarget_ = RagdollBlendMode::Powered;
    float transitionDuration_ = 0.3f;
    float transitionElapsed_ = 0.0f;
    float transitionStartStrength_ = 1.0f;
    float transitionEndStrength_ = 1.0f;

    // Reference to physics world for body management
    PhysicsWorld* physicsWorld_ = nullptr;
    bool enabled_ = true;
};
