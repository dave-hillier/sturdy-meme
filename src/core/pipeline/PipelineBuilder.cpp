#include "PipelineBuilder.h"
#include "VulkanRAII.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <array>

PipelineBuilder::PipelineBuilder(VkDevice device) : device(device) {}

PipelineBuilder::~PipelineBuilder() { cleanupShaderModules(); }

PipelineBuilder& PipelineBuilder::reset() {
    descriptorBindings.clear();
    pushConstantRanges.clear();
    shaderStages.clear();
    pipelineCacheHandle = VK_NULL_HANDLE;
    cleanupShaderModules();
    return *this;
}

PipelineBuilder& PipelineBuilder::setPipelineCache(VkPipelineCache cache) {
    pipelineCacheHandle = cache;
    return *this;
}

PipelineBuilder& PipelineBuilder::addDescriptorBinding(uint32_t binding, VkDescriptorType type, uint32_t count,
                                                       VkShaderStageFlags stageFlags, const VkSampler* immutableSamplers) {
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = type;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stageFlags;
    layoutBinding.pImmutableSamplers = immutableSamplers;
    descriptorBindings.push_back(layoutBinding);
    return *this;
}

bool PipelineBuilder::buildDescriptorSetLayout(VkDescriptorSetLayout& layout) const {
    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindingCount(static_cast<uint32_t>(descriptorBindings.size()))
        .setPBindings(reinterpret_cast<const vk::DescriptorSetLayoutBinding*>(descriptorBindings.data()));

    vk::Device vkDevice(device);
    layout = vkDevice.createDescriptorSetLayout(layoutInfo);
    return true;
}

PipelineBuilder& PipelineBuilder::addPushConstantRange(VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size) {
    VkPushConstantRange range{};
    range.stageFlags = stageFlags;
    range.offset = offset;
    range.size = size;
    pushConstantRanges.push_back(range);
    return *this;
}

PipelineBuilder& PipelineBuilder::addShaderStage(const std::string& path, VkShaderStageFlagBits stage, const char* entry) {
    auto module = ShaderLoader::loadShaderModule(device, path);
    if (!module) {
        SDL_Log("Failed to load shader module at %s", path.c_str());
        return *this;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = stage;
    stageInfo.module = *module;
    stageInfo.pName = entry;

    shaderStages.push_back(stageInfo);
    shaderModules.push_back(*module);
    return *this;
}

bool PipelineBuilder::buildPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts, VkPipelineLayout& layout) const {
    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayoutCount(static_cast<uint32_t>(setLayouts.size()))
        .setPSetLayouts(reinterpret_cast<const vk::DescriptorSetLayout*>(setLayouts.data()))
        .setPushConstantRangeCount(static_cast<uint32_t>(pushConstantRanges.size()))
        .setPPushConstantRanges(reinterpret_cast<const vk::PushConstantRange*>(pushConstantRanges.data()));

    vk::Device vkDevice(device);
    layout = vkDevice.createPipelineLayout(layoutInfo);
    return true;
}

bool PipelineBuilder::buildComputePipeline(VkPipelineLayout layout, VkPipeline& pipeline) {
    if (shaderStages.empty()) {
        SDL_Log("No shader stages provided for compute pipeline");
        return false;
    }

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(*reinterpret_cast<const vk::PipelineShaderStageCreateInfo*>(&shaderStages[0]))
        .setLayout(layout);

    vk::Device vkDevice(device);
    auto result = vkDevice.createComputePipelines(pipelineCacheHandle, pipelineInfo);
    cleanupShaderModules();

    pipeline = result.value[0];
    return true;
}

bool PipelineBuilder::buildGraphicsPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfoBase, VkPipelineLayout layout,
                                            VkPipeline& pipeline) {
    if (shaderStages.empty()) {
        SDL_Log("No shader stages provided for graphics pipeline");
        return false;
    }

    // Copy the base info and modify it
    VkGraphicsPipelineCreateInfo pipelineInfo = pipelineInfoBase;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.layout = layout;

    vk::Device vkDevice(device);
    auto result = vkDevice.createGraphicsPipelines(
        pipelineCacheHandle,
        *reinterpret_cast<const vk::GraphicsPipelineCreateInfo*>(&pipelineInfo));
    cleanupShaderModules();

    pipeline = result.value[0];
    return true;
}

void PipelineBuilder::cleanupShaderModules() {
    vk::Device vkDevice(device);
    for (VkShaderModule module : shaderModules) {
        vkDevice.destroyShaderModule(module);
    }
    shaderModules.clear();
    shaderStages.clear();
}

bool PipelineBuilder::buildGraphicsPipeline(const GraphicsPipelineConfig& config, VkPipelineLayout layout,
                                            VkPipeline& pipeline) {
    if (shaderStages.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No shader stages provided for graphics pipeline");
        return false;
    }

    // Vertex input - optional meshlet vertex input (vec2)
    auto bindingDesc = vk::VertexInputBindingDescription{}
        .setBinding(0)
        .setStride(sizeof(glm::vec2))
        .setInputRate(vk::VertexInputRate::eVertex);

    auto attrDesc = vk::VertexInputAttributeDescription{}
        .setBinding(0)
        .setLocation(0)
        .setFormat(vk::Format::eR32G32Sfloat)
        .setOffset(0);

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};
    if (config.useMeshletVertexInput) {
        vertexInputInfo.setVertexBindingDescriptions(bindingDesc)
            .setVertexAttributeDescriptions(attrDesc);
    }

    // Input assembly - always triangle list
    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    // Viewport state - dynamic
    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    // Rasterization
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(static_cast<vk::PolygonMode>(config.polygonMode))
        .setLineWidth(1.0f)
        .setCullMode(static_cast<vk::CullModeFlags>(config.cullMode))
        .setFrontFace(static_cast<vk::FrontFace>(config.frontFace))
        .setDepthBiasEnable(config.depthBiasEnable);

    // Multisampling
    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    // Depth/stencil
    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(config.depthTestEnable)
        .setDepthWriteEnable(config.depthWriteEnable)
        .setDepthCompareOp(static_cast<vk::CompareOp>(config.depthCompareOp));

    // Color blending
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{};
    if (config.hasColorAttachment) {
        colorBlending.setAttachments(colorBlendAttachment);
    }

    // Dynamic states
    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    if (config.dynamicDepthBias) {
        dynamicStates.push_back(vk::DynamicState::eDepthBias);
    }

    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    // Create pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(reinterpret_cast<const vk::PipelineShaderStageCreateInfo*>(shaderStages.data()))
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(layout)
        .setRenderPass(config.renderPass)
        .setSubpass(config.subpass);

    vk::Device vkDevice(device);
    auto result = vkDevice.createGraphicsPipelines(pipelineCacheHandle, pipelineInfo);
    cleanupShaderModules();

    pipeline = result.value[0];
    return true;
}

// ============================================================================
// RAII-managed build methods
// ============================================================================

bool PipelineBuilder::buildManagedDescriptorSetLayout(ManagedDescriptorSetLayout& outLayout) const {
    VkDescriptorSetLayout rawLayout = VK_NULL_HANDLE;
    if (!buildDescriptorSetLayout(rawLayout)) {
        return false;
    }
    outLayout = ManagedDescriptorSetLayout::fromRaw(device, rawLayout);
    return true;
}

bool PipelineBuilder::buildManagedPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts,
                                                  ManagedPipelineLayout& outLayout) const {
    VkPipelineLayout rawLayout = VK_NULL_HANDLE;
    if (!buildPipelineLayout(setLayouts, rawLayout)) {
        return false;
    }
    outLayout = ManagedPipelineLayout::fromRaw(device, rawLayout);
    return true;
}

bool PipelineBuilder::buildManagedComputePipeline(VkPipelineLayout layout, ManagedPipeline& outPipeline) {
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!buildComputePipeline(layout, rawPipeline)) {
        return false;
    }
    outPipeline = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool PipelineBuilder::buildManagedGraphicsPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfoBase,
                                                    VkPipelineLayout layout, ManagedPipeline& outPipeline) {
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!buildGraphicsPipeline(pipelineInfoBase, layout, rawPipeline)) {
        return false;
    }
    outPipeline = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

bool PipelineBuilder::buildManagedGraphicsPipeline(const GraphicsPipelineConfig& config,
                                                    VkPipelineLayout layout, ManagedPipeline& outPipeline) {
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!buildGraphicsPipeline(config, layout, rawPipeline)) {
        return false;
    }
    outPipeline = ManagedPipeline::fromRaw(device, rawPipeline);
    return true;
}

