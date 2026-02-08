#pragma once

#include "material/DescriptorManager.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <string>
#include <functional>

class VulkanContext;
class PostProcessSystem;
class IDescriptorAllocator;

/**
 * DescriptorInfrastructure - Owns descriptor layouts, pools, and graphics pipeline
 *
 * Extracted from Renderer to reduce coupling. Groups:
 * - Main descriptor set layout (for scene rendering)
 * - Pipeline layout (wraps descriptor layout + push constants)
 * - Main graphics pipeline (for standard mesh rendering)
 * - Descriptor pool (auto-growing pool for all systems)
 *
 * Lifecycle:
 * - Create via default constructor
 * - Call init() after VulkanContext and PostProcessSystem are ready
 * - Access via getters for descriptor allocation and pipeline binding
 */
class DescriptorInfrastructure {
public:
    struct Config {
        uint32_t setsPerPool = 64;
        DescriptorPoolSizes poolSizes = DescriptorPoolSizes::standard();
    };

    DescriptorInfrastructure() = default;
    ~DescriptorInfrastructure() = default;

    // Non-copyable, non-movable (owns GPU resources)
    DescriptorInfrastructure(const DescriptorInfrastructure&) = delete;
    DescriptorInfrastructure& operator=(const DescriptorInfrastructure&) = delete;
    DescriptorInfrastructure(DescriptorInfrastructure&&) = delete;
    DescriptorInfrastructure& operator=(DescriptorInfrastructure&&) = delete;

    /**
     * Initialize descriptor set layout and pool.
     * Call before createGraphicsPipeline().
     */
    bool initDescriptors(VulkanContext& context, const Config& config);

    /**
     * Create the graphics pipeline for standard scene rendering.
     * Requires PostProcessSystem to be initialized (for HDR render pass).
     */
    bool createGraphicsPipeline(VulkanContext& context, VkRenderPass hdrRenderPass,
                                const std::string& resourcePath);

    /**
     * Cleanup all resources.
     * Called automatically by destructor, but can be called explicitly.
     */
    void cleanup();

    // Accessors
    vk::DescriptorSetLayout getDescriptorSetLayout() const {
        return descriptorSetLayout_ ? **descriptorSetLayout_ : vk::DescriptorSetLayout{};
    }

    vk::PipelineLayout getPipelineLayout() const {
        return pipelineLayout_ ? **pipelineLayout_ : vk::PipelineLayout{};
    }

    vk::Pipeline getGraphicsPipeline() const {
        return graphicsPipeline_ ? **graphicsPipeline_ : vk::Pipeline{};
    }

    // Pointer accessors for storing references in config structs
    const vk::Pipeline* getGraphicsPipelinePtr() const {
        return graphicsPipeline_ ? &**graphicsPipeline_ : nullptr;
    }

    const vk::PipelineLayout* getPipelineLayoutPtr() const {
        return pipelineLayout_ ? &**pipelineLayout_ : nullptr;
    }

    DescriptorManager::Pool* getDescriptorPool() {
        return descriptorManagerPool_.has_value() ? &*descriptorManagerPool_ : nullptr;
    }

    const DescriptorManager::Pool* getDescriptorPool() const {
        return descriptorManagerPool_.has_value() ? &*descriptorManagerPool_ : nullptr;
    }

    // Get allocator via interface (for reduced coupling)
    IDescriptorAllocator* getDescriptorAllocator() {
        return descriptorManagerPool_.has_value() ? &*descriptorManagerPool_ : nullptr;
    }

    // Raw handle accessors for compatibility
    VkDescriptorSetLayout getVkDescriptorSetLayout() const {
        return descriptorSetLayout_ ? static_cast<VkDescriptorSetLayout>(**descriptorSetLayout_) : VK_NULL_HANDLE;
    }

    VkPipelineLayout getVkPipelineLayout() const {
        return pipelineLayout_ ? static_cast<VkPipelineLayout>(**pipelineLayout_) : VK_NULL_HANDLE;
    }

    VkPipeline getVkGraphicsPipeline() const {
        return graphicsPipeline_ ? static_cast<VkPipeline>(**graphicsPipeline_) : VK_NULL_HANDLE;
    }

    /**
     * Add common descriptor bindings shared between main and skinned mesh layouts.
     * Provides the standard binding layout used by shader.frag.
     * Can be used by other systems (e.g., SkinnedMeshRenderer) to ensure layout compatibility.
     */
    static void addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder);

    /**
     * Set bindless descriptor set layouts (Sets 1 and 2) for inclusion in the
     * pipeline layout. Must be called before createGraphicsPipeline().
     */
    void setBindlessLayouts(vk::DescriptorSetLayout textureSetLayout,
                           vk::DescriptorSetLayout materialSetLayout) {
        bindlessTextureSetLayout_ = textureSetLayout;
        bindlessMaterialSetLayout_ = materialSetLayout;
    }

    bool isInitialized() const { return initialized_; }
    bool hasPipeline() const { return graphicsPipeline_.has_value(); }

private:
    bool createDescriptorSetLayout(VkDevice device, const vk::raii::Device& raiiDevice);
    bool createDescriptorPool(VkDevice device, const Config& config);

    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> graphicsPipeline_;
    std::optional<DescriptorManager::Pool> descriptorManagerPool_;

    // Bindless layouts (non-owning, owned by BindlessManager)
    vk::DescriptorSetLayout bindlessTextureSetLayout_;
    vk::DescriptorSetLayout bindlessMaterialSetLayout_;

    bool initialized_ = false;
};
