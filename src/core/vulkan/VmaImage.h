#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <memory>

// ============================================================================
// VmaImageDeleter - Deleter for VMA-allocated images
// ============================================================================

struct VmaImageDeleter {
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    using pointer = VkImage;

    void operator()(VkImage image) const noexcept {
        if (image != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator, image, allocation);
        }
    }
};

using UniqueVmaImage = std::unique_ptr<std::remove_pointer_t<VkImage>, VmaImageDeleter>;

inline UniqueVmaImage makeUniqueVmaImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation) {
    return UniqueVmaImage(image, {allocator, allocation});
}

// ============================================================================
// VmaImage - RAII wrapper for VkImage + VmaAllocation
// ============================================================================

class VmaImage : public UniqueVmaImage {
public:
    using UniqueVmaImage::UniqueVmaImage;

    VmaImage() = default;

    VmaImage(UniqueVmaImage&& other) noexcept
        : UniqueVmaImage(std::move(other)) {}

    // Create using vulkan-hpp ImageCreateInfo
    static bool create(VmaAllocator allocator,
                       const vk::ImageCreateInfo& imageInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       VmaImage& outImage) {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult vkResult = vmaCreateImage(allocator,
            reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
            &allocInfo, &image, &allocation, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "VmaImage::create failed: %d", vkResult);
            return false;
        }

        outImage = VmaImage(makeUniqueVmaImage(allocator, image, allocation));
        return true;
    }

    // Create using raw VkImageCreateInfo (for compatibility)
    static bool create(VmaAllocator allocator,
                       const VkImageCreateInfo& imageInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       VmaImage& outImage) {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult vkResult = vmaCreateImage(allocator, &imageInfo, &allocInfo,
                                           &image, &allocation, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "VmaImage::create failed: %d", vkResult);
            return false;
        }

        outImage = VmaImage(makeUniqueVmaImage(allocator, image, allocation));
        return true;
    }

    static VmaImage fromRaw(VmaAllocator allocator, VkImage image, VmaAllocation allocation) {
        return VmaImage(makeUniqueVmaImage(allocator, image, allocation));
    }

    VmaAllocator allocator() const { return get_deleter().allocator; }
    VmaAllocation getAllocation() const { return get_deleter().allocation; }

    void releaseToRaw(VkImage& outImage, VmaAllocation& outAllocation) {
        outImage = get();
        outAllocation = get_deleter().allocation;
        release();
    }
};

// Type alias for backward compatibility
using ManagedImage = VmaImage;
