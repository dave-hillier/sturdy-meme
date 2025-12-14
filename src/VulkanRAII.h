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
        , allocator_(other.allocator_) {
        other.buffer_ = VK_NULL_HANDLE;
        other.allocation_ = VK_NULL_HANDLE;
        other.allocator_ = VK_NULL_HANDLE;
    }

    ManagedBuffer& operator=(ManagedBuffer&& other) noexcept {
        if (this != &other) {
            destroy();
            buffer_ = other.buffer_;
            allocation_ = other.allocation_;
            allocator_ = other.allocator_;
            other.buffer_ = VK_NULL_HANDLE;
            other.allocation_ = VK_NULL_HANDLE;
            other.allocator_ = VK_NULL_HANDLE;
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

    void destroy() {
        if (buffer_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE) {
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
        void* data = nullptr;
        if (vmaMapMemory(allocator_, allocation_, &data) != VK_SUCCESS) {
            return nullptr;
        }
        return data;
    }

    void unmap() {
        if (allocator_ != VK_NULL_HANDLE && allocation_ != VK_NULL_HANDLE) {
            vmaUnmapMemory(allocator_, allocation_);
        }
    }

    // Accessors
    VkBuffer get() const { return buffer_; }
    VkBuffer* ptr() { return &buffer_; }
    VmaAllocation getAllocation() const { return allocation_; }
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

    void destroy() {
        if (sampler_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, sampler_, nullptr);
            sampler_ = VK_NULL_HANDLE;
        }
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
