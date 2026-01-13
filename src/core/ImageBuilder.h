#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <optional>
#include "VmaResources.h"
#include <SDL3/SDL_log.h>
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * ImageBuilder - Fluent builder for creating Vulkan images
 *
 * Simplifies image creation by providing sensible defaults and presets.
 * Reduces boilerplate for common image creation patterns.
 *
 * Example usage:
 *   ManagedImage image;
 *   ImageBuilder(allocator)
 *       .setExtent(width, height)
 *       .setFormat(VK_FORMAT_R8G8B8A8_SRGB)
 *       .asTexture()  // preset: sampled + transfer dst
 *       .build(image);
 */
class ImageBuilder {
public:
    explicit ImageBuilder(VmaAllocator allocator)
        : allocator_(allocator) {
        // Initialize with sensible defaults
        imageInfo_.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo_.imageType = VK_IMAGE_TYPE_2D;
        imageInfo_.extent = {1, 1, 1};
        imageInfo_.mipLevels = 1;
        imageInfo_.arrayLayers = 1;
        imageInfo_.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo_.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo_.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo_.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo_.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo_.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfo_.usage = VMA_MEMORY_USAGE_AUTO;
    }

    // Reset builder to defaults
    ImageBuilder& reset() {
        imageInfo_ = {};
        imageInfo_.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo_.imageType = VK_IMAGE_TYPE_2D;
        imageInfo_.extent = {1, 1, 1};
        imageInfo_.mipLevels = 1;
        imageInfo_.arrayLayers = 1;
        imageInfo_.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo_.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo_.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo_.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo_.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo_.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        allocInfo_ = {};
        allocInfo_.usage = VMA_MEMORY_USAGE_AUTO;

        return *this;
    }

    // ========================================================================
    // Dimension setters
    // ========================================================================

    ImageBuilder& setExtent(uint32_t width, uint32_t height, uint32_t depth = 1) {
        imageInfo_.extent = {width, height, depth};
        return *this;
    }

    ImageBuilder& setExtent(VkExtent2D extent) {
        imageInfo_.extent = {extent.width, extent.height, 1};
        return *this;
    }

    ImageBuilder& setExtent(VkExtent3D extent) {
        imageInfo_.extent = extent;
        return *this;
    }

    ImageBuilder& setMipLevels(uint32_t mipLevels) {
        imageInfo_.mipLevels = mipLevels;
        return *this;
    }

    // Calculate mip levels based on current extent
    ImageBuilder& setMipLevelsFromExtent() {
        imageInfo_.mipLevels = calculateMipLevels(imageInfo_.extent.width, imageInfo_.extent.height);
        return *this;
    }

    ImageBuilder& setArrayLayers(uint32_t arrayLayers) {
        imageInfo_.arrayLayers = arrayLayers;
        return *this;
    }

    // ========================================================================
    // Format and usage setters
    // ========================================================================

    ImageBuilder& setFormat(VkFormat format) {
        imageInfo_.format = format;
        return *this;
    }

    ImageBuilder& setUsage(VkImageUsageFlags usage) {
        imageInfo_.usage = usage;
        return *this;
    }

    ImageBuilder& addUsage(VkImageUsageFlags usage) {
        imageInfo_.usage |= usage;
        return *this;
    }

    ImageBuilder& setTiling(VkImageTiling tiling) {
        imageInfo_.tiling = tiling;
        return *this;
    }

    ImageBuilder& setSamples(VkSampleCountFlagBits samples) {
        imageInfo_.samples = samples;
        return *this;
    }

    ImageBuilder& setImageType(VkImageType imageType) {
        imageInfo_.imageType = imageType;
        return *this;
    }

    ImageBuilder& setFlags(VkImageCreateFlags flags) {
        imageInfo_.flags = flags;
        return *this;
    }

    ImageBuilder& addFlags(VkImageCreateFlags flags) {
        imageInfo_.flags |= flags;
        return *this;
    }

    // ========================================================================
    // Memory allocation options
    // ========================================================================

    ImageBuilder& setMemoryUsage(VmaMemoryUsage usage) {
        allocInfo_.usage = usage;
        return *this;
    }

    ImageBuilder& setAllocationFlags(VmaAllocationCreateFlags flags) {
        allocInfo_.flags = flags;
        return *this;
    }

    ImageBuilder& setGpuOnly() {
        allocInfo_.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        return *this;
    }

    // ========================================================================
    // Presets - common image configurations
    // ========================================================================

    // Standard texture for sampling (transfer dst + sampled)
    ImageBuilder& asTexture() {
        imageInfo_.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return *this;
    }

    // Texture with mipmaps (transfer src + dst + sampled)
    ImageBuilder& asTextureWithMipmaps() {
        imageInfo_.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return *this;
    }

    // Color attachment for rendering (color attachment + sampled)
    ImageBuilder& asColorAttachment() {
        imageInfo_.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return *this;
    }

    // Depth attachment
    ImageBuilder& asDepthAttachment() {
        imageInfo_.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        return *this;
    }

    // Depth attachment that can be sampled
    ImageBuilder& asSampledDepthAttachment() {
        imageInfo_.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return *this;
    }

    // Storage image for compute (storage + sampled)
    ImageBuilder& asStorageImage() {
        imageInfo_.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        return *this;
    }

    // Render target that can also be used as compute storage
    ImageBuilder& asRenderTargetStorage() {
        imageInfo_.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        return *this;
    }

    // Cube map compatible
    ImageBuilder& asCubeMap() {
        imageInfo_.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo_.arrayLayers = 6;
        return *this;
    }

    // ========================================================================
    // Build methods
    // ========================================================================

    bool build(ManagedImage& outImage) const {
        return ManagedImage::create(allocator_, imageInfo_, allocInfo_, outImage);
    }

    // Build image and view together using vk::raii::ImageView
    bool build(const vk::raii::Device& device, ManagedImage& outImage,
               std::optional<vk::raii::ImageView>& outView,
               vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor) const {
        if (!ManagedImage::create(allocator_, imageInfo_, allocInfo_, outImage)) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(outImage.get())
            .setViewType(static_cast<vk::ImageViewType>(getViewType()))
            .setFormat(static_cast<vk::Format>(imageInfo_.format))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(aspectMask)
                .setBaseMipLevel(0)
                .setLevelCount(imageInfo_.mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(imageInfo_.arrayLayers));

        try {
            outView.emplace(device, viewInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImageBuilder: Failed to create image view: %s", e.what());
            outImage.reset();
            return false;
        }

        return true;
    }

    // Build image and view together - outputs raw VkImageView (for legacy code compatibility)
    bool build(VkDevice device, ManagedImage& outImage, VkImageView& outView,
               VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT) const {
        if (!ManagedImage::create(allocator_, imageInfo_, allocInfo_, outImage)) {
            return false;
        }

        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(outImage.get())
            .setViewType(static_cast<vk::ImageViewType>(getViewType()))
            .setFormat(static_cast<vk::Format>(imageInfo_.format))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(static_cast<vk::ImageAspectFlags>(aspectMask))
                .setBaseMipLevel(0)
                .setLevelCount(imageInfo_.mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(imageInfo_.arrayLayers));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &outView) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImageBuilder: Failed to create image view");
            outImage.reset();
            return false;
        }

        return true;
    }

    // Build into raw handles (for legacy code compatibility)
    bool build(VkImage& outImage, VmaAllocation& outAllocation) const {
        ManagedImage managedImage;
        if (!ManagedImage::create(allocator_, imageInfo_, allocInfo_, managedImage)) {
            return false;
        }
        managedImage.releaseToRaw(outImage, outAllocation);
        return true;
    }

    // ========================================================================
    // Accessors (for inspection/debugging)
    // ========================================================================

    const VkImageCreateInfo& getImageInfo() const { return imageInfo_; }
    VkFormat getFormat() const { return imageInfo_.format; }
    VkExtent3D getExtent() const { return imageInfo_.extent; }
    uint32_t getMipLevels() const { return imageInfo_.mipLevels; }
    uint32_t getArrayLayers() const { return imageInfo_.arrayLayers; }

    // ========================================================================
    // Static utilities
    // ========================================================================

    static uint32_t calculateMipLevels(uint32_t width, uint32_t height) {
        return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    }

private:
    VkImageViewType getViewType() const {
        if (imageInfo_.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
            return imageInfo_.arrayLayers > 6 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
        }
        if (imageInfo_.arrayLayers > 1) {
            return imageInfo_.imageType == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        }
        switch (imageInfo_.imageType) {
            case VK_IMAGE_TYPE_1D: return VK_IMAGE_VIEW_TYPE_1D;
            case VK_IMAGE_TYPE_3D: return VK_IMAGE_VIEW_TYPE_3D;
            default: return VK_IMAGE_VIEW_TYPE_2D;
        }
    }

    VmaAllocator allocator_;
    VkImageCreateInfo imageInfo_{};
    VmaAllocationCreateInfo allocInfo_{};
};

/**
 * MipChainBuilder - Builder for creating images with multiple mip levels and per-level views
 *
 * Useful for Hi-Z pyramids, bloom chains, and other multi-resolution image hierarchies.
 *
 * Example usage:
 *   MipChainBuilder::Result result;
 *   MipChainBuilder(device, allocator)
 *       .setExtent(1920, 1080)
 *       .setFormat(VK_FORMAT_R32_SFLOAT)
 *       .asStorageImage()
 *       .build(result);
 *
 *   // result.image - the VkImage with all mip levels
 *   // result.fullView - view of all mip levels
 *   // result.mipViews - individual per-level views
 */
class MipChainBuilder {
public:
    struct Result {
        ManagedImage image;
        std::optional<vk::raii::ImageView> fullView;
        std::vector<std::optional<vk::raii::ImageView>> mipViews;
        uint32_t mipLevelCount = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;

        void reset() {
            mipViews.clear();
            fullView.reset();
            image.reset();
            mipLevelCount = 0;
            format = VK_FORMAT_UNDEFINED;
        }

        bool isValid() const {
            return image.get() != VK_NULL_HANDLE && fullView.has_value() && mipLevelCount > 0;
        }
    };

    MipChainBuilder(VkDevice device, VmaAllocator allocator)
        : device_(device), raiiDevice_(nullptr), allocator_(allocator), imageBuilder_(allocator) {}

    MipChainBuilder(const vk::raii::Device& raiiDevice, VmaAllocator allocator)
        : device_(static_cast<VkDevice>(*raiiDevice)), raiiDevice_(&raiiDevice), allocator_(allocator), imageBuilder_(allocator) {}

    MipChainBuilder& reset() {
        imageBuilder_.reset();
        aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
        return *this;
    }

    // ========================================================================
    // Dimension setters
    // ========================================================================

    MipChainBuilder& setExtent(uint32_t width, uint32_t height) {
        imageBuilder_.setExtent(width, height);
        return *this;
    }

    MipChainBuilder& setExtent(VkExtent2D extent) {
        imageBuilder_.setExtent(extent);
        return *this;
    }

    MipChainBuilder& setMipLevels(uint32_t mipLevels) {
        imageBuilder_.setMipLevels(mipLevels);
        return *this;
    }

    // Auto-calculate mip levels from extent (default behavior)
    MipChainBuilder& setMipLevelsAuto() {
        autoMipLevels_ = true;
        return *this;
    }

    // ========================================================================
    // Format and usage setters
    // ========================================================================

    MipChainBuilder& setFormat(VkFormat format) {
        imageBuilder_.setFormat(format);
        return *this;
    }

    MipChainBuilder& setUsage(VkImageUsageFlags usage) {
        imageBuilder_.setUsage(usage);
        return *this;
    }

    MipChainBuilder& addUsage(VkImageUsageFlags usage) {
        imageBuilder_.addUsage(usage);
        return *this;
    }

    MipChainBuilder& setAspectMask(VkImageAspectFlags aspectMask) {
        aspectMask_ = aspectMask;
        return *this;
    }

    // ========================================================================
    // Presets
    // ========================================================================

    // Storage image for compute-based mip generation
    MipChainBuilder& asStorageImage() {
        imageBuilder_.asStorageImage();
        return *this;
    }

    // Color attachment chain (for graphics-based mip generation)
    MipChainBuilder& asColorAttachment() {
        imageBuilder_.asColorAttachment();
        return *this;
    }

    // Depth pyramid (for Hi-Z occlusion culling)
    MipChainBuilder& asDepthPyramid() {
        imageBuilder_.asStorageImage();
        aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;  // R32_SFLOAT stores depth, uses color aspect
        return *this;
    }

    // ========================================================================
    // Build
    // ========================================================================

    bool build(Result& outResult) {
        outResult.reset();

        // Calculate mip levels if auto
        if (autoMipLevels_) {
            imageBuilder_.setMipLevelsFromExtent();
        }

        const auto& imageInfo = imageBuilder_.getImageInfo();
        uint32_t mipLevelCount = imageInfo.mipLevels;

        if (mipLevelCount == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MipChainBuilder: Invalid mip level count");
            return false;
        }

        // Create the image
        if (!imageBuilder_.build(outResult.image)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MipChainBuilder: Failed to create image");
            return false;
        }

        outResult.mipLevelCount = mipLevelCount;
        outResult.format = imageInfo.format;

        // Need raiiDevice for creating views
        if (!raiiDevice_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MipChainBuilder: raiiDevice required for creating views");
            outResult.reset();
            return false;
        }

        // Create full image view (all mip levels)
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(outResult.image.get())
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(static_cast<vk::Format>(imageInfo.format))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(static_cast<vk::ImageAspectFlags>(aspectMask_))
                .setBaseMipLevel(0)
                .setLevelCount(mipLevelCount)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        try {
            outResult.fullView.emplace(*raiiDevice_, viewInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MipChainBuilder: Failed to create full view: %s", e.what());
            outResult.reset();
            return false;
        }

        // Create per-mip-level views
        outResult.mipViews.resize(mipLevelCount);
        for (uint32_t i = 0; i < mipLevelCount; ++i) {
            viewInfo.subresourceRange.baseMipLevel = i;
            viewInfo.subresourceRange.levelCount = 1;

            try {
                outResult.mipViews[i].emplace(*raiiDevice_, viewInfo);
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "MipChainBuilder: Failed to create mip view %u: %s", i, e.what());
                outResult.reset();
                return false;
            }
        }

        return true;
    }

    // ========================================================================
    // Static utilities
    // ========================================================================

    static uint32_t calculateMipLevels(uint32_t width, uint32_t height) {
        return ImageBuilder::calculateMipLevels(width, height);
    }

    static VkExtent2D getMipExtent(VkExtent2D baseExtent, uint32_t mipLevel) {
        return {
            std::max(1u, baseExtent.width >> mipLevel),
            std::max(1u, baseExtent.height >> mipLevel)
        };
    }

private:
    VkDevice device_;
    const vk::raii::Device* raiiDevice_ = nullptr;
    VmaAllocator allocator_;
    ImageBuilder imageBuilder_;
    VkImageAspectFlags aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
    bool autoMipLevels_ = true;
};

/**
 * ImageWithView - RAII container for a Vulkan image with its associated view
 *
 * Bundles a ManagedImage (VkImage + VmaAllocation) with a vk::raii::ImageView
 * for convenient lifetime management and passing around.
 *
 * Example usage:
 *   auto result = createImageWithView(device, allocator, 1920, 1080,
 *                                      VK_FORMAT_R8G8B8A8_SRGB,
 *                                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
 *   if (!result.isValid()) {
 *       // handle error
 *   }
 *   // Use result.image and result.view
 */
struct ImageWithView {
    ManagedImage image;
    std::optional<vk::raii::ImageView> view;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent = {0, 0, 0};
    uint32_t mipLevels = 0;
    uint32_t arrayLayers = 0;

    void reset() {
        view.reset();
        image.reset();
        format = VK_FORMAT_UNDEFINED;
        extent = {0, 0, 0};
        mipLevels = 0;
        arrayLayers = 0;
    }

    [[nodiscard]] bool isValid() const {
        return image.get() != VK_NULL_HANDLE && view.has_value();
    }

    // Get the raw VkImage handle
    [[nodiscard]] VkImage getImage() const { return image.get(); }

    // Get the raw VkImageView handle
    [[nodiscard]] VkImageView getView() const {
        return view.has_value() ? static_cast<VkImageView>(**view) : VK_NULL_HANDLE;
    }
};

/**
 * createImageWithView - Convenience function to create an image with its view
 *
 * Combines image creation, memory allocation, and view creation in one call.
 * Uses sensible defaults for most parameters.
 *
 * @param device      The Vulkan RAII device
 * @param allocator   The VMA allocator
 * @param width       Image width in pixels
 * @param height      Image height in pixels
 * @param format      Vulkan format for the image
 * @param usage       Image usage flags
 * @param aspectMask  Image aspect mask (default: eColor)
 * @param mipLevels   Number of mip levels (default: 1)
 * @param arrayLayers Number of array layers (default: 1)
 * @return ImageWithView containing the created resources, check isValid() for success
 */
inline ImageWithView createImageWithView(
    const vk::raii::Device& device,
    VmaAllocator allocator,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
    uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1) {

    ImageWithView result;

    ImageBuilder builder(allocator);
    builder.setExtent(width, height)
           .setFormat(format)
           .setUsage(usage)
           .setMipLevels(mipLevels)
           .setArrayLayers(arrayLayers);

    if (!builder.build(device, result.image, result.view, aspectMask)) {
        result.reset();
        return result;
    }

    result.format = format;
    result.extent = {width, height, 1};
    result.mipLevels = mipLevels;
    result.arrayLayers = arrayLayers;

    return result;
}

/**
 * createImageWithView - Overload accepting VkExtent2D
 */
inline ImageWithView createImageWithView(
    const vk::raii::Device& device,
    VmaAllocator allocator,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
    uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1) {

    return createImageWithView(device, allocator, extent.width, extent.height,
                               format, usage, aspectMask, mipLevels, arrayLayers);
}

/**
 * createImageWithView - Overload using ImageBuilder for advanced configuration
 *
 * Use when you need more control over image creation parameters.
 *
 * Example:
 *   ImageBuilder builder(allocator);
 *   builder.setExtent(1920, 1080)
 *          .setFormat(VK_FORMAT_R16G16B16A16_SFLOAT)
 *          .asColorAttachment()
 *          .setSamples(VK_SAMPLE_COUNT_4_BIT);
 *   auto result = createImageWithView(device, builder);
 */
inline ImageWithView createImageWithView(
    const vk::raii::Device& device,
    const ImageBuilder& builder,
    vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor) {

    ImageWithView result;

    ManagedImage image;
    std::optional<vk::raii::ImageView> view;

    if (!builder.build(device, image, view, aspectMask)) {
        return result;
    }

    result.image = std::move(image);
    result.view = std::move(view);
    result.format = builder.getFormat();
    result.extent = builder.getExtent();
    result.mipLevels = builder.getMipLevels();
    result.arrayLayers = builder.getArrayLayers();

    return result;
}
