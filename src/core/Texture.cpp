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

    // Transition all mip levels to transfer dst
    {
        CommandScope cmd(device, commandPool, queue);
        if (!cmd.begin()) return false;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = managedImage.get();
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd.get(),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        if (!cmd.end()) return false;
    }

    // Copy each mip level from staging buffer to image
    {
        CommandScope cmd(device, commandPool, queue);
        if (!cmd.begin()) return false;

        std::vector<VkBufferImageCopy> regions(mipLevels);
        for (uint32_t i = 0; i < mipLevels; ++i) {
            regions[i] = {};
            regions[i].bufferOffset = mipOffsets[i];
            regions[i].bufferRowLength = 0;
            regions[i].bufferImageHeight = 0;
            regions[i].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[i].imageSubresource.mipLevel = i;
            regions[i].imageSubresource.baseArrayLayer = 0;
            regions[i].imageSubresource.layerCount = 1;
            regions[i].imageOffset = {0, 0, 0};
            regions[i].imageExtent = {mipWidths[i], mipHeights[i], 1};
        }

        vkCmdCopyBufferToImage(cmd.get(), stagingBuffer.get(), managedImage.get(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());

        if (!cmd.end()) return false;
    }

    // Transition all mip levels to shader read
    {
        CommandScope cmd(device, commandPool, queue);
        if (!cmd.begin()) return false;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = managedImage.get();
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd.get(),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        if (!cmd.end()) return false;
    }

    // Create image view with all mip levels
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = managedImage.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    ManagedImageView managedView;
    if (!ManagedImageView::create(device, viewInfo, managedView)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create image view for texture: %s", path.c_str());
        return false;
    }

    // Create sampler with mipmapping and optional anisotropy
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = enableAnisotropy ? 8.0f : 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    ManagedSampler managedSampler;
    if (!ManagedSampler::create(device, samplerInfo, managedSampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler for texture: %s", path.c_str());
        return false;
    }

    // Transfer ownership
    managedImage.releaseToRaw(image, allocation);
    imageView = managedView.release();
    sampler = managedSampler.release();

    SDL_Log("Loaded texture with %u mip levels: %s (%dx%d)", mipLevels, path.c_str(), width, height);
    return true;
}
