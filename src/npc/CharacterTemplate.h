#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <array>
#include <string>
#include <memory>

#include "NPCData.h"  // For NPCLODLevel
#include "animation/SkinnedMesh.h"
#include "animation/Animation.h"
#include "animation/CharacterLOD.h"
#include "Mesh.h"

// Forward declarations
class AnimatedCharacter;

// CharacterTemplate - Shared resources for a character type
// Multiple NPCs can reference the same template to reduce memory
struct CharacterTemplate {
    // Source path (for debugging/identification)
    std::string sourcePath;

    // Shared mesh data (uploaded once, used by all NPCs with this template)
    std::unique_ptr<SkinnedMesh> skinnedMesh;
    std::unique_ptr<Mesh> renderMesh;  // For bounds/scene object

    // Shared skeleton (bone hierarchy, bind poses)
    Skeleton skeleton;
    std::vector<glm::mat4> bindPoseLocalTransforms;

    // Shared animation clips
    std::vector<AnimationClip> animations;

    // Bone LOD masks (which bones are active at each LOD level)
    std::array<BoneLODMask, CHARACTER_LOD_LEVELS> boneLODMasks;
    std::vector<BoneCategory> boneCategories;

    // Check if template is valid
    bool isValid() const {
        return skinnedMesh && renderMesh && !skeleton.joints.empty();
    }

    // Get animation clip by name
    const AnimationClip* getAnimation(const std::string& name) const {
        for (const auto& clip : animations) {
            if (clip.name == name) {
                return &clip;
            }
        }
        return nullptr;
    }

    // Get animation clip by index
    const AnimationClip* getAnimation(size_t index) const {
        return index < animations.size() ? &animations[index] : nullptr;
    }

    // Get total bone count
    uint32_t getBoneCount() const {
        return static_cast<uint32_t>(skeleton.joints.size());
    }
};

// Factory for creating CharacterTemplates from AnimatedCharacter
// Extracts shared resources that can be reused across multiple NPCs
class CharacterTemplateFactory {
public:
    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue queue;
    };

    explicit CharacterTemplateFactory(const InitInfo& info);

    // Create a template from an existing AnimatedCharacter
    // The character is moved into the template (transfers ownership of resources)
    std::unique_ptr<CharacterTemplate> createFromCharacter(
        std::unique_ptr<AnimatedCharacter> character);

    // Load a template directly from file
    std::unique_ptr<CharacterTemplate> loadFromFile(
        const std::string& path,
        const std::vector<std::string>& additionalAnimations = {});

private:
    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue queue_;
};

// Per-NPC instance data when using templates
// Minimal state needed for animation playback and rendering
struct NPCInstance {
    uint32_t templateIndex = 0;      // Index into template array

    // Spatial data
    glm::vec3 position{0.0f};
    float yawDegrees = 0.0f;

    // Animation playback state
    size_t currentClipIndex = 0;
    float animationTime = 0.0f;
    float playbackSpeed = 1.0f;

    // Cached bone matrices (computed during update, reused for rendering)
    std::vector<glm::mat4> boneMatrices;

    // LOD state
    NPCLODLevel lodLevel = NPCLODLevel::Real;
    uint32_t framesSinceUpdate = 0;

    // Scene integration
    size_t renderableIndex = 0;
};
