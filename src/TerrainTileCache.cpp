#include "TerrainTileCache.h"
#include <SDL3/SDL.h>
#include <stb_image.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>

bool TerrainTileCache::init(const InitInfo& info) {
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
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create sampler");
        return false;
    }

    // Create tile info buffer for shader
    // Layout: uint activeTileCount, uint padding[3], TileInfoGPU tiles[MAX_ACTIVE_TILES]
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(uint32_t) * 4 + MAX_ACTIVE_TILES * sizeof(TileInfoGPU);
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &tileInfoBuffer,
                       &tileInfoAllocation, &allocationInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile info buffer");
        return false;
    }
    tileInfoMappedPtr = allocationInfo.pMappedData;

    // Create tile array image (2D array texture with MAX_ACTIVE_TILES layers)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;  // 32-bit float for height values
    imageInfo.extent.width = tileResolution;
    imageInfo.extent.height = tileResolution;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = MAX_ACTIVE_TILES;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &imgAllocInfo, &tileArrayImage,
                      &tileArrayAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile array image");
        return false;
    }

    // Create image view for the tile array
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tileArrayImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = MAX_ACTIVE_TILES;

    if (vkCreateImageView(device, &viewInfo, nullptr, &tileArrayView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile array image view");
        return false;
    }

    // Transition tile array to shader read layout
    {
        VkCommandBuffer cmd;
        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tileArrayImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = MAX_ACTIVE_TILES;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                            0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    }

    SDL_Log("TerrainTileCache initialized: %s", cacheDirectory.c_str());
    SDL_Log("  Terrain size: %.0fm, Tile resolution: %u, LOD levels: %u",
            terrainSize, tileResolution, numLODLevels);
    SDL_Log("  LOD0 grid: %ux%u tiles", tilesX, tilesZ);

    return true;
}

void TerrainTileCache::destroy() {
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

    // Destroy tile info buffer
    if (tileInfoBuffer) {
        vmaDestroyBuffer(allocator, tileInfoBuffer, tileInfoAllocation);
        tileInfoBuffer = VK_NULL_HANDLE;
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

    // Destroy sampler
    if (sampler) {
        vkDestroySampler(device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
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

                // Check if this tile should be at this LOD level
                uint32_t idealLOD = getLODForDistance(dist);
                if (idealLOD == lod && dist < loadRadius) {
                    TileCoord coord{tx, tz};
                    if (!isTileLoaded(coord, lod)) {
                        tilesToLoad.push_back({coord, lod});
                    }
                }
            }
        }
    }

    // Find tiles to unload (too far from camera)
    for (auto& [key, tile] : loadedTiles) {
        float tileCenterX = (tile.worldMinX + tile.worldMaxX) * 0.5f;
        float tileCenterZ = (tile.worldMinZ + tile.worldMaxZ) * 0.5f;

        float dist = std::sqrt((tileCenterX - camX) * (tileCenterX - camX) +
                               (tileCenterZ - camZ) * (tileCenterZ - camZ));

        if (dist > unloadRadius) {
            tilesToUnload.push_back(key);
        }
    }

    // Unload distant tiles
    for (uint64_t key : tilesToUnload) {
        auto it = loadedTiles.find(key);
        if (it != loadedTiles.end()) {
            TerrainTile& tile = it->second;
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
    uint64_t key = makeTileKey(coord, lod);
    TerrainTile& tile = loadedTiles[key];
    tile.coord = coord;
    tile.lod = lod;

    // Calculate world bounds for this tile
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

    // Convert to float32 directly - NO resampling
    tile.cpuData.resize(tileResolution * tileResolution);
    for (uint32_t i = 0; i < tileResolution * tileResolution; i++) {
        tile.cpuData[i] = static_cast<float>(data[i]) / 65535.0f;
    }

    stbi_image_free(data);

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

    tile.loaded = true;

    SDL_Log("TerrainTileCache: Loaded tile (%d, %d) LOD%u - world bounds [%.0f,%.0f]-[%.0f,%.0f]",
            coord.x, coord.z, lod, tile.worldMinX, tile.worldMinZ, tile.worldMaxX, tile.worldMaxZ);

    return true;
}

bool TerrainTileCache::createTileGPUResources(TerrainTile& tile) {
    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = {tileResolution, tileResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &tile.image, &tile.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tile.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &tile.imageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create tile image view");
        return false;
    }

    return true;
}

bool TerrainTileCache::uploadTileToGPU(TerrainTile& tile) {
    VkDeviceSize imageSize = tileResolution * tileResolution * sizeof(float);

    // Create staging buffer
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Copy data to staging buffer
    void* mappedData;
    vmaMapMemory(allocator, stagingAllocation, &mappedData);
    memcpy(mappedData, tile.cpuData.data(), imageSize);
    vmaUnmapMemory(allocator, stagingAllocation);

    // Allocate command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition image to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tile.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {tileResolution, tileResolution, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, tile.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition image to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    return true;
}

void TerrainTileCache::updateTileInfoBuffer() {
    if (!tileInfoMappedPtr) return;

    // First 4 bytes: active tile count
    uint32_t* countPtr = static_cast<uint32_t*>(tileInfoMappedPtr);
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

        // Copy tile data to the tile array texture (layer i)
        copyTileToArrayLayer(tile, static_cast<uint32_t>(i));
    }
}

bool TerrainTileCache::isTileLoaded(TileCoord coord, uint32_t lod) const {
    uint64_t key = makeTileKey(coord, lod);
    return loadedTiles.find(key) != loadedTiles.end();
}

bool TerrainTileCache::getHeightAt(float worldX, float worldZ, float& outHeight) const {
    // Find the best tile that covers this position
    for (const TerrainTile* tile : activeTiles) {
        if (worldX >= tile->worldMinX && worldX < tile->worldMaxX &&
            worldZ >= tile->worldMinZ && worldZ < tile->worldMaxZ) {

            // Calculate UV within tile
            float u = (worldX - tile->worldMinX) / (tile->worldMaxX - tile->worldMinX);
            float v = (worldZ - tile->worldMinZ) / (tile->worldMaxZ - tile->worldMinZ);

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

            float h00 = tile->cpuData[y0 * tileResolution + x0];
            float h10 = tile->cpuData[y0 * tileResolution + x1];
            float h01 = tile->cpuData[y1 * tileResolution + x0];
            float h11 = tile->cpuData[y1 * tileResolution + x1];

            float h0 = h00 * (1.0f - tx) + h10 * tx;
            float h1 = h01 * (1.0f - tx) + h11 * tx;
            float h = h0 * (1.0f - ty) + h1 * ty;

            // Convert to world height
            outHeight = h * heightScale + minAltitude;
            return true;
        }
    }

    return false; // No tile covers this position
}

void TerrainTileCache::copyTileToArrayLayer(TerrainTile* tile, uint32_t layerIndex) {
    if (!tile || tile->cpuData.empty() || layerIndex >= MAX_ACTIVE_TILES) return;

    // Create staging buffer for upload
    VkDeviceSize imageSize = tileResolution * tileResolution * sizeof(float);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                            VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stagingInfo{};
    if (vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer,
                       &stagingAllocation, &stagingInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TerrainTileCache: Failed to create staging buffer for tile copy");
        return;
    }

    // Copy tile data to staging buffer
    memcpy(stagingInfo.pMappedData, tile->cpuData.data(), imageSize);

    // Allocate command buffer
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition tile array layer to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tileArrayImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = layerIndex;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image layer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = layerIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {tileResolution, tileResolution, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, tileArrayImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition back to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Cleanup
    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

const TerrainTile* TerrainTileCache::getLoadedTile(TileCoord coord, uint32_t lod) const {
    uint64_t key = makeTileKey(coord, lod);
    auto it = loadedTiles.find(key);
    if (it != loadedTiles.end() && it->second.loaded) {
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

    // Load the tile
    return loadTile(coord, lod);
}
