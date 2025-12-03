#include "TerrainTile.h"
#include "TerrainImporter.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>

// Simple noise function for procedural terrain
static float hash(float n) {
    return glm::fract(sin(n) * 43758.5453123f);
}

static float noise(float x, float z) {
    glm::vec2 p = glm::floor(glm::vec2(x, z));
    glm::vec2 f = glm::fract(glm::vec2(x, z));
    f = f * f * (3.0f - 2.0f * f);  // Smoothstep

    float n = p.x + p.y * 57.0f;
    return glm::mix(
        glm::mix(hash(n + 0.0f), hash(n + 1.0f), f.x),
        glm::mix(hash(n + 57.0f), hash(n + 58.0f), f.x),
        f.y
    );
}

static float fbm(float x, float z, int octaves, float persistence) {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += noise(x * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / maxValue;
}

void TerrainTile::init(const Coord& tileCoord, const TerrainTileConfig& cfg) {
    coord = tileCoord;
    config = cfg;

    // Calculate tile size based on LOD level
    // LOD 0 = baseTileSize, LOD 1 = 2x, LOD 2 = 4x, etc.
    tileSize = cfg.baseTileSize * static_cast<float>(1u << coord.lod);

    // Calculate world position (tile grid coordinates are relative to LOD level)
    worldMin.x = static_cast<float>(coord.x) * tileSize;
    worldMin.y = static_cast<float>(coord.z) * tileSize;

    // Reserve CPU data (same resolution for all LODs, but covers larger area at higher LOD)
    uint32_t dataSize = config.heightmapResolution * config.heightmapResolution;
    cpuHeightData.resize(dataSize, 0.0f);

    loadState.store(TileLoadState::Unloaded);
}

bool TerrainTile::loadHeightData() {
    uint32_t res = config.heightmapResolution;

    // Try to load from cache if available
    if (!config.cacheDirectory.empty()) {
        SDL_Log("TerrainTile: Loading from cache directory: %s", config.cacheDirectory.c_str());
        std::string tilePath = TerrainImporter::getTilePath(
            config.cacheDirectory, coord.x, coord.z, coord.lod);

        std::ifstream file(tilePath, std::ios::binary);
        if (file.is_open()) {
            // Read resolution header
            uint32_t fileResX, fileResZ;
            file.read(reinterpret_cast<char*>(&fileResX), sizeof(fileResX));
            file.read(reinterpret_cast<char*>(&fileResZ), sizeof(fileResZ));

            if (fileResX == res && fileResZ == res) {
                // Read 16-bit height data
                std::vector<uint16_t> rawData(res * res);
                file.read(reinterpret_cast<char*>(rawData.data()), rawData.size() * sizeof(uint16_t));

                if (file.good()) {
                    // Convert 16-bit to normalized float [0, 1]
                    float invMax = 1.0f / 65535.0f;
                    for (uint32_t i = 0; i < res * res; i++) {
                        cpuHeightData[i] = static_cast<float>(rawData[i]) * invMax;
                    }
                    return true;
                }
            }

            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tile cache: %s", tilePath.c_str());
        }
        // Fall through to procedural if cache load fails
    }

    // Generate procedural heightmap data as fallback
    float invRes = 1.0f / static_cast<float>(res - 1);

    for (uint32_t z = 0; z < res; z++) {
        for (uint32_t x = 0; x < res; x++) {
            // Convert to world coordinates
            float worldX = worldMin.x + static_cast<float>(x) * invRes * tileSize;
            float worldZ = worldMin.y + static_cast<float>(z) * invRes * tileSize;

            // Generate height using fractal brownian motion
            float height = 0.0f;

            // Large-scale terrain features
            height += fbm(worldX * 0.002f, worldZ * 0.002f, 6, 0.5f) * 0.7f;

            // Medium-scale hills
            height += fbm(worldX * 0.01f, worldZ * 0.01f, 4, 0.5f) * 0.2f;

            // Small-scale detail
            height += fbm(worldX * 0.05f, worldZ * 0.05f, 3, 0.5f) * 0.1f;

            // Store normalized height [0, 1]
            cpuHeightData[z * res + x] = height;
        }
    }

    return true;
}

bool TerrainTile::createGPUResources(VkDevice device, VmaAllocator allocator,
                                      VkQueue graphicsQueue, VkCommandPool commandPool) {
    // Create heightmap image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = {config.heightmapResolution, config.heightmapResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo,
                       &heightmapImage, &heightmapAllocation, nullptr) != VK_SUCCESS) {
        return false;
    }

    // Create staging buffer and upload
    VkDeviceSize imageSize = config.heightmapResolution * config.heightmapResolution * sizeof(float);

    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = imageSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                             VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingAllocationInfo;

    if (vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                        &stagingBuffer, &stagingAllocation, &stagingAllocationInfo) != VK_SUCCESS) {
        vmaDestroyImage(allocator, heightmapImage, heightmapAllocation);
        return false;
    }

    memcpy(stagingAllocationInfo.pMappedData, cpuHeightData.data(), imageSize);

    // Record and submit transfer commands
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

    // Transition to transfer dst
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = heightmapImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
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
    region.imageExtent = {config.heightmapResolution, config.heightmapResolution, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, heightmapImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = heightmapImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &heightmapView) != VK_SUCCESS) {
        vmaDestroyImage(allocator, heightmapImage, heightmapAllocation);
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &heightmapSampler) != VK_SUCCESS) {
        vkDestroyImageView(device, heightmapView, nullptr);
        vmaDestroyImage(allocator, heightmapImage, heightmapAllocation);
        return false;
    }

    // Create CBT buffer for this tile
    cbtBufferSize = calculateCBTBufferSize(config.cbtMaxDepth);

    VkBufferCreateInfo cbtBufferInfo{};
    cbtBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cbtBufferInfo.size = cbtBufferSize;
    cbtBufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    cbtBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo cbtAllocInfo{};
    cbtAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateBuffer(allocator, &cbtBufferInfo, &cbtAllocInfo,
                        &cbtBuffer, &cbtAllocation, nullptr) != VK_SUCCESS) {
        vkDestroySampler(device, heightmapSampler, nullptr);
        vkDestroyImageView(device, heightmapView, nullptr);
        vmaDestroyImage(allocator, heightmapImage, heightmapAllocation);
        return false;
    }

    // Initialize CBT with staging buffer
    VkBufferCreateInfo cbtStagingInfo{};
    cbtStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cbtStagingInfo.size = cbtBufferSize;
    cbtStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo cbtStagingAllocInfo{};
    cbtStagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    cbtStagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                 VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer cbtStagingBuffer;
    VmaAllocation cbtStagingAllocation;
    VmaAllocationInfo cbtStagingAllocationInfo;

    if (vmaCreateBuffer(allocator, &cbtStagingInfo, &cbtStagingAllocInfo,
                        &cbtStagingBuffer, &cbtStagingAllocation, &cbtStagingAllocationInfo) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, cbtBuffer, cbtAllocation);
        vkDestroySampler(device, heightmapSampler, nullptr);
        vkDestroyImageView(device, heightmapView, nullptr);
        vmaDestroyImage(allocator, heightmapImage, heightmapAllocation);
        return false;
    }

    initializeCBT(cbtStagingAllocationInfo.pMappedData);

    // Upload CBT buffer
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = cbtBufferSize;
    vkCmdCopyBuffer(cmd, cbtStagingBuffer, cbtBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vmaDestroyBuffer(allocator, cbtStagingBuffer, cbtStagingAllocation);

    return true;
}

void TerrainTile::destroyGPUResources(VkDevice device, VmaAllocator allocator) {
    if (cbtBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, cbtBuffer, cbtAllocation);
        cbtBuffer = VK_NULL_HANDLE;
        cbtAllocation = VK_NULL_HANDLE;
    }

    if (heightmapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, heightmapSampler, nullptr);
        heightmapSampler = VK_NULL_HANDLE;
    }

    if (heightmapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, heightmapView, nullptr);
        heightmapView = VK_NULL_HANDLE;
    }

    if (heightmapImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, heightmapImage, heightmapAllocation);
        heightmapImage = VK_NULL_HANDLE;
        heightmapAllocation = VK_NULL_HANDLE;
    }
}

void TerrainTile::reset() {
    loadState.store(TileLoadState::Unloaded);
    cpuHeightData.clear();
    lastAccessFrame = 0;
}

float TerrainTile::getHeightAt(float localX, float localZ) const {
    if (cpuHeightData.empty()) {
        return 0.0f;
    }

    // Convert local position to UV
    float u = localX / tileSize;
    float v = localZ / tileSize;

    u = glm::clamp(u, 0.0f, 1.0f);
    v = glm::clamp(v, 0.0f, 1.0f);

    // Bilinear interpolation
    float fx = u * (config.heightmapResolution - 1);
    float fz = v * (config.heightmapResolution - 1);

    int x0 = static_cast<int>(fx);
    int z0 = static_cast<int>(fz);
    int x1 = std::min(x0 + 1, static_cast<int>(config.heightmapResolution) - 1);
    int z1 = std::min(z0 + 1, static_cast<int>(config.heightmapResolution) - 1);

    float tx = fx - x0;
    float tz = fz - z0;

    uint32_t res = config.heightmapResolution;
    float h00 = cpuHeightData[z0 * res + x0];
    float h10 = cpuHeightData[z0 * res + x1];
    float h01 = cpuHeightData[z1 * res + x0];
    float h11 = cpuHeightData[z1 * res + x1];

    float h0 = glm::mix(h00, h10, tx);
    float h1 = glm::mix(h01, h11, tx);

    // Convert normalized [0,1] to actual altitude
    float normalizedHeight = glm::mix(h0, h1, tz);
    return config.minAltitude + normalizedHeight * config.getHeightScale();
}

size_t TerrainTile::getGPUMemoryUsage() const {
    size_t usage = 0;

    // Heightmap image
    usage += config.heightmapResolution * config.heightmapResolution * sizeof(float);

    // CBT buffer
    usage += cbtBufferSize;

    return usage;
}

float TerrainTile::getDistanceToCamera(const glm::vec3& cameraPos) const {
    glm::vec2 center = getWorldCenter();
    glm::vec2 camPos2D(cameraPos.x, cameraPos.z);
    return glm::distance(center, camPos2D);
}

uint32_t TerrainTile::calculateCBTBufferSize(int maxDepth) {
    // CBT buffer structure:
    // - Sum reduction tree (depth levels 0 to maxDepth-5)
    // - Bitfield at maxDepth

    // For maxDepth D, we need:
    // - Bitfield: 2^D bits = 2^(D-5) uint32s
    // - Sum tree above that

    uint32_t bitfieldSize = (1u << maxDepth) / 32;  // Number of uint32s for bitfield
    uint32_t totalSize = 0;

    // Sum reduction levels (stored above bitfield)
    for (int level = 0; level < maxDepth - 4; level++) {
        uint32_t levelSize = 1u << level;
        totalSize += levelSize * sizeof(uint32_t);
    }

    // Bitfield
    totalSize += bitfieldSize * sizeof(uint32_t);

    // Add header (marker and padding)
    totalSize += 16;  // Alignment padding

    return totalSize;
}

void TerrainTile::initializeCBT(void* mappedBuffer) {
    uint32_t* data = static_cast<uint32_t*>(mappedBuffer);
    memset(data, 0, cbtBufferSize);

    // Set the marker at the start (1 << maxDepth tells shaders the tree depth)
    data[0] = 1u << config.cbtMaxDepth;

    // Initialize with 2^initDepth leaf nodes
    // This means setting bits in the bitfield for the initial subdivision
    uint32_t numInitialLeaves = 1u << config.cbtInitDepth;

    // The bitfield starts after the sum reduction tree
    // For simplicity, we'll set up the sum tree correctly

    // First, set the root sum to the initial leaf count
    data[1] = numInitialLeaves;

    // Build up the sum tree from the leaves
    // This is simplified - in practice the GPU will rebuild it each frame
    // We just need a valid initial state

    // Calculate bitfield offset
    uint32_t bitfieldOffset = 0;
    for (int level = 0; level < config.cbtMaxDepth - 4; level++) {
        bitfieldOffset += 1u << level;
    }
    bitfieldOffset += 4;  // Header offset (in uint32s)

    // Set initial leaf bits
    // For initDepth leaves, we mark them as active in the bitfield
    // Each leaf at depth D corresponds to a bit at position 2^D + leafIndex
    uint32_t leafBase = 1u << config.cbtInitDepth;
    for (uint32_t i = 0; i < numInitialLeaves; i++) {
        uint32_t bitIndex = leafBase + i;
        uint32_t wordIndex = bitfieldOffset + (bitIndex / 32);
        uint32_t bitOffset = bitIndex % 32;
        if (wordIndex < cbtBufferSize / sizeof(uint32_t)) {
            data[wordIndex] |= (1u << bitOffset);
        }
    }
}
