#include "StateEncoder.h"

#include <SDL3/SDL_log.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

void StateEncoder::configure(size_t numJoints, size_t targetFrameCount) {
    numJoints_ = numJoints;
    tau_ = targetFrameCount;

    SDL_Log("StateEncoder configured: %zu joints, tau=%zu, observation dim=%zu",
            numJoints_, tau_, getObservationDim());
}

size_t StateEncoder::getFrameEncodingDim() const {
    // Root height (1) + root quat (4) + joint positions (3J) + joint quats (4J)
    // + root lin vel (3) + root ang vel (3) + joint ang vels (3J)
    return 11 + 10 * numJoints_;
}

size_t StateEncoder::getObservationDim() const {
    // (1 + tau) frame encodings + tau root offsets
    return (1 + tau_) * getFrameEncodingDim() + tau_ * ROOT_OFFSET_DIM;
}

void StateEncoder::encode(const ArticulatedBody& body,
                           const PhysicsWorld& physics,
                           const std::vector<TargetFrame>& targetFrames,
                           std::vector<float>& observation) const {
    const size_t dim = getObservationDim();
    observation.resize(dim, 0.0f);

    if (body.getPartCount() == 0 || numJoints_ == 0) return;

    // Extract current physics state
    std::vector<ArticulatedBody::PartState> states;
    body.getState(states, physics);

    const size_t partCount = std::min(states.size(), numJoints_);

    // Current root state
    const glm::vec3& rootPos = states[0].position;
    const glm::quat& rootRot = states[0].rotation;
    const glm::vec3& rootLinVel = states[0].linearVelocity;
    const glm::vec3& rootAngVel = states[0].angularVelocity;

    // Gather joint positions, rotations, angular velocities from physics
    std::vector<glm::vec3> jointPositions(numJoints_);
    std::vector<glm::quat> jointRotations(numJoints_);
    std::vector<glm::vec3> jointAngVels(numJoints_);

    for (size_t i = 0; i < partCount; ++i) {
        jointPositions[i] = states[i].position;
        jointRotations[i] = states[i].rotation;
        jointAngVels[i] = states[i].angularVelocity;
    }

    float* out = observation.data();

    // Encode current state o(X_t)
    size_t written = encodeFrame(rootPos, rootRot, rootLinVel, rootAngVel,
                                 jointPositions, jointRotations, jointAngVels, out);
    out += written;

    // Encode each target frame o(X~_{t+k}) for k = 1..tau
    const size_t numTargets = std::min(targetFrames.size(), tau_);
    for (size_t k = 0; k < numTargets; ++k) {
        const auto& target = targetFrames[k];

        // Pad target joint arrays if smaller than numJoints_
        std::vector<glm::vec3> tJointPos(numJoints_, glm::vec3(0.0f));
        std::vector<glm::quat> tJointRot(numJoints_, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        std::vector<glm::vec3> tJointAngVel(numJoints_, glm::vec3(0.0f));

        size_t count = std::min(target.jointPositions.size(), numJoints_);
        for (size_t i = 0; i < count; ++i) tJointPos[i] = target.jointPositions[i];

        count = std::min(target.jointRotations.size(), numJoints_);
        for (size_t i = 0; i < count; ++i) tJointRot[i] = target.jointRotations[i];

        count = std::min(target.jointAngularVelocities.size(), numJoints_);
        for (size_t i = 0; i < count; ++i) tJointAngVel[i] = target.jointAngularVelocities[i];

        written = encodeFrame(target.rootPosition, target.rootRotation,
                              target.rootLinearVelocity, target.rootAngularVelocity,
                              tJointPos, tJointRot, tJointAngVel, out);
        out += written;
    }

    // Zero-pad any missing target frames
    for (size_t k = numTargets; k < tau_; ++k) {
        std::fill_n(out, getFrameEncodingDim(), 0.0f);
        out += getFrameEncodingDim();
    }

    // Encode root offsets y(X_t, X~_{t+k}) for k = 1..tau
    for (size_t k = 0; k < numTargets; ++k) {
        const auto& target = targetFrames[k];
        written = encodeRootOffset(rootPos, rootRot,
                                   target.rootPosition, target.rootRotation, out);
        out += written;
    }

    // Zero-pad any missing root offsets
    for (size_t k = numTargets; k < tau_; ++k) {
        std::fill_n(out, ROOT_OFFSET_DIM, 0.0f);
        out += ROOT_OFFSET_DIM;
    }
}

size_t StateEncoder::encodeFrame(const glm::vec3& rootPos, const glm::quat& rootRot,
                                  const glm::vec3& rootLinVel, const glm::vec3& rootAngVel,
                                  const std::vector<glm::vec3>& jointPositions,
                                  const std::vector<glm::quat>& jointRotations,
                                  const std::vector<glm::vec3>& jointAngVels,
                                  float* out) const {
    float* start = out;

    // Compute inverse root rotation for transforming to root-local frame.
    // Root heading = rotation around Y axis only (remove pitch/roll).
    float yaw = atan2f(2.0f * (rootRot.w * rootRot.y + rootRot.x * rootRot.z),
                       1.0f - 2.0f * (rootRot.y * rootRot.y + rootRot.z * rootRot.z));
    glm::quat headingRot = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat headingInv = glm::inverse(headingRot);

    // 1. Root height (Y-up)
    *out++ = rootPos.y;

    // 2. Root rotation quaternion in heading-local frame
    // This captures pitch/roll relative to heading direction
    glm::quat localRot = headingInv * rootRot;
    *out++ = localRot.w;
    *out++ = localRot.x;
    *out++ = localRot.y;
    *out++ = localRot.z;

    // 3. Joint positions relative to root, rotated into heading frame (3J)
    for (size_t i = 0; i < numJoints_; ++i) {
        glm::vec3 relPos = headingInv * (jointPositions[i] - rootPos);
        *out++ = relPos.x;
        *out++ = relPos.y;
        *out++ = relPos.z;
    }

    // 4. Joint rotation quaternions in heading-local frame (4J)
    for (size_t i = 0; i < numJoints_; ++i) {
        glm::quat localJointRot = headingInv * jointRotations[i];
        *out++ = localJointRot.w;
        *out++ = localJointRot.x;
        *out++ = localJointRot.y;
        *out++ = localJointRot.z;
    }

    // 5. Root linear velocity in heading-local frame (3)
    glm::vec3 localLinVel = headingInv * rootLinVel;
    *out++ = localLinVel.x;
    *out++ = localLinVel.y;
    *out++ = localLinVel.z;

    // 6. Root angular velocity in heading-local frame (3)
    glm::vec3 localAngVel = headingInv * rootAngVel;
    *out++ = localAngVel.x;
    *out++ = localAngVel.y;
    *out++ = localAngVel.z;

    // 7. Joint angular velocities in heading-local frame (3J)
    for (size_t i = 0; i < numJoints_; ++i) {
        glm::vec3 localJAngVel = headingInv * jointAngVels[i];
        *out++ = localJAngVel.x;
        *out++ = localJAngVel.y;
        *out++ = localJAngVel.z;
    }

    return static_cast<size_t>(out - start);
}

size_t StateEncoder::encodeRootOffset(const glm::vec3& actualRootPos, const glm::quat& actualRootRot,
                                       const glm::vec3& targetRootPos, const glm::quat& targetRootRot,
                                       float* out) const {
    // Transform position offset into actual root's heading-local frame
    float yaw = atan2f(2.0f * (actualRootRot.w * actualRootRot.y + actualRootRot.x * actualRootRot.z),
                       1.0f - 2.0f * (actualRootRot.y * actualRootRot.y + actualRootRot.z * actualRootRot.z));
    glm::quat headingRot = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat headingInv = glm::inverse(headingRot);

    glm::vec3 worldOffset = targetRootPos - actualRootPos;
    glm::vec3 localOffset = headingInv * worldOffset;

    // 1. Horizontal position offset in root local frame (2: X, Z)
    *out++ = localOffset.x;
    *out++ = localOffset.z;

    // 2. Height offset (1)
    *out++ = localOffset.y;

    // 3. Rotation offset quaternion (4)
    // q_offset = inv(actual_heading) * target_rotation
    glm::quat rotOffset = headingInv * targetRootRot;
    *out++ = rotOffset.w;
    *out++ = rotOffset.x;
    *out++ = rotOffset.y;
    *out++ = rotOffset.z;

    return ROOT_OFFSET_DIM;
}
