#include "ThreadedCommandPool.h"
#include "VulkanContext.h"
#include <SDL3/SDL_log.h>

bool ThreadedCommandPool::initialize(VulkanContext& context, uint32_t threadCount) {
    if (initialized_) {
        return true;
    }

    device_ = context.getVkDevice();
    graphicsQueueFamily_ = context.getGraphicsQueueFamily();
    threadCount_ = threadCount;

    // Calculate total pools needed: frames × threads
    // As mentioned in video: "we need to take the number of frames in flight
    // and multiply that by the number of threads we have access to"
    uint32_t totalPools = MAX_FRAMES_IN_FLIGHT * threadCount;
    SDL_Log("ThreadedCommandPool: Creating %u pools (%u frames x %u threads)",
            totalPools, MAX_FRAMES_IN_FLIGHT, threadCount);

    pools_.resize(MAX_FRAMES_IN_FLIGHT);

    try {
        for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
            pools_[frame].resize(threadCount);

            for (uint32_t thread = 0; thread < threadCount; ++thread) {
                // Create command pool with reset capability
                auto poolInfo = vk::CommandPoolCreateInfo{}
                    .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
                    .setQueueFamilyIndex(graphicsQueueFamily_);

                pools_[frame][thread].pool = vk::raii::CommandPool(
                    context.getRaiiDevice(), poolInfo);

                // Pre-allocate command buffers
                auto primaryAllocInfo = vk::CommandBufferAllocateInfo{}
                    .setCommandPool(*pools_[frame][thread].pool)
                    .setLevel(vk::CommandBufferLevel::ePrimary)
                    .setCommandBufferCount(INITIAL_PRIMARY_BUFFERS);

                pools_[frame][thread].primaryBuffers =
                    device_.allocateCommandBuffers(primaryAllocInfo);

                auto secondaryAllocInfo = vk::CommandBufferAllocateInfo{}
                    .setCommandPool(*pools_[frame][thread].pool)
                    .setLevel(vk::CommandBufferLevel::eSecondary)
                    .setCommandBufferCount(INITIAL_SECONDARY_BUFFERS);

                pools_[frame][thread].secondaryBuffers =
                    device_.allocateCommandBuffers(secondaryAllocInfo);
            }
        }
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "ThreadedCommandPool: Failed to create pools: %s", e.what());
        shutdown();
        return false;
    }

    initialized_ = true;
    SDL_Log("ThreadedCommandPool: Initialized with %u command pools", totalPools);
    return true;
}

void ThreadedCommandPool::shutdown() {
    if (!initialized_) {
        return;
    }

    pools_.clear();
    initialized_ = false;
    SDL_Log("ThreadedCommandPool: Shutdown complete");
}

void ThreadedCommandPool::resetFrame(uint32_t frameIndex) {
    if (!initialized_ || frameIndex >= MAX_FRAMES_IN_FLIGHT) {
        return;
    }

    // Reset all pools for this frame
    // This is called at frame start after waiting for the frame's fence
    for (auto& threadPool : pools_[frameIndex]) {
        // Reset command pool (implicitly resets all command buffers)
        device_.resetCommandPool(*threadPool.pool, {});

        // Reset allocation counters
        threadPool.nextPrimary = 0;
        threadPool.nextSecondary = 0;
    }
}

vk::CommandBuffer ThreadedCommandPool::allocatePrimary(uint32_t frameIndex, uint32_t threadId) {
    if (!initialized_ || frameIndex >= MAX_FRAMES_IN_FLIGHT || threadId >= threadCount_) {
        return nullptr;
    }

    auto& threadPool = pools_[frameIndex][threadId];

    // Check if we have a pre-allocated buffer available
    if (threadPool.nextPrimary < threadPool.primaryBuffers.size()) {
        return threadPool.primaryBuffers[threadPool.nextPrimary++];
    }

    // Need to allocate more - this should be rare
    {
        std::lock_guard<std::mutex> lock(allocationMutex_);

        auto allocInfo = vk::CommandBufferAllocateInfo{}
            .setCommandPool(*threadPool.pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);

        auto buffers = device_.allocateCommandBuffers(allocInfo);
        threadPool.primaryBuffers.push_back(buffers[0]);
        return threadPool.primaryBuffers[threadPool.nextPrimary++];
    }
}

vk::CommandBuffer ThreadedCommandPool::allocateSecondary(uint32_t frameIndex, uint32_t threadId) {
    if (!initialized_ || frameIndex >= MAX_FRAMES_IN_FLIGHT || threadId >= threadCount_) {
        return nullptr;
    }

    auto& threadPool = pools_[frameIndex][threadId];

    // Check if we have a pre-allocated buffer available
    if (threadPool.nextSecondary < threadPool.secondaryBuffers.size()) {
        return threadPool.secondaryBuffers[threadPool.nextSecondary++];
    }

    // Need to allocate more - this should be rare
    {
        std::lock_guard<std::mutex> lock(allocationMutex_);

        auto allocInfo = vk::CommandBufferAllocateInfo{}
            .setCommandPool(*threadPool.pool)
            .setLevel(vk::CommandBufferLevel::eSecondary)
            .setCommandBufferCount(1);

        auto buffers = device_.allocateCommandBuffers(allocInfo);
        threadPool.secondaryBuffers.push_back(buffers[0]);
        return threadPool.secondaryBuffers[threadPool.nextSecondary++];
    }
}

vk::CommandPool ThreadedCommandPool::getPool(uint32_t frameIndex, uint32_t threadId) const {
    if (!initialized_ || frameIndex >= MAX_FRAMES_IN_FLIGHT || threadId >= threadCount_) {
        return nullptr;
    }
    return *pools_[frameIndex][threadId].pool;
}
