#include "TerrainTileCache.h"
#include "TerrainHeight.h"
#include "CommandBufferUtils.h"
#include "VmaResources.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <stb_image.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>

std::unique_ptr<TerrainTileCache> TerrainTileCache::create(const InitInfo& info) {
    auto cache = std::make_unique<TerrainTileCache>(ConstructToken{});
    if (!cache->initInternal(info)) {
        return nullptr;
    }
    return cache;
}

TerrainTileCache::~TerrainTileCache() {
    cleanup();
}

bool TerrainTileCache::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: raiiDevice is null");
        return false;
    }
    raiiDevice_ = info.raiiDevice;
    cacheDirectory = info.cacheDirectory;
    device = info.device;
    allocator = info.allocator;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    terrainSize = info.terrainSize;
    heightScale = info.heightScale;
    minAltitude = info.minAltitude;
    maxAltitude = info.maxAltitude;

    // Load metadata from cache
    if (!loadMetadata()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to load metadata from %s",
                     cacheDirectory.c_str());
        return false;
    }

    // Create sampler for tile textures using factory
    auto sampler = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create sampler");
        return false;
    }
    sampler_ = std::move(*sampler);

    // Create tile info buffers for shader using VulkanResourceFactory (triple-buffered)
    // Layout: uint activeTileCount, uint padding[3], TileInfoGPU tiles[MAX_ACTIVE_TILES]
    VkDeviceSize bufferSize = sizeof(uint32_t) * 4 + MAX_ACTIVE_TILES * sizeof(TileInfoGPU);
    tileInfoBuffers_.resize(FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        if (!VmaBufferFactory::createStorageBufferHostReadable(allocator, bufferSize, tileInfoBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile info buffer %u", i);
            return false;
        }
        tileInfoMappedPtrs_[i] = tileInfoBuffers_[i].map();
    }

    // Create tile array image (2D array texture with MAX_ACTIVE_TILES layers)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(tileResolution, tileResolution)
                .setFormat(VK_FORMAT_R32_SFLOAT)
                .setArrayLayers(MAX_ACTIVE_TILES)
                .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build(device, image, tileArrayView)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile array image");
            return false;
        }
        image.releaseToRaw(tileArrayImage, tileArrayAllocation);
    }

    // Transition tile array to shader read layout
    {
        VkCommandBuffer cmd;
        auto cmdAllocInfo = vk::CommandBufferAllocateInfo{}
            .setCommandPool(commandPool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);
        vkAllocateCommandBuffers(device, reinterpret_cast<const VkCommandBufferAllocateInfo*>(&cmdAllocInfo), &cmd);

        auto beginInfo = vk::CommandBufferBeginInfo{}
            .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        vkBeginCommandBuffer(cmd, reinterpret_cast<const VkCommandBufferBeginInfo*>(&beginInfo));

        vk::CommandBuffer vkCmd(cmd);
        auto barrier = vk::ImageMemoryBarrier{}
            .setSrcAccessMask(vk::AccessFlags{})
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(tileArrayImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(MAX_ACTIVE_TILES));
        vkCmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                              vk::PipelineStageFlagBits::eVertexShader,
                              {}, {}, {}, barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    }

    // Initialize all tile info buffers with activeTileCount = 0
    // This ensures shaders don't read garbage if they run before first updateActiveTiles()
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        currentFrameIndex_ = i;
        updateTileInfoBuffer();
    }
    currentFrameIndex_ = 0;

    // Initialize all array layers as free
    freeArrayLayers_.fill(true);

    SDL_Log("TerrainTileCache initialized: %s", cacheDirectory.c_str());
    SDL_Log("  Terrain size: %.0fm, Tile resolution: %u, LOD levels: %u",
            terrainSize, tileResolution, numLODLevels);
    SDL_Log("  LOD0 grid: %ux%u tiles", tilesX, tilesZ);

    // Load all base LOD tiles synchronously at startup
    // These tiles cover the entire terrain and provide fallback height data
    if (!loadBaseLODTiles()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Failed to load base LOD tiles");
        return false;
    }

    // Initialize hole mask (starts empty - no holes)
    holeMaskCpuData_.resize(holeMaskResolution_ * holeMaskResolution_, 0);
    if (!createHoleMaskResources()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Failed to create hole mask resources");
        return false;
    }
    if (!uploadHoleMaskToGPUInternal()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Failed to upload hole mask to GPU");
        return false;
    }

    return true;
}

void TerrainTileCache::cleanup() {
    // Wait for GPU to finish
    if (device) {
        vkDeviceWaitIdle(device);
    }

    // Clear base tiles list (pointers into loadedTiles)
    baseTiles_.clear();
    baseHeightMapCpuData_.clear();

    // Unload all tiles
    for (auto& [key, tile] : loadedTiles) {
        if (tile.imageView) vkDestroyImageView(device, tile.imageView, nullptr);
        if (tile.image) vmaDestroyImage(allocator, tile.image, tile.allocation);
    }
    loadedTiles.clear();
    activeTiles.clear();

    // Destroy tile info buffers (RAII via clear)
    tileInfoBuffers_.forEach([](uint32_t, ManagedBuffer& buffer) {
        buffer.reset();
    });
    tileInfoBuffers_.clear();
    tileInfoMappedPtrs_.fill(nullptr);

    // Destroy tile array texture
    if (tileArrayView) {
        vkDestroyImageView(device, tileArrayView, nullptr);
        tileArrayView = VK_NULL_HANDLE;
    }
    if (tileArrayImage) {
        vmaDestroyImage(allocator, tileArrayImage, tileArrayAllocation);
        tileArrayImage = VK_NULL_HANDLE;
    }

    // Destroy base heightmap texture
    if (baseHeightMapView_) {
        vkDestroyImageView(device, baseHeightMapView_, nullptr);
        baseHeightMapView_ = VK_NULL_HANDLE;
    }
    if (baseHeightMapImage_) {
        vmaDestroyImage(allocator, baseHeightMapImage_, baseHeightMapAllocation_);
        baseHeightMapImage_ = VK_NULL_HANDLE;
    }

    // Destroy hole mask resources
    holeMaskSampler_.reset();
    if (holeMaskImageView_) {
        vkDestroyImageView(device, holeMaskImageView_, nullptr);
        holeMaskImageView_ = VK_NULL_HANDLE;
    }
    if (holeMaskImage_) {
        vmaDestroyImage(allocator, holeMaskImage_, holeMaskAllocation_);
        holeMaskImage_ = VK_NULL_HANDLE;
    }
    holeMaskCpuData_.clear();
    holes_.clear();

    // Destroy sampler (RAII via reset)
    sampler_.reset();
}

bool TerrainTileCache::loadMetadata() {
    std::string metaPath = cacheDirectory + "/terrain_tiles.meta";
    std::ifstream file(metaPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Cannot open metadata: %s",
                     metaPath.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            std::getline(iss, value);

            if (key == "tileResolution") tileResolution = std::stoul(value);
            else if (key == "numLODLevels") numLODLevels = std::stoul(value);
            else if (key == "tilesX") tilesX = std::stoul(value);
            else if (key == "tilesZ") tilesZ = std::stoul(value);
            else if (key == "sourceWidth") sourceWidth = std::stoul(value);
            else if (key == "sourceHeight") sourceHeight = std::stoul(value);
            else if (key == "minAltitude") minAltitude = std::stof(value);
            else if (key == "maxAltitude") maxAltitude = std::stof(value);
        }
    }

    // Recalculate height scale from altitude range
    heightScale = maxAltitude - minAltitude;

    return true;
}

std::string TerrainTileCache::getTilePath(TileCoord coord, uint32_t lod) const {
    std::ostringstream oss;
    oss << cacheDirectory << "/tile_" << coord.x << "_" << coord.z << "_lod" << lod << ".png";
    return oss.str();
}

uint64_t TerrainTileCache::makeTileKey(TileCoord coord, uint32_t lod) const {
    // Pack coord and LOD into a single 64-bit key
    return (static_cast<uint64_t>(lod) << 48) |
           (static_cast<uint64_t>(static_cast<uint32_t>(coord.x)) << 24) |
           static_cast<uint64_t>(static_cast<uint32_t>(coord.z));
}

void TerrainTileCache::calculateTileWorldBounds(TileCoord coord, uint32_t lod, TerrainTile& tile) const {
    uint32_t lodTilesX = tilesX >> lod;
    uint32_t lodTilesZ = tilesZ >> lod;
    if (lodTilesX < 1) lodTilesX = 1;
    if (lodTilesZ < 1) lodTilesZ = 1;

    float tileWorldSizeX = terrainSize / lodTilesX;
    float tileWorldSizeZ = terrainSize / lodTilesZ;

    tile.worldMinX = (static_cast<float>(coord.x) / lodTilesX - 0.5f) * terrainSize;
    tile.worldMinZ = (static_cast<float>(coord.z) / lodTilesZ - 0.5f) * terrainSize;
    tile.worldMaxX = tile.worldMinX + tileWorldSizeX;
    tile.worldMaxZ = tile.worldMinZ + tileWorldSizeZ;
}

bool TerrainTileCache::loadTileDataFromDisk(TileCoord coord, uint32_t lod, TerrainTile& tile) {
    std::string path = getTilePath(coord, lod);

    int width, height, channels;
    uint16_t* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to load tile: %s",
                    path.c_str());
        return false;
    }

    // Tiles must match expected resolution
    if (width != static_cast<int>(tileResolution) || height != static_cast<int>(tileResolution)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Tile %s is %dx%d, expected %ux%u - refusing to resample",
                     path.c_str(), width, height, tileResolution, tileResolution);
        stbi_image_free(data);
        return false;
    }

    // Initialize tile metadata
    tile.coord = coord;
    tile.lod = lod;
    calculateTileWorldBounds(coord, lod, tile);

    // Convert 16-bit to normalized float32
    tile.cpuData.resize(tileResolution * tileResolution);
    for (uint32_t i = 0; i < tileResolution * tileResolution; i++) {
        tile.cpuData[i] = static_cast<float>(data[i]) / 65535.0f;
    }

    stbi_image_free(data);
    return true;
}

uint32_t TerrainTileCache::getLODForDistance(float distance) const {
    if (distance < LOD0_MAX_DISTANCE) return 0;
    if (distance < LOD1_MAX_DISTANCE && numLODLevels > 1) return 1;
    if (distance < LOD2_MAX_DISTANCE && numLODLevels > 2) return 2;
    if (distance < LOD3_MAX_DISTANCE && numLODLevels > 3) return 3;
    return numLODLevels; // Beyond all LOD levels - use global fallback
}

TileCoord TerrainTileCache::worldToTileCoord(float worldX, float worldZ, uint32_t lod) const {
    // Convert world position to normalized [0, 1]
    float normX = (worldX / terrainSize) + 0.5f;
    float normZ = (worldZ / terrainSize) + 0.5f;

    // Clamp to valid range
    normX = std::clamp(normX, 0.0f, 0.9999f);
    normZ = std::clamp(normZ, 0.0f, 0.9999f);

    // Calculate tile count at this LOD level
    uint32_t lodTilesX = tilesX >> lod;
    uint32_t lodTilesZ = tilesZ >> lod;
    if (lodTilesX < 1) lodTilesX = 1;
    if (lodTilesZ < 1) lodTilesZ = 1;

    TileCoord coord;
    coord.x = static_cast<int32_t>(normX * lodTilesX);
    coord.z = static_cast<int32_t>(normZ * lodTilesZ);

    return coord;
}

void TerrainTileCache::updateActiveTiles(const glm::vec3& cameraPos, float loadRadius, float unloadRadius) {
    // Determine which tiles should be loaded based on camera position
    std::vector<std::pair<TileCoord, uint32_t>> tilesToLoad;
    std::vector<uint64_t> tilesToUnload;

    // Calculate camera position in terrain space
    float camX = cameraPos.x;
    float camZ = cameraPos.z;

    // For each LOD level, determine which tiles should be loaded
    for (uint32_t lod = 0; lod < numLODLevels; lod++) {
        float lodMinDist = (lod == 0) ? 0.0f :
                          (lod == 1) ? LOD0_MAX_DISTANCE :
                          (lod == 2) ? LOD1_MAX_DISTANCE :
                          LOD2_MAX_DISTANCE;
        float lodMaxDist = (lod == 0) ? LOD0_MAX_DISTANCE :
                          (lod == 1) ? LOD1_MAX_DISTANCE :
                          (lod == 2) ? LOD2_MAX_DISTANCE :
                          LOD3_MAX_DISTANCE;

        // Skip this LOD if camera is outside its range
        // (we still need tiles for areas within our loadRadius though)

        // Calculate tile size at this LOD
        uint32_t lodTilesX = tilesX >> lod;
        uint32_t lodTilesZ = tilesZ >> lod;
        if (lodTilesX < 1) lodTilesX = 1;
        if (lodTilesZ < 1) lodTilesZ = 1;

        float tileWorldSizeX = terrainSize / lodTilesX;
        float tileWorldSizeZ = terrainSize / lodTilesZ;

        // Calculate tile range to check around camera
        int32_t minTileX = static_cast<int32_t>(((camX - loadRadius) / terrainSize + 0.5f) * lodTilesX);
        int32_t maxTileX = static_cast<int32_t>(((camX + loadRadius) / terrainSize + 0.5f) * lodTilesX);
        int32_t minTileZ = static_cast<int32_t>(((camZ - loadRadius) / terrainSize + 0.5f) * lodTilesZ);
        int32_t maxTileZ = static_cast<int32_t>(((camZ + loadRadius) / terrainSize + 0.5f) * lodTilesZ);

        // Clamp to valid tile range
        minTileX = std::max(0, minTileX);
        maxTileX = std::min(static_cast<int32_t>(lodTilesX - 1), maxTileX);
        minTileZ = std::max(0, minTileZ);
        maxTileZ = std::min(static_cast<int32_t>(lodTilesZ - 1), maxTileZ);

        for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
            for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
                // Calculate tile center in world space
                float tileCenterX = ((static_cast<float>(tx) + 0.5f) / lodTilesX - 0.5f) * terrainSize;
                float tileCenterZ = ((static_cast<float>(tz) + 0.5f) / lodTilesZ - 0.5f) * terrainSize;

                float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                                       (tileCenterZ - camZ) * (tileCenterZ - camZ));

                // Load tile if any part of it is within this LOD's max distance
                // and within the overall load radius.
                // Each LOD level covers from 0 to lodMaxDist, with finer LODs
                // preferred when available (handled by shader/sampling).
                // This ensures no gaps at LOD boundaries.
                if (dist < lodMaxDist && dist < loadRadius) {
                    TileCoord coord{tx, tz};
                    if (!isTileLoaded(coord, lod)) {
                        tilesToLoad.push_back({coord, lod});
                    }
                }
            }
        }
    }

    // Find tiles to unload (beyond their LOD's useful range)
    // Each LOD has its own max distance - unload when beyond that + hysteresis
    // Never unload base LOD tiles - they're the fallback for the entire terrain
    for (auto& [key, tile] : loadedTiles) {
        // Skip base LOD tiles - they're never unloaded
        if (tile.lod == baseLOD_) continue;

        float tileCenterX = (tile.worldMinX + tile.worldMaxX) * 0.5f;
        float tileCenterZ = (tile.worldMinZ + tile.worldMaxZ) * 0.5f;

        float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                               (tileCenterZ - camZ) * (tileCenterZ - camZ));

        // Get the max distance for this tile's LOD level
        float lodMaxDist = (tile.lod == 0) ? LOD0_MAX_DISTANCE :
                          (tile.lod == 1) ? LOD1_MAX_DISTANCE :
                          (tile.lod == 2) ? LOD2_MAX_DISTANCE :
                          LOD3_MAX_DISTANCE;

        // Unload if beyond this LOD's range (with hysteresis to prevent thrashing)
        // The coarser LOD tiles will still provide coverage
        float unloadDist = lodMaxDist + (unloadRadius - loadRadius);
        if (dist > unloadDist) {
            tilesToUnload.push_back(key);
        }
    }

    // Unload distant tiles
    for (uint64_t key : tilesToUnload) {
        auto it = loadedTiles.find(key);
        if (it != loadedTiles.end()) {
            TerrainTile& tile = it->second;
            // Free the array layer used by this tile
            if (tile.arrayLayerIndex >= 0) {
                freeArrayLayer(tile.arrayLayerIndex);
            }
            if (tile.imageView) vkDestroyImageView(device, tile.imageView, nullptr);
            if (tile.image) vmaDestroyImage(allocator, tile.image, tile.allocation);
            loadedTiles.erase(it);
        }
    }

    // Load new tiles (limit per frame to avoid stalls)
    constexpr uint32_t MAX_TILES_PER_FRAME = 4;
    uint32_t tilesLoadedThisFrame = 0;

    for (const auto& [coord, lod] : tilesToLoad) {
        if (tilesLoadedThisFrame >= MAX_TILES_PER_FRAME) break;
        if (loadedTiles.size() >= MAX_ACTIVE_TILES) break;

        if (loadTile(coord, lod)) {
            tilesLoadedThisFrame++;
        }
    }

    // Update active tiles list
    activeTiles.clear();
    for (auto& [key, tile] : loadedTiles) {
        if (tile.loaded) {
            activeTiles.push_back(&tile);
        }
    }

    // Update tile info buffer
    updateTileInfoBuffer();
}

bool TerrainTileCache::loadTile(TileCoord coord, uint32_t lod) {
    uint64_t key = makeTileKey(coord, lod);

    // Check if tile already has GPU resources (fully loaded)
    auto existingIt = loadedTiles.find(key);
    if (existingIt != loadedTiles.end() && existingIt->second.loaded) {
        return true;  // Already fully loaded
    }

    // Check if we already have CPU data from loadTileCPUOnly
    bool hasCpuData = (existingIt != loadedTiles.end() && !existingIt->second.cpuData.empty());

    TerrainTile* tilePtr = nullptr;

    if (hasCpuData) {
        // Already have CPU data, just need to add GPU resources
        tilePtr = &existingIt->second;
    } else {
        // Need to load from disk - create entry and populate with helper
        TerrainTile& tile = loadedTiles[key];
        if (!loadTileDataFromDisk(coord, lod, tile)) {
            loadedTiles.erase(key);
            return false;
        }
        tilePtr = &tile;
    }

    TerrainTile& tile = *tilePtr;

    // Create GPU resources and upload
    if (!createTileGPUResources(tile)) {
        loadedTiles.erase(key);
        return false;
    }

    if (!uploadTileToGPU(tile)) {
        if (tile.imageView) vkDestroyImageView(device, tile.imageView, nullptr);
        if (tile.image) vmaDestroyImage(allocator, tile.image, tile.allocation);
        loadedTiles.erase(key);
        return false;
    }

    // Allocate a layer in the tile array and copy data to it (one-time upload)
    int32_t layerIndex = allocateArrayLayer();
    if (layerIndex >= 0) {
        tile.arrayLayerIndex = layerIndex;
        copyTileToArrayLayer(&tile, static_cast<uint32_t>(layerIndex));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: No free array layers for tile (%d, %d) LOD%u",
                    coord.x, coord.z, lod);
    }

    tile.loaded = true;

    SDL_Log("TerrainTileCache: Loaded tile (%d, %d) LOD%u layer %d - world bounds [%.0f,%.0f]-[%.0f,%.0f]%s",
            coord.x, coord.z, lod, tile.arrayLayerIndex, tile.worldMinX, tile.worldMinZ, tile.worldMaxX, tile.worldMaxZ,
            hasCpuData ? " (added GPU to existing)" : "");

    return true;
}

bool TerrainTileCache::createTileGPUResources(TerrainTile& tile) {
    ManagedImage image;
    if (!ImageBuilder(allocator)
            .setExtent(tileResolution, tileResolution)
            .setFormat(VK_FORMAT_R32_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build(device, image, tile.imageView)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile image");
        return false;
    }
    image.releaseToRaw(tile.image, tile.allocation);
    return true;
}

bool TerrainTileCache::uploadTileToGPU(TerrainTile& tile) {
    VkDeviceSize imageSize = tileResolution * tileResolution * sizeof(float);

    // Create staging buffer using RAII wrapper
    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        return false;
    }

    // Copy data to staging buffer
    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, tile.cpuData.data(), imageSize);
    stagingBuffer.unmap();

    // Use CommandScope for one-time command submission
    CommandScope cmd(device, commandPool, graphicsQueue);
    if (!cmd.begin()) return false;

    // Copy staging buffer to tile image with explicit barriers
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
            .setImage(tile.image)
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
            .setImageExtent({tileResolution, tileResolution, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), tile.image, vk::ImageLayout::eTransferDstOptimal, region);
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
            .setImage(tile.image)
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

    // ManagedBuffer automatically destroyed on scope exit
    return true;
}

void TerrainTileCache::updateTileInfoBuffer() {
    // Write to the current frame's buffer (triple-buffered to avoid CPU-GPU sync issues)
    void* mappedPtr = tileInfoMappedPtrs_[currentFrameIndex_ % FRAMES_IN_FLIGHT];
    if (!mappedPtr) return;

    // First 4 bytes: active tile count
    uint32_t* countPtr = static_cast<uint32_t*>(mappedPtr);
    countPtr[0] = static_cast<uint32_t>(activeTiles.size());
    countPtr[1] = 0; // padding1
    countPtr[2] = 0; // padding2
    countPtr[3] = 0; // padding3

    if (activeTiles.empty()) return;

    // Tile info array follows after the count (offset by 16 bytes for alignment)
    TileInfoGPU* tileInfoArray = reinterpret_cast<TileInfoGPU*>(countPtr + 4);

    for (size_t i = 0; i < activeTiles.size() && i < MAX_ACTIVE_TILES; i++) {
        TerrainTile* tile = activeTiles[i];

        tileInfoArray[i].worldBounds = glm::vec4(
            tile->worldMinX, tile->worldMinZ,
            tile->worldMaxX, tile->worldMaxZ
        );

        // UV scale and offset for sampling this tile
        // UV = (worldPos - worldMin) / (worldMax - worldMin)
        float sizeX = tile->worldMaxX - tile->worldMinX;
        float sizeZ = tile->worldMaxZ - tile->worldMinZ;
        tileInfoArray[i].uvScaleOffset = glm::vec4(
            1.0f / sizeX, 1.0f / sizeZ,
            -tile->worldMinX / sizeX, -tile->worldMinZ / sizeZ
        );

        // Store the array layer index where this tile's data is stored
        // The tile data was copied to the array when it was first loaded (in loadTile)
        // so we don't need to re-copy it every frame - just tell the shader which layer to sample
        tileInfoArray[i].layerIndex = glm::ivec4(tile->arrayLayerIndex, 0, 0, 0);
    }
}

bool TerrainTileCache::isTileLoaded(TileCoord coord, uint32_t lod) const {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    // A tile is fully loaded only if it has GPU resources (tile.loaded = true)
    return it != loadedTiles.end() && it->second.loaded;
}

bool TerrainTileCache::getHeightAt(float worldX, float worldZ, float& outHeight) const {
    // Helper to sample height from a tile using TerrainHeight bilinear sampling
    auto sampleTile = [&](const TerrainTile& tile) -> bool {
        if (tile.cpuData.empty()) return false;
        if (worldX < tile.worldMinX || worldX >= tile.worldMaxX ||
            worldZ < tile.worldMinZ || worldZ >= tile.worldMaxZ) return false;

        // Calculate UV within tile
        float u = (worldX - tile.worldMinX) / (tile.worldMaxX - tile.worldMinX);
        float v = (worldZ - tile.worldMinZ) / (tile.worldMaxZ - tile.worldMinZ);

        // Sample and convert to world height using TerrainHeight helpers
        outHeight = TerrainHeight::sampleWorldHeight(u, v, tile.cpuData.data(),
                                                      tileResolution, heightScale);
        return true;
    };

    // First check active tiles (GPU tiles - highest priority)
    for (const TerrainTile* tile : activeTiles) {
        if (sampleTile(*tile)) return true;
    }

    // Also check all loaded tiles (includes CPU-only tiles from physics preloading)
    // This ensures physics and CPU queries use the same high-res tile data
    for (const auto& [key, tile] : loadedTiles) {
        // Skip base LOD tiles here - we'll check them as fallback
        if (tile.lod == baseLOD_) continue;
        if (sampleTile(tile)) return true;
    }

    // Fallback to base LOD tiles (always loaded, covers entire terrain)
    return sampleBaseLOD(worldX, worldZ, outHeight);
}

void TerrainTileCache::copyTileToArrayLayer(TerrainTile* tile, uint32_t layerIndex) {
    if (!tile || tile->cpuData.empty() || layerIndex >= MAX_ACTIVE_TILES) return;

    // Create staging buffer using RAII wrapper
    VkDeviceSize imageSize = tileResolution * tileResolution * sizeof(float);

    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create staging buffer for tile copy");
        return;
    }

    // Copy tile data to staging buffer
    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, tile->cpuData.data(), imageSize);
    stagingBuffer.unmap();

    // Use CommandScope for one-time command submission
    CommandScope cmd(device, commandPool, graphicsQueue);
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
            .setImage(tileArrayImage)
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
            .setImageExtent({tileResolution, tileResolution, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), tileArrayImage, vk::ImageLayout::eTransferDstOptimal, region);
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
            .setImage(tileArrayImage)
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
    // ManagedBuffer automatically destroyed on scope exit
}

const TerrainTile* TerrainTileCache::getLoadedTile(TileCoord coord, uint32_t lod) const {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    // Return tile if fully loaded OR if CPU data is available (for physics)
    if (it != loadedTiles.end() && (it->second.loaded || !it->second.cpuData.empty())) {
        return &it->second;
    }
    return nullptr;
}

bool TerrainTileCache::requestTileLoad(TileCoord coord, uint32_t lod) {
    // Check if already loaded
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    if (it != loadedTiles.end() && it->second.loaded) {
        return true;
    }

    // Also accept CPU-only loaded tiles (no GPU resources yet)
    if (it != loadedTiles.end() && !it->second.cpuData.empty()) {
        return true;
    }

    // Load the tile
    return loadTile(coord, lod);
}

bool TerrainTileCache::loadTileCPUOnly(TileCoord coord, uint32_t lod) {
    // Check if already loaded (either fully or CPU-only)
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    if (it != loadedTiles.end() && !it->second.cpuData.empty()) {
        return true;  // Already has CPU data
    }

    // Create tile entry and load from disk using helper
    TerrainTile& tile = loadedTiles[key];
    if (!loadTileDataFromDisk(coord, lod, tile)) {
        loadedTiles.erase(key);
        return false;
    }

    // Leave loaded=false - GPU resources will be created later when needed
    tile.loaded = false;

    SDL_Log("TerrainTileCache: Loaded tile CPU data (%d, %d) LOD%u - world bounds [%.0f,%.0f]-[%.0f,%.0f]",
            coord.x, coord.z, lod, tile.worldMinX, tile.worldMinZ, tile.worldMaxX, tile.worldMaxZ);

    return true;
}

void TerrainTileCache::preloadTilesAround(float worldX, float worldZ, float radius) {
    // Pre-load LOD0 tiles (highest resolution) around the given position
    // This is synchronous and blocks until tiles are loaded
    const uint32_t lod = 0;
    const float tileWorldSizeX = terrainSize / tilesX;
    const float tileWorldSizeZ = terrainSize / tilesZ;

    // Calculate tile range covering the radius
    int32_t minTileX = static_cast<int32_t>(((worldX - radius) / terrainSize + 0.5f) * tilesX);
    int32_t maxTileX = static_cast<int32_t>(((worldX + radius) / terrainSize + 0.5f) * tilesX);
    int32_t minTileZ = static_cast<int32_t>(((worldZ - radius) / terrainSize + 0.5f) * tilesZ);
    int32_t maxTileZ = static_cast<int32_t>(((worldZ + radius) / terrainSize + 0.5f) * tilesZ);

    // Clamp to valid range
    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int32_t>(tilesX - 1), maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int32_t>(tilesZ - 1), maxTileZ);

    uint32_t tilesLoaded = 0;
    for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
        for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
            // Calculate tile center for distance check
            float tileCenterX = ((static_cast<float>(tx) + 0.5f) / tilesX - 0.5f) * terrainSize;
            float tileCenterZ = ((static_cast<float>(tz) + 0.5f) / tilesZ - 0.5f) * terrainSize;

            float dx = worldX - tileCenterX;
            float dz = worldZ - tileCenterZ;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < radius) {
                TileCoord coord{tx, tz};
                if (loadTileCPUOnly(coord, lod)) {
                    tilesLoaded++;
                }
            }
        }
    }

    SDL_Log("TerrainTileCache: Pre-loaded %u LOD0 tiles around (%.0f, %.0f) radius %.0f",
            tilesLoaded, worldX, worldZ, radius);
}

int32_t TerrainTileCache::allocateArrayLayer() {
    for (uint32_t i = 0; i < MAX_ACTIVE_TILES; i++) {
        if (freeArrayLayers_[i]) {
            freeArrayLayers_[i] = false;
            return static_cast<int32_t>(i);
        }
    }
    return -1;  // No free layers
}

void TerrainTileCache::freeArrayLayer(int32_t layerIndex) {
    if (layerIndex >= 0 && layerIndex < static_cast<int32_t>(MAX_ACTIVE_TILES)) {
        freeArrayLayers_[layerIndex] = true;
    }
}

bool TerrainTileCache::loadBaseLODTiles() {
    // Load all tiles at the coarsest LOD level (LOD3 typically)
    // These tiles cover the entire terrain and are never unloaded
    baseLOD_ = numLODLevels - 1;  // Use highest LOD number (coarsest resolution)

    // Calculate number of tiles at base LOD level
    uint32_t baseTilesX = tilesX >> baseLOD_;
    uint32_t baseTilesZ = tilesZ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    SDL_Log("TerrainTileCache: Loading %ux%u base LOD tiles (LOD%u) synchronously...",
            baseTilesX, baseTilesZ, baseLOD_);

    baseTiles_.clear();
    baseTiles_.reserve(baseTilesX * baseTilesZ);

    uint32_t tilesLoaded = 0;
    uint32_t tilesFailed = 0;

    for (uint32_t tz = 0; tz < baseTilesZ; tz++) {
        for (uint32_t tx = 0; tx < baseTilesX; tx++) {
            TileCoord coord{static_cast<int32_t>(tx), static_cast<int32_t>(tz)};

            // Load with CPU data only first (no GPU resources yet)
            if (loadTileCPUOnly(coord, baseLOD_)) {
                uint64_t key = makeTileKey(coord, baseLOD_);
                auto it = loadedTiles.find(key);
                if (it != loadedTiles.end()) {
                    baseTiles_.push_back(&it->second);
                    tilesLoaded++;
                }
            } else {
                tilesFailed++;
            }
        }
    }

    SDL_Log("TerrainTileCache: Loaded %u/%u base LOD tiles (%u failed)",
            tilesLoaded, baseTilesX * baseTilesZ, tilesFailed);

    if (tilesLoaded == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Failed to load any base LOD tiles");
        return false;
    }

    // Create combined base heightmap texture from base tiles
    if (!createBaseHeightMap()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TerrainTileCache: Failed to create combined base heightmap");
        // Not fatal - CPU queries will still work via sampleBaseLOD
    }

    return true;
}

bool TerrainTileCache::createBaseHeightMap() {
    // Create a combined heightmap texture from all base LOD tiles
    // This provides a single GPU texture for rendering fallback

    uint32_t baseTilesX = tilesX >> baseLOD_;
    uint32_t baseTilesZ = tilesZ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    // Calculate base heightmap resolution
    // With 4x4 tiles at 512px each, native would be 2048x2048
    // Use 1024x1024 as a good balance between quality and memory
    // This preserves detail while using less VRAM than full native resolution
    uint32_t nativeRes = std::max(baseTilesX, baseTilesZ) * tileResolution;
    baseHeightMapResolution_ = std::min(nativeRes, 1024u);

    // Create CPU data by sampling from base tiles
    // Optimized: compute tile index directly instead of linear search (O(n²) vs O(n²·m))
    baseHeightMapCpuData_.resize(baseHeightMapResolution_ * baseHeightMapResolution_);

    // Precompute inverse for faster division
    float invTerrainSize = 1.0f / terrainSize;

    for (uint32_t y = 0; y < baseHeightMapResolution_; y++) {
        for (uint32_t x = 0; x < baseHeightMapResolution_; x++) {
            // Map pixel to world coordinates
            float worldX = (static_cast<float>(x) / (baseHeightMapResolution_ - 1) - 0.5f) * terrainSize;
            float worldZ = (static_cast<float>(y) / (baseHeightMapResolution_ - 1) - 0.5f) * terrainSize;

            // Compute tile index directly from world position (tiles stored in row-major order)
            float normalizedX = (worldX * invTerrainSize) + 0.5f;
            float normalizedZ = (worldZ * invTerrainSize) + 0.5f;
            int tileIdxX = std::clamp(static_cast<int>(normalizedX * baseTilesX), 0, static_cast<int>(baseTilesX) - 1);
            int tileIdxZ = std::clamp(static_cast<int>(normalizedZ * baseTilesZ), 0, static_cast<int>(baseTilesZ) - 1);

            size_t tileIdx = static_cast<size_t>(tileIdxZ * baseTilesX + tileIdxX);
            float height = 0.0f;

            if (tileIdx < baseTiles_.size()) {
                const TerrainTile* tile = baseTiles_[tileIdx];
                if (tile && !tile->cpuData.empty()) {
                    // Calculate UV within tile and sample using helper
                    float u = (worldX - tile->worldMinX) / (tile->worldMaxX - tile->worldMinX);
                    float v = (worldZ - tile->worldMinZ) / (tile->worldMaxZ - tile->worldMinZ);
                    height = TerrainHeight::sampleBilinear(u, v, tile->cpuData.data(), tileResolution);
                }
            }

            baseHeightMapCpuData_[y * baseHeightMapResolution_ + x] = height;
        }
    }

    // Create GPU image
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(baseHeightMapResolution_, baseHeightMapResolution_)
                .setFormat(VK_FORMAT_R32_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build(device, image, baseHeightMapView_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create base heightmap image");
            return false;
        }
        image.releaseToRaw(baseHeightMapImage_, baseHeightMapAllocation_);
    }

    // Upload to GPU
    VkDeviceSize imageSize = baseHeightMapResolution_ * baseHeightMapResolution_ * sizeof(float);

    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        return false;
    }

    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, baseHeightMapCpuData_.data(), imageSize);
    stagingBuffer.unmap();

    CommandScope cmd(device, commandPool, graphicsQueue);
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
            .setImage(baseHeightMapImage_)
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
            .setImageExtent({baseHeightMapResolution_, baseHeightMapResolution_, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), baseHeightMapImage_, vk::ImageLayout::eTransferDstOptimal, region);
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
            .setImage(baseHeightMapImage_)
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

    SDL_Log("TerrainTileCache: Created base heightmap (%ux%u) from %zu base tiles",
            baseHeightMapResolution_, baseHeightMapResolution_, baseTiles_.size());

    return true;
}

bool TerrainTileCache::sampleBaseLOD(float worldX, float worldZ, float& outHeight) const {
    // Sample height from base LOD tiles (fallback when no high-res tile covers position)
    // Optimized: compute tile index directly instead of linear search

    if (baseTiles_.empty()) return false;

    // Compute tile grid dimensions at base LOD
    uint32_t baseTilesX = tilesX >> baseLOD_;
    uint32_t baseTilesZ = tilesZ >> baseLOD_;
    if (baseTilesX < 1) baseTilesX = 1;
    if (baseTilesZ < 1) baseTilesZ = 1;

    // Compute tile index from world position
    float invTerrainSize = 1.0f / terrainSize;
    float normalizedX = (worldX * invTerrainSize) + 0.5f;
    float normalizedZ = (worldZ * invTerrainSize) + 0.5f;
    int tileIdxX = std::clamp(static_cast<int>(normalizedX * baseTilesX), 0, static_cast<int>(baseTilesX) - 1);
    int tileIdxZ = std::clamp(static_cast<int>(normalizedZ * baseTilesZ), 0, static_cast<int>(baseTilesZ) - 1);

    size_t tileIdx = static_cast<size_t>(tileIdxZ * baseTilesX + tileIdxX);
    if (tileIdx >= baseTiles_.size()) return false;

    const TerrainTile* tile = baseTiles_[tileIdx];
    if (!tile || tile->cpuData.empty()) return false;

    // Calculate UV within tile and sample using helper
    float u = (worldX - tile->worldMinX) / (tile->worldMaxX - tile->worldMinX);
    float v = (worldZ - tile->worldMinZ) / (tile->worldMaxZ - tile->worldMinZ);

    outHeight = TerrainHeight::sampleWorldHeight(u, v, tile->cpuData.data(),
                                                  tileResolution, heightScale);
    return true;
}

std::vector<const TerrainTile*> TerrainTileCache::getAllCPUTiles() const {
    std::vector<const TerrainTile*> result;
    result.reserve(loadedTiles.size());
    for (const auto& [key, tile] : loadedTiles) {
        if (!tile.cpuData.empty()) {
            result.push_back(&tile);
        }
    }
    return result;
}

// ============================================================================
// Hole mask functionality
// ============================================================================

bool TerrainTileCache::createHoleMaskResources() {
    // Create Vulkan image for hole mask (R8_UNORM: 0=solid, 255=hole)
    {
        ManagedImage image;
        if (!ImageBuilder(allocator)
                .setExtent(holeMaskResolution_, holeMaskResolution_)
                .setFormat(VK_FORMAT_R8_UNORM)
                .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                .build(device, image, holeMaskImageView_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create hole mask image");
            return false;
        }
        image.releaseToRaw(holeMaskImage_, holeMaskAllocation_);
    }

    // Create sampler (linear filtering for smooth edges) using factory
    auto holeSampler = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!holeSampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create hole mask sampler");
        return false;
    }
    holeMaskSampler_ = std::move(*holeSampler);

    return true;
}

bool TerrainTileCache::uploadHoleMaskToGPUInternal() {
    VkDeviceSize imageSize = holeMaskResolution_ * holeMaskResolution_ * sizeof(uint8_t);

    // Create staging buffer
    ManagedBuffer stagingBuffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create staging buffer for hole mask");
        return false;
    }

    // Copy data to staging buffer
    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, holeMaskCpuData_.data(), imageSize);
    stagingBuffer.unmap();

    // Use CommandScope for one-time command submission
    CommandScope cmd(device, commandPool, graphicsQueue);
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
            .setImage(holeMaskImage_)
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
            .setImageExtent({holeMaskResolution_, holeMaskResolution_, 1});
        vkCmd.copyBufferToImage(stagingBuffer.get(), holeMaskImage_, vk::ImageLayout::eTransferDstOptimal, region);
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
            .setImage(holeMaskImage_)
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

    return true;
}

void TerrainTileCache::worldToHoleMaskTexel(float x, float z, int& texelX, int& texelY) const {
    float u = (x / terrainSize) + 0.5f;
    float v = (z / terrainSize) + 0.5f;
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    texelX = static_cast<int>(u * (holeMaskResolution_ - 1));
    texelY = static_cast<int>(v * (holeMaskResolution_ - 1));
    texelX = std::clamp(texelX, 0, static_cast<int>(holeMaskResolution_ - 1));
    texelY = std::clamp(texelY, 0, static_cast<int>(holeMaskResolution_ - 1));
}

bool TerrainTileCache::isHole(float x, float z) const {
    return TileGrid::isPointInHole(x, z, holes_);
}

void TerrainTileCache::addHoleCircle(float centerX, float centerZ, float radius) {
    holes_.push_back({centerX, centerZ, radius});

    // Rasterize to global mask and mark dirty
    rasterizeHolesToGlobalMask();
    holeMaskDirty_ = true;

    SDL_Log("TerrainTileCache: Added hole circle at (%.1f, %.1f) radius %.1f, total holes: %zu",
            centerX, centerZ, radius, holes_.size());
}

void TerrainTileCache::removeHoleCircle(float centerX, float centerZ, float radius) {
    auto it = std::remove_if(holes_.begin(), holes_.end(), [&](const TerrainHole& h) {
        return std::abs(h.centerX - centerX) < 0.1f &&
               std::abs(h.centerZ - centerZ) < 0.1f &&
               std::abs(h.radius - radius) < 0.1f;
    });
    if (it != holes_.end()) {
        holes_.erase(it, holes_.end());
        rasterizeHolesToGlobalMask();
        holeMaskDirty_ = true;
        SDL_Log("TerrainTileCache: Removed hole circle at (%.1f, %.1f), total holes: %zu",
                centerX, centerZ, holes_.size());
    }
}

void TerrainTileCache::rasterizeHolesToGlobalMask() {
    // Use TileGrid helper to rasterize holes for the entire terrain
    float halfTerrain = terrainSize * 0.5f;
    holeMaskCpuData_ = TileGrid::rasterizeHolesForTile(
        -halfTerrain, -halfTerrain, halfTerrain, halfTerrain,
        holeMaskResolution_, holes_);
}

std::vector<uint8_t> TerrainTileCache::rasterizeHolesForTile(
    float tileMinX, float tileMinZ, float tileMaxX, float tileMaxZ, uint32_t resolution) const {

    std::vector<uint8_t> tileMask(resolution * resolution, 0);

    float tileWidth = tileMaxX - tileMinX;
    float tileHeight = tileMaxZ - tileMinZ;

    // For each hole, check if it intersects this tile
    for (const auto& hole : holes_) {
        // Quick AABB check for circle-rectangle intersection
        float closestX = std::clamp(hole.centerX, tileMinX, tileMaxX);
        float closestZ = std::clamp(hole.centerZ, tileMinZ, tileMaxZ);
        float dx = hole.centerX - closestX;
        float dz = hole.centerZ - closestZ;
        if (dx * dx + dz * dz > hole.radius * hole.radius) {
            continue;  // Circle doesn't intersect tile
        }

        // Rasterize circle into tile mask
        // Shrink radius slightly to match GPU rendering
        float shrinkAmount = tileWidth / resolution;
        float effectiveRadius = hole.radius - shrinkAmount;
        if (effectiveRadius <= 0.0f) effectiveRadius = hole.radius * 0.5f;
        float radiusSq = effectiveRadius * effectiveRadius;

        for (uint32_t y = 0; y < resolution; y++) {
            for (uint32_t x = 0; x < resolution; x++) {
                float worldX = tileMinX + (static_cast<float>(x) / (resolution - 1)) * tileWidth;
                float worldZ = tileMinZ + (static_cast<float>(y) / (resolution - 1)) * tileHeight;

                float distX = worldX - hole.centerX;
                float distZ = worldZ - hole.centerZ;
                if (distX * distX + distZ * distZ < radiusSq) {
                    tileMask[y * resolution + x] = 255;
                }
            }
        }
    }

    return tileMask;
}

void TerrainTileCache::uploadHoleMaskToGPU() {
    if (holeMaskDirty_) {
        uploadHoleMaskToGPUInternal();
        holeMaskDirty_ = false;
    }
}
