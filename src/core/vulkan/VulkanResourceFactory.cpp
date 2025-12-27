#include "VulkanResourceFactory.h"
#include <SDL3/SDL_log.h>
#include <array>

using namespace vk;

// ============================================================================
// SyncResources
// ============================================================================

void VulkanResourceFactory::SyncResources::destroy(VkDevice device) {
    for (auto semaphore : imageAvailableSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (auto semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (auto fence : inFlightFences) {
        vkDestroyFence(device, fence, nullptr);
    }
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
}

// ============================================================================
// DepthResources
// ============================================================================

void VulkanResourceFactory::DepthResources::destroy(VkDevice device, VmaAllocator allocator) {
    sampler.reset();
    if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image, allocation);
        image = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Command Pool & Buffers
// ============================================================================

bool VulkanResourceFactory::createCommandPool(
    VkDevice device,
    uint32_t queueFamilyIndex,
    VkCommandPoolCreateFlags flags,
    VkCommandPool& outPool)
{
    CommandPoolCreateInfo poolInfo{
        static_cast<CommandPoolCreateFlags>(flags),
        queueFamilyIndex
    };

    auto vkPoolInfo = static_cast<VkCommandPoolCreateInfo>(poolInfo);
    if (vkCreateCommandPool(device, &vkPoolInfo, nullptr, &outPool) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create command pool");
        return false;
    }

    return true;
}

bool VulkanResourceFactory::createCommandBuffers(
    VkDevice device,
    VkCommandPool pool,
    uint32_t count,
    std::vector<VkCommandBuffer>& outBuffers)
{
    outBuffers.resize(count);

    CommandBufferAllocateInfo allocInfo{
        pool,
        CommandBufferLevel::ePrimary,
        count
    };

    auto vkAllocInfo = static_cast<VkCommandBufferAllocateInfo>(allocInfo);
    if (vkAllocateCommandBuffers(device, &vkAllocInfo, outBuffers.data()) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate command buffers");
        return false;
    }

    return true;
}

// ============================================================================
// Synchronization
// ============================================================================

bool VulkanResourceFactory::createSyncResources(
    VkDevice device,
    uint32_t framesInFlight,
    SyncResources& outResources)
{
    outResources.imageAvailableSemaphores.resize(framesInFlight);
    outResources.renderFinishedSemaphores.resize(framesInFlight);
    outResources.inFlightFences.resize(framesInFlight);

    SemaphoreCreateInfo semaphoreInfo{};

    FenceCreateInfo fenceInfo{
        FenceCreateFlagBits::eSignaled
    };

    auto vkSemaphoreInfo = static_cast<VkSemaphoreCreateInfo>(semaphoreInfo);
    auto vkFenceInfo = static_cast<VkFenceCreateInfo>(fenceInfo);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (vkCreateSemaphore(device, &vkSemaphoreInfo, nullptr, &outResources.imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &vkSemaphoreInfo, nullptr, &outResources.renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &vkFenceInfo, nullptr, &outResources.inFlightFences[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create sync objects for frame %u", i);
            // Cleanup already created resources
            outResources.destroy(device);
            return false;
        }
    }

    return true;
}

// ============================================================================
// Depth Buffer
// ============================================================================

bool VulkanResourceFactory::createDepthResources(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent2D extent,
    VkFormat format,
    DepthResources& outResources)
{
    outResources.format = format;

    // Create depth image
    ImageCreateInfo imageInfo{
        {},                                              // flags
        ImageType::e2D,
        static_cast<Format>(format),
        Extent3D{extent.width, extent.height, 1},
        1, 1,                                            // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        // SAMPLED_BIT for Hi-Z pyramid generation
        ImageUsageFlagBits::eDepthStencilAttachment | ImageUsageFlagBits::eSampled,
        SharingMode::eExclusive,
        0, nullptr,                                      // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &outResources.image, &outResources.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    // Create depth image view
    ImageViewCreateInfo viewInfo{
        {},                                              // flags
        outResources.image,
        ImageViewType::e2D,
        static_cast<Format>(format),
        {},                                              // components (identity)
        ImageSubresourceRange{
            ImageAspectFlagBits::eDepth,
            0, 1,                                        // baseMipLevel, levelCount
            0, 1                                         // baseArrayLayer, layerCount
        }
    };

    auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
    if (vkCreateImageView(device, &vkViewInfo, nullptr, &outResources.view) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image view");
        outResources.destroy(device, allocator);
        return false;
    }

    // Create depth sampler for Hi-Z pyramid generation
    if (!createSamplerNearestClamp(device, outResources.sampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth sampler");
        outResources.destroy(device, allocator);
        return false;
    }

    return true;
}

bool VulkanResourceFactory::createDepthImageAndView(
    VkDevice device,
    VmaAllocator allocator,
    VkExtent2D extent,
    VkFormat format,
    VkImage& outImage,
    VmaAllocation& outAllocation,
    VkImageView& outView)
{
    // Create depth image
    ImageCreateInfo imageInfo{
        {},                                              // flags
        ImageType::e2D,
        static_cast<Format>(format),
        Extent3D{extent.width, extent.height, 1},
        1, 1,                                            // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        ImageUsageFlagBits::eDepthStencilAttachment | ImageUsageFlagBits::eSampled,
        SharingMode::eExclusive,
        0, nullptr,                                      // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &outImage, &outAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    // Create depth image view
    ImageViewCreateInfo viewInfo{
        {},                                              // flags
        outImage,
        ImageViewType::e2D,
        static_cast<Format>(format),
        {},                                              // components (identity)
        ImageSubresourceRange{
            ImageAspectFlagBits::eDepth,
            0, 1,                                        // baseMipLevel, levelCount
            0, 1                                         // baseArrayLayer, layerCount
        }
    };

    auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
    if (vkCreateImageView(device, &vkViewInfo, nullptr, &outView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image view");
        vmaDestroyImage(allocator, outImage, outAllocation);
        outImage = VK_NULL_HANDLE;
        outAllocation = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

// ============================================================================
// Framebuffers
// ============================================================================

bool VulkanResourceFactory::createFramebuffers(
    VkDevice device,
    VkRenderPass renderPass,
    const std::vector<VkImageView>& swapchainImageViews,
    VkImageView depthImageView,
    VkExtent2D extent,
    std::vector<VkFramebuffer>& outFramebuffers)
{
    outFramebuffers.resize(swapchainImageViews.size());

    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImageView
        };

        FramebufferCreateInfo framebufferInfo{
            {},                                          // flags
            renderPass,
            static_cast<uint32_t>(attachments.size()),
            reinterpret_cast<const ImageView*>(attachments.data()),
            extent.width,
            extent.height,
            1                                            // layers
        };

        auto vkFramebufferInfo = static_cast<VkFramebufferCreateInfo>(framebufferInfo);
        if (vkCreateFramebuffer(device, &vkFramebufferInfo, nullptr, &outFramebuffers[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create framebuffer %zu", i);
            // Cleanup already created framebuffers
            destroyFramebuffers(device, outFramebuffers);
            return false;
        }
    }

    return true;
}

void VulkanResourceFactory::destroyFramebuffers(
    VkDevice device,
    std::vector<VkFramebuffer>& framebuffers)
{
    for (auto framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    framebuffers.clear();
}

// ============================================================================
// Render Pass
// ============================================================================

bool VulkanResourceFactory::createRenderPass(
    VkDevice device,
    const RenderPassConfig& config,
    VkRenderPass& outRenderPass)
{
    if (config.depthOnly) {
        // Depth-only render pass (for shadow maps)
        AttachmentDescription depthAttachment{
            {},                                          // flags
            static_cast<Format>(config.depthFormat),
            SampleCountFlagBits::e1,
            config.clearDepth ? AttachmentLoadOp::eClear : AttachmentLoadOp::eLoad,
            config.storeDepth ? AttachmentStoreOp::eStore : AttachmentStoreOp::eDontCare,
            AttachmentLoadOp::eDontCare,                 // stencilLoadOp
            AttachmentStoreOp::eDontCare,                // stencilStoreOp
            ImageLayout::eUndefined,
            static_cast<ImageLayout>(config.finalDepthLayout)
        };

        AttachmentReference depthAttachmentRef{
            0,
            ImageLayout::eDepthStencilAttachmentOptimal
        };

        SubpassDescription subpass{
            {},                                          // flags
            PipelineBindPoint::eGraphics,
            {},                                          // inputAttachments
            {},                                          // colorAttachments
            {},                                          // resolveAttachments
            &depthAttachmentRef,                         // pDepthStencilAttachment
            {}                                           // preserveAttachments
        };

        SubpassDependency dependency{
            VK_SUBPASS_EXTERNAL,                         // srcSubpass
            0,                                           // dstSubpass
            PipelineStageFlagBits::eFragmentShader,      // srcStageMask
            PipelineStageFlagBits::eEarlyFragmentTests,  // dstStageMask
            AccessFlagBits::eShaderRead,                 // srcAccessMask
            AccessFlagBits::eDepthStencilAttachmentWrite, // dstAccessMask
            {}                                           // dependencyFlags
        };

        RenderPassCreateInfo renderPassInfo{
            {},                                          // flags
            depthAttachment,
            subpass,
            dependency
        };

        auto vkRenderPassInfo = static_cast<VkRenderPassCreateInfo>(renderPassInfo);
        if (vkCreateRenderPass(device, &vkRenderPassInfo, nullptr, &outRenderPass) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth-only render pass");
            return false;
        }

        return true;
    }

    // Standard color + depth render pass
    AttachmentDescription colorAttachment{
        {},                                          // flags
        static_cast<Format>(config.colorFormat),
        SampleCountFlagBits::e1,
        config.clearColor ? AttachmentLoadOp::eClear : AttachmentLoadOp::eLoad,
        AttachmentStoreOp::eStore,
        AttachmentLoadOp::eDontCare,                 // stencilLoadOp
        AttachmentStoreOp::eDontCare,                // stencilStoreOp
        ImageLayout::eUndefined,
        static_cast<ImageLayout>(config.finalColorLayout)
    };

    AttachmentDescription depthAttachment{
        {},                                          // flags
        static_cast<Format>(config.depthFormat),
        SampleCountFlagBits::e1,
        config.clearDepth ? AttachmentLoadOp::eClear : AttachmentLoadOp::eLoad,
        config.storeDepth ? AttachmentStoreOp::eStore : AttachmentStoreOp::eDontCare,
        AttachmentLoadOp::eDontCare,                 // stencilLoadOp
        AttachmentStoreOp::eDontCare,                // stencilStoreOp
        ImageLayout::eUndefined,
        static_cast<ImageLayout>(config.finalDepthLayout)
    };

    AttachmentReference colorAttachmentRef{
        0,
        ImageLayout::eColorAttachmentOptimal
    };

    AttachmentReference depthAttachmentRef{
        1,
        ImageLayout::eDepthStencilAttachmentOptimal
    };

    SubpassDescription subpass{
        {},                                          // flags
        PipelineBindPoint::eGraphics,
        {},                                          // inputAttachments
        colorAttachmentRef,                          // colorAttachments
        {},                                          // resolveAttachments
        &depthAttachmentRef,                         // pDepthStencilAttachment
        {}                                           // preserveAttachments
    };

    SubpassDependency dependency{
        VK_SUBPASS_EXTERNAL,                         // srcSubpass
        0,                                           // dstSubpass
        PipelineStageFlagBits::eColorAttachmentOutput | PipelineStageFlagBits::eEarlyFragmentTests,
        PipelineStageFlagBits::eColorAttachmentOutput | PipelineStageFlagBits::eEarlyFragmentTests,
        {},                                          // srcAccessMask
        AccessFlagBits::eColorAttachmentWrite | AccessFlagBits::eDepthStencilAttachmentWrite,
        {}                                           // dependencyFlags
    };

    std::array<AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    RenderPassCreateInfo renderPassInfo{
        {},                                          // flags
        attachments,
        subpass,
        dependency
    };

    auto vkRenderPassInfo = static_cast<VkRenderPassCreateInfo>(renderPassInfo);
    if (vkCreateRenderPass(device, &vkRenderPassInfo, nullptr, &outRenderPass) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render pass");
        return false;
    }

    return true;
}

// ============================================================================
// DepthArrayResources
// ============================================================================

void VulkanResourceFactory::DepthArrayResources::destroy(VkDevice device, VmaAllocator allocator) {
    sampler.reset();
    for (auto& view : layerViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    layerViews.clear();
    if (arrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, arrayView, nullptr);
        arrayView = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, image, allocation);
        image = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Depth Array Resources (for shadow maps)
// ============================================================================

bool VulkanResourceFactory::createDepthArrayResources(
    VkDevice device,
    VmaAllocator allocator,
    const DepthArrayConfig& config,
    DepthArrayResources& outResources)
{
    // Create depth image array
    ImageCreateInfo imageInfo{
        config.cubeCompatible ? ImageCreateFlagBits::eCubeCompatible : ImageCreateFlags{},
        ImageType::e2D,
        static_cast<Format>(config.format),
        Extent3D{config.extent.width, config.extent.height, 1},
        1, config.arrayLayers,                       // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        ImageUsageFlagBits::eDepthStencilAttachment | ImageUsageFlagBits::eSampled,
        SharingMode::eExclusive,
        0, nullptr,                                  // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    if (vmaCreateImage(allocator, &vkImageInfo, &allocInfo, &outResources.image,
                       &outResources.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array image");
        return false;
    }

    // Create array view (for sampling all layers in shader)
    ImageViewCreateInfo arrayViewInfo{
        {},                                              // flags
        outResources.image,
        config.cubeCompatible ? ImageViewType::eCubeArray : ImageViewType::e2DArray,
        static_cast<Format>(config.format),
        {},                                              // components (identity)
        ImageSubresourceRange{
            ImageAspectFlagBits::eDepth,
            0, 1,                                        // baseMipLevel, levelCount
            0, config.arrayLayers                        // baseArrayLayer, layerCount
        }
    };

    auto vkArrayViewInfo = static_cast<VkImageViewCreateInfo>(arrayViewInfo);
    if (vkCreateImageView(device, &vkArrayViewInfo, nullptr, &outResources.arrayView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array view");
        outResources.destroy(device, allocator);
        return false;
    }

    // Create per-layer views (for rendering to individual layers)
    outResources.layerViews.resize(config.arrayLayers);
    for (uint32_t i = 0; i < config.arrayLayers; i++) {
        ImageViewCreateInfo layerViewInfo{
            {},                                          // flags
            outResources.image,
            ImageViewType::e2D,
            static_cast<Format>(config.format),
            {},                                          // components (identity)
            ImageSubresourceRange{
                ImageAspectFlagBits::eDepth,
                0, 1,                                    // baseMipLevel, levelCount
                i, 1                                     // baseArrayLayer, layerCount
            }
        };

        auto vkLayerViewInfo = static_cast<VkImageViewCreateInfo>(layerViewInfo);
        if (vkCreateImageView(device, &vkLayerViewInfo, nullptr, &outResources.layerViews[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth layer view %u", i);
            outResources.destroy(device, allocator);
            return false;
        }
    }

    // Create sampler with depth comparison (for shadow mapping)
    if (config.createSampler) {
        if (!createSamplerShadowComparison(device, outResources.sampler)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array sampler");
            outResources.destroy(device, allocator);
            return false;
        }
    }

    return true;
}

bool VulkanResourceFactory::createDepthOnlyFramebuffers(
    VkDevice device,
    VkRenderPass renderPass,
    const std::vector<VkImageView>& depthImageViews,
    VkExtent2D extent,
    std::vector<VkFramebuffer>& outFramebuffers)
{
    outFramebuffers.resize(depthImageViews.size());

    for (size_t i = 0; i < depthImageViews.size(); i++) {
        FramebufferCreateInfo framebufferInfo{
            {},                                          // flags
            renderPass,
            1,                                           // attachmentCount
            reinterpret_cast<const ImageView*>(&depthImageViews[i]),
            extent.width,
            extent.height,
            1                                            // layers
        };

        auto vkFramebufferInfo = static_cast<VkFramebufferCreateInfo>(framebufferInfo);
        if (vkCreateFramebuffer(device, &vkFramebufferInfo, nullptr, &outFramebuffers[i]) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth-only framebuffer %zu", i);
            destroyFramebuffers(device, outFramebuffers);
            return false;
        }
    }

    return true;
}

// ============================================================================
// Buffer Factories
// ============================================================================

bool VulkanResourceFactory::createStagingBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eTransferSrc,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createVertexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eTransferDst | BufferUsageFlagBits::eVertexBuffer,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createIndexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eTransferDst | BufferUsageFlagBits::eIndexBuffer,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createUniformBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eUniformBuffer,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createStorageBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eStorageBuffer |
            BufferUsageFlagBits::eTransferDst |
            BufferUsageFlagBits::eTransferSrc,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createStorageBufferHostReadable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eStorageBuffer |
            BufferUsageFlagBits::eTransferDst |
            BufferUsageFlagBits::eTransferSrc,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createStorageBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eStorageBuffer |
            BufferUsageFlagBits::eTransferDst |
            BufferUsageFlagBits::eTransferSrc,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createReadbackBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createVertexStorageBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eVertexBuffer |
            BufferUsageFlagBits::eStorageBuffer |
            BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createVertexStorageBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eVertexBuffer |
            BufferUsageFlagBits::eStorageBuffer |
            BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createIndexBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eIndexBuffer |
            BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createIndirectBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eIndirectBuffer |
            BufferUsageFlagBits::eStorageBuffer |
            BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

bool VulkanResourceFactory::createDynamicVertexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    BufferCreateInfo bufferInfo{
        {},                                              // flags
        size,
        BufferUsageFlagBits::eVertexBuffer |
            BufferUsageFlagBits::eTransferDst,
        SharingMode::eExclusive,
        0, nullptr                                       // queueFamilyIndexCount, pQueueFamilyIndices
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    auto vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    return ManagedBuffer::create(allocator, vkBufferInfo, allocInfo, outBuffer);
}

// ============================================================================
// Sampler Factories
// ============================================================================

bool VulkanResourceFactory::createSamplerNearestClamp(VkDevice device, ManagedSampler& outSampler) {
    SamplerCreateInfo samplerInfo{
        {},                                              // flags
        Filter::eNearest,                                // magFilter
        Filter::eNearest,                                // minFilter
        SamplerMipmapMode::eNearest,
        SamplerAddressMode::eClampToEdge,                // addressModeU
        SamplerAddressMode::eClampToEdge,                // addressModeV
        SamplerAddressMode::eClampToEdge,                // addressModeW
        0.0f,                                            // mipLodBias
        VK_FALSE,                                        // anisotropyEnable
        1.0f,                                            // maxAnisotropy
        VK_FALSE,                                        // compareEnable
        CompareOp::eNever,                               // compareOp
        0.0f,                                            // minLod
        0.0f,                                            // maxLod
        BorderColor::eFloatTransparentBlack,             // borderColor
        VK_FALSE                                         // unnormalizedCoordinates
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, outSampler);
}

bool VulkanResourceFactory::createSamplerLinearClamp(VkDevice device, ManagedSampler& outSampler) {
    SamplerCreateInfo samplerInfo{
        {},                                              // flags
        Filter::eLinear,                                 // magFilter
        Filter::eLinear,                                 // minFilter
        SamplerMipmapMode::eLinear,
        SamplerAddressMode::eClampToEdge,                // addressModeU
        SamplerAddressMode::eClampToEdge,                // addressModeV
        SamplerAddressMode::eClampToEdge,                // addressModeW
        0.0f,                                            // mipLodBias
        VK_FALSE,                                        // anisotropyEnable
        1.0f,                                            // maxAnisotropy
        VK_FALSE,                                        // compareEnable
        CompareOp::eNever,                               // compareOp
        0.0f,                                            // minLod
        VK_LOD_CLAMP_NONE,                               // maxLod
        BorderColor::eFloatTransparentBlack,             // borderColor
        VK_FALSE                                         // unnormalizedCoordinates
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, outSampler);
}

bool VulkanResourceFactory::createSamplerLinearRepeat(VkDevice device, ManagedSampler& outSampler) {
    SamplerCreateInfo samplerInfo{
        {},                                              // flags
        Filter::eLinear,                                 // magFilter
        Filter::eLinear,                                 // minFilter
        SamplerMipmapMode::eLinear,
        SamplerAddressMode::eRepeat,                     // addressModeU
        SamplerAddressMode::eRepeat,                     // addressModeV
        SamplerAddressMode::eRepeat,                     // addressModeW
        0.0f,                                            // mipLodBias
        VK_FALSE,                                        // anisotropyEnable
        1.0f,                                            // maxAnisotropy
        VK_FALSE,                                        // compareEnable
        CompareOp::eNever,                               // compareOp
        0.0f,                                            // minLod
        VK_LOD_CLAMP_NONE,                               // maxLod
        BorderColor::eFloatTransparentBlack,             // borderColor
        VK_FALSE                                         // unnormalizedCoordinates
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, outSampler);
}

bool VulkanResourceFactory::createSamplerLinearRepeatAnisotropic(VkDevice device, float maxAnisotropy, ManagedSampler& outSampler) {
    SamplerCreateInfo samplerInfo{
        {},                                              // flags
        Filter::eLinear,                                 // magFilter
        Filter::eLinear,                                 // minFilter
        SamplerMipmapMode::eLinear,
        SamplerAddressMode::eRepeat,                     // addressModeU
        SamplerAddressMode::eRepeat,                     // addressModeV
        SamplerAddressMode::eRepeat,                     // addressModeW
        0.0f,                                            // mipLodBias
        VK_TRUE,                                         // anisotropyEnable
        maxAnisotropy,                                   // maxAnisotropy
        VK_FALSE,                                        // compareEnable
        CompareOp::eNever,                               // compareOp
        0.0f,                                            // minLod
        VK_LOD_CLAMP_NONE,                               // maxLod
        BorderColor::eFloatTransparentBlack,             // borderColor
        VK_FALSE                                         // unnormalizedCoordinates
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, outSampler);
}

bool VulkanResourceFactory::createSamplerShadowComparison(VkDevice device, ManagedSampler& outSampler) {
    SamplerCreateInfo samplerInfo{
        {},                                              // flags
        Filter::eLinear,                                 // magFilter
        Filter::eLinear,                                 // minFilter
        SamplerMipmapMode::eNearest,
        SamplerAddressMode::eClampToBorder,              // addressModeU
        SamplerAddressMode::eClampToBorder,              // addressModeV
        SamplerAddressMode::eClampToBorder,              // addressModeW
        0.0f,                                            // mipLodBias
        VK_FALSE,                                        // anisotropyEnable
        1.0f,                                            // maxAnisotropy
        VK_TRUE,                                         // compareEnable
        CompareOp::eLess,                                // compareOp
        0.0f,                                            // minLod
        0.0f,                                            // maxLod
        BorderColor::eFloatOpaqueWhite,                  // borderColor
        VK_FALSE                                         // unnormalizedCoordinates
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    return ManagedSampler::create(device, vkSamplerInfo, outSampler);
}
