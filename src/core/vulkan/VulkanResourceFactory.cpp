#include "VulkanResourceFactory.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL_log.h>
#include <array>

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
    auto poolInfo = vk::CommandPoolCreateInfo{}
        .setFlags(static_cast<vk::CommandPoolCreateFlags>(flags))
        .setQueueFamilyIndex(queueFamilyIndex);

    if (vkCreateCommandPool(device, reinterpret_cast<const VkCommandPoolCreateInfo*>(&poolInfo), nullptr, &outPool) != VK_SUCCESS) {
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

    auto allocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(pool)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(count);

    if (vkAllocateCommandBuffers(device, reinterpret_cast<const VkCommandBufferAllocateInfo*>(&allocInfo), outBuffers.data()) != VK_SUCCESS) {
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

    auto semaphoreInfo = vk::SemaphoreCreateInfo{};

    auto fenceInfo = vk::FenceCreateInfo{}
        .setFlags(vk::FenceCreateFlagBits::eSignaled);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (vkCreateSemaphore(device, reinterpret_cast<const VkSemaphoreCreateInfo*>(&semaphoreInfo), nullptr, &outResources.imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, reinterpret_cast<const VkSemaphoreCreateInfo*>(&semaphoreInfo), nullptr, &outResources.renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, reinterpret_cast<const VkFenceCreateInfo*>(&fenceInfo), nullptr, &outResources.inFlightFences[i]) != VK_SUCCESS) {
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

    // Create depth image using vulkan-hpp builder
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(static_cast<vk::Format>(format))
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        // SAMPLED_BIT for Hi-Z pyramid generation
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &outResources.image, &outResources.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    // Create depth image view using vulkan-hpp builder
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(outResources.image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &outResources.view) != VK_SUCCESS) {
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
    // Create depth image using vulkan-hpp builder
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{extent.width, extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setFormat(static_cast<vk::Format>(format))
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &outImage, &outAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth image");
        return false;
    }

    // Create depth image view using vulkan-hpp builder
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(outImage)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &outView) != VK_SUCCESS) {
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

        auto framebufferInfo = vk::FramebufferCreateInfo{}
            .setRenderPass(renderPass)
            .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
            .setPAttachments(reinterpret_cast<const vk::ImageView*>(attachments.data()))
            .setWidth(extent.width)
            .setHeight(extent.height)
            .setLayers(1);

        if (vkCreateFramebuffer(device, reinterpret_cast<const VkFramebufferCreateInfo*>(&framebufferInfo), nullptr, &outFramebuffers[i]) != VK_SUCCESS) {
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
        // Depth-only render pass (for shadow maps) using vulkan-hpp builders
        auto depthAttachment = vk::AttachmentDescription{}
            .setFormat(static_cast<vk::Format>(config.depthFormat))
            .setSamples(vk::SampleCountFlagBits::e1)
            .setLoadOp(config.clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
            .setStoreOp(config.storeDepth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare)
            .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
            .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setFinalLayout(static_cast<vk::ImageLayout>(config.finalDepthLayout));

        auto depthAttachmentRef = vk::AttachmentReference{}
            .setAttachment(0)
            .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        auto subpass = vk::SubpassDescription{}
            .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
            .setColorAttachmentCount(0)
            .setPDepthStencilAttachment(&depthAttachmentRef);

        auto dependency = vk::SubpassDependency{}
            .setSrcSubpass(VK_SUBPASS_EXTERNAL)
            .setDstSubpass(0)
            .setSrcStageMask(vk::PipelineStageFlagBits::eFragmentShader)
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests)
            .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite);

        auto renderPassInfo = vk::RenderPassCreateInfo{}
            .setAttachmentCount(1)
            .setPAttachments(&depthAttachment)
            .setSubpassCount(1)
            .setPSubpasses(&subpass)
            .setDependencyCount(1)
            .setPDependencies(&dependency);

        if (vkCreateRenderPass(device, reinterpret_cast<const VkRenderPassCreateInfo*>(&renderPassInfo), nullptr, &outRenderPass) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth-only render pass");
            return false;
        }

        return true;
    }

    // Standard color + depth render pass using vulkan-hpp builders
    auto colorAttachment = vk::AttachmentDescription{}
        .setFormat(static_cast<vk::Format>(config.colorFormat))
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(config.clearColor ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(static_cast<vk::ImageLayout>(config.finalColorLayout));

    auto depthAttachment = vk::AttachmentDescription{}
        .setFormat(static_cast<vk::Format>(config.depthFormat))
        .setSamples(vk::SampleCountFlagBits::e1)
        .setLoadOp(config.clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
        .setStoreOp(config.storeDepth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare)
        .setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
        .setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setFinalLayout(static_cast<vk::ImageLayout>(config.finalDepthLayout));

    auto colorAttachmentRef = vk::AttachmentReference{}
        .setAttachment(0)
        .setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    auto depthAttachmentRef = vk::AttachmentReference{}
        .setAttachment(1)
        .setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    auto subpass = vk::SubpassDescription{}
        .setPipelineBindPoint(vk::PipelineBindPoint::eGraphics)
        .setColorAttachmentCount(1)
        .setPColorAttachments(&colorAttachmentRef)
        .setPDepthStencilAttachment(&depthAttachmentRef);

    auto dependency = vk::SubpassDependency{}
        .setSrcSubpass(VK_SUBPASS_EXTERNAL)
        .setDstSubpass(0)
        .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setSrcAccessMask(vk::AccessFlags{})
        .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
        .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

    std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    auto renderPassInfo = vk::RenderPassCreateInfo{}
        .setAttachmentCount(static_cast<uint32_t>(attachments.size()))
        .setPAttachments(attachments.data())
        .setSubpassCount(1)
        .setPSubpasses(&subpass)
        .setDependencyCount(1)
        .setPDependencies(&dependency);

    if (vkCreateRenderPass(device, reinterpret_cast<const VkRenderPassCreateInfo*>(&renderPassInfo), nullptr, &outRenderPass) != VK_SUCCESS) {
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
    // Create depth image array using vulkan-hpp builder
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setExtent(vk::Extent3D{config.extent.width, config.extent.height, 1})
        .setMipLevels(1)
        .setArrayLayers(config.arrayLayers)
        .setFormat(static_cast<vk::Format>(config.format))
        .setTiling(vk::ImageTiling::eOptimal)
        .setInitialLayout(vk::ImageLayout::eUndefined)
        .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setSharingMode(vk::SharingMode::eExclusive);
    if (config.cubeCompatible) {
        imageInfo.setFlags(vk::ImageCreateFlagBits::eCubeCompatible);
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &outResources.image,
                       &outResources.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array image");
        return false;
    }

    // Create array view (for sampling all layers in shader) using vulkan-hpp builder
    auto arrayViewInfo = vk::ImageViewCreateInfo{}
        .setImage(outResources.image)
        .setViewType(config.cubeCompatible ? vk::ImageViewType::eCubeArray : vk::ImageViewType::e2DArray)
        .setFormat(static_cast<vk::Format>(config.format))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eDepth)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(config.arrayLayers));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&arrayViewInfo), nullptr, &outResources.arrayView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create depth array view");
        outResources.destroy(device, allocator);
        return false;
    }

    // Create per-layer views (for rendering to individual layers) using vulkan-hpp builder
    outResources.layerViews.resize(config.arrayLayers);
    for (uint32_t i = 0; i < config.arrayLayers; i++) {
        auto layerViewInfo = vk::ImageViewCreateInfo{}
            .setImage(outResources.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(static_cast<vk::Format>(config.format))
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eDepth)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(i)
                .setLayerCount(1));

        if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&layerViewInfo), nullptr, &outResources.layerViews[i]) != VK_SUCCESS) {
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
        auto framebufferInfo = vk::FramebufferCreateInfo{}
            .setRenderPass(renderPass)
            .setAttachmentCount(1)
            .setPAttachments(reinterpret_cast<const vk::ImageView*>(&depthImageViews[i]))
            .setWidth(extent.width)
            .setHeight(extent.height)
            .setLayers(1);

        if (vkCreateFramebuffer(device, reinterpret_cast<const VkFramebufferCreateInfo*>(&framebufferInfo), nullptr, &outFramebuffers[i]) != VK_SUCCESS) {
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
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createVertexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createIndexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createUniformBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createStorageBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst |
                  vk::BufferUsageFlagBits::eTransferSrc)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createStorageBufferHostReadable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
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

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createStorageBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
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

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createReadbackBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createVertexStorageBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createVertexStorageBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
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

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createIndexBufferHostWritable(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eIndexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createIndirectBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eIndirectBuffer |
                  vk::BufferUsageFlagBits::eStorageBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

bool VulkanResourceFactory::createDynamicVertexBuffer(VmaAllocator allocator, VkDeviceSize size, ManagedBuffer& outBuffer) {
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    return ManagedBuffer::create(allocator, reinterpret_cast<const VkBufferCreateInfo&>(bufferInfo), allocInfo, outBuffer);
}

// ============================================================================
// Sampler Factories
// ============================================================================

bool VulkanResourceFactory::createSamplerNearestClamp(VkDevice device, ManagedSampler& outSampler) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setMipmapMode(vk::SamplerMipmapMode::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(0.0f);

    return ManagedSampler::create(device, reinterpret_cast<const VkSamplerCreateInfo&>(samplerInfo), outSampler);
}

bool VulkanResourceFactory::createSamplerLinearClamp(VkDevice device, ManagedSampler& outSampler) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    return ManagedSampler::create(device, reinterpret_cast<const VkSamplerCreateInfo&>(samplerInfo), outSampler);
}

bool VulkanResourceFactory::createSamplerLinearRepeat(VkDevice device, ManagedSampler& outSampler) {
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eLinear)
        .setMinFilter(vk::Filter::eLinear)
        .setMipmapMode(vk::SamplerMipmapMode::eLinear)
        .setAddressModeU(vk::SamplerAddressMode::eRepeat)
        .setAddressModeV(vk::SamplerAddressMode::eRepeat)
        .setAddressModeW(vk::SamplerAddressMode::eRepeat)
        .setMinLod(0.0f)
        .setMaxLod(VK_LOD_CLAMP_NONE);

    return ManagedSampler::create(device, reinterpret_cast<const VkSamplerCreateInfo&>(samplerInfo), outSampler);
}

bool VulkanResourceFactory::createSamplerLinearRepeatAnisotropic(VkDevice device, float maxAnisotropy, ManagedSampler& outSampler) {
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

    return ManagedSampler::create(device, reinterpret_cast<const VkSamplerCreateInfo&>(samplerInfo), outSampler);
}

bool VulkanResourceFactory::createSamplerShadowComparison(VkDevice device, ManagedSampler& outSampler) {
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

    return ManagedSampler::create(device, reinterpret_cast<const VkSamplerCreateInfo&>(samplerInfo), outSampler);
}
