#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>

class BloomSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
    };

    BloomSystem() = default;
    ~BloomSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);
    void resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent);

    void recordBloomPass(VkCommandBuffer cmd, VkImageView hdrInput);

    VkImageView getBloomOutput() const { return mipChain.empty() ? VK_NULL_HANDLE : mipChain[0].imageView; }
    VkSampler getBloomSampler() const { return sampler; }

    void setThreshold(float t) { threshold = t; }
    float getThreshold() const { return threshold; }
    void setIntensity(float i) { intensity = i; }
    float getIntensity() const { return intensity; }

private:
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
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;

    static constexpr VkFormat BLOOM_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr uint32_t MAX_MIP_LEVELS = 6;

    std::vector<MipLevel> mipChain;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    // Downsample pipeline
    VkDescriptorSetLayout downsampleDescSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout downsamplePipelineLayout = VK_NULL_HANDLE;
    VkPipeline downsamplePipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> downsampleDescSets;

    // Upsample pipeline
    VkDescriptorSetLayout upsampleDescSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout upsamplePipelineLayout = VK_NULL_HANDLE;
    VkPipeline upsamplePipeline = VK_NULL_HANDLE;
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
