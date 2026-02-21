#include "RagdollInstance.h"
#include "PhysicsConversions.h"
#include "GLTFLoader.h"
#include "CharacterController.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Skeleton/Skeleton.h>
#include <Jolt/Skeleton/SkeletonPose.h>

#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

JPH_SUPPRESS_WARNINGS

using namespace PhysicsConversions;

namespace physics {

RagdollInstance::RagdollInstance(JPH::Ref<JPH::RagdollSettings> settings,
                                 const Skeleton& skeleton,
                                 JPH::PhysicsSystem* physicsSystem)
    : settings_(std::move(settings))
    , physicsSystem_(physicsSystem)
    , skeleton_(&skeleton) {

    if (!settings_ || !physicsSystem_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "RagdollInstance: null settings or physics system");
        return;
    }

    // Store the base motor torque from settings
    if (!settings_->mParts.empty() && settings_->mParts[0].mToParent) {
        // Read from the first constraint that has motor settings
        for (const auto& part : settings_->mParts) {
            if (part.mToParent) {
                auto* swingTwist = dynamic_cast<const JPH::SwingTwistConstraintSettings*>(
                    part.mToParent.GetPtr());
                if (swingTwist) {
                    baseMaxTorque_ = swingTwist->mSwingMotorSettings.mMaxTorqueLimit;
                    break;
                }
            }
        }
    }

    // Create the runtime ragdoll instance
    ragdoll_ = settings_->CreateRagdoll(0, 0, physicsSystem_);
    if (!ragdoll_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "RagdollInstance: failed to create ragdoll");
    }
}

RagdollInstance::~RagdollInstance() {
    if (active_ && ragdoll_) {
        ragdoll_->RemoveFromPhysicsSystem();
    }
}

RagdollInstance::RagdollInstance(RagdollInstance&& other) noexcept
    : ragdoll_(std::move(other.ragdoll_))
    , settings_(std::move(other.settings_))
    , physicsSystem_(other.physicsSystem_)
    , skeleton_(other.skeleton_)
    , motorStrength_(other.motorStrength_)
    , baseMaxTorque_(other.baseMaxTorque_)
    , active_(other.active_)
    , motorsEnabled_(other.motorsEnabled_) {
    other.physicsSystem_ = nullptr;
    other.skeleton_ = nullptr;
    other.active_ = false;
}

RagdollInstance& RagdollInstance::operator=(RagdollInstance&& other) noexcept {
    if (this != &other) {
        if (active_ && ragdoll_) {
            ragdoll_->RemoveFromPhysicsSystem();
        }
        ragdoll_ = std::move(other.ragdoll_);
        settings_ = std::move(other.settings_);
        physicsSystem_ = other.physicsSystem_;
        skeleton_ = other.skeleton_;
        motorStrength_ = other.motorStrength_;
        baseMaxTorque_ = other.baseMaxTorque_;
        active_ = other.active_;
        motorsEnabled_ = other.motorsEnabled_;
        other.physicsSystem_ = nullptr;
        other.skeleton_ = nullptr;
        other.active_ = false;
    }
    return *this;
}

// --- Pose control ---

void RagdollInstance::driveToTargetPose(const SkeletonPose& targetPose) {
    if (!ragdoll_ || !active_ || !skeleton_) return;

    JPH::SkeletonPose joltPose;
    buildJoltPose(targetPose, *skeleton_, joltPose);
    joltPose.CalculateJointMatrices();

    ragdoll_->DriveToPoseUsingMotors(joltPose);
}

void RagdollInstance::setPoseImmediate(const SkeletonPose& pose,
                                        const Skeleton& skeleton) {
    if (!ragdoll_) return;

    JPH::SkeletonPose joltPose;
    buildJoltPose(pose, skeleton, joltPose);
    joltPose.CalculateJointMatrices();

    ragdoll_->SetPose(joltPose);
}

void RagdollInstance::readPose(SkeletonPose& outPose, const Skeleton& skeleton) const {
    if (!ragdoll_ || !active_) return;

    const size_t numJoints = skeleton.joints.size();
    outPose.resize(numJoints);

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    // Read world-space transforms from physics bodies
    std::vector<glm::mat4> worldTransforms(numJoints, glm::mat4(1.0f));
    for (size_t i = 0; i < numJoints && i < ragdoll_->GetBodyCount(); ++i) {
        JPH::BodyID bodyID = ragdoll_->GetBodyID(static_cast<int>(i));
        if (bodyID.IsInvalid()) continue;

        JPH::RVec3 pos = bodyInterface.GetPosition(bodyID);
        JPH::Quat rot = bodyInterface.GetRotation(bodyID);

        glm::vec3 glmPos = toGLM(pos);
        glm::quat glmRot = toGLM(rot);

        worldTransforms[i] = glm::translate(glm::mat4(1.0f), glmPos)
                            * glm::mat4_cast(glmRot);
    }

    // Convert world transforms back to local-space BonePoses
    for (size_t i = 0; i < numJoints; ++i) {
        glm::mat4 localTransform;
        int parentIdx = skeleton.joints[i].parentIndex;
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < numJoints) {
            glm::mat4 parentInv = glm::inverse(worldTransforms[parentIdx]);
            localTransform = parentInv * worldTransforms[i];
        } else {
            localTransform = worldTransforms[i];
        }

        outPose[i] = BonePose::fromMatrix(localTransform, skeleton.joints[i].preRotation);
    }
}

// --- Body velocity queries ---

void RagdollInstance::readBodyLinearVelocities(std::vector<glm::vec3>& outVels) const {
    if (!ragdoll_ || !active_) return;

    size_t count = ragdoll_->GetBodyCount();
    outVels.resize(count, glm::vec3(0.0f));

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    for (size_t i = 0; i < count; ++i) {
        JPH::BodyID bodyID = ragdoll_->GetBodyID(static_cast<int>(i));
        if (!bodyID.IsInvalid()) {
            outVels[i] = toGLM(bodyInterface.GetLinearVelocity(bodyID));
        }
    }
}

void RagdollInstance::readBodyAngularVelocities(std::vector<glm::vec3>& outVels) const {
    if (!ragdoll_ || !active_) return;

    size_t count = ragdoll_->GetBodyCount();
    outVels.resize(count, glm::vec3(0.0f));

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    for (size_t i = 0; i < count; ++i) {
        JPH::BodyID bodyID = ragdoll_->GetBodyID(static_cast<int>(i));
        if (!bodyID.IsInvalid()) {
            outVels[i] = toGLM(bodyInterface.GetAngularVelocity(bodyID));
        }
    }
}

// --- Root body queries ---

glm::vec3 RagdollInstance::getRootPosition() const {
    if (!ragdoll_ || !active_ || ragdoll_->GetBodyCount() == 0) {
        return glm::vec3(0.0f);
    }
    JPH::RVec3 pos;
    JPH::Quat rot;
    ragdoll_->GetRootTransform(pos, rot);
    return toGLM(pos);
}

glm::quat RagdollInstance::getRootRotation() const {
    if (!ragdoll_ || !active_ || ragdoll_->GetBodyCount() == 0) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    JPH::RVec3 pos;
    JPH::Quat rot;
    ragdoll_->GetRootTransform(pos, rot);
    return toGLM(rot);
}

glm::vec3 RagdollInstance::getRootLinearVelocity() const {
    if (!ragdoll_ || !active_ || ragdoll_->GetBodyCount() == 0) {
        return glm::vec3(0.0f);
    }
    JPH::BodyID rootID = ragdoll_->GetBodyID(0);
    if (rootID.IsInvalid()) return glm::vec3(0.0f);

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    return toGLM(bodyInterface.GetLinearVelocity(rootID));
}

glm::vec3 RagdollInstance::getRootAngularVelocity() const {
    if (!ragdoll_ || !active_ || ragdoll_->GetBodyCount() == 0) {
        return glm::vec3(0.0f);
    }
    JPH::BodyID rootID = ragdoll_->GetBodyID(0);
    if (rootID.IsInvalid()) return glm::vec3(0.0f);

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    return toGLM(bodyInterface.GetAngularVelocity(rootID));
}

// --- State management ---

void RagdollInstance::activate() {
    if (!ragdoll_ || active_) return;

    ragdoll_->AddToPhysicsSystem(JPH::EActivation::Activate);
    active_ = true;

    // Enable motors if they should be on
    if (motorsEnabled_) {
        setMotorsEnabled(true);
    }

    SDL_Log("RagdollInstance: activated (%zu bodies)", ragdoll_->GetBodyCount());
}

void RagdollInstance::deactivate() {
    if (!ragdoll_ || !active_) return;

    ragdoll_->RemoveFromPhysicsSystem();
    active_ = false;

    SDL_Log("RagdollInstance: deactivated");
}

// --- External forces ---

void RagdollInstance::addImpulse(int boneIndex, const glm::vec3& impulse) {
    if (!ragdoll_ || !active_) return;
    if (boneIndex < 0 || static_cast<size_t>(boneIndex) >= ragdoll_->GetBodyCount()) return;

    JPH::BodyID bodyID = ragdoll_->GetBodyID(boneIndex);
    if (bodyID.IsInvalid()) return;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.AddImpulse(bodyID, toJolt(impulse));
}

void RagdollInstance::addImpulseAtWorldPos(const glm::vec3& impulse, const glm::vec3& worldPos) {
    if (!ragdoll_ || !active_ || ragdoll_->GetBodyCount() == 0) return;

    // Find closest body to the world position
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    float closestDistSq = std::numeric_limits<float>::max();
    JPH::BodyID closestBody;

    for (size_t i = 0; i < ragdoll_->GetBodyCount(); ++i) {
        JPH::BodyID bodyID = ragdoll_->GetBodyID(static_cast<int>(i));
        if (bodyID.IsInvalid()) continue;

        glm::vec3 bodyPos = toGLM(bodyInterface.GetPosition(bodyID));
        float distSq = glm::dot(bodyPos - worldPos, bodyPos - worldPos);
        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            closestBody = bodyID;
        }
    }

    if (!closestBody.IsInvalid()) {
        bodyInterface.AddImpulse(closestBody, toJolt(impulse));
    }
}

// --- Motor strength control ---

void RagdollInstance::setMotorStrength(float strength) {
    motorStrength_ = std::clamp(strength, 0.0f, 1.0f);

    if (!ragdoll_ || !active_) return;

    float scaledTorque = baseMaxTorque_ * motorStrength_;

    for (size_t i = 0; i < ragdoll_->GetConstraintCount(); ++i) {
        auto* constraint = ragdoll_->GetConstraint(static_cast<int>(i));
        if (!constraint) continue;

        auto* swingTwist = dynamic_cast<JPH::SwingTwistConstraint*>(constraint);
        if (swingTwist) {
            swingTwist->GetSwingMotorSettings().SetTorqueLimit(scaledTorque);
            swingTwist->GetTwistMotorSettings().SetTorqueLimit(scaledTorque);
        }
    }
}

void RagdollInstance::setMotorsEnabled(bool enabled) {
    motorsEnabled_ = enabled;
    if (!ragdoll_ || !active_) return;

    JPH::EMotorState state = enabled ? JPH::EMotorState::Position : JPH::EMotorState::Off;

    for (size_t i = 0; i < ragdoll_->GetConstraintCount(); ++i) {
        auto* constraint = ragdoll_->GetConstraint(static_cast<int>(i));
        if (!constraint) continue;

        auto* swingTwist = dynamic_cast<JPH::SwingTwistConstraint*>(constraint);
        if (swingTwist) {
            swingTwist->SetSwingMotorState(state);
            swingTwist->SetTwistMotorState(state);
        }
    }
}

// --- Sync with character controller ---

void RagdollInstance::syncCharacterController(CharacterController& controller) const {
    if (!ragdoll_ || !active_) return;

    glm::vec3 rootPos = getRootPosition();
    controller.setPosition(rootPos);
}

// --- Query ---

size_t RagdollInstance::bodyCount() const {
    return ragdoll_ ? ragdoll_->GetBodyCount() : 0;
}

JPH::BodyID RagdollInstance::getBodyID(int index) const {
    if (!ragdoll_ || index < 0 || static_cast<size_t>(index) >= ragdoll_->GetBodyCount()) {
        return JPH::BodyID();
    }
    return ragdoll_->GetBodyID(index);
}

// --- Private helpers ---

void RagdollInstance::buildJoltPose(const SkeletonPose& enginePose,
                                     const Skeleton& skeleton,
                                     JPH::SkeletonPose& outJoltPose) const {
    if (!settings_) return;

    const auto& joltSkeleton = settings_->mSkeleton;
    outJoltPose.SetSkeleton(joltSkeleton);

    size_t numJoints = std::min(enginePose.size(), skeleton.joints.size());

    // Compute world-space transforms from the engine pose
    std::vector<glm::mat4> worldTransforms(numJoints, glm::mat4(1.0f));
    for (size_t i = 0; i < numJoints; ++i) {
        const BonePose& bp = enginePose[i];
        glm::mat4 local = bp.toMatrix(skeleton.joints[i].preRotation);

        int32_t parentIdx = skeleton.joints[i].parentIndex;
        if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < numJoints) {
            worldTransforms[i] = worldTransforms[parentIdx] * local;
        } else {
            worldTransforms[i] = local;
        }
    }

    // Set Jolt pose from world transforms
    auto& joltJoints = outJoltPose.GetJoints();
    for (size_t i = 0; i < numJoints && i < static_cast<size_t>(joltSkeleton->GetJointCount()); ++i) {
        glm::vec3 pos(worldTransforms[i][3]);
        glm::quat rot = glm::quat_cast(worldTransforms[i]);

        joltJoints[i].mTranslation = JPH::Vec3(pos.x, pos.y, pos.z);
        joltJoints[i].mRotation = toJolt(rot);
    }

    outJoltPose.SetRootOffset(joltJoints[0].mTranslation);
}

} // namespace physics
