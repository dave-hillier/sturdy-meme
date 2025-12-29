#include "CloudShadowSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>

std::unique_ptr<CloudShadowSystem> CloudShadowSystem::create(const InitInfo& info) {
    std::unique_ptr<CloudShadowSystem> system(new CloudShadowSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<CloudShadowSystem> CloudShadowSystem::create(const InitContext& ctx, VkImageView cloudMapLUTView_, VkSampler cloudMapLUTSampler_) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.cloudMapLUTView = cloudMapLUTView_;
    info.cloudMapLUTSampler = cloudMapLUTSampler_;
    return create(info);
}

CloudShadowSystem::~CloudShadowSystem() {
    cleanup();
}

bool CloudShadowSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    cloudMapLUTView = info.cloudMapLUTView;
    cloudMapLUTSampler = info.cloudMapLUTSampler;

    if (!createShadowMap()) return false;
    if (!createSampler()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipeline()) return false;

    SDL_Log("Cloud Shadow System initialized (%dx%d shadow map)", SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    return true;
}

void CloudShadowSystem::cleanup() {
    if (!device) return;  // Not initialized

    // RAII wrappers handle cleanup automatically
    computePipeline.reset();
    pipelineLayout.reset();
    descriptorSetLayout.reset();

    BufferUtils::destroyBuffers(allocator, uniformBuffers);

    shadowMapSampler.reset();
    shadowMapView_.reset();
    shadowMap_.reset();
}

bool CloudShadowSystem::createShadowMap() {
    // Create cloud shadow map texture
    // R16F format stores shadow attenuation factor (0 = full shadow, 1 = no shadow)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR16Sfloat)
        .setExtent(vk::Extent3D{SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (!ManagedImage::create(allocator, reinterpret_cast<const VkImageCreateInfo&>(imageInfo), allocInfo, shadowMap_)) {
        SDL_Log("Failed to create cloud shadow map");
        return false;
    }

    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(shadowMap_.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR16Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (!ManagedImageView::create(device, reinterpret_cast<const VkImageViewCreateInfo&>(viewInfo), shadowMapView_)) {
        SDL_Log("Failed to create cloud shadow map view");
        return false;
    }

    return true;
}

bool CloudShadowSystem::createSampler() {
    // Bilinear filtering for smooth shadow edges
    if (!VulkanResourceFactory::createSamplerLinearClamp(device, shadowMapSampler)) {
        SDL_Log("Failed to create cloud shadow sampler");
        return false;
    }

    return true;
}

bool CloudShadowSystem::createUniformBuffers() {
    if (!BufferUtils::PerFrameBufferBuilder()
            .setAllocator(allocator)
            .setFrameCount(framesInFlight)
            .setSize(sizeof(CloudShadowUniforms))
            .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            .setMemoryUsage(VMA_MEMORY_USAGE_CPU_TO_GPU)
            .setAllocationFlags(VMA_ALLOCATION_CREATE_MAPPED_BIT)
            .build(uniformBuffers)) {
        SDL_Log("Failed to create cloud shadow uniform buffers");
        return false;
    }

    return true;
}

bool CloudShadowSystem::createDescriptorSetLayout() {
    // Layout:
    // 0: Cloud shadow map (storage image for compute output)
    // 1: Cloud map LUT (sampled image from atmosphere system)
    // 2: Uniform buffer

    if (!DescriptorManager::LayoutBuilder(device)
            .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)            // 0: Cloud shadow map
            .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)    // 1: Cloud map LUT
            .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)           // 2: Uniform buffer
            .buildManaged(descriptorSetLayout)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cloud shadow descriptor set layout");
        return false;
    }

    return true;
}

bool CloudShadowSystem::createDescriptorSets() {
    // Allocate descriptor sets using managed pool
    descriptorSets = descriptorPool->allocate(descriptorSetLayout.get(), framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate cloud shadow descriptor sets");
        return false;
    }

    // Update descriptor sets
    for (uint32_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeStorageImage(0, shadowMapView_.get())
            .writeImage(1, cloudMapLUTView, cloudMapLUTSampler)
            .writeBuffer(2, uniformBuffers.buffers[i], 0, sizeof(CloudShadowUniforms))
            .update();
    }

    return true;
}

bool CloudShadowSystem::createComputePipeline() {
    // Load compute shader
    auto shaderCode = ShaderLoader::readFile(shaderPath + "/cloud_shadow.comp.spv");
    if (!shaderCode) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load cloud shadow compute shader");
        return false;
    }

    auto shaderModule = ShaderLoader::createShaderModule(device, *shaderCode);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cloud shadow shader module");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*shaderModule)
        .setPName("main");

    // Push constant for temporal spreading quadrant index
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eCompute)
        .setOffset(0)
        .setSize(sizeof(uint32_t));  // quadrantIndex

    if (!DescriptorManager::createManagedPipelineLayout(device, descriptorSetLayout.get(), pipelineLayout, {pushConstantRange})) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cloud shadow pipeline layout");
        return false;
    }

    auto pipelineInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(pipelineLayout.get());

    if (!ManagedPipeline::createCompute(device, VK_NULL_HANDLE, reinterpret_cast<const VkComputePipelineCreateInfo&>(pipelineInfo), computePipeline)) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cloud shadow compute pipeline");
        return false;
    }

    vkDestroyShaderModule(device, *shaderModule, nullptr);
    return true;
}

void CloudShadowSystem::updateWorldToShadowMatrix(const glm::vec3& sunDir, const glm::vec3& cameraPos) {
    // The shadow map is centered on the camera's XZ position
    // It covers WORLD_SIZE x WORLD_SIZE area
    float halfSize = WORLD_SIZE * 0.5f;

    // Center the shadow map on the camera (snapped to texel grid for stability)
    float texelSize = WORLD_SIZE / static_cast<float>(SHADOW_MAP_SIZE);
    float centerX = std::floor(cameraPos.x / texelSize) * texelSize;
    float centerZ = std::floor(cameraPos.z / texelSize) * texelSize;

    // World to shadow UV transform:
    // 1. Translate so center is at origin
    // 2. Scale to [-0.5, 0.5] range
    // 3. Translate to [0, 1] range
    glm::mat4 translate = glm::translate(glm::mat4(1.0f), glm::vec3(-centerX, 0.0f, -centerZ));
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / WORLD_SIZE, 1.0f, 1.0f / WORLD_SIZE));
    glm::mat4 offset = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.5f));

    // For high-fidelity shadows, we also account for sun angle
    // This creates proper parallax as the sun moves
    // The offset is based on the horizontal component of the sun direction
    // and the cloud layer height
    float cloudMidHeight = (CLOUD_LAYER_BOTTOM + CLOUD_LAYER_TOP) * 0.5f;

    // Calculate horizontal offset based on sun angle
    // This makes shadows shift as sun moves (realistic parallax)
    glm::vec3 sunH = glm::normalize(glm::vec3(sunDir.x, 0.0f, sunDir.z));
    float sunAngle = sunDir.y > 0.01f ? std::atan(std::sqrt(sunDir.x * sunDir.x + sunDir.z * sunDir.z) / sunDir.y) : 1.5f;

    // Shadow offset from cloud height (in world units)
    // Scale down since cloud height is in meters but we want texels
    float shadowOffset = cloudMidHeight * std::tan(sunAngle) * 0.001f;  // Scale factor for world units

    glm::mat4 parallaxOffset = glm::translate(glm::mat4(1.0f),
        glm::vec3(-sunH.x * shadowOffset, 0.0f, -sunH.z * shadowOffset));

    worldToShadowUV = offset * scale * parallaxOffset * translate;
}

void CloudShadowSystem::recordUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                                      const glm::vec3& sunDir, float sunIntensity,
                                      const glm::vec3& windOffset, float windTime,
                                      const glm::vec3& cameraPos) {
    if (!enabled || sunIntensity < 0.01f) {
        // Clear shadow map to 1.0 (no shadows) when disabled or no sun
        // This is handled by initializing the image - skip update
        return;
    }

    // Update world-to-shadow matrix
    updateWorldToShadowMatrix(sunDir, cameraPos);

    // Update uniform buffer
    CloudShadowUniforms uniforms{};
    uniforms.worldToShadowUV = worldToShadowUV;
    uniforms.toSunDirection = glm::vec4(sunDir, sunIntensity);
    uniforms.windOffset = glm::vec4(windOffset, windTime);
    uniforms.shadowParams = glm::vec4(shadowIntensity, shadowSoftness,
                                       CLOUD_LAYER_BOTTOM, CLOUD_LAYER_TOP - CLOUD_LAYER_BOTTOM);

    // World bounds for the shadow map
    float halfSize = WORLD_SIZE * 0.5f;
    float texelSize = WORLD_SIZE / static_cast<float>(SHADOW_MAP_SIZE);
    float centerX = std::floor(cameraPos.x / texelSize) * texelSize;
    float centerZ = std::floor(cameraPos.z / texelSize) * texelSize;

    uniforms.worldBounds = glm::vec4(centerX - halfSize, centerZ - halfSize, WORLD_SIZE, WORLD_SIZE);
    uniforms.cloudCoverage = cloudCoverage;
    uniforms.cloudDensity = cloudDensity;
    uniforms.shadowBias = 0.001f;
    uniforms.padding = 0.0f;

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(uniforms));

    // Transition shadow map to general layout for compute write
    Barriers::transitionImage(cmd, shadowMap_.get(),
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout.get(), 0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Push current quadrant index for temporal spreading
    vkCmdPushConstants(cmd, pipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(uint32_t), &quadrantIndex);

    // Cycle quadrant for next frame (0->1->2->3->0...)
    quadrantIndex = (quadrantIndex + 1) % 4;

    // Dispatch compute shader (16x16 workgroups)
    uint32_t groupCountX = (SHADOW_MAP_SIZE + 15) / 16;
    uint32_t groupCountY = (SHADOW_MAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition shadow map to shader read for fragment shaders
    Barriers::imageComputeToSampling(cmd, shadowMap_.get(),
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
