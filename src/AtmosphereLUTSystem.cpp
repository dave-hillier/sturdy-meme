#include "AtmosphereLUTSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <array>
#include <vector>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

bool AtmosphereLUTSystem::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;

    if (!createTransmittanceLUT()) return false;
    if (!createMultiScatterLUT()) return false;
    if (!createSkyViewLUT()) return false;
    if (!createLUTSampler()) return false;
    if (!createUniformBuffer()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipelines()) return false;

    SDL_Log("Atmosphere LUT System initialized");
    return true;
}

void AtmosphereLUTSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroyLUTResources();

    if (uniformBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, uniformBuffer, uniformAllocation);
        uniformBuffer = VK_NULL_HANDLE;
    }

    if (transmittancePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, transmittancePipeline, nullptr);
        transmittancePipeline = VK_NULL_HANDLE;
    }
    if (multiScatterPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, multiScatterPipeline, nullptr);
        multiScatterPipeline = VK_NULL_HANDLE;
    }
    if (skyViewPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, skyViewPipeline, nullptr);
        skyViewPipeline = VK_NULL_HANDLE;
    }

    if (transmittancePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, transmittancePipelineLayout, nullptr);
        transmittancePipelineLayout = VK_NULL_HANDLE;
    }
    if (multiScatterPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, multiScatterPipelineLayout, nullptr);
        multiScatterPipelineLayout = VK_NULL_HANDLE;
    }
    if (skyViewPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, skyViewPipelineLayout, nullptr);
        skyViewPipelineLayout = VK_NULL_HANDLE;
    }

    if (transmittanceDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, transmittanceDescriptorSetLayout, nullptr);
        transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (multiScatterDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, multiScatterDescriptorSetLayout, nullptr);
        multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (skyViewDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, skyViewDescriptorSetLayout, nullptr);
        skyViewDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (lutSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, lutSampler, nullptr);
        lutSampler = VK_NULL_HANDLE;
    }
}

void AtmosphereLUTSystem::destroyLUTResources() {
    if (transmittanceLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, transmittanceLUTView, nullptr);
        transmittanceLUTView = VK_NULL_HANDLE;
    }
    if (transmittanceLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, transmittanceLUT, transmittanceLUTAllocation);
        transmittanceLUT = VK_NULL_HANDLE;
    }

    if (multiScatterLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, multiScatterLUTView, nullptr);
        multiScatterLUTView = VK_NULL_HANDLE;
    }
    if (multiScatterLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, multiScatterLUT, multiScatterLUTAllocation);
        multiScatterLUT = VK_NULL_HANDLE;
    }

    if (skyViewLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, skyViewLUTView, nullptr);
        skyViewLUTView = VK_NULL_HANDLE;
    }
    if (skyViewLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, skyViewLUT, skyViewLUTAllocation);
        skyViewLUT = VK_NULL_HANDLE;
    }
}

bool AtmosphereLUTSystem::createTransmittanceLUT() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &transmittanceLUT, &transmittanceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create transmittance LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = transmittanceLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &transmittanceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create transmittance LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createMultiScatterLUT() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;
    imageInfo.extent = {MULTISCATTER_SIZE, MULTISCATTER_SIZE, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &multiScatterLUT, &multiScatterLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create multi-scatter LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = multiScatterLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &multiScatterLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create multi-scatter LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createSkyViewLUT() {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {SKYVIEW_WIDTH, SKYVIEW_HEIGHT, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &skyViewLUT, &skyViewLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create sky-view LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = skyViewLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &skyViewLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create sky-view LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createLUTSampler() {
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
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &lutSampler) != VK_SUCCESS) {
        SDL_Log("Failed to create LUT sampler");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(AtmosphereLUTUniforms);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                        &uniformBuffer, &uniformAllocation, &allocResult) != VK_SUCCESS) {
        SDL_Log("Failed to create atmosphere uniform buffer");
        return false;
    }

    uniformMappedPtr = allocResult.pMappedData;
    return true;
}

bool AtmosphereLUTSystem::createDescriptorSetLayouts() {
    // Transmittance LUT descriptor set layout (just output image and uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &transmittanceDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &transmittanceDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &transmittancePipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline layout");
            return false;
        }
    }

    // Multi-scatter LUT descriptor set layout (transmittance input, output image, uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &multiScatterDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &multiScatterDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &multiScatterPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline layout");
            return false;
        }
    }

    // Sky-view LUT descriptor set layout (transmittance + multiscatter inputs, output image, uniform buffer)
    {
        std::array<VkDescriptorSetLayoutBinding, 4> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &skyViewDescriptorSetLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view descriptor set layout");
            return false;
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &skyViewDescriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &skyViewPipelineLayout) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline layout");
            return false;
        }
    }

    return true;
}

bool AtmosphereLUTSystem::createDescriptorSets() {
    // Allocate transmittance descriptor set
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &transmittanceDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &transmittanceDescriptorSet) != VK_SUCCESS) {
            SDL_Log("Failed to allocate transmittance descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 2> writes{};

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = transmittanceLUTView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = transmittanceDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereLUTUniforms);

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = transmittanceDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate multi-scatter descriptor set
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &multiScatterDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &multiScatterDescriptorSet) != VK_SUCCESS) {
            SDL_Log("Failed to allocate multi-scatter descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 3> writes{};

        VkDescriptorImageInfo outputImageInfo{};
        outputImageInfo.imageView = multiScatterLUTView;
        outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = multiScatterDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &outputImageInfo;

        VkDescriptorImageInfo transmittanceImageInfo{};
        transmittanceImageInfo.imageView = transmittanceLUTView;
        transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceImageInfo.sampler = lutSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = multiScatterDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &transmittanceImageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereLUTUniforms);

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = multiScatterDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Allocate sky-view descriptor set
    {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &skyViewDescriptorSetLayout;

        if (vkAllocateDescriptorSets(device, &allocInfo, &skyViewDescriptorSet) != VK_SUCCESS) {
            SDL_Log("Failed to allocate sky-view descriptor set");
            return false;
        }

        std::array<VkWriteDescriptorSet, 4> writes{};

        VkDescriptorImageInfo outputImageInfo{};
        outputImageInfo.imageView = skyViewLUTView;
        outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = skyViewDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &outputImageInfo;

        VkDescriptorImageInfo transmittanceImageInfo{};
        transmittanceImageInfo.imageView = transmittanceLUTView;
        transmittanceImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        transmittanceImageInfo.sampler = lutSampler;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = skyViewDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &transmittanceImageInfo;

        VkDescriptorImageInfo multiScatterImageInfo{};
        multiScatterImageInfo.imageView = multiScatterLUTView;
        multiScatterImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        multiScatterImageInfo.sampler = lutSampler;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = skyViewDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &multiScatterImageInfo;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(AtmosphereLUTUniforms);

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = skyViewDescriptorSet;
        writes[3].dstBinding = 3;
        writes[3].dstArrayElement = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

bool AtmosphereLUTSystem::createComputePipelines() {
    // Create transmittance pipeline
    {
        std::string shaderFile = shaderPath + "/transmittance_lut.comp.spv";
        std::vector<char> shaderCode = loadShaderFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = transmittancePipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transmittancePipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create transmittance pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create multi-scatter pipeline
    {
        std::string shaderFile = shaderPath + "/multiscatter_lut.comp.spv";
        std::vector<char> shaderCode = loadShaderFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = multiScatterPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &multiScatterPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create multi-scatter pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    // Create sky-view pipeline
    {
        std::string shaderFile = shaderPath + "/skyview_lut.comp.spv";
        std::vector<char> shaderCode = loadShaderFile(shaderFile);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = shaderCode.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view shader module");
            return false;
        }

        VkPipelineShaderStageCreateInfo shaderStageInfo{};
        shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shaderStageInfo.module = shaderModule;
        shaderStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = shaderStageInfo;
        pipelineInfo.layout = skyViewPipelineLayout;

        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &skyViewPipeline) != VK_SUCCESS) {
            SDL_Log("Failed to create sky-view pipeline");
            vkDestroyShaderModule(device, shaderModule, nullptr);
            return false;
        }

        vkDestroyShaderModule(device, shaderModule, nullptr);
    }

    return true;
}

void AtmosphereLUTSystem::computeTransmittanceLUT(VkCommandBuffer cmd) {
    // Update uniform buffer with atmosphere params
    AtmosphereLUTUniforms uniforms{};
    uniforms.params = atmosphereParams;
    memcpy(uniformMappedPtr, &uniforms, sizeof(AtmosphereLUTUniforms));

    // Transition to GENERAL layout for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = transmittanceLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, transmittancePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, transmittancePipelineLayout,
                           0, 1, &transmittanceDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (TRANSMITTANCE_WIDTH + 15) / 16;
    uint32_t groupCountY = (TRANSMITTANCE_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling in later stages
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed transmittance LUT (%dx%d)", TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT);
}

void AtmosphereLUTSystem::computeMultiScatterLUT(VkCommandBuffer cmd) {
    // Transition to GENERAL layout for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = multiScatterLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, multiScatterPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, multiScatterPipelineLayout,
                           0, 1, &multiScatterDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (MULTISCATTER_SIZE + 7) / 8;
    uint32_t groupCountY = (MULTISCATTER_SIZE + 7) / 8;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed multi-scatter LUT (%dx%d)", MULTISCATTER_SIZE, MULTISCATTER_SIZE);
}

void AtmosphereLUTSystem::computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir,
                                            const glm::vec3& cameraPos, float cameraAltitude) {
    // Update uniform buffer
    AtmosphereLUTUniforms uniforms{};
    uniforms.params = atmosphereParams;
    uniforms.sunDirection = glm::vec4(sunDir, 0.0f);
    uniforms.cameraPosition = glm::vec4(cameraPos, cameraAltitude);
    memcpy(uniformMappedPtr, &uniforms, sizeof(AtmosphereLUTUniforms));

    // Transition to GENERAL layout for compute write
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = skyViewLUT;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Bind pipeline and dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, skyViewPipelineLayout,
                           0, 1, &skyViewDescriptorSet, 0, nullptr);

    uint32_t groupCountX = (SKYVIEW_WIDTH + 15) / 16;
    uint32_t groupCountY = (SKYVIEW_HEIGHT + 15) / 16;
    vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

    // Transition to SHADER_READ for sampling
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    SDL_Log("Computed sky-view LUT (%dx%d)", SKYVIEW_WIDTH, SKYVIEW_HEIGHT);
}

bool AtmosphereLUTSystem::exportImageToPNG(VkImage image, VkFormat format, uint32_t width, uint32_t height, const std::string& filename) {
    // Create staging buffer
    VkDeviceSize imageSize = width * height * 4 * sizeof(uint16_t); // RGBA16F

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create staging buffer for PNG export");
        return false;
    }

    // Create command buffer for the copy
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0; // Assuming graphics queue family 0
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo2{};
    allocInfo2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo2.commandPool = commandPool;
    allocInfo2.commandBufferCount = 1;

    vkAllocateCommandBuffers(device, &allocInfo2, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // Transition image to TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    // Transition back
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    // Submit and wait
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, 0, 0, &graphicsQueue);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Map and convert to 8-bit RGBA for PNG
    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);

    std::vector<uint8_t> rgba8(width * height * 4);
    uint16_t* src = static_cast<uint16_t*>(data);

    for (uint32_t i = 0; i < width * height * 4; i++) {
        // Convert float16 to float32 to uint8 (simple conversion)
        // For proper conversion, we'd need a float16 library
        // For now, just copy the lower 8 bits scaled
        float val = static_cast<float>(src[i]) / 65535.0f * 255.0f;
        rgba8[i] = static_cast<uint8_t>(glm::clamp(val * 10.0f, 0.0f, 255.0f)); // Scale up for visibility
    }

    vmaUnmapMemory(allocator, stagingAllocation);

    // Write PNG
    int result = stbi_write_png(filename.c_str(), width, height, 4, rgba8.data(), width * 4);

    // Cleanup
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    vkDestroyCommandPool(device, commandPool, nullptr);

    if (result == 0) {
        SDL_Log("Failed to write PNG: %s", filename.c_str());
        return false;
    }

    SDL_Log("Exported LUT to: %s", filename.c_str());
    return true;
}

bool AtmosphereLUTSystem::exportLUTsAsPNG(const std::string& outputDir) {
    SDL_Log("Exporting atmosphere LUTs as PNG...");

    bool success = true;
    success &= exportImageToPNG(transmittanceLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                TRANSMITTANCE_WIDTH, TRANSMITTANCE_HEIGHT,
                                outputDir + "/transmittance_lut.png");

    success &= exportImageToPNG(multiScatterLUT, VK_FORMAT_R16G16_SFLOAT,
                                MULTISCATTER_SIZE, MULTISCATTER_SIZE,
                                outputDir + "/multiscatter_lut.png");

    success &= exportImageToPNG(skyViewLUT, VK_FORMAT_R16G16B16A16_SFLOAT,
                                SKYVIEW_WIDTH, SKYVIEW_HEIGHT,
                                outputDir + "/skyview_lut.png");

    return success;
}
