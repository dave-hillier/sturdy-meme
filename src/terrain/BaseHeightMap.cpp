#include "BaseHeightMap.h"
#include "TerrainTileCache.h"
#include "TerrainHeight.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/vulkan/CommandBufferUtils.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <algorithm>
#include <cmath>

BaseHeightMap::~BaseHeightMap() {
    cleanup();
}

void BaseHeightMap::init(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    graphicsQueue_ = info.graphicsQueue;
    commandPool_ = info.commandPool;
    terrainSize_ = info.terrainSize;
    heightScale_ = info.heightScale;
    tileResolution_ = info.tileResolution;
    tilesX_ = info.tilesX;
    tilesZ_ = info.tilesZ;
    numLODLevels_ = info.numLODLevels;
    yieldCallback_ = info.yieldCallback;

    baseLOD_ = numLODLevels_ - 1;
}

void BaseHeightMap::cleanup() {
    baseTiles_.clear();
    heightMapCpuData_.clear();

    if (heightMapView_) {
        vkDestroyImageView(device_, heightMapView_, nullptr);
        heightMapView_ = VK_NULL_HANDLE;
    }
    if (heightMapImage_) {
        vmaDestroyImage(allocator_, heightMapImage_, heightMapAllocation_);
        heightMapImage_ = VK_NULL_HANDLE;
    }
}

bool BaseHeightMap::loadBaseLODTiles(const LoadTileFunc& loadTileFunc) {
    uint32_t baseTilesX = tilesX_ >> baseLOD_;
    uint32_t baseTilesZ = tilesZ_ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    SDL_Log("BaseHeightMap: Loading %ux%u base LOD tiles (LOD%u)...",
            baseTilesX, baseTilesZ, baseLOD_);

    baseTiles_.clear();
    baseTiles_.reserve(baseTilesX * baseTilesZ);

    uint32_t tilesLoaded = 0;
    uint32_t tilesFailed = 0;
    uint32_t totalTiles = baseTilesX * baseTilesZ;

    for (uint32_t tz = 0; tz < baseTilesZ; tz++) {
        for (uint32_t tx = 0; tx < baseTilesX; tx++) {
            TerrainTile* tile = loadTileFunc(static_cast<int32_t>(tx), static_cast<int32_t>(tz), baseLOD_);
            if (tile) {
                baseTiles_.push_back(tile);
                tilesLoaded++;
            } else {
                tilesFailed++;
            }

            if (yieldCallback_) {
                float progress = static_cast<float>(tilesLoaded + tilesFailed) / totalTiles * 0.5f;
                yieldCallback_(progress, "Loading terrain tiles");
            }
        }
    }

    SDL_Log("BaseHeightMap: Loaded %u/%u base LOD tiles (%u failed)",
            tilesLoaded, totalTiles, tilesFailed);

    if (tilesLoaded == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BaseHeightMap: Failed to load any base LOD tiles");
        return false;
    }

    if (!createCombinedHeightMap()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "BaseHeightMap: Failed to create combined base heightmap");
        // Not fatal - CPU queries will still work via sampleHeight
    }

    return true;
}

bool BaseHeightMap::createCombinedHeightMap() {
    uint32_t baseTilesX = tilesX_ >> baseLOD_;
    uint32_t baseTilesZ = tilesZ_ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    uint32_t nativeRes = std::max(baseTilesX, baseTilesZ) * tileResolution_;
    heightMapResolution_ = std::min(nativeRes, 1024u);

    heightMapCpuData_.resize(heightMapResolution_ * heightMapResolution_);

    float invTerrainSize = 1.0f / terrainSize_;
    constexpr uint32_t YIELD_INTERVAL = 32;

    for (uint32_t y = 0; y < heightMapResolution_; y++) {
        for (uint32_t x = 0; x < heightMapResolution_; x++) {
            float worldX = (static_cast<float>(x) / (heightMapResolution_ - 1) - 0.5f) * terrainSize_;
            float worldZ = (static_cast<float>(y) / (heightMapResolution_ - 1) - 0.5f) * terrainSize_;

            float normalizedX = (worldX * invTerrainSize) + 0.5f;
            float normalizedZ = (worldZ * invTerrainSize) + 0.5f;
            int tileIdxX = std::clamp(static_cast<int>(normalizedX * baseTilesX), 0, static_cast<int>(baseTilesX) - 1);
            int tileIdxZ = std::clamp(static_cast<int>(normalizedZ * baseTilesZ), 0, static_cast<int>(baseTilesZ) - 1);

            size_t tileIdx = static_cast<size_t>(tileIdxZ * baseTilesX + tileIdxX);
            float height = 0.0f;

            if (tileIdx < baseTiles_.size()) {
                const TerrainTile* tile = baseTiles_[tileIdx];
                if (tile && !tile->cpuData.empty()) {
                    float u = (worldX - tile->worldMinX) / (tile->worldMaxX - tile->worldMinX);
                    float v = (worldZ - tile->worldMinZ) / (tile->worldMaxZ - tile->worldMinZ);
                    uint32_t actualRes = static_cast<uint32_t>(std::sqrt(tile->cpuData.size()));
                    height = TerrainHeight::sampleBilinear(u, v, tile->cpuData.data(), actualRes);
                }
            }

            heightMapCpuData_[y * heightMapResolution_ + x] = height;
        }

        if (yieldCallback_ && (y % YIELD_INTERVAL) == 0) {
            float progress = 0.5f + (static_cast<float>(y) / heightMapResolution_) * 0.4f;
            yieldCallback_(progress, "Building terrain heightmap");
        }
    }

    // Create GPU image
    {
        ManagedImage image;
        if (!ImageBuilder(allocator_)
                .setExtent(heightMapResolution_, heightMapResolution_)
                .setFormat(VK_FORMAT_R32_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build(device_, image, heightMapView_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "BaseHeightMap: Failed to create heightmap image");
            return false;
        }
        image.releaseToRaw(heightMapImage_, heightMapAllocation_);
    }

    // Upload to GPU
    VkDeviceSize imageSize = heightMapResolution_ * heightMapResolution_ * sizeof(float);

    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator_, imageSize, stagingBuffer)) {
        return false;
    }

    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, heightMapCpuData_.data(), imageSize);
    stagingBuffer.unmap();

    CommandScope cmd(device_, commandPool_, graphicsQueue_);
    if (!cmd.begin()) return false;

    vk::CommandBuffer vkCmd(cmd.get());

    // Transition to transfer dst
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(heightMapImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::PipelineStageFlagBits::eTransfer,
                              {}, {}, {}, barrier);
    }

    // Copy buffer to image
    {
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
            .setImageExtent({heightMapResolution_, heightMapResolution_, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), heightMapImage_, vk::ImageLayout::eTransferDstOptimal, region);
    }

    // Transition to shader read
    {
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(heightMapImage_)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                              vk::PipelineStageFlagBits::eFragmentShader,
                              {}, {}, {}, barrier);
    }

    if (!cmd.end()) return false;

    SDL_Log("BaseHeightMap: Created base heightmap (%ux%u) from %zu base tiles",
            heightMapResolution_, heightMapResolution_, baseTiles_.size());

    return true;
}

bool BaseHeightMap::sampleHeight(float worldX, float worldZ, float& outHeight) const {
    if (baseTiles_.empty()) return false;

    uint32_t baseTilesX = tilesX_ >> baseLOD_;
    uint32_t baseTilesZ = tilesZ_ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    float invTerrainSize = 1.0f / terrainSize_;
    float normalizedX = (worldX * invTerrainSize) + 0.5f;
    float normalizedZ = (worldZ * invTerrainSize) + 0.5f;
    int tileIdxX = std::clamp(static_cast<int>(normalizedX * baseTilesX), 0, static_cast<int>(baseTilesX) - 1);
    int tileIdxZ = std::clamp(static_cast<int>(normalizedZ * baseTilesZ), 0, static_cast<int>(baseTilesZ) - 1);

    size_t tileIdx = static_cast<size_t>(tileIdxZ * baseTilesX + tileIdxX);
    if (tileIdx >= baseTiles_.size()) return false;

    const TerrainTile* tile = baseTiles_[tileIdx];
    if (!tile || tile->cpuData.empty()) return false;

    float u = (worldX - tile->worldMinX) / (tile->worldMaxX - tile->worldMinX);
    float v = (worldZ - tile->worldMinZ) / (tile->worldMaxZ - tile->worldMinZ);
    uint32_t actualRes = static_cast<uint32_t>(std::sqrt(tile->cpuData.size()));

    outHeight = TerrainHeight::sampleWorldHeight(u, v, tile->cpuData.data(),
                                                  actualRes, heightScale_);

    static int baseLodDebugCount = 0;
    if (baseLodDebugCount < 5) {
        SDL_Log("getHeightAt(%.1f, %.1f): baseLOD LOD%u tile(%d,%d) uv(%.4f,%.4f) res=%u h=%.2f",
                worldX, worldZ, tile->lod, tile->coord.x, tile->coord.z,
                u, v, actualRes, outHeight);
        baseLodDebugCount++;
    }
    return true;
}

const TerrainTile* BaseHeightMap::getTileAt(float worldX, float worldZ) const {
    if (baseTiles_.empty()) return nullptr;

    uint32_t baseTilesX = tilesX_ >> baseLOD_;
    uint32_t baseTilesZ = tilesZ_ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    float invTerrainSize = 1.0f / terrainSize_;
    float normX = (worldX * invTerrainSize) + 0.5f;
    float normZ = (worldZ * invTerrainSize) + 0.5f;
    normX = std::clamp(normX, 0.0f, 0.9999f);
    normZ = std::clamp(normZ, 0.0f, 0.9999f);

    uint32_t tx = static_cast<uint32_t>(normX * baseTilesX);
    uint32_t tz = static_cast<uint32_t>(normZ * baseTilesZ);
    uint32_t tileIndex = tz * baseTilesX + tx;

    if (tileIndex < baseTiles_.size()) {
        return baseTiles_[tileIndex];
    }
    return nullptr;
}
