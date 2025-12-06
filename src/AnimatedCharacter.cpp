#include "AnimatedCharacter.h"
#include "GLTFLoader.h"
#include "FBXLoader.h"
#include "PhysicsSystem.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace {
bool endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

bool AnimatedCharacter::load(const std::string& path, VmaAllocator allocator, VkDevice device,
                              VkCommandPool commandPool, VkQueue queue) {
    std::optional<GLTFSkinnedLoadResult> result;

    // Detect file format and use appropriate loader
    if (endsWith(path, ".fbx") || endsWith(path, ".FBX")) {
        result = FBXLoader::loadSkinned(path);
    } else {
        result = GLTFLoader::loadSkinned(path);
    }

    if (!result) {
        SDL_Log("AnimatedCharacter: Failed to load %s", path.c_str());
        return false;
    }

    // Store bind pose data
    bindPoseVertices = std::move(result->vertices);
    indices = std::move(result->indices);
    skeleton = std::move(result->skeleton);
    animations = std::move(result->animations);
    materials = std::move(result->materials);

    // Log loaded materials
    if (!materials.empty()) {
        SDL_Log("AnimatedCharacter: Loaded %zu materials", materials.size());
        for (const auto& mat : materials) {
            SDL_Log("  Material '%s': roughness=%.2f metallic=%.2f",
                    mat.name.c_str(), mat.roughness, mat.metallic);
        }
    }

    // Store bind pose local transforms so we can reset before each animation sample
    bindPoseLocalTransforms.resize(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        bindPoseLocalTransforms[i] = skeleton.joints[i].localTransform;
    }

    // GPU skinning: Upload skinned mesh with original bind pose vertices
    // The GPU will apply bone matrices in the vertex shader
    SkinnedMeshData meshData;
    meshData.vertices = bindPoseVertices;
    meshData.indices = indices;
    meshData.skeleton = skeleton;
    skinnedMesh.setData(meshData);
    skinnedMesh.upload(allocator, device, commandPool, queue);

    // Initialize renderMesh with bind pose for bounds/transform tracking
    // This mesh is used by sceneObjects for Hi-Z culling and transform updates,
    // but actual rendering is skipped (handled by recordSkinnedCharacter)
    meshVertices.resize(bindPoseVertices.size());
    for (size_t i = 0; i < bindPoseVertices.size(); ++i) {
        meshVertices[i].position = bindPoseVertices[i].position;
        meshVertices[i].normal = bindPoseVertices[i].normal;
        meshVertices[i].texCoord = bindPoseVertices[i].texCoord;
        meshVertices[i].tangent = bindPoseVertices[i].tangent;
        meshVertices[i].color = bindPoseVertices[i].color;
    }
    renderMesh.setCustomGeometry(meshVertices, indices);
    renderMesh.upload(allocator, device, commandPool, queue);

    // Set up default animation (play first one if available)
    if (!animations.empty()) {
        animationPlayer.setAnimation(&animations[0]);
        SDL_Log("AnimatedCharacter: Loaded with %zu animations, playing '%s'",
                animations.size(), animations[0].name.c_str());

        // Set up animation state machine with locomotion animations
        // Look for idle, walk, run, jump animations by name
        const AnimationClip* idleClip = nullptr;
        const AnimationClip* walkClip = nullptr;
        const AnimationClip* runClip = nullptr;
        const AnimationClip* jumpClip = nullptr;

        for (const auto& clip : animations) {
            std::string lowerName = clip.name;
            for (char& c : lowerName) c = std::tolower(c);

            if (lowerName.find("idle") != std::string::npos) {
                idleClip = &clip;
            } else if (lowerName.find("walk") != std::string::npos) {
                walkClip = &clip;
            } else if (lowerName.find("run") != std::string::npos) {
                runClip = &clip;
            } else if (lowerName.find("jump") != std::string::npos) {
                jumpClip = &clip;
            }
        }

        // Add states to state machine
        if (idleClip) {
            stateMachine.addState("idle", idleClip, true);
            SDL_Log("AnimatedCharacter: Added 'idle' state");
        }
        if (walkClip) {
            stateMachine.addState("walk", walkClip, true);
            SDL_Log("AnimatedCharacter: Added 'walk' state");
        }
        if (runClip) {
            stateMachine.addState("run", runClip, true);
            SDL_Log("AnimatedCharacter: Added 'run' state");
        }
        if (jumpClip) {
            stateMachine.addState("jump", jumpClip, false);
            SDL_Log("AnimatedCharacter: Added 'jump' state");
        }

        // Enable state machine if we have at least idle
        if (idleClip) {
            stateMachine.setState("idle");
            useStateMachine = true;
            SDL_Log("AnimatedCharacter: State machine enabled with %s locomotion animations",
                    (walkClip && runClip) ? "full" : "partial");
        }
    } else {
        SDL_Log("AnimatedCharacter: Loaded but no animations found");
    }

    loaded = true;
    return true;
}

void AnimatedCharacter::destroy(VmaAllocator allocator) {
    skinnedMesh.destroy(allocator);
    renderMesh.destroy(allocator);
    bindPoseVertices.clear();
    indices.clear();
    skeleton.joints.clear();
    bindPoseLocalTransforms.clear();
    animations.clear();
    meshVertices.clear();
    loaded = false;
}

void AnimatedCharacter::loadAdditionalAnimations(const std::vector<std::string>& paths) {
    if (!loaded) {
        SDL_Log("AnimatedCharacter: Cannot load animations before loading character");
        return;
    }

    for (const auto& path : paths) {
        auto newAnims = FBXLoader::loadAnimations(path, skeleton);
        for (auto& anim : newAnims) {
            animations.push_back(std::move(anim));
        }
    }

    // Re-setup state machine with all animations
    stateMachine = AnimationStateMachine();  // Reset

    const AnimationClip* idleClip = nullptr;
    const AnimationClip* walkClip = nullptr;
    const AnimationClip* runClip = nullptr;
    const AnimationClip* jumpClip = nullptr;

    for (const auto& clip : animations) {
        std::string lowerName = clip.name;
        for (char& c : lowerName) c = std::tolower(c);

        if (lowerName.find("idle") != std::string::npos) {
            idleClip = &clip;
        } else if (lowerName.find("walk") != std::string::npos) {
            walkClip = &clip;
        } else if (lowerName.find("run") != std::string::npos) {
            runClip = &clip;
        } else if (lowerName.find("jump") != std::string::npos) {
            jumpClip = &clip;
        }
    }

    if (idleClip) {
        stateMachine.addState("idle", idleClip, true);
        SDL_Log("AnimatedCharacter: Added 'idle' state");
    }
    if (walkClip) {
        stateMachine.addState("walk", walkClip, true);
        SDL_Log("AnimatedCharacter: Added 'walk' state");
    }
    if (runClip) {
        stateMachine.addState("run", runClip, true);
        SDL_Log("AnimatedCharacter: Added 'run' state");
    }
    if (jumpClip) {
        stateMachine.addState("jump", jumpClip, false);
        SDL_Log("AnimatedCharacter: Added 'jump' state");
    }

    if (idleClip) {
        stateMachine.setState("idle");
        useStateMachine = true;
        SDL_Log("AnimatedCharacter: State machine refreshed with %zu total animations",
                animations.size());
    }
}

void AnimatedCharacter::playAnimation(const std::string& name) {
    for (size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == name || animations[i].name.find(name) != std::string::npos) {
            playAnimation(i);
            return;
        }
    }
    SDL_Log("AnimatedCharacter: Animation '%s' not found", name.c_str());
}

void AnimatedCharacter::playAnimation(size_t index) {
    if (index < animations.size()) {
        animationPlayer.setAnimation(&animations[index]);
        SDL_Log("AnimatedCharacter: Now playing '%s'", animations[index].name.c_str());
    }
}

void AnimatedCharacter::setPlaybackSpeed(float speed) {
    animationPlayer.setPlaybackSpeed(speed);
}

void AnimatedCharacter::setLooping(bool loop) {
    animationPlayer.setLooping(loop);
}

void AnimatedCharacter::startJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) {
    if (useStateMachine) {
        stateMachine.startJump(startPos, velocity, gravity, physics);
    }
}

const AnimationClip* AnimatedCharacter::getCurrentAnimation() const {
    for (const auto& clip : animations) {
        if (animationPlayer.getDuration() == clip.duration) {
            return &clip;
        }
    }
    return nullptr;
}

void AnimatedCharacter::update(float deltaTime, VmaAllocator allocator, VkDevice device,
                                VkCommandPool commandPool, VkQueue queue,
                                float movementSpeed, bool isGrounded, bool isJumping,
                                const glm::mat4& worldTransform) {
    if (!loaded) return;

    // Reset skeleton to bind pose before applying animation
    // This ensures joints not affected by the current animation keep their bind pose
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        skeleton.joints[i].localTransform = bindPoseLocalTransforms[i];
    }

    if (useStateMachine) {
        // Use state machine for animation selection and blending
        stateMachine.update(deltaTime, movementSpeed, isGrounded, isJumping);
        stateMachine.applyToSkeleton(skeleton);
    } else {
        // Fallback to simple animation player
        animationPlayer.update(deltaTime);
        animationPlayer.applyToSkeleton(skeleton);
    }

    // Update foot locking state based on movement speed
    // During idle, feet should lock in place to prevent IK sliding
    // During movement, feet should follow the animation with IK ground adaptation
    constexpr float IDLE_THRESHOLD = 0.1f;
    constexpr float LOCK_BLEND_SPEED = 5.0f;  // How fast to blend into/out of lock

    auto updateFootLock = [&](FootPlacementIK* foot) {
        if (!foot || !foot->enabled) return;

        float targetLockBlend = (movementSpeed < IDLE_THRESHOLD) ? 1.0f : 0.0f;

        // Smoothly blend toward target lock state
        if (deltaTime > 0.0f) {
            float blendDelta = LOCK_BLEND_SPEED * deltaTime;
            if (foot->lockBlend < targetLockBlend) {
                foot->lockBlend = std::min(foot->lockBlend + blendDelta, targetLockBlend);
            } else if (foot->lockBlend > targetLockBlend) {
                foot->lockBlend = std::max(foot->lockBlend - blendDelta, targetLockBlend);
                // When unlocking, reset the lock so it re-establishes when stopping
                if (foot->lockBlend < 0.1f) {
                    foot->isLocked = false;
                }
            }
        }
    };

    updateFootLock(ikSystem.getFootPlacement("LeftFoot"));
    updateFootLock(ikSystem.getFootPlacement("RightFoot"));

    // Apply IK after animation sampling
    // Pass world transform so foot placement can query terrain in world space
    if (ikSystem.hasEnabledChains()) {
        ikSystem.solve(skeleton, worldTransform, deltaTime);
    }

    // GPU skinning: Bone matrices are computed and uploaded by Renderer each frame
    // No mesh re-upload needed - the vertex shader applies skinning
}

void AnimatedCharacter::computeBoneMatrices(std::vector<glm::mat4>& outBoneMatrices) const {
    // First compute global transforms
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Then multiply by inverse bind matrices to get final bone matrices
    outBoneMatrices.resize(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        outBoneMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
    }
}

void AnimatedCharacter::setupDefaultIKChains() {
    if (!loaded) {
        SDL_Log("AnimatedCharacter: Cannot setup IK chains before loading character");
        return;
    }

    ikSystem.clear();

    // Common bone name patterns for humanoid rigs
    // Mixamo uses "mixamorig:" prefix, others may not
    auto findBone = [this](const std::vector<std::string>& names) -> std::string {
        for (const auto& name : names) {
            if (skeleton.findJointIndex(name) >= 0) {
                return name;
            }
            // Try with mixamorig prefix
            std::string mixamoName = "mixamorig:" + name;
            if (skeleton.findJointIndex(mixamoName) >= 0) {
                return mixamoName;
            }
        }
        return "";
    };

    // Left arm chain
    std::string leftShoulder = findBone({"LeftArm", "LeftUpperArm", "L_UpperArm", "shoulder.L", "upperarm_l"});
    std::string leftElbow = findBone({"LeftForeArm", "LeftLowerArm", "L_LowerArm", "forearm.L", "lowerarm_l"});
    std::string leftHand = findBone({"LeftHand", "L_Hand", "hand.L", "hand_l"});

    if (!leftShoulder.empty() && !leftElbow.empty() && !leftHand.empty()) {
        if (ikSystem.addTwoBoneChain("LeftArm", skeleton, leftShoulder, leftElbow, leftHand)) {
            SDL_Log("AnimatedCharacter: Setup left arm IK chain");
        }
    }

    // Right arm chain
    std::string rightShoulder = findBone({"RightArm", "RightUpperArm", "R_UpperArm", "shoulder.R", "upperarm_r"});
    std::string rightElbow = findBone({"RightForeArm", "RightLowerArm", "R_LowerArm", "forearm.R", "lowerarm_r"});
    std::string rightHand = findBone({"RightHand", "R_Hand", "hand.R", "hand_r"});

    if (!rightShoulder.empty() && !rightElbow.empty() && !rightHand.empty()) {
        if (ikSystem.addTwoBoneChain("RightArm", skeleton, rightShoulder, rightElbow, rightHand)) {
            SDL_Log("AnimatedCharacter: Setup right arm IK chain");
        }
    }

    // Note: Leg chains are NOT created as separate two-bone chains.
    // Leg IK is handled by the foot placement system which creates its own
    // two-bone chains internally. Creating separate leg chains would cause
    // double-solving and incorrect results.

    // Find leg bones for foot placement (used below)
    std::string leftThigh = findBone({"LeftUpLeg", "LeftUpperLeg", "L_UpperLeg", "thigh.L", "thigh_l"});
    std::string leftKnee = findBone({"LeftLeg", "LeftLowerLeg", "L_LowerLeg", "shin.L", "calf_l"});
    std::string leftFoot = findBone({"LeftFoot", "L_Foot", "foot.L", "foot_l"});
    std::string rightThigh = findBone({"RightUpLeg", "RightUpperLeg", "R_UpperLeg", "thigh.R", "thigh_r"});
    std::string rightKnee = findBone({"RightLeg", "RightLowerLeg", "R_LowerLeg", "shin.R", "calf_r"});
    std::string rightFoot = findBone({"RightFoot", "R_Foot", "foot.R", "foot_r"});

    // Look-At IK (head tracking)
    std::string head = findBone({"Head", "head"});
    std::string neck = findBone({"Neck", "neck"});
    std::string spine2 = findBone({"Spine2", "Spine1", "spine_02", "spine2"});

    if (!head.empty()) {
        if (ikSystem.setupLookAt(skeleton, head, neck, spine2)) {
            SDL_Log("AnimatedCharacter: Setup look-at IK");
        }
    }

    // Foot Placement IK
    std::string leftToe = findBone({"LeftToeBase", "LeftToe", "L_Toe", "toe.L", "ball_l"});
    std::string rightToe = findBone({"RightToeBase", "RightToe", "R_Toe", "toe.R", "ball_r"});

    if (!leftThigh.empty() && !leftKnee.empty() && !leftFoot.empty()) {
        if (ikSystem.addFootPlacement("LeftFoot", skeleton, leftThigh, leftKnee, leftFoot, leftToe)) {
            // Set knee pole vector (forward)
            if (auto* foot = ikSystem.getFootPlacement("LeftFoot")) {
                foot->poleVector = glm::vec3(0, 0, 1);
            }
            SDL_Log("AnimatedCharacter: Setup left foot placement IK");
        }
    }

    if (!rightThigh.empty() && !rightKnee.empty() && !rightFoot.empty()) {
        if (ikSystem.addFootPlacement("RightFoot", skeleton, rightThigh, rightKnee, rightFoot, rightToe)) {
            // Set knee pole vector (forward)
            if (auto* foot = ikSystem.getFootPlacement("RightFoot")) {
                foot->poleVector = glm::vec3(0, 0, 1);
            }
            SDL_Log("AnimatedCharacter: Setup right foot placement IK");
        }
    }

    // Pelvis adjustment for foot IK
    std::string hips = findBone({"Hips", "Pelvis", "pelvis", "hip"});
    if (!hips.empty()) {
        if (ikSystem.setupPelvisAdjustment(skeleton, hips)) {
            SDL_Log("AnimatedCharacter: Setup pelvis adjustment");
        }
    }

    SDL_Log("AnimatedCharacter: IK setup complete");
}

SkeletonDebugData AnimatedCharacter::getSkeletonDebugData(const glm::mat4& worldTransform) const {
    SkeletonDebugData data;

    if (!loaded || skeleton.joints.empty()) {
        return data;
    }

    // Compute global transforms for all joints
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Extract world positions for all joints
    data.jointPositions.resize(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        glm::vec4 localPos = globalTransforms[i] * glm::vec4(0, 0, 0, 1);
        glm::vec4 worldPos = worldTransform * localPos;
        data.jointPositions[i] = glm::vec3(worldPos);
    }

    // Build bone data (lines from parent to child)
    data.bones.reserve(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        const auto& joint = skeleton.joints[i];

        SkeletonDebugData::Bone bone;
        bone.name = joint.name;
        bone.parentIndex = joint.parentIndex;
        bone.endPos = data.jointPositions[i];

        // Check if this is an end effector (no children)
        bool hasChildren = false;
        for (size_t j = 0; j < skeleton.joints.size(); ++j) {
            if (skeleton.joints[j].parentIndex == static_cast<int32_t>(i)) {
                hasChildren = true;
                break;
            }
        }
        bone.isEndEffector = !hasChildren;

        if (joint.parentIndex >= 0 && joint.parentIndex < static_cast<int32_t>(data.jointPositions.size())) {
            bone.startPos = data.jointPositions[joint.parentIndex];
        } else {
            // Root bone - draw from origin to position
            bone.startPos = bone.endPos;
        }

        data.bones.push_back(bone);
    }

    return data;
}
