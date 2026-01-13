#include "ComputePipelineBuilder.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>

ComputePipelineBuilder::ComputePipelineBuilder(VkDevice device) : device(device) {}

ComputePipelineBuilder::~ComputePipelineBuilder() {
    cleanup();
}

ComputePipelineBuilder& ComputePipelineBuilder::reset() {
    cleanup();

    shaderPath.clear();
    entryPoint = "main";
    pipelineLayout = VK_NULL_HANDLE;
    pipelineCacheHandle = VK_NULL_HANDLE;
    specializationInfo = nullptr;

    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setPipelineCache(VkPipelineCache cache) {
    pipelineCacheHandle = cache;
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setShader(const std::string& path) {
    shaderPath = path;
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setEntryPoint(const std::string& entry) {
    entryPoint = entry;
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setPipelineLayout(VkPipelineLayout layout) {
    pipelineLayout = layout;
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setSpecializationInfo(const VkSpecializationInfo* info) {
    specializationInfo = info;
    return *this;
}

bool ComputePipelineBuilder::loadShaderModule(VkPipelineShaderStageCreateInfo& stage) {
    if (shaderPath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputePipelineBuilder: Shader path not set");
        return false;
    }

    auto shaderCode = ShaderLoader::readFile(shaderPath);
    if (!shaderCode) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputePipelineBuilder: Failed to read shader file: %s", shaderPath.c_str());
        return false;
    }

    auto module = ShaderLoader::createShaderModule(device, *shaderCode);
    if (!module) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputePipelineBuilder: Failed to create shader module: %s", shaderPath.c_str());
        return false;
    }

    shaderModule = *module;

    stage = {};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = *shaderModule;
    stage.pName = entryPoint.c_str();
    stage.pSpecializationInfo = specializationInfo;

    return true;
}

bool ComputePipelineBuilder::build(VkPipeline& pipeline) {
    // Validate required state
    if (pipelineLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputePipelineBuilder: Pipeline layout not set");
        return false;
    }

    // Load shader
    VkPipelineShaderStageCreateInfo shaderStage{};
    if (!loadShaderModule(shaderStage)) {
        return false;
    }

    // Create the pipeline
    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(*reinterpret_cast<const vk::PipelineShaderStageCreateInfo*>(&shaderStage))
        .setLayout(pipelineLayout);

    vk::Device vkDevice(device);
    auto result = vkDevice.createComputePipelines(pipelineCacheHandle, pipelineInfo);
    cleanup();

    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ComputePipelineBuilder: Failed to create compute pipeline");
        return false;
    }

    pipeline = result.value[0];
    return true;
}

void ComputePipelineBuilder::cleanup() {
    if (shaderModule) {
        vk::Device vkDevice(device);
        vkDevice.destroyShaderModule(*shaderModule);
        shaderModule.reset();
    }
}
