#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

#include "TreeSystem.h"
#include "core/VulkanRAII.h"
#include "core/DescriptorManager.h"
#include "BufferUtils.h"

// Push constants for tree rendering
struct TreeBranchPushConstants {
    glm::mat4 model;      // offset 0, size 64
    float time;           // offset 64, size 4
    float _pad1[3];       // offset 68, size 12 (padding to align vec3 to 16 bytes)
    glm::vec3 barkTint;   // offset 80, size 12
    float roughnessScale; // offset 92, size 4
};

struct TreeLeafPushConstants {
    glm::mat4 model;    // offset 0, size 64
    float time;         // offset 64, size 4
    float _pad1[3];     // offset 68, size 12 (padding to align vec3 to 16 bytes)
    glm::vec3 leafTint; // offset 80, size 12
    float alphaTest;    // offset 92, size 4
};

class TreeRenderer {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass hdrRenderPass;
        VkRenderPass shadowRenderPass;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string resourcePath;
        uint32_t maxFramesInFlight;
    };

    static std::unique_ptr<TreeRenderer> create(const InitInfo& info);
    ~TreeRenderer();

    // Non-copyable, non-movable
    TreeRenderer(const TreeRenderer&) = delete;
    TreeRenderer& operator=(const TreeRenderer&) = delete;
    TreeRenderer(TreeRenderer&&) = delete;
    TreeRenderer& operator=(TreeRenderer&&) = delete;

    // Update descriptor sets for a specific texture type
    void updateBarkDescriptorSet(
        uint32_t frameIndex,
        const std::string& barkType,
        VkBuffer uniformBuffer,
        VkBuffer windBuffer,
        VkImageView shadowMapView,
        VkSampler shadowSampler,
        VkImageView barkAlbedo,
        VkImageView barkNormal,
        VkImageView barkRoughness,
        VkImageView barkAO,
        VkSampler barkSampler);

    void updateLeafDescriptorSet(
        uint32_t frameIndex,
        const std::string& leafType,
        VkBuffer uniformBuffer,
        VkBuffer windBuffer,
        VkImageView shadowMapView,
        VkSampler shadowSampler,
        VkImageView leafAlbedo,
        VkSampler leafSampler);

    // Get descriptor set for a specific type (returns default if type not found)
    VkDescriptorSet getBranchDescriptorSet(uint32_t frameIndex, const std::string& barkType) const;
    VkDescriptorSet getLeafDescriptorSet(uint32_t frameIndex, const std::string& leafType) const;

    // Render all trees
    void render(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                const TreeSystem& treeSystem);

    // Render tree shadows
    void renderShadows(VkCommandBuffer cmd, uint32_t frameIndex,
                       const TreeSystem& treeSystem, int cascadeIndex);

    // Update extent on resize
    void setExtent(VkExtent2D newExtent);

    VkDevice getDevice() const { return device_; }

private:
    TreeRenderer() = default;
    bool initInternal(const InitInfo& info);
    bool createPipelines(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets(uint32_t maxFramesInFlight);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    VkExtent2D extent_{};

    // Pipelines
    ManagedPipeline branchPipeline_;
    ManagedPipeline leafPipeline_;
    ManagedPipeline branchShadowPipeline_;
    ManagedPipeline leafShadowPipeline_;

    // Pipeline layouts
    ManagedPipelineLayout branchPipelineLayout_;
    ManagedPipelineLayout leafPipelineLayout_;
    ManagedPipelineLayout shadowPipelineLayout_;

    // Descriptor set layouts
    ManagedDescriptorSetLayout branchDescriptorSetLayout_;
    ManagedDescriptorSetLayout leafDescriptorSetLayout_;

    // Per-frame descriptor sets (indexed by frame, then by texture type)
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> branchDescriptorSets_;
    std::vector<std::unordered_map<std::string, VkDescriptorSet>> leafDescriptorSets_;

    // Default descriptor sets for types without registered textures
    std::vector<VkDescriptorSet> defaultBranchDescriptorSets_;
    std::vector<VkDescriptorSet> defaultLeafDescriptorSets_;
};
