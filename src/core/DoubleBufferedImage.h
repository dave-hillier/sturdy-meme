#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace BufferUtils {

// Double-buffered images for ping-pong rendering (temporal effects, SSR, etc.)
struct DoubleBufferedImageSet {
    VkImage images[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView views[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation allocations[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    bool isValid() const { return images[0] != VK_NULL_HANDLE && images[1] != VK_NULL_HANDLE; }
};

// Builder for double-buffered images (ping-pong for temporal effects)
class DoubleBufferedImageBuilder {
public:
    DoubleBufferedImageBuilder& setDevice(VkDevice device);
    DoubleBufferedImageBuilder& setAllocator(VmaAllocator allocator);
    DoubleBufferedImageBuilder& setExtent(uint32_t width, uint32_t height);
    DoubleBufferedImageBuilder& setExtent3D(uint32_t width, uint32_t height, uint32_t depth);
    DoubleBufferedImageBuilder& setFormat(VkFormat format);
    DoubleBufferedImageBuilder& setUsage(VkImageUsageFlags usage);
    DoubleBufferedImageBuilder& setAspectMask(VkImageAspectFlags aspect);

    bool build(DoubleBufferedImageSet& outImages) const;

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t depth_ = 1;
    VkFormat format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageUsageFlags usage_ = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlags aspectMask_ = VK_IMAGE_ASPECT_COLOR_BIT;
};

void destroyImages(VkDevice device, VmaAllocator allocator, DoubleBufferedImageSet& images);

}  // namespace BufferUtils
