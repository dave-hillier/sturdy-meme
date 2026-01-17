#pragma once

#include "vulkan/AsyncTransferManager.h"
#include "vulkan/ThreadedCommandPool.h"
#include "pipeline/FrameGraph.h"
#include "loading/LoadJobFactory.h"
#include "asset/AssetRegistry.h"

class VulkanContext;

/**
 * RenderingInfrastructure - Owns multi-threading and asset management infrastructure
 *
 * Extracted from Renderer to reduce coupling. Groups:
 * - AsyncTransferManager: Non-blocking GPU uploads
 * - ThreadedCommandPool: Parallel command buffer recording
 * - FrameGraph: Render pass dependency management
 * - AsyncTextureUploader: Background texture uploads
 * - AssetRegistry: Centralized asset management with deduplication
 *
 * Lifecycle:
 * - Create via default constructor
 * - Call init() after VulkanContext is ready
 * - Call shutdown() before destruction (or let destructor handle it)
 */
class RenderingInfrastructure {
public:
    RenderingInfrastructure() = default;
    ~RenderingInfrastructure();

    // Non-copyable, non-movable (owns GPU resources)
    RenderingInfrastructure(const RenderingInfrastructure&) = delete;
    RenderingInfrastructure& operator=(const RenderingInfrastructure&) = delete;
    RenderingInfrastructure(RenderingInfrastructure&&) = delete;
    RenderingInfrastructure& operator=(RenderingInfrastructure&&) = delete;

    /**
     * Initialize all infrastructure components.
     * @param context Vulkan context (provides device, queues, allocator)
     * @param threadCount Number of threads for parallel command recording (0 = single-threaded)
     * @return true if all critical components initialized successfully
     */
    bool init(VulkanContext& context, uint32_t threadCount);

    /**
     * Initialize the asset registry separately (needs command pool from VulkanContext).
     * Called after init() once command pool is available.
     */
    void initAssetRegistry(VkDevice device, VkPhysicalDevice physicalDevice,
                           VmaAllocator allocator, VkCommandPool commandPool,
                           VkQueue graphicsQueue);

    /**
     * Shutdown all infrastructure components in correct order.
     */
    void shutdown();

    /**
     * Process completed async transfers.
     * Call once per frame from the render thread.
     */
    void processPendingTransfers() { asyncTransferManager_.processPendingTransfers(); }

    // Component accessors
    AsyncTransferManager& asyncTransferManager() { return asyncTransferManager_; }
    const AsyncTransferManager& asyncTransferManager() const { return asyncTransferManager_; }

    ThreadedCommandPool& threadedCommandPool() { return threadedCommandPool_; }
    const ThreadedCommandPool& threadedCommandPool() const { return threadedCommandPool_; }

    FrameGraph& frameGraph() { return frameGraph_; }
    const FrameGraph& frameGraph() const { return frameGraph_; }

    Loading::AsyncTextureUploader& asyncTextureUploader() { return asyncTextureUploader_; }
    const Loading::AsyncTextureUploader& asyncTextureUploader() const { return asyncTextureUploader_; }

    AssetRegistry& assetRegistry() { return assetRegistry_; }
    const AssetRegistry& assetRegistry() const { return assetRegistry_; }

    bool isInitialized() const { return initialized_; }

private:
    AsyncTransferManager asyncTransferManager_;
    ThreadedCommandPool threadedCommandPool_;
    FrameGraph frameGraph_;
    Loading::AsyncTextureUploader asyncTextureUploader_;
    AssetRegistry assetRegistry_;

    bool initialized_ = false;
};
