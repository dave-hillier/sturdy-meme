#include "ArticulatedBody.h"
#include "PhysicsConversions.h"
#include "JoltLayerConfig.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

JPH_SUPPRESS_WARNINGS

using namespace PhysicsConversions;

// Unique group ID counter for ragdoll collision groups
static uint32_t sNextRagdollGroupID = 1000;

// ─── ArticulatedBody move semantics ────────────────────────────────────────────

ArticulatedBody::~ArticulatedBody() {
    if (ragdoll_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ArticulatedBody destroyed without calling destroy() - ragdoll leaked");
    }
}

ArticulatedBody::ArticulatedBody(ArticulatedBody&& other) noexcept
    : ragdoll_(other.ragdoll_)
    , bodyIDs_(std::move(other.bodyIDs_))
    , jointIndices_(std::move(other.jointIndices_))
    , effortFactors_(std::move(other.effortFactors_))
    , ownerPhysics_(other.ownerPhysics_)
{
    other.ragdoll_ = nullptr;
    other.ownerPhysics_ = nullptr;
}

ArticulatedBody& ArticulatedBody::operator=(ArticulatedBody&& other) noexcept {
    if (this != &other) {
        ragdoll_ = other.ragdoll_;
        bodyIDs_ = std::move(other.bodyIDs_);
        jointIndices_ = std::move(other.jointIndices_);
        effortFactors_ = std::move(other.effortFactors_);
        ownerPhysics_ = other.ownerPhysics_;
        other.ragdoll_ = nullptr;
        other.ownerPhysics_ = nullptr;
    }
    return *this;
}

// ─── create ────────────────────────────────────────────────────────────────────

bool ArticulatedBody::create(PhysicsWorld& physics, const ArticulatedBodyConfig& config,
                             const glm::vec3& rootPosition) {
    if (config.parts.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ArticulatedBody::create: empty config");
        return false;
    }

    JPH::PhysicsSystem* joltSystem = physics.getJoltSystem();
    if (!joltSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ArticulatedBody::create: no Jolt system");
        return false;
    }

    const float scale = config.globalScale;
    const size_t numParts = config.parts.size();

    // Phase 1: Compute world positions for each part by traversing the parent chain
    std::vector<glm::vec3> partPositions(numParts);
    std::vector<glm::quat> partRotations(numParts, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < numParts; ++i) {
        const auto& part = config.parts[i];
        if (part.parentPartIndex < 0) {
            partPositions[i] = rootPosition;
        } else {
            size_t parentIdx = static_cast<size_t>(part.parentPartIndex);
            glm::vec3 parentPos = partPositions[parentIdx];
            glm::vec3 worldAnchor = parentPos + partRotations[parentIdx] * (part.localAnchorInParent * scale);
            partPositions[i] = worldAnchor - partRotations[i] * (part.localAnchorInChild * scale);
        }
    }

    // Phase 2: Create Jolt Skeleton (required by RagdollSettings)
    JPH::Ref<JPH::Skeleton> joltSkeleton = new JPH::Skeleton();
    for (size_t i = 0; i < numParts; ++i) {
        joltSkeleton->AddJoint(config.parts[i].name, config.parts[i].parentPartIndex);
    }

    // Phase 3: Create RagdollSettings with Parts
    JPH::Ref<JPH::RagdollSettings> ragdollSettings = new JPH::RagdollSettings();
    ragdollSettings->mSkeleton = joltSkeleton;
    ragdollSettings->mParts.resize(numParts);

    for (size_t i = 0; i < numParts; ++i) {
        auto& joltPart = ragdollSettings->mParts[i];
        const auto& part = config.parts[i];
        float halfH = part.halfHeight * scale;
        float rad = part.radius * scale;

        // Part extends BodyCreationSettings — set shape and body properties
        joltPart.SetShapeSettings(new JPH::CapsuleShapeSettings(halfH, rad));

        const glm::vec3& pos = partPositions[i];
        joltPart.mPosition = JPH::RVec3(pos.x, pos.y, pos.z);
        joltPart.mRotation = JPH::Quat::sIdentity();
        joltPart.mMotionType = JPH::EMotionType::Dynamic;
        joltPart.mObjectLayer = PhysicsLayers::MOVING;

        joltPart.mFriction = 0.8f;
        joltPart.mRestitution = 0.0f;
        joltPart.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        joltPart.mMassPropertiesOverride.mMass = std::max(part.mass, 2.0f);

        joltPart.mLinearDamping = 0.5f;
        joltPart.mAngularDamping = 0.9f;
        joltPart.mMotionQuality = JPH::EMotionQuality::LinearCast;
        joltPart.mNumVelocityStepsOverride = 30;
        joltPart.mNumPositionStepsOverride = 10;

        // Constraint to parent (SwingTwist in local space)
        if (part.parentPartIndex >= 0) {
            auto* constraint = new JPH::SwingTwistConstraintSettings();
            constraint->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;

            // Local-space positions: offset from each body's center
            constraint->mPosition1 = toJolt(part.localAnchorInParent * scale);
            constraint->mPosition2 = toJolt(part.localAnchorInChild * scale);

            constraint->mTwistAxis1 = toJolt(part.twistAxis);
            constraint->mPlaneAxis1 = toJolt(part.planeAxis);
            constraint->mTwistAxis2 = toJolt(part.twistAxis);
            constraint->mPlaneAxis2 = toJolt(part.planeAxis);

            constraint->mNormalHalfConeAngle = part.normalHalfConeAngle;
            constraint->mPlaneHalfConeAngle = part.planeHalfConeAngle;
            constraint->mTwistMinAngle = part.twistMinAngle;
            constraint->mTwistMaxAngle = part.twistMaxAngle;
            constraint->mMaxFrictionTorque = 10.0f;

            joltPart.mToParent = constraint;
        }
    }

    // Phase 4: Jolt's ragdoll stabilization pipeline
    // Stabilize() fixes constraint axis orthogonality and numerical singularities
    if (!ragdollSettings->Stabilize()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ArticulatedBody::create: RagdollSettings::Stabilize() failed");
    }

    // DisableParentChildCollisions sets up GroupFilterTable automatically
    ragdollSettings->DisableParentChildCollisions();

    // Phase 5: Create the ragdoll instance
    uint32_t groupID = sNextRagdollGroupID++;
    ragdoll_ = ragdollSettings->CreateRagdoll(groupID, 0, joltSystem);
    if (!ragdoll_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ArticulatedBody::create: CreateRagdoll failed");
        return false;
    }

    // Phase 6: Add to physics system (activates bodies)
    ragdoll_->AddToPhysicsSystem(JPH::EActivation::Activate);

    // Phase 7: Cache body IDs and metadata for our API
    bodyIDs_.resize(numParts);
    jointIndices_.resize(numParts, -1);
    effortFactors_.resize(numParts, 200.0f);

    for (size_t i = 0; i < numParts; ++i) {
        bodyIDs_[i] = ragdoll_->GetBodyID(static_cast<int>(i)).GetIndexAndSequenceNumber();
        jointIndices_[i] = config.parts[i].skeletonJointIndex;
        effortFactors_[i] = config.parts[i].effortFactor;
    }

    ownerPhysics_ = &physics;

    SDL_Log("ArticulatedBody created via Jolt Ragdoll API: %zu parts, group %u",
            numParts, groupID);
    return true;
}

// ─── destroy ───────────────────────────────────────────────────────────────────

void ArticulatedBody::destroy(PhysicsWorld& physics) {
    cleanup(physics);
}

void ArticulatedBody::cleanup(PhysicsWorld& physics) {
    if (ragdoll_) {
        ragdoll_->RemoveFromPhysicsSystem();
        delete ragdoll_;
        ragdoll_ = nullptr;
    }

    bodyIDs_.clear();
    jointIndices_.clear();
    effortFactors_.clear();
    ownerPhysics_ = nullptr;
}

// ─── State extraction ──────────────────────────────────────────────────────────

void ArticulatedBody::getState(std::vector<PartState>& states, const PhysicsWorld& physics) const {
    states.resize(bodyIDs_.size());

    for (size_t i = 0; i < bodyIDs_.size(); ++i) {
        PhysicsBodyInfo info = physics.getBodyInfo(bodyIDs_[i]);
        states[i].position = info.position;
        states[i].rotation = info.rotation;
        states[i].linearVelocity = info.linearVelocity;
        states[i].angularVelocity = info.angularVelocity;
    }
}

// ─── Torque application ────────────────────────────────────────────────────────

void ArticulatedBody::applyTorques(PhysicsWorld& physics, const std::vector<glm::vec3>& torques) const {
    size_t count = std::min(torques.size(), bodyIDs_.size());
    for (size_t i = 0; i < count; ++i) {
        glm::vec3 scaledTorque = torques[i] * effortFactors_[i];
        physics.applyTorque(bodyIDs_[i], scaledTorque);
    }
}

// ─── Accessors ─────────────────────────────────────────────────────────────────

PhysicsBodyID ArticulatedBody::getPartBodyID(size_t index) const {
    if (index >= bodyIDs_.size()) return INVALID_BODY_ID;
    return bodyIDs_[index];
}

int32_t ArticulatedBody::getPartJointIndex(size_t index) const {
    if (index >= jointIndices_.size()) return -1;
    return jointIndices_[index];
}

glm::vec3 ArticulatedBody::getRootPosition(const PhysicsWorld& physics) const {
    if (bodyIDs_.empty()) return glm::vec3(0.0f);
    return physics.getBodyInfo(bodyIDs_[0]).position;
}

glm::quat ArticulatedBody::getRootRotation(const PhysicsWorld& physics) const {
    if (bodyIDs_.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return physics.getBodyInfo(bodyIDs_[0]).rotation;
}

bool ArticulatedBody::hasNaNState(const PhysicsWorld& physics) const {
    for (auto bodyID : bodyIDs_) {
        if (bodyID == INVALID_BODY_ID) continue;
        PhysicsBodyInfo info = physics.getBodyInfo(bodyID);
        if (std::isnan(info.position.x) || std::isnan(info.position.y) || std::isnan(info.position.z))
            return true;
        if (std::isnan(info.angularVelocity.x) || std::isnan(info.angularVelocity.y) || std::isnan(info.angularVelocity.z))
            return true;
    }
    return false;
}

