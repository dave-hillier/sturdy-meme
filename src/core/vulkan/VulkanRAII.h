#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <SDL3/SDL_log.h>
#include <utility>
#include <functional>
#include <memory>
#include <vector>

// ============================================================================
// VkHandleDeleter - Generic deleter for Vulkan handles using unique_ptr
// ============================================================================
// This template enables using std::unique_ptr for RAII management of Vulkan
// handles that only require a VkDevice for destruction.

template<typename Handle, void (*DestroyFn)(VkDevice, Handle, const VkAllocationCallbacks*)>
struct VkHandleDeleter {
    VkDevice device = VK_NULL_HANDLE;

    using pointer = Handle;  // Required for unique_ptr with non-pointer types

    void operator()(Handle handle) const noexcept {
        if (handle != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            DestroyFn(device, handle, nullptr);
        }
    }
};

// ============================================================================
// VmaImageDeleter - Deleter for VMA-allocated images
// ============================================================================
// VMA images require both allocator and allocation for destruction.
// This deleter stores the allocation alongside the allocator.

struct VmaImageDeleter {
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    using pointer = VkImage;  // Required for unique_ptr with non-pointer types

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
// VMA buffers require both allocator and allocation for destruction.
// This deleter stores the allocation alongside the allocator.
// Also tracks mapped state to auto-unmap before destruction.

struct VmaBufferDeleter {
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    mutable bool mapped = false;  // Track if buffer is currently mapped

    using pointer = VkBuffer;  // Required for unique_ptr with non-pointer types

    void operator()(VkBuffer buffer) const noexcept {
        if (buffer != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE) {
            // Auto-unmap if still mapped to avoid VMA assertion
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
// Unique* type aliases - RAII wrappers using std::unique_ptr
// ============================================================================

using UniquePipeline = std::unique_ptr<
    std::remove_pointer_t<VkPipeline>,
    VkHandleDeleter<VkPipeline, vkDestroyPipeline>>;

using UniqueRenderPass = std::unique_ptr<
    std::remove_pointer_t<VkRenderPass>,
    VkHandleDeleter<VkRenderPass, vkDestroyRenderPass>>;

using UniquePipelineLayout = std::unique_ptr<
    std::remove_pointer_t<VkPipelineLayout>,
    VkHandleDeleter<VkPipelineLayout, vkDestroyPipelineLayout>>;

using UniqueDescriptorSetLayout = std::unique_ptr<
    std::remove_pointer_t<VkDescriptorSetLayout>,
    VkHandleDeleter<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>>;

using UniqueImageView = std::unique_ptr<
    std::remove_pointer_t<VkImageView>,
    VkHandleDeleter<VkImageView, vkDestroyImageView>>;

using UniqueFramebuffer = std::unique_ptr<
    std::remove_pointer_t<VkFramebuffer>,
    VkHandleDeleter<VkFramebuffer, vkDestroyFramebuffer>>;

using UniqueFence = std::unique_ptr<
    std::remove_pointer_t<VkFence>,
    VkHandleDeleter<VkFence, vkDestroyFence>>;

using UniqueSemaphore = std::unique_ptr<
    std::remove_pointer_t<VkSemaphore>,
    VkHandleDeleter<VkSemaphore, vkDestroySemaphore>>;

using UniqueCommandPool = std::unique_ptr<
    std::remove_pointer_t<VkCommandPool>,
    VkHandleDeleter<VkCommandPool, vkDestroyCommandPool>>;

using UniqueDescriptorPool = std::unique_ptr<
    std::remove_pointer_t<VkDescriptorPool>,
    VkHandleDeleter<VkDescriptorPool, vkDestroyDescriptorPool>>;

using UniqueSampler = std::unique_ptr<
    std::remove_pointer_t<VkSampler>,
    VkHandleDeleter<VkSampler, vkDestroySampler>>;

// ============================================================================
// Factory functions for creating Unique* handles
// ============================================================================

inline UniquePipeline makeUniquePipeline(VkDevice device, VkPipeline pipeline) {
    return UniquePipeline(pipeline, {device});
}

inline UniqueRenderPass makeUniqueRenderPass(VkDevice device, VkRenderPass renderPass) {
    return UniqueRenderPass(renderPass, {device});
}

inline UniquePipelineLayout makeUniquePipelineLayout(VkDevice device, VkPipelineLayout layout) {
    return UniquePipelineLayout(layout, {device});
}

inline UniqueDescriptorSetLayout makeUniqueDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout) {
    return UniqueDescriptorSetLayout(layout, {device});
}

inline UniqueImageView makeUniqueImageView(VkDevice device, VkImageView imageView) {
    return UniqueImageView(imageView, {device});
}

inline UniqueFramebuffer makeUniqueFramebuffer(VkDevice device, VkFramebuffer framebuffer) {
    return UniqueFramebuffer(framebuffer, {device});
}

inline UniqueFence makeUniqueFence(VkDevice device, VkFence fence) {
    return UniqueFence(fence, {device});
}

inline UniqueSemaphore makeUniqueSemaphore(VkDevice device, VkSemaphore semaphore) {
    return UniqueSemaphore(semaphore, {device});
}

inline UniqueCommandPool makeUniqueCommandPool(VkDevice device, VkCommandPool pool) {
    return UniqueCommandPool(pool, {device});
}

inline UniqueDescriptorPool makeUniqueDescriptorPool(VkDevice device, VkDescriptorPool pool) {
    return UniqueDescriptorPool(pool, {device});
}

inline UniqueSampler makeUniqueSampler(VkDevice device, VkSampler sampler) {
    return UniqueSampler(sampler, {device});
}

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
// ManagedBuffer - RAII wrapper for VkBuffer + VmaAllocation (inherits from UniqueVmaBuffer)
// ============================================================================
// Adds map()/unmap() methods by accessing allocator and allocation from deleter.

class ManagedBuffer : public UniqueVmaBuffer {
public:
    using UniqueVmaBuffer::UniqueVmaBuffer;  // Inherit constructors

    ManagedBuffer() = default;

    // Allow conversion from UniqueVmaBuffer
    ManagedBuffer(UniqueVmaBuffer&& other) noexcept
        : UniqueVmaBuffer(std::move(other)) {}

    // Create a buffer with the given parameters
    static bool create(VmaAllocator allocator,
                       const VkBufferCreateInfo& bufferInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       ManagedBuffer& outBuffer) {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult vkResult = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                                            &buffer, &allocation, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedBuffer::create failed: %d", vkResult);
            return false;
        }

        outBuffer = ManagedBuffer(makeUniqueVmaBuffer(allocator, buffer, allocation));
        return true;
    }

    // Adopt an existing raw buffer (takes ownership)
    static ManagedBuffer fromRaw(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation) {
        return ManagedBuffer(makeUniqueVmaBuffer(allocator, buffer, allocation));
    }

    // Access allocator and allocation from deleter
    VmaAllocator allocator() const { return get_deleter().allocator; }
    VmaAllocation getAllocation() const { return get_deleter().allocation; }

    // Map memory for writing
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
        // Track mapped state in deleter for auto-unmap on destruction
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

    // Release ownership (for transferring to non-RAII code)
    void releaseToRaw(VkBuffer& outBuffer, VmaAllocation& outAllocation) {
        outBuffer = get();
        outAllocation = get_deleter().allocation;
        release();  // Release without destroying
    }
};

// ============================================================================
// ManagedImage - RAII wrapper for VkImage + VmaAllocation (inherits from UniqueVmaImage)
// ============================================================================

class ManagedImage : public UniqueVmaImage {
public:
    using UniqueVmaImage::UniqueVmaImage;  // Inherit constructors

    ManagedImage() = default;

    // Allow conversion from UniqueVmaImage
    ManagedImage(UniqueVmaImage&& other) noexcept
        : UniqueVmaImage(std::move(other)) {}

    // Create an image with the given parameters
    static bool create(VmaAllocator allocator,
                       const VkImageCreateInfo& imageInfo,
                       const VmaAllocationCreateInfo& allocInfo,
                       ManagedImage& outImage) {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        VkResult vkResult = vmaCreateImage(allocator, &imageInfo, &allocInfo,
                                           &image, &allocation, nullptr);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedImage::create failed: %d", vkResult);
            return false;
        }

        outImage = ManagedImage(makeUniqueVmaImage(allocator, image, allocation));
        return true;
    }

    // Adopt an existing raw image (takes ownership)
    static ManagedImage fromRaw(VmaAllocator allocator, VkImage image, VmaAllocation allocation) {
        return ManagedImage(makeUniqueVmaImage(allocator, image, allocation));
    }

    // Access allocator and allocation from deleter
    VmaAllocator allocator() const { return get_deleter().allocator; }
    VmaAllocation getAllocation() const { return get_deleter().allocation; }

    // Release ownership (for transferring to non-RAII code)
    void releaseToRaw(VkImage& outImage, VmaAllocation& outAllocation) {
        outImage = get();
        outAllocation = get_deleter().allocation;
        release();  // Release without destroying
    }
};

// ============================================================================
// ManagedImageView - RAII wrapper for VkImageView (inherits from UniqueImageView)
// ============================================================================

class ManagedImageView : public UniqueImageView {
public:
    using UniqueImageView::UniqueImageView;  // Inherit constructors

    ManagedImageView() = default;

    // Allow conversion from UniqueImageView
    ManagedImageView(UniqueImageView&& other) noexcept
        : UniqueImageView(std::move(other)) {}

    static bool create(VkDevice device,
                       const VkImageViewCreateInfo& viewInfo,
                       ManagedImageView& outView) {
        VkImageView imageView = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedImageView::create failed: %d", vkResult);
            return false;
        }

        outView = ManagedImageView(makeUniqueImageView(device, imageView));
        return true;
    }

    // Adopt an existing raw image view
    static ManagedImageView fromRaw(VkDevice device, VkImageView imageView) {
        return ManagedImageView(makeUniqueImageView(device, imageView));
    }
};

// ============================================================================
// ManagedSampler - RAII wrapper for VkSampler (inherits from UniqueSampler)
// ============================================================================
// Adds convenience factory methods for common sampler configurations.

class ManagedSampler : public UniqueSampler {
public:
    using UniqueSampler::UniqueSampler;  // Inherit constructors

    ManagedSampler() = default;

    // Allow conversion from UniqueSampler
    ManagedSampler(UniqueSampler&& other) noexcept
        : UniqueSampler(std::move(other)) {}

    static bool create(VkDevice device,
                       const VkSamplerCreateInfo& samplerInfo,
                       ManagedSampler& outSampler) {
        VkSampler sampler = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateSampler(device, &samplerInfo, nullptr, &sampler);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedSampler::create failed: %d", vkResult);
            return false;
        }

        outSampler = ManagedSampler(makeUniqueSampler(device, sampler));
        return true;
    }

    // Adopt an existing raw sampler
    static ManagedSampler fromRaw(VkDevice device, VkSampler sampler) {
        return ManagedSampler(makeUniqueSampler(device, sampler));
    }
};

// ============================================================================
// ManagedDescriptorSetLayout - RAII wrapper (inherits from UniqueDescriptorSetLayout)
// ============================================================================

class ManagedDescriptorSetLayout : public UniqueDescriptorSetLayout {
public:
    using UniqueDescriptorSetLayout::UniqueDescriptorSetLayout;  // Inherit constructors

    ManagedDescriptorSetLayout() = default;

    // Allow conversion from UniqueDescriptorSetLayout
    ManagedDescriptorSetLayout(UniqueDescriptorSetLayout&& other) noexcept
        : UniqueDescriptorSetLayout(std::move(other)) {}

    static bool create(VkDevice device,
                       const VkDescriptorSetLayoutCreateInfo& layoutInfo,
                       ManagedDescriptorSetLayout& outLayout) {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedDescriptorSetLayout::create failed: %d", vkResult);
            return false;
        }

        outLayout = ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device, layout));
        return true;
    }

    // Adopt an existing raw descriptor set layout (e.g., created by DescriptorManager)
    static ManagedDescriptorSetLayout fromRaw(VkDevice device, VkDescriptorSetLayout layout) {
        return ManagedDescriptorSetLayout(makeUniqueDescriptorSetLayout(device, layout));
    }
};

// ============================================================================
// ManagedPipelineLayout - RAII wrapper (inherits from UniquePipelineLayout)
// ============================================================================

class ManagedPipelineLayout : public UniquePipelineLayout {
public:
    using UniquePipelineLayout::UniquePipelineLayout;  // Inherit constructors

    ManagedPipelineLayout() = default;

    // Allow conversion from UniquePipelineLayout
    ManagedPipelineLayout(UniquePipelineLayout&& other) noexcept
        : UniquePipelineLayout(std::move(other)) {}

    static bool create(VkDevice device,
                       const VkPipelineLayoutCreateInfo& layoutInfo,
                       ManagedPipelineLayout& outLayout) {
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkResult vkResult = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedPipelineLayout::create failed: %d", vkResult);
            return false;
        }

        outLayout = ManagedPipelineLayout(makeUniquePipelineLayout(device, layout));
        return true;
    }

    // Adopt an existing raw pipeline layout (e.g., created by PipelineBuilder)
    static ManagedPipelineLayout fromRaw(VkDevice device, VkPipelineLayout layout) {
        return ManagedPipelineLayout(makeUniquePipelineLayout(device, layout));
    }
};

// ============================================================================
// ManagedPipeline - RAII wrapper for VkPipeline (inherits from UniquePipeline)
// ============================================================================

class ManagedPipeline : public UniquePipeline {
public:
    using UniquePipeline::UniquePipeline;  // Inherit constructors

    ManagedPipeline() = default;

    // Allow conversion from UniquePipeline
    ManagedPipeline(UniquePipeline&& other) noexcept
        : UniquePipeline(std::move(other)) {}

    // Create graphics pipeline
    static bool createGraphics(VkDevice device,
                               VkPipelineCache pipelineCache,
                               const VkGraphicsPipelineCreateInfo& pipelineInfo,
                               ManagedPipeline& outPipeline) {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedPipeline::createGraphics failed: %d", vkResult);
            return false;
        }

        outPipeline = ManagedPipeline(makeUniquePipeline(device, pipeline));
        return true;
    }

    // Create compute pipeline
    static bool createCompute(VkDevice device,
                              VkPipelineCache pipelineCache,
                              const VkComputePipelineCreateInfo& pipelineInfo,
                              ManagedPipeline& outPipeline) {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateComputePipelines(device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedPipeline::createCompute failed: %d", vkResult);
            return false;
        }

        outPipeline = ManagedPipeline(makeUniquePipeline(device, pipeline));
        return true;
    }

    // Adopt an existing raw pipeline (e.g., created by PipelineBuilder)
    static ManagedPipeline fromRaw(VkDevice device, VkPipeline pipeline) {
        return ManagedPipeline(makeUniquePipeline(device, pipeline));
    }
};

// ============================================================================
// ManagedRenderPass - RAII wrapper for VkRenderPass (inherits from UniqueRenderPass)
// ============================================================================

class ManagedRenderPass : public UniqueRenderPass {
public:
    using UniqueRenderPass::UniqueRenderPass;  // Inherit constructors

    ManagedRenderPass() = default;

    // Allow conversion from UniqueRenderPass
    ManagedRenderPass(UniqueRenderPass&& other) noexcept
        : UniqueRenderPass(std::move(other)) {}

    static bool create(VkDevice device,
                       const VkRenderPassCreateInfo& renderPassInfo,
                       ManagedRenderPass& outRenderPass) {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedRenderPass::create failed: %d", vkResult);
            return false;
        }

        outRenderPass = ManagedRenderPass(makeUniqueRenderPass(device, renderPass));
        return true;
    }

    // Adopt an existing raw render pass
    static ManagedRenderPass fromRaw(VkDevice device, VkRenderPass renderPass) {
        return ManagedRenderPass(makeUniqueRenderPass(device, renderPass));
    }
};

// ============================================================================
// ManagedFramebuffer - RAII wrapper for VkFramebuffer (inherits from UniqueFramebuffer)
// ============================================================================

class ManagedFramebuffer : public UniqueFramebuffer {
public:
    using UniqueFramebuffer::UniqueFramebuffer;  // Inherit constructors

    ManagedFramebuffer() = default;

    // Allow conversion from UniqueFramebuffer
    ManagedFramebuffer(UniqueFramebuffer&& other) noexcept
        : UniqueFramebuffer(std::move(other)) {}

    static bool create(VkDevice device,
                       const VkFramebufferCreateInfo& framebufferInfo,
                       ManagedFramebuffer& outFramebuffer) {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffer);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedFramebuffer::create failed: %d", vkResult);
            return false;
        }

        outFramebuffer = ManagedFramebuffer(makeUniqueFramebuffer(device, framebuffer));
        return true;
    }

    // Adopt an existing raw framebuffer
    static ManagedFramebuffer fromRaw(VkDevice device, VkFramebuffer framebuffer) {
        return ManagedFramebuffer(makeUniqueFramebuffer(device, framebuffer));
    }
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

        // Create a fence for fine-grained synchronization (better than vkQueueWaitIdle)
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = 0; // Not signaled initially

        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(device_, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to create fence");
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer_;

        VkResult submitResult = vkQueueSubmit(queue_, 1, &submitInfo, fence);
        if (submitResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to submit queue");
            vkDestroyFence(device_, fence, nullptr);
            return false;
        }

        // Wait only for this specific submission to complete
        VkResult waitResult = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device_, fence, nullptr);

        if (waitResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CommandScope: Failed to wait for fence");
            return false;
        }

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
// RenderPassScope - RAII wrapper for render pass begin/end
// ============================================================================
// Usage:
//   VkRenderPassBeginInfo beginInfo = {...};
//   {
//       RenderPassScope renderPass(cmd, beginInfo);
//       vkCmdBindPipeline(cmd, ...);
//       vkCmdDraw(cmd, ...);
//   } // vkCmdEndRenderPass called automatically
//
// Or with fluent builder:
//   {
//       auto renderPass = RenderPassScope::begin(cmd)
//           .renderPass(myRenderPass)
//           .framebuffer(myFramebuffer)
//           .renderArea(0, 0, width, height)
//           .clearColor(0.0f, 0.0f, 0.0f, 1.0f)
//           .clearDepth(1.0f, 0);
//       // render commands...
//   }

class RenderPassScope {
public:
    // Builder for fluent construction
    class Builder {
    public:
        explicit Builder(VkCommandBuffer cmd) : cmd_(cmd) {
            beginInfo_.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        }

        Builder& renderPass(VkRenderPass rp) {
            beginInfo_.renderPass = rp;
            return *this;
        }

        Builder& framebuffer(VkFramebuffer fb) {
            beginInfo_.framebuffer = fb;
            return *this;
        }

        Builder& renderArea(int32_t x, int32_t y, uint32_t width, uint32_t height) {
            beginInfo_.renderArea = {{x, y}, {width, height}};
            return *this;
        }

        Builder& renderArea(VkRect2D area) {
            beginInfo_.renderArea = area;
            return *this;
        }

        Builder& renderAreaFullExtent(uint32_t width, uint32_t height) {
            beginInfo_.renderArea = {{0, 0}, {width, height}};
            return *this;
        }

        Builder& clearColor(float r, float g, float b, float a) {
            VkClearValue clear;
            clear.color = {{r, g, b, a}};
            clearValues_.push_back(clear);
            return *this;
        }

        Builder& clearDepth(float depth, uint32_t stencil) {
            VkClearValue clear;
            clear.depthStencil = {depth, stencil};
            clearValues_.push_back(clear);
            return *this;
        }

        Builder& clearValues(const VkClearValue* values, uint32_t count) {
            clearValues_.assign(values, values + count);
            return *this;
        }

        Builder& subpassContents(VkSubpassContents contents) {
            contents_ = contents;
            return *this;
        }

        // Implicit conversion to RenderPassScope starts the render pass
        operator RenderPassScope() {
            beginInfo_.clearValueCount = static_cast<uint32_t>(clearValues_.size());
            beginInfo_.pClearValues = clearValues_.empty() ? nullptr : clearValues_.data();
            return RenderPassScope(cmd_, beginInfo_, contents_);
        }

    private:
        VkCommandBuffer cmd_;
        VkRenderPassBeginInfo beginInfo_{};
        std::vector<VkClearValue> clearValues_;
        VkSubpassContents contents_ = VK_SUBPASS_CONTENTS_INLINE;
    };

    // Direct construction with pre-built begin info
    RenderPassScope(VkCommandBuffer cmd, const VkRenderPassBeginInfo& beginInfo,
                    VkSubpassContents contents = VK_SUBPASS_CONTENTS_INLINE)
        : cmd_(cmd) {
        vkCmdBeginRenderPass(cmd_, &beginInfo, contents);
    }

    ~RenderPassScope() {
        if (cmd_ != VK_NULL_HANDLE) {
            vkCmdEndRenderPass(cmd_);
        }
    }

    // Non-copyable
    RenderPassScope(const RenderPassScope&) = delete;
    RenderPassScope& operator=(const RenderPassScope&) = delete;

    // Move-only (transfers ownership)
    RenderPassScope(RenderPassScope&& other) noexcept : cmd_(other.cmd_) {
        other.cmd_ = VK_NULL_HANDLE;
    }

    RenderPassScope& operator=(RenderPassScope&& other) noexcept {
        if (this != &other) {
            if (cmd_ != VK_NULL_HANDLE) {
                vkCmdEndRenderPass(cmd_);
            }
            cmd_ = other.cmd_;
            other.cmd_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    // Fluent builder entry point
    static Builder begin(VkCommandBuffer cmd) {
        return Builder(cmd);
    }

    // Access the command buffer for issuing draw commands
    VkCommandBuffer cmd() const { return cmd_; }

private:
    VkCommandBuffer cmd_;
};

// ============================================================================
// ManagedCommandPool - RAII wrapper for VkCommandPool (inherits from UniqueCommandPool)
// ============================================================================

class ManagedCommandPool : public UniqueCommandPool {
public:
    using UniqueCommandPool::UniqueCommandPool;  // Inherit constructors

    ManagedCommandPool() = default;

    // Allow conversion from UniqueCommandPool
    ManagedCommandPool(UniqueCommandPool&& other) noexcept
        : UniqueCommandPool(std::move(other)) {}

    static bool create(VkDevice device,
                       uint32_t queueFamilyIndex,
                       VkCommandPoolCreateFlags flags,
                       ManagedCommandPool& outPool) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = flags;
        poolInfo.queueFamilyIndex = queueFamilyIndex;

        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedCommandPool::create failed: %d", vkResult);
            return false;
        }

        outPool = ManagedCommandPool(makeUniqueCommandPool(device, commandPool));
        return true;
    }

    // Adopt an existing raw command pool
    static ManagedCommandPool fromRaw(VkDevice device, VkCommandPool commandPool) {
        return ManagedCommandPool(makeUniqueCommandPool(device, commandPool));
    }
};

// ============================================================================
// ManagedSemaphore - RAII wrapper for VkSemaphore (inherits from UniqueSemaphore)
// ============================================================================

class ManagedSemaphore : public UniqueSemaphore {
public:
    using UniqueSemaphore::UniqueSemaphore;  // Inherit constructors

    ManagedSemaphore() = default;

    // Allow conversion from UniqueSemaphore
    ManagedSemaphore(UniqueSemaphore&& other) noexcept
        : UniqueSemaphore(std::move(other)) {}

    static bool create(VkDevice device, ManagedSemaphore& outSemaphore) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedSemaphore::create failed: %d", vkResult);
            return false;
        }

        outSemaphore = ManagedSemaphore(makeUniqueSemaphore(device, semaphore));
        return true;
    }

    // Adopt an existing raw semaphore
    static ManagedSemaphore fromRaw(VkDevice device, VkSemaphore semaphore) {
        return ManagedSemaphore(makeUniqueSemaphore(device, semaphore));
    }
};

// ============================================================================
// ManagedFence - RAII wrapper for VkFence (inherits from UniqueFence)
// ============================================================================
// Adds wait() and reset() convenience methods by accessing device from deleter.

class ManagedFence : public UniqueFence {
public:
    using UniqueFence::UniqueFence;  // Inherit constructors

    ManagedFence() = default;

    // Allow conversion from UniqueFence
    ManagedFence(UniqueFence&& other) noexcept
        : UniqueFence(std::move(other)) {}

    static bool create(VkDevice device, VkFenceCreateFlags flags, ManagedFence& outFence) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = flags;

        VkFence fence = VK_NULL_HANDLE;
        VkResult vkResult = vkCreateFence(device, &fenceInfo, nullptr, &fence);
        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ManagedFence::create failed: %d", vkResult);
            return false;
        }

        outFence = ManagedFence(makeUniqueFence(device, fence));
        return true;
    }

    // Convenience: create signaled fence (common for frame synchronization)
    static bool createSignaled(VkDevice device, ManagedFence& outFence) {
        return create(device, VK_FENCE_CREATE_SIGNALED_BIT, outFence);
    }

    // Adopt an existing raw fence
    static ManagedFence fromRaw(VkDevice device, VkFence fence) {
        return ManagedFence(makeUniqueFence(device, fence));
    }

    // Access device from deleter (for sync operations)
    VkDevice device() const { return get_deleter().device; }

    // Convenience methods for fence operations
    VkResult wait(uint64_t timeout = UINT64_MAX) const {
        VkFence f = get();
        return vkWaitForFences(device(), 1, &f, VK_TRUE, timeout);
    }

    VkResult resetFence() const {
        VkFence f = get();
        return vkResetFences(device(), 1, &f);
    }
};
