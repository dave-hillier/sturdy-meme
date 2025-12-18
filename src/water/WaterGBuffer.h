#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

#include "DescriptorManager.h"
#include "core/VulkanRAII.h"

/**
 * WaterGBuffer - Phase 3: Screen-Space Mini G-Buffer
 *
 * Stores per-pixel water data for deferred water compositing:
 * - Data texture: shader ID, material index, LOD level, foam amount
 * - Mesh normal texture: low-res mesh normals
 * - Water-only depth buffer (separate from scene depth)
 *
 * Based on Far Cry 5's water rendering approach (GDC 2018).
 */
class WaterGBuffer {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkExtent2D fullResExtent;       // Full screen resolution
        float resolutionScale = 0.5f;   // G-buffer resolution relative to full res
        uint32_t framesInFlight;
        std::string shaderPath;         // Path to shader SPV files
        DescriptorManager::Pool* descriptorPool; // For allocating descriptor sets
    };

    // G-buffer data packed into textures
    // Data texture (RGBA8):
    //   R: Shader/material ID (0-255)
    //   G: LOD level (0-255, maps to 0.0-1.0)
    //   B: Foam amount (0-255, maps to 0.0-1.0)
    //   A: Reserved (blend material ID, etc.)
    //
    // Normal texture (RGBA16F):
    //   RGB: Mesh normal (world space)
    //   A: Water depth (for refraction)
    //
    // Depth texture (D32F):
    //   Water-only depth for proper compositing

    /**
     * Factory: Create and initialize WaterGBuffer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<WaterGBuffer> create(const InitInfo& info);

    ~WaterGBuffer();

    // Non-copyable, non-movable
    WaterGBuffer(const WaterGBuffer&) = delete;
    WaterGBuffer& operator=(const WaterGBuffer&) = delete;
    WaterGBuffer(WaterGBuffer&&) = delete;
    WaterGBuffer& operator=(WaterGBuffer&&) = delete;

    // Resize G-buffer when window changes
    void resize(VkExtent2D newFullResExtent);

    // Get render pass for position pass
    VkRenderPass getRenderPass() const { return renderPass.get(); }
    VkFramebuffer getFramebuffer() const { return framebuffer.get(); }
    VkExtent2D getExtent() const { return gbufferExtent; }

    // Get G-buffer textures for sampling in composite pass
    VkImageView getDataImageView() const { return dataImageView; }
    VkImageView getNormalImageView() const { return normalImageView; }
    VkImageView getDepthImageView() const { return depthImageView; }
    VkSampler getSampler() const { return sampler.get(); }

    // Get pipeline resources
    VkPipeline getPipeline() const { return pipeline.get(); }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout.get(); }
    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

    // Create descriptor sets after resources are available
    bool createDescriptorSets(
        const std::vector<VkBuffer>& mainUBOs,
        VkDeviceSize mainUBOSize,
        const std::vector<VkBuffer>& waterUBOs,
        VkDeviceSize waterUBOSize,
        VkImageView terrainHeightView, VkSampler terrainSampler,
        VkImageView flowMapView, VkSampler flowMapSampler);

    // Begin/end G-buffer rendering
    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);

    // Clear G-buffer (call at start of frame)
    void clear(VkCommandBuffer cmd);

private:
    WaterGBuffer() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createImages();
    bool createRenderPass();
    bool createFramebuffer();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createPipelineLayout();
    bool createPipeline();
    void destroyImages();

    // Device handles
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Resolution
    VkExtent2D fullResExtent = {0, 0};
    VkExtent2D gbufferExtent = {0, 0};
    float resolutionScale = 0.5f;

    // G-buffer images
    VkImage dataImage = VK_NULL_HANDLE;
    VkImageView dataImageView = VK_NULL_HANDLE;
    VmaAllocation dataAllocation = VK_NULL_HANDLE;

    VkImage normalImage = VK_NULL_HANDLE;
    VkImageView normalImageView = VK_NULL_HANDLE;
    VmaAllocation normalAllocation = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VmaAllocation depthAllocation = VK_NULL_HANDLE;

    // Render pass and framebuffer (RAII-managed)
    ManagedRenderPass renderPass;
    ManagedFramebuffer framebuffer;

    // Sampler for reading G-buffer in composite pass (RAII-managed)
    ManagedSampler sampler;

    // Graphics pipeline for position pass (RAII-managed)
    ManagedPipeline pipeline;
    ManagedPipelineLayout pipelineLayout;
    ManagedDescriptorSetLayout descriptorSetLayout;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::vector<VkDescriptorSet> descriptorSets;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
};
