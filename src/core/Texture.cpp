#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "Texture.h"
#include "DDSLoader.h"
#include "VulkanRAII.h"
#include "VulkanResourceFactory.h"
#include "VulkanBarriers.h"
#include "ImageBuilder.h"
#include <cstring>
#include <algorithm>

// Check if a path ends with a specific extension (case-insensitive)
static bool hasExtension(const std::string& path, const std::string& ext) {
    if (path.size() < ext.size()) return false;
    std::string pathExt = path.substr(path.size() - ext.size());
    std::transform(pathExt.begin(), pathExt.end(), pathExt.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return pathExt == ext;
}

bool Texture::load(const std::string& path, VmaAllocator allocator, VkDevice device,
                   VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                   bool useSRGB) {

    // Check if this is a DDS file
    if (hasExtension(path, ".dds")) {
        return loadDDS(path, allocator, device, commandPool, queue, useSRGB);
    }

    // Load with stb_image (PNG, JPG, etc.)
    int channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture: %s", path.c_str());
        return false;
    }

    // Use scope guard to ensure pixels are freed on any exit path
    auto pixelGuard = makeScopeGuard([&]() { stbi_image_free(pixels); });

    VkDeviceSize imageSize = width * height * 4;

    // Create staging buffer using RAII
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for texture: %s", path.c_str());
        return false;
    }

    // Copy pixel data to staging buffer
    void* data = stagingBuffer.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map staging buffer for texture: %s", path.c_str());
        return false;
    }
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    stagingBuffer.unmap();

    VkFormat imageFormat = useSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Create image using ImageBuilder
    ManagedImage managedImage;
    if (!ImageBuilder(allocator)
            .setExtent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
            .setFormat(imageFormat)
            .asTexture()
            .build(managedImage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image for texture: %s", path.c_str());
        return false;
    }

    // Transition and copy using RAII command scope
    if (!transitionImageLayout(device, commandPool, queue, managedImage.get(),
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        return false;
    }

    if (!copyBufferToImage(device, commandPool, queue, stagingBuffer.get(), managedImage.get(),
                           static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }

    if (!transitionImageLayout(device, commandPool, queue, managedImage.get(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        return false;
    }

    // stagingBuffer automatically destroyed here

    // Create image view using RAII
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = managedImage.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    ManagedImageView managedView;
    if (!ManagedImageView::create(device, viewInfo, managedView)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for texture: %s", path.c_str());
        return false;
    }

    // Create sampler using RAII
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    ManagedSampler managedSampler;
    if (!ManagedSampler::create(device, samplerInfo, managedSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for texture: %s", path.c_str());
        return false;
    }

    // Success - transfer ownership to member variables
    managedImage.releaseToRaw(image, allocation);
    imageView = managedView.release();
    sampler = managedSampler.release();

    return true;
}

bool Texture::loadDDS(const std::string& path, VmaAllocator allocator, VkDevice device,
                      VkCommandPool commandPool, VkQueue queue, bool useSRGB) {
    DDSLoader::Image dds = DDSLoader::load(path);
    if (!dds.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load DDS texture: %s", path.c_str());
        return false;
    }

    width = static_cast<int>(dds.width);
    height = static_cast<int>(dds.height);

    // Get the format - adjust for sRGB if requested
    VkFormat imageFormat = dds.format;
    if (useSRGB) {
        // Convert to sRGB variant if available
        switch (dds.format) {
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
                imageFormat = VK_FORMAT_BC1_RGB_SRGB_BLOCK;
                break;
            case VK_FORMAT_BC7_UNORM_BLOCK:
                imageFormat = VK_FORMAT_BC7_SRGB_BLOCK;
                break;
            default:
                break;  // Keep original format
        }
    }

    VkDeviceSize imageSize = dds.data.size();

    // Create staging buffer
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for DDS texture: %s", path.c_str());
        return false;
    }

    // Copy compressed data to staging buffer
    void* data = stagingBuffer.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map staging buffer for DDS texture: %s", path.c_str());
        return false;
    }
    memcpy(data, dds.data.data(), imageSize);
    stagingBuffer.unmap();

    // Create image with BCn format
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = imageFormat;
    imageInfo.extent.width = dds.width;
    imageInfo.extent.height = dds.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = dds.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image for DDS texture: %s", path.c_str());
        return false;
    }

    // Transition and copy
    if (!transitionImageLayout(device, commandPool, queue, image,
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        return false;
    }

    if (!copyBufferToImage(device, commandPool, queue, stagingBuffer.get(), image,
                           dds.width, dds.height)) {
        return false;
    }

    if (!transitionImageLayout(device, commandPool, queue, image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = dds.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for DDS texture: %s", path.c_str());
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(dds.mipLevels);

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for DDS texture: %s", path.c_str());
        return false;
    }

    SDL_Log("Loaded DDS texture: %s (%ux%u, format %u)", path.c_str(), dds.width, dds.height, imageFormat);
    return true;
}

bool Texture::createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                               VmaAllocator allocator, VkDevice device,
                               VkCommandPool commandPool, VkQueue queue) {
    width = 1;
    height = 1;
    uint8_t pixels[4] = {r, g, b, a};

    VkDeviceSize imageSize = 4;

    // Create staging buffer using RAII
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for solid color texture");
        return false;
    }

    void* data = stagingBuffer.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map staging buffer for solid color texture");
        return false;
    }
    memcpy(data, pixels, imageSize);
    stagingBuffer.unmap();

    // Create image using ImageBuilder
    ManagedImage managedImage;
    if (!ImageBuilder(allocator)
            .setExtent(1, 1)
            .setFormat(VK_FORMAT_R8G8B8A8_SRGB)
            .asTexture()
            .build(managedImage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image for solid color texture");
        return false;
    }

    if (!transitionImageLayout(device, commandPool, queue, managedImage.get(),
                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
        return false;
    }

    if (!copyBufferToImage(device, commandPool, queue, stagingBuffer.get(), managedImage.get(), 1, 1)) {
        return false;
    }

    if (!transitionImageLayout(device, commandPool, queue, managedImage.get(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        return false;
    }

    // Create image view using RAII
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = managedImage.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    ManagedImageView managedView;
    if (!ManagedImageView::create(device, viewInfo, managedView)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for solid color texture");
        return false;
    }

    // Create sampler using RAII
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    ManagedSampler managedSampler;
    if (!ManagedSampler::create(device, samplerInfo, managedSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for solid color texture");
        return false;
    }

    // Success - transfer ownership to member variables
    managedImage.releaseToRaw(image, allocation);
    imageView = managedView.release();
    sampler = managedSampler.release();

    return true;
}

void Texture::destroy(VmaAllocator allocator, VkDevice device) {
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image, allocation);
        image = VK_NULL_HANDLE;
    }
}

bool Texture::transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                                    VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    CommandScope cmd(device, commandPool, queue);
    if (!cmd.begin()) {
        return false;
    }

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        Barriers::prepareImageForTransferDst(cmd.get(), image);
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        Barriers::imageTransferToSampling(cmd.get(), image);
    } else {
        Barriers::transitionImage(cmd.get(), image, oldLayout, newLayout,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0);
    }

    return cmd.end();
}

bool Texture::copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                                VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    CommandScope cmd(device, commandPool, queue);
    if (!cmd.begin()) {
        return false;
    }

    // Copy buffer to image (image must already be in TRANSFER_DST_OPTIMAL)
    Barriers::copyBufferToImageRegion(cmd.get(), buffer, image, 0, 0, width, height);

    return cmd.end();
}
