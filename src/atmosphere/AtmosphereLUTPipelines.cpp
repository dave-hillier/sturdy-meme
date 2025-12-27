#include "AtmosphereLUTSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <vector>

using namespace vk;

// Helper to create a compute pipeline from shader file
static bool createComputePipelineHelper(
    VkDevice device,
    const std::string& shaderFile,
    VkPipelineLayout pipelineLayout,
    VkPipeline& outPipeline,
    const char* pipelineName)
{
    auto shaderCode = ShaderLoader::readFile(shaderFile);
    if (!shaderCode) {
        SDL_Log("Failed to read %s shader file", pipelineName);
        return false;
    }

    ShaderModuleCreateInfo moduleInfo{
        {},                                      // flags
        shaderCode->size(),
        reinterpret_cast<const uint32_t*>(shaderCode->data())
    };

    auto vkModuleInfo = static_cast<VkShaderModuleCreateInfo>(moduleInfo);
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &vkModuleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        SDL_Log("Failed to create %s shader module", pipelineName);
        return false;
    }

    PipelineShaderStageCreateInfo shaderStageInfo{
        {},                              // flags
        ShaderStageFlagBits::eCompute,
        shaderModule,
        "main"
    };

    ComputePipelineCreateInfo pipelineInfo{
        {},                              // flags
        shaderStageInfo,
        pipelineLayout
    };

    auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &vkPipelineInfo, nullptr, &outPipeline) != VK_SUCCESS) {
        SDL_Log("Failed to create %s pipeline", pipelineName);
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, shaderModule, nullptr);
    return true;
}

bool AtmosphereLUTSystem::createComputePipelines() {
    // Create transmittance pipeline
    if (!createComputePipelineHelper(device, shaderPath + "/transmittance_lut.comp.spv",
                                     transmittancePipelineLayout, transmittancePipeline, "transmittance")) {
        return false;
    }

    // Create multi-scatter pipeline
    if (!createComputePipelineHelper(device, shaderPath + "/multiscatter_lut.comp.spv",
                                     multiScatterPipelineLayout, multiScatterPipeline, "multi-scatter")) {
        return false;
    }

    // Create sky-view pipeline
    if (!createComputePipelineHelper(device, shaderPath + "/skyview_lut.comp.spv",
                                     skyViewPipelineLayout, skyViewPipeline, "sky-view")) {
        return false;
    }

    // Create irradiance pipeline
    if (!createComputePipelineHelper(device, shaderPath + "/irradiance_lut.comp.spv",
                                     irradiancePipelineLayout, irradiancePipeline, "irradiance")) {
        return false;
    }

    // Create cloud map pipeline
    if (!createComputePipelineHelper(device, shaderPath + "/cloudmap_lut.comp.spv",
                                     cloudMapPipelineLayout, cloudMapPipeline, "cloud map")) {
        return false;
    }

    return true;
}
