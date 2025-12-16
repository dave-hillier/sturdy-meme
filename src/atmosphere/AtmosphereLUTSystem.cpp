#include "AtmosphereLUTSystem.h"
#include <SDL3/SDL_log.h>

bool AtmosphereLUTSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createTransmittanceLUT()) return false;
    if (!createMultiScatterLUT()) return false;
    if (!createSkyViewLUT()) return false;
    if (!createIrradianceLUTs()) return false;
    if (!createCloudMapLUT()) return false;
    if (!createLUTSampler()) return false;
    if (!createUniformBuffer()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipelines()) return false;

    SDL_Log("Atmosphere LUT System initialized");
    return true;
}

bool AtmosphereLUTSystem::init(const InitContext& ctx) {
    device = ctx.device;
    allocator = ctx.allocator;
    descriptorPool = ctx.descriptorPool;
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;

    if (!createTransmittanceLUT()) return false;
    if (!createMultiScatterLUT()) return false;
    if (!createSkyViewLUT()) return false;
    if (!createIrradianceLUTs()) return false;
    if (!createCloudMapLUT()) return false;
    if (!createLUTSampler()) return false;
    if (!createUniformBuffer()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipelines()) return false;

    SDL_Log("Atmosphere LUT System initialized");
    return true;
}

void AtmosphereLUTSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroyLUTResources();

    // Destroy all uniform buffers using consistent BufferUtils pattern
    BufferUtils::destroyBuffers(allocator, staticUniformBuffers);
    BufferUtils::destroyBuffers(allocator, skyViewUniformBuffers);
    BufferUtils::destroyBuffers(allocator, cloudMapUniformBuffers);

    if (transmittancePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, transmittancePipeline, nullptr);
        transmittancePipeline = VK_NULL_HANDLE;
    }
    if (multiScatterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, multiScatterPipeline, nullptr);
        multiScatterPipeline = VK_NULL_HANDLE;
    }
    if (skyViewPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skyViewPipeline, nullptr);
        skyViewPipeline = VK_NULL_HANDLE;
    }
    if (irradiancePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, irradiancePipeline, nullptr);
        irradiancePipeline = VK_NULL_HANDLE;
    }
    if (cloudMapPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, cloudMapPipeline, nullptr);
        cloudMapPipeline = VK_NULL_HANDLE;
    }

    if (transmittancePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, transmittancePipelineLayout, nullptr);
        transmittancePipelineLayout = VK_NULL_HANDLE;
    }
    if (multiScatterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, multiScatterPipelineLayout, nullptr);
        multiScatterPipelineLayout = VK_NULL_HANDLE;
    }
    if (skyViewPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, skyViewPipelineLayout, nullptr);
        skyViewPipelineLayout = VK_NULL_HANDLE;
    }
    if (irradiancePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, irradiancePipelineLayout, nullptr);
        irradiancePipelineLayout = VK_NULL_HANDLE;
    }
    if (cloudMapPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, cloudMapPipelineLayout, nullptr);
        cloudMapPipelineLayout = VK_NULL_HANDLE;
    }

    if (transmittanceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, transmittanceDescriptorSetLayout, nullptr);
        transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (multiScatterDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, multiScatterDescriptorSetLayout, nullptr);
        multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (skyViewDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, skyViewDescriptorSetLayout, nullptr);
        skyViewDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (irradianceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, irradianceDescriptorSetLayout, nullptr);
        irradianceDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (cloudMapDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, cloudMapDescriptorSetLayout, nullptr);
        cloudMapDescriptorSetLayout = VK_NULL_HANDLE;
    }

    lutSampler.destroy();
}
