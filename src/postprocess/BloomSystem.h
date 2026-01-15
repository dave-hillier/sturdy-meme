#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "DescriptorManager.h"
#include "InitContext.h"

class BloomSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };

    // Legacy InitInfo - kept for backward compatibility during migration
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
        const vk::raii::Device* raiiDevice = nullptr;
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
    VkSampler getBloomSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    void setThreshold(float t) { threshold = t; }
    float getThreshold() const { return threshold; }
    void setIntensity(float i) { intensity = i; }
    float getIntensity() const { return intensity; }

    explicit BloomSystem(ConstructToken) {}

private:
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
    const vk::raii::Device* raiiDevice_ = nullptr;

    static constexpr VkFormat BLOOM_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr uint32_t MAX_MIP_LEVELS = 6;

    std::vector<MipLevel> mipChain;

    std::optional<vk::raii::RenderPass> downsampleRenderPass_;
    std::optional<vk::raii::RenderPass> upsampleRenderPass_;
    std::optional<vk::raii::Sampler> sampler_;

    // Downsample pipeline
    std::optional<vk::raii::DescriptorSetLayout> downsampleDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> downsamplePipelineLayout_;
    std::optional<vk::raii::Pipeline> downsamplePipeline_;
    std::vector<VkDescriptorSet> downsampleDescSets;

    // Upsample pipeline
    std::optional<vk::raii::DescriptorSetLayout> upsampleDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> upsamplePipelineLayout_;
    std::optional<vk::raii::Pipeline> upsamplePipeline_;
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
