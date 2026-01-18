#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <memory>

// ============================================================================
// VmaBufferDeleter - Deleter for VMA-allocated buffers
// ============================================================================

struct VmaBufferDeleter {
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    mutable bool mapped = false;

    using pointer = VkBuffer;

    void operator()(VkBuffer buffer) const noexcept {
        if (buffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            if (mapped && allocation != VK_NULL_HANDLE) {
                vmaUnmapMemory(allocator, allocation);
            }
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }
};

using UniqueVmaBuffer = std::unique_ptr<std::remove_pointer_t<VkBuffer>, VmaBufferDeleter>;

inline UniqueVmaBuffer makeUniqueVmaBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) {
    return UniqueVmaBuffer(buffer, {allocator, allocation});
}

// ============================================================================
// VmaBuffer - RAII wrapper for VkBuffer + VmaAllocation
// ============================================================================

class VmaBuffer : public UniqueVmaBuffer {
public:
    using UniqueVmaBuffer::UniqueVmaBuffer;

    VmaBuffer() = default;

    VmaBuffer(UniqueVmaBuffer&& other) noexcept
        : UniqueVmaBuffer(std::move(other)) {}

    // Create using vulkan-hpp BufferCreateInfo
    static bool create(VmaAllocator allocator,
                       const vk::BufferCreateInfo& bufferInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       VmaBuffer& outBuffer) {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult vkResult = vmaCreateBuffer(allocator,
            reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
            &allocInfo, &buffer, &allocation, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "VmaBuffer::create failed: %d", vkResult);
            return false;
        }

        outBuffer = VmaBuffer(makeUniqueVmaBuffer(allocator, buffer, allocation));
        return true;
    }

    // Create using raw VkBufferCreateInfo (for compatibility)
    static bool create(VmaAllocator allocator,
                       const VkBufferCreateInfo& bufferInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       VmaBuffer& outBuffer) {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult vkResult = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                            &buffer, &allocation, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "VmaBuffer::create failed: %d", vkResult);
            return false;
        }

        outBuffer = VmaBuffer(makeUniqueVmaBuffer(allocator, buffer, allocation));
        return true;
    }

    static VmaBuffer fromRaw(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) {
        return VmaBuffer(makeUniqueVmaBuffer(allocator, buffer, allocation));
    }

    VmaAllocator allocator() const { return get_deleter().allocator; }
    VmaAllocation getAllocation() const { return get_deleter().allocation; }

    void* map() {
        VmaAllocator alloc = allocator();
        VmaAllocation allocation = getAllocation();
        if (alloc == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
            return nullptr;
        }
        void* data = nullptr;
        if (vmaMapMemory(alloc, allocation, &data) != VK_SUCCESS) {
            return nullptr;
        }
        get_deleter().mapped = true;
        return data;
    }

    void unmap() {
        VmaAllocator alloc = allocator();
        VmaAllocation allocation = getAllocation();
        if (alloc == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) {
            return;
        }
        vmaUnmapMemory(alloc, allocation);
        get_deleter().mapped = false;
    }

    void releaseToRaw(VkBuffer& outBuffer, VmaAllocation& outAllocation) {
        outBuffer = get();
        outAllocation = get_deleter().allocation;
        release();
    }
};

// Type alias for backward compatibility
using ManagedBuffer = VmaBuffer;
