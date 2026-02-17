#include "ArticulatedBody.h"
#include "PhysicsConversions.h"
#include "JoltLayerConfig.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

// Skeleton is defined in GLTFLoader.h
#include "GLTFLoader.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

JPH_SUPPRESS_WARNINGS

using namespace PhysicsConversions;

// ─── ArticulatedBody move semantics ────────────────────────────────────────────

ArticulatedBody::~ArticulatedBody() {
    // If we still own bodies, the caller forgot to call destroy().
    // We can't clean up here because we don't have a physics reference.
    if (!bodyIDs_.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ArticulatedBody destroyed without calling destroy() - %zu bodies leaked",
                    bodyIDs_.size());
    }
}

ArticulatedBody::ArticulatedBody(ArticulatedBody&& other) noexcept
    : bodyIDs_(std::move(other.bodyIDs_))
    , jointIndices_(std::move(other.jointIndices_))
    , effortFactors_(std::move(other.effortFactors_))
    , constraints_(std::move(other.constraints_))
    , ownerPhysics_(other.ownerPhysics_)
{
    other.ownerPhysics_ = nullptr;
}

ArticulatedBody& ArticulatedBody::operator=(ArticulatedBody&& other) noexcept {
    if (this != &other) {
        bodyIDs_ = std::move(other.bodyIDs_);
        jointIndices_ = std::move(other.jointIndices_);
        effortFactors_ = std::move(other.effortFactors_);
        constraints_ = std::move(other.constraints_);
        ownerPhysics_ = other.ownerPhysics_;
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

    bodyIDs_.resize(numParts, INVALID_BODY_ID);
    jointIndices_.resize(numParts, -1);
    effortFactors_.resize(numParts, 200.0f);

    // Phase 1: Create all rigid bodies
    // Each part gets a capsule shape positioned relative to the root.
    // For simplicity, all parts start at rootPosition offset by their parent chain.
    // The actual positions will be set using the parent anchor offsets.

    // Compute world positions for each part by traversing the parent chain
    std::vector<glm::vec3> partPositions(numParts);
    std::vector<glm::quat> partRotations(numParts, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < numParts; ++i) {
        const auto& part = config.parts[i];
        if (part.parentPartIndex < 0) {
            // Root part: place at rootPosition
            partPositions[i] = rootPosition;
        } else {
            // Child part: offset from parent's position using the anchor points
            size_t parentIdx = static_cast<size_t>(part.parentPartIndex);
            glm::vec3 parentPos = partPositions[parentIdx];
            // The child is placed such that localAnchorInParent in parent space
            // connects to localAnchorInChild in child space
            glm::vec3 worldAnchor = parentPos + partRotations[parentIdx] * (part.localAnchorInParent * scale);
            partPositions[i] = worldAnchor - partRotations[i] * (part.localAnchorInChild * scale);
        }
    }

    for (size_t i = 0; i < numParts; ++i) {
        const auto& part = config.parts[i];
        float halfH = part.halfHeight * scale;
        float rad = part.radius * scale;

        PhysicsBodyID bodyID = physics.createCapsule(
            partPositions[i], halfH, rad, part.mass,
            0.8f,  // friction
            0.0f   // restitution (ragdoll should not bounce)
        );

        if (bodyID == INVALID_BODY_ID) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ArticulatedBody::create: failed to create body for part '%s'",
                         part.name.c_str());
            cleanup(physics);
            return false;
        }

        bodyIDs_[i] = bodyID;
        jointIndices_[i] = part.skeletonJointIndex;
        effortFactors_[i] = part.effortFactor;
    }

    // Phase 2: Create SwingTwist constraints between parent-child pairs
    for (size_t i = 0; i < numParts; ++i) {
        const auto& part = config.parts[i];
        if (part.parentPartIndex < 0) continue;  // Root has no constraint

        size_t parentIdx = static_cast<size_t>(part.parentPartIndex);
        JPH::BodyID parentJoltID(bodyIDs_[parentIdx]);
        JPH::BodyID childJoltID(bodyIDs_[i]);

        // Lock both bodies to create the constraint
        JPH::BodyID lockIDs[2] = { parentJoltID, childJoltID };
        JPH::BodyLockMultiWrite lock(joltSystem->GetBodyLockInterface(), lockIDs, 2);

        JPH::Body* parentBody = lock.GetBody(0);
        JPH::Body* childBody = lock.GetBody(1);

        if (!parentBody || !childBody) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "ArticulatedBody::create: failed to lock bodies for constraint '%s'",
                         part.name.c_str());
            cleanup(physics);
            return false;
        }

        // Compute world-space constraint position (the anchor point between parent and child)
        glm::vec3 worldAnchor = partPositions[parentIdx] +
            partRotations[parentIdx] * (part.localAnchorInParent * scale);

        // Set up SwingTwist constraint in world space
        JPH::SwingTwistConstraintSettings settings;
        settings.mSpace = JPH::EConstraintSpace::WorldSpace;

        settings.mPosition1 = JPH::RVec3(worldAnchor.x, worldAnchor.y, worldAnchor.z);
        settings.mPosition2 = JPH::RVec3(worldAnchor.x, worldAnchor.y, worldAnchor.z);

        settings.mTwistAxis1 = toJolt(part.twistAxis);
        settings.mPlaneAxis1 = toJolt(part.planeAxis);
        settings.mTwistAxis2 = toJolt(part.twistAxis);
        settings.mPlaneAxis2 = toJolt(part.planeAxis);

        settings.mNormalHalfConeAngle = part.normalHalfConeAngle;
        settings.mPlaneHalfConeAngle = part.planeHalfConeAngle;
        settings.mTwistMinAngle = part.twistMinAngle;
        settings.mTwistMaxAngle = part.twistMaxAngle;

        // Add some friction to prevent jittery ragdoll
        settings.mMaxFrictionTorque = 2.0f;

        JPH::TwoBodyConstraint* constraint = static_cast<JPH::TwoBodyConstraint*>(
            settings.Create(*parentBody, *childBody)
        );

        // Must release the body locks before adding the constraint to the system
        lock.ReleaseLocks();

        joltSystem->AddConstraint(constraint);
        constraints_.push_back(constraint);
    }

    ownerPhysics_ = &physics;

    SDL_Log("ArticulatedBody created: %zu parts, %zu constraints",
            numParts, constraints_.size());
    return true;
}

// ─── destroy ───────────────────────────────────────────────────────────────────

void ArticulatedBody::destroy(PhysicsWorld& physics) {
    cleanup(physics);
}

void ArticulatedBody::cleanup(PhysicsWorld& physics) {
    JPH::PhysicsSystem* joltSystem = physics.getJoltSystem();

    // Remove constraints first (they reference bodies)
    if (joltSystem) {
        for (auto* constraint : constraints_) {
            if (constraint) {
                joltSystem->RemoveConstraint(constraint);
            }
        }
    }
    constraints_.clear();

    // Remove all bodies
    for (auto bodyID : bodyIDs_) {
        if (bodyID != INVALID_BODY_ID) {
            physics.removeBody(bodyID);
        }
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
        // Scale the normalized policy output by the part's effort factor
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

        // Convert physics world transform to the joint's local transform
        // The joint's local transform is relative to its parent
        auto& joint = skeleton.joints[jointIdx];

        if (joint.parentIndex < 0) {
            // Root joint: world transform is the local transform
            glm::mat4 transform = glm::translate(glm::mat4(1.0f), info.position);
            transform *= glm::mat4_cast(info.rotation);
            joint.localTransform = transform;
        } else {
            // Non-root: we need to compute local transform relative to parent
            // For now, set the rotation from the physics body.
            // Parent's global transform would need to be computed first.
            // This is a simplification — a full implementation would
            // compute the parent's global transform and invert it.

            // Compute the parent body's world transform
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

                // Local rotation = inverse(parentWorldRot) * childWorldRot
                glm::quat localRot = parentRotInv * info.rotation;
                // Local position = inverse(parentWorldRot) * (childWorldPos - parentWorldPos)
                glm::vec3 localPos = parentRotInv * (info.position - parentInfo.position);

                glm::mat4 localTransform = glm::translate(glm::mat4(1.0f), localPos);
                localTransform *= glm::mat4_cast(localRot);
                joint.localTransform = localTransform;
            } else {
                // Parent joint not mapped to a physics body — use world transform
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

    // Map of part name -> (candidate joint names, parent part index)
    // We build the 20-body humanoid from UniCon's specification:
    // Pelvis, LowerSpine, UpperSpine, Chest, Head,
    // L/R UpperArm, L/R Forearm, L/R Hand,
    // L/R Thigh, L/R Shin, L/R Foot

    struct PartTemplate {
        std::string name;
        std::vector<std::string> jointNames;
        int32_t parentPart;         // index in the parts array, -1 for root
        float halfHeight;
        float radius;
        float mass;
        glm::vec3 anchorInParent;   // where this attaches to parent
        glm::vec3 anchorInChild;    // attachment point in this part's local space
        glm::vec3 twistAxis;
        glm::vec3 planeAxis;
        float twistMin, twistMax;
        float normalCone, planeCone;
        float effortFactor;
    };

    // Y-up coordinate system, capsules aligned along Y
    // Anchor points are relative to the center of each capsule
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

        // 4: Head
        {"Head",
         {"Head", "head", "mixamorig:Head", "Bip01_Head"},
         3,
         0.06f, 0.09f, 4.0f,
         {0, 0.10f, 0}, {0, -0.06f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.4f, 0.4f, 0.3f, 0.3f, 100.0f},

        // 5: Left Upper Arm
        {"LeftUpperArm",
         {"LeftArm", "upperarm_l", "L_UpperArm", "mixamorig:LeftArm", "Bip01_L_UpperArm"},
         3,
         0.12f, 0.04f, 2.5f,
         {-0.18f, 0.08f, 0}, {0, 0.12f, 0},
         {0, -1, 0}, {1, 0, 0},
         -1.2f, 1.2f, 1.2f, 0.8f, 150.0f},

        // 6: Left Forearm
        {"LeftForearm",
         {"LeftForeArm", "lowerarm_l", "L_Forearm", "mixamorig:LeftForeArm", "Bip01_L_Forearm"},
         5,
         0.11f, 0.035f, 1.5f,
         {0, -0.12f, 0}, {0, 0.11f, 0},
         {0, -1, 0}, {1, 0, 0},
         -2.0f, 0.0f, 0.1f, 0.1f, 100.0f},

        // 7: Left Hand
        {"LeftHand",
         {"LeftHand", "hand_l", "L_Hand", "mixamorig:LeftHand", "Bip01_L_Hand"},
         6,
         0.04f, 0.03f, 0.5f,
         {0, -0.11f, 0}, {0, 0.04f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.4f, 0.4f, 50.0f},

        // 8: Right Upper Arm
        {"RightUpperArm",
         {"RightArm", "upperarm_r", "R_UpperArm", "mixamorig:RightArm", "Bip01_R_UpperArm"},
         3,
         0.12f, 0.04f, 2.5f,
         {0.18f, 0.08f, 0}, {0, 0.12f, 0},
         {0, -1, 0}, {1, 0, 0},
         -1.2f, 1.2f, 1.2f, 0.8f, 150.0f},

        // 9: Right Forearm
        {"RightForearm",
         {"RightForeArm", "lowerarm_r", "R_Forearm", "mixamorig:RightForeArm", "Bip01_R_Forearm"},
         8,
         0.11f, 0.035f, 1.5f,
         {0, -0.12f, 0}, {0, 0.11f, 0},
         {0, -1, 0}, {1, 0, 0},
         -2.0f, 0.0f, 0.1f, 0.1f, 100.0f},

        // 10: Right Hand
        {"RightHand",
         {"RightHand", "hand_r", "R_Hand", "mixamorig:RightHand", "Bip01_R_Hand"},
         9,
         0.04f, 0.03f, 0.5f,
         {0, -0.11f, 0}, {0, 0.04f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.4f, 0.4f, 50.0f},

        // 11: Left Thigh
        {"LeftThigh",
         {"LeftUpLeg", "thigh_l", "L_Thigh", "mixamorig:LeftUpLeg", "Bip01_L_Thigh"},
         0,
         0.18f, 0.06f, 6.0f,
         {-0.10f, -0.08f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.8f, 0.5f, 600.0f},

        // 12: Left Shin
        {"LeftShin",
         {"LeftLeg", "calf_l", "L_Shin", "mixamorig:LeftLeg", "Bip01_L_Calf"},
         11,
         0.18f, 0.05f, 4.0f,
         {0, -0.18f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         0.0f, 2.5f, 0.1f, 0.1f, 400.0f},

        // 13: Left Foot
        {"LeftFoot",
         {"LeftFoot", "foot_l", "L_Foot", "mixamorig:LeftFoot", "Bip01_L_Foot"},
         12,
         0.06f, 0.035f, 1.0f,
         {0, -0.18f, 0}, {0, 0.035f, 0.03f},
         {1, 0, 0}, {0, 1, 0},
         -0.5f, 0.5f, 0.3f, 0.3f, 100.0f},

        // 14: Right Thigh
        {"RightThigh",
         {"RightUpLeg", "thigh_r", "R_Thigh", "mixamorig:RightUpLeg", "Bip01_R_Thigh"},
         0,
         0.18f, 0.06f, 6.0f,
         {0.10f, -0.08f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         -0.5f, 0.5f, 0.8f, 0.5f, 600.0f},

        // 15: Right Shin
        {"RightShin",
         {"RightLeg", "calf_r", "R_Shin", "mixamorig:RightLeg", "Bip01_R_Calf"},
         14,
         0.18f, 0.05f, 4.0f,
         {0, -0.18f, 0}, {0, 0.18f, 0},
         {0, -1, 0}, {1, 0, 0},
         0.0f, 2.5f, 0.1f, 0.1f, 400.0f},

        // 16: Right Foot
        {"RightFoot",
         {"RightFoot", "foot_r", "R_Foot", "mixamorig:RightFoot", "Bip01_R_Foot"},
         15,
         0.06f, 0.035f, 1.0f,
         {0, -0.18f, 0}, {0, 0.035f, 0.03f},
         {1, 0, 0}, {0, 1, 0},
         -0.5f, 0.5f, 0.3f, 0.3f, 100.0f},

        // 17: Neck
        {"Neck",
         {"Neck", "neck_01", "mixamorig:Neck", "Bip01_Neck"},
         3,
         0.04f, 0.04f, 2.0f,
         {0, 0.10f, 0}, {0, -0.04f, 0},
         {0, 1, 0}, {1, 0, 0},
         -0.3f, 0.3f, 0.3f, 0.3f, 100.0f},

        // 18: Left Shoulder (clavicle)
        {"LeftShoulder",
         {"LeftShoulder", "clavicle_l", "L_Clavicle", "mixamorig:LeftShoulder", "Bip01_L_Clavicle"},
         3,
         0.06f, 0.03f, 1.5f,
         {-0.06f, 0.08f, 0}, {0.06f, 0, 0},
         {-1, 0, 0}, {0, 1, 0},
         -0.2f, 0.2f, 0.2f, 0.2f, 100.0f},

        // 19: Right Shoulder (clavicle)
        {"RightShoulder",
         {"RightShoulder", "clavicle_r", "R_Clavicle", "mixamorig:RightShoulder", "Bip01_R_Clavicle"},
         3,
         0.06f, 0.03f, 1.5f,
         {0.06f, 0.08f, 0}, {-0.06f, 0, 0},
         {1, 0, 0}, {0, 1, 0},
         -0.2f, 0.2f, 0.2f, 0.2f, 100.0f},
    };

    // If we have shoulder parts (18, 19), reparent the arms to shoulders
    // This is done in the template above by having UpperArm parent = Chest (3)
    // but ideally LeftUpperArm should parent to LeftShoulder and
    // RightUpperArm to RightShoulder if those joints exist.

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

    // Reparent arms to shoulder parts if the shoulder joints were found
    if (config.parts.size() >= 20) {
        // LeftUpperArm (5) -> LeftShoulder (18) if shoulder joint found
        if (config.parts[18].skeletonJointIndex >= 0) {
            config.parts[5].parentPartIndex = 18;
            config.parts[5].localAnchorInParent = {-0.06f, 0, 0};
        }
        // RightUpperArm (8) -> RightShoulder (19) if shoulder joint found
        if (config.parts[19].skeletonJointIndex >= 0) {
            config.parts[8].parentPartIndex = 19;
            config.parts[8].localAnchorInParent = {0.06f, 0, 0};
        }
        // Head (4) -> Neck (17) if neck joint found
        if (config.parts[17].skeletonJointIndex >= 0) {
            config.parts[4].parentPartIndex = 17;
            config.parts[4].localAnchorInParent = {0, 0.04f, 0};
        }
    }

    SDL_Log("createHumanoidConfig: %d/%zu joints mapped to skeleton (%zu total skeleton joints)",
            mappedCount, templates.size(), skeleton.joints.size());

    return config;
}
