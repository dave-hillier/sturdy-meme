#pragma once

#include "AnimationBlend.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>

struct Skeleton;
class CharacterController;

namespace JPH {
    class PhysicsSystem;
}

namespace physics {

// Per-NPC ragdoll instance that wraps a Jolt Ragdoll.
//
// Provides the bridge between CALM motor targets and the Jolt physics simulation:
//   - driveToTargetPose(): sets motor targets from CALM action output
//   - readPose(): reads physics-resolved pose back for rendering/observation
//   - Motor strength control for animated-to-ragdoll transitions
//
// Lifecycle:
//   1. Construct from RagdollSettings + PhysicsSystem
//   2. setPoseImmediate() to snap to initial pose
//   3. activate() to add to physics world
//   4. Each frame: driveToTargetPose() before physics step, readPose() after
//   5. Destructor removes from physics world
class RagdollInstance {
public:
    // Create a ragdoll instance from shared settings.
    // The ragdoll is NOT added to the physics world until activate() is called.
    RagdollInstance(JPH::Ref<JPH::RagdollSettings> settings,
                    const Skeleton& skeleton,
                    JPH::PhysicsSystem* physicsSystem);

    ~RagdollInstance();

    // Non-copyable, movable
    RagdollInstance(const RagdollInstance&) = delete;
    RagdollInstance& operator=(const RagdollInstance&) = delete;
    RagdollInstance(RagdollInstance&& other) noexcept;
    RagdollInstance& operator=(RagdollInstance&& other) noexcept;

    // --- Pose control ---

    // Set motor targets from a CALM-generated skeleton pose.
    // Motors will drive toward this pose during the next physics step.
    void driveToTargetPose(const SkeletonPose& targetPose);

    // Hard-set all body positions/rotations immediately (bypasses physics).
    // Use for initialization, teleporting, or LOD transitions.
    void setPoseImmediate(const SkeletonPose& pose,
                          const Skeleton& skeleton);

    // Read the current physics-resolved pose back into a SkeletonPose.
    // This is used for rendering and observation extraction.
    void readPose(SkeletonPose& outPose, const Skeleton& skeleton) const;

    // --- Body velocity queries (for CALM observation) ---

    // Read per-body linear velocities.
    void readBodyLinearVelocities(std::vector<glm::vec3>& outVels) const;

    // Read per-body angular velocities.
    void readBodyAngularVelocities(std::vector<glm::vec3>& outVels) const;

    // --- Root body queries ---

    glm::vec3 getRootPosition() const;
    glm::quat getRootRotation() const;
    glm::vec3 getRootLinearVelocity() const;
    glm::vec3 getRootAngularVelocity() const;

    // --- State management ---

    // Add ragdoll to physics world.
    void activate();

    // Remove ragdoll from physics world (keeps instance alive).
    void deactivate();

    bool isActive() const { return active_; }

    // --- External forces ---

    // Apply an impulse to a specific bone.
    void addImpulse(int boneIndex, const glm::vec3& impulse);

    // Apply an impulse at a world-space position (affects closest bone).
    void addImpulseAtWorldPos(const glm::vec3& impulse, const glm::vec3& worldPos);

    // --- Motor strength control ---

    // Set the overall motor strength (0 = limp ragdoll, 1 = full CALM control).
    // Internally scales motor torque limits.
    void setMotorStrength(float strength);
    float getMotorStrength() const { return motorStrength_; }

    // Enable/disable all motors
    void setMotorsEnabled(bool enabled);

    // --- Sync with character controller ---

    // Move the character controller capsule to match the ragdoll root position.
    void syncCharacterController(CharacterController& controller) const;

    // --- Query ---

    size_t bodyCount() const;
    JPH::BodyID getBodyID(int index) const;

private:
    JPH::Ref<JPH::Ragdoll> ragdoll_;
    JPH::Ref<JPH::RagdollSettings> settings_;
    JPH::PhysicsSystem* physicsSystem_ = nullptr;
    const Skeleton* skeleton_ = nullptr;

    float motorStrength_ = 1.0f;
    float baseMaxTorque_ = 200.0f;
    bool active_ = false;
    bool motorsEnabled_ = true;

    // Build a Jolt SkeletonPose from the engine's SkeletonPose
    void buildJoltPose(const SkeletonPose& enginePose,
                       const Skeleton& skeleton,
                       JPH::SkeletonPose& outJoltPose) const;
};

} // namespace physics
