#include "TerrainHeightMap.h"
#include "TerrainHeight.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

#define STB_IMAGE_IMPLEMENTATION_SKIP  // Already implemented elsewhere
#include <stb_image.h>

bool TerrainHeightMap::init(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    resolution = info.resolution;
    terrainSize = info.terrainSize;
    heightScale = info.heightScale;

    // Either load from file or generate procedurally
    if (!info.heightmapPath.empty()) {
        if (!loadHeightDataFromFile(info.heightmapPath, info.minAltitude, info.maxAltitude)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap from file, falling back to procedural");
            if (!generateHeightData()) return false;
        }
    } else {
        if (!generateHeightData()) return false;
    }

    // Initialize hole mask to all solid (no holes)
    holeMaskCpuData.resize(resolution * resolution, 0);

    if (!createGPUResources()) return false;
    if (!createHoleMaskResources()) return false;
    if (!uploadToGPU()) return false;
    if (!uploadHoleMaskToGPUInternal()) return false;

    SDL_Log("TerrainHeightMap initialized: %ux%u (with hole mask support)", resolution, resolution);
    return true;
}

void TerrainHeightMap::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy height map resources
    if (sampler) vkDestroySampler(device, sampler, nullptr);
    if (imageView) vkDestroyImageView(device, imageView, nullptr);
    if (image) vmaDestroyImage(allocator, image, allocation);

    sampler = VK_NULL_HANDLE;
    imageView = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;

    // Destroy hole mask resources
    if (holeMaskSampler) vkDestroySampler(device, holeMaskSampler, nullptr);
    if (holeMaskImageView) vkDestroyImageView(device, holeMaskImageView, nullptr);
    if (holeMaskImage) vmaDestroyImage(allocator, holeMaskImage, holeMaskAllocation);

    holeMaskSampler = VK_NULL_HANDLE;
    holeMaskImageView = VK_NULL_HANDLE;
    holeMaskImage = VK_NULL_HANDLE;
    holeMaskAllocation = VK_NULL_HANDLE;
}

bool TerrainHeightMap::generateHeightData() {
    cpuData.resize(resolution * resolution);

    for (uint32_t y = 0; y < resolution; y++) {
        for (uint32_t x = 0; x < resolution; x++) {
            float fx = static_cast<float>(x) / resolution;
            float fy = static_cast<float>(y) / resolution;

            // Distance from center (0.5, 0.5)
            float dx = fx - 0.5f;
            float dy = fy - 0.5f;
            float dist = sqrt(dx * dx + dy * dy);

            // Multiple octaves of sine-based noise for hills
            float height = 0.0f;
            height += 0.5f * sin(fx * 3.14159f * 2.0f) * sin(fy * 3.14159f * 2.0f);
            height += 0.25f * sin(fx * 3.14159f * 4.0f + 0.5f) * sin(fy * 3.14159f * 4.0f + 0.3f);
            height += 0.125f * sin(fx * 3.14159f * 8.0f + 1.0f) * sin(fy * 3.14159f * 8.0f + 0.7f);
            height += 0.0625f * sin(fx * 3.14159f * 16.0f + 2.0f) * sin(fy * 3.14159f * 16.0f + 1.5f);

            // Flatten center area where scene objects are
            float flattenFactor = glm::smoothstep(0.02f, 0.08f, dist);
            height *= flattenFactor;

            // Add steep cliff area for testing triplanar mapping
            float cliffCenterX = 0.70f;
            float cliffCenterY = 0.70f;
            float distToCliffCenter = sqrt((fx - cliffCenterX) * (fx - cliffCenterX) +
                                           (fy - cliffCenterY) * (fy - cliffCenterY));

            float cliffRadius = 0.08f;
            float cliffTransition = 0.015f;
            float cliffHeight = 0.8f;

            float cliffFactor = 1.0f - glm::smoothstep(cliffRadius - cliffTransition,
                                                        cliffRadius + cliffTransition,
                                                        distToCliffCenter);
            height += cliffFactor * cliffHeight;

            // Add a second smaller cliff area
            float cliff2CenterX = 0.25f;
            float cliff2CenterY = 0.30f;
            float distToCliff2 = sqrt((fx - cliff2CenterX) * (fx - cliff2CenterX) +
                                      (fy - cliff2CenterY) * (fy - cliff2CenterY));
            float cliff2Factor = 1.0f - glm::smoothstep(0.05f - 0.01f, 0.05f + 0.01f, distToCliff2);
            height += cliff2Factor * 0.6f;

            // Normalize to [0, 1]
            height = (height + 1.0f) * 0.5f;
            height = std::clamp(height, 0.0f, 1.0f);
            cpuData[y * resolution + x] = height;
        }
    }

    return true;
}

bool TerrainHeightMap::loadHeightDataFromFile(const std::string& path, float minAlt, float maxAlt) {
    int width, height, channels;

    // Load as 16-bit if available
    stbi_us* data16 = stbi_load_16(path.c_str(), &width, &height, &channels, 1);
    if (data16) {
        SDL_Log("Loaded 16-bit heightmap: %s (%dx%d)", path.c_str(), width, height);

        // The heightmap might be larger than our target resolution - resample if needed
        uint32_t srcWidth = static_cast<uint32_t>(width);
        uint32_t srcHeight = static_cast<uint32_t>(height);

        cpuData.resize(resolution * resolution);

        // Bilinear resampling from source to target resolution
        for (uint32_t y = 0; y < resolution; y++) {
            for (uint32_t x = 0; x < resolution; x++) {
                // Map target pixel to source coordinates
                float srcX = (static_cast<float>(x) / (resolution - 1)) * (srcWidth - 1);
                float srcY = (static_cast<float>(y) / (resolution - 1)) * (srcHeight - 1);

                int x0 = static_cast<int>(srcX);
                int y0 = static_cast<int>(srcY);
                int x1 = std::min(x0 + 1, static_cast<int>(srcWidth - 1));
                int y1 = std::min(y0 + 1, static_cast<int>(srcHeight - 1));

                float tx = srcX - x0;
                float ty = srcY - y0;

                // Sample 4 corners (16-bit values)
                float h00 = static_cast<float>(data16[y0 * srcWidth + x0]) / 65535.0f;
                float h10 = static_cast<float>(data16[y0 * srcWidth + x1]) / 65535.0f;
                float h01 = static_cast<float>(data16[y1 * srcWidth + x0]) / 65535.0f;
                float h11 = static_cast<float>(data16[y1 * srcWidth + x1]) / 65535.0f;

                // Bilinear interpolation
                float h0 = h00 * (1.0f - tx) + h10 * tx;
                float h1 = h01 * (1.0f - tx) + h11 * tx;
                float h = h0 * (1.0f - ty) + h1 * ty;

                // Store as normalized [0,1] value
                cpuData[y * resolution + x] = h;
            }
        }

        stbi_image_free(data16);

        SDL_Log("Height scale: %.1fm (altitude range: %.1fm to %.1fm)", heightScale, minAlt, maxAlt);

        return true;
    }

    // Fall back to 8-bit
    stbi_uc* data8 = stbi_load(path.c_str(), &width, &height, &channels, 1);
    if (data8) {
        SDL_Log("Loaded 8-bit heightmap: %s (%dx%d)", path.c_str(), width, height);

        uint32_t srcWidth = static_cast<uint32_t>(width);
        uint32_t srcHeight = static_cast<uint32_t>(height);

        cpuData.resize(resolution * resolution);

        for (uint32_t y = 0; y < resolution; y++) {
            for (uint32_t x = 0; x < resolution; x++) {
                float srcX = (static_cast<float>(x) / (resolution - 1)) * (srcWidth - 1);
                float srcY = (static_cast<float>(y) / (resolution - 1)) * (srcHeight - 1);

                int x0 = static_cast<int>(srcX);
                int y0 = static_cast<int>(srcY);
                int x1 = std::min(x0 + 1, static_cast<int>(srcWidth - 1));
                int y1 = std::min(y0 + 1, static_cast<int>(srcHeight - 1));

                float tx = srcX - x0;
                float ty = srcY - y0;

                float h00 = static_cast<float>(data8[y0 * srcWidth + x0]) / 255.0f;
                float h10 = static_cast<float>(data8[y0 * srcWidth + x1]) / 255.0f;
                float h01 = static_cast<float>(data8[y1 * srcWidth + x0]) / 255.0f;
                float h11 = static_cast<float>(data8[y1 * srcWidth + x1]) / 255.0f;

                float h0 = h00 * (1.0f - tx) + h10 * tx;
                float h1 = h01 * (1.0f - tx) + h11 * tx;
                float h = h0 * (1.0f - ty) + h1 * ty;

                cpuData[y * resolution + x] = h;
            }
        }

        stbi_image_free(data8);

        SDL_Log("Height scale: %.1fm (altitude range: %.1fm to %.1fm)", heightScale, minAlt, maxAlt);

        return true;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap: %s - %s", path.c_str(), stbi_failure_reason());
    return false;
}

bool TerrainHeightMap::createGPUResources() {
    // Create Vulkan image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT;
    imageInfo.extent = {resolution, resolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create height map image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create height map image view");
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create height map sampler");
        return false;
    }

    return true;
}

bool TerrainHeightMap::createHoleMaskResources() {
    // Create Vulkan image for hole mask (R8_UNORM: 0=solid, 255=hole)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8_UNORM;
    imageInfo.extent = {resolution, resolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &holeMaskImage, &holeMaskAllocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create hole mask image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = holeMaskImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &holeMaskImageView) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create hole mask image view");
        return false;
    }

    // Create sampler (nearest filtering for crisp hole edges)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;  // Smooth edges for rendering
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &holeMaskSampler) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create hole mask sampler");
        return false;
    }

    return true;
}

bool TerrainHeightMap::uploadHoleMaskToGPUInternal() {
    VkDeviceSize imageSize = resolution * resolution * sizeof(uint8_t);

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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for hole mask upload");
        return false;
    }

    // Copy data to staging buffer
    void* mappedData;
    vmaMapMemory(allocator, stagingAllocation, &mappedData);
    memcpy(mappedData, holeMaskCpuData.data(), imageSize);
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
    barrier.image = holeMaskImage;
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
    region.imageExtent = {resolution, resolution, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, holeMaskImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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

bool TerrainHeightMap::uploadToGPU() {
    VkDeviceSize imageSize = resolution * resolution * sizeof(float);

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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create staging buffer for height map upload");
        return false;
    }

    // Copy data to staging buffer
    void* mappedData;
    vmaMapMemory(allocator, stagingAllocation, &mappedData);
    memcpy(mappedData, cpuData.data(), imageSize);
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
    barrier.image = image;
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
    region.imageExtent = {resolution, resolution, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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

void TerrainHeightMap::worldToTexel(float x, float z, int& texelX, int& texelY) const {
    float u = (x / terrainSize) + 0.5f;
    float v = (z / terrainSize) + 0.5f;
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    texelX = static_cast<int>(u * (resolution - 1));
    texelY = static_cast<int>(v * (resolution - 1));
    texelX = std::clamp(texelX, 0, static_cast<int>(resolution - 1));
    texelY = std::clamp(texelY, 0, static_cast<int>(resolution - 1));
}

bool TerrainHeightMap::isHole(float x, float z) const {
    int texelX, texelY;
    worldToTexel(x, z, texelX, texelY);
    return holeMaskCpuData[texelY * resolution + texelX] > 127;
}

void TerrainHeightMap::setHole(float x, float z, bool hole) {
    int texelX, texelY;
    worldToTexel(x, z, texelX, texelY);
    holeMaskCpuData[texelY * resolution + texelX] = hole ? 255 : 0;
    holeMaskDirty = true;
}

void TerrainHeightMap::setHoleCircle(float centerX, float centerZ, float radius, bool hole) {
    // Convert radius to texel space
    float texelsPerUnit = (resolution - 1) / terrainSize;
    int texelRadius = static_cast<int>(std::ceil(radius * texelsPerUnit));

    int centerTexelX, centerTexelY;
    worldToTexel(centerX, centerZ, centerTexelX, centerTexelY);

    // Iterate over bounding box of circle
    for (int dy = -texelRadius; dy <= texelRadius; dy++) {
        for (int dx = -texelRadius; dx <= texelRadius; dx++) {
            int tx = centerTexelX + dx;
            int ty = centerTexelY + dy;

            // Check bounds
            if (tx < 0 || tx >= static_cast<int>(resolution) ||
                ty < 0 || ty >= static_cast<int>(resolution)) {
                continue;
            }

            // Check if within circle (in world space for accuracy)
            float worldX = (static_cast<float>(tx) / (resolution - 1) - 0.5f) * terrainSize;
            float worldZ = (static_cast<float>(ty) / (resolution - 1) - 0.5f) * terrainSize;
            float distSq = (worldX - centerX) * (worldX - centerX) +
                          (worldZ - centerZ) * (worldZ - centerZ);

            if (distSq <= radius * radius) {
                holeMaskCpuData[ty * resolution + tx] = hole ? 255 : 0;
            }
        }
    }
    holeMaskDirty = true;
}

void TerrainHeightMap::uploadHoleMaskToGPU() {
    if (holeMaskDirty) {
        uploadHoleMaskToGPUInternal();
        holeMaskDirty = false;
    }
}

float TerrainHeightMap::getHeightAt(float x, float z) const {
    // Check hole mask first
    if (isHole(x, z)) {
        return NO_GROUND;
    }

    // Convert world position to UV coordinates
    float u = (x / terrainSize) + 0.5f;
    float v = (z / terrainSize) + 0.5f;

    // Clamp to valid range
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    // Sample height map with bilinear interpolation
    float fx = u * (resolution - 1);
    float fy = v * (resolution - 1);

    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, static_cast<int>(resolution - 1));
    int y1 = std::min(y0 + 1, static_cast<int>(resolution - 1));

    float tx = fx - x0;
    float ty = fy - y0;

    float h00 = cpuData[y0 * resolution + x0];
    float h10 = cpuData[y0 * resolution + x1];
    float h01 = cpuData[y1 * resolution + x0];
    float h11 = cpuData[y1 * resolution + x1];

    float h0 = h00 * (1.0f - tx) + h10 * tx;
    float h1 = h01 * (1.0f - tx) + h11 * tx;
    float h = h0 * (1.0f - ty) + h1 * ty;

    // Use shared TerrainHeight function (see TerrainHeight.h for authoritative formula)
    return TerrainHeight::toWorld(h, heightScale);
}
