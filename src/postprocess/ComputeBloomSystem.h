#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "DescriptorManager.h"
#include "InitContext.h"

/**
 * Compute-based bloom system for HDR rendering.
 *
 * This replaces the render-pass based BloomSystem with compute shaders,
 * eliminating 11 render pass transitions and providing better GPU utilization.
 *
 * Performance improvements:
 * - No render pass begin/end overhead (was 11 render passes -> 0)
 * - Better cache utilization with compute dispatch
 * - All mip levels use storage images instead of framebuffers
 * - Half-res first pass option (bloom starts at 1/4 resolution)
 * - Reduced mip levels (5 instead of 6, mip 5 is 1/1024 of screen)
 * - Optional async compute overlap with other GPU work
 */
class ComputeBloomSystem {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit ComputeBloomSystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        const vk::raii::Device* raiiDevice = nullptr;
        bool halfResFirstPass = true;   // Start bloom at half-res (recommended)
        bool useAsyncCompute = false;   // Use async compute queue if available
        VkQueue asyncComputeQueue = VK_NULL_HANDLE;
        uint32_t asyncComputeQueueFamily = 0;
    };

    static std::unique_ptr<ComputeBloomSystem> create(const InitInfo& info);
    static std::unique_ptr<ComputeBloomSystem> create(const InitContext& ctx);

    ~ComputeBloomSystem();

    ComputeBloomSystem(ComputeBloomSystem&&) = delete;
    ComputeBloomSystem& operator=(ComputeBloomSystem&&) = delete;
    ComputeBloomSystem(const ComputeBloomSystem&) = delete;
    ComputeBloomSystem& operator=(const ComputeBloomSystem&) = delete;

    void resize(VkExtent2D newExtent);

    /**
     * Record bloom compute passes.
     * @param cmd Command buffer (should be outside a render pass)
     * @param hdrImage HDR input image (must support STORAGE usage)
     * @param hdrView Image view for the HDR input
     */
    void recordBloomPass(VkCommandBuffer cmd, VkImage hdrImage, VkImageView hdrView);

    VkImageView getBloomOutput() const { return mipChain_.empty() ? VK_NULL_HANDLE : mipChain_[0].imageView; }
    VkSampler getBloomSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    void setThreshold(float t) { threshold_ = t; }
    float getThreshold() const { return threshold_; }
    void setIntensity(float i) { intensity_ = i; }
    float getIntensity() const { return intensity_; }

    // Async compute support
    bool isAsyncComputeEnabled() const { return useAsyncCompute_ && asyncComputeQueue_ != VK_NULL_HANDLE; }
    VkQueue getAsyncComputeQueue() const { return asyncComputeQueue_; }
    uint32_t getAsyncComputeQueueFamily() const { return asyncComputeQueueFamily_; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    struct MipLevel {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkExtent2D extent = {0, 0};
    };

    bool createMipChain();
    bool createSampler();
    bool createDescriptorSetLayouts();
    bool createPipelines();
    bool createDescriptorSets();
    void destroyMipChain();

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    VkExtent2D extent_ = {0, 0};
    std::string shaderPath_;
    const vk::raii::Device* raiiDevice_ = nullptr;

    static constexpr VkFormat BLOOM_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr uint32_t MAX_MIP_LEVELS = 5;  // Reduced from 6 - mip 4 is already 1/512 of screen

    bool halfResFirstPass_ = true;  // Start bloom extraction at half resolution
    bool useAsyncCompute_ = false;
    VkQueue asyncComputeQueue_ = VK_NULL_HANDLE;
    uint32_t asyncComputeQueueFamily_ = 0;

    std::vector<MipLevel> mipChain_;
    std::optional<vk::raii::Sampler> sampler_;

    // Downsample compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> downsampleDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> downsamplePipelineLayout_;
    std::optional<vk::raii::Pipeline> downsamplePipeline_;
    std::vector<VkDescriptorSet> downsampleDescSets_;

    // Upsample compute pipeline
    std::optional<vk::raii::DescriptorSetLayout> upsampleDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> upsamplePipelineLayout_;
    std::optional<vk::raii::Pipeline> upsamplePipeline_;
    std::vector<VkDescriptorSet> upsampleDescSets_;

    // HDR input descriptor (updated each frame)
    VkDescriptorSet hdrInputDescSet_ = VK_NULL_HANDLE;

    float threshold_ = 1.0f;
    float intensity_ = 1.0f;

    struct DownsamplePushConstants {
        float srcResolutionX;
        float srcResolutionY;
        float threshold;
        int isFirstPass;
    };

    struct UpsamplePushConstants {
        float srcResolutionX;
        float srcResolutionY;
        float filterRadius;
        float padding;
    };
};
