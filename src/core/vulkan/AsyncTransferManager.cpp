#include "AsyncTransferManager.h"
#include "VulkanContext.h"
#include <SDL3/SDL_log.h>

AsyncTransferManager::~AsyncTransferManager() {
    shutdown();
}

bool AsyncTransferManager::initialize(VulkanContext& context) {
    if (initialized_) {
        return true;
    }

    context_.emplace(context);
    device_ = context.getVkDevice();
    transferQueue_ = context.getVkTransferQueue();
    transferQueueFamily_ = context.getTransferQueueFamily();
    graphicsQueueFamily_ = context.getGraphicsQueueFamily();
    hasDedicatedTransfer_ = context.hasDedicatedTransferQueue();
    allocator_ = context.getAllocator();

    // Create command pool for transfer queue
    auto poolInfo = vk::CommandPoolCreateInfo{}
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                  vk::CommandPoolCreateFlagBits::eTransient)
        .setQueueFamilyIndex(transferQueueFamily_);

    try {
        transferCommandPool_.emplace(context.getRaiiDevice(), poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager: Failed to create command pool: %s", e.what());
        return false;
    }

    // Create timeline semaphore for tracking transfer completion (Vulkan 1.2)
    try {
        auto typeInfo = vk::SemaphoreTypeCreateInfo{}
            .setSemaphoreType(vk::SemaphoreType::eTimeline)
            .setInitialValue(0);

        auto semaphoreInfo = vk::SemaphoreCreateInfo{}
            .setPNext(&typeInfo);

        transferTimeline_.emplace(context.getRaiiDevice(), semaphoreInfo);
        nextTimelineValue_ = 1;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager: Failed to create timeline semaphore: %s", e.what());
        return false;
    }

    initialized_ = true;
    SDL_Log("AsyncTransferManager: Initialized with timeline semaphore (dedicated transfer: %s)",
            hasDedicatedTransfer_ ? "yes" : "no");
    return true;
}

void AsyncTransferManager::shutdown() {
    if (!initialized_) {
        return;
    }

    waitAll();

    // Clear staging buffer pool
    {
        std::lock_guard<std::mutex> lock(stagingMutex_);
        stagingBufferPool_.clear();
    }

    // Clear any remaining transfers
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        pendingTransfers_.clear();
    }

    transferTimeline_.reset();
    transferCommandPool_.reset();
    initialized_ = false;

    SDL_Log("AsyncTransferManager: Shutdown complete");
}

vk::CommandBuffer AsyncTransferManager::allocateTransferCommandBuffer() {
    auto allocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(*transferCommandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    auto buffers = device_.allocateCommandBuffers(allocInfo);
    return buffers[0];
}

void AsyncTransferManager::freeTransferCommandBuffer(vk::CommandBuffer cmd) {
    device_.freeCommandBuffers(*transferCommandPool_, cmd);
}

VmaBuffer AsyncTransferManager::acquireStagingBuffer(vk::DeviceSize size) {
    // Try to find a buffer from the pool that's large enough
    {
        std::lock_guard<std::mutex> lock(stagingMutex_);
        for (auto it = stagingBufferPool_.begin(); it != stagingBufferPool_.end(); ++it) {
            VmaAllocationInfo allocInfo;
            vmaGetAllocationInfo(allocator_, it->getAllocation(), &allocInfo);
            if (allocInfo.size >= size) {
                VmaBuffer buffer = std::move(*it);
                stagingBufferPool_.erase(it);
                return buffer;
            }
        }
    }

    // Create new staging buffer
    VmaBuffer buffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator_, size, buffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager: Failed to create staging buffer (size: %llu)",
            static_cast<unsigned long long>(size));
        return {};
    }
    return buffer;
}

void AsyncTransferManager::releaseStagingBuffer(VmaBuffer buffer) {
    if (!buffer) return;

    std::lock_guard<std::mutex> lock(stagingMutex_);
    if (stagingBufferPool_.size() < MAX_STAGING_POOL_SIZE) {
        stagingBufferPool_.push_back(std::move(buffer));
    }
    // Otherwise buffer is destroyed when going out of scope
}

TransferHandle AsyncTransferManager::submitBufferTransfer(
    const void* data, vk::DeviceSize size,
    vk::Buffer dstBuffer, vk::DeviceSize dstOffset,
    CompletionCallback onComplete)
{
    if (!initialized_ || !data || size == 0) {
        return {};
    }

    // Acquire staging buffer and copy data
    VmaBuffer staging = acquireStagingBuffer(size);
    if (!staging) {
        return {};
    }

    // Copy data to staging buffer (VMA buffers are created mapped)
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(allocator_, staging.getAllocation(), &allocInfo);
    memcpy(allocInfo.pMappedData, data, size);
    vmaFlushAllocation(allocator_, staging.getAllocation(), 0, size);

    // Allocate command buffer
    vk::CommandBuffer cmd = allocateTransferCommandBuffer();

    // Record transfer commands
    cmd.begin(vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    auto copyRegion = vk::BufferCopy{}
        .setSrcOffset(0)
        .setDstOffset(dstOffset)
        .setSize(size);
    cmd.copyBuffer(staging.get(), dstBuffer, copyRegion);

    cmd.end();

    // Get next timeline value to signal for this transfer
    uint64_t timelineValue = nextTimelineValue_++;

    // Submit to transfer queue with timeline semaphore signal
    uint64_t signalValue = timelineValue;
    auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
        .setSignalSemaphoreValueCount(1)
        .setPSignalSemaphoreValues(&signalValue);

    vk::Semaphore signalSemaphores[] = {**transferTimeline_};
    auto submitInfo = vk::SubmitInfo{}
        .setPNext(&timelineInfo)
        .setCommandBuffers(cmd)
        .setSignalSemaphores(signalSemaphores);

    transferQueue_.submit(submitInfo, nullptr);  // No fence, using timeline semaphore

    // Track pending transfer
    uint64_t id = nextTransferId_++;
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        pendingTransfers_.push_back(PendingTransfer{
            .id = id,
            .timelineValue = timelineValue,
            .cmdBuffer = cmd,
            .stagingBuffer = std::move(staging),
            .onComplete = std::move(onComplete),
            .needsOwnershipTransfer = false,
            .targetImage = nullptr,
            .finalLayout = vk::ImageLayout::eUndefined
        });
    }

    return TransferHandle{id};
}

TransferHandle AsyncTransferManager::submitImageTransfer(
    const void* data, vk::DeviceSize size,
    vk::Image dstImage, vk::Extent3D extent,
    vk::ImageLayout finalLayout,
    uint32_t mipLevels,
    uint32_t layerCount,
    CompletionCallback onComplete)
{
    if (!initialized_ || !data || size == 0) {
        return {};
    }

    // Acquire staging buffer and copy data
    VmaBuffer staging = acquireStagingBuffer(size);
    if (!staging) {
        return {};
    }

    // Copy data to staging buffer
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(allocator_, staging.getAllocation(), &allocInfo);
    memcpy(allocInfo.pMappedData, data, size);
    vmaFlushAllocation(allocator_, staging.getAllocation(), 0, size);

    // Allocate command buffer
    vk::CommandBuffer cmd = allocateTransferCommandBuffer();

    // Record transfer commands
    cmd.begin(vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // Transition image to transfer destination layout
    auto barrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(dstImage)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(layerCount))
        .setSrcAccessMask(vk::AccessFlagBits::eNone)
        .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer,
        {}, {}, {}, barrier);

    // Copy buffer to image
    auto region = vk::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(vk::ImageSubresourceLayers{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(layerCount))
        .setImageOffset({0, 0, 0})
        .setImageExtent(extent);

    cmd.copyBufferToImage(staging.get(), dstImage,
        vk::ImageLayout::eTransferDstOptimal, region);

    // Transition to final layout
    // If using dedicated transfer queue and final layout needs graphics access,
    // we need queue ownership transfer
    bool needsOwnershipTransfer = hasDedicatedTransfer_ &&
        (finalLayout == vk::ImageLayout::eShaderReadOnlyOptimal ||
         finalLayout == vk::ImageLayout::eGeneral);

    if (needsOwnershipTransfer) {
        // Release ownership from transfer queue
        auto releaseBarrier = vk::ImageMemoryBarrier{}
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(finalLayout)
            .setSrcQueueFamilyIndex(transferQueueFamily_)
            .setDstQueueFamilyIndex(graphicsQueueFamily_)
            .setImage(dstImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(layerCount))
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eNone);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {}, releaseBarrier);
    } else {
        // Same queue, just transition to final layout
        auto finalBarrier = vk::ImageMemoryBarrier{}
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(finalLayout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(dstImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(layerCount))
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, finalBarrier);
    }

    cmd.end();

    // Get next timeline value to signal for this transfer
    uint64_t timelineValue = nextTimelineValue_++;

    // Submit to transfer queue with timeline semaphore signal
    uint64_t signalValue = timelineValue;
    auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
        .setSignalSemaphoreValueCount(1)
        .setPSignalSemaphoreValues(&signalValue);

    vk::Semaphore signalSemaphores[] = {**transferTimeline_};
    auto submitInfo = vk::SubmitInfo{}
        .setPNext(&timelineInfo)
        .setCommandBuffers(cmd)
        .setSignalSemaphores(signalSemaphores);

    transferQueue_.submit(submitInfo, nullptr);  // No fence, using timeline semaphore

    // Track pending transfer
    uint64_t id = nextTransferId_++;
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        pendingTransfers_.push_back(PendingTransfer{
            .id = id,
            .timelineValue = timelineValue,
            .cmdBuffer = cmd,
            .stagingBuffer = std::move(staging),
            .onComplete = std::move(onComplete),
            .needsOwnershipTransfer = needsOwnershipTransfer,
            .targetImage = dstImage,
            .finalLayout = finalLayout
        });
    }

    return TransferHandle{id};
}

bool AsyncTransferManager::isComplete(TransferHandle handle) const {
    if (!handle.isValid() || !transferTimeline_) return true;

    std::lock_guard<std::mutex> lock(transferMutex_);
    for (const auto& transfer : pendingTransfers_) {
        if (transfer.id == handle.id) {
            // Non-blocking check using timeline semaphore counter
            uint64_t currentValue = device_.getSemaphoreCounterValue(**transferTimeline_);
            return currentValue >= transfer.timelineValue;
        }
    }
    return true; // Not found = already completed
}

void AsyncTransferManager::wait(TransferHandle handle) {
    if (!handle.isValid() || !transferTimeline_) return;

    uint64_t waitValue = 0;
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        for (const auto& transfer : pendingTransfers_) {
            if (transfer.id == handle.id) {
                waitValue = transfer.timelineValue;
                break;
            }
        }
    }

    if (waitValue > 0) {
        // Wait using timeline semaphore
        auto waitInfo = vk::SemaphoreWaitInfo{}
            .setSemaphores(**transferTimeline_)
            .setValues(waitValue);
        (void)device_.waitSemaphores(waitInfo, UINT64_MAX);
    }

    // Process to clean up this transfer
    processPendingTransfers();
}

void AsyncTransferManager::processPendingTransfers() {
    if (!initialized_ || !transferTimeline_) return;

    std::vector<PendingTransfer> completed;

    // Check for completed transfers using timeline semaphore counter (non-blocking)
    {
        std::lock_guard<std::mutex> lock(transferMutex_);

        // Get current timeline value once (non-blocking)
        uint64_t currentValue = device_.getSemaphoreCounterValue(**transferTimeline_);

        auto it = pendingTransfers_.begin();
        while (it != pendingTransfers_.end()) {
            if (currentValue >= it->timelineValue) {
                completed.push_back(std::move(*it));
                it = pendingTransfers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Process completed transfers
    for (auto& transfer : completed) {
        // Free command buffer
        freeTransferCommandBuffer(transfer.cmdBuffer);

        // Return staging buffer to pool
        releaseStagingBuffer(std::move(transfer.stagingBuffer));

        // Execute completion callback
        if (transfer.onComplete) {
            transfer.onComplete();
        }

        // Note: Queue ownership transfer acquire happens implicitly
        // when graphics queue first uses the resource with proper barrier
    }
}

void AsyncTransferManager::waitAll() {
    if (!initialized_ || !transferTimeline_) return;

    // Find the highest timeline value we need to wait for
    uint64_t maxValue = 0;
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        for (const auto& transfer : pendingTransfers_) {
            maxValue = std::max(maxValue, transfer.timelineValue);
        }
    }

    // Wait for all pending transfers to complete
    if (maxValue > 0) {
        auto waitInfo = vk::SemaphoreWaitInfo{}
            .setSemaphores(**transferTimeline_)
            .setValues(maxValue);
        (void)device_.waitSemaphores(waitInfo, UINT64_MAX);
    }

    processPendingTransfers();
}

size_t AsyncTransferManager::getPendingCount() const {
    std::lock_guard<std::mutex> lock(transferMutex_);
    return pendingTransfers_.size();
}
