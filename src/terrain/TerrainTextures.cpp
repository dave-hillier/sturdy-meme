#include "TerrainTextures.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <stb_image.h>
#include <cstring>

bool TerrainTextures::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    resourcePath = info.resourcePath;

    if (!createAlbedoTexture()) return false;
    if (!createGrassFarLODTexture()) return false;

    SDL_Log("TerrainTextures initialized");
    return true;
}

void TerrainTextures::destroy(VkDevice device, VmaAllocator allocator) {
    // Samplers via RAII
    albedoSampler.reset();
    if (albedoView) vkDestroyImageView(device, albedoView, nullptr);
    if (albedoImage) vmaDestroyImage(allocator, albedoImage, albedoAllocation);

    grassFarLODSampler.reset();
    if (grassFarLODView) vkDestroyImageView(device, grassFarLODView, nullptr);
    if (grassFarLODImage) vmaDestroyImage(allocator, grassFarLODImage, grassFarLODAllocation);

    albedoView = VK_NULL_HANDLE;
    albedoImage = VK_NULL_HANDLE;
    grassFarLODView = VK_NULL_HANDLE;
    grassFarLODImage = VK_NULL_HANDLE;
}

bool TerrainTextures::createAlbedoTexture() {
    std::string texturePath = resourcePath + "/grass/grass/grass01.jpg";

    int texWidth, texHeight, channels;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &channels, STBI_rgb_alpha);

    if (!pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load terrain albedo texture: %s", texturePath.c_str());
        return false;
    }

    uint32_t width = static_cast<uint32_t>(texWidth);
    uint32_t height = static_cast<uint32_t>(texHeight);

    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &albedoImage, &albedoAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain albedo image");
        stbi_image_free(pixels);
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = albedoImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &albedoView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain albedo image view");
        stbi_image_free(pixels);
        return false;
    }

    // Create sampler
    if (!VulkanResourceFactory::createSamplerLinearRepeatAnisotropic(device, 16.0f, albedoSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain albedo sampler");
        stbi_image_free(pixels);
        return false;
    }

    // Upload texture to GPU
    if (!uploadImageData(albedoImage, pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB, 4)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload terrain albedo texture to GPU");
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);
    SDL_Log("Terrain albedo texture loaded: %s (%ux%u)", texturePath.c_str(), width, height);
    return true;
}

bool TerrainTextures::createGrassFarLODTexture() {
    std::string texturePath = resourcePath + "/grass/grass/grass01.jpg";

    int texWidth, texHeight, channels;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &channels, STBI_rgb_alpha);

    if (!pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass far LOD texture: %s", texturePath.c_str());
        return false;
    }

    uint32_t width = static_cast<uint32_t>(texWidth);
    uint32_t height = static_cast<uint32_t>(texHeight);

    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &grassFarLODImage, &grassFarLODAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass far LOD image");
        stbi_image_free(pixels);
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = grassFarLODImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &grassFarLODView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass far LOD image view");
        stbi_image_free(pixels);
        return false;
    }

    // Create sampler
    if (!VulkanResourceFactory::createSamplerLinearRepeatAnisotropic(device, 16.0f, grassFarLODSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass far LOD sampler");
        stbi_image_free(pixels);
        return false;
    }

    // Upload texture to GPU
    if (!uploadImageData(grassFarLODImage, pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB, 4)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload grass far LOD texture to GPU");
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);
    SDL_Log("Grass far LOD texture loaded: %s (%ux%u)", texturePath.c_str(), width, height);
    return true;
}

bool TerrainTextures::uploadImageData(VkImage image, const void* data, uint32_t width, uint32_t height,
                                       VkFormat format, uint32_t bytesPerPixel) {
    VkDeviceSize imageSize = width * height * bytesPerPixel;

    // Create staging buffer using RAII wrapper
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for image upload");
        return false;
    }

    // Copy data to staging buffer
    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, data, imageSize);
    stagingBuffer.unmap();

    // Use CommandScope for one-time command submission
    CommandScope cmd(device, commandPool, graphicsQueue);
    if (!cmd.begin()) return false;

    // Copy staging buffer to image with automatic barrier transitions
    Barriers::copyBufferToImage(cmd.get(), stagingBuffer.get(), image, width, height);

    if (!cmd.end()) return false;

    // ManagedBuffer automatically destroyed on scope exit
    return true;
}
