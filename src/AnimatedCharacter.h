#pragma once

#include "SkinnedMesh.h"
#include "Animation.h"
#include "AnimationStateMachine.h"
#include "GLTFLoader.h"
#include "Mesh.h"
#include "IKSolver.h"
#include <vk_mem_alloc.h>
#include <string>
#include <vector>
#include <memory>

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
    AnimatedCharacter() = default;
    ~AnimatedCharacter() = default;

    // Load character from glTF file
    bool load(const std::string& path, VmaAllocator allocator, VkDevice device,
              VkCommandPool commandPool, VkQueue queue);

    // Load additional animations from separate FBX files
    void loadAdditionalAnimations(const std::vector<std::string>& paths);

    void destroy(VmaAllocator allocator);

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

    // IK System access
    IKSystem& getIKSystem() { return ikSystem; }
    const IKSystem& getIKSystem() const { return ikSystem; }

    // Setup common IK chains (arms, legs) by searching for standard bone names
    void setupDefaultIKChains();

    // Get IK debug visualization data
    IKDebugData getIKDebugData() const { return ikSystem.getDebugData(skeleton); }

    // Get skeleton debug data for wireframe rendering
    // worldTransform: the character's world transform matrix
    SkeletonDebugData getSkeletonDebugData(const glm::mat4& worldTransform) const;

    bool isLoaded() const { return loaded; }

private:
    // Original skinned mesh data (bind pose)
    std::vector<SkinnedVertex> bindPoseVertices;
    std::vector<uint32_t> indices;

    // Skeleton and animations
    Skeleton skeleton;
    std::vector<glm::mat4> bindPoseLocalTransforms;  // Store original bind pose transforms
    std::vector<AnimationClip> animations;
    AnimationPlayer animationPlayer;
    AnimationStateMachine stateMachine;
    bool useStateMachine = false;  // Set true after state machine is initialized

    // IK system for procedural adjustments
    IKSystem ikSystem;

    // Materials loaded from FBX/glTF
    std::vector<MaterialInfo> materials;

    // GPU skinning: SkinnedMesh keeps original vertex data, bone matrices are updated each frame
    SkinnedMesh skinnedMesh;

    // Render mesh (for scene object bounds/transform tracking)
    std::vector<Vertex> meshVertices;  // Bind pose vertices for bounds calculation
    Mesh renderMesh;

    bool loaded = false;
    bool needsUpload = false;
};
