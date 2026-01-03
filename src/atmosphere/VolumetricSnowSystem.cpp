#include "VolumetricSnowSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "VulkanBarriers.h"
#include "VmaResources.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <array>
#include <vector>

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

    cascadeSampler_.reset();

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
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        auto imageInfo = vk::ImageCreateInfo{}
            .setImageType(vk::ImageType::e2D)
            .setExtent(vk::Extent3D{SNOW_CASCADE_SIZE, SNOW_CASCADE_SIZE, 1})
            .setMipLevels(1)
            .setArrayLayers(1)
            .setFormat(vk::Format::eR16Sfloat)  // R16F for height value
            .setTiling(vk::ImageTiling::eOptimal)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setSamples(vk::SampleCountFlagBits::e1);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(getAllocator(), reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo,
                           &cascadeImages[i], &cascadeAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create volumetric snow cascade %d image", i);
            return false;
        }

        // Create image view
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(cascadeImages[i])
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        if (vkCreateImageView(getDevice(), reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &cascadeViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create volumetric snow cascade %d image view", i);
            return false;
        }
    }

    // Create shared sampler for all cascades
    if (auto* raiiDevice = lifecycle.getRaiiDevice(); raiiDevice) {
        cascadeSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice);
        if (!cascadeSampler_) {
            SDL_Log("Failed to create volumetric snow cascade sampler");
            return false;
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RAII device not available for cascade sampler");
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
    vk::CommandBuffer vkCmd(cmd);
    vkCmd.bindPipeline(vk::PipelineBindPoint::eCompute, getComputePipelineHandles().pipeline);
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             getComputePipelineHandles().pipelineLayout, 0,
                             vk::DescriptorSet(computeDescriptorSets[frameIndex]), {});

    // Dispatch for each cascade (same shader, different region in uniforms)
    // All cascades are the same resolution so same dispatch count
    uint32_t workgroupCount = SNOW_CASCADE_SIZE / WORKGROUP_SIZE;
    vkCmd.dispatch(workgroupCount, workgroupCount, NUM_SNOW_CASCADES);

    barrierCascadesForSampling(cmd);

    // Mark first frame as done
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        isFirstFrame[i] = false;
    }

    // Clear interactions for next frame
    clearInteractions();
}

void VolumetricSnowSystem::barrierCascadesForCompute(VkCommandBuffer cmd) {
    vk::CommandBuffer vkCmd(cmd);
    vk::PipelineStageFlags srcStage = isFirstFrame[0] ?
        vk::PipelineStageFlagBits::eTopOfPipe : vk::PipelineStageFlagBits::eFragmentShader;

    std::vector<vk::ImageMemoryBarrier> barriers;
    barriers.reserve(NUM_SNOW_CASCADES);
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        vk::ImageLayout oldLayout = isFirstFrame[i] ?
            vk::ImageLayout::eUndefined : vk::ImageLayout::eShaderReadOnlyOptimal;
        vk::AccessFlags srcAccess = isFirstFrame[i] ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderRead;

        barriers.push_back(vk::ImageMemoryBarrier{}
            .setSrcAccessMask(srcAccess)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite)
            .setOldLayout(oldLayout)
            .setNewLayout(vk::ImageLayout::eGeneral)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascadeImages[i])
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
    }
    vkCmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, barriers);
}

void VolumetricSnowSystem::barrierCascadesForSampling(VkCommandBuffer cmd) {
    vk::CommandBuffer vkCmd(cmd);
    std::vector<vk::ImageMemoryBarrier> barriers;
    barriers.reserve(NUM_SNOW_CASCADES);
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        barriers.push_back(vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eGeneral)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(cascadeImages[i])
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
    }
    vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                          {}, {}, {}, barriers);
}
