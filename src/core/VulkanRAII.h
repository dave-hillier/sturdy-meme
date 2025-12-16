#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <utility>
#include <functional>

// ============================================================================
// VK_CHECK - Error checking macro for Vulkan calls
// ============================================================================

#define VK_CHECK(result) \
    do { \
        VkResult res_ = (result); \
        if (res_ != VK_SUCCESS) { \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, \
                "Vulkan error %d at %s:%d", res_, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

#define VK_CHECK_VOID(result) \
    do { \
        VkResult res_ = (result); \
        if (res_ != VK_SUCCESS) { \
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, \
                "Vulkan error %d at %s:%d", res_, __FILE__, __LINE__); \
            return; \
        } \
    } while(0)

// ============================================================================
// ScopeGuard - RAII cleanup helper for exception-safe resource management
// ============================================================================
// Usage:
//   auto guard = makeScopeGuard([&]() { cleanup(); });
//   // ... code that might fail ...
//   guard.dismiss();  // Only call if everything succeeded

template<typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F func) : cleanup_(std::move(func)), active_(true) {}

    ~ScopeGuard() {
        if (active_) {
            cleanup_();
        }
    }

    // Disable cleanup (call when operation succeeded)
    void dismiss() { active_ = false; }

    // Move-only
    ScopeGuard(ScopeGuard&& other) noexcept
        : cleanup_(std::move(other.cleanup_)), active_(other.active_) {
        other.active_ = false;
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

private:
    F cleanup_;
    bool active_;
};

template<typename F>
ScopeGuard<F> makeScopeGuard(F func) {
    return ScopeGuard<F>(std::move(func));
}

// ============================================================================
// ManagedBuffer - RAII wrapper for VkBuffer + VmaAllocation
// ============================================================================

class ManagedBuffer {
public:
    ManagedBuffer() = default;

    ~ManagedBuffer() {
        destroy();
    }

    // Move-only semantics
    ManagedBuffer(ManagedBuffer&& other) noexcept
        : buffer_(other.buffer_)
        , allocation_(other.allocation_)
        , allocator_(other.allocator_)
        , mapped_(other.mapped_) {
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
        other.mapped_ = false;
    }

    ManagedBuffer& operator=(ManagedBuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            buffer_ = other.buffer_;
            allocation_ = other.allocation_;
            allocator_ = other.allocator_;
            mapped_ = other.mapped_;
            other.buffer_ = VK_NULL_HANDLE;
            other.allocation_ = VK_NULL_HANDLE;
            other.allocator_ = VK_NULL_HANDLE;
            other.mapped_ = false;
        }
        return *this;
    }

    ManagedBuffer(const ManagedBuffer&) = delete;
    ManagedBuffer& operator=(const ManagedBuffer&) = delete;

    // Create a buffer with the given parameters
    static bool create(VmaAllocator allocator,
                       const VkBufferCreateInfo& bufferInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       ManagedBuffer& outBuffer) {
        ManagedBuffer result;
        result.allocator_ = allocator;

        VkResult vkResult = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                            &result.buffer_, &result.allocation_, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedBuffer::create failed: %d", vkResult);
            return false;
        }

        outBuffer = std::move(result);
        return true;
    }

    // Convenience factory for staging buffers
    static bool createStaging(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for vertex buffers
    static bool createVertex(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for index buffers
    static bool createIndex(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for uniform buffers (host-visible, mapped for CPU updates)
    static bool createUniform(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for storage buffers (device-local, GPU-only)
    static bool createStorage(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for storage buffers with host read access (for readback)
    static bool createStorageHostReadable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for readback buffers (GPU -> CPU transfer destination)
    static bool createReadback(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for indirect draw/dispatch buffers (GPU-only)
    static bool createIndirect(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for dynamic vertex buffers (CPU-visible, for per-frame updates)
    static bool createDynamicVertex(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for storage buffers with CPU write access (for uploading)
    static bool createStorageHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for vertex + storage buffers (CPU-writable, for meshlets used in compute)
    static bool createVertexStorage(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    // Convenience factory for index buffers with host write access (for CPU-initialized meshes)
    static bool createIndexHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        return create(allocator, bufferInfo, allocInfo, outBuffer);
    }

    void destroy() {
        if (buffer_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE) {
            // Unmap before destroying to avoid VMA assertion
            if (mapped_) {
                vmaUnmapMemory(allocator_, allocation_);
                mapped_ = false;
            }
            vmaDestroyBuffer(allocator_, buffer_, allocation_);
            buffer_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
        }
    }

    // Map memory for writing
    void* map() {
        if (allocator_ == VK_NULL_HANDLE || allocation_ == VK_NULL_HANDLE) {
            return nullptr;
        }
        if (mapped_) {
            // Already mapped - VMA allows multiple map calls but we should track it
            // Return the existing mapping via vmaGetAllocationInfo
            VmaAllocationInfo info;
            vmaGetAllocationInfo(allocator_, allocation_, &info);
            return info.pMappedData;
        }
        void* data = nullptr;
        if (vmaMapMemory(allocator_, allocation_, &data) != VK_SUCCESS) {
            return nullptr;
        }
        mapped_ = true;
        return data;
    }

    void unmap() {
        if (allocator_ != VK_NULL_HANDLE && allocation_ != VK_NULL_HANDLE && mapped_) {
            vmaUnmapMemory(allocator_, allocation_);
            mapped_ = false;
        }
    }

    bool isMapped() const { return mapped_; }

    // Accessors
    VkBuffer get() const { return buffer_; }
    VkBuffer* ptr() { return &buffer_; }
    VmaAllocation getAllocation() const { return allocation_; }
    VmaAllocator getAllocator() const { return allocator_; }
    explicit operator bool() const { return buffer_ != VK_NULL_HANDLE; }

    // Release ownership (for transferring to non-RAII code)
    VkBuffer release() {
        VkBuffer tmp = buffer_;
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        return tmp;
    }

    void releaseToRaw(VkBuffer& outBuffer, VmaAllocation& outAllocation) {
        outBuffer = buffer_;
        outAllocation = allocation_;
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    bool mapped_ = false;
};

// ============================================================================
// ManagedImage - RAII wrapper for VkImage + VmaAllocation
// ============================================================================

class ManagedImage {
public:
    ManagedImage() = default;

    ~ManagedImage() {
        destroy();
    }

    // Move-only semantics
    ManagedImage(ManagedImage&& other) noexcept
        : image_(other.image_)
        , allocation_(other.allocation_)
        , allocator_(other.allocator_) {
        other.image_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
    }

    ManagedImage& operator=(ManagedImage&& other) noexcept {
        if (this != &other) {
            destroy();
            image_ = other.image_;
            allocation_ = other.allocation_;
            allocator_ = other.allocator_;
            other.image_ = VK_NULL_HANDLE;
            other.allocation_ = VK_NULL_HANDLE;
            other.allocator_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedImage(const ManagedImage&) = delete;
    ManagedImage& operator=(const ManagedImage&) = delete;

    // Create an image with the given parameters
    static bool create(VmaAllocator allocator,
                       const VkImageCreateInfo& imageInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       ManagedImage& outImage) {
        ManagedImage result;
        result.allocator_ = allocator;

        VkResult vkResult = vmaCreateImage(allocator, &imageInfo, &allocInfo,
                                           &result.image_, &result.allocation_, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedImage::create failed: %d", vkResult);
            return false;
        }

        outImage = std::move(result);
        return true;
    }

    void destroy() {
        if (image_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE) {
            vmaDestroyImage(allocator_, image_, allocation_);
            image_ = VK_NULL_HANDLE;
            allocation_ = VK_NULL_HANDLE;
        }
    }

    // Adopt an existing raw image (takes ownership)
    static ManagedImage fromRaw(VmaAllocator allocator, VkImage image, VmaAllocation allocation) {
        ManagedImage result;
        result.allocator_ = allocator;
        result.image_ = image;
        result.allocation_ = allocation;
        return result;
    }

    // Accessors
    VkImage get() const { return image_; }
    VkImage* ptr() { return &image_; }
    VmaAllocation getAllocation() const { return allocation_; }
    explicit operator bool() const { return image_ != VK_NULL_HANDLE; }

    // Release ownership (for transferring to non-RAII code)
    void releaseToRaw(VkImage& outImage, VmaAllocation& outAllocation) {
        outImage = image_;
        outAllocation = allocation_;
        image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }

private:
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedImageView - RAII wrapper for VkImageView
// ============================================================================

class ManagedImageView {
public:
    ManagedImageView() = default;

    ~ManagedImageView() {
        destroy();
    }

    // Move-only semantics
    ManagedImageView(ManagedImageView&& other) noexcept
        : imageView_(other.imageView_)
        , device_(other.device_) {
        other.imageView_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedImageView& operator=(ManagedImageView&& other) noexcept {
        if (this != &other) {
            destroy();
            imageView_ = other.imageView_;
            device_ = other.device_;
            other.imageView_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedImageView(const ManagedImageView&) = delete;
    ManagedImageView& operator=(const ManagedImageView&) = delete;

    static bool create(VkDevice device,
                       const VkImageViewCreateInfo& viewInfo,
                       ManagedImageView& outView) {
        ManagedImageView result;
        result.device_ = device;

        VkResult vkResult = vkCreateImageView(device, &viewInfo, nullptr, &result.imageView_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedImageView::create failed: %d", vkResult);
            return false;
        }

        outView = std::move(result);
        return true;
    }

    void destroy() {
        if (imageView_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
    }

    // Adopt an existing raw image view
    static ManagedImageView fromRaw(VkDevice device, VkImageView imageView) {
        ManagedImageView result;
        result.device_ = device;
        result.imageView_ = imageView;
        return result;
    }

    VkImageView get() const { return imageView_; }
    explicit operator bool() const { return imageView_ != VK_NULL_HANDLE; }

    VkImageView release() {
        VkImageView tmp = imageView_;
        imageView_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkImageView imageView_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedSampler - RAII wrapper for VkSampler
// ============================================================================

class ManagedSampler {
public:
    ManagedSampler() = default;

    ~ManagedSampler() {
        destroy();
    }

    // Move-only semantics
    ManagedSampler(ManagedSampler&& other) noexcept
        : sampler_(other.sampler_)
        , device_(other.device_) {
        other.sampler_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedSampler& operator=(ManagedSampler&& other) noexcept {
        if (this != &other) {
            destroy();
            sampler_ = other.sampler_;
            device_ = other.device_;
            other.sampler_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedSampler(const ManagedSampler&) = delete;
    ManagedSampler& operator=(const ManagedSampler&) = delete;

    static bool create(VkDevice device,
                       const VkSamplerCreateInfo& samplerInfo,
                       ManagedSampler& outSampler) {
        ManagedSampler result;
        result.device_ = device;

        VkResult vkResult = vkCreateSampler(device, &samplerInfo, nullptr, &result.sampler_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedSampler::create failed: %d", vkResult);
            return false;
        }

        outSampler = std::move(result);
        return true;
    }

    // Convenience factory: nearest filtering with clamp-to-edge (depth/integer textures)
    static bool createNearestClamp(VkDevice device, ManagedSampler& outSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        return create(device, samplerInfo, outSampler);
    }

    // Convenience factory: linear filtering with clamp-to-edge
    static bool createLinearClamp(VkDevice device, ManagedSampler& outSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        return create(device, samplerInfo, outSampler);
    }

    // Convenience factory: linear filtering with repeat (standard textures)
    static bool createLinearRepeat(VkDevice device, ManagedSampler& outSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        return create(device, samplerInfo, outSampler);
    }

    // Convenience factory: linear filtering with repeat and anisotropy (terrain/high-quality textures)
    static bool createLinearRepeatAnisotropic(VkDevice device, float maxAnisotropy, ManagedSampler& outSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = maxAnisotropy;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

        return create(device, samplerInfo, outSampler);
    }

    // Convenience factory: shadow map comparison sampler
    static bool createShadowComparison(VkDevice device, ManagedSampler& outSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS;

        return create(device, samplerInfo, outSampler);
    }

    void destroy() {
        if (sampler_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
            sampler_ = VK_NULL_HANDLE;
        }
    }

    // Adopt an existing raw sampler
    static ManagedSampler fromRaw(VkDevice device, VkSampler sampler) {
        ManagedSampler result;
        result.device_ = device;
        result.sampler_ = sampler;
        return result;
    }

    VkSampler get() const { return sampler_; }
    explicit operator bool() const { return sampler_ != VK_NULL_HANDLE; }

    VkSampler release() {
        VkSampler tmp = sampler_;
        sampler_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedDescriptorSetLayout - RAII wrapper for VkDescriptorSetLayout
// ============================================================================

class ManagedDescriptorSetLayout {
public:
    ManagedDescriptorSetLayout() = default;

    ~ManagedDescriptorSetLayout() {
        destroy();
    }

    // Move-only semantics
    ManagedDescriptorSetLayout(ManagedDescriptorSetLayout&& other) noexcept
        : layout_(other.layout_)
        , device_(other.device_) {
        other.layout_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedDescriptorSetLayout& operator=(ManagedDescriptorSetLayout&& other) noexcept {
        if (this != &other) {
            destroy();
            layout_ = other.layout_;
            device_ = other.device_;
            other.layout_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedDescriptorSetLayout(const ManagedDescriptorSetLayout&) = delete;
    ManagedDescriptorSetLayout& operator=(const ManagedDescriptorSetLayout&) = delete;

    static bool create(VkDevice device,
                       const VkDescriptorSetLayoutCreateInfo& layoutInfo,
                       ManagedDescriptorSetLayout& outLayout) {
        ManagedDescriptorSetLayout result;
        result.device_ = device;

        VkResult vkResult = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &result.layout_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedDescriptorSetLayout::create failed: %d", vkResult);
            return false;
        }

        outLayout = std::move(result);
        return true;
    }

    // Adopt an existing raw descriptor set layout (e.g., created by DescriptorManager)
    static ManagedDescriptorSetLayout fromRaw(VkDevice device, VkDescriptorSetLayout layout) {
        ManagedDescriptorSetLayout result;
        result.device_ = device;
        result.layout_ = layout;
        return result;
    }

    void destroy() {
        if (layout_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
    }

    VkDescriptorSetLayout get() const { return layout_; }
    explicit operator bool() const { return layout_ != VK_NULL_HANDLE; }

    VkDescriptorSetLayout release() {
        VkDescriptorSetLayout tmp = layout_;
        layout_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedPipelineLayout - RAII wrapper for VkPipelineLayout
// ============================================================================

class ManagedPipelineLayout {
public:
    ManagedPipelineLayout() = default;

    ~ManagedPipelineLayout() {
        destroy();
    }

    // Move-only semantics
    ManagedPipelineLayout(ManagedPipelineLayout&& other) noexcept
        : layout_(other.layout_)
        , device_(other.device_) {
        other.layout_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedPipelineLayout& operator=(ManagedPipelineLayout&& other) noexcept {
        if (this != &other) {
            destroy();
            layout_ = other.layout_;
            device_ = other.device_;
            other.layout_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedPipelineLayout(const ManagedPipelineLayout&) = delete;
    ManagedPipelineLayout& operator=(const ManagedPipelineLayout&) = delete;

    static bool create(VkDevice device,
                       const VkPipelineLayoutCreateInfo& layoutInfo,
                       ManagedPipelineLayout& outLayout) {
        ManagedPipelineLayout result;
        result.device_ = device;

        VkResult vkResult = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &result.layout_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedPipelineLayout::create failed: %d", vkResult);
            return false;
        }

        outLayout = std::move(result);
        return true;
    }

    // Adopt an existing raw pipeline layout (e.g., created by PipelineBuilder)
    static ManagedPipelineLayout fromRaw(VkDevice device, VkPipelineLayout layout) {
        ManagedPipelineLayout result;
        result.device_ = device;
        result.layout_ = layout;
        return result;
    }

    void destroy() {
        if (layout_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, layout_, nullptr);
            layout_ = VK_NULL_HANDLE;
        }
    }

    VkPipelineLayout get() const { return layout_; }
    explicit operator bool() const { return layout_ != VK_NULL_HANDLE; }

    VkPipelineLayout release() {
        VkPipelineLayout tmp = layout_;
        layout_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedPipeline - RAII wrapper for VkPipeline
// ============================================================================

class ManagedPipeline {
public:
    ManagedPipeline() = default;

    ~ManagedPipeline() {
        destroy();
    }

    // Move-only semantics
    ManagedPipeline(ManagedPipeline&& other) noexcept
        : pipeline_(other.pipeline_)
        , device_(other.device_) {
        other.pipeline_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedPipeline& operator=(ManagedPipeline&& other) noexcept {
        if (this != &other) {
            destroy();
            pipeline_ = other.pipeline_;
            device_ = other.device_;
            other.pipeline_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedPipeline(const ManagedPipeline&) = delete;
    ManagedPipeline& operator=(const ManagedPipeline&) = delete;

    // Create graphics pipeline
    static bool createGraphics(VkDevice device,
                               VkPipelineCache pipelineCache,
                               const VkGraphicsPipelineCreateInfo& pipelineInfo,
                               ManagedPipeline& outPipeline) {
        ManagedPipeline result;
        result.device_ = device;

        VkResult vkResult = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &result.pipeline_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedPipeline::createGraphics failed: %d", vkResult);
            return false;
        }

        outPipeline = std::move(result);
        return true;
    }

    // Create compute pipeline
    static bool createCompute(VkDevice device,
                              VkPipelineCache pipelineCache,
                              const VkComputePipelineCreateInfo& pipelineInfo,
                              ManagedPipeline& outPipeline) {
        ManagedPipeline result;
        result.device_ = device;

        VkResult vkResult = vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &result.pipeline_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedPipeline::createCompute failed: %d", vkResult);
            return false;
        }

        outPipeline = std::move(result);
        return true;
    }

    // Adopt an existing raw pipeline (e.g., created by PipelineBuilder)
    static ManagedPipeline fromRaw(VkDevice device, VkPipeline pipeline) {
        ManagedPipeline result;
        result.device_ = device;
        result.pipeline_ = pipeline;
        return result;
    }

    void destroy() {
        if (pipeline_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
    }

    VkPipeline get() const { return pipeline_; }
    explicit operator bool() const { return pipeline_ != VK_NULL_HANDLE; }

    VkPipeline release() {
        VkPipeline tmp = pipeline_;
        pipeline_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedRenderPass - RAII wrapper for VkRenderPass
// ============================================================================

class ManagedRenderPass {
public:
    ManagedRenderPass() = default;

    ~ManagedRenderPass() {
        destroy();
    }

    // Move-only semantics
    ManagedRenderPass(ManagedRenderPass&& other) noexcept
        : renderPass_(other.renderPass_)
        , device_(other.device_) {
        other.renderPass_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedRenderPass& operator=(ManagedRenderPass&& other) noexcept {
        if (this != &other) {
            destroy();
            renderPass_ = other.renderPass_;
            device_ = other.device_;
            other.renderPass_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedRenderPass(const ManagedRenderPass&) = delete;
    ManagedRenderPass& operator=(const ManagedRenderPass&) = delete;

    static bool create(VkDevice device,
                       const VkRenderPassCreateInfo& renderPassInfo,
                       ManagedRenderPass& outRenderPass) {
        ManagedRenderPass result;
        result.device_ = device;

        VkResult vkResult = vkCreateRenderPass(device, &renderPassInfo, nullptr, &result.renderPass_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedRenderPass::create failed: %d", vkResult);
            return false;
        }

        outRenderPass = std::move(result);
        return true;
    }

    void destroy() {
        if (renderPass_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
    }

    // Adopt an existing raw render pass
    static ManagedRenderPass fromRaw(VkDevice device, VkRenderPass renderPass) {
        ManagedRenderPass result;
        result.device_ = device;
        result.renderPass_ = renderPass;
        return result;
    }

    VkRenderPass get() const { return renderPass_; }
    explicit operator bool() const { return renderPass_ != VK_NULL_HANDLE; }

    VkRenderPass release() {
        VkRenderPass tmp = renderPass_;
        renderPass_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedFramebuffer - RAII wrapper for VkFramebuffer
// ============================================================================

class ManagedFramebuffer {
public:
    ManagedFramebuffer() = default;

    ~ManagedFramebuffer() {
        destroy();
    }

    // Move-only semantics
    ManagedFramebuffer(ManagedFramebuffer&& other) noexcept
        : framebuffer_(other.framebuffer_)
        , device_(other.device_) {
        other.framebuffer_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedFramebuffer& operator=(ManagedFramebuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            framebuffer_ = other.framebuffer_;
            device_ = other.device_;
            other.framebuffer_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedFramebuffer(const ManagedFramebuffer&) = delete;
    ManagedFramebuffer& operator=(const ManagedFramebuffer&) = delete;

    static bool create(VkDevice device,
                       const VkFramebufferCreateInfo& framebufferInfo,
                       ManagedFramebuffer& outFramebuffer) {
        ManagedFramebuffer result;
        result.device_ = device;

        VkResult vkResult = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &result.framebuffer_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedFramebuffer::create failed: %d", vkResult);
            return false;
        }

        outFramebuffer = std::move(result);
        return true;
    }

    void destroy() {
        if (framebuffer_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, framebuffer_, nullptr);
            framebuffer_ = VK_NULL_HANDLE;
        }
    }

    // Adopt an existing raw framebuffer
    static ManagedFramebuffer fromRaw(VkDevice device, VkFramebuffer framebuffer) {
        ManagedFramebuffer result;
        result.device_ = device;
        result.framebuffer_ = framebuffer;
        return result;
    }

    VkFramebuffer get() const { return framebuffer_; }
    explicit operator bool() const { return framebuffer_ != VK_NULL_HANDLE; }

    VkFramebuffer release() {
        VkFramebuffer tmp = framebuffer_;
        framebuffer_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// CommandScope - RAII wrapper for one-time command buffer submission
// ============================================================================
// Usage:
//   CommandScope cmd(device, commandPool, queue);
//   if (!cmd.begin()) return false;
//   vkCmdCopyBuffer(cmd.get(), ...);
//   if (!cmd.end()) return false;

class CommandScope {
public:
    CommandScope(VkDevice device, VkCommandPool commandPool, VkQueue queue)
        : device_(device), commandPool_(commandPool), queue_(queue) {}

    ~CommandScope() {
        if (commandBuffer_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer_);
        }
    }

    // Non-copyable, non-movable
    CommandScope(const CommandScope&) = delete;
    CommandScope& operator=(const CommandScope&) = delete;
    CommandScope(CommandScope&&) = delete;
    CommandScope& operator=(CommandScope&&) = delete;

    bool begin() {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool_;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer_) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to allocate command buffer");
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer_, &beginInfo) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to begin command buffer");
            return false;
        }

        return true;
    }

    bool end() {
        if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to end command buffer");
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer_;

        if (vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to submit queue");
            return false;
        }

        vkQueueWaitIdle(queue_);
        return true;
    }

    VkCommandBuffer get() const { return commandBuffer_; }

private:
    VkDevice device_;
    VkCommandPool commandPool_;
    VkQueue queue_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedCommandPool - RAII wrapper for VkCommandPool
// ============================================================================

class ManagedCommandPool {
public:
    ManagedCommandPool() = default;

    ~ManagedCommandPool() {
        destroy();
    }

    // Move-only semantics
    ManagedCommandPool(ManagedCommandPool&& other) noexcept
        : commandPool_(other.commandPool_)
        , device_(other.device_) {
        other.commandPool_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedCommandPool& operator=(ManagedCommandPool&& other) noexcept {
        if (this != &other) {
            destroy();
            commandPool_ = other.commandPool_;
            device_ = other.device_;
            other.commandPool_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedCommandPool(const ManagedCommandPool&) = delete;
    ManagedCommandPool& operator=(const ManagedCommandPool&) = delete;

    static bool create(VkDevice device,
                       uint32_t queueFamilyIndex,
                       VkCommandPoolCreateFlags flags,
                       ManagedCommandPool& outPool) {
        ManagedCommandPool result;
        result.device_ = device;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = flags;
        poolInfo.queueFamilyIndex = queueFamilyIndex;

        VkResult vkResult = vkCreateCommandPool(device, &poolInfo, nullptr, &result.commandPool_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedCommandPool::create failed: %d", vkResult);
            return false;
        }

        outPool = std::move(result);
        return true;
    }

    void destroy() {
        if (commandPool_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
    }

    VkCommandPool get() const { return commandPool_; }
    explicit operator bool() const { return commandPool_ != VK_NULL_HANDLE; }

    VkCommandPool release() {
        VkCommandPool tmp = commandPool_;
        commandPool_ = VK_NULL_HANDLE;
        return tmp;
    }

private:
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedSemaphore - RAII wrapper for VkSemaphore
// ============================================================================

class ManagedSemaphore {
public:
    ManagedSemaphore() = default;

    ~ManagedSemaphore() {
        destroy();
    }

    // Move-only semantics
    ManagedSemaphore(ManagedSemaphore&& other) noexcept
        : semaphore_(other.semaphore_)
        , device_(other.device_) {
        other.semaphore_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedSemaphore& operator=(ManagedSemaphore&& other) noexcept {
        if (this != &other) {
            destroy();
            semaphore_ = other.semaphore_;
            device_ = other.device_;
            other.semaphore_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedSemaphore(const ManagedSemaphore&) = delete;
    ManagedSemaphore& operator=(const ManagedSemaphore&) = delete;

    static bool create(VkDevice device, ManagedSemaphore& outSemaphore) {
        ManagedSemaphore result;
        result.device_ = device;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkResult vkResult = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &result.semaphore_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedSemaphore::create failed: %d", vkResult);
            return false;
        }

        outSemaphore = std::move(result);
        return true;
    }

    void destroy() {
        if (semaphore_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore_, nullptr);
            semaphore_ = VK_NULL_HANDLE;
        }
    }

    VkSemaphore get() const { return semaphore_; }
    VkSemaphore* ptr() { return &semaphore_; }
    explicit operator bool() const { return semaphore_ != VK_NULL_HANDLE; }

private:
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};

// ============================================================================
// ManagedFence - RAII wrapper for VkFence
// ============================================================================

class ManagedFence {
public:
    ManagedFence() = default;

    ~ManagedFence() {
        destroy();
    }

    // Move-only semantics
    ManagedFence(ManagedFence&& other) noexcept
        : fence_(other.fence_)
        , device_(other.device_) {
        other.fence_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }

    ManagedFence& operator=(ManagedFence&& other) noexcept {
        if (this != &other) {
            destroy();
            fence_ = other.fence_;
            device_ = other.device_;
            other.fence_ = VK_NULL_HANDLE;
            other.device_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    ManagedFence(const ManagedFence&) = delete;
    ManagedFence& operator=(const ManagedFence&) = delete;

    static bool create(VkDevice device, VkFenceCreateFlags flags, ManagedFence& outFence) {
        ManagedFence result;
        result.device_ = device;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = flags;

        VkResult vkResult = vkCreateFence(device, &fenceInfo, nullptr, &result.fence_);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedFence::create failed: %d", vkResult);
            return false;
        }

        outFence = std::move(result);
        return true;
    }

    // Convenience: create signaled fence (common for frame synchronization)
    static bool createSignaled(VkDevice device, ManagedFence& outFence) {
        return create(device, VK_FENCE_CREATE_SIGNALED_BIT, outFence);
    }

    void destroy() {
        if (fence_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence_, nullptr);
            fence_ = VK_NULL_HANDLE;
        }
    }

    VkFence get() const { return fence_; }
    VkFence* ptr() { return &fence_; }
    explicit operator bool() const { return fence_ != VK_NULL_HANDLE; }

    // Convenience methods for fence operations
    VkResult wait(uint64_t timeout = UINT64_MAX) const {
        return vkWaitForFences(device_, 1, &fence_, VK_TRUE, timeout);
    }

    VkResult reset() const {
        return vkResetFences(device_, 1, &fence_);
    }

private:
    VkFence fence_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};
