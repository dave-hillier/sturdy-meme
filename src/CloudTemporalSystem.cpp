#include "CloudTemporalSystem.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <vector>
#include <cstring>

// Helper to load SPIR-V shader
static std::vector<uint32_t> loadShaderSPV(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        SDL_Log("Failed to open shader file: %s", path.c_str());
        return {};
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    return buffer;
}

bool CloudTemporalSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    // Store external LUT references
    transmittanceLUTView = info.transmittanceLUTView;
    multiScatterLUTView = info.multiScatterLUTView;
    lutSampler = info.lutSampler;

    if (!createCloudMaps()) {
        SDL_Log("CloudTemporalSystem: Failed to create cloud maps");
        return false;
    }

    if (!createSampler()) {
        SDL_Log("CloudTemporalSystem: Failed to create sampler");
        return false;
    }

    if (!createDescriptorSetLayout()) {
        SDL_Log("CloudTemporalSystem: Failed to create descriptor set layout");
        return false;
    }

    if (!createUniformBuffers()) {
        SDL_Log("CloudTemporalSystem: Failed to create uniform buffers");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_Log("CloudTemporalSystem: Failed to create descriptor sets");
        return false;
    }

    if (!createComputePipeline()) {
        SDL_Log("CloudTemporalSystem: Failed to create compute pipeline");
        return false;
    }

    SDL_Log("CloudTemporalSystem: Initialized with %dx%d cloud maps", CLOUD_MAP_SIZE, CLOUD_MAP_SIZE);
    return true;
}

void CloudTemporalSystem::destroy(VkDevice device, VmaAllocator allocator) {
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
    }

    vkDestroySampler(device, cloudSampler, nullptr);

    for (uint32_t i = 0; i < NUM_CLOUD_BUFFERS; i++) {
        vkDestroyImageView(device, cloudMapViews[i], nullptr);
        vmaDestroyImage(allocator, cloudMaps[i], cloudMapAllocations[i]);
    }

    SDL_Log("CloudTemporalSystem: Destroyed");
}

bool CloudTemporalSystem::createCloudMaps() {
    // Create two cloud maps for ping-pong temporal buffering
    // Format: RGBA16F - RGB = scattered light, A = transmittance
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {CLOUD_MAP_SIZE, CLOUD_MAP_SIZE, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    for (uint32_t i = 0; i < NUM_CLOUD_BUFFERS; i++) {
        if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                          &cloudMaps[i], &cloudMapAllocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map %u", i);
            return false;
        }

        // Create image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cloudMaps[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &cloudMapViews[i]) != VK_SUCCESS) {
            SDL_Log("Failed to create cloud map view %u", i);
            return false;
        }
    }

    return true;
}

bool CloudTemporalSystem::createSampler() {
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
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    return vkCreateSampler(device, &samplerInfo, nullptr, &cloudSampler) == VK_SUCCESS;
}

bool CloudTemporalSystem::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // Binding 0: Uniform buffer
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: Current cloud map (output, storage image)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: History cloud map (input, sampled image)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Transmittance LUT
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Multi-scatter LUT
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) == VK_SUCCESS;
}

bool CloudTemporalSystem::createUniformBuffers() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(CloudTemporalUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VmaAllocationInfo mapInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &uniformBuffers[i], &uniformAllocations[i], &mapInfo) != VK_SUCCESS) {
            return false;
        }
        uniformMappedPtrs[i] = mapInfo.pMappedData;
    }

    return true;
}

bool CloudTemporalSystem::createDescriptorSets() {
    // Allocate descriptor sets (one per frame in flight)
    // But we also need to handle the ping-pong buffer swap,
    // so we'll update descriptors dynamically per frame
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

    // Initialize descriptor sets with current buffer configuration
    for (uint32_t i = 0; i < framesInFlight; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CloudTemporalUniforms);

        VkDescriptorImageInfo currentMapInfo{};
        currentMapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        currentMapInfo.imageView = cloudMapViews[currentWriteIndex];

        VkDescriptorImageInfo historyMapInfo{};
        historyMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        historyMapInfo.imageView = cloudMapViews[currentReadIndex];
        historyMapInfo.sampler = cloudSampler;

        VkDescriptorImageInfo transmittanceInfo{};
        transmittanceInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceInfo.imageView = transmittanceLUTView;
        transmittanceInfo.sampler = lutSampler;

        VkDescriptorImageInfo multiScatterInfo{};
        multiScatterInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        multiScatterInfo.imageView = multiScatterLUTView;
        multiScatterInfo.sampler = lutSampler;

        std::array<VkWriteDescriptorSet, 5> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &currentMapInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &historyMapInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSets[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &transmittanceInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSets[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &multiScatterInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

bool CloudTemporalSystem::createComputePipeline() {
    // Load compute shader
    std::string shaderFile = shaderPath + "/cloud_temporal.comp.spv";
    auto shaderCode = loadShaderSPV(shaderFile);
    if (shaderCode.empty()) {
        SDL_Log("Failed to load cloud temporal shader: %s", shaderFile.c_str());
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = shaderCode.size() * sizeof(uint32_t);
    moduleInfo.pCode = shaderCode.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        SDL_Log("Failed to create shader module");
        return false;
    }

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    // Compute pipeline
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline);

    vkDestroyShaderModule(device, shaderModule, nullptr);

    return result == VK_SUCCESS;
}

void CloudTemporalSystem::swapBuffers() {
    // Swap read and write indices for ping-pong buffering
    std::swap(currentWriteIndex, currentReadIndex);
}

void CloudTemporalSystem::recordCloudUpdate(VkCommandBuffer cmd, uint32_t frameIndex,
                                            const glm::mat4& view, const glm::mat4& proj,
                                            const glm::vec3& cameraPos,
                                            const glm::vec3& sunDir, float sunIntensity,
                                            const glm::vec3& sunColor,
                                            const glm::vec3& moonDir, float moonIntensity,
                                            const glm::vec3& moonColor, float moonPhase,
                                            const glm::vec2& windDir, float windSpeed, float windTime) {

    // Update descriptors for current buffer configuration
    // We need to update the output and history image bindings based on current ping-pong state
    {
        VkDescriptorImageInfo currentMapInfo{};
        currentMapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        currentMapInfo.imageView = cloudMapViews[currentWriteIndex];

        VkDescriptorImageInfo historyMapInfo{};
        historyMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        historyMapInfo.imageView = cloudMapViews[currentReadIndex];
        historyMapInfo.sampler = cloudSampler;

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets[frameIndex];
        writes[0].dstBinding = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &currentMapInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets[frameIndex];
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &historyMapInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Update uniform buffer
    glm::mat4 viewProj = proj * view;

    CloudTemporalUniforms ubo{};
    ubo.invViewProj = glm::inverse(viewProj);
    ubo.prevViewProj = prevViewProj;
    ubo.cameraPosition = glm::vec4(cameraPos, 0.0f);
    ubo.sunDirection = glm::vec4(glm::normalize(sunDir), sunIntensity);
    ubo.sunColor = glm::vec4(sunColor, 1.0f);
    ubo.moonDirection = glm::vec4(glm::normalize(moonDir), moonIntensity);
    ubo.moonColor = glm::vec4(moonColor, moonPhase);
    ubo.windParams = glm::vec4(windDir, windSpeed, windTime);
    ubo.cloudParams = glm::vec4(coverage, density,
                                temporalEnabled ? temporalBlend : 0.0f,
                                static_cast<float>(frameCounter));
    // Atmospheric parameters (matching sky.frag constants)
    ubo.atmosphereParams = glm::vec4(6371.0f, 6471.0f, 1.5f, 4.0f);  // planet radius, atmo radius, cloud bottom, cloud top

    std::memcpy(uniformMappedPtrs[frameIndex], &ubo, sizeof(CloudTemporalUniforms));

    // Transition history map to shader read if needed (from previous frame's write)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cloudMaps[currentReadIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Transition current output map to general layout for writing
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cloudMaps[currentWriteIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                            0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Dispatch compute shader
    uint32_t groupsX = (CLOUD_MAP_SIZE + 15) / 16;
    uint32_t groupsY = (CLOUD_MAP_SIZE + 15) / 16;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition output to shader read for sky fragment shader
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = cloudMaps[currentWriteIndex];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Store current view-proj for next frame's reprojection
    prevViewProj = viewProj;
    frameCounter++;

    // Swap buffers for next frame
    swapBuffers();
}
