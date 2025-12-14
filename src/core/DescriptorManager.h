#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <optional>

// Declarative descriptor set management with automatic pool growth
// Replaces verbose manual descriptor set creation patterns

class DescriptorManager {
public:
    // Builder for creating descriptor set layouts with a declarative API
    class LayoutBuilder {
    public:
        explicit LayoutBuilder(VkDevice device);

        // Add binding at next available index
        LayoutBuilder& addUniformBuffer(VkShaderStageFlags stages, uint32_t count = 1);
        LayoutBuilder& addStorageBuffer(VkShaderStageFlags stages, uint32_t count = 1);
        LayoutBuilder& addCombinedImageSampler(VkShaderStageFlags stages, uint32_t count = 1);
        LayoutBuilder& addStorageImage(VkShaderStageFlags stages, uint32_t count = 1);

        // Add binding at a specific index
        LayoutBuilder& addBinding(uint32_t binding, VkDescriptorType type,
                                  VkShaderStageFlags stages, uint32_t count = 1);

        VkDescriptorSetLayout build();

    private:
        VkDevice device;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        uint32_t nextBinding = 0;
    };

    // Fluent writer for updating descriptor sets
    class SetWriter {
    public:
        SetWriter(VkDevice device, VkDescriptorSet set);

        SetWriter& writeBuffer(uint32_t binding, VkBuffer buffer,
                               VkDeviceSize offset, VkDeviceSize range,
                               VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        SetWriter& writeImage(uint32_t binding, VkImageView view, VkSampler sampler,
                              VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        SetWriter& writeStorageImage(uint32_t binding, VkImageView view,
                                     VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);

        // Write to a specific array element
        SetWriter& writeBufferArray(uint32_t binding, uint32_t arrayElement,
                                    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range,
                                    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        SetWriter& writeImageArray(uint32_t binding, uint32_t arrayElement,
                                   VkImageView view, VkSampler sampler,
                                   VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        void update();

    private:
        VkDevice device;
        VkDescriptorSet set;
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
    };

    // Pool that automatically grows when exhausted
    class Pool {
    public:
        Pool(VkDevice device, uint32_t initialSetsPerPool = 32);
        ~Pool();

        // Non-copyable but movable
        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;
        Pool(Pool&& other) noexcept;
        Pool& operator=(Pool&& other) noexcept;

        // Allocate descriptor sets (grows pool if needed)
        std::vector<VkDescriptorSet> allocate(VkDescriptorSetLayout layout, uint32_t count);

        // Allocate a single set
        VkDescriptorSet allocateSingle(VkDescriptorSetLayout layout);

        // Reset all pools (frees all allocated sets)
        void reset();

        // Destroy all pools
        void destroy();

        // Get statistics
        uint32_t getPoolCount() const { return static_cast<uint32_t>(pools.size()); }
        uint32_t getTotalAllocatedSets() const { return totalAllocatedSets; }

    private:
        VkDescriptorPool createPool();
        bool tryAllocate(VkDescriptorPool pool, VkDescriptorSetLayout layout,
                         uint32_t count, std::vector<VkDescriptorSet>& outSets);

        VkDevice device;
        std::vector<VkDescriptorPool> pools;
        uint32_t setsPerPool;
        uint32_t currentPoolIndex = 0;
        uint32_t totalAllocatedSets = 0;

        // Track descriptor type counts for pool sizing
        struct PoolSizes {
            uint32_t uniformBuffers = 16;
            uint32_t storageBuffers = 16;
            uint32_t combinedImageSamplers = 32;
            uint32_t storageImages = 8;
        } poolSizes;
    };

    // Helper: Create pipeline layout from descriptor set layouts
    static VkPipelineLayout createPipelineLayout(
        VkDevice device,
        const std::vector<VkDescriptorSetLayout>& setLayouts,
        const std::vector<VkPushConstantRange>& pushConstants = {});

    // Helper: Create pipeline layout from a single layout
    static VkPipelineLayout createPipelineLayout(
        VkDevice device,
        VkDescriptorSetLayout setLayout,
        const std::vector<VkPushConstantRange>& pushConstants = {});
};
