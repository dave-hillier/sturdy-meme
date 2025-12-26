#include "VolumetricSnowSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

using namespace vk;  // Vulkan-Hpp type-safe wrappers

std::unique_ptr<VolumetricSnowSystem> VolumetricSnowSystem::create(const InitInfo& info) {
    std::unique_ptr<VolumetricSnowSystem> system(new VolumetricSnowSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

VolumetricSnowSystem::~VolumetricSnowSystem() {
    cleanup();
}

bool VolumetricSnowSystem::initInternal(const InitInfo& info) {
    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this]() { return createComputeDescriptorSetLayout(); };
    hooks.createComputePipeline = [this]() { return createComputePipeline(); };
    hooks.createGraphicsDescriptorSetLayout = []() { return true; };  // No graphics pipeline
    hooks.createGraphicsPipeline = []() { return true; };             // No graphics pipeline
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };
    hooks.usesGraphicsPipeline = []() { return false; };  // Compute-only system

    return lifecycle.init(info, hooks);
}

void VolumetricSnowSystem::cleanup() {
    if (!lifecycle.getDevice()) return;  // Not initialized

    cascadeSampler.reset();

    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        if (cascadeViews[i]) {
            vkDestroyImageView(lifecycle.getDevice(), cascadeViews[i], nullptr);
            cascadeViews[i] = VK_NULL_HANDLE;
        }
        if (cascadeImages[i]) {
            vmaDestroyImage(lifecycle.getAllocator(), cascadeImages[i], cascadeAllocations[i]);
            cascadeImages[i] = VK_NULL_HANDLE;
        }
    }

    lifecycle.destroy(lifecycle.getDevice(), lifecycle.getAllocator());
}

void VolumetricSnowSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
    BufferUtils::destroyBuffers(alloc, interactionBuffers);
}

bool VolumetricSnowSystem::createBuffers() {
    VkDeviceSize uniformBufferSize = sizeof(VolumetricSnowUniforms);
    VkDeviceSize interactionBufferSize = sizeof(VolumetricSnowInteraction) * MAX_INTERACTIONS;

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create volumetric snow uniform buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder interactionBuilder;
    if (!interactionBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(interactionBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
             .build(interactionBuffers)) {
        SDL_Log("Failed to create volumetric snow interaction buffers");
        return false;
    }

    return createCascadeTextures();
}

bool VolumetricSnowSystem::createCascadeTextures() {
    // Create cascade textures (R16F height in meters)
    ImageCreateInfo imageInfo{
        {},                                  // flags
        ImageType::e2D,
        Format::eR16Sfloat,                  // R16F for height value
        Extent3D{SNOW_CASCADE_SIZE, SNOW_CASCADE_SIZE, 1},
        1, 1,                                // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        ImageUsageFlagBits::eStorage | ImageUsageFlagBits::eSampled,
        SharingMode::eExclusive,
        0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);

    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        if (vmaCreateImage(getAllocator(), &vkImageInfo, &allocInfo,
                           &cascadeImages[i], &cascadeAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create volumetric snow cascade %d image", i);
            return false;
        }

        // Create image view
        ImageViewCreateInfo viewInfo{
            {},                              // flags
            cascadeImages[i],
            ImageViewType::e2D,
            Format::eR16Sfloat,
            ComponentMapping{},              // identity swizzle
            ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
        if (vkCreateImageView(getDevice(), &vkViewInfo, nullptr, &cascadeViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create volumetric snow cascade %d image view", i);
            return false;
        }
    }

    // Create shared sampler for all cascades
    if (!VulkanResourceFactory::createSamplerLinearClamp(getDevice(), cascadeSampler)) {
        SDL_Log("Failed to create volumetric snow cascade sampler");
        return false;
    }

    // Initialize cascade origins at world center
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float halfSize = SNOW_CASCADE_COVERAGE[i] * 0.5f;
        cascadeOrigins[i] = glm::vec2(-halfSize, -halfSize);
    }

    return true;
}

bool VolumetricSnowSystem::createComputeDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());

    // binding 0: cascade 0 storage image (read/write)
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 1: cascade 1 storage image (read/write)
           .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 2: cascade 2 storage image (read/write)
           .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 3: uniform buffer
           .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 4: interaction sources SSBO
           .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(getComputePipelineHandles().descriptorSetLayout);
}

bool VolumetricSnowSystem::createComputePipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/volumetric_snow.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

    if (!builder.buildPipelineLayout({getComputePipelineHandles().descriptorSetLayout},
                                      getComputePipelineHandles().pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(getComputePipelineHandles().pipelineLayout,
                                         getComputePipelineHandles().pipeline);
}

bool VolumetricSnowSystem::createDescriptorSets() {
    // Allocate descriptor sets using managed pool
    computeDescriptorSets = getDescriptorPool()->allocate(
        getComputePipelineHandles().descriptorSetLayout, getFramesInFlight());
    if (computeDescriptorSets.size() != getFramesInFlight()) {
        SDL_Log("Failed to allocate volumetric snow descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < getFramesInFlight(); i++) {
        DescriptorManager::SetWriter(getDevice(), computeDescriptorSets[i])
            .writeStorageImage(0, cascadeViews[0])
            .writeStorageImage(1, cascadeViews[1])
            .writeStorageImage(2, cascadeViews[2])
            .writeBuffer(3, uniformBuffers.buffers[i], 0, sizeof(VolumetricSnowUniforms))
            .writeBuffer(4, interactionBuffers.buffers[i], 0, sizeof(VolumetricSnowInteraction) * MAX_INTERACTIONS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    return true;
}

void VolumetricSnowSystem::updateCascadeOrigins(const glm::vec3& cameraPos) {
    // Each cascade is centered on the camera position
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float halfSize = SNOW_CASCADE_COVERAGE[i] * 0.5f;
        cascadeOrigins[i] = glm::vec2(cameraPos.x - halfSize, cameraPos.z - halfSize);
    }
    lastCameraPosition = cameraPos;
}

void VolumetricSnowSystem::setCameraPosition(const glm::vec3& worldPos) {
    updateCascadeOrigins(worldPos);
}

void VolumetricSnowSystem::updateUniforms(uint32_t frameIndex, float deltaTime, bool isSnowing,
                                           float weatherIntensity, const EnvironmentSettings& settings) {
    VolumetricSnowUniforms uniforms{};

    // Cascade regions
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float texelSize = SNOW_CASCADE_COVERAGE[i] / static_cast<float>(SNOW_CASCADE_SIZE);
        glm::vec4 region = glm::vec4(cascadeOrigins[i].x, cascadeOrigins[i].y,
                                      SNOW_CASCADE_COVERAGE[i], texelSize);
        if (i == 0) uniforms.cascade0Region = region;
        else if (i == 1) uniforms.cascade1Region = region;
        else uniforms.cascade2Region = region;
    }

    // Convert coverage-based accumulation to height-based
    // Target height = snowAmount * MAX_SNOW_HEIGHT
    float targetHeight = settings.snowAmount * MAX_SNOW_HEIGHT;

    uniforms.accumulationParams = glm::vec4(
        settings.snowAccumulationRate * MAX_SNOW_HEIGHT,  // Height accumulation rate
        settings.snowMeltRate * MAX_SNOW_HEIGHT,          // Height melt rate
        deltaTime,
        isSnowing ? 1.0f : 0.0f
    );

    uniforms.snowParams = glm::vec4(
        targetHeight,
        weatherIntensity,
        static_cast<float>(currentInteractions.size()),
        MAX_SNOW_HEIGHT
    );

    // Wind parameters
    uniforms.windParams = glm::vec4(
        windDirection.x,
        windDirection.y,
        windStrength,
        driftRate
    );

    uniforms.cameraPosition = glm::vec4(lastCameraPosition, 0.0f);

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(VolumetricSnowUniforms));

    // Copy interaction sources to buffer
    if (!currentInteractions.empty()) {
        size_t copySize = sizeof(VolumetricSnowInteraction) * std::min(currentInteractions.size(),
                                                                        static_cast<size_t>(MAX_INTERACTIONS));
        memcpy(interactionBuffers.mappedPointers[frameIndex], currentInteractions.data(), copySize);
    }
}

void VolumetricSnowSystem::addInteraction(const glm::vec3& position, float radius, float strength, float depthFactor) {
    if (currentInteractions.size() >= MAX_INTERACTIONS) {
        return;
    }

    VolumetricSnowInteraction interaction{};
    interaction.positionAndRadius = glm::vec4(position, radius);
    interaction.strengthAndDepth = glm::vec4(strength, depthFactor, 0.0f, 0.0f);

    currentInteractions.push_back(interaction);
}

void VolumetricSnowSystem::clearInteractions() {
    currentInteractions.clear();
}

std::array<glm::vec4, NUM_SNOW_CASCADES> VolumetricSnowSystem::getCascadeParams() const {
    std::array<glm::vec4, NUM_SNOW_CASCADES> params;
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        float texelSize = SNOW_CASCADE_COVERAGE[i] / static_cast<float>(SNOW_CASCADE_SIZE);
        params[i] = glm::vec4(cascadeOrigins[i].x, cascadeOrigins[i].y,
                              SNOW_CASCADE_COVERAGE[i], texelSize);
    }
    return params;
}

void VolumetricSnowSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    barrierCascadesForCompute(cmd);

    // Bind compute pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch for each cascade (same shader, different region in uniforms)
    // All cascades are the same resolution so same dispatch count
    uint32_t workgroupCount = SNOW_CASCADE_SIZE / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, workgroupCount, workgroupCount, NUM_SNOW_CASCADES);

    barrierCascadesForSampling(cmd);

    // Mark first frame as done
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        isFirstFrame[i] = false;
    }

    // Clear interactions for next frame
    clearInteractions();
}

void VolumetricSnowSystem::barrierCascadesForCompute(VkCommandBuffer cmd) {
    VkPipelineStageFlags srcStage = isFirstFrame[0] ?
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    Barriers::BarrierBatch batch(cmd, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        VkImageLayout oldLayout = isFirstFrame[i] ?
            VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAccessFlags srcAccess = isFirstFrame[i] ? 0 : VK_ACCESS_SHADER_READ_BIT;

        batch.imageTransition(cascadeImages[i], oldLayout, VK_IMAGE_LAYOUT_GENERAL,
                              srcAccess, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }
}

void VolumetricSnowSystem::barrierCascadesForSampling(VkCommandBuffer cmd) {
    Barriers::BarrierBatch batch(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        batch.imageTransition(cascadeImages[i],
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
}
