#include "RagdollBuilder.h"
#include "JoltLayerConfig.h"
#include "PhysicsConversions.h"
#include "GLTFLoader.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Skeleton/Skeleton.h>
#include <Jolt/Skeleton/SkeletonPose.h>

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <numeric>

JPH_SUPPRESS_WARNINGS

using namespace PhysicsConversions;

namespace physics {

float RagdollBuilder::computeBoneLength(const glm::mat4& parentGlobal,
                                         const glm::mat4& childGlobal) {
    glm::vec3 parentPos(parentGlobal[3]);
    glm::vec3 childPos(childGlobal[3]);
    return glm::length(childPos - parentPos);
}

float RagdollBuilder::estimateRadius(float boneLength,
                                      const std::string& boneName,
                                      const RagdollConfig& config) {
    // Check per-bone override
    auto it = config.boneOverrides.find(boneName);
    if (it != config.boneOverrides.end() && it->second.radius > 0.0f) {
        return it->second.radius;
    }

    float r = boneLength * config.radiusFraction;
    return std::clamp(r, config.minRadius, config.maxRadius);
}

float RagdollBuilder::capsuleVolume(float halfHeight, float radius) {
    // Volume of a capsule = cylinder + sphere
    float cylinderVol = 3.14159265f * radius * radius * (2.0f * halfHeight);
    float sphereVol = (4.0f / 3.0f) * 3.14159265f * radius * radius * radius;
    return cylinderVol + sphereVol;
}

JPH::Ref<JPH::RagdollSettings> RagdollBuilder::build(
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& globalBindPose,
    const RagdollConfig& config) {

    const size_t numJoints = skeleton.joints.size();
    if (numJoints == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RagdollBuilder: empty skeleton");
        return nullptr;
    }

    // 1. Build Jolt skeleton
    JPH::Ref<JPH::Skeleton> joltSkeleton = new JPH::Skeleton();
    for (size_t i = 0; i < numJoints; ++i) {
        const auto& joint = skeleton.joints[i];
        joltSkeleton->AddJoint(JPH::String(joint.name.c_str()), joint.parentIndex);
    }
    joltSkeleton->CalculateParentJointIndices();

    // 2. Create ragdoll settings
    auto ragdollSettings = new JPH::RagdollSettings();
    ragdollSettings->mSkeleton = joltSkeleton;
    ragdollSettings->mParts.resize(numJoints);

    // 3. Compute bone lengths and volumes for mass distribution
    struct BoneInfo {
        float length = 0.1f;
        float radius = 0.03f;
        float halfHeight = 0.02f;
        float volume = 0.001f;
        glm::vec3 midpoint{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        bool hasChildren = false;
    };
    std::vector<BoneInfo> boneInfos(numJoints);

    // Find children for each joint to compute bone directions
    std::vector<std::vector<int>> children(numJoints);
    for (size_t i = 0; i < numJoints; ++i) {
        int parent = skeleton.joints[i].parentIndex;
        if (parent >= 0 && static_cast<size_t>(parent) < numJoints) {
            children[parent].push_back(static_cast<int>(i));
        }
    }

    for (size_t i = 0; i < numJoints; ++i) {
        auto& info = boneInfos[i];
        const auto& joint = skeleton.joints[i];
        glm::vec3 jointPos(globalBindPose[i][3]);

        float lengthScale = 1.0f;
        auto overrideIt = config.boneOverrides.find(joint.name);
        if (overrideIt != config.boneOverrides.end()) {
            lengthScale = overrideIt->second.lengthScale;
        }

        if (!children[i].empty()) {
            info.hasChildren = true;
            // Average child position for bone direction
            glm::vec3 avgChildPos(0.0f);
            for (int c : children[i]) {
                avgChildPos += glm::vec3(globalBindPose[c][3]);
            }
            avgChildPos /= static_cast<float>(children[i].size());

            glm::vec3 boneDir = avgChildPos - jointPos;
            info.length = glm::length(boneDir) * lengthScale;

            if (info.length > 0.001f) {
                info.midpoint = jointPos + boneDir * 0.5f;

                // Orient capsule along bone direction
                // Jolt capsules are Y-axis aligned, so we need rotation from Y to boneDir
                glm::vec3 dir = glm::normalize(boneDir);
                glm::vec3 yAxis(0.0f, 1.0f, 0.0f);
                float dot = glm::dot(yAxis, dir);
                if (dot > 0.9999f) {
                    info.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                } else if (dot < -0.9999f) {
                    info.orientation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
                } else {
                    glm::vec3 cross = glm::cross(yAxis, dir);
                    info.orientation = glm::normalize(glm::quat(1.0f + dot, cross.x, cross.y, cross.z));
                }
            } else {
                info.midpoint = jointPos;
            }
        } else {
            // Leaf joint — use parent direction scaled down
            info.hasChildren = false;
            if (joint.parentIndex >= 0) {
                glm::vec3 parentPos(globalBindPose[joint.parentIndex][3]);
                glm::vec3 boneDir = jointPos - parentPos;
                info.length = glm::length(boneDir) * 0.5f * lengthScale;  // Half the parent bone length
                if (info.length > 0.001f) {
                    info.midpoint = jointPos + glm::normalize(boneDir) * info.length * 0.5f;
                    glm::vec3 dir = glm::normalize(boneDir);
                    glm::vec3 yAxis(0.0f, 1.0f, 0.0f);
                    float dot = glm::dot(yAxis, dir);
                    if (dot > 0.9999f) {
                        info.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    } else if (dot < -0.9999f) {
                        info.orientation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
                    } else {
                        glm::vec3 cross = glm::cross(yAxis, dir);
                        info.orientation = glm::normalize(glm::quat(1.0f + dot, cross.x, cross.y, cross.z));
                    }
                } else {
                    info.midpoint = jointPos;
                }
            } else {
                info.length = 0.1f;
                info.midpoint = jointPos;
            }
        }

        info.radius = estimateRadius(info.length, joint.name, config);
        info.halfHeight = std::max(0.0f, info.length * 0.5f - info.radius);
        info.volume = capsuleVolume(info.halfHeight, info.radius);
    }

    // 4. Distribute mass proportional to volume
    float totalVolume = 0.0f;
    for (const auto& info : boneInfos) {
        totalVolume += info.volume;
    }
    if (totalVolume < 0.0001f) totalVolume = 1.0f;

    // 5. Create body settings and constraints for each part
    for (size_t i = 0; i < numJoints; ++i) {
        auto& part = ragdollSettings->mParts[i];
        const auto& joint = skeleton.joints[i];
        const auto& info = boneInfos[i];

        // Create capsule shape (or sphere for very short bones)
        JPH::Ref<JPH::Shape> shape;
        if (info.halfHeight < 0.001f) {
            shape = new JPH::SphereShape(info.radius);
        } else {
            shape = new JPH::CapsuleShape(info.halfHeight, info.radius);
        }

        // Offset shape to bone midpoint relative to joint position
        glm::vec3 jointPos(globalBindPose[i][3]);
        glm::vec3 localOffset = info.midpoint - jointPos;
        if (glm::length(localOffset) > 0.001f || info.orientation != glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
            JPH::RotatedTranslatedShapeSettings offsetSettings(
                toJolt(localOffset),
                toJolt(info.orientation),
                shape
            );
            auto result = offsetSettings.Create();
            if (result.IsValid()) {
                shape = result.Get();
            }
        }

        // Body creation settings
        part.SetShape(shape);
        part.mPosition = JPH::RVec3(jointPos.x, jointPos.y, jointPos.z);
        part.mRotation = JPH::Quat::sIdentity();
        part.mMotionType = JPH::EMotionType::Dynamic;
        part.mObjectLayer = PhysicsLayers::MOVING;  // Use MOVING layer for ragdoll bones

        // Mass
        float massScale = 1.0f;
        auto overrideIt = config.boneOverrides.find(joint.name);
        if (overrideIt != config.boneOverrides.end()) {
            massScale = overrideIt->second.massScale;
        }
        float boneMass = (info.volume / totalVolume) * config.totalMass * massScale;
        boneMass = std::max(0.1f, boneMass);  // Minimum mass

        part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        part.mMassPropertiesOverride.mMass = boneMass;

        // Damping
        part.mLinearDamping = config.linearDamping;
        part.mAngularDamping = config.angularDamping;

        // Constraint to parent
        if (joint.parentIndex >= 0 && static_cast<size_t>(joint.parentIndex) < numJoints) {
            auto constraintSettings = new JPH::SwingTwistConstraintSettings();
            constraintSettings->mSpace = JPH::EConstraintSpace::WorldSpace;

            // Constraint position at the child joint
            constraintSettings->mPosition1 = JPH::RVec3(jointPos.x, jointPos.y, jointPos.z);
            constraintSettings->mPosition2 = constraintSettings->mPosition1;

            // Compute twist axis (parent-to-child direction)
            glm::vec3 parentPos(globalBindPose[joint.parentIndex][3]);
            glm::vec3 boneDir = jointPos - parentPos;
            if (glm::length(boneDir) > 0.001f) {
                boneDir = glm::normalize(boneDir);
            } else {
                boneDir = glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // Plane axis is perpendicular to twist axis
            glm::vec3 planeAxis;
            if (std::abs(boneDir.y) < 0.9f) {
                planeAxis = glm::normalize(glm::cross(boneDir, glm::vec3(0.0f, 1.0f, 0.0f)));
            } else {
                planeAxis = glm::normalize(glm::cross(boneDir, glm::vec3(1.0f, 0.0f, 0.0f)));
            }

            JPH::Vec3 twistAxis = toJolt(boneDir);
            JPH::Vec3 planeAxisJ = toJolt(planeAxis);

            constraintSettings->mTwistAxis1 = twistAxis;
            constraintSettings->mPlaneAxis1 = planeAxisJ;
            constraintSettings->mTwistAxis2 = twistAxis;
            constraintSettings->mPlaneAxis2 = planeAxisJ;

            // Joint limits from presets
            JointLimitPreset limits = findJointLimitPreset(joint.name);
            constraintSettings->mNormalHalfConeAngle = limits.swingYHalfAngle;
            constraintSettings->mPlaneHalfConeAngle = limits.swingZHalfAngle;
            constraintSettings->mTwistMinAngle = limits.twistMin;
            constraintSettings->mTwistMaxAngle = limits.twistMax;

            // Motor settings for CALM-driven pose tracking
            JPH::MotorSettings swingMotor;
            swingMotor.mSpringSettings.mFrequency = config.motorFrequency;
            swingMotor.mSpringSettings.mDamping = config.motorDamping;
            swingMotor.SetTorqueLimit(config.maxMotorTorque);

            JPH::MotorSettings twistMotor;
            twistMotor.mSpringSettings.mFrequency = config.motorFrequency;
            twistMotor.mSpringSettings.mDamping = config.motorDamping;
            twistMotor.SetTorqueLimit(config.maxMotorTorque);

            constraintSettings->mSwingMotorSettings = swingMotor;
            constraintSettings->mTwistMotorSettings = twistMotor;

            // Friction helps with stability
            constraintSettings->mMaxFrictionTorque = 1.0f;

            part.mToParent = constraintSettings;
        }
    }

    // 6. Post-processing for stability
    ragdollSettings->Stabilize();
    ragdollSettings->DisableParentChildCollisions();
    ragdollSettings->CalculateBodyIndexToConstraintIndex();
    ragdollSettings->CalculateConstraintIndexToBodyIdxPair();

    SDL_Log("RagdollBuilder: built ragdoll with %zu parts (total mass=%.1f kg)",
            numJoints, config.totalMass);

    return ragdollSettings;
}

} // namespace physics
