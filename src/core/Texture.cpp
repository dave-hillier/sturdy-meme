#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "Texture.h"
#include "DDSLoader.h"
#include "VmaResources.h"
#include "VulkanHelpers.h"
#include "VulkanResourceFactory.h"
#include "VulkanBarriers.h"
#include "ImageBuilder.h"
#include <vulkan/vulkan.hpp>
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

    // Create image view using vulkan-hpp builder
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(managedImage.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(imageFormat))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    VkImageView createdView;
    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &createdView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for texture: %s", path.c_str());
        return false;
    }

    // Create sampler using vulkan-hpp builder
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setAnisotropyEnable(vk::False)
        .setMaxAnisotropy(1.0f)
        .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
        .setUnnormalizedCoordinates(vk::False)
        .setCompareEnable(vk::False)
        .setCompareOp(vk::CompareOp::eAlways)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setMipLodBias(0.0f)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    VkSampler createdSampler;
    if (vkCreateSampler(device, reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo), nullptr, &createdSampler) != VK_SUCCESS) {
        vkDestroyImageView(device, createdView, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for texture: %s", path.c_str());
        return false;
    }

    // Success - transfer ownership to member variables
    managedImage.releaseToRaw(image, allocation);
    imageView = createdView;
    sampler = createdSampler;

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

    // Create image with BCn format using vulkan-hpp builder
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(static_cast<vk::Format>(imageFormat))
        .setExtent(vk::Extent3D{dds.width, dds.height, 1})
        .setMipLevels(dds.mipLevels)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
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

    // Create image view using vulkan-hpp builder
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(imageFormat))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(dds.mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &imageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for DDS texture: %s", path.c_str());
        return false;
    }

    // Create sampler using vulkan-hpp builder
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setAnisotropyEnable(vk::False)
        .setMaxAnisotropy(1.0f)
        .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
        .setUnnormalizedCoordinates(vk::False)
        .setCompareEnable(vk::False)
        .setCompareOp(vk::CompareOp::eAlways)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setMipLodBias(0.0f)
        .setMinLod(0.0f)
        .setMaxLod(static_cast<float>(dds.mipLevels));

    if (vkCreateSampler(device, reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo), nullptr, &sampler) != VK_SUCCESS) {
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

    // Create image view using vulkan-hpp builder
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(managedImage.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR8G8B8A8Srgb)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    VkImageView createdView;
    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &createdView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for solid color texture");
        return false;
    }

    // Create sampler using vulkan-hpp builder
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setAnisotropyEnable(vk::False)
        .setMaxAnisotropy(1.0f)
        .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
        .setUnnormalizedCoordinates(vk::False)
        .setCompareEnable(vk::False)
        .setCompareOp(vk::CompareOp::eAlways)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setMipLodBias(0.0f)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    VkSampler createdSampler;
    if (vkCreateSampler(device, reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo), nullptr, &createdSampler) != VK_SUCCESS) {
        vkDestroyImageView(device, createdView, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for solid color texture");
        return false;
    }

    // Success - transfer ownership to member variables
    managedImage.releaseToRaw(image, allocation);
    imageView = createdView;
    sampler = createdSampler;

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

// Generate a single mip level on CPU with alpha-coverage preservation
// This keeps alpha values high enough to pass alpha testing at lower mip levels
static void generateMipLevelAlphaCoverage(
    const uint8_t* srcPixels, int srcWidth, int srcHeight,
    uint8_t* dstPixels, int dstWidth, int dstHeight,
    float alphaTestThreshold = 0.5f)
{
    for (int dy = 0; dy < dstHeight; ++dy) {
        for (int dx = 0; dx < dstWidth; ++dx) {
            // Sample 2x2 block from source
            int sx = dx * 2;
            int sy = dy * 2;

            float r = 0, g = 0, b = 0, a = 0;
            float totalWeight = 0;
            int passingPixels = 0;

            for (int oy = 0; oy < 2; ++oy) {
                for (int ox = 0; ox < 2; ++ox) {
                    int px = std::min(sx + ox, srcWidth - 1);
                    int py = std::min(sy + oy, srcHeight - 1);
                    const uint8_t* src = srcPixels + (py * srcWidth + px) * 4;

                    float srcA = src[3] / 255.0f;

                    // Count pixels that pass alpha test
                    if (srcA >= alphaTestThreshold) {
                        passingPixels++;
                    }

                    // Weight RGB by alpha for proper color blending
                    float weight = srcA;
                    r += src[0] * weight;
                    g += src[1] * weight;
                    b += src[2] * weight;
                    a += srcA;
                    totalWeight += weight;
                }
            }

            uint8_t* dst = dstPixels + (dy * dstWidth + dx) * 4;

            if (totalWeight > 0.001f) {
                // Normalize RGB by total weight (alpha-weighted average)
                dst[0] = static_cast<uint8_t>(std::clamp(r / totalWeight, 0.0f, 255.0f));
                dst[1] = static_cast<uint8_t>(std::clamp(g / totalWeight, 0.0f, 255.0f));
                dst[2] = static_cast<uint8_t>(std::clamp(b / totalWeight, 0.0f, 255.0f));
            } else {
                dst[0] = dst[1] = dst[2] = 0;
            }

            // Scale alpha to preserve coverage ratio
            // If 2 out of 4 pixels passed alpha test, output alpha should also pass
            float coverageRatio = passingPixels / 4.0f;
            float avgAlpha = a / 4.0f;

            // Boost alpha to maintain coverage: if any pixels passed, ensure output can pass too
            float outputAlpha;
            if (passingPixels > 0) {
                // Scale alpha so that coverage is preserved
                // If original coverage was 50% (2/4 pixels), output alpha should be ~threshold
                outputAlpha = std::max(avgAlpha, coverageRatio * (alphaTestThreshold + 0.1f) + (1.0f - coverageRatio) * avgAlpha);
                outputAlpha = std::min(outputAlpha, 1.0f);
            } else {
                outputAlpha = avgAlpha;
            }

            dst[3] = static_cast<uint8_t>(outputAlpha * 255.0f);
        }
    }
}

bool Texture::loadWithMipmaps(const std::string& path, VmaAllocator allocator, VkDevice device,
                               VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                               bool useSRGB, bool enableAnisotropy) {
    // Load with stb_image (PNG, JPG, etc.)
    int channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture: %s", path.c_str());
        return false;
    }

    auto pixelGuard = makeScopeGuard([&]() { stbi_image_free(pixels); });

    VkFormat imageFormat = useSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Calculate mip levels
    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // Generate all mip levels on CPU with alpha coverage preservation
    std::vector<std::vector<uint8_t>> mipData(mipLevels);
    std::vector<uint32_t> mipWidths(mipLevels);
    std::vector<uint32_t> mipHeights(mipLevels);

    // Level 0 is the original image
    mipWidths[0] = width;
    mipHeights[0] = height;
    mipData[0].resize(width * height * 4);
    memcpy(mipData[0].data(), pixels, width * height * 4);

    // Generate subsequent mip levels
    for (uint32_t i = 1; i < mipLevels; ++i) {
        mipWidths[i] = std::max(1u, mipWidths[i-1] / 2);
        mipHeights[i] = std::max(1u, mipHeights[i-1] / 2);
        mipData[i].resize(mipWidths[i] * mipHeights[i] * 4);

        generateMipLevelAlphaCoverage(
            mipData[i-1].data(), mipWidths[i-1], mipHeights[i-1],
            mipData[i].data(), mipWidths[i], mipHeights[i],
            0.5f  // Alpha test threshold
        );
    }

    // Calculate total size needed for staging buffer
    VkDeviceSize totalSize = 0;
    std::vector<VkDeviceSize> mipOffsets(mipLevels);
    for (uint32_t i = 0; i < mipLevels; ++i) {
        mipOffsets[i] = totalSize;
        totalSize += mipWidths[i] * mipHeights[i] * 4;
    }

    // Create staging buffer for all mip levels
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, totalSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for texture: %s", path.c_str());
        return false;
    }

    // Copy all mip levels to staging buffer
    uint8_t* data = static_cast<uint8_t*>(stagingBuffer.map());
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map staging buffer for texture: %s", path.c_str());
        return false;
    }
    for (uint32_t i = 0; i < mipLevels; ++i) {
        memcpy(data + mipOffsets[i], mipData[i].data(), mipData[i].size());
    }
    stagingBuffer.unmap();

    // Create image with mip levels
    ManagedImage managedImage;
    if (!ImageBuilder(allocator)
            .setExtent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
            .setFormat(imageFormat)
            .setMipLevels(mipLevels)
            .asTexture()  // Just transfer dst + sampled, no need for transfer src
            .build(managedImage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image for texture: %s", path.c_str());
        return false;
    }

    // Transition all mip levels to transfer dst using vulkan-hpp builder
    {
        CommandScope cmd(device, commandPool, queue);
        if (!cmd.begin()) return false;

        auto barrier = vk::ImageMemoryBarrier{}
            .setImage(managedImage.get())
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

        vkCmdPipelineBarrier(cmd.get(),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, reinterpret_cast<const VkImageMemoryBarrier*>(&barrier));

        if (!cmd.end()) return false;
    }

    // Copy each mip level from staging buffer to image
    {
        CommandScope cmd(device, commandPool, queue);
        if (!cmd.begin()) return false;

        std::vector<vk::BufferImageCopy> regions(mipLevels);
        for (uint32_t i = 0; i < mipLevels; ++i) {
            regions[i] = vk::BufferImageCopy{}
                .setBufferOffset(mipOffsets[i])
                .setBufferRowLength(0)
                .setBufferImageHeight(0)
                .setImageSubresource(vk::ImageSubresourceLayers{}
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setMipLevel(i)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1))
                .setImageOffset({0, 0, 0})
                .setImageExtent({mipWidths[i], mipHeights[i], 1});
        }

        vk::CommandBuffer vkCmd(cmd.get());
        vkCmd.copyBufferToImage(stagingBuffer.get(), managedImage.get(),
                                vk::ImageLayout::eTransferDstOptimal, regions);

        if (!cmd.end()) return false;
    }

    // Transition all mip levels to shader read using vulkan-hpp builder
    {
        CommandScope cmd(device, commandPool, queue);
        if (!cmd.begin()) return false;

        auto barrier = vk::ImageMemoryBarrier{}
            .setImage(managedImage.get())
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        vkCmdPipelineBarrier(cmd.get(),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, reinterpret_cast<const VkImageMemoryBarrier*>(&barrier));

        if (!cmd.end()) return false;
    }

    // Create image view with all mip levels using vulkan-hpp builder
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(managedImage.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(imageFormat))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    VkImageView createdView;
    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &createdView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for texture: %s", path.c_str());
        return false;
    }

    // Create sampler with mipmapping and optional anisotropy using vulkan-hpp builder
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setAnisotropyEnable(enableAnisotropy ? vk::True : vk::False)
        .setMaxAnisotropy(enableAnisotropy ? 8.0f : 1.0f)
        .setBorderColor(vk::BorderColor::eIntOpaqueBlack)
        .setUnnormalizedCoordinates(vk::False)
        .setCompareEnable(vk::False)
        .setCompareOp(vk::CompareOp::eAlways)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setMipLodBias(0.0f)
        .setMinLod(0.0f)
        .setMaxLod(static_cast<float>(mipLevels));

    VkSampler createdSampler;
    if (vkCreateSampler(device, reinterpret_cast<const VkSamplerCreateInfo*>(&samplerInfo), nullptr, &createdSampler) != VK_SUCCESS) {
        vkDestroyImageView(device, createdView, nullptr);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for texture: %s", path.c_str());
        return false;
    }

    // Transfer ownership
    managedImage.releaseToRaw(image, allocation);
    imageView = createdView;
    sampler = createdSampler;

    SDL_Log("Loaded texture with %u mip levels: %s (%dx%d)", mipLevels, path.c_str(), width, height);
    return true;
}
