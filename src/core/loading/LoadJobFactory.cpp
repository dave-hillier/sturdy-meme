#include "LoadJobFactory.h"
#include "../vulkan/VmaBufferFactory.h"
#include "../vulkan/VmaImage.h"
#include "../vulkan/CommandBufferUtils.h"
#include "../ImageBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <stb_image.h>
#include <fstream>
#include <cstring>

namespace Loading {

LoadJob LoadJobFactory::createTextureJob(const std::string& id, const std::string& path,
                                         bool srgb, int priority) {
    LoadJob job;
    job.id = id;
    job.phase = "Textures";
    job.priority = priority;
    job.execute = [path, srgb, id]() -> std::unique_ptr<StagedResource> {
        int width, height, channels;
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

        if (!pixels) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to load texture '%s': %s", path.c_str(), stbi_failure_reason());
            return nullptr;
        }

        auto staged = std::make_unique<StagedTexture>();
        staged->width = static_cast<uint32_t>(width);
        staged->height = static_cast<uint32_t>(height);
        staged->channels = 4;
        staged->srgb = srgb;
        staged->name = id;

        size_t dataSize = width * height * 4;
        staged->pixels.resize(dataSize);
        std::memcpy(staged->pixels.data(), pixels, dataSize);

        stbi_image_free(pixels);

        SDL_Log("Loaded texture '%s': %dx%d", id.c_str(), width, height);
        return staged;
    };

    return job;
}

LoadJob LoadJobFactory::createHeightmapJob(const std::string& id, const std::string& path,
                                           int priority) {
    LoadJob job;
    job.id = id;
    job.phase = "Terrain";
    job.priority = priority;
    job.execute = [path, id]() -> std::unique_ptr<StagedResource> {
        int width, height, channels;

        // Try loading as 16-bit first
        stbi_us* pixels16 = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
        if (pixels16) {
            auto staged = std::make_unique<StagedHeightmap>();
            staged->width = static_cast<uint32_t>(width);
            staged->height = static_cast<uint32_t>(height);
            staged->name = id;

            size_t pixelCount = width * height;
            staged->heights.resize(pixelCount);
            std::memcpy(staged->heights.data(), pixels16, pixelCount * sizeof(uint16_t));

            stbi_image_free(pixels16);

            SDL_Log("Loaded heightmap '%s': %dx%d (16-bit)", id.c_str(), width, height);
            return staged;
        }

        // Fall back to 8-bit
        stbi_uc* pixels8 = stbi_load(path.c_str(), &width, &height, &channels, 1);
        if (pixels8) {
            auto staged = std::make_unique<StagedHeightmap>();
            staged->width = static_cast<uint32_t>(width);
            staged->height = static_cast<uint32_t>(height);
            staged->name = id;

            size_t pixelCount = width * height;
            staged->heights.resize(pixelCount);

            // Scale 8-bit to 16-bit
            for (size_t i = 0; i < pixelCount; ++i) {
                staged->heights[i] = static_cast<uint16_t>(pixels8[i]) << 8;
            }

            stbi_image_free(pixels8);

            SDL_Log("Loaded heightmap '%s': %dx%d (8-bit upscaled)", id.c_str(), width, height);
            return staged;
        }

        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to load heightmap '%s': %s", path.c_str(), stbi_failure_reason());
        return nullptr;
    };

    return job;
}

LoadJob LoadJobFactory::createFileJob(const std::string& id, const std::string& path,
                                      const std::string& phase, int priority) {
    LoadJob job;
    job.id = id;
    job.phase = phase;
    job.priority = priority;
    job.execute = [path, id]() -> std::unique_ptr<StagedResource> {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to open file '%s'", path.c_str());
            return nullptr;
        }

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        auto staged = std::make_unique<StagedBuffer>();
        staged->data.resize(size);
        staged->name = id;

        if (!file.read(reinterpret_cast<char*>(staged->data.data()), size)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to read file '%s'", path.c_str());
            return nullptr;
        }

        SDL_Log("Loaded file '%s': %zu bytes", id.c_str(), size);
        return staged;
    };

    return job;
}

LoadJob LoadJobFactory::createCustomJob(const std::string& id, const std::string& phase,
                                        std::function<std::unique_ptr<StagedResource>()> execute,
                                        int priority) {
    LoadJob job;
    job.id = id;
    job.phase = phase;
    job.priority = priority;
    job.execute = std::move(execute);
    return job;
}

// StagedResourceUploader implementation

UploadedTexture StagedResourceUploader::uploadTexture(const StagedTexture& staged) {
    UploadedTexture result;

    if (staged.pixels.empty() || staged.width == 0 || staged.height == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot upload empty texture '%s'", staged.name.c_str());
        return result;
    }

    VkDeviceSize imageSize = staged.pixels.size();

    // Create staging buffer
    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(ctx_.allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create staging buffer for '%s'", staged.name.c_str());
        return result;
    }

    // Copy to staging buffer
    void* data = stagingBuffer.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to map staging buffer for '%s'", staged.name.c_str());
        return result;
    }
    std::memcpy(data, staged.pixels.data(), imageSize);
    stagingBuffer.unmap();

    VkFormat imageFormat = staged.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Create image
    ManagedImage managedImage;
    if (!ImageBuilder(ctx_.allocator)
            .setExtent(staged.width, staged.height)
            .setFormat(imageFormat)
            .asTexture()
            .build(managedImage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create image for '%s'", staged.name.c_str());
        return result;
    }

    // Use CommandScope for one-time submission
    {
        CommandScope cmdScope(vk::Device(ctx_.device),
                              vk::CommandPool(ctx_.commandPool),
                              vk::Queue(ctx_.queue));
        if (!cmdScope.begin()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to begin command buffer for '%s'", staged.name.c_str());
            return result;
        }

        vk::CommandBuffer vkCmd(cmdScope.getHandle());

        // Transition image to TRANSFER_DST_OPTIMAL
        auto barrier1 = vk::ImageMemoryBarrier{}
            .setImage(managedImage.get())
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {}, barrier1);

        // Copy buffer to image
        auto region = vk::BufferImageCopy{}
            .setBufferOffset(0)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(0)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setImageOffset({0, 0, 0})
            .setImageExtent({staged.width, staged.height, 1});

        vkCmd.copyBufferToImage(
            stagingBuffer.get(),
            managedImage.get(),
            vk::ImageLayout::eTransferDstOptimal,
            region);

        // Transition image to SHADER_READ_ONLY_OPTIMAL
        auto barrier2 = vk::ImageMemoryBarrier{}
            .setImage(managedImage.get())
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1))
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        vkCmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, barrier2);

        if (!cmdScope.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to submit commands for '%s'", staged.name.c_str());
            return result;
        }
    }

    // Create image view using vulkan-hpp
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(managedImage.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(imageFormat))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    VkImageView imageView;
    vk::Device vkDevice(ctx_.device);
    try {
        imageView = static_cast<VkImageView>(vkDevice.createImageView(viewInfo));
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create image view for '%s': %s", staged.name.c_str(), e.what());
        return result;
    }

    // Transfer ownership from managed to result
    managedImage.releaseToRaw(result.image, result.allocation);
    result.view = imageView;
    result.width = staged.width;
    result.height = staged.height;
    result.valid = true;

    SDL_Log("Uploaded texture '%s': %ux%u", staged.name.c_str(), staged.width, staged.height);
    return result;
}

VkBuffer StagedResourceUploader::uploadBuffer(const StagedBuffer& staged, VkBufferUsageFlags usage) {
    (void)usage;  // Currently unused - we create storage buffers

    if (staged.data.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot upload empty buffer '%s'", staged.name.c_str());
        return VK_NULL_HANDLE;
    }

    VkDeviceSize bufferSize = staged.data.size();

    // Create staging buffer
    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(ctx_.allocator, bufferSize, stagingBuffer)) {
        return VK_NULL_HANDLE;
    }

    // Copy to staging
    void* data = stagingBuffer.map();
    if (!data) {
        return VK_NULL_HANDLE;
    }
    std::memcpy(data, staged.data.data(), bufferSize);
    stagingBuffer.unmap();

    // Create device-local storage buffer (includes TRANSFER_DST usage)
    ManagedBuffer deviceBuffer;
    if (!VmaBufferFactory::createStorageBuffer(ctx_.allocator, bufferSize, deviceBuffer)) {
        return VK_NULL_HANDLE;
    }

    // Use CommandScope for transfer
    {
        CommandScope cmdScope(vk::Device(ctx_.device),
                              vk::CommandPool(ctx_.commandPool),
                              vk::Queue(ctx_.queue));
        if (!cmdScope.begin()) {
            return VK_NULL_HANDLE;
        }

        vk::CommandBuffer vkCmd(cmdScope.getHandle());
        auto copyRegion = vk::BufferCopy{}.setSrcOffset(0).setDstOffset(0).setSize(bufferSize);
        vkCmd.copyBuffer(stagingBuffer.get(), deviceBuffer.get(), copyRegion);

        if (!cmdScope.end()) {
            return VK_NULL_HANDLE;
        }
    }

    SDL_Log("Uploaded buffer '%s': %zu bytes", staged.name.c_str(), staged.data.size());
    return deviceBuffer.release();
}

// AsyncTextureUploader implementation

AsyncTextureUploader::~AsyncTextureUploader() {
    shutdown();
}

bool AsyncTextureUploader::initialize(VkDevice device, VmaAllocator allocator,
                                       AsyncTransferManager* transferManager) {
    if (initialized_) {
        return true;
    }

    if (!device || !allocator || !transferManager) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader: Invalid initialization parameters");
        return false;
    }

    device_ = device;
    allocator_ = allocator;
    transferManager_ = transferManager;
    initialized_ = true;

    SDL_Log("AsyncTextureUploader: Initialized");
    return true;
}

void AsyncTextureUploader::shutdown() {
    if (!initialized_) {
        return;
    }

    // Wait for all pending transfers and clean up
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        vk::Device vkDevice(device_);
        for (auto& [id, upload] : pendingUploads_) {
            // Wait for transfer to complete before destroying resources
            if (upload.transferHandle.isValid()) {
                transferManager_->wait(upload.transferHandle);
            }

            // Clean up GPU resources
            if (upload.view != VK_NULL_HANDLE) {
                vkDevice.destroyImageView(upload.view);
            }
            // ManagedImage destructor handles image cleanup
        }
        pendingUploads_.clear();
    }

    initialized_ = false;
    SDL_Log("AsyncTextureUploader: Shutdown complete");
}

AsyncTextureHandle AsyncTextureUploader::submitTexture(const StagedTexture& staged) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader: Not initialized");
        return {};
    }

    if (staged.pixels.empty() || staged.width == 0 || staged.height == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader: Cannot upload empty texture '%s'", staged.name.c_str());
        return {};
    }

    VkFormat imageFormat = staged.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Create GPU image immediately (this is fast, just allocation)
    ManagedImage managedImage;
    if (!ImageBuilder(allocator_)
            .setExtent(staged.width, staged.height)
            .setFormat(imageFormat)
            .asTexture()
            .build(managedImage)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader: Failed to create image for '%s'", staged.name.c_str());
        return {};
    }

    // Create image view immediately using vulkan-hpp
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(managedImage.get())
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(static_cast<vk::Format>(imageFormat))
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    VkImageView imageView;
    vk::Device vkDevice(device_);
    try {
        imageView = static_cast<VkImageView>(vkDevice.createImageView(viewInfo));
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader: Failed to create image view for '%s': %s", staged.name.c_str(), e.what());
        return {};
    }

    // Submit async transfer
    TransferHandle transferHandle = transferManager_->submitImageTransfer(
        staged.pixels.data(),
        static_cast<vk::DeviceSize>(staged.pixels.size()),
        vk::Image(managedImage.get()),
        vk::Extent3D{staged.width, staged.height, 1},
        vk::ImageLayout::eShaderReadOnlyOptimal,
        1,  // mipLevels
        1   // layerCount
    );

    if (!transferHandle.isValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader: Failed to submit transfer for '%s'", staged.name.c_str());
        vkDevice.destroyImageView(imageView);
        return {};
    }

    // Track pending upload
    uint64_t id = nextId_++;
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingUploads_[id] = PendingUpload{
            .id = id,
            .transferHandle = transferHandle,
            .image = std::move(managedImage),
            .view = imageView,
            .width = staged.width,
            .height = staged.height,
            .name = staged.name
        };
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
        "AsyncTextureUploader: Submitted async upload for '%s' (id=%llu)",
        staged.name.c_str(), static_cast<unsigned long long>(id));

    return AsyncTextureHandle{id};
}

bool AsyncTextureUploader::isComplete(AsyncTextureHandle handle) const {
    if (!initialized_ || !handle.isValid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pendingUploads_.find(handle.id);
    if (it == pendingUploads_.end()) {
        return false;  // Already taken or invalid
    }

    return transferManager_->isComplete(it->second.transferHandle);
}

AsyncUploadedTexture AsyncTextureUploader::takeCompletedTexture(AsyncTextureHandle handle) {
    AsyncUploadedTexture result;

    if (!initialized_ || !handle.isValid()) {
        return result;
    }

    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pendingUploads_.find(handle.id);
    if (it == pendingUploads_.end()) {
        return result;  // Already taken or invalid
    }

    // Check if complete
    if (!transferManager_->isComplete(it->second.transferHandle)) {
        return result;  // Not ready yet
    }

    // Extract the completed upload
    PendingUpload& upload = it->second;

    // Transfer ownership to result
    VkImage rawImage;
    VmaAllocation rawAlloc;
    upload.image.releaseToRaw(rawImage, rawAlloc);

    result.image = rawImage;
    result.view = upload.view;
    result.allocation = rawAlloc;
    result.width = upload.width;
    result.height = upload.height;
    result.name = std::move(upload.name);
    result.valid = true;

    // Remove from pending
    pendingUploads_.erase(it);

    SDL_Log("AsyncTextureUploader: Completed upload '%s' (%ux%u)",
        result.name.c_str(), result.width, result.height);

    return result;
}

std::vector<AsyncUploadedTexture> AsyncTextureUploader::takeAllCompleted() {
    std::vector<AsyncUploadedTexture> results;

    if (!initialized_) {
        return results;
    }

    std::lock_guard<std::mutex> lock(pendingMutex_);

    std::vector<uint64_t> completedIds;
    for (const auto& [id, upload] : pendingUploads_) {
        if (transferManager_->isComplete(upload.transferHandle)) {
            completedIds.push_back(id);
        }
    }

    for (uint64_t id : completedIds) {
        auto it = pendingUploads_.find(id);
        if (it == pendingUploads_.end()) continue;

        PendingUpload& upload = it->second;

        AsyncUploadedTexture result;
        VkImage rawImage;
        VmaAllocation rawAlloc;
        upload.image.releaseToRaw(rawImage, rawAlloc);

        result.image = rawImage;
        result.view = upload.view;
        result.allocation = rawAlloc;
        result.width = upload.width;
        result.height = upload.height;
        result.name = std::move(upload.name);
        result.valid = true;

        results.push_back(std::move(result));
        pendingUploads_.erase(it);

        SDL_Log("AsyncTextureUploader: Completed upload '%s' (%ux%u)",
            results.back().name.c_str(), results.back().width, results.back().height);
    }

    return results;
}

size_t AsyncTextureUploader::getPendingCount() const {
    if (!initialized_) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(pendingMutex_);
    return pendingUploads_.size();
}

} // namespace Loading
