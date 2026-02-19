#include "ActiveRagdoll.h"
#include "PhysicsSystem.h"
#include "PhysicsConversions.h"
#include "JoltLayerConfig.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

JPH_SUPPRESS_WARNINGS

using namespace PhysicsConversions;

// Helper to decompose a glm::mat4 into position and rotation
static void decomposeMat4(const glm::mat4& m, glm::vec3& pos, glm::quat& rot) {
    pos = glm::vec3(m[3]);
    // Extract rotation by normalizing columns
    glm::vec3 col0 = glm::normalize(glm::vec3(m[0]));
    glm::vec3 col1 = glm::normalize(glm::vec3(m[1]));
    glm::vec3 col2 = glm::normalize(glm::vec3(m[2]));
    glm::mat3 rotMat(col0, col1, col2);
    rot = glm::quat_cast(rotMat);
}

// Helper to compute bone length from skeleton
static float computeBoneLength(const Skeleton& skeleton, int32_t boneIndex,
                                const std::vector<glm::mat4>& bindGlobals) {
    // Find a child bone to measure length
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        if (skeleton.joints[i].parentIndex == boneIndex) {
            glm::vec3 parentPos = glm::vec3(bindGlobals[boneIndex][3]);
            glm::vec3 childPos = glm::vec3(bindGlobals[i][3]);
            return glm::length(childPos - parentPos);
        }
    }
    // Leaf bone - use a small default
    return 0.1f;
}

// =============================================================================
// RagdollDefinition
// =============================================================================

RagdollDefinition RagdollDefinition::buildFromSkeleton(const Skeleton& skeleton) {
    RagdollDefinition def;

    // Compute bind pose global transforms for measuring bone lengths
    std::vector<glm::mat4> bindGlobals;
    skeleton.computeGlobalTransforms(bindGlobals);

    // Bone name patterns for major body parts (Mixamo convention)
    struct BonePattern {
        std::vector<std::string> namePatterns;
        float massMultiplier;
        glm::vec3 swingLimits;
        float twistMin;
        float twistMax;
    };

    // Define which bones to include in the ragdoll
    // We map the major bones - not every finger/toe
    std::vector<BonePattern> patterns = {
        // Hips (root)
        {{"Hips", "hips", "pelvis", "Pelvis"},
         5.0f, glm::vec3(glm::radians(30.0f)), glm::radians(-10.0f), glm::radians(10.0f)},
        // Spine
        {{"Spine", "spine", "Spine1"},
         3.0f, glm::vec3(glm::radians(30.0f)), glm::radians(-20.0f), glm::radians(20.0f)},
        {{"Spine1", "Spine2", "spine1", "spine2"},
         3.0f, glm::vec3(glm::radians(25.0f)), glm::radians(-15.0f), glm::radians(15.0f)},
        // Head
        {{"Head", "head"},
         2.0f, glm::vec3(glm::radians(40.0f)), glm::radians(-30.0f), glm::radians(30.0f)},
        // Left arm
        {{"LeftArm", "LeftUpperArm", "L_UpperArm", "upperarm.L"},
         1.5f, glm::vec3(glm::radians(90.0f)), glm::radians(-90.0f), glm::radians(90.0f)},
        {{"LeftForeArm", "L_Forearm", "forearm.L"},
         1.0f, glm::vec3(glm::radians(5.0f), glm::radians(140.0f), glm::radians(5.0f)),
         glm::radians(-90.0f), glm::radians(90.0f)},
        {{"LeftHand", "L_Hand", "hand.L"},
         0.5f, glm::vec3(glm::radians(60.0f)), glm::radians(-30.0f), glm::radians(30.0f)},
        // Right arm
        {{"RightArm", "RightUpperArm", "R_UpperArm", "upperarm.R"},
         1.5f, glm::vec3(glm::radians(90.0f)), glm::radians(-90.0f), glm::radians(90.0f)},
        {{"RightForeArm", "R_Forearm", "forearm.R"},
         1.0f, glm::vec3(glm::radians(5.0f), glm::radians(140.0f), glm::radians(5.0f)),
         glm::radians(-90.0f), glm::radians(90.0f)},
        {{"RightHand", "R_Hand", "hand.R"},
         0.5f, glm::vec3(glm::radians(60.0f)), glm::radians(-30.0f), glm::radians(30.0f)},
        // Left leg
        {{"LeftUpLeg", "LeftThigh", "L_Thigh", "thigh.L"},
         2.0f, glm::vec3(glm::radians(80.0f)), glm::radians(-30.0f), glm::radians(30.0f)},
        {{"LeftLeg", "LeftShin", "L_Shin", "shin.L"},
         1.5f, glm::vec3(glm::radians(5.0f), glm::radians(120.0f), glm::radians(5.0f)),
         glm::radians(-5.0f), glm::radians(5.0f)},
        {{"LeftFoot", "L_Foot", "foot.L"},
         0.5f, glm::vec3(glm::radians(40.0f)), glm::radians(-20.0f), glm::radians(20.0f)},
        // Right leg
        {{"RightUpLeg", "RightThigh", "R_Thigh", "thigh.R"},
         2.0f, glm::vec3(glm::radians(80.0f)), glm::radians(-30.0f), glm::radians(30.0f)},
        {{"RightLeg", "RightShin", "R_Shin", "shin.R"},
         1.5f, glm::vec3(glm::radians(5.0f), glm::radians(120.0f), glm::radians(5.0f)),
         glm::radians(-5.0f), glm::radians(5.0f)},
        {{"RightFoot", "R_Foot", "foot.R"},
         0.5f, glm::vec3(glm::radians(40.0f)), glm::radians(-20.0f), glm::radians(20.0f)},
    };

    // Try to find each bone in the skeleton
    for (const auto& pattern : patterns) {
        int32_t foundIndex = -1;
        std::string foundName;

        for (const auto& namePattern : pattern.namePatterns) {
            // Try exact match first
            foundIndex = skeleton.findJointIndex(namePattern);
            if (foundIndex >= 0) {
                foundName = namePattern;
                break;
            }
            // Try with mixamorig: prefix
            foundIndex = skeleton.findJointIndex("mixamorig:" + namePattern);
            if (foundIndex >= 0) {
                foundName = "mixamorig:" + namePattern;
                break;
            }
        }

        if (foundIndex < 0) continue;

        RagdollBoneMapping mapping;
        mapping.boneIndex = foundIndex;
        mapping.boneName = foundName;
        mapping.mass = pattern.massMultiplier;
        mapping.swingLimits = pattern.swingLimits;
        mapping.twistMin = pattern.twistMin;
        mapping.twistMax = pattern.twistMax;

        // Compute capsule dimensions from bone length
        float boneLength = computeBoneLength(skeleton, foundIndex, bindGlobals);
        mapping.capsuleHalfHeight = std::max(0.02f, boneLength * 0.4f);
        mapping.capsuleRadius = std::max(0.02f, boneLength * 0.15f);

        // Find parent mapping
        int32_t parentBoneIdx = skeleton.joints[foundIndex].parentIndex;
        mapping.parentMappingIndex = -1;
        while (parentBoneIdx >= 0) {
            // Search existing mappings for this parent bone
            for (size_t i = 0; i < def.bones.size(); ++i) {
                if (def.bones[i].boneIndex == parentBoneIdx) {
                    mapping.parentMappingIndex = static_cast<int32_t>(i);
                    break;
                }
            }
            if (mapping.parentMappingIndex >= 0) break;
            parentBoneIdx = skeleton.joints[parentBoneIdx].parentIndex;
        }

        def.bones.push_back(mapping);
    }

    SDL_Log("Built ragdoll definition with %zu bones", def.bones.size());
    return def;
}

// =============================================================================
// ActiveRagdoll
// =============================================================================

std::unique_ptr<ActiveRagdoll> ActiveRagdoll::create(
    PhysicsWorld& physicsWorld,
    const RagdollDefinition& definition,
    const Skeleton& skeleton,
    const glm::vec3& characterPosition)
{
    auto ragdoll = std::unique_ptr<ActiveRagdoll>(new ActiveRagdoll());
    ragdoll->definition_ = definition;
    ragdoll->physicsWorld_ = &physicsWorld;

    ragdoll->perBoneMotorStrength_.resize(definition.bones.size(), 1.0f);
    ragdoll->boneStates_.resize(definition.bones.size());

    if (!ragdoll->createBodies(physicsWorld, skeleton, characterPosition)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create ragdoll bodies");
        return nullptr;
    }

    SDL_Log("Created active ragdoll with %zu bodies", definition.bones.size());
    return ragdoll;
}

ActiveRagdoll::~ActiveRagdoll() {
    if (physicsWorld_) {
        // Remove all bodies
        for (auto& state : boneStates_) {
            if (state.bodyId != INVALID_BODY_ID) {
                physicsWorld_->removeBody(state.bodyId);
                state.bodyId = INVALID_BODY_ID;
            }
        }
    }
}

bool ActiveRagdoll::createBodies(
    PhysicsWorld& physicsWorld,
    const Skeleton& skeleton,
    const glm::vec3& characterPosition)
{
    // Get bind pose global transforms
    std::vector<glm::mat4> bindGlobals;
    skeleton.computeGlobalTransforms(bindGlobals);

    for (size_t i = 0; i < definition_.bones.size(); ++i) {
        const auto& mapping = definition_.bones[i];
        auto& state = boneStates_[i];

        if (mapping.boneIndex < 0 ||
            static_cast<size_t>(mapping.boneIndex) >= bindGlobals.size()) {
            continue;
        }

        // Get bone world position from bind pose
        glm::vec3 bonePos;
        glm::quat boneRot;
        decomposeMat4(bindGlobals[mapping.boneIndex], bonePos, boneRot);

        // Offset by character position
        glm::vec3 worldPos = characterPosition + bonePos;

        // Create a dynamic sphere body for this bone on the RAGDOLL layer
        // RAGDOLL layer avoids collision with CHARACTER and other ragdoll bones
        state.bodyId = physicsWorld.createSphereOnLayer(
            worldPos,
            mapping.capsuleRadius + mapping.capsuleHalfHeight,
            PhysicsLayers::RAGDOLL,
            mapping.mass,
            0.3f,  // friction
            0.1f   // restitution
        );

        if (state.bodyId == INVALID_BODY_ID) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to create ragdoll body for bone %s",
                        mapping.boneName.c_str());
        }
    }

    return true;
}

void ActiveRagdoll::driveToAnimationPose(
    const std::vector<glm::mat4>& globalBoneTransforms,
    const glm::mat4& characterTransform,
    float deltaTime)
{
    if (!enabled_ || blendMode_ == RagdollBlendMode::FullyAnimated) return;

    updateTransition(deltaTime);

    for (size_t i = 0; i < definition_.bones.size(); ++i) {
        const auto& mapping = definition_.bones[i];
        auto& state = boneStates_[i];

        if (state.bodyId == INVALID_BODY_ID) continue;
        if (mapping.boneIndex < 0 ||
            static_cast<size_t>(mapping.boneIndex) >= globalBoneTransforms.size()) continue;

        // Compute target position from animation
        glm::mat4 boneWorldMatrix = characterTransform * globalBoneTransforms[mapping.boneIndex];
        glm::vec3 targetPos;
        glm::quat targetRot;
        decomposeMat4(boneWorldMatrix, targetPos, targetRot);

        state.targetPosition = targetPos;
        state.targetRotation = targetRot;

        // Determine effective motor strength
        float effectiveStrength = motorStrength_ * perBoneMotorStrength_[i];

        if (blendMode_ == RagdollBlendMode::FullRagdoll) {
            effectiveStrength = 0.0f;
        }

        if (effectiveStrength > 0.001f) {
            // Drive body toward animation target using velocity
            auto bodyInfo = physicsWorld_->getBodyInfo(state.bodyId);
            glm::vec3 currentPos = bodyInfo.position;

            // PD controller to compute desired velocity
            glm::vec3 posError = targetPos - currentPos;
            float spring = motorSettings_.springFrequency * effectiveStrength;
            float damping = motorSettings_.springDamping;

            glm::vec3 desiredVel = posError * spring * 2.0f * glm::pi<float>();

            // Clamp velocity to prevent instability
            float maxSpeed = motorSettings_.maxForce * deltaTime / std::max(0.1f, mapping.mass);
            float speed = glm::length(desiredVel);
            if (speed > maxSpeed) {
                desiredVel *= maxSpeed / speed;
            }

            // Blend with current velocity for damping
            glm::vec3 currentVel = bodyInfo.linearVelocity;
            glm::vec3 finalVel = glm::mix(currentVel, desiredVel, std::min(1.0f, damping * deltaTime * 10.0f));

            physicsWorld_->setBodyVelocity(state.bodyId, finalVel);
        }
    }
}

void ActiveRagdoll::readPhysicsTransforms(
    std::vector<glm::mat4>& outGlobalTransforms,
    const std::vector<glm::mat4>& animationTransforms,
    const glm::mat4& characterTransform) const
{
    if (!enabled_) return;

    glm::mat4 invCharTransform = glm::inverse(characterTransform);

    for (size_t i = 0; i < definition_.bones.size(); ++i) {
        const auto& mapping = definition_.bones[i];
        const auto& state = boneStates_[i];

        if (state.bodyId == INVALID_BODY_ID) continue;
        if (mapping.boneIndex < 0 ||
            static_cast<size_t>(mapping.boneIndex) >= outGlobalTransforms.size()) continue;

        float blend = 0.0f;
        switch (blendMode_) {
            case RagdollBlendMode::FullyAnimated:
                blend = 0.0f;
                break;
            case RagdollBlendMode::Powered:
                // In powered mode, blend physics influence based on inverse motor strength
                // Strong motors = mostly animation, weak motors = mostly physics
                blend = 1.0f - (motorStrength_ * perBoneMotorStrength_[i]);
                blend = std::clamp(blend, 0.0f, 0.8f); // Never fully physics in powered mode
                break;
            case RagdollBlendMode::PartialRagdoll:
                blend = 1.0f - perBoneMotorStrength_[i];
                break;
            case RagdollBlendMode::FullRagdoll:
                blend = 1.0f;
                break;
        }

        if (blend < 0.001f) continue;

        // Get physics body transform
        glm::mat4 physicsWorldTransform = physicsWorld_->getBodyTransform(state.bodyId);

        // Convert to local (character) space
        glm::mat4 physicsLocalTransform = invCharTransform * physicsWorldTransform;

        // Blend between animation and physics
        glm::vec3 animPos, physPos;
        glm::quat animRot, physRot;
        decomposeMat4(animationTransforms[mapping.boneIndex], animPos, animRot);
        decomposeMat4(physicsLocalTransform, physPos, physRot);

        glm::vec3 blendedPos = glm::mix(animPos, physPos, blend);
        glm::quat blendedRot = glm::slerp(animRot, physRot, blend);

        outGlobalTransforms[mapping.boneIndex] =
            glm::translate(glm::mat4(1.0f), blendedPos) * glm::mat4_cast(blendedRot);
    }
}

void ActiveRagdoll::applyImpulse(int32_t boneIndex, const glm::vec3& impulse, const glm::vec3& point) {
    int32_t ragdollIdx = findRagdollBoneIndex(boneIndex);
    if (ragdollIdx < 0) return;

    auto& state = boneStates_[ragdollIdx];
    if (state.bodyId == INVALID_BODY_ID) return;

    physicsWorld_->applyImpulse(state.bodyId, impulse);

    // Temporarily reduce motor strength on hit bone for physics response
    perBoneMotorStrength_[ragdollIdx] *= 0.3f;
}

void ActiveRagdoll::applyImpulseAtPoint(const glm::vec3& worldPoint, const glm::vec3& impulse) {
    // Find nearest bone
    float minDist = FLT_MAX;
    int32_t nearestIdx = -1;

    for (size_t i = 0; i < boneStates_.size(); ++i) {
        if (boneStates_[i].bodyId == INVALID_BODY_ID) continue;
        auto info = physicsWorld_->getBodyInfo(boneStates_[i].bodyId);
        float dist = glm::distance(info.position, worldPoint);
        if (dist < minDist) {
            minDist = dist;
            nearestIdx = static_cast<int32_t>(i);
        }
    }

    if (nearestIdx >= 0) {
        physicsWorld_->applyImpulse(boneStates_[nearestIdx].bodyId, impulse);
        perBoneMotorStrength_[nearestIdx] *= 0.3f;
    }
}

void ActiveRagdoll::setBlendMode(RagdollBlendMode mode) {
    blendMode_ = mode;
}

void ActiveRagdoll::setMotorStrength(float strength) {
    motorStrength_ = std::clamp(strength, 0.0f, 1.0f);
}

void ActiveRagdoll::setBoneMotorStrength(int32_t boneIndex, float strength) {
    int32_t idx = findRagdollBoneIndex(boneIndex);
    if (idx >= 0 && static_cast<size_t>(idx) < perBoneMotorStrength_.size()) {
        perBoneMotorStrength_[idx] = std::clamp(strength, 0.0f, 1.0f);
    }
}

void ActiveRagdoll::setMotorSettings(const RagdollMotorSettings& settings) {
    motorSettings_ = settings;
}

void ActiveRagdoll::transitionToMode(RagdollBlendMode targetMode, float duration) {
    transitionActive_ = true;
    transitionTarget_ = targetMode;
    transitionDuration_ = std::max(0.01f, duration);
    transitionElapsed_ = 0.0f;
    transitionStartStrength_ = motorStrength_;

    switch (targetMode) {
        case RagdollBlendMode::FullyAnimated:
            transitionEndStrength_ = 1.0f;
            break;
        case RagdollBlendMode::Powered:
            transitionEndStrength_ = 0.8f;
            break;
        case RagdollBlendMode::PartialRagdoll:
            transitionEndStrength_ = 0.4f;
            break;
        case RagdollBlendMode::FullRagdoll:
            transitionEndStrength_ = 0.0f;
            break;
    }
}

void ActiveRagdoll::updateTransition(float deltaTime) {
    if (!transitionActive_) return;

    transitionElapsed_ += deltaTime;
    float t = std::min(1.0f, transitionElapsed_ / transitionDuration_);

    // Smooth step
    t = t * t * (3.0f - 2.0f * t);

    motorStrength_ = glm::mix(transitionStartStrength_, transitionEndStrength_, t);

    if (transitionElapsed_ >= transitionDuration_) {
        transitionActive_ = false;
        blendMode_ = transitionTarget_;
        motorStrength_ = transitionEndStrength_;
    }
}

PhysicsBodyID ActiveRagdoll::getBoneBody(int32_t boneIndex) const {
    int32_t idx = findRagdollBoneIndex(boneIndex);
    if (idx < 0) return INVALID_BODY_ID;
    return boneStates_[idx].bodyId;
}

int32_t ActiveRagdoll::findBoneForBody(PhysicsBodyID bodyId) const {
    for (size_t i = 0; i < boneStates_.size(); ++i) {
        if (boneStates_[i].bodyId == bodyId) {
            return definition_.bones[i].boneIndex;
        }
    }
    return -1;
}

void ActiveRagdoll::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;

    // When disabled, we could remove bodies from simulation
    // For now just stop driving them
}

void ActiveRagdoll::teleportToAnimation(
    const std::vector<glm::mat4>& globalBoneTransforms,
    const glm::mat4& characterTransform)
{
    for (size_t i = 0; i < definition_.bones.size(); ++i) {
        const auto& mapping = definition_.bones[i];
        auto& state = boneStates_[i];

        if (state.bodyId == INVALID_BODY_ID) continue;
        if (mapping.boneIndex < 0 ||
            static_cast<size_t>(mapping.boneIndex) >= globalBoneTransforms.size()) continue;

        glm::mat4 boneWorld = characterTransform * globalBoneTransforms[mapping.boneIndex];
        glm::vec3 pos = glm::vec3(boneWorld[3]);

        physicsWorld_->setBodyPosition(state.bodyId, pos);
        physicsWorld_->setBodyVelocity(state.bodyId, glm::vec3(0.0f));
    }

    // Reset motor strengths
    std::fill(perBoneMotorStrength_.begin(), perBoneMotorStrength_.end(), 1.0f);
}

int32_t ActiveRagdoll::findRagdollBoneIndex(int32_t skeletonBoneIndex) const {
    for (size_t i = 0; i < definition_.bones.size(); ++i) {
        if (definition_.bones[i].boneIndex == skeletonBoneIndex) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}
