#include "TerrainTileCache.h"
#include "TerrainHeight.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <stb_image.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>

std::unique_ptr<TerrainTileCache> TerrainTileCache::create(const InitInfo& info) {
    std::unique_ptr<TerrainTileCache> cache(new TerrainTileCache());
    if (!cache->initInternal(info)) {
        return nullptr;
    }
    return cache;
}

TerrainTileCache::~TerrainTileCache() {
    cleanup();
}

bool TerrainTileCache::initInternal(const InitInfo& info) {
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

    // Create sampler for tile textures
    if (!VulkanResourceFactory::createSamplerLinearClamp(device, sampler)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create sampler");
        return false;
    }

    // Create tile info buffers for shader using VulkanResourceFactory (triple-buffered)
    // Layout: uint activeTileCount, uint padding[3], TileInfoGPU tiles[MAX_ACTIVE_TILES]
    VkDeviceSize bufferSize = sizeof(uint32_t) * 4 + MAX_ACTIVE_TILES * sizeof(TileInfoGPU);
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        if (!VulkanResourceFactory::createStorageBufferHostReadable(allocator, bufferSize, tileInfoBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile info buffer %u", i);
            return false;
        }
        tileInfoMappedPtrs_[i] = tileInfoBuffers_[i].map();
    }

    // Create tile array image (2D array texture with MAX_ACTIVE_TILES layers)
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR32Sfloat)  // 32-bit float for height values
        .setExtent(vk::Extent3D{tileResolution, tileResolution, 1})
        .setMipLevels(1)
        .setArrayLayers(MAX_ACTIVE_TILES)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &imgAllocInfo, &tileArrayImage,
                      &tileArrayAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile array image");
        return false;
    }

    // Create image view for the tile array
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(tileArrayImage)
        .setViewType(vk::ImageViewType::e2DArray)
        .setFormat(vk::Format::eR32Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(MAX_ACTIVE_TILES));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &tileArrayView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile array image view");
        return false;
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

        Barriers::transitionImage(cmd, tileArrayImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, MAX_ACTIVE_TILES);

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

    // Initialize stats
    stats_.maxActiveTiles = MAX_ACTIVE_TILES;

    SDL_Log("TerrainTileCache initialized: %s", cacheDirectory.c_str());
    SDL_Log("  Terrain size: %.0fm, Tile resolution: %u, LOD levels: %u",
            terrainSize, tileResolution, numLODLevels);
    SDL_Log("  LOD0 grid: %ux%u tiles", tilesX, tilesZ);

    // Load coarse LOD (LOD3) tiles for initial coverage before any high-res streaming
    loadCoarseLODCoverage();

    return true;
}

void TerrainTileCache::cleanup() {
    // Wait for GPU to finish
    if (device) {
        vkDeviceWaitIdle(device);
    }

    // Unload all tiles
    for (auto& [key, tile] : loadedTiles) {
        if (tile.imageView) vkDestroyImageView(device, tile.imageView, nullptr);
        if (tile.image) vmaDestroyImage(allocator, tile.image, tile.allocation);
    }
    loadedTiles.clear();
    activeTiles.clear();

    // Destroy tile info buffers (RAII via reset)
    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        tileInfoBuffers_[i].reset();
    }

    // Destroy tile array texture
    if (tileArrayView) {
        vkDestroyImageView(device, tileArrayView, nullptr);
        tileArrayView = VK_NULL_HANDLE;
    }
    if (tileArrayImage) {
        vmaDestroyImage(allocator, tileArrayImage, tileArrayAllocation);
        tileArrayImage = VK_NULL_HANDLE;
    }

    // Destroy sampler (RAII via reset)
    sampler.reset();
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

uint32_t TerrainTileCache::getLODForDistance(float distance) const {
    if (distance < LOD0_MAX_DISTANCE) return 0;
    if (distance < LOD1_MAX_DISTANCE && numLODLevels > 1) return 1;
    if (distance < LOD2_MAX_DISTANCE && numLODLevels > 2) return 2;
    if (distance < LOD3_MAX_DISTANCE && numLODLevels > 3) return 3;
    return numLODLevels; // Beyond all LOD levels - use global fallback
}

// Helper: Get source dimensions at a given LOD level
static void getLODSourceDimensions(uint32_t sourceWidth, uint32_t sourceHeight, uint32_t lod,
                                    uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = sourceWidth >> lod;
    outHeight = sourceHeight >> lod;
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;
}

// Helper: Get tile count at a given LOD level (accounting for 1-texel overlap)
static void getLODTileCount(uint32_t lodSourceWidth, uint32_t lodSourceHeight, uint32_t tileRes,
                             uint32_t& outTilesX, uint32_t& outTilesZ) {
    // Tiles overlap by 1 pixel for seamless boundaries (stride = tileRes - 1)
    uint32_t stride = tileRes - 1;  // 511 for 512x512 tiles
    outTilesX = (lodSourceWidth > 1) ? ((lodSourceWidth - 1) / stride) + 1 : 1;
    outTilesZ = (lodSourceHeight > 1) ? ((lodSourceHeight - 1) / stride) + 1 : 1;
}

TileCoord TerrainTileCache::worldToTileCoord(float worldX, float worldZ, uint32_t lod) const {
    // Get LOD source dimensions and tile count
    uint32_t lodSourceW, lodSourceH;
    getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);
    uint32_t lodTilesX, lodTilesZ;
    getLODTileCount(lodSourceW, lodSourceH, tileResolution, lodTilesX, lodTilesZ);

    // Convert world position to normalized [0, 1]
    float normX = (worldX / terrainSize) + 0.5f;
    float normZ = (worldZ / terrainSize) + 0.5f;

    // Clamp to valid range
    normX = std::clamp(normX, 0.0f, 0.9999f);
    normZ = std::clamp(normZ, 0.0f, 0.9999f);

    // With overlapping tiles (stride = tileRes - 1), compute tile index based on stride
    uint32_t stride = tileResolution - 1;
    float worldPixelX = normX * static_cast<float>(lodSourceW);
    float worldPixelZ = normZ * static_cast<float>(lodSourceH);

    TileCoord coord;
    coord.x = static_cast<int32_t>(worldPixelX / static_cast<float>(stride));
    coord.z = static_cast<int32_t>(worldPixelZ / static_cast<float>(stride));

    // Clamp to valid tile range
    coord.x = std::clamp(coord.x, 0, static_cast<int32_t>(lodTilesX - 1));
    coord.z = std::clamp(coord.z, 0, static_cast<int32_t>(lodTilesZ - 1));

    return coord;
}

void TerrainTileCache::updateActiveTiles(const glm::vec3& cameraPos, float loadRadius, float unloadRadius) {
    // Reset per-frame stats
    stats_.tilesLoadedThisFrame = 0;

    // Determine which tiles should be loaded based on camera position
    std::vector<std::pair<TileCoord, uint32_t>> tilesToLoad;
    std::vector<uint64_t> tilesToUnload;

    // Calculate camera position in terrain space
    float camX = cameraPos.x;
    float camZ = cameraPos.z;

    // Process LODs from coarsest to finest - only request finer LODs when coarse coverage exists
    // This ensures LOD3 loads first, then LOD2, etc.
    for (int lodInt = static_cast<int>(numLODLevels) - 1; lodInt >= 0; lodInt--) {
        uint32_t lod = static_cast<uint32_t>(lodInt);

        // Calculate tile count at this LOD using overlap-aware formula
        uint32_t lodSourceW, lodSourceH;
        getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);
        uint32_t lodTilesX, lodTilesZ;
        getLODTileCount(lodSourceW, lodSourceH, tileResolution, lodTilesX, lodTilesZ);

        // With overlapping tiles, stride determines tile spacing
        uint32_t stride = tileResolution - 1;
        float strideWorld = terrainSize * static_cast<float>(stride) / static_cast<float>(lodSourceW);

        // Calculate tile range to check around camera based on stride
        int32_t minTileX = static_cast<int32_t>(std::floor(((camX - loadRadius) / terrainSize + 0.5f) * lodSourceW / stride));
        int32_t maxTileX = static_cast<int32_t>(std::floor(((camX + loadRadius) / terrainSize + 0.5f) * lodSourceW / stride));
        int32_t minTileZ = static_cast<int32_t>(std::floor(((camZ - loadRadius) / terrainSize + 0.5f) * lodSourceH / stride));
        int32_t maxTileZ = static_cast<int32_t>(std::floor(((camZ + loadRadius) / terrainSize + 0.5f) * lodSourceH / stride));

        // Clamp to valid tile range
        minTileX = std::max(0, minTileX);
        maxTileX = std::min(static_cast<int32_t>(lodTilesX - 1), maxTileX);
        minTileZ = std::max(0, minTileZ);
        maxTileZ = std::min(static_cast<int32_t>(lodTilesZ - 1), maxTileZ);

        for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
            for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
                // Calculate tile center in world space (accounting for stride-based positioning)
                float tileStartX = (static_cast<float>(tx * stride) / lodSourceW - 0.5f) * terrainSize;
                float tileStartZ = (static_cast<float>(tz * stride) / lodSourceH - 0.5f) * terrainSize;
                float tileCenterX = tileStartX + strideWorld * 0.5f;
                float tileCenterZ = tileStartZ + strideWorld * 0.5f;

                float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                                       (tileCenterZ - camZ) * (tileCenterZ - camZ));

                // Check if this tile should be at this LOD level based on distance
                uint32_t idealLOD = getLODForDistance(dist);
                if (idealLOD != lod || dist >= loadRadius) {
                    continue;
                }

                TileCoord coord{tx, tz};

                // Skip if already GPU-loaded
                if (isTileLoaded(coord, lod)) {
                    continue;
                }

                // For non-coarsest LODs, only request if coarser LOD is already GPU-loaded
                // This ensures progressive loading from LOD3 → LOD2 → LOD1 → LOD0
                if (lod < numLODLevels - 1) {
                    uint32_t coarserLOD = lod + 1;
                    TileCoord coarserCoord = worldToTileCoord(tileCenterX, tileCenterZ, coarserLOD);
                    if (!isTileLoaded(coarserCoord, coarserLOD)) {
                        // Coarser LOD not ready yet, skip this finer tile
                        continue;
                    }
                }

                tilesToLoad.push_back({coord, lod});
            }
        }
    }

    // Find tiles to unload (too far from camera)
    for (auto& [key, tile] : loadedTiles) {
        float tileCenterX = (tile.worldMinX + tile.worldMaxX) * 0.5f;
        float tileCenterZ = (tile.worldMinZ + tile.worldMaxZ) * 0.5f;

        float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                               (tileCenterZ - camZ) * (tileCenterZ - camZ));

        // Only unload if too far - keep coarse LODs as fallback
        if (dist > unloadRadius) {
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

    // Sort tiles by LOD: higher LOD values (coarser detail) load FIRST
    std::sort(tilesToLoad.begin(), tilesToLoad.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });

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

    // Track pending loads for stats
    stats_.pendingLoads = static_cast<uint32_t>(tilesToLoad.size()) - tilesLoadedThisFrame;
    stats_.tilesLoadedThisFrame = tilesLoadedThisFrame;

    // Update active tiles list (GPU-loaded tiles only)
    activeTiles.clear();
    for (auto& [key, tile] : loadedTiles) {
        if (tile.loaded) {
            activeTiles.push_back(&tile);
        }
    }

    // Update statistics
    updateStats();

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
        // Need to load from disk
        std::string path = getTilePath(coord, lod);

        int width, height, channels;

        // Load 16-bit PNG at NATIVE resolution - NO downsampling!
        uint16_t* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
        if (!data) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to load tile: %s",
                        path.c_str());
            return false;
        }

        // CRITICAL: Tiles must be 512x512 - refuse to resample
        if (width != static_cast<int>(tileResolution) || height != static_cast<int>(tileResolution)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "TerrainTileCache: Tile %s is %dx%d, expected %ux%u - refusing to resample",
                         path.c_str(), width, height, tileResolution, tileResolution);
            stbi_image_free(data);
            return false;
        }

        // Create tile entry
        TerrainTile& tile = loadedTiles[key];
        tile.coord = coord;
        tile.lod = lod;

        // Calculate world bounds for this tile using overlap-aware formula
        uint32_t lodSourceW, lodSourceH;
        getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);

        // With overlapping tiles, stride determines world position of each tile
        uint32_t stride = tileResolution - 1;  // 511 for 512x512 tiles
        float pixelWorldSize = terrainSize / static_cast<float>(lodSourceW);

        // Tile N starts at source pixel (N * stride)
        tile.worldMinX = (static_cast<float>(coord.x * stride) / lodSourceW - 0.5f) * terrainSize;
        tile.worldMinZ = (static_cast<float>(coord.z * stride) / lodSourceH - 0.5f) * terrainSize;
        // Each tile covers tileResolution pixels worth of world space
        tile.worldMaxX = tile.worldMinX + static_cast<float>(tileResolution) * pixelWorldSize;
        tile.worldMaxZ = tile.worldMinZ + static_cast<float>(tileResolution) * (terrainSize / static_cast<float>(lodSourceH));

        // Convert to float32 directly - NO resampling
        tile.cpuData.resize(tileResolution * tileResolution);
        for (uint32_t i = 0; i < tileResolution * tileResolution; i++) {
            tile.cpuData[i] = static_cast<float>(data[i]) / 65535.0f;
        }

        stbi_image_free(data);
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
    // Create Vulkan image
    auto imageInfo = vk::ImageCreateInfo{}
        .setImageType(vk::ImageType::e2D)
        .setFormat(vk::Format::eR32Sfloat)
        .setExtent(vk::Extent3D{tileResolution, tileResolution, 1})
        .setMipLevels(1)
        .setArrayLayers(1)
        .setSamples(vk::SampleCountFlagBits::e1)
        .setTiling(vk::ImageTiling::eOptimal)
        .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive)
        .setInitialLayout(vk::ImageLayout::eUndefined);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, reinterpret_cast<const VkImageCreateInfo*>(&imageInfo), &allocInfo, &tile.image, &tile.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile image");
        return false;
    }

    // Create image view
    auto viewInfo = vk::ImageViewCreateInfo{}
        .setImage(tile.image)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(vk::Format::eR32Sfloat)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1));

    if (vkCreateImageView(device, reinterpret_cast<const VkImageViewCreateInfo*>(&viewInfo), nullptr, &tile.imageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile image view");
        return false;
    }

    return true;
}

bool TerrainTileCache::uploadTileToGPU(TerrainTile& tile) {
    VkDeviceSize imageSize = tileResolution * tileResolution * sizeof(float);

    // Create staging buffer using RAII wrapper
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
        return false;
    }

    // Copy data to staging buffer
    void* mappedData = stagingBuffer.map();
    memcpy(mappedData, tile.cpuData.data(), imageSize);
    stagingBuffer.unmap();

    // Use CommandScope for one-time command submission
    CommandScope cmd(device, commandPool, graphicsQueue);
    if (!cmd.begin()) return false;

    // Copy staging buffer to tile image with automatic barrier transitions
    Barriers::copyBufferToImage(cmd.get(), stagingBuffer.get(), tile.image, tileResolution, tileResolution);

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
    // Helper to sample height from a tile
    auto sampleTile = [&](const TerrainTile& tile) -> bool {
        if (tile.cpuData.empty()) return false;
        if (worldX < tile.worldMinX || worldX >= tile.worldMaxX ||
            worldZ < tile.worldMinZ || worldZ >= tile.worldMaxZ) return false;

        // Calculate UV within tile
        float u = (worldX - tile.worldMinX) / (tile.worldMaxX - tile.worldMinX);
        float v = (worldZ - tile.worldMinZ) / (tile.worldMaxZ - tile.worldMinZ);

        // Clamp to valid range
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        // Sample with bilinear interpolation
        float fx = u * (tileResolution - 1);
        float fy = v * (tileResolution - 1);

        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, static_cast<int>(tileResolution - 1));
        int y1 = std::min(y0 + 1, static_cast<int>(tileResolution - 1));

        float tx = fx - x0;
        float ty = fy - y0;

        float h00 = tile.cpuData[y0 * tileResolution + x0];
        float h10 = tile.cpuData[y0 * tileResolution + x1];
        float h01 = tile.cpuData[y1 * tileResolution + x0];
        float h11 = tile.cpuData[y1 * tileResolution + x1];

        float h0 = h00 * (1.0f - tx) + h10 * tx;
        float h1 = h01 * (1.0f - tx) + h11 * tx;
        float h = h0 * (1.0f - ty) + h1 * ty;

        // Convert to world height using authoritative formula from TerrainHeight.h
        outHeight = TerrainHeight::toWorld(h, heightScale);
        return true;
    };

    // First check active tiles (GPU tiles - highest priority)
    for (const TerrainTile* tile : activeTiles) {
        if (sampleTile(*tile)) return true;
    }

    // Also check all loaded tiles (includes CPU-only tiles from physics preloading)
    // This ensures physics and CPU queries use the same high-res tile data
    for (const auto& [key, tile] : loadedTiles) {
        if (sampleTile(tile)) return true;
    }

    return false; // No tile covers this position
}

void TerrainTileCache::copyTileToArrayLayer(TerrainTile* tile, uint32_t layerIndex) {
    if (!tile || tile->cpuData.empty() || layerIndex >= MAX_ACTIVE_TILES) return;

    // Create staging buffer using RAII wrapper
    VkDeviceSize imageSize = tileResolution * tileResolution * sizeof(float);

    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, imageSize, stagingBuffer)) {
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

    // Transition tile array layer to transfer dst
    Barriers::transitionImage(cmd.get(), tileArrayImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layerIndex, 1);

    // Copy buffer to image layer
    Barriers::copyBufferToImageLayer(cmd.get(), stagingBuffer.get(), tileArrayImage,
                                     tileResolution, tileResolution, layerIndex);

    // Transition back to shader read
    Barriers::transitionImage(cmd.get(), tileArrayImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, layerIndex, 1);

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

    std::string path = getTilePath(coord, lod);

    int width, height, channels;

    // Load 16-bit PNG at NATIVE resolution - NO downsampling!
    uint16_t* data = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to load tile (CPU): %s",
                    path.c_str());
        return false;
    }

    // CRITICAL: Tiles must be 512x512 - refuse to resample
    if (width != static_cast<int>(tileResolution) || height != static_cast<int>(tileResolution)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "TerrainTileCache: Tile %s is %dx%d, expected %ux%u - refusing to resample",
                     path.c_str(), width, height, tileResolution, tileResolution);
        stbi_image_free(data);
        return false;
    }

    // Create tile entry with CPU data only (no GPU)
    TerrainTile& tile = loadedTiles[key];
    tile.coord = coord;
    tile.lod = lod;

    // Calculate world bounds for this tile using overlap-aware formula
    uint32_t lodSourceW, lodSourceH;
    getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);

    // With overlapping tiles, stride determines world position of each tile
    uint32_t stride = tileResolution - 1;  // 511 for 512x512 tiles
    float pixelWorldSize = terrainSize / static_cast<float>(lodSourceW);

    // Tile N starts at source pixel (N * stride)
    tile.worldMinX = (static_cast<float>(coord.x * stride) / lodSourceW - 0.5f) * terrainSize;
    tile.worldMinZ = (static_cast<float>(coord.z * stride) / lodSourceH - 0.5f) * terrainSize;
    // Each tile covers tileResolution pixels worth of world space
    tile.worldMaxX = tile.worldMinX + static_cast<float>(tileResolution) * pixelWorldSize;
    tile.worldMaxZ = tile.worldMinZ + static_cast<float>(tileResolution) * (terrainSize / static_cast<float>(lodSourceH));

    // Convert to float32 directly - NO resampling
    tile.cpuData.resize(tileResolution * tileResolution);
    for (uint32_t i = 0; i < tileResolution * tileResolution; i++) {
        tile.cpuData[i] = static_cast<float>(data[i]) / 65535.0f;
    }

    stbi_image_free(data);

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

    // Calculate LOD0 tile count using overlap-aware formula
    uint32_t lodSourceW, lodSourceH;
    getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);
    uint32_t lodTilesX, lodTilesZ;
    getLODTileCount(lodSourceW, lodSourceH, tileResolution, lodTilesX, lodTilesZ);
    uint32_t stride = tileResolution - 1;

    // Calculate tile range covering the radius
    int32_t minTileX = static_cast<int32_t>(std::floor(((worldX - radius) / terrainSize + 0.5f) * lodSourceW / stride));
    int32_t maxTileX = static_cast<int32_t>(std::floor(((worldX + radius) / terrainSize + 0.5f) * lodSourceW / stride));
    int32_t minTileZ = static_cast<int32_t>(std::floor(((worldZ - radius) / terrainSize + 0.5f) * lodSourceH / stride));
    int32_t maxTileZ = static_cast<int32_t>(std::floor(((worldZ + radius) / terrainSize + 0.5f) * lodSourceH / stride));

    // Clamp to valid range
    minTileX = std::max(0, minTileX);
    maxTileX = std::min(static_cast<int32_t>(lodTilesX - 1), maxTileX);
    minTileZ = std::max(0, minTileZ);
    maxTileZ = std::min(static_cast<int32_t>(lodTilesZ - 1), maxTileZ);

    float strideWorld = terrainSize * static_cast<float>(stride) / static_cast<float>(lodSourceW);

    uint32_t tilesLoaded = 0;
    for (int32_t tz = minTileZ; tz <= maxTileZ; tz++) {
        for (int32_t tx = minTileX; tx <= maxTileX; tx++) {
            // Calculate tile center for distance check
            float tileStartX = (static_cast<float>(tx * stride) / lodSourceW - 0.5f) * terrainSize;
            float tileStartZ = (static_cast<float>(tz * stride) / lodSourceH - 0.5f) * terrainSize;
            float tileCenterX = tileStartX + strideWorld * 0.5f;
            float tileCenterZ = tileStartZ + strideWorld * 0.5f;

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

void TerrainTileCache::updateStats() {
    // Reset per-LOD counts
    for (uint32_t i = 0; i < 4; i++) {
        stats_.tilesLoadedPerLOD[i] = 0;
    }
    stats_.totalTilesLoaded = 0;

    // Count tiles by LOD (count both GPU-loaded and CPU-only tiles)
    for (const auto& [key, tile] : loadedTiles) {
        if ((tile.loaded || !tile.cpuData.empty()) && tile.lod < 4) {
            stats_.tilesLoadedPerLOD[tile.lod]++;
            stats_.totalTilesLoaded++;
        }
    }

    // Check if coarsest LOD coverage is complete
    uint32_t coarsestLOD = numLODLevels - 1;
    uint32_t coarseSourceW, coarseSourceH;
    getLODSourceDimensions(sourceWidth, sourceHeight, coarsestLOD, coarseSourceW, coarseSourceH);
    uint32_t coarseTilesX, coarseTilesZ;
    getLODTileCount(coarseSourceW, coarseSourceH, tileResolution, coarseTilesX, coarseTilesZ);
    uint32_t expectedCoarseTiles = coarseTilesX * coarseTilesZ;
    stats_.initialLoadComplete = (stats_.tilesLoadedPerLOD[coarsestLOD] >= expectedCoarseTiles);
}

void TerrainTileCache::loadCoarseLODCoverage() {
    // Load all coarsest LOD tiles with full GPU upload
    // This provides immediate terrain coverage for both physics and rendering
    const uint32_t lod = numLODLevels - 1;  // Coarsest LOD (usually 3)

    // Calculate tile count using overlap-aware formula
    uint32_t lodSourceW, lodSourceH;
    getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);
    uint32_t lodTilesX, lodTilesZ;
    getLODTileCount(lodSourceW, lodSourceH, tileResolution, lodTilesX, lodTilesZ);

    uint32_t tilesLoaded = 0;
    uint32_t tilesFailed = 0;

    SDL_Log("TerrainTileCache: Loading LOD%u coverage (%ux%u tiles, with GPU upload)...", lod, lodTilesX, lodTilesZ);

    for (uint32_t tz = 0; tz < lodTilesZ; tz++) {
        for (uint32_t tx = 0; tx < lodTilesX; tx++) {
            TileCoord coord{static_cast<int32_t>(tx), static_cast<int32_t>(tz)};
            // Full load with GPU upload - tiles are immediately available for shader
            if (loadTile(coord, lod)) {
                tilesLoaded++;
            } else {
                tilesFailed++;
            }
        }
    }

    // Update active tiles list so shader can use them immediately
    activeTiles.clear();
    for (auto& [key, tile] : loadedTiles) {
        if (tile.loaded) {
            activeTiles.push_back(&tile);
        }
    }

    // Update tile info buffer so shader knows about the tiles
    updateTileInfoBuffer();

    // Update stats
    updateStats();

    SDL_Log("TerrainTileCache: LOD%u coverage complete - %u tiles loaded (CPU), %u failed",
            lod, tilesLoaded, tilesFailed);
}

int TerrainTileCache::getTileLODAt(int tileX, int tileZ) const {
    // Convert LOD0 grid position to world position for lookup
    uint32_t lod0SourceW, lod0SourceH;
    getLODSourceDimensions(sourceWidth, sourceHeight, 0, lod0SourceW, lod0SourceH);
    uint32_t stride0 = tileResolution - 1;

    // Calculate world position at center of LOD0 tile
    float worldX = (static_cast<float>(tileX * stride0 + tileResolution / 2) / lod0SourceW - 0.5f) * terrainSize;
    float worldZ = (static_cast<float>(tileZ * stride0 + tileResolution / 2) / lod0SourceH - 0.5f) * terrainSize;

    // Check from highest detail (LOD0) to lowest (LOD3)
    // Return the first (highest detail) LOD that has a tile covering this position
    for (uint32_t lod = 0; lod < numLODLevels; lod++) {
        // Calculate tile count for this LOD
        uint32_t lodSourceW, lodSourceH;
        getLODSourceDimensions(sourceWidth, sourceHeight, lod, lodSourceW, lodSourceH);
        uint32_t lodTilesX, lodTilesZ;
        getLODTileCount(lodSourceW, lodSourceH, tileResolution, lodTilesX, lodTilesZ);

        // Convert world position to this LOD's tile coordinate
        TileCoord coord = worldToTileCoord(worldX, worldZ, lod);

        // Check bounds
        if (coord.x < 0 || coord.x >= static_cast<int>(lodTilesX) ||
            coord.z < 0 || coord.z >= static_cast<int>(lodTilesZ)) {
            continue;
        }

        // Check if tile is loaded (either GPU or CPU-only)
        uint64_t key = makeTileKey(coord, lod);
        auto it = loadedTiles.find(key);
        if (it != loadedTiles.end() && (it->second.loaded || !it->second.cpuData.empty())) {
            return static_cast<int>(lod);
        }
    }
    return -1;  // No tile loaded
}
