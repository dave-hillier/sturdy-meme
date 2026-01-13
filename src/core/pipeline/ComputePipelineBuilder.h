#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <optional>

/**
 * ComputePipelineBuilder - Fluent builder for Vulkan compute pipelines
 *
 * Reduces duplication in compute pipeline creation by providing:
 * - Sensible defaults for all pipeline states
 * - Fluent API for customization
 * - Automatic shader module cleanup
 *
 * Usage:
 *   ComputePipelineBuilder builder(device);
 *   builder.setShader("shaders/myshader.comp.spv")
 *          .setPipelineLayout(layout)
 *          .build(pipeline);
 */
class ComputePipelineBuilder {
public:
    explicit ComputePipelineBuilder(VkDevice device);
    ~ComputePipelineBuilder();

    // Reset all state to defaults
    ComputePipelineBuilder& reset();

    // Set pipeline cache for faster pipeline creation
    ComputePipelineBuilder& setPipelineCache(VkPipelineCache cache);

    // Shader configuration
    ComputePipelineBuilder& setShader(const std::string& path);
    ComputePipelineBuilder& setEntryPoint(const std::string& entryPoint);

    // Pipeline layout
    ComputePipelineBuilder& setPipelineLayout(VkPipelineLayout layout);

    // Specialization constants
    ComputePipelineBuilder& setSpecializationInfo(const VkSpecializationInfo* info);

    // Build the pipeline (raw handle - caller must manage lifetime)
    bool build(VkPipeline& pipeline);

    // Cleanup any allocated shader modules (called automatically by build)
    void cleanup();

private:
    VkDevice device;
    VkPipelineCache pipelineCacheHandle = VK_NULL_HANDLE;

    // Shader state
    std::string shaderPath;
    std::string entryPoint = "main";
    std::optional<VkShaderModule> shaderModule;

    // Pipeline configuration
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    const VkSpecializationInfo* specializationInfo = nullptr;

    bool loadShaderModule(VkPipelineShaderStageCreateInfo& stage);
};
