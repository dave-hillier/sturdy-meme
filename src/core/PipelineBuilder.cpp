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
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(descriptorBindings.size());
    layoutInfo.pBindings = descriptorBindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        SDL_Log("Failed to create descriptor set layout via PipelineBuilder");
        return false;
    }
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
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges.data();

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        SDL_Log("Failed to create pipeline layout via PipelineBuilder");
        return false;
    }

    return true;
}

bool PipelineBuilder::buildComputePipeline(VkPipelineLayout layout, VkPipeline& pipeline) {
    if (shaderStages.empty()) {
        SDL_Log("No shader stages provided for compute pipeline");
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStages[0];
    pipelineInfo.layout = layout;

    VkResult result = vkCreateComputePipelines(device, pipelineCacheHandle, 1, &pipelineInfo, nullptr, &pipeline);
    cleanupShaderModules();

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create compute pipeline via PipelineBuilder");
        return false;
    }

    return true;
}

bool PipelineBuilder::buildGraphicsPipeline(const VkGraphicsPipelineCreateInfo& pipelineInfoBase, VkPipelineLayout layout,
                                            VkPipeline& pipeline) {
    if (shaderStages.empty()) {
        SDL_Log("No shader stages provided for graphics pipeline");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = pipelineInfoBase;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.layout = layout;

    VkResult result = vkCreateGraphicsPipelines(device, pipelineCacheHandle, 1, &pipelineInfo, nullptr, &pipeline);
    cleanupShaderModules();

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create graphics pipeline via PipelineBuilder");
        return false;
    }

    return true;
}

void PipelineBuilder::cleanupShaderModules() {
    for (VkShaderModule module : shaderModules) {
        vkDestroyShaderModule(device, module, nullptr);
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
    VkVertexInputBindingDescription bindingDesc{};
    VkVertexInputAttributeDescription attrDesc{};
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    if (config.useMeshletVertexInput) {
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(glm::vec2);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        attrDesc.binding = 0;
        attrDesc.location = 0;
        attrDesc.format = VK_FORMAT_R32G32_SFLOAT;
        attrDesc.offset = 0;

        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = 1;
        vertexInputInfo.pVertexAttributeDescriptions = &attrDesc;
    }

    // Input assembly - always triangle list
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport state - dynamic
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = config.depthBiasEnable ? VK_TRUE : VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = config.depthCompareOp;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    if (config.hasColorAttachment) {
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
    } else {
        colorBlending.attachmentCount = 0;
    }

    // Dynamic states
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    if (config.dynamicDepthBias) {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = config.renderPass;
    pipelineInfo.subpass = config.subpass;

    VkResult result = vkCreateGraphicsPipelines(device, pipelineCacheHandle, 1, &pipelineInfo, nullptr, &pipeline);
    cleanupShaderModules();

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
        return false;
    }

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

