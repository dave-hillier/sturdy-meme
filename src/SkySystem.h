#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>

class AtmosphereLUTSystem;

class SkySystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkExtent2D extent;
        VkRenderPass hdrRenderPass;
    };

    SkySystem() = default;
    ~SkySystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Create descriptor sets after uniform buffers and LUTs are ready
    bool createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                              VkDeviceSize uniformBufferSize,
                              AtmosphereLUTSystem& atmosphereLUTSystem);

    // Record sky rendering commands
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

private:
    bool createDescriptorSetLayout();
    bool createPipeline();

    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkExtent2D extent = {0, 0};
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
};
