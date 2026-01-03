#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <optional>
#include <SDL3/SDL_log.h>
#include <memory>

// ============================================================================
// VMA Resource Wrappers
// ============================================================================
// vulkan-hpp's vk::raii::* types don't integrate with VMA, so we need custom
// wrappers for VMA-allocated resources (buffers and images).

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

// ============================================================================
// Buffer Factory Functions
// ============================================================================

namespace VmaBufferFactory {

inline bool createStagingBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createVertexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createIndexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createUniformBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createStorageBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createStorageBufferHostReadable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createStorageBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createReadbackBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createVertexStorageBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createVertexStorageBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createIndexBufferHostWritable(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createIndirectBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eIndirectBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

inline bool createDynamicVertexBuffer(VmaAllocator allocator, vk::DeviceSize size, VmaBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return VmaBuffer::create(allocator, bufferInfo, allocInfo, outBuffer);
}

} // namespace VmaBufferFactory

// ============================================================================
// Type aliases for backward compatibility during migration
// ============================================================================

using ManagedBuffer = VmaBuffer;
using ManagedImage = VmaImage;

// ============================================================================
// Sampler Factory Functions
// ============================================================================

namespace SamplerFactory {

inline std::optional<vk::raii::Sampler> createSamplerNearestClamp(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerLinearClamp(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeat(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerLinearRepeatAnisotropic(
        const vk::raii::Device& device, float maxAnisotropy) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setAnisotropyEnable(vk::True)
        .setMaxAnisotropy(maxAnisotropy)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

inline std::optional<vk::raii::Sampler> createSamplerShadowComparison(const vk::raii::Device& device) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
        .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
        .setBorderColor(vk::BorderColor::eFloatOpaqueWhite)
        .setCompareEnable(vk::True)
        .setCompareOp(vk::CompareOp::eLess);

    try {
        return vk::raii::Sampler(device, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sampler: %s", e.what());
        return std::nullopt;
    }
}

} // namespace SamplerFactory
