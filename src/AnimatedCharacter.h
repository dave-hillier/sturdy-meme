#pragma once

#include "SkinnedMesh.h"
#include "Animation.h"
#include "AnimationStateMachine.h"
#include "Mesh.h"
#include <vk_mem_alloc.h>
#include <string>
#include <vector>
#include <memory>

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
    void destroy(VmaAllocator allocator);

    // Animation control
    void playAnimation(const std::string& name);
    void playAnimation(size_t index);
    void setPlaybackSpeed(float speed);
    void setLooping(bool loop);

    // Update animation and re-skin mesh
    // deltaTime: time since last frame in seconds
    // movementSpeed: horizontal movement speed for animation state selection
    // isGrounded: whether the character is on the ground
    // isJumping: whether the character just started a jump
    void update(float deltaTime, VmaAllocator allocator, VkDevice device,
                VkCommandPool commandPool, VkQueue queue,
                float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false);

    // Get the skinned mesh for rendering (uses SkinnedVertex format for GPU skinning)
    SkinnedMesh& getSkinnedMesh() { return skinnedMesh; }
    const SkinnedMesh& getSkinnedMesh() const { return skinnedMesh; }

    // Legacy: Get render mesh for CPU skinning fallback
    Mesh& getMesh() { return renderMesh; }
    const Mesh& getMesh() const { return renderMesh; }

    // Check if GPU skinning is enabled
    bool isGPUSkinningEnabled() const { return useGPUSkinning; }

    // Get skeleton for external use
    const Skeleton& getSkeleton() const { return skeleton; }
    Skeleton& getSkeleton() { return skeleton; }

    // Animation info
    const std::vector<AnimationClip>& getAnimations() const { return animations; }
    size_t getAnimationCount() const { return animations.size(); }
    const AnimationClip* getCurrentAnimation() const;
    float getCurrentTime() const { return animationPlayer.getCurrentTime(); }
    float getCurrentDuration() const { return animationPlayer.getDuration(); }

    // Get bone matrices for GPU skinning
    void computeBoneMatrices(std::vector<glm::mat4>& outBoneMatrices) const;

    bool isLoaded() const { return loaded; }

private:
    // Compute skinned vertices (CPU skinning)
    void applySkinning();

    // Re-upload mesh to GPU
    void uploadMesh(VmaAllocator allocator, VkDevice device,
                    VkCommandPool commandPool, VkQueue queue);

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

    // GPU skinning: SkinnedMesh keeps original vertex data, bone matrices are updated each frame
    SkinnedMesh skinnedMesh;
    bool useGPUSkinning = true;  // Enable GPU skinning by default

    // CPU skinning fallback (deprecated, kept for compatibility)
    std::vector<Vertex> skinnedVertices;
    Mesh renderMesh;

    bool loaded = false;
    bool needsUpload = false;
};
