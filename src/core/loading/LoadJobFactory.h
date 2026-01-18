#pragma once

#include "LoadJobQueue.h"
#include "../vulkan/AsyncTransferManager.h"
#include "../vulkan/VmaImage.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>

/**
 * LoadJobFactory - Factory for creating common load jobs
 *
 * Provides convenience functions for creating async load jobs
 * for textures, meshes, heightmaps, and custom data.
 */

namespace Loading {

/**
 * Factory for creating common load jobs
 */
class LoadJobFactory {
public:
    /**
     * Create a texture load job (PNG/JPG via stb_image)
     * @param id Unique identifier
     * @param path Full path to texture file
     * @param srgb Whether to treat as sRGB
     * @param priority Lower = higher priority
     */
    static LoadJob createTextureJob(const std::string& id, const std::string& path,
                                    bool srgb = true, int priority = 0);

    /**
     * Create a heightmap load job (16-bit or 8-bit PNG)
     * @param id Unique identifier
     * @param path Full path to heightmap file
     * @param priority Lower = higher priority
     */
    static LoadJob createHeightmapJob(const std::string& id, const std::string& path,
                                      int priority = 0);

    /**
     * Create a raw file load job
     * @param id Unique identifier
     * @param path Full path to file
     * @param phase Phase name for progress display
     * @param priority Lower = higher priority
     */
    static LoadJob createFileJob(const std::string& id, const std::string& path,
                                 const std::string& phase = "Data", int priority = 0);

    /**
     * Create a custom CPU job (e.g., procedural generation)
     * @param id Unique identifier
     * @param phase Phase name for progress display
     * @param execute Function that produces the staged resource
     * @param priority Lower = higher priority
     */
    static LoadJob createCustomJob(const std::string& id, const std::string& phase,
                                   std::function<std::unique_ptr<StagedResource>()> execute,
                                   int priority = 0);
};

/**
 * GPU upload context for staged resources
 */
struct GPUUploadContext {
    VmaAllocator allocator;
    VkDevice device;
    VkCommandPool commandPool;
    VkQueue queue;
    VkPhysicalDevice physicalDevice;
};

/**
 * Result of a GPU texture upload
 */
struct UploadedTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

/**
 * StagedResourceUploader - Uploads staged resources to GPU
 *
 * Call these from the main thread after async loading completes.
 */
class StagedResourceUploader {
public:
    explicit StagedResourceUploader(const GPUUploadContext& ctx) : ctx_(ctx) {}

    /**
     * Upload a staged texture to GPU
     * Creates VkImage, VkImageView, handles staging buffer and transitions
     */
    UploadedTexture uploadTexture(const StagedTexture& staged);

    /**
     * Upload a staged buffer to GPU (returns VkBuffer)
     */
    VkBuffer uploadBuffer(const StagedBuffer& staged, VkBufferUsageFlags usage);

private:
    GPUUploadContext ctx_;
};

/**
 * Handle to a pending async texture upload.
 * Use AsyncTextureUploader::isComplete() to check status.
 */
struct AsyncTextureHandle {
    uint64_t id = 0;
    bool isValid() const { return id != 0; }
};

/**
 * Result of a completed async texture upload.
 * Returned when isComplete() returns true.
 */
struct AsyncUploadedTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string name;
    bool valid = false;
};

/**
 * AsyncTextureUploader - Non-blocking texture uploads using AsyncTransferManager
 *
 * Flow:
 * 1. Call submitTexture() with staged texture data - returns immediately with handle
 * 2. Each frame, call processCompletedUploads() to check for completed transfers
 * 3. Use isComplete()/getCompletedTexture() to retrieve finished textures
 *
 * The GPU image is created immediately (fast), but data transfer is async.
 * Textures are usable only after their transfer completes.
 */
class AsyncTextureUploader {
public:
    AsyncTextureUploader() = default;
    ~AsyncTextureUploader();

    // Non-copyable
    AsyncTextureUploader(const AsyncTextureUploader&) = delete;
    AsyncTextureUploader& operator=(const AsyncTextureUploader&) = delete;

    /**
     * Initialize the uploader with GPU resources
     */
    bool initialize(VkDevice device, VmaAllocator allocator, AsyncTransferManager* transferManager);

    /**
     * Submit a staged texture for async GPU upload.
     * Creates GPU image immediately, submits async transfer.
     * @return Handle to track upload completion
     */
    AsyncTextureHandle submitTexture(const StagedTexture& staged);

    /**
     * Check if a specific upload is complete
     */
    bool isComplete(AsyncTextureHandle handle) const;

    /**
     * Get a completed texture (removes from internal tracking).
     * Only call after isComplete() returns true.
     * @return The uploaded texture, or invalid texture if not ready
     */
    AsyncUploadedTexture takeCompletedTexture(AsyncTextureHandle handle);

    /**
     * Get all completed textures (removes from internal tracking).
     * Convenience method to process all finished uploads at once.
     */
    std::vector<AsyncUploadedTexture> takeAllCompleted();

    /**
     * Get count of pending uploads
     */
    size_t getPendingCount() const;

    /**
     * Shutdown and cleanup all pending uploads
     */
    void shutdown();

private:
    struct PendingUpload {
        uint64_t id;
        TransferHandle transferHandle;
        ManagedImage image;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::string name;
    };

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    AsyncTransferManager* transferManager_ = nullptr;

    mutable std::mutex pendingMutex_;
    std::unordered_map<uint64_t, PendingUpload> pendingUploads_;
    uint64_t nextId_ = 1;

    bool initialized_ = false;
};

} // namespace Loading
