#include "WaterDisplacement.h"
#include "ShaderLoader.h"
#include "VmaBufferFactory.h"
#include "SamplerFactory.h"
#include "DescriptorManager.h"
#include "core/pipeline/ComputePipelineBuilder.h"
#include "core/vulkan/PipelineLayoutBuilder.h"
#include "core/vulkan/DescriptorSetLayoutBuilder.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <algorithm>
#include <cstring>

std::unique_ptr<WaterDisplacement> WaterDisplacement::create(const InitInfo& info) {
    auto system = std::make_unique<WaterDisplacement>(ConstructToken{});
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
    if (!raiiDevice_) return;

    raiiDevice_->waitIdle();

    // RAII wrappers handle cleanup automatically - just reset them
    descriptorPool_.reset();
    descriptorSetLayout_.reset();
    computePipeline_.reset();
    computePipelineLayout_.reset();

    // Destroy particle buffers (RAII-managed)
    particleBuffers_.clear();
    particleMapped.clear();

    // RAII-managed sampler
    sampler_.reset();

    // Destroy displacement maps (RAII-managed image views)
    displacementMapView_.reset();
    if (displacementMap != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, displacementMap, displacementAllocation);
        displacementMap = VK_NULL_HANDLE;
        displacementAllocation = VK_NULL_HANDLE;
    }

    prevDisplacementMapView_.reset();
    if (prevDisplacementMap != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, prevDisplacementMap, prevDisplacementAllocation);
        prevDisplacementMap = VK_NULL_HANDLE;
        prevDisplacementAllocation = VK_NULL_HANDLE;
    }

    device = VK_NULL_HANDLE;
    raiiDevice_ = nullptr;
    SDL_Log("WaterDisplacement: Destroyed");
}

bool WaterDisplacement::createDisplacementMap() {
    // Create current displacement map
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(displacementResolution, displacementResolution)
                .setFormat(VK_FORMAT_R16_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .setGpuOnly()
                .build(*raiiDevice_, image, displacementMapView_)) {
            return false;
        }
        image.releaseToRaw(displacementMap, displacementAllocation);
    }

    // Create previous frame displacement map (for temporal blending)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(displacementResolution, displacementResolution)
                .setFormat(VK_FORMAT_R16_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .setGpuOnly()
                .build(*raiiDevice_, image, prevDisplacementMapView_)) {
            return false;
        }
        image.releaseToRaw(prevDisplacementMap, prevDisplacementAllocation);
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
    if (!DescriptorSetLayoutBuilder()
            .addBinding(BindingBuilder::storageImage(0, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::combinedImageSampler(1, vk::ShaderStageFlagBits::eCompute))
            .addBinding(BindingBuilder::storageBuffer(2, vk::ShaderStageFlagBits::eCompute))
            .buildInto(*raiiDevice_, descriptorSetLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create displacement descriptor set layout");
        return false;
    }

    // Pipeline layout using builder
    if (!PipelineLayoutBuilder(*raiiDevice_)
            .addDescriptorSetLayout(**descriptorSetLayout_)
            .addPushConstantRange<DisplacementPushConstants>(vk::ShaderStageFlagBits::eCompute)
            .buildInto(computePipelineLayout_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create displacement pipeline layout");
        return false;
    }

    // Create compute pipeline - allow failure since system works without splashes
    if (!ComputePipelineBuilder(*raiiDevice_)
            .setShader("shaders/water_displacement.comp.spv")
            .setPipelineLayout(**computePipelineLayout_)
            .buildInto(computePipeline_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "WaterDisplacement: Compute shader not found, using fallback");
        return true;  // Allow system to work without splashes
    }

    return true;
}

bool WaterDisplacement::createDescriptorSets() {
    // Create descriptor pool
    std::array<vk::DescriptorPoolSize, 3> poolSizes = {
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageImage)
            .setDescriptorCount(framesInFlight),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setDescriptorCount(framesInFlight),
        vk::DescriptorPoolSize{}
            .setType(vk::DescriptorType::eStorageBuffer)
            .setDescriptorCount(framesInFlight)
    };

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setPoolSizes(poolSizes)
        .setMaxSets(framesInFlight);

    try {
        descriptorPool_.emplace(*raiiDevice_, poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create displacement descriptor pool: %s", e.what());
        return false;
    }

    // Allocate descriptor sets
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, **descriptorSetLayout_);

    auto allocInfo = vk::DescriptorSetAllocateInfo{}
        .setDescriptorPool(**descriptorPool_)
        .setSetLayouts(layouts);

    try {
        auto allocatedSets = vk::Device(device).allocateDescriptorSets(allocInfo);
        descriptorSets.resize(framesInFlight);
        for (uint32_t i = 0; i < framesInFlight; ++i) {
            descriptorSets[i] = allocatedSets[i];
        }
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate displacement descriptor sets: %s", e.what());
        return false;
    }

    // Update descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeStorageImage(0, **displacementMapView_)
            .writeImage(1, **prevDisplacementMapView_, **sampler_)
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
