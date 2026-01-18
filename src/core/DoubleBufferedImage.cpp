#include "DoubleBufferedImage.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>

namespace BufferUtils {

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setDevice(VkDevice newDevice) {
    device_ = newDevice;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setAllocator(VmaAllocator newAllocator) {
    allocator_ = newAllocator;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setExtent(uint32_t w, uint32_t h) {
    width_ = w;
    height_ = h;
    depth_ = 1;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setExtent3D(uint32_t w, uint32_t h, uint32_t d) {
    width_ = w;
    height_ = h;
    depth_ = d;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setFormat(VkFormat newFormat) {
    format_ = newFormat;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setUsage(VkImageUsageFlags newUsage) {
    usage_ = newUsage;
    return *this;
}

DoubleBufferedImageBuilder& DoubleBufferedImageBuilder::setAspectMask(VkImageAspectFlags aspect) {
    aspectMask_ = aspect;
    return *this;
}

bool DoubleBufferedImageBuilder::build(DoubleBufferedImageSet& outImages) const {
    if (!device_ || !allocator_ || width_ == 0 || height_ == 0) {
        SDL_Log("DoubleBufferedImageBuilder missing required fields (device=%p, allocator=%p, width=%u, height=%u)",
                device_, allocator_, width_, height_);
        return false;
    }

    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType((depth_ > 1) ? vk::ImageType::e3D : vk::ImageType::e2D)
        .setFormat(static_cast<vk::Format>(format_))
        .setExtent(vk::Extent3D{width_, height_, depth_})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(static_cast<vk::ImageUsageFlags>(usage_))
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    DoubleBufferedImageSet result{};

    // Create both images
    for (int i = 0; i < 2; i++) {
        if (vmaCreateImage(allocator_, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &result.images[i],
                           &result.allocations[i], nullptr) != VK_SUCCESS) {
            SDL_Log("Failed to create double-buffered image %d", i);
            // Clean up any already created
            for (int j = 0; j < i; j++) {
                vmaDestroyImage(allocator_, result.images[j], result.allocations[j]);
            }
            return false;
        }
    }

    // Create image views using vulkan-hpp builder
    for (int i = 0; i < 2; i++) {
        auto viewInfo = vk::ImageViewCreateInfo{}
            .setImage(result.images[i])
            .setViewType((depth_ > 1) ? vk::ImageViewType::e3D : vk::ImageViewType::e2D)
            .setFormat(static_cast<vk::Format>(format_))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(static_cast<vk::ImageAspectFlags>(aspectMask_))
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        vk::Device vkDevice(device_);
        auto viewResult = vkDevice.createImageView(viewInfo);
        if (viewResult == vk::ImageView{}) {
            SDL_Log("Failed to create double-buffered image view %d", i);
            // Clean up views and images
            for (int j = 0; j < i; j++) {
                vkDevice.destroyImageView(result.views[j]);
            }
            for (int j = 0; j < 2; j++) {
                vmaDestroyImage(allocator_, result.images[j], result.allocations[j]);
            }
            return false;
        }
        result.views[i] = static_cast<VkImageView>(viewResult);
    }

    outImages = result;
    return true;
}

void destroyImages(VkDevice device, VmaAllocator allocator, DoubleBufferedImageSet& images) {
    if (!device || !allocator) return;

    vk::Device vkDevice(device);
    for (int i = 0; i < 2; i++) {
        if (images.views[i] != VK_NULL_HANDLE) {
            vkDevice.destroyImageView(images.views[i]);
        }
        if (images.images[i] != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, images.images[i], images.allocations[i]);
        }
    }
    images = {};
}

}  // namespace BufferUtils
