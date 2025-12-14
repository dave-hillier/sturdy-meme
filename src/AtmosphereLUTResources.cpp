#include "AtmosphereLUTSystem.h"
#include <SDL3/SDL_log.h>

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

bool AtmosphereLUTSystem::createIrradianceLUTs() {
    // Create Rayleigh Irradiance LUT (64×16, RGBA16F)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {IRRADIANCE_WIDTH, IRRADIANCE_HEIGHT, 1};
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
                       &rayleighIrradianceLUT, &rayleighIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Rayleigh irradiance LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = rayleighIrradianceLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &rayleighIrradianceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create Rayleigh irradiance LUT view");
        return false;
    }

    // Create Mie Irradiance LUT (same dimensions and format)
    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &mieIrradianceLUT, &mieIrradianceLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT");
        return false;
    }

    viewInfo.image = mieIrradianceLUT;
    if (vkCreateImageView(device, &viewInfo, nullptr, &mieIrradianceLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create Mie irradiance LUT view");
        return false;
    }

    return true;
}

bool AtmosphereLUTSystem::createCloudMapLUT() {
    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.extent = {CLOUDMAP_SIZE, CLOUDMAP_SIZE, 1};
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
                       &cloudMapLUT, &cloudMapLUTAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud map LUT");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = cloudMapLUT;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &cloudMapLUTView) != VK_SUCCESS) {
        SDL_Log("Failed to create cloud map LUT view");
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
    // Create atmosphere LUT uniform buffer (single buffer for one-time LUT computations)
    VkDeviceSize bufferSize = sizeof(AtmosphereUniforms);

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

    // Create per-frame uniform buffers for sky view LUT updates (double-buffered)
    BufferUtils::PerFrameBufferBuilder skyViewBuilder;
    if (!skyViewBuilder.setAllocator(allocator)
             .setFrameCount(framesInFlight)
             .setSize(sizeof(AtmosphereUniforms))
             .build(skyViewUniformBuffers)) {
        SDL_Log("Failed to create sky view per-frame uniform buffers");
        return false;
    }

    // Create per-frame uniform buffers for cloud map LUT updates (double-buffered)
    BufferUtils::PerFrameBufferBuilder cloudMapBuilder;
    if (!cloudMapBuilder.setAllocator(allocator)
             .setFrameCount(framesInFlight)
             .setSize(sizeof(CloudMapUniforms))
             .build(cloudMapUniformBuffers)) {
        SDL_Log("Failed to create cloud map per-frame uniform buffers");
        return false;
    }

    return true;
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

    if (rayleighIrradianceLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, rayleighIrradianceLUTView, nullptr);
        rayleighIrradianceLUTView = VK_NULL_HANDLE;
    }
    if (rayleighIrradianceLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, rayleighIrradianceLUT, rayleighIrradianceLUTAllocation);
        rayleighIrradianceLUT = VK_NULL_HANDLE;
    }

    if (mieIrradianceLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, mieIrradianceLUTView, nullptr);
        mieIrradianceLUTView = VK_NULL_HANDLE;
    }
    if (mieIrradianceLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, mieIrradianceLUT, mieIrradianceLUTAllocation);
        mieIrradianceLUT = VK_NULL_HANDLE;
    }

    if (cloudMapLUTView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, cloudMapLUTView, nullptr);
        cloudMapLUTView = VK_NULL_HANDLE;
    }
    if (cloudMapLUT != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, cloudMapLUT, cloudMapLUTAllocation);
        cloudMapLUT = VK_NULL_HANDLE;
    }
}
