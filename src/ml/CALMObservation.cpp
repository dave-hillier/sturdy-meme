#include "CALMObservation.h"
#include "GLTFLoader.h"
#include "CharacterController.h"
#include "RagdollInstance.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cassert>

namespace ml {

CALMObservationExtractor::CALMObservationExtractor(const CALMCharacterConfig& config)
    : config_(config) {
    // Pre-allocate history frames
    for (auto& frame : history_) {
        frame.resize(config_.observationDim, 0.0f);
    }
    prevDOFPositions_.resize(config_.actionDim, 0.0f);
}

void CALMObservationExtractor::reset() {
    historyIndex_ = 0;
    historyCount_ = 0;
    hasPreviousFrame_ = false;
    std::fill(prevDOFPositions_.begin(), prevDOFPositions_.end(), 0.0f);
    prevRootRotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void CALMObservationExtractor::extractFrame(const Skeleton& skeleton,
                                             const CharacterController& controller,
                                             float deltaTime) {
    auto& frame = history_[historyIndex_];
    frame.clear();
    frame.reserve(config_.observationDim);

    extractRootFeatures(skeleton, controller, deltaTime, frame);
    extractDOFFeatures(skeleton, deltaTime, frame);
    extractKeyBodyFeatures(skeleton, frame);

    assert(static_cast<int>(frame.size()) == config_.observationDim);

    historyIndex_ = (historyIndex_ + 1) % MAX_OBS_HISTORY;
    if (historyCount_ < MAX_OBS_HISTORY) {
        ++historyCount_;
    }

    hasPreviousFrame_ = true;
}

Tensor CALMObservationExtractor::getCurrentObs() const {
    if (historyCount_ == 0) {
        return Tensor(config_.observationDim);
    }
    int latest = (historyIndex_ - 1 + MAX_OBS_HISTORY) % MAX_OBS_HISTORY;
    return Tensor(1, config_.observationDim,
                  std::vector<float>(history_[latest].begin(), history_[latest].end()));
}

Tensor CALMObservationExtractor::getStackedObs(int numSteps) const {
    int totalDim = numSteps * config_.observationDim;
    std::vector<float> stacked(totalDim, 0.0f);

    int available = std::min(numSteps, historyCount_);
    for (int s = 0; s < available; ++s) {
        // Stack from oldest to newest
        int frameIdx = (historyIndex_ - available + s + MAX_OBS_HISTORY) % MAX_OBS_HISTORY;
        int offset = s * config_.observationDim;
        const auto& frame = history_[frameIdx];
        std::copy(frame.begin(), frame.end(), stacked.begin() + offset);
    }

    return Tensor(1, totalDim, std::move(stacked));
}

Tensor CALMObservationExtractor::getEncoderObs() const {
    return getStackedObs(config_.numAMPEncObsSteps);
}

Tensor CALMObservationExtractor::getPolicyObs() const {
    return getStackedObs(config_.numAMPObsSteps);
}

// ---- Root features ----

void CALMObservationExtractor::extractRootFeatures(
    const Skeleton& skeleton,
    const CharacterController& controller,
    float deltaTime,
    std::vector<float>& obs) {

    // Root position and rotation
    glm::vec3 rootPos = controller.getPosition();
    const auto& rootJoint = skeleton.joints[config_.rootJointIndex];
    glm::quat rootRot = glm::quat_cast(rootJoint.localTransform);

    // 1) Root height (1D)
    obs.push_back(rootPos.y);

    // 2) Root rotation — heading-invariant 6D (6D)
    glm::quat headingFree = removeHeading(rootRot);
    float rot6d[6];
    quatToTanNorm6D(headingFree, rot6d);
    for (int i = 0; i < 6; ++i) {
        obs.push_back(rot6d[i]);
    }

    // 3) Local root velocity in heading frame (3D)
    float headingAngle = getHeadingAngle(rootRot);
    float cosH = std::cos(-headingAngle);
    float sinH = std::sin(-headingAngle);

    glm::vec3 worldVel = controller.getVelocity();
    glm::vec3 localVel;
    localVel.x = cosH * worldVel.x + sinH * worldVel.z;
    localVel.y = worldVel.y;
    localVel.z = -sinH * worldVel.x + cosH * worldVel.z;
    obs.push_back(localVel.x);
    obs.push_back(localVel.y);
    obs.push_back(localVel.z);

    // 4) Local root angular velocity (3D)
    if (hasPreviousFrame_ && deltaTime > 0.0f) {
        // Approximate angular velocity from quaternion difference
        glm::quat deltaRot = rootRot * glm::inverse(prevRootRotation_);
        // Convert to axis-angle
        float angle = 2.0f * std::acos(std::clamp(deltaRot.w, -1.0f, 1.0f));
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        float sinHalfAngle = std::sqrt(1.0f - deltaRot.w * deltaRot.w);
        if (sinHalfAngle > 1e-6f) {
            axis = glm::vec3(deltaRot.x, deltaRot.y, deltaRot.z) / sinHalfAngle;
        }
        glm::vec3 angVel = axis * (angle / deltaTime);
        // Rotate into heading frame
        glm::vec3 localAngVel;
        localAngVel.x = cosH * angVel.x + sinH * angVel.z;
        localAngVel.y = angVel.y;
        localAngVel.z = -sinH * angVel.x + cosH * angVel.z;
        obs.push_back(localAngVel.x);
        obs.push_back(localAngVel.y);
        obs.push_back(localAngVel.z);
    } else {
        obs.push_back(0.0f);
        obs.push_back(0.0f);
        obs.push_back(0.0f);
    }

    prevRootRotation_ = rootRot;
}

// ---- DOF features ----

void CALMObservationExtractor::extractDOFFeatures(
    const Skeleton& skeleton,
    float deltaTime,
    std::vector<float>& obs) {

    // Extract current DOF positions (joint angles)
    std::vector<float> currentDOFs(config_.actionDim, 0.0f);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        const auto& joint = skeleton.joints[mapping.jointIndex];

        // Decompose local transform to Euler angles
        glm::vec3 euler = matrixToEulerXYZ(joint.localTransform);
        currentDOFs[d] = euler[mapping.axis];
    }

    // DOF positions
    for (int d = 0; d < config_.actionDim; ++d) {
        obs.push_back(currentDOFs[d]);
    }

    // DOF velocities (finite difference)
    for (int d = 0; d < config_.actionDim; ++d) {
        if (hasPreviousFrame_ && deltaTime > 0.0f) {
            obs.push_back((currentDOFs[d] - prevDOFPositions_[d]) / deltaTime);
        } else {
            obs.push_back(0.0f);
        }
    }

    prevDOFPositions_ = currentDOFs;
}

// ---- Key body features ----

void CALMObservationExtractor::extractKeyBodyFeatures(
    const Skeleton& skeleton,
    std::vector<float>& obs) {

    // Compute global transforms
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Root position and heading for local frame conversion
    glm::vec3 rootPos(0.0f);
    float headingAngle = 0.0f;

    if (config_.rootJointIndex >= 0 &&
        static_cast<size_t>(config_.rootJointIndex) < globalTransforms.size()) {
        rootPos = glm::vec3(globalTransforms[config_.rootJointIndex][3]);
        glm::quat rootRot = glm::quat_cast(globalTransforms[config_.rootJointIndex]);
        headingAngle = getHeadingAngle(rootRot);
    }

    float cosH = std::cos(-headingAngle);
    float sinH = std::sin(-headingAngle);

    for (const auto& kb : config_.keyBodies) {
        if (kb.jointIndex >= 0 &&
            static_cast<size_t>(kb.jointIndex) < globalTransforms.size()) {
            glm::vec3 worldPos(globalTransforms[kb.jointIndex][3]);
            glm::vec3 relPos = worldPos - rootPos;

            // Rotate into heading frame
            float localX = cosH * relPos.x + sinH * relPos.z;
            float localY = relPos.y;
            float localZ = -sinH * relPos.x + cosH * relPos.z;

            obs.push_back(localX);
            obs.push_back(localY);
            obs.push_back(localZ);
        } else {
            obs.push_back(0.0f);
            obs.push_back(0.0f);
            obs.push_back(0.0f);
        }
    }
}

// ---- Static helpers ----

void CALMObservationExtractor::quatToTanNorm6D(const glm::quat& q, float out[6]) {
    // Convert quaternion to rotation matrix, take first two columns
    glm::mat3 m = glm::mat3_cast(q);
    // Column 0
    out[0] = m[0][0];
    out[1] = m[0][1];
    out[2] = m[0][2];
    // Column 1
    out[3] = m[1][0];
    out[4] = m[1][1];
    out[5] = m[1][2];
}

float CALMObservationExtractor::getHeadingAngle(const glm::quat& q) {
    // Project forward direction onto XZ plane, compute yaw
    glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, 1.0f);
    return std::atan2(forward.x, forward.z);
}

glm::quat CALMObservationExtractor::removeHeading(const glm::quat& q) {
    float heading = getHeadingAngle(q);
    glm::quat headingQuat = glm::angleAxis(-heading, glm::vec3(0.0f, 1.0f, 0.0f));
    return headingQuat * q;
}

glm::vec3 CALMObservationExtractor::matrixToEulerXYZ(const glm::mat4& m) {
    // Extract Euler angles (XYZ intrinsic order) from a rotation matrix
    glm::vec3 euler;
    float sy = m[0][2];
    if (std::abs(sy) < 0.99999f) {
        euler.x = std::atan2(-m[1][2], m[2][2]);
        euler.y = std::asin(sy);
        euler.z = std::atan2(-m[0][1], m[0][0]);
    } else {
        // Gimbal lock
        euler.x = std::atan2(m[2][1], m[1][1]);
        euler.y = (sy > 0.0f) ? 1.5707963f : -1.5707963f;
        euler.z = 0.0f;
    }
    return euler;
}

// ---- Ragdoll-based observation extraction ----

void CALMObservationExtractor::extractFrameFromRagdoll(
    const Skeleton& skeleton,
    const physics::RagdollInstance& ragdoll,
    float deltaTime) {

    auto& frame = history_[historyIndex_];
    frame.clear();
    frame.reserve(config_.observationDim);

    extractRootFeaturesFromRagdoll(skeleton, ragdoll, deltaTime, frame);
    extractDOFFeaturesFromRagdoll(skeleton, ragdoll, deltaTime, frame);
    extractKeyBodyFeatures(skeleton, frame);  // Reuse — works from skeleton global transforms

    assert(static_cast<int>(frame.size()) == config_.observationDim);

    historyIndex_ = (historyIndex_ + 1) % MAX_OBS_HISTORY;
    if (historyCount_ < MAX_OBS_HISTORY) {
        ++historyCount_;
    }

    hasPreviousFrame_ = true;
}

void CALMObservationExtractor::extractRootFeaturesFromRagdoll(
    const Skeleton& skeleton,
    const physics::RagdollInstance& ragdoll,
    float deltaTime,
    std::vector<float>& obs) {

    // Root position and rotation from ragdoll physics body
    glm::vec3 rootPos = ragdoll.getRootPosition();
    glm::quat rootRot = ragdoll.getRootRotation();

    // 1) Root height (1D)
    obs.push_back(rootPos.y);

    // 2) Root rotation — heading-invariant 6D (6D)
    glm::quat headingFree = removeHeading(rootRot);
    float rot6d[6];
    quatToTanNorm6D(headingFree, rot6d);
    for (int i = 0; i < 6; ++i) {
        obs.push_back(rot6d[i]);
    }

    // 3) Local root velocity in heading frame (3D)
    float headingAngle = getHeadingAngle(rootRot);
    float cosH = std::cos(-headingAngle);
    float sinH = std::sin(-headingAngle);

    // Exact velocity from physics instead of finite differences
    glm::vec3 worldVel = ragdoll.getRootLinearVelocity();
    glm::vec3 localVel;
    localVel.x = cosH * worldVel.x + sinH * worldVel.z;
    localVel.y = worldVel.y;
    localVel.z = -sinH * worldVel.x + cosH * worldVel.z;
    obs.push_back(localVel.x);
    obs.push_back(localVel.y);
    obs.push_back(localVel.z);

    // 4) Local root angular velocity (3D)
    // Exact angular velocity from physics — much more accurate than finite differences
    glm::vec3 angVel = ragdoll.getRootAngularVelocity();
    glm::vec3 localAngVel;
    localAngVel.x = cosH * angVel.x + sinH * angVel.z;
    localAngVel.y = angVel.y;
    localAngVel.z = -sinH * angVel.x + cosH * angVel.z;
    obs.push_back(localAngVel.x);
    obs.push_back(localAngVel.y);
    obs.push_back(localAngVel.z);

    prevRootRotation_ = rootRot;
}

void CALMObservationExtractor::extractDOFFeaturesFromRagdoll(
    const Skeleton& skeleton,
    const physics::RagdollInstance& ragdoll,
    float deltaTime,
    std::vector<float>& obs) {

    // Read current pose from ragdoll
    SkeletonPose ragdollPose;
    ragdoll.readPose(ragdollPose, skeleton);

    // Extract DOF positions from ragdoll pose
    std::vector<float> currentDOFs(config_.actionDim, 0.0f);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        if (mapping.jointIndex >= 0 &&
            static_cast<size_t>(mapping.jointIndex) < ragdollPose.size()) {
            const auto& bp = ragdollPose[mapping.jointIndex];
            // Convert rotation to Euler angles
            glm::mat4 rotMat = glm::mat4_cast(bp.rotation);
            glm::vec3 euler = matrixToEulerXYZ(rotMat);
            currentDOFs[d] = euler[mapping.axis];
        }
    }

    // DOF positions
    for (int d = 0; d < config_.actionDim; ++d) {
        obs.push_back(currentDOFs[d]);
    }

    // DOF velocities — use per-body angular velocities from physics
    // This is more accurate than finite differences
    std::vector<glm::vec3> angVels;
    ragdoll.readBodyAngularVelocities(angVels);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        if (mapping.jointIndex >= 0 &&
            static_cast<size_t>(mapping.jointIndex) < angVels.size()) {
            // Project angular velocity onto the DOF axis
            obs.push_back(angVels[mapping.jointIndex][mapping.axis]);
        } else {
            obs.push_back(0.0f);
        }
    }

    prevDOFPositions_ = currentDOFs;
}

} // namespace ml
