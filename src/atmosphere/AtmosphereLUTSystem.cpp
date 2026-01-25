#include "AtmosphereLUTSystem.h"
#include "core/InitInfoBuilder.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<AtmosphereLUTSystem> AtmosphereLUTSystem::create(const InitInfo& info) {
    auto system = std::make_unique<AtmosphereLUTSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<AtmosphereLUTSystem> AtmosphereLUTSystem::create(const InitContext& ctx) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    return create(info);
}

AtmosphereLUTSystem::~AtmosphereLUTSystem() {
    cleanup();
}

bool AtmosphereLUTSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

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

void AtmosphereLUTSystem::cleanup() {
    if (!device) return;  // Not initialized
    destroyLUTResources();

    // Destroy all uniform buffers using consistent BufferUtils pattern
    BufferUtils::destroyBuffers(allocator, staticUniformBuffers);
    BufferUtils::destroyBuffers(allocator, skyViewUniformBuffers);
    BufferUtils::destroyBuffers(allocator, cloudMapUniformBuffers);

    vk::Device vkDevice(device);

    // Destroy pipelines
    auto destroyPipeline = [&](VkPipeline& pipeline) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDevice.destroyPipeline(pipeline);
            pipeline = VK_NULL_HANDLE;
        }
    };
    destroyPipeline(transmittancePipeline);
    destroyPipeline(multiScatterPipeline);
    destroyPipeline(skyViewPipeline);
    destroyPipeline(irradiancePipeline);
    destroyPipeline(cloudMapPipeline);

    // Destroy pipeline layouts
    auto destroyLayout = [&](VkPipelineLayout& layout) {
        if (layout != VK_NULL_HANDLE) {
            vkDevice.destroyPipelineLayout(layout);
            layout = VK_NULL_HANDLE;
        }
    };
    destroyLayout(transmittancePipelineLayout);
    destroyLayout(multiScatterPipelineLayout);
    destroyLayout(skyViewPipelineLayout);
    destroyLayout(irradiancePipelineLayout);
    destroyLayout(cloudMapPipelineLayout);

    // Destroy descriptor set layouts
    auto destroyDescLayout = [&](VkDescriptorSetLayout& layout) {
        if (layout != VK_NULL_HANDLE) {
            vkDevice.destroyDescriptorSetLayout(layout);
            layout = VK_NULL_HANDLE;
        }
    };
    destroyDescLayout(transmittanceDescriptorSetLayout);
    destroyDescLayout(multiScatterDescriptorSetLayout);
    destroyDescLayout(skyViewDescriptorSetLayout);
    destroyDescLayout(irradianceDescriptorSetLayout);
    destroyDescLayout(cloudMapDescriptorSetLayout);

    lutSampler_.reset();
}
