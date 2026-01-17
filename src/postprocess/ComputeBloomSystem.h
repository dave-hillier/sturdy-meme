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
 * - No render pass begin/end overhead (was 11 render passes)
 * - Better cache utilization with compute dispatch
 * - All mip levels use storage images instead of framebuffers
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
    static constexpr uint32_t MAX_MIP_LEVELS = 6;

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
