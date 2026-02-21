#include "TileArrayManager.h"
#include "TerrainTileCache.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/vulkan/CommandBufferUtils.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <cmath>

TileArrayManager::~TileArrayManager() {
    cleanup();
}

bool TileArrayManager::init(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    graphicsQueue_ = info.graphicsQueue;
    commandPool_ = info.commandPool;
    storedTileResolution_ = info.storedTileResolution;
    maxLayers_ = info.maxLayers;

    // Create tile array image (2D array texture with maxLayers layers)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator_)
                .setExtent(storedTileResolution_, storedTileResolution_)
                .setFormat(VK_FORMAT_R32_SFLOAT)
                .setArrayLayers(maxLayers_)
                .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build(device_, image, arrayView_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TileArrayManager: Failed to create tile array image");
            return false;
        }
        image.releaseToRaw(arrayImage_, arrayAllocation_);
    }

    // Transition tile array to shader read layout
    {
        CommandScope cmd(device_, commandPool_, graphicsQueue_);
        if (!cmd.begin()) return false;

        vk::CommandBuffer vkCmd(cmd.get());
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(arrayImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(maxLayers_));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::PipelineStageFlagBits::eVertexShader,
                              {}, {}, {}, barrier);

        if (!cmd.end()) return false;
    }

    // Initialize all layers as free
    freeLayers_.fill(true);

    SDL_Log("TileArrayManager: Created tile array (%ux%u x %u layers)",
            storedTileResolution_, storedTileResolution_, maxLayers_);

    return true;
}

void TileArrayManager::cleanup() {
    if (arrayView_) {
        vkDestroyImageView(device_, arrayView_, nullptr);
        arrayView_ = VK_NULL_HANDLE;
    }
    if (arrayImage_) {
        vmaDestroyImage(allocator_, arrayImage_, arrayAllocation_);
        arrayImage_ = VK_NULL_HANDLE;
    }
}

int32_t TileArrayManager::allocateLayer() {
    for (uint32_t i = 0; i < maxLayers_; i++) {
        if (freeLayers_[i]) {
            freeLayers_[i] = false;
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void TileArrayManager::freeLayer(int32_t layerIndex) {
    if (layerIndex >= 0 && layerIndex < static_cast<int32_t>(maxLayers_)) {
        freeLayers_[layerIndex] = true;
    }
}

void TileArrayManager::copyTileToLayer(const TerrainTile& tile, uint32_t layerIndex) {
    if (tile.cpuData.empty() || layerIndex >= maxLayers_) return;

    // Infer actual resolution from tile data
    uint32_t actualRes = static_cast<uint32_t>(std::sqrt(tile.cpuData.size()));

    VkDeviceSize imageSize = tile.cpuData.size() * sizeof(float);

    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator_, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TileArrayManager: Failed to create staging buffer for tile copy");
        return;
    }

    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, tile.cpuData.data(), imageSize);
    stagingBuffer.unmap();

    CommandScope cmd(device_, commandPool_, graphicsQueue_);
    if (!cmd.begin()) return;

    vk::CommandBuffer vkCmd(cmd.get());

    // Transition tile array layer to transfer dst
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eShaderRead)
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(arrayImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(layerIndex)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eVertexShader,
                              vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, barrier);
    }

    // Copy buffer to image layer
    {
        auto region = vk::BufferImageCopy{}
            .setBufferOffset(0)
            .setBufferRowLength(0)
            .setBufferImageHeight(0)
            .setImageSubresource(vk::ImageSubresourceLayers{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setMipLevel(0)
                .setBaseArrayLayer(layerIndex)
                .setLayerCount(1))
            .setImageOffset({0, 0, 0})
            .setImageExtent({actualRes, actualRes, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), arrayImage_, vk::ImageLayout::eTransferDstOptimal, region);
    }

    // Transition back to shader read
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(arrayImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(layerIndex)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eVertexShader,
                              {}, {}, {}, barrier);
    }

    cmd.end();
}
