#include "TerrainHeightMap.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
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
            std::cerr << "Failed to load heightmap from file, falling back to procedural" << std::endl;
            if (!generateHeightData()) return false;
        }
    } else {
        if (!generateHeightData()) return false;
    }

    if (!createGPUResources()) return false;
    if (!uploadToGPU()) return false;

    std::cout << "TerrainHeightMap initialized: " << resolution << "x" << resolution << std::endl;
    return true;
}

void TerrainHeightMap::destroy(VkDevice device, VmaAllocator allocator) {
    if (sampler) vkDestroySampler(device, sampler, nullptr);
    if (imageView) vkDestroyImageView(device, imageView, nullptr);
    if (image) vmaDestroyImage(allocator, image, allocation);

    sampler = VK_NULL_HANDLE;
    imageView = VK_NULL_HANDLE;
    image = VK_NULL_HANDLE;
    allocation = VK_NULL_HANDLE;
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
        std::cout << "Loaded 16-bit heightmap: " << path << " (" << width << "x" << height << ")" << std::endl;

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

        std::cout << "Height scale: " << heightScale << "m (altitude range: " << minAlt << "m to " << maxAlt << "m)" << std::endl;

        return true;
    }

    // Fall back to 8-bit
    stbi_uc* data8 = stbi_load(path.c_str(), &width, &height, &channels, 1);
    if (data8) {
        std::cout << "Loaded 8-bit heightmap: " << path << " (" << width << "x" << height << ")" << std::endl;

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

        std::cout << "Height scale: " << heightScale << "m (altitude range: " << minAlt << "m to " << maxAlt << "m)" << std::endl;

        return true;
    }

    std::cerr << "Failed to load heightmap: " << path << " - " << stbi_failure_reason() << std::endl;
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
        std::cerr << "Failed to create height map image" << std::endl;
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
        std::cerr << "Failed to create height map image view" << std::endl;
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
        std::cerr << "Failed to create height map sampler" << std::endl;
        return false;
    }

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
        std::cerr << "Failed to create staging buffer for height map upload" << std::endl;
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

float TerrainHeightMap::getHeightAt(float x, float z) const {
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

    // Match shader: h * heightScale (0 = ground level)
    return h * heightScale;
}
