#include "WaterDisplacement.h"
#include "ShaderLoader.h"
#include "VmaResources.h"
#include "DescriptorManager.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <cstring>

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
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement requires raiiDevice");
        return false;
    }

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
    descriptorSetLayout_.reset();
    computePipeline_.reset();
    computePipelineLayout_.reset();

    // Destroy particle buffers (RAII-managed)
    particleBuffers_.clear();
    particleMapped.clear();

    // RAII-managed sampler
    sampler_.reset();

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
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16Sfloat)
            .setExtent(vk::Extent3D{displacementResolution, displacementResolution, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &displacementMap, &displacementAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(displacementMap)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &displacementMapView) != VK_SUCCESS) {
            return false;
        }
    }

    // Create previous frame displacement map (for temporal blending)
    {
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16Sfloat)
            .setExtent(vk::Extent3D{displacementResolution, displacementResolution, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &prevDisplacementMap, &prevDisplacementAllocation, nullptr) != VK_SUCCESS) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(prevDisplacementMap)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &prevDisplacementMapView) != VK_SUCCESS) {
            return false;
        }
    }

    // Create sampler using factory
    sampler_ = SamplerFactory::createSamplerLinearClampLimitedMip(*raiiDevice_, 0.0f);
    if (!sampler_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create displacement sampler");
        return false;
    }
    return true;
}

bool WaterDisplacement::createParticleBuffer() {
    particleBuffers_.resize(framesInFlight);
    particleMapped.resize(framesInFlight);

    VkDeviceSize bufferSize = sizeof(SplashParticle) * MAX_PARTICLES;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (!VmaBufferFactory::createStorageBufferHostReadable(allocator, bufferSize, particleBuffers_[i])) {
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

    VkDescriptorSetLayout rawLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &rawLayout) != VK_SUCCESS) {
        return false;
    }
    descriptorSetLayout_.emplace(*raiiDevice_, rawLayout);

    // Pipeline layout using builder
    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descriptorSetLayout_)
            .addPushConstantRange<DisplacementPushConstants>(vk::ShaderStageFlagBits::eCompute)
            .buildInto(computePipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create displacement pipeline layout");
        return false;
    }

    // Load compute shader
    auto shaderModule = ShaderLoader::loadShaderModule(device, "shaders/water_displacement.comp.spv");
    if (!shaderModule) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Compute shader not found, using fallback");
        // Create a simple pass-through pipeline or return true to allow system to work without splashes
        return true;
    }

    auto shaderStage = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(shaderStage)
        .setLayout(**computePipelineLayout_);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
        reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo), nullptr, &rawPipeline);

    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (result == VK_SUCCESS) {
        computePipeline_.emplace(*raiiDevice_, rawPipeline);
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
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, **descriptorSetLayout_);

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
            .writeImage(1, prevDisplacementMapView, **sampler_)
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
    if (!computePipeline_) return;

    // Update particle buffer
    updateParticleBuffer(frameIndex);

    // Transition displacement map to general layout for compute write
    vk::CommandBuffer vkCmd(cmd);
    BarrierHelpers::imageToGeneral(vkCmd, displacementMap);

    // Bind compute pipeline
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, **computePipelineLayout_,
                             0, vk::DescriptorSet(descriptorSets[frameIndex]), {});

    // Push constants
    DisplacementPushConstants pushConstants{};
    pushConstants.worldExtent = glm::vec4(worldCenter, glm::vec2(worldSize));
    pushConstants.time = currentTime;
    pushConstants.deltaTime = 1.0f / 60.0f;  // Assume 60fps for now
    pushConstants.numParticles = static_cast<uint32_t>(particles.size());
    pushConstants.decayRate = decayRate;

    vkCmd.pushConstants<DisplacementPushConstants>(**computePipelineLayout_,
                                                    vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

    // Dispatch compute shader
    uint32_t groupSize = 16;
    uint32_t groupsX = (displacementResolution + groupSize - 1) / groupSize;
    uint32_t groupsY = (displacementResolution + groupSize - 1) / groupSize;
    vkCmd.dispatch(groupsX, groupsY, 1);

    // Transition to shader read for water shader sampling
    BarrierHelpers::imageToShaderRead(vkCmd, displacementMap,
        vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader);
}

void WaterDisplacement::setWorldExtent(const glm::vec2& center, const glm::vec2& size) {
    worldCenter = center;
    worldSize = std::max(size.x, size.y);
}

void WaterDisplacement::clear() {
    particles.clear();
    currentTime = 0.0f;
}
