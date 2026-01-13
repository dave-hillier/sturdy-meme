#include "FroxelSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "core/vulkan/BarrierHelpers.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>

std::unique_ptr<FroxelSystem> FroxelSystem::create(const InitInfo& info) {
    std::unique_ptr<FroxelSystem> system(new FroxelSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<FroxelSystem> FroxelSystem::create(const InitContext& ctx, VkImageView shadowMapView_, VkSampler shadowSampler_,
                                                    const std::vector<VkBuffer>& lightBuffers_) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.shadowMapView = shadowMapView_;
    info.shadowSampler = shadowSampler_;
    info.lightBuffers = lightBuffers_;
    info.raiiDevice = ctx.raiiDevice;

    return create(info);
}

FroxelSystem::~FroxelSystem() {
    cleanup();
}

bool FroxelSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    shadowMapView = info.shadowMapView;
    shadowSampler = info.shadowSampler;
    lightBuffers = info.lightBuffers;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_Log("FroxelSystem requires raiiDevice");
        return false;
    }

    if (!createScatteringVolume()) return false;
    if (!createIntegratedVolume()) return false;
    if (!createSampler()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorSets()) return false;
    if (!createFroxelUpdatePipeline()) return false;
    if (!createIntegrationPipeline()) return false;

    return true;
}

void FroxelSystem::cleanup() {
    if (!device) return;  // Not initialized

    destroyVolumeResources();

    BufferUtils::destroyBuffers(allocator, uniformBuffers);

    // RAII wrappers handle cleanup automatically
    froxelUpdatePipeline_.reset();
    integrationPipeline_.reset();
    froxelPipelineLayout_.reset();
    froxelDescriptorSetLayout_.reset();
    volumeSampler_.reset();
}

void FroxelSystem::destroyVolumeResources() {
    // RAII wrappers handle cleanup automatically
    for (int i = 0; i < 2; i++) {
        scatteringVolumeViews_[i].reset();
        scatteringVolumes_[i] = ManagedImage();
    }

    integratedVolumeView_.reset();
    integratedVolume_ = ManagedImage();
}

void FroxelSystem::resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent) {
    extent = newExtent;
    // Froxel grid size is fixed, no need to recreate volumes
}

bool FroxelSystem::createScatteringVolume() {
    // Create two 3D images for double-buffered scattering data (ping-pong for temporal)
    // Format: R16G16B16A16_SFLOAT for in-scatter RGB and opacity
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e3D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{FROXEL_WIDTH, FROXEL_HEIGHT, FROXEL_DEPTH})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Create both buffers for ping-pong
    for (int i = 0; i < 2; i++) {
        if (!ManagedImage::create(allocator, *reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
                                  allocInfo, scatteringVolumes_[i])) {
            SDL_Log("Failed to create scattering volume %d", i);
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(scatteringVolumes_[i].get())
            .setViewType(vk::ImageViewType::e3D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        try {
            scatteringVolumeViews_[i].emplace(*raiiDevice_, viewInfo);
        } catch (const std::exception& e) {
            SDL_Log("Failed to create scattering volume view %d: %s", i, e.what());
            return false;
        }
    }

    return true;
}

bool FroxelSystem::createIntegratedVolume() {
    // Create 3D image for integrated scattering (front-to-back)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e3D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setExtent(vk::Extent3D{FROXEL_WIDTH, FROXEL_HEIGHT, FROXEL_DEPTH})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!ManagedImage::create(allocator, *reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
                              allocInfo, integratedVolume_)) {
        SDL_Log("Failed to create integrated volume");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(integratedVolume_.get())
        .setViewType(vk::ImageViewType::e3D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    try {
        integratedVolumeView_.emplace(*raiiDevice_, viewInfo);
    } catch (const std::exception& e) {
        SDL_Log("Failed to create integrated volume view: %s", e.what());
        return false;
    }

    return true;
}

bool FroxelSystem::createSampler() {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMipLodBias(0.0f)
        .setAnisotropyEnable(VK_FALSE)
        .setCompareEnable(VK_FALSE)
        .setMinLod(0.0f)
        .setMaxLod(0.0f)
        .setBorderColor(vk::BorderColor::eFloatTransparentBlack);

    try {
        volumeSampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const std::exception& e) {
        SDL_Log("Failed to create volume sampler: %s", e.what());
        return false;
    }

    return true;
}

bool FroxelSystem::createDescriptorSetLayout() {
    // 0: Scattering volume (storage image)
    // 1: Integrated volume (storage image)
    // 2: Uniform buffer
    // 3: Shadow map (combined image sampler)
    // 4: Light buffer (storage buffer)
    // 5: Previous scattering volume (storage image)

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 0: Scattering volume
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 1: Integrated volume
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: Uniform buffer
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 3: Shadow map
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 4: Light buffer
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 5: Previous scattering
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create froxel descriptor set layout");
        return false;
    }
    froxelDescriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    try {
        vk::DescriptorSetLayout layouts[] = { **froxelDescriptorSetLayout_ };
        auto layoutInfo = vk::PipelineLayoutCreateInfo{}.setSetLayouts(layouts);
        froxelPipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const std::exception& e) {
        SDL_Log("Failed to create froxel pipeline layout: %s", e.what());
        return false;
    }

    return true;
}

bool FroxelSystem::createUniformBuffers() {
    return BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(sizeof(FroxelUniforms))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(uniformBuffers);
}

bool FroxelSystem::createDescriptorSets() {
    // Allocate froxel descriptor sets using managed pool
    froxelDescriptorSets = descriptorPool->allocate(**froxelDescriptorSetLayout_, framesInFlight);
    if (froxelDescriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate froxel descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, froxelDescriptorSets[i])
            .writeStorageImage(0, **scatteringVolumeViews_[0])  // Current scattering volume (write target)
            .writeStorageImage(1, **integratedVolumeView_)      // Integrated volume
            .writeBuffer(2, uniformBuffers.buffers[i], 0, sizeof(FroxelUniforms))
            .writeImage(3, shadowMapView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeBuffer(4, lightBuffers[i], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeStorageImage(5, **scatteringVolumeViews_[1])  // History scattering volume (read for temporal)
            .update();
    }

    return true;
}

bool FroxelSystem::createFroxelUpdatePipeline() {
    std::string shaderFile = shaderPath + "/froxel_update.comp.spv";
    auto shaderCode = ShaderLoader::readFile(shaderFile);
    if (!shaderCode) {
        SDL_Log("Failed to load froxel update shader: %s", shaderFile.c_str());
        return false;
    }

    auto shaderModule = ShaderLoader::createShaderModule(device, *shaderCode);
    if (!shaderModule) {
        SDL_Log("Failed to create froxel update shader module");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**froxelPipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_Log("Failed to create froxel update pipeline");
        return false;
    }
    vkDestroyShaderModule(device, *shaderModule, nullptr);
    froxelUpdatePipeline_.emplace(*raiiDevice_, rawPipeline);

    return true;
}

bool FroxelSystem::createIntegrationPipeline() {
    std::string shaderFile = shaderPath + "/froxel_integrate.comp.spv";
    auto shaderCode = ShaderLoader::readFile(shaderFile);
    if (!shaderCode) {
        SDL_Log("Failed to load froxel integration shader: %s", shaderFile.c_str());
        return false;
    }

    auto shaderModule = ShaderLoader::createShaderModule(device, *shaderCode);
    if (!shaderModule) {
        SDL_Log("Failed to create froxel integration shader module");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(**froxelPipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr, &rawPipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_Log("Failed to create froxel integration pipeline");
        return false;
    }
    vkDestroyShaderModule(device, *shaderModule, nullptr);
    integrationPipeline_.emplace(*raiiDevice_, rawPipeline);

    return true;
}

void FroxelSystem::recordFroxelUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                                       const glm::mat4& view, const glm::mat4& proj,
                                       const glm::vec3& cameraPos,
                                       const glm::vec3& sunDir, float sunIntensity,
                                       const glm::vec3& sunColor,
                                       const glm::mat4* cascadeMatrices,
                                       const glm::vec4& cascadeSplits) {
    if (!enabled) return;

    // Update uniform buffer
    glm::mat4 viewProj = proj * view;
    FroxelUniforms* ubo = static_cast<FroxelUniforms*>(uniformBuffers.mappedPointers[frameIndex]);
    ubo->invViewProj = glm::inverse(viewProj);
    ubo->prevViewProj = prevViewProj;

    // Copy cascade matrices for shadow sampling
    for (uint32_t i = 0; i < FROXEL_NUM_CASCADES; i++) {
        ubo->cascadeViewProj[i] = cascadeMatrices[i];
    }
    ubo->cascadeSplits = cascadeSplits;

    ubo->cameraPosition = glm::vec4(cameraPos, 1.0f);
    ubo->toSunDirection = glm::vec4(sunDir, sunIntensity);
    ubo->sunColor = glm::vec4(sunColor, 1.0f);
    ubo->fogParams = glm::vec4(fogBaseHeight, fogScaleHeight, fogDensity, fogAbsorption);
    ubo->layerParams = glm::vec4(layerHeight, layerThickness, layerDensity, waterLevel);
    // Disable temporal blending on the first frame to avoid sampling uninitialized history volume
    // frameCounter is 0 on first call, so first frame temporal blend should be 0
    float effectiveTemporalBlend = (frameCounter == 0) ? 0.0f : temporalBlend;
    ubo->gridParams = glm::vec4(volumetricFarPlane, DEPTH_DISTRIBUTION,
                                 static_cast<float>(frameCounter), effectiveTemporalBlend);
    ubo->shadowParams = glm::vec4(2048.0f, 0.001f, 1.0f, 0.0f);  // Shadow map size, bias, pcf radius
    // Underwater fog parameters (passed to froxel shader for underwater volumetrics)
    ubo->underwaterParams = glm::vec4(
        underwaterDensity,
        underwaterAbsorptionScale,
        underwaterColorMult,
        underwaterEnabled ? 1.0f : 0.0f
    );

    // Store for next frame's temporal reprojection
    prevViewProj = viewProj;

    // Double-buffering: determine which volume is current (write) and which is history (read)
    // frameCounter starts at 0, so frame 0 writes to volume[0], reads history from volume[1]
    uint32_t currentVolumeIdx = frameCounter % 2;
    uint32_t historyVolumeIdx = (frameCounter + 1) % 2;

    frameCounter++;

    // Update descriptor sets with correct volume bindings for this frame
    DescriptorManager::SetWriter(device, froxelDescriptorSets[frameIndex])
        .writeStorageImage(0, **scatteringVolumeViews_[currentVolumeIdx])  // Current scattering volume (write)
        .writeStorageImage(5, **scatteringVolumeViews_[historyVolumeIdx])  // History scattering volume (read)
        .update();

    // Note: frameCounter was already incremented above, so first frame is frameCounter == 1
    bool isFirstFrame = (frameCounter == 1);

    // Current scattering volume (write target) - can discard previous contents
    vk::CommandBuffer vkCmd(cmd);
    BarrierHelpers::imageToGeneral(vkCmd, scatteringVolumes_[currentVolumeIdx].get());

    // History scattering volume (read source) - preserve data from previous frame
    if (isFirstFrame) {
        // First frame: no valid history yet, clear to zero
        auto historyBarrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(scatteringVolumes_[historyVolumeIdx].get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, historyBarrier);

        auto clearValue = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
        auto clearRange = vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1);
        vkCmd.clearColorImage(scatteringVolumes_[historyVolumeIdx].get(),
                              vk::ImageLayout::eTransferDstOptimal, clearValue, clearRange);

        // Transition to GENERAL for shader access
        auto toGeneralBarrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(scatteringVolumes_[historyVolumeIdx].get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                              {}, {}, {}, toGeneralBarrier);
    } else {
        // Subsequent frames: history volume was written in previous frame
        BarrierHelpers::computeWriteToComputeRead(vkCmd, scatteringVolumes_[historyVolumeIdx].get());
    }

    // Integrated volume: transitions between GENERAL (for compute) and SHADER_READ_ONLY (for fragment)
    if (isFirstFrame) {
        // First frame: clear to zero
        auto integratedBarrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eNone)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(integratedVolume_.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, integratedBarrier);

        auto clearValueInt = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
        auto clearRangeInt = vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1);
        vkCmd.clearColorImage(integratedVolume_.get(),
                              vk::ImageLayout::eTransferDstOptimal, clearValueInt, clearRangeInt);

        // Transition to GENERAL for compute
        auto integratedToGeneralBarrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(integratedVolume_.get())
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                              {}, {}, {}, integratedToGeneralBarrier);
    } else {
        // Subsequent frames: transition from SHADER_READ_ONLY_OPTIMAL
        BarrierHelpers::shaderReadToGeneral(vkCmd, integratedVolume_.get());
    }

    // Dispatch froxel update compute shader
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **froxelUpdatePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **froxelPipelineLayout_,
                             0, vk::DescriptorSet(froxelDescriptorSets[frameIndex]), {});

    // Dispatch with 4x4x4 local size
    uint32_t groupsX = (FROXEL_WIDTH + 3) / 4;
    uint32_t groupsY = (FROXEL_HEIGHT + 3) / 4;
    uint32_t groupsZ = (FROXEL_DEPTH + 3) / 4;
    vkCmd.dispatch(groupsX, groupsY, groupsZ);

    // Barrier between update and integration - wait for current volume write
    auto memoryBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                          {}, memoryBarrier, {}, {});

    // Dispatch integration pass
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **integrationPipeline_);

    // Integration dispatches per XY column, iterating through Z
    groupsX = (FROXEL_WIDTH + 3) / 4;
    groupsY = (FROXEL_HEIGHT + 3) / 4;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Transition integrated volume to shader read for fragment sampling
    BarrierHelpers::imageToShaderRead(vkCmd, integratedVolume_.get(), vk::PipelineStageFlagBits::eFragmentShader);
}
