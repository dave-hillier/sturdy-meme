#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <string>

// Atmosphere LUT system for precomputed atmospheric scattering
// Generates transmittance and multi-scatter lookup tables

class AtmosphereLUTSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        std::string shaderPath;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
    };

    // LUT dimensions (from Phase 4 docs)
    static constexpr uint32_t TRANSMITTANCE_WIDTH = 256;
    static constexpr uint32_t TRANSMITTANCE_HEIGHT = 64;
    static constexpr uint32_t MULTISCATTER_SIZE = 32;

    AtmosphereLUTSystem() = default;
    ~AtmosphereLUTSystem() = default;

    bool init(const InitInfo& info);
    void destroy();

    // Generate LUTs (call once at startup or when atmosphere parameters change)
    bool generateLUTs();

    // Save LUTs to disk for visualization (PPM format)
    bool saveLUTsToDisk(const std::string& outputDir);

    // Get the LUT textures for sky shader binding
    VkImageView getTransmittanceLUTView() const { return transmittanceLUTView; }
    VkImageView getMultiScatterLUTView() const { return multiScatterLUTView; }
    VkSampler getLUTSampler() const { return lutSampler; }

private:
    bool createTransmittanceLUT();
    bool createMultiScatterLUT();
    bool createSampler();
    bool createComputePipelines();

    bool createTransmittancePipeline();
    bool createMultiScatterPipeline();

    // Helper to run a compute shader once
    bool dispatchCompute(VkPipeline pipeline, VkDescriptorSet descriptorSet,
                        uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ = 1);

    // Helper to read back image data for saving
    bool readImageToBuffer(VkImage image, uint32_t width, uint32_t height, std::vector<float>& outData);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::string shaderPath;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;

    // Transmittance LUT (256x64, RGBA16F)
    VkImage transmittanceLUT = VK_NULL_HANDLE;
    VmaAllocation transmittanceLUTAllocation = VK_NULL_HANDLE;
    VkImageView transmittanceLUTView = VK_NULL_HANDLE;

    // Multi-scatter LUT (32x32, RGBA16F)
    VkImage multiScatterLUT = VK_NULL_HANDLE;
    VmaAllocation multiScatterLUTAllocation = VK_NULL_HANDLE;
    VkImageView multiScatterLUTView = VK_NULL_HANDLE;

    // Sampler for LUT access
    VkSampler lutSampler = VK_NULL_HANDLE;

    // Compute pipelines
    VkDescriptorSetLayout transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout transmittancePipelineLayout = VK_NULL_HANDLE;
    VkPipeline transmittancePipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout multiScatterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline multiScatterPipeline = VK_NULL_HANDLE;

    // Descriptor sets for compute dispatches
    VkDescriptorSet transmittanceDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet multiScatterDescriptorSet = VK_NULL_HANDLE;
};
