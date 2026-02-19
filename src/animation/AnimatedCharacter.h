#pragma once

#include "SkinnedMesh.h"
#include "Animation.h"
#include "AnimationStateMachine.h"
#include "AnimationLayerController.h"
#include "BlendSpace.h"
#include "FootPhaseTracker.h"
#include "CharacterLOD.h"
#include "GLTFLoader.h"
#include "Mesh.h"
#include "IKSolver.h"
#include "MotionMatchingController.h"
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <filesystem>

// Debug data for skeleton visualization
struct SkeletonDebugData {
    struct Bone {
        glm::vec3 startPos;   // Parent joint position
        glm::vec3 endPos;     // This joint's position
        std::string name;
        int32_t parentIndex;
        bool isEndEffector;   // True if this is a leaf bone (hand, foot, head tip)
    };
    std::vector<Bone> bones;
    std::vector<glm::vec3> jointPositions;  // All joint world positions
};

// High-level animated character class
// Combines: skinned mesh, skeleton, animations, and animation player
// Uses GPU skinning for performance (bone matrices uploaded to UBO each frame)
class AnimatedCharacter {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit AnimatedCharacter(ConstructToken) {}

    struct InitInfo {
        std::string path;
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue queue;
    };

    /**
     * Factory: Create and load AnimatedCharacter from file.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<AnimatedCharacter> create(const InitInfo& info);


    ~AnimatedCharacter();

    // Non-copyable, non-movable
    AnimatedCharacter(const AnimatedCharacter&) = delete;
    AnimatedCharacter& operator=(const AnimatedCharacter&) = delete;
    AnimatedCharacter(AnimatedCharacter&&) = delete;
    AnimatedCharacter& operator=(AnimatedCharacter&&) = delete;

    // Load additional animations from separate FBX files
    void loadAdditionalAnimations(const std::vector<std::string>& paths);

    // Animation control
    void playAnimation(const std::string& name);
    void playAnimation(size_t index);
    void setPlaybackSpeed(float speed);
    void setLooping(bool loop);

    // Start a jump with trajectory prediction for animation sync
    void startJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const class PhysicsWorld* physics);

    // Update animation and re-skin mesh
    // deltaTime: time since last frame in seconds
    // movementSpeed: horizontal movement speed for animation state selection
    // isGrounded: whether the character is on the ground
    // isJumping: whether the character just started a jump
    // worldTransform: character's world transform matrix (for IK ground queries)
    void update(float deltaTime, VmaAllocator allocator, VkDevice device,
                VkCommandPool commandPool, VkQueue queue,
                float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false,
                const glm::mat4& worldTransform = glm::mat4(1.0f));

    // Get the skinned mesh for rendering (uses SkinnedVertex format for GPU skinning)
    SkinnedMesh& getSkinnedMesh() { return skinnedMesh; }
    const SkinnedMesh& getSkinnedMesh() const { return skinnedMesh; }

    // Get render mesh (for scene object bounds/transform tracking)
    Mesh& getMesh() { return renderMesh; }

    // Get skeleton for external use
    const Skeleton& getSkeleton() const { return skeleton; }
    Skeleton& getSkeleton() { return skeleton; }

    // Animation info
    const std::vector<AnimationClip>& getAnimations() const { return animations; }
    size_t getAnimationCount() const { return animations.size(); }
    const AnimationClip* getCurrentAnimation() const;
    float getCurrentTime() const { return animationPlayer.getCurrentTime(); }
    float getCurrentDuration() const { return animationPlayer.getDuration(); }

    // Material info
    const std::vector<MaterialInfo>& getMaterials() const { return materials; }
    bool hasMaterials() const { return !materials.empty(); }

    // Get bone matrices for GPU skinning
    void computeBoneMatrices(std::vector<glm::mat4>& outBoneMatrices) const;

    // Override bone matrices for this frame (e.g. from combat ragdoll blending).
    // The override is consumed on the next computeBoneMatrices call.
    void setBoneMatrixOverride(const std::vector<glm::mat4>& matrices);
    void clearBoneMatrixOverride();

    // LOD support - animation update control
    // When skipAnimation is true, reuses cached bone matrices from last frame
    void setSkipAnimationUpdate(bool skip) { skipAnimationUpdate_ = skip; }
    bool isAnimationUpdateSkipped() const { return skipAnimationUpdate_; }

    // Get cached bone matrices (used when animation update is skipped)
    const std::vector<glm::mat4>& getCachedBoneMatrices() const { return cachedBoneMatrices_; }
    bool hasCachedBoneMatrices() const { return !cachedBoneMatrices_.empty(); }

    // Get current LOD level (for external use)
    uint32_t getLODLevel() const { return lodLevel_; }
    void setLODLevel(uint32_t level);

    // Bone LOD - builds masks for which bones are active at each LOD
    void buildBoneLODMasks();
    uint32_t getActiveBoneCount() const;  // Returns active bones at current LOD
    uint32_t getTotalBoneCount() const { return static_cast<uint32_t>(skeleton.joints.size()); }
    const BoneLODMask& getBoneLODMask(uint32_t lod) const;
    const std::vector<BoneCategory>& getBoneCategories() const { return boneCategories_; }

    // IK System access
    IKSystem& getIKSystem() { return ikSystem; }
    const IKSystem& getIKSystem() const { return ikSystem; }

    // Foot Phase Tracker access (for phase-aware IK)
    FootPhaseTracker& getFootPhaseTracker() { return footPhaseTracker; }
    const FootPhaseTracker& getFootPhaseTracker() const { return footPhaseTracker; }

    // Check if foot phase tracking is enabled
    bool hasFootPhaseTracking() const { return useFootPhaseTracking; }
    void setUseFootPhaseTracking(bool use) { useFootPhaseTracking = use; }

    // Animation Layer Controller access (advanced layer-based blending)
    AnimationLayerController& getLayerController() { return layerController; }
    const AnimationLayerController& getLayerController() const { return layerController; }

    // Enable layer controller mode (disables state machine)
    void setUseLayerController(bool use);
    bool isUsingLayerController() const { return useLayerController; }

    // BlendSpace access for locomotion blending (from state machine)
    BlendSpace1D& getLocomotionBlendSpace() { return stateMachine.getLocomotionBlendSpace(); }
    const BlendSpace1D& getLocomotionBlendSpace() const { return stateMachine.getLocomotionBlendSpace(); }

    // Enable blend space mode for smooth locomotion transitions
    // When enabled, idle/walk/run blend smoothly based on speed instead of discrete state changes
    void setUseBlendSpace(bool use);
    bool isUsingBlendSpace() const { return stateMachine.isUsingBlendSpace(); }

    // Setup locomotion blend space from available animations
    // Call this after loading to configure the blend space with idle/walk/run clips
    void setupLocomotionBlendSpace();

    // ========== Motion Matching Mode ==========
    // When enabled, uses motion matching instead of state machine for animation selection

    // Enable motion matching mode
    void setUseMotionMatching(bool use);
    bool isUsingMotionMatching() const { return useMotionMatching; }

    // Initialize motion matching with configuration
    void initializeMotionMatching(const MotionMatching::ControllerConfig& config = {});

    // Update motion matching with player input
    // position: current character world position
    // facing: current character facing direction
    // inputDirection: desired movement direction from input
    // inputMagnitude: 0-1 how much movement is desired
    void updateMotionMatching(const glm::vec3& position,
                               const glm::vec3& facing,
                               const glm::vec3& inputDirection,
                               float inputMagnitude,
                               float deltaTime);

    // Get motion matching controller for advanced configuration
    MotionMatching::MotionMatchingController& getMotionMatchingController() { return motionMatchingController; }
    const MotionMatching::MotionMatchingController& getMotionMatchingController() const { return motionMatchingController; }

    // Get motion matching statistics for debugging
    const MotionMatching::MotionMatchingStats& getMotionMatchingStats() const {
        return motionMatchingController.getStats();
    }

    // Reset foot IK locks (call when teleporting or significantly repositioning the character)
    void resetFootLocks() { ikSystem.resetFootLocks(); }

    // Setup common IK chains (arms, legs) by searching for standard bone names
    void setupDefaultIKChains();

    // Get IK debug visualization data
    IKDebugData getIKDebugData() const { return ikSystem.getDebugData(skeleton); }

    // Get skeleton debug data for wireframe rendering
    // worldTransform: the character's world transform matrix
    SkeletonDebugData getSkeletonDebugData(const glm::mat4& worldTransform) const;

    bool isLoaded() const { return loaded; }

private:
    bool loadInternal(const InitInfo& info);
    void cleanup();

    // Stored for RAII cleanup
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    // Original skinned mesh data (bind pose)
    std::vector<SkinnedVertex> bindPoseVertices;
    std::vector<uint32_t> indices;

    // Skeleton and animations
    Skeleton skeleton;
    std::vector<glm::mat4> bindPoseLocalTransforms;  // Store original bind pose transforms
    std::vector<AnimationClip> animations;
    AnimationPlayer animationPlayer;
    AnimationStateMachine stateMachine;
    AnimationLayerController layerController;
    MotionMatching::MotionMatchingController motionMatchingController;
    bool useStateMachine = false;  // Set true after state machine is initialized
    bool useLayerController = false;  // Set true to use layer controller instead
    bool useMotionMatching = false;  // Set true to use motion matching
    size_t currentAnimationIndex = 0;  // Track current animation by index, not duration

    // IK system for procedural adjustments
    IKSystem ikSystem;

    // Foot phase tracking for phase-aware IK
    FootPhaseTracker footPhaseTracker;
    bool useFootPhaseTracking = true;  // Enable by default

    // Materials loaded from FBX/glTF
    std::vector<MaterialInfo> materials;

    // GPU skinning: SkinnedMesh keeps original vertex data, bone matrices are updated each frame
    SkinnedMesh skinnedMesh;

    // Render mesh (for scene object bounds/transform tracking)
    std::vector<Vertex> meshVertices;  // Bind pose vertices for bounds calculation
    Mesh renderMesh;

    bool loaded = false;
    bool needsUpload = false;
    std::filesystem::path modelPath_;  // Stored for deriving cache paths

    // LOD support
    bool skipAnimationUpdate_ = false;
    uint32_t lodLevel_ = 0;
    mutable std::vector<glm::mat4> cachedBoneMatrices_;

    // Combat/ragdoll bone matrix override
    mutable std::vector<glm::mat4> boneMatrixOverride_;
    mutable bool hasBoneMatrixOverride_ = false;

    // Bone LOD support
    std::vector<BoneCategory> boneCategories_;  // Category for each bone
    std::array<BoneLODMask, CHARACTER_LOD_LEVELS> boneLODMasks_;  // Which bones active at each LOD
    bool boneLODMasksBuilt_ = false;

    // Upper body strafe twist
    int32_t spineJointIndex_ = -1;   // Cached spine bone index for strafe twist
    bool spineLookedUp_ = false;     // Whether we've attempted the lookup
};
