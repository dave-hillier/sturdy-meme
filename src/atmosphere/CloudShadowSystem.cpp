#include "CloudShadowSystem.h"
#include "ShaderLoader.h"
#include "DescriptorManager.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>

bool CloudShadowSystem::init(const InitInfo& info) {
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

bool CloudShadowSystem::init(const InitContext& ctx, VkImageView cloudMapLUTView_, VkSampler cloudMapLUTSampler_) {
    device = ctx.device;
    allocator = ctx.allocator;
    descriptorPool = ctx.descriptorPool;
    shaderPath = ctx.shaderPath;
    framesInFlight = ctx.framesInFlight;
    cloudMapLUTView = cloudMapLUTView_;
    cloudMapLUTSampler = cloudMapLUTSampler_;

    if (!createShadowMap()) return false;
    if (!createSampler()) return false;
    if (!createUniformBuffers()) return false;
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipeline()) return false;

    SDL_Log("Cloud Shadow System initialized (%dx%d shadow map)", SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    return true;
}

void CloudShadowSystem::destroy() {
    // RAII wrappers handle cleanup automatically
    computePipeline = ManagedPipeline();
    pipelineLayout = ManagedPipelineLayout();
    descriptorSetLayout = ManagedDescriptorSetLayout();

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
        }
    }
    uniformBuffers.clear();
    uniformAllocations.clear();
    uniformMappedPtrs.clear();

    shadowMapSampler.reset();
    if (shadowMapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowMapView, nullptr);
        shadowMapView = VK_NULL_HANDLE;
    }
    if (shadowMap != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, shadowMap, shadowMapAllocation);
        shadowMap = VK_NULL_HANDLE;
    }
}

bool CloudShadowSystem::createShadowMap() {
    // Create cloud shadow map texture
    // R16F format stores shadow attenuation factor (0 = full shadow, 1 = no shadow)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;
    imageInfo.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
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
                       &shadowMap, &shadowMapAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud shadow map");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = shadowMap;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &shadowMapView) != VK_SUCCESS) {
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
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(CloudShadowUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &uniformBuffers[i], &uniformAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud shadow uniform buffer");
            return false;
        }
        uniformMappedPtrs[i] = allocationInfo.pMappedData;
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
            .writeStorageImage(0, shadowMapView)
            .writeImage(1, cloudMapLUTView, cloudMapLUTSampler)
            .writeBuffer(2, uniformBuffers[i], 0, sizeof(CloudShadowUniforms))
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

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    if (!DescriptorManager::createManagedPipelineLayout(device, descriptorSetLayout.get(), pipelineLayout)) {
        vkDestroyShaderModule(device, *shaderModule, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cloud shadow pipeline layout");
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout.get();

    if (!ManagedPipeline::createCompute(device, VK_NULL_HANDLE, pipelineInfo, computePipeline)) {
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
    uniforms.sunDirection = glm::vec4(sunDir, sunIntensity);
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

    memcpy(uniformMappedPtrs[frameIndex], &uniforms, sizeof(uniforms));

    // Transition shadow map to general layout for compute write
    Barriers::transitionImage(cmd, shadowMap,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    // Bind pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout.get(), 0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Dispatch compute shader (16x16 workgroups)
    uint32_t groupCountX = (SHADOW_MAP_SIZE + 15) / 16;
    uint32_t groupCountY = (SHADOW_MAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition shadow map to shader read for fragment shaders
    Barriers::imageComputeToSampling(cmd, shadowMap,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
