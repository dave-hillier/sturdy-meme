#pragma once

// ============================================================================
// VulkanRAII.h - RAII wrappers for Vulkan resources
// ============================================================================
// This file includes VmaResources.h and VulkanHelpers.h for VMA and utility
// implementations, and provides additional RAII wrappers for Vulkan handles.

#include "VmaResources.h"
#include "VulkanHelpers.h"
#include <vulkan/vulkan.h>
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

    // Non-blocking fence status check (VK_SUCCESS = signaled, VK_NOT_READY = not signaled)
    VkResult getStatus() const {
        return vkGetFenceStatus(device(), get());
    }

    // Convenience method to check if fence is already signaled (non-blocking)
    bool isSignaled() const {
        return getStatus() == VK_SUCCESS;
    }
};
