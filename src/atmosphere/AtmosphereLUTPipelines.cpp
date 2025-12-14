#include "AtmosphereLUTSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <vector>

bool AtmosphereLUTSystem::createComputePipelines() {
    // Create transmittance pipeline
    {
        std::string shaderFile = shaderPath + "/transmittance_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = transmittancePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transmittancePipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create multi-scatter pipeline
    {
        std::string shaderFile = shaderPath + "/multiscatter_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = multiScatterPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &multiScatterPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create sky-view pipeline
    {
        std::string shaderFile = shaderPath + "/skyview_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = skyViewPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyViewPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create irradiance pipeline
    {
        std::string shaderFile = shaderPath + "/irradiance_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = irradiancePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &irradiancePipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create irradiance pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create cloud map pipeline
    {
        std::string shaderFile = shaderPath + "/cloudmap_lut.comp.spv";
        std::vector<char> shaderCode = ShaderLoader::readFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = cloudMapPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &cloudMapPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    return true;
}
