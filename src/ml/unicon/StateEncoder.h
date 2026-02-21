#pragma once

#include "../../physics/ArticulatedBody.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace ml::unicon {

// A single future target pose for the executor to track.
// All positions/rotations are in world space; the encoder transforms them
// to root-local coordinates when building the observation vector.
struct TargetFrame {
    glm::vec3 rootPosition{0.0f};
    glm::quat rootRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 rootLinearVelocity{0.0f};
    glm::vec3 rootAngularVelocity{0.0f};
    std::vector<glm::vec3> jointPositions;       // World-space per-joint positions
    std::vector<glm::quat> jointRotations;       // World-space per-joint rotations
    std::vector<glm::vec3> jointAngularVelocities;
};

// Builds the observation vector for the UniCon low-level policy.
//
// Per UniCon Equation 4, the observation is:
//   s_t = [o(X_t), o(X~_{t+1}), ..., o(X~_{t+tau}), y(X_t, X~_{t+1}), ..., y(X_t, X~_{t+tau})]
//
// Where o(X) encodes a character state in the root's local frame:
//   - Root height (1)
//   - Root rotation quaternion (4)
//   - Joint positions relative to root (3J)
//   - Joint rotation quaternions (4J)
//   - Root linear velocity in local frame (3)
//   - Root angular velocity in local frame (3)
//   - Joint angular velocities in local frame (3J)
//
// And y(X, X~) encodes relative root offset between actual and target:
//   - Horizontal position offset in root local frame (2)
//   - Height offset (1)
//   - Rotation offset quaternion (4)
//
// Total per-frame: 11 + 10J
// Total y per target: 7
// Full observation: (1 + tau) * (11 + 10J) + tau * 7
class StateEncoder {
public:
    void configure(size_t numJoints, size_t targetFrameCount);

    void encode(const ArticulatedBody& body,
                const PhysicsWorld& physics,
                const std::vector<TargetFrame>& targetFrames,
                std::vector<float>& observation) const;

    size_t getObservationDim() const;
    size_t getNumJoints() const { return numJoints_; }
    size_t getTargetFrameCount() const { return tau_; }
    size_t getFrameEncodingDim() const;

    static constexpr size_t ROOT_OFFSET_DIM = 7;

private:
    size_t encodeFrame(const glm::vec3& rootPos, const glm::quat& rootRot,
                       const glm::vec3& rootLinVel, const glm::vec3& rootAngVel,
                       const std::vector<glm::vec3>& jointPositions,
                       const std::vector<glm::quat>& jointRotations,
                       const std::vector<glm::vec3>& jointAngVels,
                       float* out) const;

    size_t encodeRootOffset(const glm::vec3& actualRootPos, const glm::quat& actualRootRot,
                            const glm::vec3& targetRootPos, const glm::quat& targetRootRot,
                            float* out) const;

    size_t numJoints_ = 0;
    size_t tau_ = 1;
};

} // namespace ml::unicon
