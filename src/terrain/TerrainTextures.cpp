#include "TerrainTextures.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
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
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setExtent(vk::Extent3D{width, height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &albedoImage, &albedoAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain albedo image");
        stbi_image_free(pixels);
        return false;
    }

    // Create image view
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(albedoImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &albedoView) != VK_SUCCESS) {
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
    // Use grass02 for variety - was previously loading grass01 twice (duplicate loading bug)
    std::string texturePath = resourcePath + "/grass/grass/grass02.jpg";

    int texWidth, texHeight, channels;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &channels, STBI_rgb_alpha);

    if (!pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass far LOD texture: %s", texturePath.c_str());
        return false;
    }

    uint32_t width = static_cast<uint32_t>(texWidth);
    uint32_t height = static_cast<uint32_t>(texHeight);

    // Create Vulkan image
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setExtent(vk::Extent3D{width, height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &grassFarLODImage, &grassFarLODAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass far LOD image");
        stbi_image_free(pixels);
        return false;
    }

    // Create image view
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(grassFarLODImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &grassFarLODView) != VK_SUCCESS) {
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
