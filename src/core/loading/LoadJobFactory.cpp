#include "LoadJobFactory.h"
#include "../vulkan/VulkanResourceFactory.h"
#include "../vulkan/VulkanBarriers.h"
#include "../vulkan/VulkanHelpers.h"
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
    if (!VulkanResourceFactory::createStagingBuffer(ctx_.allocator, imageSize, stagingBuffer)) {
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

    // Use CommandScope for one-time submission with Barriers helper
    {
        CommandScope cmdScope(vk::Device(ctx_.device),
                              vk::CommandPool(ctx_.commandPool),
                              vk::Queue(ctx_.queue));
        if (!cmdScope.begin()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to begin command buffer for '%s'", staged.name.c_str());
            return result;
        }

        // Barriers::copyBufferToImage handles: transition to transfer dst, copy, transition to shader read
        Barriers::copyBufferToImage(cmdScope.getHandle(),
                                    stagingBuffer.get(),
                                    managedImage.get(),
                                    staged.width, staged.height);

        if (!cmdScope.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to submit commands for '%s'", staged.name.c_str());
            return result;
        }
    }

    // Create image view using C API (vulkan-hpp structured binding not always available)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = managedImage.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(ctx_.device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create image view for '%s'", staged.name.c_str());
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
    if (!VulkanResourceFactory::createStagingBuffer(ctx_.allocator, bufferSize, stagingBuffer)) {
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
    if (!VulkanResourceFactory::createStorageBuffer(ctx_.allocator, bufferSize, deviceBuffer)) {
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

} // namespace Loading
