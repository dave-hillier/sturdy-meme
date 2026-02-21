#include "ArticulatedBody.h"
#include "GLTFLoader.h"

#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>

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
