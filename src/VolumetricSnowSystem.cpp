#include "VolumetricSnowSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

bool VolumetricSnowSystem::init(const InitInfo& info) {
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

void VolumetricSnowSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    vkDestroySampler(dev, cascadeSampler, nullptr);

    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        vkDestroyImageView(dev, cascadeViews[i], nullptr);
        vmaDestroyImage(alloc, cascadeImages[i], cascadeAllocations[i]);
    }

    lifecycle.destroy(dev, alloc);
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
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = SNOW_CASCADE_SIZE;
        imageInfo.extent.height = SNOW_CASCADE_SIZE;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R16_SFLOAT;  // R16F for height value
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(getAllocator(), &imageInfo, &allocInfo,
                           &cascadeImages[i], &cascadeAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create volumetric snow cascade %d image", i);
            return false;
        }

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cascadeImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(getDevice(), &viewInfo, nullptr, &cascadeViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create volumetric snow cascade %d image view", i);
            return false;
        }
    }

    // Create shared sampler for all cascades
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(getDevice(), &samplerInfo, nullptr, &cascadeSampler) != VK_SUCCESS) {
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

    // Prepare image infos for all cascades
    std::array<VkDescriptorImageInfo, NUM_SNOW_CASCADES> imageInfos{};
    for (uint32_t c = 0; c < NUM_SNOW_CASCADES; c++) {
        imageInfos[c].imageView = cascadeViews[c];
        imageInfos[c].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    for (uint32_t i = 0; i < getFramesInFlight(); i++) {
        VkDescriptorBufferInfo uniformInfo{};
        uniformInfo.buffer = uniformBuffers.buffers[i];
        uniformInfo.offset = 0;
        uniformInfo.range = sizeof(VolumetricSnowUniforms);

        VkDescriptorBufferInfo interactionInfo{};
        interactionInfo.buffer = interactionBuffers.buffers[i];
        interactionInfo.offset = 0;
        interactionInfo.range = sizeof(VolumetricSnowInteraction) * MAX_INTERACTIONS;

        std::array<VkWriteDescriptorSet, 5> writes{};

        // Cascade 0 storage image
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = computeDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfos[0];

        // Cascade 1 storage image
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = computeDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfos[1];

        // Cascade 2 storage image
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = computeDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &imageInfos[2];

        // Uniform buffer
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = computeDescriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &uniformInfo;

        // Interaction sources buffer
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = computeDescriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].dstArrayElement = 0;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &interactionInfo;

        vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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
    // Transition all cascade images to general layout for compute write
    std::array<VkImageMemoryBarrier, NUM_SNOW_CASCADES> imageBarriers{};

    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        imageBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarriers[i].image = cascadeImages[i];
        imageBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarriers[i].subresourceRange.baseMipLevel = 0;
        imageBarriers[i].subresourceRange.levelCount = 1;
        imageBarriers[i].subresourceRange.baseArrayLayer = 0;
        imageBarriers[i].subresourceRange.layerCount = 1;
        imageBarriers[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        if (isFirstFrame[i]) {
            imageBarriers[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarriers[i].srcAccessMask = 0;
        } else {
            imageBarriers[i].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarriers[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
    }

    VkPipelineStageFlags srcStage = isFirstFrame[0] ?
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    vkCmdPipelineBarrier(cmd,
                         srcStage,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr,
                         NUM_SNOW_CASCADES, imageBarriers.data());

    // Bind compute pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch for each cascade (same shader, different region in uniforms)
    // All cascades are the same resolution so same dispatch count
    uint32_t workgroupCount = SNOW_CASCADE_SIZE / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, workgroupCount, workgroupCount, NUM_SNOW_CASCADES);

    // Transition all cascades to shader read optimal for fragment shaders
    std::array<VkImageMemoryBarrier, NUM_SNOW_CASCADES> readBarriers{};
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        readBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        readBarriers[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        readBarriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        readBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readBarriers[i].image = cascadeImages[i];
        readBarriers[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        readBarriers[i].subresourceRange.baseMipLevel = 0;
        readBarriers[i].subresourceRange.levelCount = 1;
        readBarriers[i].subresourceRange.baseArrayLayer = 0;
        readBarriers[i].subresourceRange.layerCount = 1;
        readBarriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        readBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr,
                         NUM_SNOW_CASCADES, readBarriers.data());

    // Mark first frame as done
    for (uint32_t i = 0; i < NUM_SNOW_CASCADES; i++) {
        isFirstFrame[i] = false;
    }

    // Clear interactions for next frame
    clearInteractions();
}
