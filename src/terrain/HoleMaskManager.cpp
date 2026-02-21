#include "HoleMaskManager.h"
#include "TerrainTileCache.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/vulkan/CommandBufferUtils.h"
#include "core/vulkan/SamplerFactory.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>

HoleMaskManager::~HoleMaskManager() {
    cleanup();
}

bool HoleMaskManager::init(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    graphicsQueue_ = info.graphicsQueue;
    commandPool_ = info.commandPool;
    storedTileResolution_ = info.storedTileResolution;
    maxLayers_ = info.maxLayers;

    // Create Vulkan 2D array image for hole mask (R8_UNORM: 0=solid, 255=hole)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator_)
                .setExtent(storedTileResolution_, storedTileResolution_)
                .setFormat(VK_FORMAT_R8_UNORM)
                .setArrayLayers(maxLayers_)
                .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build(device_, image, arrayView_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "HoleMaskManager: Failed to create hole mask array image");
            return false;
        }
        image.releaseToRaw(arrayImage_, arrayAllocation_);
    }

    // Transition hole mask array to shader read layout
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
                              vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, barrier);

        if (!cmd.end()) return false;
    }

    // Create sampler (linear filtering for smooth edges)
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "HoleMaskManager: raiiDevice is null");
        return false;
    }
    auto holeSampler = SamplerFactory::createSamplerLinearClamp(*info.raiiDevice);
    if (!holeSampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "HoleMaskManager: Failed to create hole mask sampler");
        return false;
    }
    sampler_ = std::move(*holeSampler);

    SDL_Log("HoleMaskManager: Created hole mask array (%ux%u x %u layers)",
            storedTileResolution_, storedTileResolution_, maxLayers_);

    return true;
}

void HoleMaskManager::cleanup() {
    sampler_.reset();
    if (arrayView_) {
        vkDestroyImageView(device_, arrayView_, nullptr);
        arrayView_ = VK_NULL_HANDLE;
    }
    if (arrayImage_) {
        vmaDestroyImage(allocator_, arrayImage_, arrayAllocation_);
        arrayImage_ = VK_NULL_HANDLE;
    }
    holes_.clear();
}

bool HoleMaskManager::isHole(float x, float z) const {
    return TileGrid::isPointInHole(x, z, holes_);
}

std::vector<uint8_t> HoleMaskManager::rasterizeHolesForTile(
    float tileMinX, float tileMinZ, float tileMaxX, float tileMaxZ, uint32_t resolution) const {
    return TileGrid::rasterizeHolesForTile(tileMinX, tileMinZ, tileMaxX, tileMaxZ, resolution, holes_);
}

std::vector<uint8_t> HoleMaskManager::generateTileHoleMask(const TerrainTile& tile) const {
    return TileGrid::rasterizeHolesForTile(
        tile.worldMinX, tile.worldMinZ,
        tile.worldMaxX, tile.worldMaxZ,
        storedTileResolution_, holes_);
}

void HoleMaskManager::uploadTileHoleMask(const TerrainTile& tile, int32_t layerIndex) {
    if (layerIndex < 0 || layerIndex >= static_cast<int32_t>(maxLayers_)) return;

    std::vector<uint8_t> holeMaskData = generateTileHoleMask(tile);

    VkDeviceSize imageSize = holeMaskData.size() * sizeof(uint8_t);
    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator_, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "HoleMaskManager: Failed to create staging buffer for hole mask");
        return;
    }

    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, holeMaskData.data(), imageSize);
    stagingBuffer.unmap();

    CommandScope cmd(device_, commandPool_, graphicsQueue_);
    if (!cmd.begin()) return;

    vk::CommandBuffer vkCmd(cmd.get());

    // Transition layer to transfer dst
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
                .setBaseArrayLayer(static_cast<uint32_t>(layerIndex))
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
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
                .setBaseArrayLayer(static_cast<uint32_t>(layerIndex))
                .setLayerCount(1))
            .setImageOffset({0, 0, 0})
            .setImageExtent({storedTileResolution_, storedTileResolution_, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), arrayImage_,
                                vk::ImageLayout::eTransferDstOptimal, region);
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
                .setBaseArrayLayer(static_cast<uint32_t>(layerIndex))
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, barrier);
    }

    cmd.end();
}

void HoleMaskManager::addHoleCircle(float centerX, float centerZ, float radius,
                                     const std::vector<TerrainTile*>& activeTiles) {
    holes_.push_back({centerX, centerZ, radius});

    // Re-upload hole masks for affected active tiles
    for (TerrainTile* tile : activeTiles) {
        if (tile && tile->arrayLayerIndex >= 0) {
            float closestX = std::clamp(centerX, tile->worldMinX, tile->worldMaxX);
            float closestZ = std::clamp(centerZ, tile->worldMinZ, tile->worldMaxZ);
            float dx = centerX - closestX;
            float dz = centerZ - closestZ;
            if (dx * dx + dz * dz <= radius * radius) {
                uploadTileHoleMask(*tile, tile->arrayLayerIndex);
            }
        }
    }

    SDL_Log("HoleMaskManager: Added hole circle at (%.1f, %.1f) radius %.1f, total holes: %zu",
            centerX, centerZ, radius, holes_.size());
}

void HoleMaskManager::removeHoleCircle(float centerX, float centerZ, float radius,
                                        const std::vector<TerrainTile*>& activeTiles) {
    auto it = std::remove_if(holes_.begin(), holes_.end(), [&](const TerrainHole& h) {
        return std::abs(h.centerX - centerX) < 0.1f &&
               std::abs(h.centerZ - centerZ) < 0.1f &&
               std::abs(h.radius - radius) < 0.1f;
    });
    if (it != holes_.end()) {
        holes_.erase(it, holes_.end());

        // Re-upload hole masks for affected active tiles
        for (TerrainTile* tile : activeTiles) {
            if (tile && tile->arrayLayerIndex >= 0) {
                float closestX = std::clamp(centerX, tile->worldMinX, tile->worldMaxX);
                float closestZ = std::clamp(centerZ, tile->worldMinZ, tile->worldMaxZ);
                float dx = centerX - closestX;
                float dz = centerZ - closestZ;
                if (dx * dx + dz * dz <= radius * radius) {
                    uploadTileHoleMask(*tile, tile->arrayLayerIndex);
                }
            }
        }

        SDL_Log("HoleMaskManager: Removed hole circle at (%.1f, %.1f), total holes: %zu",
                centerX, centerZ, holes_.size());
    }
}

void HoleMaskManager::uploadAllActiveMasks(const std::vector<TerrainTile*>& activeTiles) {
    for (TerrainTile* tile : activeTiles) {
        if (tile && tile->arrayLayerIndex >= 0) {
            uploadTileHoleMask(*tile, tile->arrayLayerIndex);
        }
    }
}
