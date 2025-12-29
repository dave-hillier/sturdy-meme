#include "FroxelSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
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
    froxelUpdatePipeline = ManagedPipeline();
    integrationPipeline = ManagedPipeline();
    froxelPipelineLayout = ManagedPipelineLayout();
    froxelDescriptorSetLayout = ManagedDescriptorSetLayout();
    volumeSampler = ManagedSampler();
}

void FroxelSystem::destroyVolumeResources() {
    // RAII wrappers handle cleanup automatically
    for (int i = 0; i < 2; i++) {
        scatteringVolumeViews_[i].reset();
        scatteringVolumes_[i].reset();
    }

    integratedVolumeView_.reset();
    integratedVolume_.reset();
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

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setViewType(vk::ImageViewType::e3D)
        .setFormat(vk::Format::eR16G16B16A16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    // Create both buffers for ping-pong
    for (int i = 0; i < 2; i++) {
        if (!ManagedImage::create(allocator, *reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
                                  allocInfo, scatteringVolumes_[i])) {
            SDL_Log("Failed to create scattering volume %d", i);
            return false;
        }

        viewInfo.setImage(scatteringVolumes_[i].get());
        if (!ManagedImageView::create(device, *reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                                      scatteringVolumeViews_[i])) {
            SDL_Log("Failed to create scattering volume view %d", i);
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

    if (!ManagedImageView::create(device, *reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo),
                                  integratedVolumeView_)) {
        SDL_Log("Failed to create integrated volume view");
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

    if (!ManagedSampler::create(device, *reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo),
                                volumeSampler)) {
        SDL_Log("Failed to create volume sampler");
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
    froxelDescriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, rawLayout);

    VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, rawLayout);
    if (rawPipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create froxel pipeline layout");
        return false;
    }
    froxelPipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

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
    froxelDescriptorSets = descriptorPool->allocate(froxelDescriptorSetLayout.get(), framesInFlight);
    if (froxelDescriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate froxel descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, froxelDescriptorSets[i])
            .writeStorageImage(0, scatteringVolumeViews_[0].get())  // Current scattering volume (write target)
            .writeStorageImage(1, integratedVolumeView_.get())      // Integrated volume
            .writeBuffer(2, uniformBuffers.buffers[i], 0, sizeof(FroxelUniforms))
            .writeImage(3, shadowMapView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeBuffer(4, lightBuffers[i], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeStorageImage(5, scatteringVolumeViews_[1].get())  // History scattering volume (read for temporal)
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
        .setLayout(froxelPipelineLayout.get());

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
        *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), froxelUpdatePipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (!success) {
        SDL_Log("Failed to create froxel update pipeline");
        return false;
    }

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
        .setLayout(froxelPipelineLayout.get());

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE,
        *reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), integrationPipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (!success) {
        SDL_Log("Failed to create froxel integration pipeline");
        return false;
    }

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
    ubo->layerParams = glm::vec4(layerHeight, layerThickness, layerDensity, 0.0f);
    // Disable temporal blending on the first frame to avoid sampling uninitialized history volume
    // frameCounter is 0 on first call, so first frame temporal blend should be 0
    float effectiveTemporalBlend = (frameCounter == 0) ? 0.0f : temporalBlend;
    ubo->gridParams = glm::vec4(volumetricFarPlane, DEPTH_DISTRIBUTION,
                                 static_cast<float>(frameCounter), effectiveTemporalBlend);
    ubo->shadowParams = glm::vec4(2048.0f, 0.001f, 1.0f, 0.0f);  // Shadow map size, bias, pcf radius

    // Store for next frame's temporal reprojection
    prevViewProj = viewProj;

    // Double-buffering: determine which volume is current (write) and which is history (read)
    // frameCounter starts at 0, so frame 0 writes to volume[0], reads history from volume[1]
    uint32_t currentVolumeIdx = frameCounter % 2;
    uint32_t historyVolumeIdx = (frameCounter + 1) % 2;

    frameCounter++;

    // Update descriptor sets with correct volume bindings for this frame
    DescriptorManager::SetWriter(device, froxelDescriptorSets[frameIndex])
        .writeStorageImage(0, scatteringVolumeViews_[currentVolumeIdx].get())  // Current scattering volume (write)
        .writeStorageImage(5, scatteringVolumeViews_[historyVolumeIdx].get())  // History scattering volume (read)
        .update();

    // Note: frameCounter was already incremented above, so first frame is frameCounter == 1
    bool isFirstFrame = (frameCounter == 1);

    // Current scattering volume (write target) - can discard previous contents
    Barriers::prepareImageForCompute(cmd, scatteringVolumes_[currentVolumeIdx].get());

    // History scattering volume (read source) - preserve data from previous frame
    if (isFirstFrame) {
        // First frame: no valid history yet, clear to zero
        Barriers::prepareImageForTransferDst(cmd, scatteringVolumes_[historyVolumeIdx].get());

        VkClearColorValue clearValue = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange clearRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, scatteringVolumes_[historyVolumeIdx].get(),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &clearRange);

        // Transition to GENERAL for shader access
        Barriers::transitionImage(cmd, scatteringVolumes_[historyVolumeIdx].get(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        // Subsequent frames: history volume was written in previous frame
        Barriers::transitionImage(cmd, scatteringVolumes_[historyVolumeIdx].get(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Integrated volume: transitions between GENERAL (for compute) and SHADER_READ_ONLY (for fragment)
    if (isFirstFrame) {
        // First frame: clear to zero
        Barriers::prepareImageForTransferDst(cmd, integratedVolume_.get());

        VkClearColorValue clearValue = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange clearRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, integratedVolume_.get(),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &clearRange);

        // Transition to GENERAL for compute
        Barriers::transitionImage(cmd, integratedVolume_.get(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    } else {
        // Subsequent frames: transition from SHADER_READ_ONLY_OPTIMAL
        Barriers::transitionImage(cmd, integratedVolume_.get(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    }

    // Dispatch froxel update compute shader
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, froxelUpdatePipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, froxelPipelineLayout.get(),
                            0, 1, &froxelDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch with 4x4x4 local size
    uint32_t groupsX = (FROXEL_WIDTH + 3) / 4;
    uint32_t groupsY = (FROXEL_HEIGHT + 3) / 4;
    uint32_t groupsZ = (FROXEL_DEPTH + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);

    // Barrier between update and integration - wait for current volume write
    Barriers::computeToCompute(cmd);

    // Dispatch integration pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, integrationPipeline.get());

    // Integration dispatches per XY column, iterating through Z
    groupsX = (FROXEL_WIDTH + 3) / 4;
    groupsY = (FROXEL_HEIGHT + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition integrated volume to shader read for fragment sampling
    Barriers::imageComputeToSampling(cmd, integratedVolume_.get());
}
