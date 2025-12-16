#include "SnowMaskSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

bool SnowMaskSystem::init(const InitInfo& info) {
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

void SnowMaskSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    snowMaskSampler.reset();
    vkDestroyImageView(dev, snowMaskView, nullptr);
    vmaDestroyImage(alloc, snowMaskImage, snowMaskAllocation);

    lifecycle.destroy(dev, alloc);
}

void SnowMaskSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
    BufferUtils::destroyBuffers(alloc, interactionBuffers);
}

bool SnowMaskSystem::createBuffers() {
    VkDeviceSize uniformBufferSize = sizeof(SnowMaskUniforms);
    VkDeviceSize interactionBufferSize = sizeof(SnowInteractionSource) * MAX_INTERACTIONS;

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create snow mask uniform buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder interactionBuilder;
    if (!interactionBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(interactionBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
             .build(interactionBuffers)) {
        SDL_Log("Failed to create snow interaction buffers");
        return false;
    }

    return createSnowMaskTexture();
}

bool SnowMaskSystem::createSnowMaskTexture() {
    // Create snow mask texture (R16F, single channel for coverage 0-1)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = SNOW_MASK_SIZE;
    imageInfo.extent.height = SNOW_MASK_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16_SFLOAT;  // R16F for coverage value
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(getAllocator(), &imageInfo, &allocInfo,
                       &snowMaskImage, &snowMaskAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create snow mask image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = snowMaskImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(getDevice(), &viewInfo, nullptr, &snowMaskView) != VK_SUCCESS) {
        SDL_Log("Failed to create snow mask image view");
        return false;
    }

    // Create sampler for other systems to sample the snow mask
    if (!VulkanResourceFactory::createSamplerLinearClamp(getDevice(), snowMaskSampler)) {
        SDL_Log("Failed to create snow mask sampler");
        return false;
    }

    return true;
}

bool SnowMaskSystem::createComputeDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());
    // binding 0: snow mask storage image (read/write)
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 1: uniform buffer
           .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
    // binding 2: interaction sources SSBO
           .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(getComputePipelineHandles().descriptorSetLayout);
}

bool SnowMaskSystem::createComputePipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/snow_accumulation.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

    if (!builder.buildPipelineLayout({getComputePipelineHandles().descriptorSetLayout},
                                      getComputePipelineHandles().pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(getComputePipelineHandles().pipelineLayout,
                                         getComputePipelineHandles().pipeline);
}

bool SnowMaskSystem::createDescriptorSets() {
    // Allocate descriptor sets using managed pool
    computeDescriptorSets = getDescriptorPool()->allocate(
        getComputePipelineHandles().descriptorSetLayout, getFramesInFlight());
    if (computeDescriptorSets.size() != getFramesInFlight()) {
        SDL_Log("Failed to allocate snow mask descriptor sets");
        return false;
    }

    // Update descriptor sets with image binding (same image for all frames)
    for (uint32_t i = 0; i < getFramesInFlight(); i++) {
        DescriptorManager::SetWriter(getDevice(), computeDescriptorSets[i])
            .writeStorageImage(0, snowMaskView)
            .writeBuffer(1, uniformBuffers.buffers[i], 0, sizeof(SnowMaskUniforms))
            .writeBuffer(2, interactionBuffers.buffers[i], 0, sizeof(SnowInteractionSource) * MAX_INTERACTIONS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    return true;
}

void SnowMaskSystem::updateUniforms(uint32_t frameIndex, float deltaTime, bool isSnowing,
                                     float weatherIntensity, const EnvironmentSettings& settings) {
    maskSize = settings.snowMaskSize;

    float texelSize = maskSize / static_cast<float>(SNOW_MASK_SIZE);

    SnowMaskUniforms uniforms{};
    uniforms.maskRegion = glm::vec4(maskOrigin.x, maskOrigin.y, maskSize, texelSize);
    uniforms.accumulationParams = glm::vec4(
        settings.snowAccumulationRate,
        settings.snowMeltRate,
        deltaTime,
        isSnowing ? 1.0f : 0.0f
    );
    uniforms.snowParams = glm::vec4(
        settings.snowAmount,
        weatherIntensity,
        static_cast<float>(currentInteractions.size()),
        0.0f
    );

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(SnowMaskUniforms));

    // Copy interaction sources to buffer
    if (!currentInteractions.empty()) {
        size_t copySize = sizeof(SnowInteractionSource) * std::min(currentInteractions.size(),
                                                                    static_cast<size_t>(MAX_INTERACTIONS));
        memcpy(interactionBuffers.mappedPointers[frameIndex], currentInteractions.data(), copySize);
    }
}

void SnowMaskSystem::addInteraction(const glm::vec3& position, float radius, float strength) {
    if (currentInteractions.size() >= MAX_INTERACTIONS) {
        return;
    }

    SnowInteractionSource source{};
    source.positionAndRadius = glm::vec4(position, radius);
    source.strengthAndShape = glm::vec4(strength, 0.0f, 0.0f, 0.0f);  // Circle shape

    currentInteractions.push_back(source);
}

void SnowMaskSystem::clearInteractions() {
    currentInteractions.clear();
}

void SnowMaskSystem::setMaskCenter(const glm::vec3& worldPos) {
    // Center the mask on the world position
    maskOrigin = glm::vec2(worldPos.x - maskSize * 0.5f, worldPos.z - maskSize * 0.5f);
}

void SnowMaskSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Transition snow mask image to general layout for compute write
    if (isFirstFrame) {
        // First frame: image is in undefined state
        Barriers::transitionImage(cmd, snowMaskImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    } else {
        // Subsequent frames: image was left in SHADER_READ_ONLY_OPTIMAL
        Barriers::transitionImage(cmd, snowMaskImage,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }

    // Bind compute pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch: 512x512 / 16x16 = 32x32 workgroups
    uint32_t workgroupCount = SNOW_MASK_SIZE / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, workgroupCount, workgroupCount, 1);

    // Transition snow mask to shader read optimal for fragment shaders
    Barriers::imageComputeToSampling(cmd, snowMaskImage,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // Mark first frame as done
    isFirstFrame = false;

    // Clear interactions for next frame
    clearInteractions();
}
