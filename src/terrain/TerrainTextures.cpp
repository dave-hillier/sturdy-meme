#include "TerrainTextures.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <stb_image.h>
#include <cstring>
#include <cmath>
#include <algorithm>

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

    // Calculate mip levels
    albedoMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // Create Vulkan image with mip levels
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setExtent(vk::Extent3D{width, height, 1})
        .setMipLevels(albedoMipLevels)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &albedoImage, &albedoAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain albedo image");
        stbi_image_free(pixels);
        return false;
    }

    // Create image view with all mip levels
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(albedoImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(albedoMipLevels)
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

    // Upload base level texture to GPU
    if (!uploadImageDataMipLevel(albedoImage, pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB, 4, 0)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload terrain albedo texture to GPU");
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);

    // Generate mipmaps on GPU
    if (!generateMipmaps(albedoImage, width, height, albedoMipLevels)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate mipmaps for terrain albedo texture");
        return false;
    }

    SDL_Log("Terrain albedo texture loaded: %s (%ux%u, %u mip levels)", texturePath.c_str(), width, height, albedoMipLevels);
    return true;
}

bool TerrainTextures::createGrassFarLODTexture() {
    // Use same texture as near LOD for color consistency at distance
    std::string texturePath = resourcePath + "/grass/grass/grass01.jpg";

    int texWidth, texHeight, channels;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &channels, STBI_rgb_alpha);

    if (!pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass far LOD texture: %s", texturePath.c_str());
        return false;
    }

    uint32_t width = static_cast<uint32_t>(texWidth);
    uint32_t height = static_cast<uint32_t>(texHeight);

    // Calculate mip levels
    grassFarLODMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // Create Vulkan image with mip levels
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setExtent(vk::Extent3D{width, height, 1})
        .setMipLevels(grassFarLODMipLevels)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &grassFarLODImage, &grassFarLODAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grass far LOD image");
        stbi_image_free(pixels);
        return false;
    }

    // Create image view with all mip levels
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(grassFarLODImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(grassFarLODMipLevels)
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

    // Upload base level texture to GPU
    if (!uploadImageDataMipLevel(grassFarLODImage, pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB, 4, 0)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload grass far LOD texture to GPU");
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);

    // Generate mipmaps on GPU
    if (!generateMipmaps(grassFarLODImage, width, height, grassFarLODMipLevels)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate mipmaps for grass far LOD texture");
        return false;
    }

    SDL_Log("Grass far LOD texture loaded: %s (%ux%u, %u mip levels)", texturePath.c_str(), width, height, grassFarLODMipLevels);
    return true;
}

bool TerrainTextures::uploadImageDataMipLevel(VkImage image, const void* data, uint32_t width, uint32_t height,
                                               VkFormat format, uint32_t bytesPerPixel, uint32_t mipLevel) {
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

    vk::CommandBuffer vkCmd(cmd.get());

    // Transition base mip level to transfer dst
    auto barrier = vk::ImageMemoryBarrier{}
        .setImage(image)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(mipLevel)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

    vkCmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlags{},
        nullptr, nullptr, barrier);

    // Copy buffer to image
    auto region = vk::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(vk::ImageSubresourceLayers{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(mipLevel)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
        .setImageOffset({0, 0, 0})
        .setImageExtent({width, height, 1});

    vkCmd.copyBufferToImage(stagingBuffer.get(), image, vk::ImageLayout::eTransferDstOptimal, region);

    if (!cmd.end()) return false;

    // ManagedBuffer automatically destroyed on scope exit
    return true;
}

bool TerrainTextures::generateMipmaps(VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels) {
    CommandScope cmd(device, commandPool, graphicsQueue);
    if (!cmd.begin()) return false;

    vk::CommandBuffer vkCmd(cmd.get());

    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);

    for (uint32_t i = 1; i < mipLevels; ++i) {
        // Transition previous mip level from transfer dst to transfer src
        auto barrier = vk::ImageMemoryBarrier{}
            .setImage(image)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(i - 1)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eTransferRead);

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{},
            nullptr, nullptr, barrier);

        // Transition current mip level to transfer dst
        barrier.setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(i)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{},
            nullptr, nullptr, barrier);

        // Blit from previous mip level to current
        int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

        auto blit = vk::ImageBlit{}
            .setSrcSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(i - 1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setSrcOffsets({vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth, mipHeight, 1}})
            .setDstSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(i)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setDstOffsets({vk::Offset3D{0, 0, 0}, vk::Offset3D{nextMipWidth, nextMipHeight, 1}});

        vkCmd.blitImage(
            image, vk::ImageLayout::eTransferSrcOptimal,
            image, vk::ImageLayout::eTransferDstOptimal,
            blit, vk::Filter::eLinear);

        // Transition previous mip level to shader read
        barrier.setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(i - 1)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{},
            nullptr, nullptr, barrier);

        mipWidth = nextMipWidth;
        mipHeight = nextMipHeight;
    }

    // Transition last mip level to shader read
    auto finalBarrier = vk::ImageMemoryBarrier{}
        .setImage(image)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(mipLevels - 1)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1))
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    vkCmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader,
        vk::DependencyFlags{},
        nullptr, nullptr, finalBarrier);

    return cmd.end();
}
