#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <optional>

#include "TreeOptions.h"
#include "ImpostorTypes.h"
#include "DescriptorManager.h"

class TreeImpostorAtlas {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };

    struct InitInfo {
        const vk::raii::Device* raiiDevice;  // vulkan-hpp RAII device
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        DescriptorManager::Pool* descriptorPool;
        std::string resourcePath;
        uint32_t maxArchetypes = 16;  // Maximum different tree types
    };

    static std::unique_ptr<TreeImpostorAtlas> create(const InitInfo& info);
    ~TreeImpostorAtlas();

    // Non-copyable, non-movable
    TreeImpostorAtlas(const TreeImpostorAtlas&) = delete;
    TreeImpostorAtlas& operator=(const TreeImpostorAtlas&) = delete;
    TreeImpostorAtlas(TreeImpostorAtlas&&) = delete;
    TreeImpostorAtlas& operator=(TreeImpostorAtlas&&) = delete;

    // Generate impostor atlas for a tree archetype
    // Returns archetype index, or -1 on failure
    int32_t generateArchetype(
        const std::string& name,
        const TreeOptions& options,
        const struct Mesh& branchMesh,
        const std::vector<struct LeafInstanceGPU>& leafInstances,
        VkImageView barkAlbedo,
        VkImageView barkNormal,
        VkImageView leafAlbedo,
        VkSampler sampler);

    // Get archetype by name
    const TreeImpostorArchetype* getArchetype(const std::string& name) const;
    const TreeImpostorArchetype* getArchetype(uint32_t index) const;

    // Get number of archetypes
    size_t getArchetypeCount() const { return archetypes_.size(); }

    // Get atlas array textures for binding (single array covers all archetypes)
    VkImageView getAlbedoAtlasArrayView() const { return octaAlbedoArrayView_ ? **octaAlbedoArrayView_ : VK_NULL_HANDLE; }
    VkImageView getNormalAtlasArrayView() const { return octaNormalArrayView_ ? **octaNormalArrayView_ : VK_NULL_HANDLE; }
    VkSampler getAtlasSampler() const { return atlasSampler_ ? **atlasSampler_ : VK_NULL_HANDLE; }

    // LOD settings
    TreeLODSettings& getLODSettings() { return lodSettings_; }
    const TreeLODSettings& getLODSettings() const { return lodSettings_; }

    // Get atlas image for UI preview (lazy-initializes ImGui descriptor on first call)
    VkDescriptorSet getPreviewDescriptorSet(uint32_t archetypeIndex);
    VkDescriptorSet getNormalPreviewDescriptorSet(uint32_t archetypeIndex);

    explicit TreeImpostorAtlas(ConstructToken) {}

private:
    bool initInternal(const InitInfo& info);

    // Pipeline and resource creation (implemented in TreeImpostorAtlasCapture.cpp)
    bool createRenderPass();
    bool createCapturePipeline();
    bool createLeafCapturePipeline();
    bool createLeafQuadMesh();

    // Atlas resource management
    bool createAtlasArrayTextures();
    bool createAtlasResources(uint32_t archetypeIndex);
    bool createSampler();

    // Octahedral rendering (implemented in TreeImpostorAtlasCapture.cpp)
    void renderOctahedralCell(
        VkCommandBuffer cmd,
        int cellX, int cellY,
        glm::vec3 viewDirection,
        const struct Mesh& branchMesh,
        const std::vector<struct LeafInstanceGPU>& leafInstances,
        float horizontalRadius,
        float boundingSphereRadius,
        float halfHeight,
        float centerHeight,
        float baseY,
        VkDescriptorSet branchDescSet,
        VkDescriptorSet leafDescSet);

    const vk::raii::Device* raiiDevice_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;

    // Render pass for capturing impostors (renders to G-buffer)
    std::optional<vk::raii::RenderPass> captureRenderPass_;

    // Pipeline for capturing tree geometry to G-buffer
    std::optional<vk::raii::Pipeline> branchCapturePipeline_;
    std::optional<vk::raii::Pipeline> leafCapturePipeline_;
    std::optional<vk::raii::PipelineLayout> capturePipelineLayout_;
    std::optional<vk::raii::PipelineLayout> leafCapturePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> captureDescriptorSetLayout_;
    std::optional<vk::raii::DescriptorSetLayout> leafCaptureDescriptorSetLayout_;

    // Leaf quad mesh for capture
    VkBuffer leafQuadVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadVertexAllocation_ = VK_NULL_HANDLE;
    VkBuffer leafQuadIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadIndexAllocation_ = VK_NULL_HANDLE;
    uint32_t leafQuadIndexCount_ = 0;

    // Archetype data
    std::vector<TreeImpostorArchetype> archetypes_;

    // Texture array for all archetypes (shared across all archetypes)
    VkImage octaAlbedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaAlbedoArrayAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> octaAlbedoArrayView_;

    VkImage octaNormalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaNormalArrayAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> octaNormalArrayView_;

    uint32_t maxArchetypes_ = 16;  // Maximum layers in the array

    // Shared sampler for atlas textures
    std::optional<vk::raii::Sampler> atlasSampler_;

    // Capture descriptor sets (reused)
    std::vector<VkDescriptorSet> captureDescriptorSets_;

    // LOD settings
    TreeLODSettings lodSettings_;

    // Leaf instance buffer for capture (temporary)
    VkBuffer leafCaptureBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafCaptureAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize leafCaptureBufferSize_ = 0;

    // Per-archetype atlas (depth buffers and framebuffers)
    struct AtlasTextures {
        VkImage depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        std::optional<vk::raii::ImageView> albedoView;   // View into octaAlbedoArrayImage_
        std::optional<vk::raii::ImageView> normalView;   // View into octaNormalArrayImage_
        std::optional<vk::raii::ImageView> depthView;
        std::optional<vk::raii::Framebuffer> framebuffer;
        VkDescriptorSet previewDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet normalPreviewDescriptorSet = VK_NULL_HANDLE;
    };
    std::vector<AtlasTextures> atlasTextures_;
};
