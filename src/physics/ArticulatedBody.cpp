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

// Skeleton is defined in GLTFLoader.h
#include "GLTFLoader.h"

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

    // CalculateConstraintPriorities ensures root constraints are solved first
    ragdollSettings->CalculateConstraintPriorities();

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

// ─── Write physics state to skeleton ───────────────────────────────────────────

void ArticulatedBody::writeToSkeleton(Skeleton& skeleton, const PhysicsWorld& physics) const {
    for (size_t i = 0; i < bodyIDs_.size(); ++i) {
        int32_t jointIdx = jointIndices_[i];
        if (jointIdx < 0 || static_cast<size_t>(jointIdx) >= skeleton.joints.size()) continue;

        PhysicsBodyInfo info = physics.getBodyInfo(bodyIDs_[i]);
        auto& joint = skeleton.joints[jointIdx];

        if (joint.parentIndex < 0) {
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), info.position);
            transform *= glm::mat4_cast(info.rotation);
            joint.localTransform = transform;
        } else {
            int32_t parentPartIdx = -1;
            for (size_t j = 0; j < jointIndices_.size(); ++j) {
                if (jointIndices_[j] == joint.parentIndex) {
                    parentPartIdx = static_cast<int32_t>(j);
                    break;
                }
            }

            if (parentPartIdx >= 0) {
                PhysicsBodyInfo parentInfo = physics.getBodyInfo(bodyIDs_[parentPartIdx]);
                glm::quat parentRotInv = glm::inverse(parentInfo.rotation);
                glm::quat localRot = parentRotInv * info.rotation;
                glm::vec3 localPos = parentRotInv * (info.position - parentInfo.position);

                glm::mat4 localTransform = glm::translate(glm::mat4(1.0f), localPos);
                localTransform *= glm::mat4_cast(localRot);
                joint.localTransform = localTransform;
            } else {
                glm::mat4 transform = glm::translate(glm::mat4(1.0f), info.position);
                transform *= glm::mat4_cast(info.rotation);
                joint.localTransform = transform;
            }
        }
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

// ─── Humanoid config factory ───────────────────────────────────────────────────

// Helper: find a joint by trying multiple common naming conventions
static int32_t findJoint(const Skeleton& skeleton, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        int32_t idx = skeleton.findJointIndex(name);
        if (idx >= 0) return idx;
    }
    return -1;
}

ArticulatedBodyConfig createHumanoidConfig(const Skeleton& skeleton) {
    ArticulatedBodyConfig config;
    config.globalScale = 1.0f;

    struct PartTemplate {
        std::string name;
        std::vector<std::string> jointNames;
        int32_t parentPart;
        float halfHeight;
        float radius;
        float mass;
        glm::vec3 anchorInParent;
        glm::vec3 anchorInChild;
        glm::vec3 twistAxis;
        glm::vec3 planeAxis;
        float twistMin, twistMax;
        float normalCone, planeCone;
        float effortFactor;
    };

    // Y-up coordinate system, capsules aligned along Y
    // Parent indices are ordered so parents always come before children
    // (required by Jolt's Skeleton/RagdollSettings)
    const std::vector<PartTemplate> templates = {
        // 0: Pelvis (root)
        {"Pelvis",
         {"Hips", "pelvis", "Pelvis", "mixamorig:Hips", "Bip01_Pelvis"},
         -1,
         0.08f, 0.12f, 10.0f,
         {0, 0, 0}, {0, 0, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.3f, 0.3f, 0.3f, 0.3f, 400.0f},

        // 1: LowerSpine
        {"LowerSpine",
         {"Spine", "spine_01", "LowerSpine", "mixamorig:Spine", "Bip01_Spine"},
         0,
         0.08f, 0.10f, 6.0f,
         {0, 0.08f, 0}, {0, -0.08f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.3f, 0.3f, 0.3f, 0.3f, 400.0f},

        // 2: UpperSpine
        {"UpperSpine",
         {"Spine1", "spine_02", "UpperSpine", "mixamorig:Spine1", "Bip01_Spine1"},
         1,
         0.08f, 0.10f, 6.0f,
         {0, 0.08f, 0}, {0, -0.08f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.2f, 0.2f, 0.2f, 0.2f, 400.0f},

        // 3: Chest
        {"Chest",
         {"Spine2", "spine_03", "Chest", "mixamorig:Spine2", "Bip01_Spine2"},
         2,
         0.10f, 0.12f, 8.0f,
         {0, 0.08f, 0}, {0, -0.10f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.2f, 0.2f, 0.2f, 0.2f, 300.0f},

        // 4: Neck
        {"Neck",
         {"Neck", "neck_01", "mixamorig:Neck", "Bip01_Neck"},
         3,
         0.04f, 0.04f, 2.0f,
         {0, 0.10f, 0}, {0, -0.04f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.3f, 0.3f, 0.3f, 0.3f, 100.0f},

        // 5: Head
        {"Head",
         {"Head", "head", "mixamorig:Head", "Bip01_Head"},
         4,
         0.06f, 0.09f, 4.0f,
         {0, 0.04f, 0}, {0, -0.06f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.4f, 0.4f, 0.3f, 0.3f, 100.0f},

        // 6: Left Shoulder (clavicle)
        {"LeftShoulder",
         {"LeftShoulder", "clavicle_l", "L_Clavicle", "mixamorig:LeftShoulder", "Bip01_L_Clavicle"},
         3,
         0.06f, 0.03f, 1.5f,
         {-0.06f, 0.08f, 0}, {0.06f, 0, 0},
         {-1, 0, 0}, {0, 1, 0},
         -0.2f, 0.2f, 0.2f, 0.2f, 100.0f},

        // 7: Left Upper Arm
        {"LeftUpperArm",
         {"LeftArm", "upperarm_l", "L_UpperArm", "mixamorig:LeftArm", "Bip01_L_UpperArm"},
         6,
         0.12f, 0.04f, 2.5f,
         {-0.06f, 0, 0}, {0, 0.12f, 0},
         {0, -1, 0}, {1, 0, 0},
         -1.2f, 1.2f, 1.2f, 0.8f, 150.0f},

        // 8: Left Forearm
        {"LeftForearm",
         {"LeftForeArm", "lowerarm_l", "L_Forearm", "mixamorig:LeftForeArm", "Bip01_L_Forearm"},
         7,
         0.11f, 0.035f, 1.5f,
         {0, -0.12f, 0}, {0, 0.11f, 0},
         {0, -1, 0}, {1, 0, 0},
         -2.0f, 0.0f, 0.1f, 0.1f, 100.0f},

        // 9: Left Hand
        {"LeftHand",
         {"LeftHand", "hand_l", "L_Hand", "mixamorig:LeftHand", "Bip01_L_Hand"},
         8,
         0.04f, 0.03f, 0.5f,
         {0, -0.11f, 0}, {0, 0.04f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.4f, 0.4f, 50.0f},

        // 10: Right Shoulder (clavicle)
        {"RightShoulder",
         {"RightShoulder", "clavicle_r", "R_Clavicle", "mixamorig:RightShoulder", "Bip01_R_Clavicle"},
         3,
         0.06f, 0.03f, 1.5f,
         {0.06f, 0.08f, 0}, {-0.06f, 0, 0},
         {1, 0, 0}, {0, 1, 0},
         -0.2f, 0.2f, 0.2f, 0.2f, 100.0f},

        // 11: Right Upper Arm
        {"RightUpperArm",
         {"RightArm", "upperarm_r", "R_UpperArm", "mixamorig:RightArm", "Bip01_R_UpperArm"},
         10,
         0.12f, 0.04f, 2.5f,
         {0.06f, 0, 0}, {0, 0.12f, 0},
         {0, -1, 0}, {1, 0, 0},
         -1.2f, 1.2f, 1.2f, 0.8f, 150.0f},

        // 12: Right Forearm
        {"RightForearm",
         {"RightForeArm", "lowerarm_r", "R_Forearm", "mixamorig:RightForeArm", "Bip01_R_Forearm"},
         11,
         0.11f, 0.035f, 1.5f,
         {0, -0.12f, 0}, {0, 0.11f, 0},
         {0, -1, 0}, {1, 0, 0},
         -2.0f, 0.0f, 0.1f, 0.1f, 100.0f},

        // 13: Right Hand
        {"RightHand",
         {"RightHand", "hand_r", "R_Hand", "mixamorig:RightHand", "Bip01_R_Hand"},
         12,
         0.04f, 0.03f, 0.5f,
         {0, -0.11f, 0}, {0, 0.04f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.4f, 0.4f, 50.0f},

        // 14: Left Thigh
        {"LeftThigh",
         {"LeftUpLeg", "thigh_l", "L_Thigh", "mixamorig:LeftUpLeg", "Bip01_L_Thigh"},
         0,
         0.18f, 0.06f, 6.0f,
         {-0.10f, -0.08f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.8f, 0.5f, 600.0f},

        // 15: Left Shin
        {"LeftShin",
         {"LeftLeg", "calf_l", "L_Shin", "mixamorig:LeftLeg", "Bip01_L_Calf"},
         14,
         0.18f, 0.05f, 4.0f,
         {0, -0.18f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         0.0f, 2.5f, 0.1f, 0.1f, 400.0f},

        // 16: Left Foot
        {"LeftFoot",
         {"LeftFoot", "foot_l", "L_Foot", "mixamorig:LeftFoot", "Bip01_L_Foot"},
         15,
         0.06f, 0.035f, 1.0f,
         {0, -0.18f, 0}, {0, 0.035f, 0.03f},
         {1, 0, 0}, {0, 1, 0},
         -0.5f, 0.5f, 0.3f, 0.3f, 100.0f},

        // 17: Right Thigh
        {"RightThigh",
         {"RightUpLeg", "thigh_r", "R_Thigh", "mixamorig:RightUpLeg", "Bip01_R_Thigh"},
         0,
         0.18f, 0.06f, 6.0f,
         {0.10f, -0.08f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.8f, 0.5f, 600.0f},

        // 18: Right Shin
        {"RightShin",
         {"RightLeg", "calf_r", "R_Shin", "mixamorig:RightLeg", "Bip01_R_Calf"},
         17,
         0.18f, 0.05f, 4.0f,
         {0, -0.18f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         0.0f, 2.5f, 0.1f, 0.1f, 400.0f},

        // 19: Right Foot
        {"RightFoot",
         {"RightFoot", "foot_r", "R_Foot", "mixamorig:RightFoot", "Bip01_R_Foot"},
         18,
         0.06f, 0.035f, 1.0f,
         {0, -0.18f, 0}, {0, 0.035f, 0.03f},
         {1, 0, 0}, {0, 1, 0},
         -0.5f, 0.5f, 0.3f, 0.3f, 100.0f},
    };

    config.parts.reserve(templates.size());
    int32_t mappedCount = 0;

    for (const auto& tmpl : templates) {
        BodyPartDef part;
        part.name = tmpl.name;
        part.skeletonJointIndex = findJoint(skeleton, tmpl.jointNames);
        part.parentPartIndex = tmpl.parentPart;
        part.halfHeight = tmpl.halfHeight;
        part.radius = tmpl.radius;
        part.mass = tmpl.mass;
        part.localAnchorInParent = tmpl.anchorInParent;
        part.localAnchorInChild = tmpl.anchorInChild;
        part.twistAxis = tmpl.twistAxis;
        part.planeAxis = tmpl.planeAxis;
        part.twistMinAngle = tmpl.twistMin;
        part.twistMaxAngle = tmpl.twistMax;
        part.normalHalfConeAngle = tmpl.normalCone;
        part.planeHalfConeAngle = tmpl.planeCone;
        part.effortFactor = tmpl.effortFactor;

        if (part.skeletonJointIndex >= 0) {
            ++mappedCount;
        }

        config.parts.push_back(part);
    }

    SDL_Log("createHumanoidConfig: %d/%zu joints mapped to skeleton (%zu total skeleton joints)",
            mappedCount, templates.size(), skeleton.joints.size());

    return config;
}
