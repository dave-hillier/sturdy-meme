#include "WaterDisplacement.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cstring>

using namespace vk;

std::unique_ptr<WaterDisplacement> WaterDisplacement::create(const InitInfo& info) {
    std::unique_ptr<WaterDisplacement> system(new WaterDisplacement());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

WaterDisplacement::~WaterDisplacement() {
    cleanup();
}

bool WaterDisplacement::initInternal(const InitInfo& info) {
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

void WaterDisplacement::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device);

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }

    // RAII wrappers handle cleanup automatically - just reset them
    descriptorSetLayout = ManagedDescriptorSetLayout();
    computePipeline = ManagedPipeline();
    computePipelineLayout = ManagedPipelineLayout();

    // Destroy particle buffers (RAII-managed)
    particleBuffers_.clear();
    particleMapped.clear();

    // RAII-managed sampler
    sampler = ManagedSampler();

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
        ImageCreateInfo imageInfo{
            {},                                  // flags
            ImageType::e2D,
            Format::eR16Sfloat,
            Extent3D{displacementResolution, displacementResolution, 1},
            1, 1,                                // mipLevels, arrayLayers
            SampleCountFlagBits::e1,
            ImageTiling::eOptimal,
            ImageUsageFlagBits::eStorage | ImageUsageFlagBits::eSampled | ImageUsageFlagBits::eTransferDst,
            SharingMode::eExclusive,
            0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
            ImageLayout::eUndefined
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &displacementMap, &displacementAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        ImageViewCreateInfo viewInfo{
            {},                                  // flags
            displacementMap,
            ImageViewType::e2D,
            Format::eR16Sfloat,
            ComponentMapping{},                  // identity swizzle
            ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &displacementMapView) != VK_SUCCESS) {
            return false;
        }
    }

    // Create previous frame displacement map (for temporal blending)
    {
        ImageCreateInfo imageInfo{
            {},                                  // flags
            ImageType::e2D,
            Format::eR16Sfloat,
            Extent3D{displacementResolution, displacementResolution, 1},
            1, 1,                                // mipLevels, arrayLayers
            SampleCountFlagBits::e1,
            ImageTiling::eOptimal,
            ImageUsageFlagBits::eStorage | ImageUsageFlagBits::eSampled | ImageUsageFlagBits::eTransferDst,
            SharingMode::eExclusive,
            0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
            ImageLayout::eUndefined
        };

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &prevDisplacementMap, &prevDisplacementAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        ImageViewCreateInfo viewInfo{
            {},                                  // flags
            prevDisplacementMap,
            ImageViewType::e2D,
            Format::eR16Sfloat,
            ComponentMapping{},                  // identity swizzle
            ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(device, &vkViewInfo, nullptr, &prevDisplacementMapView) != VK_SUCCESS) {
            return false;
        }
    }

    // Create sampler
    SamplerCreateInfo samplerInfo{
        {},                                  // flags
        Filter::eLinear,                     // magFilter
        Filter::eLinear,                     // minFilter
        SamplerMipmapMode::eNearest,
        SamplerAddressMode::eClampToEdge,    // addressModeU
        SamplerAddressMode::eClampToEdge,    // addressModeV
        SamplerAddressMode::eClampToEdge,    // addressModeW
        0.0f,                                // mipLodBias
        VK_FALSE,                            // anisotropyEnable
        1.0f,                                // maxAnisotropy
        VK_FALSE,                            // compareEnable
        {},                                  // compareOp
        0.0f,                                // minLod
        0.0f,                                // maxLod
        BorderColor::eFloatTransparentBlack
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, sampler);
}

bool WaterDisplacement::createParticleBuffer() {
    particleBuffers_.resize(framesInFlight);
    particleMapped.resize(framesInFlight);

    VkDeviceSize bufferSize = sizeof(SplashParticle) * MAX_PARTICLES;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (!VulkanResourceFactory::createStorageBufferHostReadable(allocator, bufferSize, particleBuffers_[i])) {
            return false;
        }

        particleMapped[i] = particleBuffers_[i].map();

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

    if (!ManagedDescriptorSetLayout::create(device, layoutInfo, descriptorSetLayout)) {
        return false;
    }

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(DisplacementPushConstants);

    // Pipeline layout
    VkDescriptorSetLayout rawLayout = descriptorSetLayout.get();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &rawLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (!ManagedPipelineLayout::create(device, pipelineLayoutInfo, computePipelineLayout)) {
        return false;
    }

    // Load compute shader
    auto shaderModule = ShaderLoader::loadShaderModule(device, "shaders/water_displacement.comp.spv");
    if (!shaderModule) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Compute shader not found, using fallback");
        // Create a simple pass-through pipeline or return true to allow system to work without splashes
        return true;
    }

    PipelineShaderStageCreateInfo shaderStage{
        {},                              // flags
        ShaderStageFlagBits::eCompute,
        *shaderModule,
        "main"
    };

    ComputePipelineCreateInfo pipelineInfo{
        {},                              // flags
        shaderStage,
        computePipelineLayout.get()
    };

    auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &vkPipelineInfo, nullptr, &rawPipeline);

    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (result == VK_SUCCESS) {
        computePipeline = ManagedPipeline::fromRaw(device, rawPipeline);
    }

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
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout.get());

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
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeStorageImage(0, displacementMapView)
            .writeImage(1, prevDisplacementMapView, sampler.get())
            .writeBuffer(2, particleBuffers_[i].get(), 0, sizeof(SplashParticle) * MAX_PARTICLES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
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
    if (!computePipeline.get()) return;

    // Update particle buffer
    updateParticleBuffer(frameIndex);

    // Transition displacement map to general layout for compute write
    Barriers::prepareImageForCompute(cmd, displacementMap);

    // Bind compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelineLayout.get(),
                           0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Push constants
    DisplacementPushConstants pushConstants{};
    pushConstants.worldExtent = glm::vec4(worldCenter, glm::vec2(worldSize));
    pushConstants.time = currentTime;
    pushConstants.deltaTime = 1.0f / 60.0f;  // Assume 60fps for now
    pushConstants.numParticles = static_cast<uint32_t>(particles.size());
    pushConstants.decayRate = decayRate;

    vkCmdPushConstants(cmd, computePipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
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
