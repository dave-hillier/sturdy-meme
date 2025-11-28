#include "PipelineBuilder.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include <SDL3/SDL.h>

PipelineBuilder::PipelineBuilder(VkDevice device) : device(device) {}

PipelineBuilder::~PipelineBuilder() { cleanupShaderModules(); }

PipelineBuilder& PipelineBuilder::reset() {
    descriptorBindings.clear();
    pushConstantRanges.clear();
    shaderStages.clear();
    cleanupShaderModules();
    return *this;
}

PipelineBuilder& PipelineBuilder::addDescriptorBinding(uint32_t binding, VkDescriptorType type, uint32_t count,
                                                       VkShaderStageFlags stageFlags, const VkSampler* immutableSamplers) {
    VkDescriptorSetLayoutBinding layoutBinding = BindingBuilder()
                                                     .setBinding(binding)
                                                     .setDescriptorType(type)
                                                     .setDescriptorCount(count)
                                                     .setStageFlags(stageFlags)
                                                     .setImmutableSamplers(immutableSamplers)
                                                     .build();
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
    VkShaderModule module = ShaderLoader::loadShaderModule(device, path);
    if (module == VK_NULL_HANDLE) {
        SDL_Log("Failed to load shader module at %s", path.c_str());
        return *this;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = stage;
    stageInfo.module = module;
    stageInfo.pName = entry;

    shaderStages.push_back(stageInfo);
    shaderModules.push_back(module);
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

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
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

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
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

