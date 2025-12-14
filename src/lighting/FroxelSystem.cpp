#include "FroxelSystem.h"
#include "ShaderLoader.h"
#include "BindingBuilder.h"
#include "VulkanBarriers.h"
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

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
    }
    uniformBuffers.clear();
    uniformAllocations.clear();
    uniformMappedPtrs.clear();

    if (froxelUpdatePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, froxelUpdatePipeline, nullptr);
        froxelUpdatePipeline = VK_NULL_HANDLE;
    }

    if (integrationPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, integrationPipeline, nullptr);
        integrationPipeline = VK_NULL_HANDLE;
    }

    if (froxelPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, froxelPipelineLayout, nullptr);
        froxelPipelineLayout = VK_NULL_HANDLE;
    }

    if (froxelDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, froxelDescriptorSetLayout, nullptr);
        froxelDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (volumeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, volumeSampler, nullptr);
        volumeSampler = VK_NULL_HANDLE;
    }
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

    if (vkCreateSampler(device, &samplerInfo, nullptr, &volumeSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create volume sampler");
        return false;
    }

    return true;
}

bool FroxelSystem::createDescriptorSetLayout() {
    auto makeComputeBinding = [](uint32_t binding, VkDescriptorType type) {
        return BindingBuilder()
            .setBinding(binding)
            .setDescriptorType(type)
            .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
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

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &froxelDescriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create froxel descriptor set layout");
        return false;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &froxelDescriptorSetLayout;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &froxelPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create froxel pipeline layout");
        return false;
    }

    return true;
}

bool FroxelSystem::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(FroxelUniforms);

    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocResult{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                            &uniformBuffers[i], &uniformAllocations[i], &allocResult) != VK_SUCCESS) {
            SDL_Log("Failed to create froxel uniform buffer");
            return false;
        }

        uniformMappedPtrs[i] = allocResult.pMappedData;
    }

    return true;
}

bool FroxelSystem::createDescriptorSets() {
    // Allocate froxel descriptor sets using managed pool
    froxelDescriptorSets = descriptorPool->allocate(froxelDescriptorSetLayout, framesInFlight);
    if (froxelDescriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate froxel descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 6> writes{};

        // Current scattering volume (write target) - initially volume 0
        VkDescriptorImageInfo scatteringInfo{};
        scatteringInfo.imageView = scatteringVolumeViews[0];
        scatteringInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = froxelDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].pImageInfo = &scatteringInfo;

        // Integrated volume
        VkDescriptorImageInfo integratedInfo{};
        integratedInfo.imageView = integratedVolumeView;
        integratedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = froxelDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &integratedInfo;

        // Uniform buffer
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(FroxelUniforms);

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = froxelDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorCount = 1;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].pBufferInfo = &bufferInfo;

        // Shadow map
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.sampler = shadowSampler;
        shadowInfo.imageView = shadowMapView;
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = froxelDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorCount = 1;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].pImageInfo = &shadowInfo;

        // Light buffer (SSBO)
        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = lightBuffers[i];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = VK_WHOLE_SIZE;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = froxelDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].dstArrayElement = 0;
        writes[4].descriptorCount = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].pBufferInfo = &lightBufferInfo;

        // History scattering volume (read for temporal) - initially volume 1
        VkDescriptorImageInfo historyInfo{};
        historyInfo.imageView = scatteringVolumeViews[1];
        historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = froxelDescriptorSets[i];
        writes[5].dstBinding = 5;
        writes[5].dstArrayElement = 0;
        writes[5].descriptorCount = 1;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[5].pImageInfo = &historyInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
    pipelineInfo.layout = froxelPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &froxelUpdatePipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
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
    pipelineInfo.layout = froxelPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &integrationPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
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
    FroxelUniforms* ubo = static_cast<FroxelUniforms*>(uniformMappedPtrs[frameIndex]);
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
    std::array<VkWriteDescriptorSet, 2> volumeWrites{};
    VkDescriptorImageInfo currentInfo{};
    currentInfo.imageView = scatteringVolumeViews[currentVolumeIdx];
    currentInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    volumeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    volumeWrites[0].dstSet = froxelDescriptorSets[frameIndex];
    volumeWrites[0].dstBinding = 0;  // Current scattering volume (write)
    volumeWrites[0].dstArrayElement = 0;
    volumeWrites[0].descriptorCount = 1;
    volumeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    volumeWrites[0].pImageInfo = &currentInfo;

    VkDescriptorImageInfo historyInfo{};
    historyInfo.imageView = scatteringVolumeViews[historyVolumeIdx];
    historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    volumeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    volumeWrites[1].dstSet = froxelDescriptorSets[frameIndex];
    volumeWrites[1].dstBinding = 5;  // History scattering volume (read)
    volumeWrites[1].dstArrayElement = 0;
    volumeWrites[1].descriptorCount = 1;
    volumeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    volumeWrites[1].pImageInfo = &historyInfo;

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(volumeWrites.size()), volumeWrites.data(), 0, nullptr);

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, froxelUpdatePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, froxelPipelineLayout,
                            0, 1, &froxelDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch with 4x4x4 local size
    uint32_t groupsX = (FROXEL_WIDTH + 3) / 4;
    uint32_t groupsY = (FROXEL_HEIGHT + 3) / 4;
    uint32_t groupsZ = (FROXEL_DEPTH + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, groupsZ);

    // Barrier between update and integration - wait for current volume write
    Barriers::computeToCompute(cmd);

    // Dispatch integration pass
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, integrationPipeline);

    // Integration dispatches per XY column, iterating through Z
    groupsX = (FROXEL_WIDTH + 3) / 4;
    groupsY = (FROXEL_HEIGHT + 3) / 4;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition integrated volume to shader read for fragment sampling
    Barriers::imageComputeToSampling(cmd, integratedVolume);
}
