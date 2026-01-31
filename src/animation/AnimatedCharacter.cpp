#include "AnimatedCharacter.h"
#include "GLTFLoader.h"
#include "FBXLoader.h"
#include "PhysicsSystem.h"
#include "BoneMask.h"
#include "FootPhaseTracker.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace {
bool endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

std::unique_ptr<AnimatedCharacter> AnimatedCharacter::create(const InitInfo& info) {
    auto instance = std::make_unique<AnimatedCharacter>(ConstructToken{});
    if (!instance->loadInternal(info)) {
        return nullptr;
    }
    return instance;
}

AnimatedCharacter::~AnimatedCharacter() {
    cleanup();
}

bool AnimatedCharacter::loadInternal(const InitInfo& info) {
    allocator_ = info.allocator;

    std::optional<GLTFSkinnedLoadResult> result;

    // Detect file format and use appropriate loader
    if (endsWith(info.path, ".fbx") || endsWith(info.path, ".FBX")) {
        result = FBXLoader::loadSkinned(info.path);
    } else {
        result = GLTFLoader::loadSkinned(info.path);
    }

    if (!result) {
        SDL_Log("AnimatedCharacter: Failed to load %s", info.path.c_str());
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
    skinnedMesh.upload(info.allocator, info.device, info.commandPool, info.queue);

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
    renderMesh.upload(info.allocator, info.device, info.commandPool, info.queue);

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

        // Initialize layer controller with skeleton
        layerController.initialize(skeleton);

        // Setup locomotion blend space if we have the animations
        setupLocomotionBlendSpace();
    } else {
        SDL_Log("AnimatedCharacter: Loaded but no animations found");
    }

    loaded = true;

    // Build bone LOD masks for skeleton simplification at distance
    buildBoneLODMasks();

    return true;
}

void AnimatedCharacter::cleanup() {
    if (allocator_ != VK_NULL_HANDLE) {
        skinnedMesh.destroy(allocator_);
        renderMesh.releaseGPUResources();
    }
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
        currentAnimationIndex = index;
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
    if (currentAnimationIndex < animations.size()) {
        return &animations[currentAnimationIndex];
    }
    return nullptr;
}

void AnimatedCharacter::update(float deltaTime, VmaAllocator allocator, VkDevice device,
                                VkCommandPool commandPool, VkQueue queue,
                                float movementSpeed, bool isGrounded, bool isJumping,
                                const glm::mat4& worldTransform) {
    if (!loaded) return;

    // LOD optimization: skip full animation update if flagged
    // When skipped, we use cached bone matrices from the last full update
    if (skipAnimationUpdate_ && !cachedBoneMatrices_.empty()) {
        // Still need to advance time for state machine continuity, but at reduced rate
        // This prevents animation from "jumping" when we return to full updates
        if (useLayerController) {
            layerController.update(deltaTime * 0.1f);  // Minimal time advance
        } else if (useStateMachine) {
            stateMachine.update(deltaTime * 0.1f, movementSpeed, isGrounded, isJumping);
        } else {
            animationPlayer.update(deltaTime * 0.1f);
        }
        return;
    }

    // Reset skeleton to bind pose before applying animation
    // This ensures joints not affected by the current animation keep their bind pose
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        skeleton.joints[i].localTransform = bindPoseLocalTransforms[i];
    }

    if (useMotionMatching) {
        // Motion matching mode - apply the pose from the controller
        // (updateMotionMatching was called before update, but skeleton was reset to bind pose above)
        motionMatchingController.applyToSkeleton(skeleton);
    } else if (useLayerController) {
        // Use layer controller for advanced layer-based blending
        layerController.update(deltaTime);
        layerController.applyToSkeleton(skeleton);
    } else if (useStateMachine) {
        // Use state machine for animation selection and blending
        stateMachine.update(deltaTime, movementSpeed, isGrounded, isJumping);
        stateMachine.applyToSkeleton(skeleton);
    } else {
        // Fallback to simple animation player
        animationPlayer.update(deltaTime);
        animationPlayer.applyToSkeleton(skeleton);
    }

    // Update foot phase tracking and IK weights
    // Phase-aware approach: feet only get full IK during stance phase, reduced during swing
    constexpr float IDLE_THRESHOLD = 0.1f;
    constexpr float LOCK_BLEND_SPEED = 5.0f;  // How fast to blend into/out of lock

    // Get current normalized animation time for phase tracking
    float normalizedTime = 0.0f;
    if (useStateMachine) {
        const AnimationClip* currentClip = stateMachine.getCurrentClip();
        if (currentClip && currentClip->duration > 0.0f) {
            normalizedTime = stateMachine.getCurrentTime() / currentClip->duration;
            normalizedTime = std::fmod(normalizedTime, 1.0f);
            if (normalizedTime < 0.0f) normalizedTime += 1.0f;
        }
    }

    // Get foot world positions for phase tracking
    std::vector<glm::mat4> tempGlobalTransforms;
    skeleton.computeGlobalTransforms(tempGlobalTransforms);

    glm::vec3 leftFootWorldPos(0.0f), rightFootWorldPos(0.0f);
    auto* leftFoot = ikSystem.getFootPlacement("LeftFoot");
    auto* rightFoot = ikSystem.getFootPlacement("RightFoot");

    if (leftFoot && leftFoot->footBoneIndex >= 0 &&
        leftFoot->footBoneIndex < static_cast<int32_t>(tempGlobalTransforms.size())) {
        glm::vec4 localPos = tempGlobalTransforms[leftFoot->footBoneIndex] * glm::vec4(0, 0, 0, 1);
        leftFootWorldPos = glm::vec3(worldTransform * localPos);
    }
    if (rightFoot && rightFoot->footBoneIndex >= 0 &&
        rightFoot->footBoneIndex < static_cast<int32_t>(tempGlobalTransforms.size())) {
        glm::vec4 localPos = tempGlobalTransforms[rightFoot->footBoneIndex] * glm::vec4(0, 0, 0, 1);
        rightFootWorldPos = glm::vec3(worldTransform * localPos);
    }

    // Update foot phase tracker
    if (useFootPhaseTracking && movementSpeed > IDLE_THRESHOLD) {
        footPhaseTracker.update(normalizedTime, deltaTime, leftFootWorldPos, rightFootWorldPos, worldTransform);
    }

    // Apply phase-aware IK weights and foot locking
    auto updateFootLock = [&](FootPlacementIK* foot, bool isLeftFoot) {
        if (!foot || !foot->enabled) return;

        float targetLockBlend;
        float targetWeight;

        if (movementSpeed < IDLE_THRESHOLD) {
            // Idle: full lock, full IK
            targetLockBlend = 1.0f;
            targetWeight = 1.0f;
            foot->currentPhase = FootPhase::Stance;
            foot->phaseProgress = 0.0f;
        } else if (useFootPhaseTracking && footPhaseTracker.hasTimingData()) {
            // Use phase-aware values during locomotion
            targetLockBlend = footPhaseTracker.getLockBlend(isLeftFoot);
            targetWeight = footPhaseTracker.getIKWeight(isLeftFoot);

            // Pass phase data to foot for solver to use
            const FootPhaseData& phaseData = isLeftFoot
                ? footPhaseTracker.getLeftFoot()
                : footPhaseTracker.getRightFoot();
            foot->currentPhase = phaseData.phase;
            foot->phaseProgress = phaseData.phaseProgress;
        } else {
            // Fallback: no lock during movement, moderate IK
            targetLockBlend = 0.0f;
            targetWeight = 0.5f;  // Partial IK for ground adaptation
            foot->currentPhase = FootPhase::Swing;
            foot->phaseProgress = 0.5f;
        }

        // Smoothly blend toward target lock state
        if (deltaTime > 0.0f) {
            float blendDelta = LOCK_BLEND_SPEED * deltaTime;

            // Lock blend
            if (foot->lockBlend < targetLockBlend) {
                foot->lockBlend = std::min(foot->lockBlend + blendDelta, targetLockBlend);
            } else if (foot->lockBlend > targetLockBlend) {
                foot->lockBlend = std::max(foot->lockBlend - blendDelta, targetLockBlend);
            }

            // Explicitly clear lock state when blend reaches zero
            if (foot->lockBlend <= 0.0f) {
                foot->isLocked = false;
                foot->lockedWorldPosition = glm::vec3(0.0f);
            }

            // IK weight - faster blending for responsiveness
            float weightDelta = LOCK_BLEND_SPEED * 2.0f * deltaTime;
            if (foot->weight < targetWeight) {
                foot->weight = std::min(foot->weight + weightDelta, targetWeight);
            } else if (foot->weight > targetWeight) {
                foot->weight = std::max(foot->weight - weightDelta, targetWeight);
            }
        }
    };

    updateFootLock(leftFoot, true);
    updateFootLock(rightFoot, false);

    // Apply IK after animation sampling
    // Pass world transform so foot placement can query terrain in world space
    if (ikSystem.hasEnabledChains()) {
        ikSystem.solve(skeleton, worldTransform, deltaTime);
    }

    // GPU skinning: Bone matrices are computed and uploaded by Renderer each frame
    // No mesh re-upload needed - the vertex shader applies skinning
}

void AnimatedCharacter::computeBoneMatrices(std::vector<glm::mat4>& outBoneMatrices) const {
    // If animation update was skipped and we have cached matrices, use those
    if (skipAnimationUpdate_ && !cachedBoneMatrices_.empty()) {
        outBoneMatrices = cachedBoneMatrices_;
        return;
    }

    // First compute global transforms
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Then multiply by inverse bind matrices to get final bone matrices
    outBoneMatrices.resize(skeleton.joints.size());

    // Apply bone LOD: inactive bones use their parent's final matrix
    // This makes them rigidly follow the parent instead of animating independently
    const bool useBoneLOD = boneLODMasksBuilt_ && lodLevel_ > 0;
    const BoneLODMask* lodMask = useBoneLOD ? &boneLODMasks_[lodLevel_] : nullptr;

    // First pass: compute all active bones (parents should come before children in skeleton)
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        bool isActive = !lodMask || i >= MAX_LOD_BONES || lodMask->isBoneActive(static_cast<uint32_t>(i));
        if (isActive) {
            // Active bone: normal computation
            outBoneMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
        }
    }

    // Second pass: inactive bones copy their parent's final matrix
    // This ensures vertices weighted to this bone move with the parent
    if (lodMask) {
        for (size_t i = 0; i < skeleton.joints.size(); ++i) {
            if (i < MAX_LOD_BONES && !lodMask->isBoneActive(static_cast<uint32_t>(i))) {
                int32_t parentIdx = skeleton.joints[i].parentIndex;
                if (parentIdx >= 0 && parentIdx < static_cast<int32_t>(outBoneMatrices.size())) {
                    // Use parent's final bone matrix - this makes vertices follow the parent
                    outBoneMatrices[i] = outBoneMatrices[parentIdx];
                } else {
                    // No parent, use identity transform
                    outBoneMatrices[i] = glm::mat4(1.0f);
                }
            }
        }
    }

    // Cache the computed bone matrices for LOD animation skipping
    cachedBoneMatrices_ = outBoneMatrices;
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

    // Analyze walk animation for foot phase timing
    if (!leftFoot.empty() && !rightFoot.empty()) {
        // Find the walk animation clip for analysis
        const AnimationClip* walkClip = nullptr;
        for (const auto& clip : animations) {
            std::string lowerName = clip.name;
            for (char& c : lowerName) c = std::tolower(c);
            if (lowerName.find("walk") != std::string::npos) {
                walkClip = &clip;
                break;
            }
        }

        if (walkClip) {
            if (footPhaseTracker.analyzeAnimation(*walkClip, skeleton, leftFoot, rightFoot)) {
                SDL_Log("AnimatedCharacter: Foot phase analysis complete");
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                       "AnimatedCharacter: No walk animation found for foot phase analysis");
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

void AnimatedCharacter::setUseLayerController(bool use) {
    useLayerController = use;
    if (use) {
        useStateMachine = false;
        SDL_Log("AnimatedCharacter: Switched to layer controller mode");
    } else {
        SDL_Log("AnimatedCharacter: Switched to state machine mode");
    }
}

void AnimatedCharacter::setupLocomotionBlendSpace() {
    // Delegate to the state machine's blend space setup
    stateMachine.setupLocomotionBlendSpace();
}

void AnimatedCharacter::setUseBlendSpace(bool use) {
    stateMachine.setUseBlendSpace(use);
    if (use) {
        SDL_Log("AnimatedCharacter: Blend space mode enabled for smooth locomotion");
    } else {
        SDL_Log("AnimatedCharacter: Blend space mode disabled, using discrete state transitions");
    }
}

void AnimatedCharacter::setLODLevel(uint32_t level) {
    if (level >= CHARACTER_LOD_LEVELS) {
        level = CHARACTER_LOD_LEVELS - 1;
    }
    lodLevel_ = level;

    // Build bone LOD masks on first use
    if (!boneLODMasksBuilt_) {
        buildBoneLODMasks();
    }
}

void AnimatedCharacter::buildBoneLODMasks() {
    if (!loaded || skeleton.joints.empty()) {
        return;
    }

    size_t numBones = skeleton.joints.size();

    // Categorize each bone by name
    boneCategories_.resize(numBones);
    for (size_t i = 0; i < numBones; ++i) {
        boneCategories_[i] = categorizeBone(skeleton.joints[i].name);
    }

    // Build LOD masks - each LOD includes bones up to its category threshold
    for (uint32_t lod = 0; lod < CHARACTER_LOD_LEVELS; ++lod) {
        boneLODMasks_[lod].activeBones.reset();
        boneLODMasks_[lod].activeBoneCount = 0;

        for (size_t i = 0; i < numBones && i < MAX_LOD_BONES; ++i) {
            BoneCategory cat = boneCategories_[i];
            uint32_t minLOD = getMinLODForCategory(cat);

            // Bone is active if current LOD <= minLOD for its category
            if (lod <= minLOD) {
                boneLODMasks_[lod].activeBones.set(i);
                boneLODMasks_[lod].activeBoneCount++;
            }
        }
    }

    // Log bone LOD info with category breakdown
    SDL_Log("AnimatedCharacter: Built bone LOD masks for %zu bones", numBones);

    const char* categoryNames[] = {"Core", "Limb", "Extremity", "Finger", "Face", "Secondary"};
    uint32_t categoryCounts[6] = {0};
    for (size_t i = 0; i < numBones; ++i) {
        uint32_t cat = static_cast<uint32_t>(boneCategories_[i]);
        if (cat < 6) categoryCounts[cat]++;
    }

    SDL_Log("  Bone categories: Core=%u, Limb=%u, Extremity=%u, Finger=%u, Face=%u, Secondary=%u",
            categoryCounts[0], categoryCounts[1], categoryCounts[2],
            categoryCounts[3], categoryCounts[4], categoryCounts[5]);

    for (uint32_t lod = 0; lod < CHARACTER_LOD_LEVELS; ++lod) {
        SDL_Log("  LOD%u: %u active bones", lod, boneLODMasks_[lod].activeBoneCount);
    }

    boneLODMasksBuilt_ = true;
}

uint32_t AnimatedCharacter::getActiveBoneCount() const {
    if (!boneLODMasksBuilt_ || lodLevel_ >= CHARACTER_LOD_LEVELS) {
        return static_cast<uint32_t>(skeleton.joints.size());
    }
    return boneLODMasks_[lodLevel_].activeBoneCount;
}

const BoneLODMask& AnimatedCharacter::getBoneLODMask(uint32_t lod) const {
    static BoneLODMask defaultMask;
    if (lod >= CHARACTER_LOD_LEVELS) {
        return defaultMask;
    }
    return boneLODMasks_[lod];
}

// ========== Motion Matching Implementation ==========

void AnimatedCharacter::setUseMotionMatching(bool use) {
    useMotionMatching = use;
    if (use) {
        useStateMachine = false;
        useLayerController = false;
        SDL_Log("AnimatedCharacter: Switched to motion matching mode");
    } else {
        SDL_Log("AnimatedCharacter: Disabled motion matching mode");
    }
}

void AnimatedCharacter::initializeMotionMatching(const MotionMatching::ControllerConfig& config) {
    if (!loaded) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "AnimatedCharacter: Cannot initialize motion matching before loading");
        return;
    }

    // Initialize the controller
    motionMatchingController.initialize(config);
    motionMatchingController.setSkeleton(skeleton);

    // Add all animation clips to the database
    for (size_t i = 0; i < animations.size(); ++i) {
        const auto& clip = animations[i];

        // Determine if clip is looping based on name
        std::string lowerName = clip.name;
        for (char& c : lowerName) c = std::tolower(c);

        bool looping = (lowerName.find("idle") != std::string::npos ||
                       lowerName.find("walk") != std::string::npos ||
                       lowerName.find("run") != std::string::npos);

        // Add tags based on animation type
        std::vector<std::string> tags;
        if (lowerName.find("idle") != std::string::npos) {
            tags.push_back("idle");
            tags.push_back("locomotion");
        } else if (lowerName.find("walk") != std::string::npos) {
            tags.push_back("walk");
            tags.push_back("locomotion");
        } else if (lowerName.find("run") != std::string::npos) {
            tags.push_back("run");
            tags.push_back("locomotion");
        } else if (lowerName.find("jump") != std::string::npos) {
            tags.push_back("jump");
        }

        motionMatchingController.addClip(&clip, clip.name, looping, tags);
    }

    // Build the database
    MotionMatching::DatabaseBuildOptions buildOptions;
    buildOptions.defaultSampleRate = 30.0f;
    buildOptions.pruneStaticPoses = false;  // Keep idle poses

    motionMatchingController.buildDatabase(buildOptions);

    // Enable motion matching mode
    useMotionMatching = true;
    useStateMachine = false;
    useLayerController = false;

    SDL_Log("AnimatedCharacter: Motion matching initialized with %zu clips, %zu poses",
            animations.size(),
            motionMatchingController.getDatabase().getPoseCount());
}

void AnimatedCharacter::updateMotionMatching(const glm::vec3& position,
                                              const glm::vec3& facing,
                                              const glm::vec3& inputDirection,
                                              float inputMagnitude,
                                              float deltaTime) {
    if (!useMotionMatching || !motionMatchingController.isDatabaseBuilt()) {
        return;
    }

    // Update the motion matching controller
    motionMatchingController.update(position, facing, inputDirection, inputMagnitude, deltaTime);

    // Apply the result to our skeleton
    motionMatchingController.applyToSkeleton(skeleton);
}
