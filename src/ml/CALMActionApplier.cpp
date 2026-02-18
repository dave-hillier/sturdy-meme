#include "CALMActionApplier.h"
#include "GLTFLoader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <cmath>
#include <algorithm>

namespace ml {

CALMActionApplier::CALMActionApplier(const CALMCharacterConfig& config)
    : config_(config) {
    buildJointDOFRanges();
}

void CALMActionApplier::buildJointDOFRanges() {
    jointDOFRanges_.clear();
    if (config_.dofMappings.empty()) return;

    int32_t currentJoint = config_.dofMappings[0].jointIndex;
    int rangeStart = 0;
    int count = 1;

    for (size_t i = 1; i < config_.dofMappings.size(); ++i) {
        if (config_.dofMappings[i].jointIndex == currentJoint) {
            ++count;
        } else {
            jointDOFRanges_.push_back({currentJoint, rangeStart, count});
            currentJoint = config_.dofMappings[i].jointIndex;
            rangeStart = static_cast<int>(i);
            count = 1;
        }
    }
    jointDOFRanges_.push_back({currentJoint, rangeStart, count});
}

void CALMActionApplier::applyToSkeleton(const Tensor& actions,
                                         const Skeleton& skeleton,
                                         SkeletonPose& outPose) const {
    assert(static_cast<int>(actions.size()) == config_.actionDim);

    size_t numJoints = skeleton.joints.size();
    outPose.resize(numJoints);

    // Start from the skeleton's current local transforms
    for (size_t j = 0; j < numJoints; ++j) {
        outPose[j] = BonePose::fromMatrix(skeleton.joints[j].localTransform,
                                           skeleton.joints[j].preRotation);
    }

    // Override CALM-controlled joints with action-derived rotations
    for (const auto& range : jointDOFRanges_) {
        if (range.jointIndex < 0 ||
            static_cast<size_t>(range.jointIndex) >= numJoints) {
            continue;
        }

        glm::quat targetRot = buildJointRotation(range.jointIndex, actions);
        outPose[range.jointIndex].rotation = targetRot;
    }
}

void CALMActionApplier::actionsToTargetPose(const Tensor& actions,
                                             const Skeleton& skeleton,
                                             SkeletonPose& outPose) const {
    // Same as applyToSkeleton — builds the target pose from actions.
    // This is a separate method for clarity: the caller feeds this to
    // RagdollInstance::driveToTargetPose() instead of setting it on the skeleton.
    applyToSkeleton(actions, skeleton, outPose);
}

void CALMActionApplier::applyBlended(const Tensor& actions,
                                      const Skeleton& skeleton,
                                      const SkeletonPose& basePose,
                                      float blendWeight,
                                      SkeletonPose& outPose) const {
    // First get the full CALM pose
    SkeletonPose calmPose;
    applyToSkeleton(actions, skeleton, calmPose);

    // Blend with base pose
    AnimationBlend::blend(basePose, calmPose, blendWeight, outPose);
}

void CALMActionApplier::clampActions(Tensor& actions) const {
    assert(static_cast<int>(actions.size()) == config_.actionDim);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        actions[d] = std::clamp(actions[d], mapping.rangeMin, mapping.rangeMax);
    }
}

glm::quat CALMActionApplier::buildJointRotation(int32_t jointIndex,
                                                  const Tensor& actions) const {
    // Collect the Euler angles for this joint from the action vector
    float euler[3] = {0.0f, 0.0f, 0.0f};

    for (const auto& range : jointDOFRanges_) {
        if (range.jointIndex != jointIndex) continue;

        for (int d = 0; d < range.numDOFs; ++d) {
            int dofIdx = range.firstDOF + d;
            int axis = config_.dofMappings[dofIdx].axis;
            euler[axis] = actions[dofIdx];
        }
        break;
    }

    // Build quaternion from Euler angles (XYZ intrinsic order)
    glm::quat qx = glm::angleAxis(euler[0], glm::vec3(1, 0, 0));
    glm::quat qy = glm::angleAxis(euler[1], glm::vec3(0, 1, 0));
    glm::quat qz = glm::angleAxis(euler[2], glm::vec3(0, 0, 1));
    return qz * qy * qx;  // Intrinsic XYZ = extrinsic ZYX
}

} // namespace ml
