#include "WaterDisplacement.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>

bool WaterDisplacement::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    commandPool = info.commandPool;
    computeQueue = info.computeQueue;
    framesInFlight = info.framesInFlight;
    displacementResolution = info.displacementResolution;
    worldSize = info.worldSize;

    SDL_Log("WaterDisplacement: Initializing with %dx%d resolution, %.1f world size",
            displacementResolution, displacementResolution, worldSize);

    if (!createDisplacementMap()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Failed to create displacement map");
        return false;
    }

    if (!createParticleBuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Failed to create particle buffer");
        return false;
    }

    if (!createComputePipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Failed to create compute pipeline");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("WaterDisplacement: Initialized successfully");
    return true;
}

void WaterDisplacement::destroy() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    // Destroy compute pipeline
    if (computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }

    if (computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, computePipelineLayout, nullptr);
        computePipelineLayout = VK_NULL_HANDLE;
    }

    // Destroy particle buffers
    for (size_t i = 0; i < particleBuffers.size(); i++) {
        if (particleBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, particleBuffers[i], particleAllocations[i]);
        }
    }
    particleBuffers.clear();
    particleAllocations.clear();
    particleMapped.clear();

    // Destroy sampler
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

    // Destroy displacement maps
    if (displacementMapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, displacementMapView, nullptr);
        displacementMapView = VK_NULL_HANDLE;
    }
    if (displacementMap != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, displacementMap, displacementAllocation);
        displacementMap = VK_NULL_HANDLE;
        displacementAllocation = VK_NULL_HANDLE;
    }

    if (prevDisplacementMapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, prevDisplacementMapView, nullptr);
        prevDisplacementMapView = VK_NULL_HANDLE;
    }
    if (prevDisplacementMap != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, prevDisplacementMap, prevDisplacementAllocation);
        prevDisplacementMap = VK_NULL_HANDLE;
        prevDisplacementAllocation = VK_NULL_HANDLE;
    }

    SDL_Log("WaterDisplacement: Destroyed");
}

bool WaterDisplacement::createDisplacementMap() {
    // Create current displacement map
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16_SFLOAT;
        imageInfo.extent = {displacementResolution, displacementResolution, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &displacementMap, &displacementAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = displacementMap;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &displacementMapView) != VK_SUCCESS) {
            return false;
        }
    }

    // Create previous frame displacement map (for temporal blending)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R16_SFLOAT;
        imageInfo.extent = {displacementResolution, displacementResolution, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &prevDisplacementMap, &prevDisplacementAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = prevDisplacementMap;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &prevDisplacementMapView) != VK_SUCCESS) {
            return false;
        }
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool WaterDisplacement::createParticleBuffer() {
    particleBuffers.resize(framesInFlight);
    particleAllocations.resize(framesInFlight);
    particleMapped.resize(framesInFlight);

    VkDeviceSize bufferSize = sizeof(SplashParticle) * MAX_PARTICLES;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &particleBuffers[i], &particleAllocations[i], &allocationInfo) != VK_SUCCESS) {
            return false;
        }

        particleMapped[i] = allocationInfo.pMappedData;

        // Initialize to zero
        memset(particleMapped[i], 0, bufferSize);
    }

    return true;
}

bool WaterDisplacement::createComputePipeline() {
    // Descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // Binding 0: Current displacement map (storage image, write)
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Previous displacement map (sampled image, read)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Particle buffer (SSBO)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(DisplacementPushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &computePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Load compute shader
    auto shaderModule = ShaderLoader::loadShaderModule(device, "shaders/water_displacement.comp.spv");
    if (shaderModule == VK_NULL_HANDLE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Compute shader not found, using fallback");
        // Create a simple pass-through pipeline or return true to allow system to work without splashes
        return true;
    }

    VkPipelineShaderStageCreateInfo shaderStage{};
    shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStage.module = shaderModule;
    shaderStage.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStage;
    pipelineInfo.layout = computePipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);

    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

bool WaterDisplacement::createDescriptorSets() {
    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = framesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = framesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = framesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = framesInFlight;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        return false;
    }

    // Allocate descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(framesInFlight);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        return false;
    }

    // Update descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        std::array<VkWriteDescriptorSet, 3> writes{};

        // Current displacement map (storage image)
        VkDescriptorImageInfo currentImageInfo{};
        currentImageInfo.imageView = displacementMapView;
        currentImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &currentImageInfo;

        // Previous displacement map (sampled)
        VkDescriptorImageInfo prevImageInfo{};
        prevImageInfo.sampler = sampler;
        prevImageInfo.imageView = prevDisplacementMapView;
        prevImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &prevImageInfo;

        // Particle buffer
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = particleBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(SplashParticle) * MAX_PARTICLES;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

void WaterDisplacement::addSplash(const glm::vec3& position, float radius, float intensity, float lifetime) {
    if (particles.size() >= MAX_PARTICLES) {
        // Remove oldest particle
        particles.erase(particles.begin());
    }

    SplashParticle particle{};
    particle.position = position;
    particle.radius = radius;
    particle.intensity = intensity;
    particle.age = 0.0f;
    particle.lifetime = lifetime;
    particle.falloff = 2.0f;  // Quadratic falloff
    particle.animFrame = 0;

    particles.push_back(particle);

    SDL_Log("WaterDisplacement: Added splash at (%.1f, %.1f, %.1f) radius=%.1f intensity=%.2f",
            position.x, position.y, position.z, radius, intensity);
}

void WaterDisplacement::addRipple(const glm::vec3& position, float radius, float intensity, float speed) {
    // Ripples are implemented as splashes with negative intensity (creates ring pattern in shader)
    addSplash(position, radius, -intensity, radius / speed);
}

void WaterDisplacement::update(float deltaTime) {
    currentTime += deltaTime;

    // Update particle ages and remove dead particles
    for (auto it = particles.begin(); it != particles.end(); ) {
        it->age += deltaTime / it->lifetime;

        if (it->age >= 1.0f) {
            it = particles.erase(it);
        } else {
            ++it;
        }
    }
}

void WaterDisplacement::updateParticleBuffer(uint32_t frameIndex) {
    if (particleMapped[frameIndex] == nullptr) return;

    // Copy particles to GPU buffer
    size_t copySize = std::min(particles.size(), static_cast<size_t>(MAX_PARTICLES)) * sizeof(SplashParticle);
    if (copySize > 0) {
        memcpy(particleMapped[frameIndex], particles.data(), copySize);
    }

    // Zero out remaining slots
    if (particles.size() < MAX_PARTICLES) {
        size_t remainingSize = (MAX_PARTICLES - particles.size()) * sizeof(SplashParticle);
        memset(static_cast<char*>(particleMapped[frameIndex]) + copySize, 0, remainingSize);
    }
}

void WaterDisplacement::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (computePipeline == VK_NULL_HANDLE) return;

    // Update particle buffer
    updateParticleBuffer(frameIndex);

    // Transition displacement map to general layout for compute write
    Barriers::prepareImageForCompute(cmd, displacementMap);

    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout,
                           0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Push constants
    DisplacementPushConstants pushConstants{};
    pushConstants.worldExtent = glm::vec4(worldCenter, glm::vec2(worldSize));
    pushConstants.time = currentTime;
    pushConstants.deltaTime = 1.0f / 60.0f;  // Assume 60fps for now
    pushConstants.numParticles = static_cast<uint32_t>(particles.size());
    pushConstants.decayRate = decayRate;

    vkCmdPushConstants(cmd, computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(pushConstants), &pushConstants);

    // Dispatch compute shader
    uint32_t groupSize = 16;
    uint32_t groupsX = (displacementResolution + groupSize - 1) / groupSize;
    uint32_t groupsY = (displacementResolution + groupSize - 1) / groupSize;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition to shader read for water shader sampling
    Barriers::imageComputeToSampling(cmd, displacementMap,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void WaterDisplacement::setWorldExtent(const glm::vec2& center, const glm::vec2& size) {
    worldCenter = center;
    worldSize = std::max(size.x, size.y);
}

void WaterDisplacement::clear() {
    particles.clear();
    currentTime = 0.0f;
}
