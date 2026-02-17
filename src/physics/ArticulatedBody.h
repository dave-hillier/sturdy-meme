#pragma once

#include "PhysicsSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>

// Forward declarations
struct Skeleton;

namespace JPH {
    class Ragdoll;
}

// Definition for a single rigid body part in an articulated body
struct BodyPartDef {
    std::string name;
    int32_t skeletonJointIndex = -1;
    int32_t parentPartIndex = -1;       // -1 for root (pelvis)

    // Shape: capsule (halfHeight along Y, radius)
    float halfHeight = 0.1f;
    float radius = 0.05f;
    float mass = 5.0f;

    // Constraint attachment points (in parent and child local space)
    glm::vec3 localAnchorInParent{0.0f};
    glm::vec3 localAnchorInChild{0.0f};

    // Joint constraint axes
    glm::vec3 twistAxis{0.0f, 1.0f, 0.0f};    // Primary rotation axis
    glm::vec3 planeAxis{1.0f, 0.0f, 0.0f};    // Secondary axis (perpendicular to twist)

    // Joint limits (radians)
    float twistMinAngle = -0.5f;
    float twistMaxAngle = 0.5f;
    float normalHalfConeAngle = 0.5f;   // Swing limit in normal direction
    float planeHalfConeAngle = 0.5f;    // Swing limit in plane direction

    // Torque effort factor for policy output scaling (UniCon spec: 50-600)
    float effortFactor = 200.0f;
};

struct ArticulatedBodyConfig {
    std::vector<BodyPartDef> parts;
    float globalScale = 1.0f;
};

// Builds a standard 20-body humanoid matching UniCon's specification.
// Maps skeleton joints to rigid body parts by name.
ArticulatedBodyConfig createHumanoidConfig(const Skeleton& skeleton);

// Multi-rigid-body structure connected by Jolt constraints.
// Uses Jolt's Ragdoll API for numerically stable constraint solving.
class ArticulatedBody {
public:
    ArticulatedBody() = default;
    ~ArticulatedBody();

    // Move-only
    ArticulatedBody(ArticulatedBody&& other) noexcept;
    ArticulatedBody& operator=(ArticulatedBody&& other) noexcept;
    ArticulatedBody(const ArticulatedBody&) = delete;
    ArticulatedBody& operator=(const ArticulatedBody&) = delete;

    // Create all rigid bodies and constraints from config.
    // rootPosition: world position for the root (pelvis) body.
    bool create(PhysicsWorld& physics, const ArticulatedBodyConfig& config,
                const glm::vec3& rootPosition);

    // Remove all bodies and constraints from the physics world.
    void destroy(PhysicsWorld& physics);

    bool isValid() const { return ragdoll_ != nullptr; }

    // State extraction (for building observation vectors)
    struct PartState {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocity{0.0f};
    };

    void getState(std::vector<PartState>& states, const PhysicsWorld& physics) const;

    // Torque application (from policy output).
    // torques.size() must equal getPartCount().
    // Each torque is scaled by the part's effortFactor.
    void applyTorques(PhysicsWorld& physics, const std::vector<glm::vec3>& torques) const;

    // Sync physics state to skeleton joint local transforms for rendering.
    void writeToSkeleton(Skeleton& skeleton, const PhysicsWorld& physics) const;

    size_t getPartCount() const { return bodyIDs_.size(); }
    PhysicsBodyID getPartBodyID(size_t index) const;

    // Get the skeleton joint index for a given part
    int32_t getPartJointIndex(size_t index) const;

    // Get root body position (for camera following, etc.)
    glm::vec3 getRootPosition(const PhysicsWorld& physics) const;
    glm::quat getRootRotation(const PhysicsWorld& physics) const;

    // Check if any body has NaN position/velocity (constraint solver diverged).
    // Returns true if the ragdoll is broken and should be destroyed.
    bool hasNaNState(const PhysicsWorld& physics) const;

private:
    void cleanup(PhysicsWorld& physics);

    JPH::Ragdoll* ragdoll_ = nullptr;              // Jolt ragdoll instance (manages bodies + constraints)
    std::vector<PhysicsBodyID> bodyIDs_;            // Cached body IDs from ragdoll
    std::vector<int32_t> jointIndices_;             // Maps part index -> skeleton joint index
    std::vector<float> effortFactors_;              // Torque scaling per part
    PhysicsWorld* ownerPhysics_ = nullptr;
};
