#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <memory>
#include "DescriptorManager.h"
#include "InitContext.h"
#include "core/VulkanRAII.h"

class BloomSystem {
public:
    // Legacy InitInfo - kept for backward compatibility during migration
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
    };

    /**
     * Factory: Create and initialize bloom system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<BloomSystem> create(const InitInfo& info);
    static std::unique_ptr<BloomSystem> create(const InitContext& ctx);

    ~BloomSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    BloomSystem(BloomSystem&&) = delete;
    BloomSystem& operator=(BloomSystem&&) = delete;
    BloomSystem(const BloomSystem&) = delete;
    BloomSystem& operator=(const BloomSystem&) = delete;

    void resize(VkExtent2D newExtent);

    void recordBloomPass(VkCommandBuffer cmd, VkImageView hdrInput);

    VkImageView getBloomOutput() const { return mipChain.empty() ? VK_NULL_HANDLE : mipChain[0].imageView; }
    VkSampler getBloomSampler() const { return sampler_.get(); }

    void setThreshold(float t) { threshold = t; }
    float getThreshold() const { return threshold; }
    void setIntensity(float i) { intensity = i; }
    float getIntensity() const { return intensity; }

private:
    BloomSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    struct MipLevel {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D extent = {0, 0};
    };

    bool createMipChain();
    bool createRenderPass();
    bool createSampler();
    bool createDescriptorSetLayouts();
    bool createPipelines();
    bool createDescriptorSets();

    void destroyMipChain();

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;

    static constexpr VkFormat BLOOM_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr uint32_t MAX_MIP_LEVELS = 6;

    std::vector<MipLevel> mipChain;

    ManagedRenderPass downsampleRenderPass_;
    ManagedRenderPass upsampleRenderPass_;
    ManagedSampler sampler_;

    // Downsample pipeline
    ManagedDescriptorSetLayout downsampleDescSetLayout_;
    ManagedPipelineLayout downsamplePipelineLayout_;
    ManagedPipeline downsamplePipeline_;
    std::vector<VkDescriptorSet> downsampleDescSets;

    // Upsample pipeline
    ManagedDescriptorSetLayout upsampleDescSetLayout_;
    ManagedPipelineLayout upsamplePipelineLayout_;
    ManagedPipeline upsamplePipeline_;
    std::vector<VkDescriptorSet> upsampleDescSets;

    // Parameters
    float threshold = 1.0f;
    float intensity = 1.0f;

    // Push constants for downsample
    struct DownsamplePushConstants {
        float resolutionX;
        float resolutionY;
        float threshold;
        int isFirstPass;
    };

    // Push constants for upsample
    struct UpsamplePushConstants {
        float resolutionX;
        float resolutionY;
        float filterRadius;
        float padding;
    };
};
