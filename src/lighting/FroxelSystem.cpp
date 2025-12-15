#include "FroxelSystem.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>

bool FroxelSystem::init(const InitInfo& info) {
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

bool FroxelSystem::init(const InitContext& ctx, VkImageView shadowMapView_, VkSampler shadowSampler_,
                        const std::vector<VkBuffer>& lightBuffers_) {
    device = ctx.device;
    allocator = ctx.allocator;
    descriptorPool = ctx.descriptorPool;
    extent = ctx.extent;
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;
    shadowMapView = shadowMapView_;
    shadowSampler = shadowSampler_;
    lightBuffers = lightBuffers_;

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

void FroxelSystem::destroy(VkDevice device, VmaAllocator allocator) {
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
    // Destroy both double-buffered scattering volumes
    for (int i = 0; i < 2; i++) {
        if (scatteringVolumeViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, scatteringVolumeViews[i], nullptr);
            scatteringVolumeViews[i] = VK_NULL_HANDLE;
        }
        if (scatteringVolumes[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, scatteringVolumes[i], scatteringAllocations[i]);
            scatteringVolumes[i] = VK_NULL_HANDLE;
        }
    }

    if (integratedVolumeView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, integratedVolumeView, nullptr);
        integratedVolumeView = VK_NULL_HANDLE;
    }
    if (integratedVolume != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, integratedVolume, integratedAllocation);
        integratedVolume = VK_NULL_HANDLE;
    }
}

void FroxelSystem::resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent) {
    extent = newExtent;
    // Froxel grid size is fixed, no need to recreate volumes
}

bool FroxelSystem::createScatteringVolume() {
    // Create two 3D images for double-buffered scattering data (ping-pong for temporal)
    // Format: R16G16B16A16_SFLOAT for in-scatter RGB and opacity
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {FROXEL_WIDTH, FROXEL_HEIGHT, FROXEL_DEPTH};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    // Create both buffers for ping-pong
    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                           &scatteringVolumes[i], &scatteringAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create scattering volume %d", i);
            return false;
        }

        viewInfo.image = scatteringVolumes[i];
        if (vkCreateImageView(device, &viewInfo, nullptr, &scatteringVolumeViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create scattering volume view %d", i);
            return false;
        }
    }

    return true;
}

bool FroxelSystem::createIntegratedVolume() {
    // Create 3D image for integrated scattering (front-to-back)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {FROXEL_WIDTH, FROXEL_HEIGHT, FROXEL_DEPTH};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &integratedVolume, &integratedAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create integrated volume");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = integratedVolume;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &integratedVolumeView) != VK_SUCCESS) {
        SDL_Log("Failed to create integrated volume view");
        return false;
    }

    return true;
}

bool FroxelSystem::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    if (!ManagedSampler::create(device, samplerInfo, volumeSampler)) {
        SDL_Log("Failed to create volume sampler");
        return false;
    }

    return true;
}

bool FroxelSystem::createDescriptorSetLayout() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = binding;
        b.descriptorType = type;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        return b;
    };

    std::array<VkDescriptorSetLayoutBinding, 6> bindings = {
        makeComputeBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        makeComputeBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        makeComputeBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        makeComputeBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
        makeComputeBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
        makeComputeBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (!ManagedDescriptorSetLayout::create(device, layoutInfo, froxelDescriptorSetLayout)) {
        SDL_Log("Failed to create froxel descriptor set layout");
        return false;
    }

    // Create pipeline layout
    VkDescriptorSetLayout rawLayout = froxelDescriptorSetLayout.get();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, froxelPipelineLayout)) {
        SDL_Log("Failed to create froxel pipeline layout");
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
    froxelDescriptorSets = descriptorPool->allocate(froxelDescriptorSetLayout.get(), framesInFlight);
    if (froxelDescriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate froxel descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, froxelDescriptorSets[i])
            .writeStorageImage(0, scatteringVolumeViews[0])  // Current scattering volume (write target)
            .writeStorageImage(1, integratedVolumeView)      // Integrated volume
            .writeBuffer(2, uniformBuffers.buffers[i], 0, sizeof(FroxelUniforms))
            .writeImage(3, shadowMapView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeBuffer(4, lightBuffers[i], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeStorageImage(5, scatteringVolumeViews[1])  // History scattering volume (read for temporal)
            .update();
    }

    return true;
}

bool FroxelSystem::createFroxelUpdatePipeline() {
    std::string shaderFile = shaderPath + "/froxel_update.comp.spv";
    auto shaderCode = ShaderLoader::readFile(shaderFile);
    if (shaderCode.empty()) {
        SDL_Log("Failed to load froxel update shader: %s", shaderFile.c_str());
        return false;
    }

    VkShaderModule shaderModule = ShaderLoader::createShaderModule(device, shaderCode);
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_Log("Failed to create froxel update shader module");
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = froxelPipelineLayout.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, froxelUpdatePipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (!success) {
        SDL_Log("Failed to create froxel update pipeline");
        return false;
    }

    return true;
}

bool FroxelSystem::createIntegrationPipeline() {
    std::string shaderFile = shaderPath + "/froxel_integrate.comp.spv";
    auto shaderCode = ShaderLoader::readFile(shaderFile);
    if (shaderCode.empty()) {
        SDL_Log("Failed to load froxel integration shader: %s", shaderFile.c_str());
        return false;
    }

    VkShaderModule shaderModule = ShaderLoader::createShaderModule(device, shaderCode);
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_Log("Failed to create froxel integration shader module");
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = froxelPipelineLayout.get();

    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, integrationPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

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
    ubo->sunDirection = glm::vec4(sunDir, sunIntensity);
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
        .writeStorageImage(0, scatteringVolumeViews[currentVolumeIdx])  // Current scattering volume (write)
        .writeStorageImage(5, scatteringVolumeViews[historyVolumeIdx])  // History scattering volume (read)
        .update();

    // Note: frameCounter was already incremented above, so first frame is frameCounter == 1
    bool isFirstFrame = (frameCounter == 1);

    // Current scattering volume (write target) - can discard previous contents
    Barriers::prepareImageForCompute(cmd, scatteringVolumes[currentVolumeIdx]);

    // History scattering volume (read source) - preserve data from previous frame
    if (isFirstFrame) {
        // First frame: no valid history yet, clear to zero
        Barriers::prepareImageForTransferDst(cmd, scatteringVolumes[historyVolumeIdx]);

        VkClearColorValue clearValue = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange clearRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, scatteringVolumes[historyVolumeIdx],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &clearRange);

        // Transition to GENERAL for shader access
        Barriers::transitionImage(cmd, scatteringVolumes[historyVolumeIdx],
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    } else {
        // Subsequent frames: history volume was written in previous frame
        Barriers::transitionImage(cmd, scatteringVolumes[historyVolumeIdx],
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Integrated volume: transitions between GENERAL (for compute) and SHADER_READ_ONLY (for fragment)
    if (isFirstFrame) {
        // First frame: clear to zero
        Barriers::prepareImageForTransferDst(cmd, integratedVolume);

        VkClearColorValue clearValue = {{0.0f, 0.0f, 0.0f, 0.0f}};
        VkImageSubresourceRange clearRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, integratedVolume,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &clearRange);

        // Transition to GENERAL for compute
        Barriers::transitionImage(cmd, integratedVolume,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    } else {
        // Subsequent frames: transition from SHADER_READ_ONLY_OPTIMAL
        Barriers::transitionImage(cmd, integratedVolume,
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
    Barriers::imageComputeToSampling(cmd, integratedVolume);
}
