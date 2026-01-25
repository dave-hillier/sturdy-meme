#include "SkySystem.h"
#include "AtmosphereLUTSystem.h"
#include "GraphicsPipelineFactory.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include "core/InitInfoBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <array>

std::unique_ptr<SkySystem> SkySystem::create(const InitInfo& info) {
    auto system = std::make_unique<SkySystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<SkySystem> SkySystem::create(const InitContext& ctx, VkRenderPass hdrPass) {
    InitInfo info = InitInfoBuilder::fromContext<InitInfo>(ctx);
    info.hdrRenderPass = hdrPass;
    return create(info);
}

SkySystem::~SkySystem() {
    cleanup();
}

bool SkySystem::initInternal(const InitInfo& info) {
    device = info.device;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    hdrRenderPass = info.hdrRenderPass;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SkySystem requires raiiDevice");
        return false;
    }

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;

    return true;
}

void SkySystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;  // Not initialized

    // RAII wrappers handle cleanup automatically
    pipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_.reset();
    // Descriptor sets are freed when the pool is destroyed
    descriptorSets.clear();
}

bool SkySystem::createDescriptorSetLayout() {
    // Sky shader bindings:
    // 0: UBO (same as main shader)
    // 1: Transmittance LUT sampler
    // 2: Multi-scatter LUT sampler
    // 3: Sky-view LUT sampler (updated per-frame)
    // 4: Rayleigh Irradiance LUT sampler (Phase 4.1.9)
    // 5: Mie Irradiance LUT sampler (Phase 4.1.9)
    // 6: Cloud Map LUT sampler (Paraboloid projection, updated per-frame)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
            .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
            .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: Transmittance LUT
            .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: Multi-scatter LUT
            .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: Sky-view LUT
            .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 4: Rayleigh Irradiance LUT
            .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: Mie Irradiance LUT
            .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: Cloud Map LUT
            .build();
    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky descriptor set layout");
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Create pipeline layout
    auto layoutOpt = PipelineLayoutBuilder(*raiiDevice_)
        .addDescriptorSetLayout(**descriptorSetLayout_)
        .build();
    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sky pipeline layout");
        return false;
    }
    pipelineLayout_ = std::move(layoutOpt);

    return true;
}

bool SkySystem::createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                      VkDeviceSize uniformBufferSize,
                                      AtmosphereLUTSystem& atmosphereLUTSystem) {
    // Allocate sky descriptor sets using managed pool
    descriptorSets = descriptorPool->allocate(**descriptorSetLayout_, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate sky descriptor sets");
        return false;
    }

    // Get LUT views and sampler from atmosphere system
    VkImageView transmittanceLUTView = atmosphereLUTSystem.getTransmittanceLUTView();
    VkImageView multiScatterLUTView = atmosphereLUTSystem.getMultiScatterLUTView();
    VkImageView skyViewLUTView = atmosphereLUTSystem.getSkyViewLUTView();
    VkImageView rayleighIrradianceLUTView = atmosphereLUTSystem.getRayleighIrradianceLUTView();
    VkImageView mieIrradianceLUTView = atmosphereLUTSystem.getMieIrradianceLUTView();
    VkImageView cloudMapLUTView = atmosphereLUTSystem.getCloudMapLUTView();
    VkSampler lutSampler = atmosphereLUTSystem.getLUTSampler();

    // Update each descriptor set
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeBuffer(0, uniformBuffers[i], 0, uniformBufferSize)
            .writeImage(1, transmittanceLUTView, lutSampler)
            .writeImage(2, multiScatterLUTView, lutSampler)
            .writeImage(3, skyViewLUTView, lutSampler)
            .writeImage(4, rayleighIrradianceLUTView, lutSampler)
            .writeImage(5, mieIrradianceLUTView, lutSampler)
            .writeImage(6, cloudMapLUTView, lutSampler)
            .update();
    }

    SDL_Log("Sky descriptor sets created with atmosphere LUTs (including cloud map)");
    return true;
}

bool SkySystem::createPipeline() {
    GraphicsPipelineFactory factory(device);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::FullscreenQuad)
        .setShaders(shaderPath + "/sky.vert.spv", shaderPath + "/sky.frag.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(**pipelineLayout_)
        .setExtent(extent)
        .setDynamicViewport(true)
        .build(rawPipeline);

    if (!success) {
        SDL_Log("Failed to create sky pipeline");
        return false;
    }

    pipeline_.emplace(*raiiDevice_, rawPipeline);
    return true;
}

void SkySystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    vk::CommandBuffer vkCmd(cmd);

    vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);

    // Set dynamic viewport and scissor to handle window resize
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({extent.width, extent.height});
    vkCmd.setScissor(0, scissor);

    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                             **pipelineLayout_, 0,
                             vk::DescriptorSet(descriptorSets[frameIndex]), {});
    vkCmd.draw(3, 1, 0, 0);
}
