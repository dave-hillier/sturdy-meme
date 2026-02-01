#include "MotionMatchingFeature.h"
#include "Animation.h"
#include "AnimationBlend.h"
#include "GLTFLoader.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace MotionMatching {

// Trajectory cost computation
float Trajectory::computeCost(const Trajectory& other,
                               float positionWeight,
                               float velocityWeight,
                               float facingWeight) const {
    if (sampleCount == 0 || other.sampleCount == 0) {
        return 0.0f;
    }

    float totalCost = 0.0f;
    size_t comparisons = 0;

    // Compare samples at matching time offsets
    for (size_t i = 0; i < sampleCount; ++i) {
        // Find closest sample in other trajectory
        size_t bestMatch = 0;
        float bestTimeDiff = std::abs(samples[i].timeOffset - other.samples[0].timeOffset);

        for (size_t j = 1; j < other.sampleCount; ++j) {
            float timeDiff = std::abs(samples[i].timeOffset - other.samples[j].timeOffset);
            if (timeDiff < bestTimeDiff) {
                bestTimeDiff = timeDiff;
                bestMatch = j;
            }
        }

        // Only compare if time offsets are reasonably close
        if (bestTimeDiff < 0.15f) {
            const auto& s1 = samples[i];
            const auto& s2 = other.samples[bestMatch];

            float posCost = glm::length(s1.position - s2.position) * positionWeight;
            float velCost = glm::length(s1.velocity - s2.velocity) * velocityWeight;

            // Facing cost using dot product (1 - dot gives 0 for same direction, 2 for opposite)
            // Guard against zero-length facing vectors to avoid NaN
            float facingCost = 0.0f;
            float s1FacingLen = glm::length(s1.facing);
            float s2FacingLen = glm::length(s2.facing);
            if (s1FacingLen > 0.001f && s2FacingLen > 0.001f) {
                glm::vec3 s1Norm = s1.facing / s1FacingLen;
                glm::vec3 s2Norm = s2.facing / s2FacingLen;
                float facingDot = glm::dot(s1Norm, s2Norm);
                facingCost = (1.0f - facingDot) * facingWeight;
            }

            totalCost += posCost + velCost + facingCost;
            ++comparisons;
        }
    }

    return comparisons > 0 ? totalCost / static_cast<float>(comparisons) : 0.0f;
}

// Normalized trajectory cost computation
float Trajectory::computeNormalizedCost(const Trajectory& other,
                                         const FeatureNormalization& norm,
                                         float positionWeight,
                                         float velocityWeight,
                                         float facingWeight) const {
    if (sampleCount == 0 || other.sampleCount == 0) {
        return 0.0f;
    }

    float totalCost = 0.0f;
    size_t comparisons = 0;

    for (size_t i = 0; i < sampleCount; ++i) {
        // Find closest sample in other trajectory
        size_t bestMatch = 0;
        float bestTimeDiff = std::abs(samples[i].timeOffset - other.samples[0].timeOffset);

        for (size_t j = 1; j < other.sampleCount; ++j) {
            float timeDiff = std::abs(samples[i].timeOffset - other.samples[j].timeOffset);
            if (timeDiff < bestTimeDiff) {
                bestTimeDiff = timeDiff;
                bestMatch = j;
            }
        }

        if (bestTimeDiff < 0.15f) {
            const auto& s1 = samples[i];
            const auto& s2 = other.samples[bestMatch];

            // Normalize position and velocity differences
            float posDiff = glm::length(s1.position - s2.position);
            float velDiff = glm::length(s1.velocity - s2.velocity);

            // Use normalization if available for this sample index
            if (norm.isComputed && i < MAX_TRAJECTORY_SAMPLES) {
                posDiff = norm.trajectoryPosition[i].normalize(posDiff);
                velDiff = norm.trajectoryVelocity[i].normalize(velDiff);
            }

            float posCost = posDiff * positionWeight;
            float velCost = velDiff * velocityWeight;

            // Facing cost (already normalized: 0 for same, 2 for opposite)
            float facingCost = 0.0f;
            float s1FacingLen = glm::length(s1.facing);
            float s2FacingLen = glm::length(s2.facing);
            if (s1FacingLen > 0.001f && s2FacingLen > 0.001f) {
                glm::vec3 s1Norm = s1.facing / s1FacingLen;
                glm::vec3 s2Norm = s2.facing / s2FacingLen;
                float facingDot = glm::dot(s1Norm, s2Norm);
                facingCost = (1.0f - facingDot) * facingWeight;
            }

            totalCost += posCost + velCost + facingCost;
            ++comparisons;
        }
    }

    return comparisons > 0 ? totalCost / static_cast<float>(comparisons) : 0.0f;
}

// PoseFeatures cost computation
float PoseFeatures::computeCost(const PoseFeatures& other,
                                 float boneWeight,
                                 float rootVelWeight,
                                 float angularVelWeight,
                                 float phaseWeight) const {
    float totalCost = 0.0f;

    // Bone feature costs
    size_t minBones = std::min(boneCount, other.boneCount);
    for (size_t i = 0; i < minBones; ++i) {
        totalCost += boneFeatures[i].computeCost(other.boneFeatures[i], 1.0f, 0.5f) * boneWeight;
    }
    if (minBones > 0) {
        totalCost /= static_cast<float>(minBones);
    }

    // Root velocity cost
    totalCost += glm::length(rootVelocity - other.rootVelocity) * rootVelWeight;

    // Angular velocity cost
    totalCost += std::abs(rootAngularVelocity - other.rootAngularVelocity) * angularVelWeight;

    // Phase costs (wrap-aware difference)
    auto phaseDiff = [](float a, float b) {
        float diff = std::abs(a - b);
        return std::min(diff, 1.0f - diff);
    };
    totalCost += phaseDiff(leftFootPhase, other.leftFootPhase) * phaseWeight;
    totalCost += phaseDiff(rightFootPhase, other.rightFootPhase) * phaseWeight;

    return totalCost;
}

// Normalized PoseFeatures cost computation
float PoseFeatures::computeNormalizedCost(const PoseFeatures& other,
                                           const FeatureNormalization& norm,
                                           float boneWeight,
                                           float rootVelWeight,
                                           float angularVelWeight,
                                           float phaseWeight) const {
    float totalCost = 0.0f;

    // Bone feature costs (normalized)
    size_t minBones = std::min(boneCount, other.boneCount);
    for (size_t i = 0; i < minBones; ++i) {
        float posDiff = glm::length(boneFeatures[i].position - other.boneFeatures[i].position);
        float velDiff = glm::length(boneFeatures[i].velocity - other.boneFeatures[i].velocity);

        // Apply normalization
        if (norm.isComputed && i < MAX_FEATURE_BONES) {
            posDiff = norm.bonePosition[i].normalize(posDiff);
            velDiff = norm.boneVelocity[i].normalize(velDiff);
        }

        totalCost += (posDiff + velDiff * 0.5f) * boneWeight;
    }
    if (minBones > 0) {
        totalCost /= static_cast<float>(minBones);
    }

    // Root velocity cost (normalized)
    float rootVelDiff = glm::length(rootVelocity - other.rootVelocity);
    if (norm.isComputed) {
        rootVelDiff = norm.rootVelocity.normalize(rootVelDiff);
    }
    totalCost += rootVelDiff * rootVelWeight;

    // Angular velocity cost (normalized)
    float angVelDiff = std::abs(rootAngularVelocity - other.rootAngularVelocity);
    if (norm.isComputed) {
        angVelDiff = norm.rootAngularVelocity.normalize(angVelDiff);
    }
    totalCost += angVelDiff * angularVelWeight;

    // Phase costs (already normalized 0-1, no change needed)
    auto phaseDiff = [](float a, float b) {
        float diff = std::abs(a - b);
        return std::min(diff, 1.0f - diff);
    };
    totalCost += phaseDiff(leftFootPhase, other.leftFootPhase) * phaseWeight;
    totalCost += phaseDiff(rightFootPhase, other.rightFootPhase) * phaseWeight;

    return totalCost;
}

// FeatureExtractor implementation
void FeatureExtractor::initialize(const Skeleton& skeleton, const FeatureConfig& config) {
    config_ = config;
    featureBoneIndices_.clear();

    // Find bone indices for feature bones
    for (const auto& boneName : config_.featureBoneNames) {
        int32_t index = skeleton.findJointIndex(boneName);
        if (index >= 0) {
            featureBoneIndices_.push_back(index);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                       "MotionMatching: Feature bone '%s' not found in skeleton",
                       boneName.c_str());
        }
    }

    // Find root bone (usually "Hips" or first bone)
    rootBoneIndex_ = skeleton.findJointIndex("Hips");
    if (rootBoneIndex_ < 0) {
        rootBoneIndex_ = skeleton.findJointIndex("mixamorig:Hips");
    }
    if (rootBoneIndex_ < 0 && !skeleton.joints.empty()) {
        // Find root as the joint with no parent
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            if (skeleton.joints[i].parentIndex < 0) {
                rootBoneIndex_ = static_cast<int32_t>(i);
                break;
            }
        }
    }

    // Find heading bone for orientation queries (strafe support)
    headingBoneIndex_ = skeleton.findJointIndex(config_.headingBoneName);
    if (headingBoneIndex_ < 0) {
        headingBoneIndex_ = skeleton.findJointIndex("mixamorig:" + config_.headingBoneName);
    }
    if (headingBoneIndex_ < 0) {
        // Fall back to root bone if heading bone not found
        headingBoneIndex_ = rootBoneIndex_;
    }

    // Set strafe mode from config
    strafeMode_ = config_.strafeMode;

    initialized_ = true;
    SDL_Log("MotionMatching: FeatureExtractor initialized with %zu feature bones, heading bone: %d",
            featureBoneIndices_.size(), headingBoneIndex_);
}

glm::vec3 FeatureExtractor::computeBonePosition(const Skeleton& skeleton,
                                                  const SkeletonPose& pose,
                                                  int32_t boneIndex) const {
    if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= pose.size()) {
        return glm::vec3(0.0f);
    }

    // Compute bone position by walking up the hierarchy
    glm::mat4 localToWorld = pose[boneIndex].toMatrix(skeleton.joints[boneIndex].preRotation);
    int32_t parentIdx = skeleton.joints[boneIndex].parentIndex;

    while (parentIdx >= 0 && static_cast<size_t>(parentIdx) < pose.size()) {
        glm::mat4 parentMat = pose[parentIdx].toMatrix(skeleton.joints[parentIdx].preRotation);
        localToWorld = parentMat * localToWorld;
        parentIdx = skeleton.joints[parentIdx].parentIndex;
    }

    return glm::vec3(localToWorld[3]);
}

glm::mat4 FeatureExtractor::computeRootTransform(const Skeleton& skeleton,
                                                   const SkeletonPose& pose) const {
    if (rootBoneIndex_ < 0 || static_cast<size_t>(rootBoneIndex_) >= pose.size()) {
        return glm::mat4(1.0f);
    }

    return pose[rootBoneIndex_].toMatrix(skeleton.joints[rootBoneIndex_].preRotation);
}

PoseFeatures FeatureExtractor::extractFromPose(const Skeleton& skeleton,
                                                 const SkeletonPose& pose,
                                                 const SkeletonPose& prevPose,
                                                 float deltaTime) const {
    PoseFeatures features;

    if (!initialized_ || pose.size() == 0) {
        return features;
    }

    // Extract bone features
    features.boneCount = std::min(featureBoneIndices_.size(), MAX_FEATURE_BONES);
    for (size_t i = 0; i < features.boneCount; ++i) {
        int32_t boneIdx = featureBoneIndices_[i];

        // Current position
        glm::vec3 currentPos = computeBonePosition(skeleton, pose, boneIdx);
        features.boneFeatures[i].position = currentPos;

        // Velocity from previous pose
        if (prevPose.size() > 0 && deltaTime > 0.0f) {
            glm::vec3 prevPos = computeBonePosition(skeleton, prevPose, boneIdx);
            features.boneFeatures[i].velocity = (currentPos - prevPos) / deltaTime;
        }
    }

    // Extract root velocity and angular velocity
    if (rootBoneIndex_ >= 0 && static_cast<size_t>(rootBoneIndex_) < pose.size()) {
        glm::mat4 rootTransform = computeRootTransform(skeleton, pose);
        glm::vec3 rootPos = glm::vec3(rootTransform[3]);

        if (prevPose.size() > 0 && deltaTime > 0.0f) {
            glm::mat4 prevRootTransform = computeRootTransform(skeleton, prevPose);
            glm::vec3 prevRootPos = glm::vec3(prevRootTransform[3]);
            features.rootVelocity = (rootPos - prevRootPos) / deltaTime;
            // Keep only horizontal velocity
            features.rootVelocity.y = 0.0f;

            // Compute angular velocity (Y-axis rotation rate)
            // Extract facing direction from both transforms (Z axis, flattened)
            glm::vec3 currentFacing = glm::normalize(glm::vec3(rootTransform[2].x, 0.0f, rootTransform[2].z));
            glm::vec3 prevFacing = glm::normalize(glm::vec3(prevRootTransform[2].x, 0.0f, prevRootTransform[2].z));

            // Compute signed angle between facing directions
            float dot = glm::clamp(glm::dot(prevFacing, currentFacing), -1.0f, 1.0f);
            float angle = std::acos(dot);
            // Determine sign using cross product (Y component)
            float cross = prevFacing.x * currentFacing.z - prevFacing.z * currentFacing.x;
            if (cross < 0.0f) {
                angle = -angle;
            }
            features.rootAngularVelocity = angle / deltaTime;
        }
    }

    // Extract heading feature for strafe/orientation queries
    if (config_.headingWeight > 0.0f) {
        // Use root velocity as movement direction for heading calculation
        features.heading = extractHeadingFromPose(skeleton, pose, features.rootVelocity);
    }

    return features;
}

PoseFeatures FeatureExtractor::extractFromClip(const AnimationClip& clip,
                                                 const Skeleton& skeleton,
                                                 float time,
                                                 float deltaTime) const {
    if (!initialized_) {
        return PoseFeatures{};
    }

    // Create a temporary skeleton copy for sampling
    Skeleton tempSkeleton = skeleton;

    // Sample current pose
    clip.sample(time, tempSkeleton, false);

    // Build current pose from skeleton
    SkeletonPose currentPose;
    currentPose.resize(tempSkeleton.joints.size());
    for (size_t i = 0; i < tempSkeleton.joints.size(); ++i) {
        currentPose[i] = BonePose::fromMatrix(tempSkeleton.joints[i].localTransform,
                                               tempSkeleton.joints[i].preRotation);
    }

    // Sample previous pose
    float prevTime = std::max(0.0f, time - deltaTime);
    Skeleton prevSkeleton = skeleton;
    clip.sample(prevTime, prevSkeleton, false);

    SkeletonPose prevPose;
    prevPose.resize(prevSkeleton.joints.size());
    for (size_t i = 0; i < prevSkeleton.joints.size(); ++i) {
        prevPose[i] = BonePose::fromMatrix(prevSkeleton.joints[i].localTransform,
                                            prevSkeleton.joints[i].preRotation);
    }

    return extractFromPose(skeleton, currentPose, prevPose, deltaTime);
}

Trajectory FeatureExtractor::extractTrajectoryFromClip(const AnimationClip& clip,
                                                         const Skeleton& skeleton,
                                                         float currentTime) const {
    Trajectory trajectory;

    if (!initialized_ || rootBoneIndex_ < 0) {
        return trajectory;
    }

    // First, get the reference position at currentTime (the "current" frame)
    // This makes trajectory positions relative to where the character is now
    Skeleton refSkeleton = skeleton;
    clip.sample(currentTime, refSkeleton, false);
    glm::vec3 refPosition = glm::vec3(refSkeleton.joints[rootBoneIndex_].localTransform[3]);
    refPosition.y = 0.0f;

    for (float timeOffset : config_.trajectorySampleTimes) {
        float sampleTime = currentTime + timeOffset;

        // Wrap time within clip duration
        if (clip.duration > 0.0f) {
            while (sampleTime < 0.0f) sampleTime += clip.duration;
            while (sampleTime >= clip.duration) sampleTime -= clip.duration;
        }

        // Sample the clip at this time
        Skeleton tempSkeleton = skeleton;
        clip.sample(sampleTime, tempSkeleton, false);

        // Get root position and facing
        const auto& rootJoint = tempSkeleton.joints[rootBoneIndex_];
        glm::mat4 rootMat = rootJoint.localTransform;

        TrajectorySample sample;
        sample.timeOffset = timeOffset;

        // Position relative to reference (current frame)
        glm::vec3 absPosition = glm::vec3(rootMat[3]);
        absPosition.y = 0.0f;
        sample.position = absPosition - refPosition;

        // Facing is the forward direction (Z axis)
        sample.facing = glm::normalize(glm::vec3(rootMat[2]));
        sample.facing.y = 0.0f;
        if (glm::length(sample.facing) > 0.01f) {
            sample.facing = glm::normalize(sample.facing);
        } else {
            sample.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // Velocity is computed from position delta
        float velDelta = 1.0f / 60.0f;
        float velTime = sampleTime + velDelta;
        if (clip.duration > 0.0f) {
            while (velTime >= clip.duration) velTime -= clip.duration;
        }

        Skeleton velSkeleton = skeleton;
        clip.sample(velTime, velSkeleton, false);
        glm::vec3 velPos = glm::vec3(velSkeleton.joints[rootBoneIndex_].localTransform[3]);
        velPos.y = 0.0f;

        sample.velocity = (velPos - absPosition) / velDelta;

        trajectory.addSample(sample);
    }

    return trajectory;
}

glm::mat4 FeatureExtractor::computeBoneWorldTransform(const Skeleton& skeleton,
                                                        const SkeletonPose& pose,
                                                        int32_t boneIndex) const {
    if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= pose.size()) {
        return glm::mat4(1.0f);
    }

    // Walk up the hierarchy to compute world transform
    glm::mat4 worldTransform = pose[boneIndex].toMatrix(skeleton.joints[boneIndex].preRotation);
    int32_t parentIdx = skeleton.joints[boneIndex].parentIndex;

    while (parentIdx >= 0 && static_cast<size_t>(parentIdx) < pose.size()) {
        glm::mat4 parentMat = pose[parentIdx].toMatrix(skeleton.joints[parentIdx].preRotation);
        worldTransform = parentMat * worldTransform;
        parentIdx = skeleton.joints[parentIdx].parentIndex;
    }

    return worldTransform;
}

glm::vec3 FeatureExtractor::extractHeadingDirection(const glm::mat4& boneTransform,
                                                      HeadingAxis axis,
                                                      ComponentStrip strip) const {
    // Extract the appropriate axis from the transform
    glm::vec3 direction;
    switch (axis) {
        case HeadingAxis::X:
            direction = glm::vec3(boneTransform[0]);  // X axis
            break;
        case HeadingAxis::Y:
            direction = glm::vec3(boneTransform[1]);  // Y axis
            break;
        case HeadingAxis::Z:
        default:
            direction = glm::vec3(boneTransform[2]);  // Z axis (forward)
            break;
    }

    // Apply component stripping
    switch (strip) {
        case ComponentStrip::StripY:
            direction.y = 0.0f;  // Horizontal only
            break;
        case ComponentStrip::StripXZ:
            direction.x = 0.0f;
            direction.z = 0.0f;  // Vertical only
            break;
        case ComponentStrip::None:
        default:
            break;
    }

    // Normalize the result
    float len = glm::length(direction);
    if (len > 0.001f) {
        direction /= len;
    } else {
        // Default to forward if zero length
        direction = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    return direction;
}

HeadingFeature FeatureExtractor::extractHeadingFromPose(const Skeleton& skeleton,
                                                          const SkeletonPose& pose,
                                                          const glm::vec3& movementDirection) const {
    HeadingFeature heading;

    if (!initialized_ || headingBoneIndex_ < 0) {
        return heading;
    }

    // Compute heading bone world transform
    glm::mat4 boneTransform = computeBoneWorldTransform(skeleton, pose, headingBoneIndex_);

    // Extract heading direction based on config
    heading.direction = extractHeadingDirection(boneTransform,
                                                  config_.headingAxis,
                                                  config_.headingComponentStrip);

    // Store movement direction
    heading.movementDirection = movementDirection;

    // Compute angle between heading and movement
    if (glm::length(movementDirection) > 0.001f) {
        glm::vec3 normMovement = glm::normalize(movementDirection);
        float dot = glm::clamp(glm::dot(heading.direction, normMovement), -1.0f, 1.0f);
        heading.angleDifference = std::acos(dot);

        // Determine sign using cross product (for left vs right strafe)
        float cross = heading.direction.x * normMovement.z - heading.direction.z * normMovement.x;
        if (cross < 0.0f) {
            heading.angleDifference = -heading.angleDifference;
        }
    }

    return heading;
}

} // namespace MotionMatching
